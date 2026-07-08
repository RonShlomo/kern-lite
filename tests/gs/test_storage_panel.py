
import struct

from groundstation.frame import Frame, FrameType
from groundstation.storage_panel import StorageModel


def make_status_payload(
    state=0,
    sd_mounted=1,
    file_count=4,
    current_file=0,
    total_records=0,
    wrap_count=0,
    records_in_file=0,
):
    return struct.pack(
        "<BBBBIIH",
        state,
        sd_mounted,
        file_count,
        current_file,
        total_records,
        wrap_count,
        records_in_file,
    )


def test_status_decode():
    model = StorageModel()

    payload = make_status_payload(
        state=1,
        sd_mounted=1,
        file_count=4,
        current_file=2,
        total_records=600,
        wrap_count=0,
        records_in_file=88,
    )

    frame = Frame(FrameType.Status, payload)
    model.update_from_status(frame)

    assert model.state == 1
    assert model.sd_mounted is True
    assert model.file_count == 4
    assert model.current_file == 2
    assert model.total_records == 600
    assert model.wrap_count == 0
    assert model.records_in_file == 88


def test_ring_visual_before_wrap():
    model = StorageModel()

    payload = make_status_payload(
        current_file=1,
        total_records=300,
        wrap_count=0,
        records_in_file=44,
    )

    model.update_from_status(Frame(FrameType.Status, payload))
    visual = model.ring_visual()

    assert len(visual) == 4

    assert visual[0].record_count == 256
    assert visual[1].record_count == 44
    assert visual[2].record_count == 0
    assert visual[3].record_count == 0

    assert visual[1].is_current is True


def test_ring_visual_after_wrap():
    model = StorageModel()

    payload = make_status_payload(
        current_file=2,
        total_records=1300,
        wrap_count=1,
        records_in_file=20,
    )

    model.update_from_status(Frame(FrameType.Status, payload))
    visual = model.ring_visual()

    assert len(visual) == 4

    assert visual[0].record_count == 256
    assert visual[1].record_count == 256
    assert visual[2].record_count == 20
    assert visual[3].record_count == 256

    assert visual[2].is_current is True
    assert all(entry.is_wrapped for entry in visual)


def test_live_and_replay_counters():
    model = StorageModel()

    model.increment_live_records()
    model.increment_live_records()
    model.increment_replay_records()

    assert model.live_record_count == 2
    assert model.replay_record_count == 1