import math
import struct
import zlib

from hypothesis import given
from hypothesis import strategies as st

from bridge import frames
from bridge.mcu_sidecar import valid_sensor
from bridge.msgpack_lite import Unpacker, packb, unpackb

scalars = st.one_of(st.none(), st.booleans(), st.integers(-(2**63), 2**64 - 1),
                    st.floats(allow_nan=False), st.text(max_size=300), st.binary(max_size=300))
values = st.recursive(scalars, lambda c: st.lists(c, max_size=20) | st.dictionaries(st.text(max_size=10), c, max_size=20),
                      max_leaves=40)


@given(values)
def test_msgpack_round_trip(v):
    assert unpackb(packb(v)) == v


def test_msgpack_known_bytes_and_streaming():
    assert packb([0, 1, "ec_ping", [5]]) == bytes([0x94, 0x00, 0x01, 0xA7]) + b"ec_ping" + bytes([0x91, 0x05])
    u = Unpacker()
    data = packb([2, "ec_sensor", [1, 2.5, 0.1, 0.3, 20.0, 0]]) + packb([1, 7, None, True])
    u.feed(data[:5])
    assert list(u) == []
    u.feed(data[5:])
    assert list(u) == [[2, "ec_sensor", [1, 2.5, 0.1, 0.3, 20.0, 0]], [1, 7, None, True]]


def test_frame_layout_matches_cpp_header():
    f = frames.sensor(7, 33, 123, 4.5, -1.25, 0.31, 22.0, frames.FLAG_KNOB)
    assert len(f) == 32 + 20
    magic, ver, ftype, seq, tick, tsend, plen, crc = frames.HEADER.unpack_from(f)
    assert (magic, ver, ftype, seq, tick, tsend, plen) == (0x45434D42, 1, 1, 7, 33, 123, 20)
    hdr = bytearray(f[:32])
    hdr[28:32] = b"\0\0\0\0"
    assert zlib.crc32(bytes(hdr) + f[32:]) & 0xFFFFFFFF == crc
    assert struct.unpack("<ffffI", f[32:])[4] == frames.FLAG_KNOB


def test_frame_reader_drops_corrupt_and_resyncs():
    a = frames.heartbeat(1, 1, 0, 0)
    b = bytearray(frames.heartbeat(2, 2, 0, 1))
    b[-1] ^= 1
    c = frames.heartbeat(3, 3, 0, 3)
    r = frames.Reader()
    out = r.feed(b"\x01\x02" + a + bytes(b) + c)
    assert [o[1] for o in out] == [1, 3]
    assert r.bad_crc == 1 and r.bad_header >= 1


def test_sensor_validation_trust_boundary():
    assert valid_sensor([1, 5.0, 0.2, 0.3, 20.0, 0])
    assert not valid_sensor([1, 5.0, 0.2, 0.3, 20.0])           # wrong arity
    assert not valid_sensor([1, "5", 0.2, 0.3, 20.0, 0])        # wrong type
    assert not valid_sensor([1, math.nan, 0.2, 0.3, 20.0, 0])   # not finite
    assert not valid_sensor([1, 500.0, 0.2, 0.3, 20.0, 0])      # out of range
    assert not valid_sensor("drop table")
