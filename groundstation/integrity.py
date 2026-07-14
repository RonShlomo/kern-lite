import struct
import zlib
import logging

# Set up logging for integrity alerts
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("IntegrityChecker")

class IntegrityChecker:
    def __init__(self):
        # Initializes the integrity checker
        pass

    def check_frame(self, frame) -> bool:
        """
        Validates the inner record-level CRC for RECORD frames.
        Returns True if valid, False if corrupted.
        """
        # Ensure we are only checking RECORD frames (type 0x11)
        # Payload size must be exactly 32 bytes as defined in the handbook
        if not frame.payload or len(frame.payload) != 32:
            return False

        # Extract the first 28 bytes (the actual data fields)
        data_to_check = frame.payload[0:28]

        # Extract the original CRC stored in the last 4 bytes (bytes 28-32)
        # '<I' means Little-Endian Unsigned Integer (4 bytes / 32 bits)
        # struct.unpack_from returns a tuple, so we take the first element [0]
        original_crc = struct.unpack_from("<I", frame.payload, 28)[0]

        # Calculate the CRC on our side using Python's zlib
        calculated_crc = zlib.crc32(data_to_check) & 0xFFFFFFFF

        # Compare and report
        if calculated_crc != original_crc:
            logger.error("STORAGE_CORRUPTION_WARNING. Inner record CRC mismatch.")
            return False

        return True

    def check_sequence(self, current_seq: int, last_seq: int) -> int:
        """
        Checks for gaps in the sequence numbers.
        Returns the gap size (0 means perfect sequence, >0 means a gap).
        """
        # If this is the first packet received, there is no previous sequence to compare to
        if last_seq == None or last_seq is None:
            return 0;

        # Calculate what the next sequence number should be
        expected_seq = (last_seq + 1) % 65536

        # If it matches, there is no gap
        if current_seq == expected_seq:
            return 0

        # If it doesn't match, calculate how many packets we missed.
        # Handles normal gap, and also the wrap around gap (e.g., last=65535, current=1 -> gap=1 missed: 0)
        gap_size = (current_seq - expected_seq) % 65536

        logger.warning(f"SEQ_GAP size={gap_size}. Expected {expected_seq}, but got {current_seq}")
        return gap_size
