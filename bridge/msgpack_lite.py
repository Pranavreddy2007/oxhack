"""Minimal MessagePack encoder/decoder (nil, bool, int, float, str, bin, array, map).

Pure Python, no dependency, so the sidecar needs no package installation
on the board.
"""

from __future__ import annotations

import struct


def packb(o) -> bytes:
    out = bytearray()
    _pack(o, out)
    return bytes(out)


def _pack(o, out: bytearray):
    if o is None:
        out.append(0xC0)
    elif o is True:
        out.append(0xC3)
    elif o is False:
        out.append(0xC2)
    elif isinstance(o, int):
        if 0 <= o < 0x80:
            out.append(o)
        elif -32 <= o < 0:
            out.append(o & 0xFF)
        elif 0 <= o <= 0xFFFFFFFF:
            out += b"\xce" + struct.pack(">I", o)
        elif 0 <= o:
            out += b"\xcf" + struct.pack(">Q", o)
        elif o >= -(1 << 31):
            out += b"\xd2" + struct.pack(">i", o)
        else:
            out += b"\xd3" + struct.pack(">q", o)
    elif isinstance(o, float):
        out += b"\xcb" + struct.pack(">d", o)
    elif isinstance(o, str):
        b = o.encode()
        n = len(b)
        if n < 32:
            out.append(0xA0 | n)
        elif n < 0x100:
            out += b"\xd9" + bytes([n])
        elif n < 0x10000:
            out += b"\xda" + struct.pack(">H", n)
        else:
            out += b"\xdb" + struct.pack(">I", n)
        out += b
    elif isinstance(o, (bytes, bytearray)):
        n = len(o)
        if n < 0x100:
            out += b"\xc4" + bytes([n])
        elif n < 0x10000:
            out += b"\xc5" + struct.pack(">H", n)
        else:
            out += b"\xc6" + struct.pack(">I", n)
        out += o
    elif isinstance(o, (list, tuple)):
        n = len(o)
        if n < 16:
            out.append(0x90 | n)
        elif n < 0x10000:
            out += b"\xdc" + struct.pack(">H", n)
        else:
            out += b"\xdd" + struct.pack(">I", n)
        for x in o:
            _pack(x, out)
    elif isinstance(o, dict):
        n = len(o)
        if n < 16:
            out.append(0x80 | n)
        else:
            out += b"\xde" + struct.pack(">H", n)
        for k, v in o.items():
            _pack(k, out)
            _pack(v, out)
    else:
        raise TypeError(f"cannot pack {type(o)}")


class NeedMore(Exception):
    pass


class Unpacker:
    """Streaming decoder: feed bytes, iterate complete objects."""

    def __init__(self):
        self.buf = bytearray()

    def feed(self, data: bytes):
        self.buf += data

    def __iter__(self):
        return self

    def __next__(self):
        try:
            obj, n = _unpack(self.buf, 0)
        except NeedMore:
            raise StopIteration
        del self.buf[:n]
        return obj


def unpackb(b: bytes):
    obj, n = _unpack(b, 0)
    return obj


def _need(b, i, n):
    if i + n > len(b):
        raise NeedMore


def _unpack(b, i):
    _need(b, i, 1)
    t = b[i]
    i += 1
    if t <= 0x7F:
        return t, i
    if t >= 0xE0:
        return t - 0x100, i
    if 0x80 <= t <= 0x8F:
        return _map(b, i, t & 0x0F)
    if 0x90 <= t <= 0x9F:
        return _arr(b, i, t & 0x0F)
    if 0xA0 <= t <= 0xBF:
        n = t & 0x1F
        _need(b, i, n)
        return bytes(b[i:i + n]).decode(), i + n
    fixed = {0xC0: None, 0xC2: False, 0xC3: True}
    if t in fixed:
        return fixed[t], i
    ints = {0xCC: ">B", 0xCD: ">H", 0xCE: ">I", 0xCF: ">Q", 0xD0: ">b", 0xD1: ">h", 0xD2: ">i", 0xD3: ">q",
            0xCA: ">f", 0xCB: ">d"}
    if t in ints:
        fmt = ints[t]
        n = struct.calcsize(fmt)
        _need(b, i, n)
        return struct.unpack(fmt, bytes(b[i:i + n]))[0], i + n
    lens = {0xC4: (">B", "bin"), 0xC5: (">H", "bin"), 0xC6: (">I", "bin"), 0xD9: (">B", "str"),
            0xDA: (">H", "str"), 0xDB: (">I", "str"), 0xDC: (">H", "arr"), 0xDD: (">I", "arr"),
            0xDE: (">H", "map"), 0xDF: (">I", "map")}
    if t in lens:
        fmt, kind = lens[t]
        k = struct.calcsize(fmt)
        _need(b, i, k)
        n = struct.unpack(fmt, bytes(b[i:i + k]))[0]
        i += k
        if kind == "arr":
            return _arr(b, i, n)
        if kind == "map":
            return _map(b, i, n)
        _need(b, i, n)
        raw = bytes(b[i:i + n])
        return (raw.decode() if kind == "str" else raw), i + n
    raise ValueError(f"unsupported msgpack type 0x{t:02x}")


def _arr(b, i, n):
    out = []
    for _ in range(n):
        v, i = _unpack(b, i)
        out.append(v)
    return out, i


def _map(b, i, n):
    out = {}
    for _ in range(n):
        k, i = _unpack(b, i)
        v, i = _unpack(b, i)
        out[k] = v
    return out, i
