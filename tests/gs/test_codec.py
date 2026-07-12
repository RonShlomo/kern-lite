"""Day 1 codec tests: CRC KAT, encode/decode round-trips, corruption,
resync, and cross-implementation vector."""

import struct
import pytest

from crc import crc32
from frame import (
    Frame, FrameType, encode, Decoder, DecodeResult,
    STX, ETX, MAX_PAYLOAD, FRAME_OVERHEAD,
)


def decode_all(decoder: Decoder, data: bytes) -> DecodeResult:
    """Feed every byte; return the result of the final byte."""
    result = DecodeResult.NeedMore
    for b in data:
        result = decoder.feed(b)
    return result


# ---- 1. CRC known-answer test (the Day 1 blocker) ----
def test_crc_kat():
    assert crc32(b"123456789") == 0xCBF43926


# ---- 2. Round-trip: CMD_STATUS, no payload ----
def test_roundtrip_cmd_status_empty():
    frame = Frame(type=FrameType.CmdStatus)
    wire = encode(frame)
    assert len(wire) == FRAME_OVERHEAD  # 9 bytes, empty payload

    d = Decoder()
    result = decode_all(d, wire)
    assert result == DecodeResult.FrameReady
    assert d.frame().type == FrameType.CmdStatus
    assert d.frame().payload == b""


# ---- 2b. Round-trip: RECORD with a 32-byte payload ----
def test_roundtrip_record_32():
    payload = bytes(range(32))  # 0x00, 0x01, ... 0x1F
    frame = Frame(type=FrameType.Record, payload=payload)
    wire = encode(frame)
    assert len(wire) == FRAME_OVERHEAD + 32

    d = Decoder()
    result = decode_all(d, wire)
    assert result == DecodeResult.FrameReady
    assert d.frame().type == FrameType.Record
    assert d.frame().payload == payload


# ---- 2c. Round-trip: maximum 256-byte payload ----
def test_roundtrip_max_payload():
    payload = bytes([0xAA]) * MAX_PAYLOAD
    wire = encode(Frame(type=FrameType.Record, payload=payload))
    d = Decoder()
    assert decode_all(d, wire) == DecodeResult.FrameReady
    assert d.frame().payload == payload


# ---- 3. Corruption: flip a payload byte -> CrcError ----
def test_corruption_detected():
    payload = bytes(range(32))
    wire = bytearray(encode(Frame(type=FrameType.Record, payload=payload)))
    # Flip a bit in a payload byte (index 5 = STX + TYPE + 2 LEN + payload[1])
    wire[5] ^= 0xFF
    d = Decoder()
    assert decode_all(d, bytes(wire)) == DecodeResult.CrcError


# ---- 4. Resync: garbage before a valid frame ----
def test_resync_after_garbage():
    garbage = bytes([0x00, 0xFF, 0x12, 0x34, 0xAB, 0x99, 0x55] * 2)
    good = encode(Frame(type=FrameType.Ack))
    d = Decoder()
    result = decode_all(d, garbage + good)
    assert result == DecodeResult.NeedMore
    assert d.frame().type == FrameType.Ack


# ---- 5. Oversized LEN -> SyncError (buffer-overflow guard) ----
def test_oversized_len_sync_error():
    # STX, TYPE=RECORD, LEN=511 (0xFF 0x01), then a byte
    data = bytes([STX, 0x10, 0xFF, 0x01, 0x00])
    d = Decoder()
    result = DecodeResult.NeedMore
    for b in data:
        result = d.feed(b)
        if result == DecodeResult.SyncError:
            break
    assert result == DecodeResult.SyncError


# ---- 6. Cross-implementation vector ----
# These exact bytes are an ACK frame (type 0x20, no payload).
# When integrating, your partner's firmware encode() for an ACK must
# produce this identical byte sequence.
FIRMWARE_ACK_BYTES = bytes([0xAB, 0x20, 0x00, 0x00, 0xF2, 0x9F, 0x0C, 0xC7, 0xCD])

def test_cross_vector_decode_firmware_ack():
    d = Decoder()
    result = decode_all(d, FIRMWARE_ACK_BYTES)
    assert result == DecodeResult.FrameReady
    assert d.frame().type == FrameType.Ack
    assert d.frame().payload == b""

def test_cross_vector_python_encode_matches():
    # Your Python must produce the same bytes the firmware is expected to.
    assert encode(Frame(type=FrameType.Ack)) == FIRMWARE_ACK_BYTES