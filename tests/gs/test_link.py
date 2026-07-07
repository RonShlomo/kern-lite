import struct

import pytest

from groundstation.frame import (
    Frame, FrameType, NackCode, Decoder, DecodeResult, encode,
)


# ---------------------------------------------------------------------------
# Test vectors
# ---------------------------------------------------------------------------

# Day 2 stub STATUS payload: state=0, sd_mounted=1, file_count=4,
# current_file=0, total_records=0, wrap_count=0, records_in_file=0
STUB_STATUS_PAYLOAD = struct.pack("<BBBBIIH", 0, 1, 4, 0, 0, 0, 0)

# Captured from real board on COM9 during Phase 2 Member C validation.
BOARD_STATUS_HEX = (
    "ab120e00000104000000000000000000000056ec90f4cd"
)

# Captured from real board after sending unknown opcode 0xFF.
NACK_BADCOMMAND_HEX = (
    "ab210100025c1c06d6cd"
)
# Verified command vectors (CRCs hand-checked against zlib.crc32)
CMD_STATUS_HEX = "ab0300004b6707fdcd"
CMD_REPLAY_120_HEX = "ab0402007800a86a4d7ecd"
CMD_ERASE_MAGIC_HEX = "ab060400dec0addecfcdcb48cd"


# ---------------------------------------------------------------------------
# Helper
# ---------------------------------------------------------------------------

def feed_all(decoder: Decoder, data: bytes):
    """Feed bytes one at a time; return (last_result, frame_or_None)."""
    result = DecodeResult.NeedMore
    for b in data:
        result = decoder.feed(b)
        if result == DecodeResult.FrameReady:
            return result, decoder.frame()
    return result, None


# ---------------------------------------------------------------------------
# STATUS decode: all 14 fields
# ---------------------------------------------------------------------------

def test_status_stream_decodes_all_fields():
    result, frame = feed_all(Decoder(), bytes.fromhex(BOARD_STATUS_HEX))
    assert result == DecodeResult.FrameReady
    assert frame.type == FrameType.Status
    assert len(frame.payload) == 14

    (state, sd_mounted, file_count, current_file,
     total_records, wrap_count, records_in_file) = struct.unpack_from(
        "<BBBBIIH", frame.payload)

    assert state == 0
    assert sd_mounted == 1
    assert file_count == 4
    assert current_file == 0
    assert total_records == 0
    assert wrap_count == 0
    assert records_in_file == 0


# ---------------------------------------------------------------------------
# NACK decode
# ---------------------------------------------------------------------------

def test_nack_badcommand_decodes():
    result, frame = feed_all(Decoder(), bytes.fromhex(NACK_BADCOMMAND_HEX))
    assert result == DecodeResult.FrameReady
    assert frame.type == FrameType.Nack
    assert len(frame.payload) == 1
    assert NackCode(frame.payload[0]) == NackCode.BadCommand


# ---------------------------------------------------------------------------
# Verified command vectors round-trip through the decoder
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("hex_vector, expected_type, expected_payload", [
    (CMD_STATUS_HEX, FrameType.CmdStatus, b""),
    (CMD_REPLAY_120_HEX, FrameType.CmdReplay, struct.pack("<H", 120)),
    (CMD_ERASE_MAGIC_HEX, FrameType.CmdErase, struct.pack("<I", 0xDEADC0DE)),
])
def test_known_command_vectors_decode(hex_vector, expected_type, expected_payload):
    result, frame = feed_all(Decoder(), bytes.fromhex(hex_vector))
    assert result == DecodeResult.FrameReady
    assert frame.type == expected_type
    assert frame.payload == expected_payload


# ---------------------------------------------------------------------------
# Robustness around the vectors
# ---------------------------------------------------------------------------

def test_resync_garbage_before_status():
    garbage = bytes([0x00, 0x11, 0x22, 0xCD, 0xFF, 0x42])
    result, frame = feed_all(Decoder(), garbage + bytes.fromhex(BOARD_STATUS_HEX))
    assert result == DecodeResult.FrameReady
    assert frame.type == FrameType.Status


def test_corrupted_status_payload_is_crc_error():
    raw = bytearray(bytes.fromhex(BOARD_STATUS_HEX))
    raw[6] ^= 0xFF                     # flip a payload byte
    decoder = Decoder()
    results = [decoder.feed(b) for b in raw]
    assert DecodeResult.CrcError in results
    assert DecodeResult.FrameReady not in results


def test_erase_vector_contains_etx_byte_mid_frame():
    """Regression guard: the ERASE vector's CRC contains 0xCD; the decoder
    must count through CRC bytes rather than hunt for the ETX marker."""
    raw = bytes.fromhex(CMD_ERASE_MAGIC_HEX)
    assert 0xCD in raw[1:-1]           # the interesting property of this vector
    result, frame = feed_all(Decoder(), raw)
    assert result == DecodeResult.FrameReady
    assert frame.payload == struct.pack("<I", 0xDEADC0DE)


def test_back_to_back_frames_decode_cleanly():
    stream = (bytes.fromhex(BOARD_STATUS_HEX)
              + bytes.fromhex(NACK_BADCOMMAND_HEX))
    decoder = Decoder()
    frames = []
    for b in stream:
        if decoder.feed(b) == DecodeResult.FrameReady:
            frames.append(decoder.frame())
    assert [f.type for f in frames] == [FrameType.Status, FrameType.Nack]