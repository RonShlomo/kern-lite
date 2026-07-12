import struct
import pytest

from groundstation.crc import crc32
from groundstation.frame import Frame, FrameType
from groundstation.telemetry import RecordDecoder, SensorRecord, TelemetryModel
from groundstation.state import DeviceStateModel


# ---------------------------------------------------------------
# helpers
# ---------------------------------------------------------------

def make_record_bytes(
    timestamp=0, ms=0, seq=0,
    lm35_c=0, dht_temp_c=0, dht_hum=0,
    light=0, pot=0,
    alert_bits=0, state=0, fault_bits=0,
) -> bytes:
    """Build a valid 32-byte record: pack bytes 0..27, then append CRC."""
    body = struct.pack(
        "<IHHhhHHHBBB7x",
        timestamp, ms, seq,
        lm35_c, dht_temp_c, dht_hum,
        light, pot,
        alert_bits, state, fault_bits,
    )
    assert len(body) == 28
    return body + struct.pack("<I", crc32(body))


def make_status_frame(state: int) -> Frame:
    payload = bytes([state, 1, 4, 0]) + struct.pack("<IIH", 0, 0, 0)
    return Frame(type=FrameType.Status, payload=payload)


# ---------------------------------------------------------------
# Test 1: decode a hand-crafted record, verify all scaled properties
# ---------------------------------------------------------------

def test_decode_scaled_properties():
    payload = make_record_bytes(
        timestamp=12, ms=345, seq=5,
        lm35_c=253,        # 25.3 degC
        dht_temp_c=-15,    # -1.5 degC (signed decode check)
        dht_hum=605,       # 60.5 %RH
        light=32768,       # ~0.5 normalized
        pot=65535,         # 1.0 normalized
        alert_bits=0x01, state=1, fault_bits=0x02,
    )
    rec = RecordDecoder.decode(payload)

    assert rec.timestamp == 12
    assert rec.ms == 345
    assert rec.seq == 5
    assert rec.lm35_celsius == pytest.approx(25.3)
    assert rec.dht_temp_celsius == pytest.approx(-1.5)
    assert rec.dht_humidity == pytest.approx(60.5)
    assert rec.light_normalized == pytest.approx(32768 / 65535.0)
    assert rec.pot_normalized == pytest.approx(1.0)
    assert rec.alert_bits == 0x01
    assert rec.state == 1
    assert rec.fault_bits == 0x02


# ---------------------------------------------------------------
# Test 2: record-level CRC check on the payload bytes
# ---------------------------------------------------------------

def test_record_crc_matches():
    payload = make_record_bytes(seq=1, lm35_c=253)
    stored_crc = struct.unpack_from("<I", payload, 28)[0]
    assert crc32(payload[0:28]) == stored_crc
    # the decoded field must equal the wire value too
    assert RecordDecoder.decode(payload).crc32 == stored_crc


def test_record_crc_detects_corruption():
    payload = bytearray(make_record_bytes(seq=1, lm35_c=253))
    payload[10] ^= 0xFF  # corrupt one data byte
    stored_crc = struct.unpack_from("<I", payload, 28)[0]
    assert crc32(bytes(payload[0:28])) != stored_crc


# ---------------------------------------------------------------
# Test 3: TelemetryModel min / max / mean over 5 known records
# ---------------------------------------------------------------

def test_telemetry_stats_five_records():
    model = TelemetryModel()
    lm35_values = [100, 200, 300, 400, 500]  # 10.0 .. 50.0 degC

    for i, v in enumerate(lm35_values):
        payload = make_record_bytes(timestamp=i, seq=i + 1, lm35_c=v)
        model.ingest(RecordDecoder.decode(payload))

    stats = model.channels["lm35"]
    assert stats.n == 5
    assert stats.min_val == pytest.approx(10.0)
    assert stats.max_val == pytest.approx(50.0)
    assert stats.mean == pytest.approx(30.0)
    assert len(model.records) == 5


def test_telemetry_alert_tracking():
    model = TelemetryModel()
    # records at t=0..4 s; LM35 alert active at t=1,2,3
    alert_pattern = [0x00, 0x01, 0x01, 0x01, 0x00]

    for i, ab in enumerate(alert_pattern):
        payload = make_record_bytes(timestamp=i, seq=i + 1, alert_bits=ab)
        model.ingest(RecordDecoder.decode(payload))

    stats = model.channels["lm35"]
    assert stats.alert_activations == 1          # one entry into alert
    assert stats.time_in_alert_s == pytest.approx(3.0)


# ---------------------------------------------------------------
# Test 4: DeviceStateModel transitions 0 -> 1 -> 0 -> 2
# ---------------------------------------------------------------

def test_state_transitions():
    dsm = DeviceStateModel()
    for s in [0, 1, 0, 2]:
        dsm.update_from_status(make_status_frame(s))

    assert len(dsm.transitions) == 3
    for t in dsm.transitions:
        assert t.duration_in_prev >= 0.0  # adapt to your entry structure


# ---------------------------------------------------------------
# Test 5: command gating
# ---------------------------------------------------------------

def test_command_gating_in_recording():
    dsm = DeviceStateModel()
    dsm.update_from_status(make_status_frame(1))  # Recording

    assert not dsm.command_allowed(FrameType.CmdStart)
    assert dsm.command_allowed(FrameType.CmdStop)
    assert dsm.command_allowed(FrameType.CmdStatus)

def test_record_crc_cross_vector():
    body = struct.pack('<IHHhhHHHBBB7x', 12, 345, 5, 253, -15, 605, 32768, 65535, 0x01, 1, 0x02)
    assert crc32(body) == 0x8CA5C1DB