"""EmberCast wire frames in Python (same layout as engine/include/embercast/frame.hpp)."""

from __future__ import annotations

import struct
import zlib

MAGIC = 0x45434D42
VERSION = 1
HEADER = struct.Struct("<IHHIIQII")  # magic, version, type, seq, tick, t_send_ns, payload_len, crc32
SENSOR, PERIMETER, DECISION, HEARTBEAT, FORECAST = 1, 2, 3, 4, 5
FLAG_KNOB = 0x80000000
STATES = ("OK", "STALE", "DEGRADED", "FAULT")


def encode(ftype: int, seq: int, tick: int, t_send_ns: int, payload: bytes) -> bytes:
    hdr = HEADER.pack(MAGIC, VERSION, ftype, seq & 0xFFFFFFFF, tick & 0xFFFFFFFF, t_send_ns, len(payload), 0)
    crc = zlib.crc32(hdr + payload) & 0xFFFFFFFF
    return hdr[:28] + struct.pack("<I", crc) + payload


def sensor(seq, tick, t_send_ns, ws, wd, h, temp, flags) -> bytes:
    return encode(SENSOR, seq, tick, t_send_ns, struct.pack("<ffffI", ws, wd, h, temp, flags & 0xFFFFFFFF))


def heartbeat(seq, tick, t_send_ns, state) -> bytes:
    return encode(HEARTBEAT, seq, tick, t_send_ns, struct.pack("<I", state))


class Reader:
    """Cuts a byte stream into checked frames. Drops bad frames and counts them."""

    def __init__(self):
        self.buf = bytearray()
        self.bad_crc = 0
        self.bad_header = 0

    def feed(self, data: bytes):
        self.buf += data
        out = []
        pos = 0
        while len(self.buf) - pos >= HEADER.size:
            magic, ver, ftype, seq, tick, tsend, plen, crc = HEADER.unpack_from(self.buf, pos)
            if magic != MAGIC or ver != VERSION or plen > 16 + 128 * 128 or not 1 <= ftype <= 5:
                self.bad_header += 1
                pos += 1
                continue
            end = pos + HEADER.size + plen
            if end > len(self.buf):
                break
            hdr = bytearray(self.buf[pos:pos + HEADER.size])
            hdr[28:32] = b"\0\0\0\0"
            payload = bytes(self.buf[pos + HEADER.size:end])
            if zlib.crc32(bytes(hdr) + payload) & 0xFFFFFFFF == crc:
                out.append((ftype, seq, tick, tsend, payload))
            else:
                self.bad_crc += 1
            pos = end
        del self.buf[:pos]
        return out
