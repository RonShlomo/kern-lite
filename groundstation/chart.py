"""
Holds the last N SensorRecords in a deque and draws five channels onto
matplotlib Axes with threshold lines, alert shading, and event markers.

- Channel attributes = the scaled properties defined in B3.1.
- capacity default 120 = CMD_REPLAY default (A5.2).
- seq is uint16 and wraps at 65536 (SensorRecord struct, B5.2's
  "% 65536"); plotting positions are therefore UNWRAPPED from
  consecutive deltas so the x-axis stays monotonic across a rollover.
- alert_bits assignment is NOT fixed by the handbook (only fault_bits
  is). The bit values below mirror the fault_bits ordering -- CONFIRM
  WITH MEMBER A before the Day 6 gate.
- On REBOOT_DETECTED (B5.1) the caller should clear() this chart:
  a seq restart is not a wrap and cannot be unwrapped meaningfully.
"""

import collections
from dataclasses import dataclass
from typing import List, Dict, Any, Optional
import matplotlib.pyplot as plt

try:
    # imported as part of the package: groundstation.chart (tests)
    from .telemetry import CHANNELS as DEFAULT_CHANNELS
except ImportError:
    # imported flat: scripts run from inside groundstation/ (main.py, probe.py)
    from telemetry import CHANNELS as DEFAULT_CHANNELS

SEQ_MODULO = 65536 # seq is uint16_t on the wire


@dataclass
class Marker:
    """
    A vertical event marker pinned to a (raw) sequence number.
    """
    seq: int # raw uint16 seq where this event happened
    event_type: str # reboot/gap/state_change
    details: Any # gap size, or the new state name


COLOR_ALERT_SHADE = '#ffadad'
COLOR_HI_LINE = '#f28482'
COLOR_LO_LINE = '#f6bd60'
COLOR_REBOOT = '#e5989b'
COLOR_STATE = '#84a98c'
COLOR_GAP = '#b8bedd'


