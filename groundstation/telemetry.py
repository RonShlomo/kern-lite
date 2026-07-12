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


class ChannelAccumulator:

    def __init__(self, alert_masks: int):
        self.alert_masks = alert_masks
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

        active = bool(alert_bits & self.alert_masks)

        if active and not self._in_alert:
            self.alert_activations += 1

        if active:
            self.time_in_alert_s += dt_s

        self._in_alert = active



ALERT_LM35 = 0x01 | 0x02
ALERT_LIGHT = 0x04 | 0x08
ALERT_POT = 0x10 | 0x20
ALERT_DHT_TEMP = 0x40
ALERT_DHT_HUM = 0x80


class TelemetryModel:
    def __init__(self):
        self.records: list[SensorRecord] = []
        self.channels = {
            "lm35": ChannelAccumulator(ALERT_LM35),
            "dht_temp": ChannelAccumulator(ALERT_DHT_TEMP),
            "dht_hum": ChannelAccumulator(ALERT_DHT_HUM),
            "light": ChannelAccumulator(ALERT_LIGHT),
            "pot": ChannelAccumulator(ALERT_POT),
        }

    def ingest(self, record: SensorRecord):
        if self.records:
            dt = max(0.0, record.device_time_s - self.records[-1].device_time_s)
        else:
            dt = 0.0

        self.records.append(record)

        ab = record.alert_bits
        self.channels["lm35"].update(record.lm35_celsius, ab, dt)
        self.channels["dht_temp"].update(record.dht_temp_celsius, ab, dt)
        self.channels["dht_hum"].update(record.dht_humidity, ab, dt)
        self.channels["light"].update(record.light_normalized, ab, dt)
        self.channels["pot"].update(record.pot_normalized, ab, dt)