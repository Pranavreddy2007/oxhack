"""EmberCast MCU sidecar (runs on the UNO Q Linux side). Off the hot path.

Simulation only. Not for operational use.

MCU --Bridge (arduino-router, MessagePack-RPC)--> sidecar --UDS frames--> board_daemon
board_daemon --UDS (HEARTBEAT, DECISION)--> sidecar --Bridge "ec_status"--> MCU

The router socket is a trust boundary. The sidecar accepts only the
"ec_sensor" method from it, and checks the type and range of each value.

NOT VERIFIED ON HARDWARE (docs/HARDWARE.md Q7):
- How a Linux client registers a method with arduino-router. The default
  here is a request "$/register" with params [method]. Check it on the
  board and change --register-method if needed.
- Whether a notification to "ec_status" reaches a method that the MCU
  registered with Bridge.provide_safe. The sidecar sends a request (call)
  by default; use --status-notify to send a notification.
"""

from __future__ import annotations

import argparse
import asyncio
import math
import os
import signal
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from bridge import frames  # noqa: E402
from bridge.msgpack_lite import Unpacker, packb  # noqa: E402

REQUEST, RESPONSE, NOTIFY = 0, 1, 2


def parse_unix(url: str) -> str:
    return url[len("unix://"):] if url.startswith("unix://") else url


def valid_sensor(params) -> bool:
    if not isinstance(params, list) or len(params) != 6:
        return False
    seq, ws, wd, h, t, flags = params
    if not all(isinstance(x, (int, float)) and not isinstance(x, bool) for x in params):
        return False
    vals = [float(ws), float(wd), float(h), float(t)]
    return all(math.isfinite(v) for v in vals) and 0 <= ws <= 60 and -10 <= wd <= 10 and \
        -0.5 <= h <= 1.5 and -50 <= t <= 80 and isinstance(seq, int) and isinstance(flags, int)


