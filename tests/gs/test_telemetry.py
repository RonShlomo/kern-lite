import struct
import time

import pytest

from groundstation.crc import crc32
from groundstation.frame import Frame, FrameType
from groundstation.telemetry import RecordDecoder, TelemetryModel
from groundstation.state import DeviceStateModel


def _frame_type(name_candidates, value):
    for name in name_candidates:
        if hasattr(FrameType, name):
            return getattr(FrameType, name)
    return FrameType(value)


STATUS = _frame_type(["STATUS", "Status"], 0x12)
CMD_START = _frame_type(["CMD_START", "CmdStart"], 0x01)
CMD_STOP = _frame_type(["CMD_STOP", "CmdStop"], 0x02)


def _make_frame(frame_type, payload: bytes) -> Frame:
    try:
        return Frame(type=frame_type, payload=payload)
    except TypeError:
        return Frame(frame_type, payload)


def make_status_frame(state: int, sd_mounted: int = 1) -> Frame:
    payload = struct.pack(
        "<BBBBIIH",
        state,       # state
        sd_mounted,  # sd_mounted
        4,           # file_count
        0,           # current_file
        0,           # total_records
        0,           # wrap_count
        0,           # records_in_file
    )

    assert len(payload) == 14
    return _make_frame(STATUS, payload)


def make_record_payload(
    *,
    timestamp=123,
    ms=456,
    seq=5,
    lm35_c=253,
    dht_temp_c=221,
    dht_hum=550,
    light=32768,
    pot=16384,
    alert_bits=0x00,
    state=1,
    fault_bits=0x00,
) -> bytes:
    without_crc = struct.pack(
        "<IHHhhHHHBBB7x",
        timestamp,
        ms,
        seq,
        lm35_c,
        dht_temp_c,
        dht_hum,
        light,
        pot,
        alert_bits,
        state,
        fault_bits,
    )

    assert len(without_crc) == 28

    record_crc = crc32(without_crc)
    payload = without_crc + struct.pack("<I", record_crc)

    assert len(payload) == 32
    return payload


def decode_record(payload: bytes):
    decoder = RecordDecoder()
    return decoder.decode(payload)


def test_decode_hardcoded_record_scaled_properties():
    payload = make_record_payload(
        seq=5,
        lm35_c=253,       # 25.3 degC
        dht_temp_c=221,   # 22.1 degC
        dht_hum=550,      # 55.0 %RH
        light=32768,
        pot=16384,
        state=1,
    )

    record = decode_record(payload)

    assert record.timestamp == 123
    assert record.ms == 456
    assert record.seq == 5

    assert record.lm35_c == 253
    assert record.dht_temp_c == 221
    assert record.dht_hum == 550
    assert record.light == 32768
    assert record.pot == 16384
    assert record.state == 1

    assert record.lm35_celsius == pytest.approx(25.3)
    assert record.dht_temp_celsius == pytest.approx(22.1)
    assert record.dht_humidity == pytest.approx(55.0)
    assert record.light_normalized == pytest.approx(32768 / 65535.0)
    assert record.pot_normalized == pytest.approx(16384 / 65535.0)


def test_record_crc_matches_payload_crc_field():
    payload = make_record_payload(seq=5)

    computed_crc = crc32(payload[0:28])
    stored_crc = struct.unpack_from("<I", payload, 28)[0]

    assert computed_crc == stored_crc


def _get_stats_container(model):
    for attr in ["stats", "channel_stats", "per_channel_stats", "channels"]:
        if hasattr(model, attr):
            return getattr(model, attr)

    raise AssertionError(
        "TelemetryModel should expose stats, channel_stats, per_channel_stats, or channels"
    )


def _get_channel_stats(model, channel_candidates):
    container = _get_stats_container(model)

    if isinstance(container, dict):
        for name in channel_candidates:
            if name in container:
                return container[name]

    for attr in channel_candidates:
        if hasattr(container, attr):
            return getattr(container, attr)

    for attr in ["lm35_stats", "stats_lm35", "lm35"]:
        if hasattr(model, attr):
            return getattr(model, attr)

    raise AssertionError(
        f"Could not find stats for channel. Tried: {channel_candidates}"
    )


def _stat_value(stat, names):
    if isinstance(stat, dict):
        for name in names:
            if name in stat:
                return stat[name]

    for name in names:
        if hasattr(stat, name):
            return getattr(stat, name)

    raise AssertionError(f"Could not find stat value. Tried: {names}")


def test_telemetry_model_ingest_min_max_mean_for_lm35():
    model = TelemetryModel()

    values_raw = [100, 200, 300, 400, 500]  # 10.0, 20.0, 30.0, 40.0, 50.0 degC

    for i, lm35_raw in enumerate(values_raw, start=1):
        payload = make_record_payload(seq=i, lm35_c=lm35_raw)
        record = decode_record(payload)
        model.ingest(record)

    lm35_stats = _get_channel_stats(
        model,
        ["lm35_celsius", "lm35", "lm35_c", "lm35_temp"],
    )

    min_val = _stat_value(lm35_stats, ["min", "min_val", "minimum"])
    max_val = _stat_value(lm35_stats, ["max", "max_val", "maximum"])
    mean_val = _stat_value(lm35_stats, ["mean", "avg", "average"])

    assert min_val == pytest.approx(10.0)
    assert max_val == pytest.approx(50.0)
    assert mean_val == pytest.approx(30.0)


def test_device_state_model_transitions():
    model = DeviceStateModel()

    model.update_from_status(make_status_frame(0))
    time.sleep(0.001)

    model.update_from_status(make_status_frame(1))
    time.sleep(0.001)

    model.update_from_status(make_status_frame(0))
    time.sleep(0.001)

    model.update_from_status(make_status_frame(2))

    assert len(model.transitions) == 3

    assert model.transitions[0][2] == "Idle"
    assert model.transitions[0][3] == "Recording"

    assert model.transitions[1][2] == "Recording"
    assert model.transitions[1][3] == "Idle"

    assert model.transitions[2][2] == "Idle"
    assert model.transitions[2][3] == "Fault"

    for transition in model.transitions:
        duration_in_prev = transition[4]
        assert duration_in_prev >= 0.0


def test_command_gating_in_recording_state():
    model = DeviceStateModel()
    model.update_from_status(make_status_frame(1, sd_mounted=1))

    assert model.command_allowed(CMD_START) is False
    assert model.command_allowed(CMD_STOP) is True