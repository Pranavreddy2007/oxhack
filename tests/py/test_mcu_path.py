"""MCU path without hardware: a fake arduino-router (MessagePack-RPC on a
Unix socket) plays the MCU. It tests sidecar <-> daemon <-> sim_stream:
- MCU sensor frames reach the daemon; the knob frame reaches the laptop.
- The sidecar sends ec_status to the "MCU" and measures Bridge pings.
- MCU silent -> the daemon prints SENSOR_LOST (C10).
- Linux silent (daemon stopped) -> the sidecar stops ec_status, so the MCU
  watchdog shows FAULT (C10).
The real router behaviour is NOT tested here (docs/HARDWARE.md Q7).
"""

import asyncio
import os
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

import pytest
import shutil
import tempfile

from bridge.msgpack_lite import Unpacker, packb

ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get("EMBERCAST_BUILD", ROOT / "build"))
DAEMON = BUILD / "apps" / "embercast_board_daemon"
STREAM = BUILD / "apps" / "embercast_sim_stream"
pytestmark = pytest.mark.skipif(not DAEMON.exists(), reason="build the C++ apps first")


class FakeRouter:
    def __init__(self, path):
        self.path = path
        self.status = []          # (time, state, top)
        self.registered = []
        self.sending = True
        self.knob = 0.5
        self.loop = asyncio.new_event_loop()
        self.thread = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self.thread.start()
        for _ in range(100):
            if os.path.exists(self.path):
                return
            time.sleep(0.02)

    def _run(self):
        asyncio.set_event_loop(self.loop)
        self.loop.run_until_complete(self._serve())

    async def _serve(self):
        server = await asyncio.start_unix_server(self._client, self.path)
        async with server:
            await server.serve_forever()

    async def _client(self, reader, writer):
        unp = Unpacker()

        async def sender():
            seq = 0
            while True:
                await asyncio.sleep(0.05)
                if self.sending:
                    seq += 1
                    writer.write(packb([2, "ec_sensor", [seq, 6.0, self.knob, 0.3, 21.0, -2147483648]]))
                    await writer.drain()

        task = asyncio.create_task(sender())
        try:
            while True:
                data = await reader.read(4096)
                if not data:
                    break
                unp.feed(data)
                for msg in unp:
                    if msg[0] == 0:
                        _, mid, method, params = msg
                        if method == "$/register":
                            self.registered.append(params[0])
                            res = True
                        elif method == "ec_status":
                            self.status.append((time.monotonic(), params[0], params[1]))
                            res = None
                        elif method == "ec_ping":
                            res = params[0]
                        else:
                            res = None
                        writer.write(packb([1, mid, None, res]))
                        await writer.drain()
        finally:
            task.cancel()


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


@pytest.fixture
def sock_dir():
    # macOS limits AF_UNIX paths to 104 bytes; pytest tmp_path is longer.
    d = tempfile.mkdtemp(prefix="ec", dir="/tmp")
    yield Path(d)
    shutil.rmtree(d, ignore_errors=True)


def test_mcu_path_end_to_end(tmp_path, sock_dir):
    router = FakeRouter(str(sock_dir / "router.sock"))
    router.start()
    port = free_port()
    mcu_sock = str(sock_dir / "mcu.sock")
    daemon = subprocess.Popen([str(DAEMON), "--port", str(port), "--N", "64", "--ticks", "400", "--threads", "2",
                               "--mcu-uds", mcu_sock, "--sensor-source", "mcu", "--mcu-timeout-ms", "500"],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(0.3)
    ping_out = tmp_path / "ping.csv"
    sidecar = subprocess.Popen([sys.executable, str(ROOT / "bridge" / "mcu_sidecar.py"), "--router",
                                f"unix://{router.path}", "--daemon-uds", mcu_sock, "--ping-period", "0.05",
                                "--ping-out", str(ping_out), "--daemon-timeout", "0.5", "--status-period", "0.1"],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    stream = subprocess.Popen([str(STREAM), "--board", f"127.0.0.1:{port}", "--N", "64", "--ticks", "400",
                               "--rate", "20", "--dash-port", "0", "--threads", "2", "--episodes", "1"],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        time.sleep(2.0)
        router.knob = 2.0                      # turn the knob
        time.sleep(1.5)
        router.sending = False                 # MCU silent
        time.sleep(1.5)
        n_status_before = len(router.status)
        assert n_status_before > 3
        daemon.terminate()                     # Linux engine silent
        daemon.wait(timeout=10)
        time.sleep(0.6)                        # longer than --daemon-timeout
        n_mid = len(router.status)
        time.sleep(1.0)
        n_after = len(router.status)
    finally:
        stream.terminate()
        sidecar.terminate()
        out_stream, _ = stream.communicate(timeout=10)
        out_side, _ = sidecar.communicate(timeout=10)
        if daemon.poll() is None:
            daemon.terminate()
        out_daemon, _ = daemon.communicate(timeout=10)
    assert "ec_sensor" in router.registered
    assert "MCU sidecar connected" in out_daemon
    assert "MCU sensor frames OK" in out_daemon
    assert "SENSOR_LOST" in out_daemon
    assert "knob wind shift" in out_stream
    assert n_after == n_mid, "sidecar must stop ec_status when the daemon is silent"
    assert ping_out.exists() and len(ping_out.read_text().splitlines()) > 5