class RollingChart:
    def __init__(self, capacity: int = 120, channels=None):
        # Dependency injection with a sensible default: the chart
        # RECEIVES channel metadata instead of owning it, so it stays
        # generic plotting machinery (hand it 2 channels or 9).
        self.channels = channels if channels is not None else DEFAULT_CHANNELS
        self.capacity = capacity
        self.records = collections.deque(maxlen=self.capacity)
        self.markers: List[Marker] = []
        self.thresholds: Dict[str, Dict[str, float]] = {}

    def update(self, record) -> None:
        """
        Append a record; the deque drops the oldest automatically.
        """
        self.records.append(record)
        self._prune_markers()

    def clear(self) -> None:
        """
        Reset after REBOOT_DETECTED: a seq restart is not a wrap.
        """
        self.records.clear()
        self.markers.clear()

    def set_thresholds(self, thresholds: Dict[str, Dict[str, float]]) -> None:
        """
        Store per-channel {'lo': x, 'hi': y} limits,
        in the SAME units as the scaled properties.
        """
        self.thresholds = dict(thresholds)

    def state_marker(self, seq: int, new_state: str) -> None:
        self.markers.append(Marker(seq, "state_change", new_state))

    def reboot_marker(self, seq: int) -> None:
        self.markers.append(Marker(seq, "reboot", None))

    def gap_notch(self, seq: int, gap_size: int) -> None:
        self.markers.append(Marker(seq, "gap", gap_size))

    def _prune_markers(self) -> None:
        """
        Keep only markers whose raw seq is still in the window.
        Membership (not >=) is wrap-safe: after 65535 -> 0 a plain
        comparison would wrongly prune everything.
        """
        if not self.records:
            return
        in_window = {rec.seq for rec in self.records}
        self.markers = [m for m in self.markers if m.seq in in_window]

    def _unwrapped_x(self, records) -> List[int]:
        """
        Monotonic plotting positions from a uint16 seq.
        Like a car odometer rolling 99999 -> 00000: distance kept
        increasing, only the display wrapped. Each step forward is
        (curr - prev) mod 65536, so 65535 -> 2 yields +3 and even
        preserves the visual width of packet gaps.
        """
        xs: List[int] = []
        prev_raw = None
        x = 0
        for rec in records:
            if prev_raw is None:
                x = rec.seq
            else:
                x += (rec.seq - prev_raw) % SEQ_MODULO
            prev_raw = rec.seq
            xs.append(x)
        return xs

    def channel_arrays(self, name: str, records=None) -> Optional[Dict[str, Any]]:
        """
        B6.1: 'Expose per-channel arrays for plotting: raw values,
        alert-shading booleans, threshold lo/hi lines.'
        Returns {'seq', 'values', 'alert', 'lo', 'hi'} or None.
        """
        recs = list(self.records) if records is None else records
        if name not in self.channels or not recs:
            return None
        attribute, alert_bit, _color = self.channels[name]
        seqs, values, alerts = [], [], []
        for rec in recs:
            seqs.append(rec.seq)
            values.append(getattr(rec, attribute))
            alerts.append(bool(rec.alert_bits & alert_bit))
        limits = self.thresholds.get(name, {})
        return {'seq': seqs, 'values': values, 'alert': alerts,
                'lo': limits.get('lo'), 'hi': limits.get('hi')}

    def render(self, ax_dict: Dict[str, plt.Axes],
               thresholds_dict: Dict[str, Dict[str, float]] = None) -> None:
        """
        Draw every channel present in ax_dict (that IS the toggle:
        omit a channel's Axes and it isn't drawn). Draw order per Axes:
        line -> alert shading -> thresholds -> markers -> legend,
        because the legend only sees artists that already exist.
        """
        if not self.records:
            return
        if thresholds_dict is not None: # optional override
            self.set_thresholds(thresholds_dict)

        snapshot = list(self.records)
        xs = self._unwrapped_x(snapshot)
        seq_to_x = {rec.seq: x for rec, x in zip(self.records, xs)}
        visible = [m for m in self.markers if m.seq in seq_to_x]

        for name, ax in ax_dict.items():
            data = self.channel_arrays(name, snapshot)
            if data is None:
                continue

            ax.clear()
            _attr, _bit, line_color = self.channels[name]
            y, alert = data['values'], data['alert']

            ax.plot(xs, y, color=line_color, linewidth=2.5,
                    marker='o', markersize=3, label=name)

            # full-height wash behind alerted samples: x in data
            # cords, y in axes cords (0..1)
            if any(alert):
                ax.fill_between(xs, 0, 1, where=alert,
                                transform=ax.get_xaxis_transform(),
                                color=COLOR_ALERT_SHADE, alpha=0.35,
                                label='alert')

            if data['hi'] is not None:
                ax.axhline(data['hi'], color=COLOR_HI_LINE,
                           linestyle='--', linewidth=1.8, label='hi')
            if data['lo'] is not None:
                ax.axhline(data['lo'], color=COLOR_LO_LINE,
                           linestyle='--', linewidth=1.8, label='lo')

            for m in visible:
                mx = seq_to_x[m.seq]
                if m.event_type == "reboot":
                    ax.axvline(mx, color=COLOR_REBOOT,
                               linewidth=2, label='reboot')
                elif m.event_type == "state_change":
                    ax.axvline(mx, color=COLOR_STATE,
                               linewidth=2, label=str(m.details))
                elif m.event_type == "gap":
                    ax.axvline(mx, color=COLOR_GAP, linestyle=':',
                               linewidth=2, label=f'gap ({m.details})')

            handles, labels = ax.get_legend_handles_labels()
            unique = dict(zip(labels, handles))
            ax.legend(unique.values(), unique.keys(),
                      loc='upper right', fontsize=8)
            ax.set_ylabel(name)
            if xs[0] != xs[-1]:
                ax.set_xlim(xs[0], xs[-1])