class Sidecar:
    def __init__(self, args):
        self.args = args
        self.msgid = 1
        self.pending: dict[int, asyncio.Future] = {}
        self.router_w: asyncio.StreamWriter | None = None
        self.daemon_w: asyncio.StreamWriter | None = None
        self.state = 3  # FAULT until the daemon talks
        self.top_risk = 255
        self.daemon_tick = 0
        self.last_daemon = 0.0
        self.seq = 0
        self.rejected = 0
        self.ping_samples: list[int] = []

    # ---- router (MessagePack-RPC) ----
    async def call(self, method, params, timeout=1.0):
        mid = self.msgid
        self.msgid += 1
        fut = asyncio.get_running_loop().create_future()
        self.pending[mid] = fut
        self.router_w.write(packb([REQUEST, mid, method, params]))
        await self.router_w.drain()
        try:
            return await asyncio.wait_for(fut, timeout)
        finally:
            self.pending.pop(mid, None)

    async def notify(self, method, params):
        self.router_w.write(packb([NOTIFY, method, params]))
        await self.router_w.drain()

    async def router_loop(self):
        path = parse_unix(self.args.router)
        reader, self.router_w = await asyncio.open_unix_connection(path)
        print(f"[sidecar] connected to router {path}", flush=True)
        unp = Unpacker()
        reg = asyncio.create_task(self.call(self.args.register_method, ["ec_sensor"], timeout=2.0))
        while True:
            data = await reader.read(4096)
            if not data:
                raise ConnectionError("router closed")
            unp.feed(data)
            for msg in unp:
                await self.on_router_msg(msg)
            if reg.done():
                if reg.exception():
                    print(f"[sidecar] register failed: {reg.exception()!r}", flush=True)
                reg = asyncio.create_task(asyncio.sleep(1e9))

    async def on_router_msg(self, msg):
        if not isinstance(msg, list) or not msg:
            self.rejected += 1
            return
        if msg[0] == RESPONSE and len(msg) == 4:
            fut = self.pending.get(msg[1])
            if fut and not fut.done():
                if msg[2] is not None:
                    fut.set_exception(RuntimeError(str(msg[2])))
                else:
                    fut.set_result(msg[3])
            return
        if msg[0] == NOTIFY and len(msg) == 3:
            method, params, mid = msg[1], msg[2], None
        elif msg[0] == REQUEST and len(msg) == 4:
            mid, method, params = msg[1], msg[2], msg[3]
        else:
            self.rejected += 1
            return
        ok = method == "ec_sensor" and valid_sensor(params)
        if mid is not None:
            self.router_w.write(packb([RESPONSE, mid, None if ok else "rejected", ok]))
        if not ok:
            self.rejected += 1
            return
        _, ws, wd, h, t, flags = params
        if self.daemon_w is not None:
            self.seq += 1
            self.daemon_w.write(frames.sensor(self.seq, self.daemon_tick, time.monotonic_ns(),
                                              float(ws), float(wd), float(h), float(t), int(flags)))

    # ---- board_daemon (UDS frames) ----
    async def daemon_loop(self):
        while True:
            try:
                reader, self.daemon_w = await asyncio.open_unix_connection(self.args.daemon_uds)
                print(f"[sidecar] connected to daemon {self.args.daemon_uds}", flush=True)
                rd = frames.Reader()
                while True:
                    data = await reader.read(65536)
                    if not data:
                        break
                    for ftype, seq, tick, tsend, payload in rd.feed(data):
                        self.last_daemon = time.monotonic()
                        self.daemon_tick = tick
                        if ftype == frames.HEARTBEAT and len(payload) >= 4:
                            self.state = int.from_bytes(payload[:4], "little")
                        elif ftype == frames.DECISION and len(payload) >= 28:
                            # state u32, eloss f32, cvar f32, moved u32, n u8, crews[8], top_risk[3]
                            self.top_risk = payload[25]
            except OSError:
                pass
            self.daemon_w = None
            await asyncio.sleep(0.5)

    async def status_loop(self):
        while True:
            await asyncio.sleep(self.args.status_period)
            if self.router_w is None:
                continue
            # Daemon silent -> send nothing; the MCU watchdog then shows FAULT.
            if time.monotonic() - self.last_daemon > self.args.daemon_timeout:
                continue
            try:
                if self.args.status_notify:
                    await self.notify("ec_status", [self.state, self.top_risk])
                else:
                    await self.call("ec_status", [self.state, self.top_risk], timeout=0.5)
            except (asyncio.TimeoutError, RuntimeError) as e:
                print(f"[sidecar] ec_status failed: {e!r}", flush=True)

    async def ping_loop(self):
        """Bridge round-trip latency (Linux -> MCU -> Linux), reported separately."""
        n = 0
        while self.args.ping_period > 0:
            await asyncio.sleep(self.args.ping_period)
            if self.router_w is None:
                continue
            n += 1
            t0 = time.monotonic_ns()
            try:
                r = await self.call("ec_ping", [n], timeout=1.0)
                if r == n:
                    self.ping_samples.append(time.monotonic_ns() - t0)
            except (asyncio.TimeoutError, RuntimeError):
                pass

    def write_pings(self):
        if not self.args.ping_out or not self.ping_samples:
            return
        p = Path(self.args.ping_out)
        p.parent.mkdir(parents=True, exist_ok=True)
        with open(p, "w") as f:
            f.write("label,stage,ns\n")
            for v in self.ping_samples:
                f.write(f"bridge,ping_round_trip,{v}\n")
        print(f"[sidecar] wrote {len(self.ping_samples)} Bridge pings to {p}", flush=True)

    async def run(self):
        tasks = [asyncio.create_task(self.daemon_loop()), asyncio.create_task(self.status_loop()),
                 asyncio.create_task(self.ping_loop())]
        try:
            while True:
                try:
                    await self.router_loop()
                except (OSError, ConnectionError) as e:
                    print(f"[sidecar] router: {e!r}; retry", flush=True)
                    self.router_w = None
                    await asyncio.sleep(1.0)
        finally:
            for t in tasks:
                t.cancel()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--router", default=os.environ.get("EMBERCAST_ROUTER", "unix:///var/run/arduino-router.sock"))
    ap.add_argument("--daemon-uds", default="/tmp/embercast_mcu.sock")
    ap.add_argument("--register-method", default="$/register")
    ap.add_argument("--status-notify", action="store_true")
    ap.add_argument("--status-period", type=float, default=0.5)
    ap.add_argument("--daemon-timeout", type=float, default=2.0)
    ap.add_argument("--ping-period", type=float, default=0.2)
    ap.add_argument("--ping-out", default="results/raw/board/bridge_ping.csv")
    ap.add_argument("--duration", type=float, default=0, help="stop after N seconds (0 = run forever)")
    args = ap.parse_args(argv)
    sc = Sidecar(args)

    async def go():
        if args.duration > 0:
            try:
                await asyncio.wait_for(sc.run(), args.duration)
            except asyncio.TimeoutError:
                pass
        else:
            await sc.run()

    def on_term(*_):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, on_term)
    try:
        asyncio.run(go())
    except KeyboardInterrupt:
        pass
    finally:
        sc.write_pings()


if __name__ == "__main__":
    main()
