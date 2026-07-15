from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from datetime import datetime
import struct
from typing import Optional

from groundstation.telemetry import RecordDecoder
from groundstation.crc import crc32


RECORD_SIZE = 32
WALL_TIME_SIZE = 8
SESSION_ENTRY_SIZE = RECORD_SIZE + WALL_TIME_SIZE
AUTO_FLUSH_EVERY_N = 16


@dataclass
class SessionRecord:
    record: object
    wall_time: float
    raw: bytes


class Session:
    def __init__(self, path: Path, port: Optional[str] = None, open_files: bool = True):
        self.path = Path(path)
        self.port = port
        self.records: list[SessionRecord] = []

        self._session_file = None
        self._frames_file = None
        self._records_since_flush = 0

        if open_files:
            self.path.mkdir(parents=True, exist_ok=True)
            self._session_file = open(self.path / "session.bin", "ab")
            self._frames_file = open(self.path / "frames.log", "a", encoding="utf-8")

    @classmethod
    def on_connect(cls, port: str, base_dir: str | Path = "sessions") -> "Session":
        base = Path(base_dir)
        timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        path = base / timestamp

        # Avoid collision if two sessions start in the same second.
        suffix = 1
        original_path = path
        while path.exists():
            path = Path(f"{original_path}_{suffix:02d}")
            suffix += 1

        return cls(path=path, port=port, open_files=True)

    def append_record(self, record, wall_time: float) -> None:
        self._require_open()

        raw = self._record_to_bytes(record)
        entry = raw + struct.pack("<d", float(wall_time))

        self._session_file.write(entry)
        self.records.append(SessionRecord(record=record, wall_time=float(wall_time), raw=raw))

        self._records_since_flush += 1
        if self._records_since_flush >= AUTO_FLUSH_EVERY_N:
            self._session_file.flush()
            self._records_since_flush = 0

    def append_raw_frame(self, direction: str, frame_bytes: bytes, wall_time: float) -> None:
        self._require_open()

        if not isinstance(frame_bytes, (bytes, bytearray)):
            raise TypeError("frame_bytes must be bytes")

        direction = direction.upper()
        hex_bytes = bytes(frame_bytes).hex(" ").upper()
        self._frames_file.write(f"{float(wall_time):.6f} {direction} {hex_bytes}\n")

    def close(self) -> None:
        if self._session_file is not None:
            self._session_file.flush()
            self._session_file.close()
            self._session_file = None

        if self._frames_file is not None:
            self._frames_file.flush()
            self._frames_file.close()
            self._frames_file = None

    @classmethod
    def load(cls, path: str | Path) -> "Session":
        path = Path(path)
        session = cls(path=path, open_files=False)

        session_file = path / "session.bin"
        if not session_file.exists():
            raise FileNotFoundError(f"session.bin not found in {path}")

        data = session_file.read_bytes()

        if len(data) % SESSION_ENTRY_SIZE != 0:
            raise ValueError(
                f"Invalid session.bin size: {len(data)} bytes is not a multiple of {SESSION_ENTRY_SIZE}"
            )

        decoder = RecordDecoder()

        for offset in range(0, len(data), SESSION_ENTRY_SIZE):
            raw = data[offset : offset + RECORD_SIZE]
            wall_time_bytes = data[offset + RECORD_SIZE : offset + SESSION_ENTRY_SIZE]

            wall_time = struct.unpack("<d", wall_time_bytes)[0]
            record = decoder.decode(raw)

            session.records.append(
                SessionRecord(record=record, wall_time=wall_time, raw=raw)
            )

        return session

    def _require_open(self) -> None:
        if self._session_file is None or self._frames_file is None:
            raise RuntimeError("Session is not open for writing")

    def _record_to_bytes(self, record) -> bytes:
        if isinstance(record, (bytes, bytearray)):
            raw = bytes(record)
            if len(raw) != RECORD_SIZE:
                raise ValueError(f"record bytes must be exactly {RECORD_SIZE} bytes")
            return raw

        if hasattr(record, "raw"):
            raw = bytes(record.raw)
            if len(raw) == RECORD_SIZE:
                return raw

        if hasattr(record, "raw_bytes"):
            raw = bytes(record.raw_bytes)
            if len(raw) == RECORD_SIZE:
                return raw

        if hasattr(record, "to_bytes"):
            raw = record.to_bytes()
            if len(raw) != RECORD_SIZE:
                raise ValueError(f"record.to_bytes() must return {RECORD_SIZE} bytes")
            return raw

        first_28 = struct.pack(
            "<IHHhhHHHBBB7s",
            record.timestamp,
            record.ms,
            record.seq,
            record.lm35_c,
            record.dht_temp_c,
            record.dht_hum,
            record.light,
            record.pot,
            record.alert_bits,
            record.state,
            record.fault_bits,
            b"\x00" * 7,
        )

        computed_crc = crc32(first_28)
        record_crc = getattr(record, "crc32", computed_crc)

        return first_28 + struct.pack("<I", record_crc)