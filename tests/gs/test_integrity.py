import pytest
import struct
import zlib
from groundstation.integrity import IntegrityChecker
from groundstation.frame import Frame, FrameType

def create_dummy_record_payload(seq_num: int) -> bytes:
    """
    Helper function to generate a fake 32-byte payload that perfectly mimics
    what the C++ board would send for a SensorRecord.
    """
    # Create 28 bytes of zeros, but insert our specific sequence number
    # The sequence number is at byte offset 6 (timestamp=4 bytes, ms=2 bytes, seq=2 bytes)
    # This struct.pack packs: 4 bytes (I), 2 bytes (H), 2 bytes (H), and 20 zeros
    dummy_data = struct.pack("<I H H 20x", 12345, 500, seq_num)

    # Calculate what the CRC should be for these 28 bytes
    calculated_crc = zlib.crc32(dummy_data) & 0xFFFFFFFF

    # Append the 4-byte CRC to the end to make it exactly 32 bytes
    final_payload = dummy_data + struct.pack("<I", calculated_crc)
    return final_payload


def test_valid_frame_no_warning():
    """
    TEST 1: A valid frame with a perfect CRC should return True.
    """
    checker = IntegrityChecker()
    payload = create_dummy_record_payload(seq_num=1)

    # Create a fake Frame object as if it just came out of the Decoder
    frame = Frame(type=FrameType.Record, payload=payload)

    # Assert checks if the statement is True. If it's False, the test fails.
    assert checker.check_frame(frame) == True


def test_corrupted_frame_storage_warning():
    """
    TEST 2: A frame where we flip byte 10 should fail the CRC check.
    """
    checker = IntegrityChecker()
    payload = create_dummy_record_payload(seq_num=1)

    # Strings of bytes in Python are immutable (can't be changed).
    # We convert it to a bytearray so we can intentionally corrupt it.
    mutable_payload = bytearray(payload)

    # Corrupt byte 10 (change its value arbitrarily)
    mutable_payload[10] = (mutable_payload[10] + 1) % 256

    frame = Frame(type=FrameType.Record, payload=bytes(mutable_payload))

    # It should return False because the calculated CRC won't match the original CRC
    assert checker.check_frame(frame) == False


def test_sequence_gap_detection():
    """
    TEST 3: Simulating a gap. If we received seq 1, 2, 3, and then 7,
    it should log a gap of 3.
    """
    checker = IntegrityChecker()

    # Normal sequential flow (no gaps expected, returns 0)
    assert checker.check_sequence(current_seq=1, last_seq=None) == 0
    assert checker.check_sequence(current_seq=2, last_seq=1) == 0
    assert checker.check_sequence(current_seq=3, last_seq=2) == 0

    # GAP INTRODUCED: The next packet is 7 instead of 4.
    # Gap calculation: (7 - 4) = 3
    gap = checker.check_sequence(current_seq=7, last_seq=3)
    assert gap == 3