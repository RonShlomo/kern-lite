
from dataclasses import dataclass
import struct


@dataclass
class FileRingEntry:
    index: int
    is_current: bool
    is_wrapped: bool
    record_count: int


class StorageModel:
    def __init__(self):
        self.state = 0
        self.sd_mounted = False
        self.file_count = 0
        self.current_file = 0
        self.total_records = 0
        self.wrap_count = 0
        self.records_in_file = 0

        self.live_record_count = 0
        self.replay_record_count = 0

    def update_from_status(self, frame):
        payload = frame.payload

        if len(payload) != 14:
            raise ValueError(f"STATUS payload must be 14 bytes, got {len(payload)}")

        (
            self.state,
            sd_mounted,
            self.file_count,
            self.current_file,
            self.total_records,
            self.wrap_count,
            self.records_in_file,
        ) = struct.unpack("<BBBBIIH", payload)

        self.sd_mounted = bool(sd_mounted)

    def ring_visual(self):
        entries = []

        if self.file_count <= 0:
            return entries

        for index in range(self.file_count):
            is_current = index == self.current_file
            is_wrapped = self.wrap_count > 0

            if self.wrap_count > 0:
                if is_current:
                    record_count = self.records_in_file
                else:
                    record_count = 256
            else:
                if index < self.current_file:
                    record_count = 256
                elif is_current:
                    record_count = self.records_in_file
                else:
                    record_count = 0

            entries.append(
                FileRingEntry(
                    index=index,
                    is_current=is_current,
                    is_wrapped=is_wrapped,
                    record_count=record_count,
                )
            )

        return entries

    def increment_live_records(self):
        self.live_record_count += 1

    def increment_replay_records(self):
        self.replay_record_count += 1