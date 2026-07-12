
import struct

from groundstation.crc import crc32
from groundstation.session import Session
from groundstation.telemetry import RecordDecoder


def make_record_payload(seq: int) -> bytes:
    first_28 = struct.pack(
        "<IHHhhHHHBBB7s",
        1000 + seq,       # timestamp
        seq % 1000,       # ms
        seq,              # seq
        250 + seq,        # lm35_c
        230 + seq,        # dht_temp_c
        500 + seq,        # dht_hum
        1000 + seq,       # light
        2000 + seq,       # pot
        0,                # alert_bits
        1,                # state
        0,                # fault_bits
        b"\x00" * 7,      # reserved
    )

    return first_28 + struct.pack("<I", crc32(first_28))


def test_write_10_records_close_and_reload(tmp_path):
    session = Session.on_connect("COM9", base_dir=tmp_path)

    decoder = RecordDecoder()

    for seq in range(1, 11):
        payload = make_record_payload(seq)
        record = decoder.decode(payload)
        session.append_record(record, wall_time=1000.0 + seq)

    session_path = session.path
    session.close()

    loaded = Session.load(session_path)

    assert len(loaded.records) == 10

    for index, entry in enumerate(loaded.records, start=1):
        assert entry.record.seq == index
        assert entry.record.timestamp == 1000 + index
        assert entry.record.lm35_c == 250 + index
        assert entry.record.dht_temp_c == 230 + index
        assert entry.record.dht_hum == 500 + index
        assert entry.record.light == 1000 + index
        assert entry.record.pot == 2000 + index


def test_wall_clock_timestamps_are_monotonic(tmp_path):
    session = Session.on_connect("COM9", base_dir=tmp_path)
    decoder = RecordDecoder()

    for seq in range(1, 11):
        payload = make_record_payload(seq)
        record = decoder.decode(payload)
        session.append_record(record, wall_time=2000.0 + seq)

    session_path = session.path
    session.close()

    loaded = Session.load(session_path)
    timestamps = [entry.wall_time for entry in loaded.records]

    assert timestamps == sorted(timestamps)


def test_append_raw_frame_writes_log_line(tmp_path):
    session = Session.on_connect("COM9", base_dir=tmp_path)

    session.append_raw_frame("tx", b"\xAB\x03\x00\x00\xCD", wall_time=1234.5)

    session_path = session.path
    session.close()

    log_text = (session_path / "frames.log").read_text(encoding="utf-8")

    assert "TX" in log_text
    assert "AB 03 00 00 CD" in log_text
    assert "1234.500000" in log_text