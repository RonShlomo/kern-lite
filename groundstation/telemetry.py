from __future__ import annotations
import struct
from dataclasses import dataclass


@dataclass
class SensorRecord:
    timestamp: int
    ms: int
    seq: int
    lm35_c: int
    dht_temp_c: int
    dht_hum: int
    light: int
    pot: int
    alert_bits: int
    state: int
    fault_bits: int
    crc32: int

    @property
    def lm35_celsius(self) -> float:
        return self.lm35_c / 10.0

    @property
    def dht_temp_celsius(self) -> float:
        return self.dht_temp_c / 10.0

    @property
    def dht_humidity(self) -> float:
        return self.dht_hum / 10.0

    @property
    def light_normalized(self) -> float:
        return self.light / 65535.0

    @property
    def pot_normalized(self) -> float:
        return self.pot / 65535.0

    @property
    def device_time_s(self) -> float:
        return self.timestamp + self.ms / 1000.0


class RecordDecoder:

    RECORD_SIZE = 32
    _FORMAT = "<IHHhhHHHBBB7xI"

    @staticmethod
    def decode(payload: bytes) -> SensorRecord:
        if len(payload) < RecordDecoder.RECORD_SIZE:
            raise ValueError(f"record payload too short: {len(payload)}")
        return SensorRecord(*struct.unpack_from(RecordDecoder._FORMAT, payload))


# hi/lo bit pair for each analog channel, one bit each for the DHT pair.
# MUST match firmware A3.3 bit-for-bit -- verify with Member A like the
# Day 1 CRC cross-vector.
ALERT_LM35 = 0x01 | 0x02
ALERT_LIGHT = 0x04 | 0x08
ALERT_POT = 0x10 | 0x20
ALERT_DHT_TEMP = 0x40
ALERT_DHT_HUM = 0x80

#  channel metadata (shared by chart.py, stats.py, export.py)
# attribute = scaled property on SensorRecord above
# alert mask DERIVED from the ALERT_* constants -- one truth, no drift.
CHANNELS = {
    'lm35': ('lm35_celsius', ALERT_LM35, '#8ecae6'), # powder blue
    'light': ('light_normalized', ALERT_LIGHT, '#ffcb77'), # soft amber
    'pot': ('pot_normalized', ALERT_POT, '#cdb4db'), # lilac
    'dht_temp': ('dht_temp_celsius', ALERT_DHT_TEMP, '#ffafcc'), # blush pink
    'dht_hum': ('dht_humidity', ALERT_DHT_HUM, '#95d5b2'), # mint
}


class ChannelAccumulator:

    def __init__(self, alert_mask: int):
        self.alert_mask = alert_mask
        self.n = 0
        self.min_val: float | None = None
        self.max_val: float | None = None
        self.mean = 0.0
        self.alert_activations = 0
        self.time_in_alert_s = 0.0
        self._in_alert = False

    def update(self, value: float, alert_bits: int, dt_s: float):
        self.n += 1

        if self.min_val is None or value < self.min_val:
            self.min_val = value

        if self.max_val is None or value > self.max_val:
            self.max_val = value

        self.mean += (value - self.mean) / self.n

        active = bool(alert_bits & self.alert_mask)

        if active and not self._in_alert:
            self.alert_activations += 1

        # Fence-post convention (matches C6.1): an interval counts as
        # in-alert only if BOTH its endpoints were in alert. Records at
        # t=0..4 with alert at t=1,2,3 -> spans t=1..3 -> 2 s, not 3.
        if active and self._in_alert:
            self.time_in_alert_s += dt_s

        self._in_alert = active


class TelemetryModel:
    def __init__(self):
        self.records: list[SensorRecord] = []
        self.channels = {
            name: ChannelAccumulator(mask)
            for name, (_attr, mask, _color) in CHANNELS.items()
        }

    def ingest(self, record: SensorRecord):
        if self.records:
            dt = max(0.0, record.device_time_s - self.records[-1].device_time_s)
        else:
            dt = 0.0

        self.records.append(record)

        for name, (attr, _mask, _color) in CHANNELS.items():
            self.channels[name].update(
                getattr(record, attr), record.alert_bits, dt)


# alert thresholds: mirror of firmware system/config.hpp (A3.2)
# Keep byte-for-byte in sync with Member A's ThresholdConfig constants
# {lo, hi, hysteresis}. The chart draws lo/hi; hyst is the firmware's
# release margin (alert clears only past lo+hyst / hi-hyst).
THRESHOLDS = {
    'lm35': {'lo': 10.0, 'hi': 40.0, 'hyst': 2.0}, # kThresholdLm35
    'light': {'lo': 0.05, 'hi': 0.95, 'hyst': 0.05}, # kThresholdPhoto
    'pot': {'lo': 0.02, 'hi': 0.98, 'hyst': 0.02}, # kThresholdPot
    'dht_temp': {'lo': 5.0,  'hi': 45.0, 'hyst': 2.0}, # kThresholdDht11Temp
    'dht_hum': {'lo': 10.0, 'hi': 90.0, 'hyst': 5.0}, # kThresholdDht11Hum
}