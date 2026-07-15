"""
groundstation/export.py - CSV and human-readable text exports (FR-GS-16).

AlertLog and StateTimeline already know how to write their own reports
(AlertLog.export_text, StateTimeline.export_text); the wrappers here just
give every export the same one-line call from main.py. session_to_csv is
the one export with no existing implementation elsewhere. raw_frame_log_to_text
copies the session's continuously written frames.log (Session.append_raw_frame)
to the requested path.
"""
from __future__ import annotations

import csv
import shutil
from pathlib import Path


def session_to_csv(session, path) -> None:
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow([
            "seq", "timestamp", "ms", "wall_time",
            "lm35_celsius", "dht_temp_celsius", "dht_humidity",
            "light_normalized", "pot_normalized",
            "alert_bits", "state", "fault_bits", "crc32",
        ])
        for entry in session.records:
            r = entry.record
            writer.writerow([
                r.seq, r.timestamp, r.ms, f"{entry.wall_time:.6f}",
                f"{r.lm35_celsius:.1f}", f"{r.dht_temp_celsius:.1f}", f"{r.dht_humidity:.1f}",
                f"{r.light_normalized:.4f}", f"{r.pot_normalized:.4f}",
                r.alert_bits, r.state, r.fault_bits, r.crc32,
            ])


def alert_log_to_text(alert_log, path) -> None:
    alert_log.export_text(str(path))


def timeline_to_text(timeline, path) -> None:
    timeline.export_text(str(path))


def raw_frame_log_to_text(session, path) -> None:
    src = Path(session.path) / "frames.log"
    dest = Path(path)
    if src.exists():
        shutil.copyfile(src, dest)
    else:
        dest.write_text("", encoding="utf-8")
