"""EmberCast dashboard server.

Simulation only. Not for operational use.

- Reads JSON lines from embercast_sim_stream (TCP, default 127.0.0.1:7072).
- Sends them to browsers over WebSocket (default port 8001).
- Sends browser controls (wind_shift, cut_link, restore_link, reset) back
  to sim_stream as text lines.
- Serves the static page and the results/ folder over HTTP (default port 8000).

The dashboard is visualisation only. It is not part of any measurement.
"""

from __future__ import annotations

import argparse
import asyncio
import functools
import http.server
import json
import threading
from pathlib import Path

import websockets
from websockets.asyncio.server import serve

ROOT = Path(__file__).resolve().parents[2]
STATIC = Path(__file__).resolve().parent / "static"


class Hub:
    def __init__(self):
        self.clients: set = set()
        self.init_msg: str | None = None
        self.writer: asyncio.StreamWriter | None = None

    async def broadcast(self, line: str):
        dead = []
        for ws in self.clients:
            try:
                await ws.send(line)
            except websockets.ConnectionClosed:
                dead.append(ws)
        for ws in dead:
            self.clients.discard(ws)


async def stream_reader(hub: Hub, host: str, port: int):
    while True:
        try:
            reader, writer = await asyncio.open_connection(host, port, limit=1 << 22)
            hub.writer = writer
            print(f"[dashboard] connected to sim_stream {host}:{port}", flush=True)
            while True:
                line = await reader.readline()
                if not line:
                    break
                text = line.decode()
                if text.startswith('{"type":"init"'):
                    hub.init_msg = text
                await hub.broadcast(text)
        except OSError:
            pass
        hub.writer = None
        await hub.broadcast(json.dumps({"type": "stream_down"}))
        await asyncio.sleep(1.0)


ALLOWED = {"wind_shift", "cut_link", "restore_link", "reset"}


async def ws_handler(hub: Hub, ws):
    hub.clients.add(ws)
    if hub.init_msg:
        await ws.send(hub.init_msg)
    try:
        async for msg in ws:
            cmd = str(msg).strip()
            if cmd in ALLOWED and hub.writer is not None:
                hub.writer.write((cmd + "\n").encode())
                await hub.writer.drain()
    except websockets.ConnectionClosed:
        pass
    finally:
        hub.clients.discard(ws)


class Handler(http.server.SimpleHTTPRequestHandler):
    def translate_path(self, path):
        path = path.split("?", 1)[0]
        if path.startswith("/results/"):
            return str(ROOT / path.lstrip("/"))
        return str(STATIC / (path.lstrip("/") or "index.html"))

    def log_message(self, *args):
        pass


def start_http(port: int):
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv


async def main_async(args):
    hub = Hub()
    start_http(args.http_port)
    print(f"[dashboard] open http://127.0.0.1:{args.http_port}/  (Simulation only. Not for operational use.)",
          flush=True)
    async with serve(functools.partial(ws_handler, hub), "127.0.0.1", args.ws_port, max_size=1 << 22):
        await stream_reader(hub, args.stream_host, args.stream_port)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--stream-host", default="127.0.0.1")
    ap.add_argument("--stream-port", type=int, default=7072)
    ap.add_argument("--http-port", type=int, default=8000)
    ap.add_argument("--ws-port", type=int, default=8001)
    args = ap.parse_args(argv)
    try:
        asyncio.run(main_async(args))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
