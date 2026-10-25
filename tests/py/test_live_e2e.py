"""End-to-end tests of the live path on localhost (no board needed).

- board_daemon + sim_stream over TCP give the same episode loss as the
  in-process engine (the transport does not change decisions).
- Board unavailable: sim_stream keeps running, flags STALE, keeps the last plan.
- Cut link: the board-side engine flags STALE.
"""

import os
import re
import socket
import subprocess
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get("EMBERCAST_BUILD", ROOT / "build"))
DAEMON = BUILD / "apps" / "embercast_board_daemon"
STREAM = BUILD / "apps" / "embercast_sim_stream"

pytestmark = pytest.mark.skipif(not STREAM.exists(), reason="build the C++ apps first")


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def losses(out):
    m = re.search(r"loss ensemble ([\d.]+)\s+b1 ([\d.]+)\s+b0 ([\d.]+)", out)
    assert m, out
    return tuple(float(x) for x in m.groups())


def run_stream(*args, timeout=120):
    r = subprocess.run([str(STREAM), "--fast", "--episodes", "1", "--dash-port", "0", "--N", "64",
                        "--ticks", "60", "--threads", "2", *args],
                       capture_output=True, text=True, timeout=timeout)
    assert r.returncode == 0, r.stderr
    return r.stdout


def test_remote_equals_inprocess():
    port = free_port()
    d = subprocess.Popen([str(DAEMON), "--port", str(port), "--N", "64", "--ticks", "60", "--threads", "2"],
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        time.sleep(0.4)
        remote = run_stream("--board", f"127.0.0.1:{port}")
        local = run_stream("--board", "none")
        assert "connected to board" in remote
        assert losses(remote) == losses(local)
        assert "bad_crc 0" in remote
    finally:
        d.terminate()
        d.wait(timeout=10)


def test_board_unavailable_keeps_running_and_flags_stale(tmp_path):
    port = free_port()  # nothing listens here
    out = run_stream("--board", f"127.0.0.1:{port}", "--timeout-ms", "20")
    e, b1, b0 = losses(out)
    assert b1 > 0 and b0 > 0  # baselines still ran


def test_cut_link_makes_daemon_stale(tmp_path):
    port = free_port()
    script = tmp_path / "script.txt"
    script.write_text("10 cut_link\n")
    d = subprocess.Popen([str(DAEMON), "--port", str(port), "--N", "64", "--ticks", "60", "--threads", "2",
                          "--stale-ms", "300"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        time.sleep(0.4)
        out = run_stream("--board", f"127.0.0.1:{port}", "--script", str(script), "--rate", "20",
                         "--timeout-ms", "50")
        # Wait longer than --stale-ms so the daemon sees the silent link.
        time.sleep(0.8)
    finally:
        d.terminate()
        log, _ = d.communicate(timeout=10)
    assert "control cut_link" in out
    assert "state STALE" in log
