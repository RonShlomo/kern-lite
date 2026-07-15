# how to run: "python test_chart_demo.py"

import argparse
import math
import random
from dataclasses import dataclass

import matplotlib
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation, PillowWriter
from matplotlib.widgets import Slider, CheckButtons

import sys, os
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..')))

from groundstation.chart import RollingChart
from groundstation.telemetry import CHANNELS, THRESHOLDS  # real config.hpp values


@dataclass
class SimRecord:
    """
    Shaped like the decoded SensorRecord from telemetry.py (B3.1).
    """
    seq: int
    lm35_c: int
    dht_temp_c: int
    dht_hum: int
    light: int
    pot: int
    alert_bits: int

    @property
    def lm35_celsius(self): return self.lm35_c / 10.0
    @property
    def dht_temp_celsius(self): return self.dht_temp_c / 10.0
    @property
    def dht_humidity(self): return self.dht_hum / 10.0
    @property
    def light_normalized(self): return self.light / 65535.0
    @property
    def pot_normalized(self): return self.pot / 65535.0


class FakeBoard:
    def __init__(self):
        self.seq = 0
        self.pot = 0.5 # driven by the slider / script
        self.covered = False

    def _alert_bits(self, values: dict) -> int:
        bits = 0
        for name, (_attr, bit, _c) in CHANNELS.items():
            lim = THRESHOLDS.get(name, {})
            v = values[name]
            if ('hi' in lim and v > lim['hi']) or \
               ('lo' in lim and v < lim['lo']):
                bits |= bit
        return bits

    def next_record(self) -> SimRecord:
        self.seq = (self.seq + 1) % 65536
        t = self.seq / 10.0
        values = {
            'lm35': 25.0 + 2.0 * math.sin(t / 9.0) + random.uniform(-.15, .15),
            'light': 0.02 if self.covered
                        else 0.55 + 0.2 * math.sin(t / 4.0),
            'pot': max(0.0, min(1.0, self.pot + random.uniform(-.004, .004))),
            'dht_temp': 23.0 + random.uniform(-.2, .2),
            'dht_hum': 45.0 + 4.0 * math.sin(t / 11.0),
        }
        return SimRecord(
            seq=self.seq,
            lm35_c=int(values['lm35'] * 10),
            dht_temp_c=int(values['dht_temp'] * 10),
            dht_hum=int(values['dht_hum'] * 10),
            light=int(values['light'] * 65535),
            pot=int(values['pot'] * 65535),
            alert_bits=self._alert_bits(values),
        )


def make_figure():
    fig, axes = plt.subplots(len(CHANNELS), 1, figsize=(10, 10), sharex=True)
    fig.patch.set_facecolor('#fffcf7')
    ax_dict = dict(zip(CHANNELS.keys(), axes))
    axes[-1].set_xlabel("sequence number")
    return fig, ax_dict


def run_gif(path: str):
    """
    Scripted 60 s session: pot driven to an extreme at t=20..28 s,
    photodiode covered at t=35..42 s, a 3-packet gap at t=45 s.
    """
    random.seed(7)
    board = FakeBoard()
    chart = RollingChart()
    chart.set_thresholds(THRESHOLDS)
    fig, ax_dict = make_figure()

    writer = PillowWriter(fps=8)
    with writer.saving(fig, path, dpi=75):
        for i in range(600): # 600 records = 60 s @ 10 Hz
            t = i / 10.0
            board.pot = 0.995 if 20 <= t <= 28 else 0.5
            board.covered = 35 <= t <= 42
            if 45.0 <= t < 45.3: # simulate 3 lost packets
                board.seq = (board.seq + 1) % 65536
                continue

            rec = board.next_record() ### PIPELINE (1) receive
            chart.update(rec) ### PIPELINE (2) ingest
            if i == 5:
                chart.state_marker(rec.seq, "Recording")
            if t == 45.3:
                chart.gap_notch(rec.seq, gap_size=3)

            if i % 10 == 9: ### PIPELINE (3) redraw ~1 Hz
                chart.render(ax_dict)
                fig.suptitle(f"live rolling chart -- t = {t:4.1f} s", y=0.995)
                writer.grab_frame()
    print(f"wrote {path}")


def run_interactive():
    board = FakeBoard()
    chart = RollingChart()
    chart.set_thresholds(THRESHOLDS)
    fig, ax_dict = make_figure()
    fig.subplots_adjust(bottom=0.14)

    # "hardware": a pot slider and a photodiode cover
    s_ax = fig.add_axes([0.15, 0.05, 0.45, 0.03])
    pot_slider = Slider(s_ax, 'pot', 0.0, 1.0, valinit=0.5,
                        color='#cdb4db')
    c_ax = fig.add_axes([0.70, 0.03, 0.22, 0.07])
    cover_box = CheckButtons(c_ax, ['cover photodiode'], [False])

    chart.state_marker(1, "Recording") # session starts Recording

    def tick(_frame):
        board.pot = pot_slider.val
        board.covered = cover_box.get_status()[0]
        rec = board.next_record() ### PIPELINE (1)
        chart.update(rec) ### PIPELINE (2)
        if board.seq % 5 == 0: # redraw at 2 Hz
            chart.render(ax_dict) ### PIPELINE (3)

    anim = FuncAnimation(fig, tick, interval=100, cache_frame_data=False)
    plt.show()


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--gif", metavar="PATH",
                   help="render a scripted session to an animated GIF")
    args = p.parse_args()
    if args.gif:
        matplotlib.use("Agg")
        run_gif(args.gif)
    else:
        run_interactive()
