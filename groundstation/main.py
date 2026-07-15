"""
groundstation/main.py - KERN-LITE ground station entry point (handbook Days 6-7).

Wires together every groundstation/ module against a live serial link:
link + frame (transport), commands (send + STATUS heartbeat poll), state +
storage_panel (decoded device/storage model), telemetry + integrity (record
decode/validation), session (persistence), chart + stats + timeline +
alert_log + link_quality (analytics), export (CSV/text artifacts).

Usage:
    python -m groundstation.main --port COM4
    python groundstation/main.py --port /dev/ttyACM0 --no-chart

While running, type commands at the "kern-lite> " prompt: start, stop,
status, replay [N], erase <magic>, export [dir], stats, alerts, quality,
quit. Ctrl-C or closing the chart window also exits cleanly.
"""
from __future__ import annotations

import argparse
import struct
import sys
import threading
import time
from pathlib import Path

# Allow `python groundstation/main.py ...` (direct script) as well as
# `python -m groundstation.main ...`: put the repo root on sys.path so the
# absolute `groundstation.*` imports used throughout this package resolve
# either way.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import serial

from groundstation.link import SerialLink
from groundstation.commands import CommandSender, StatusPoller
from groundstation.frame import Frame, FrameType, NackCode, encode
from groundstation.state import DeviceStateModel, STATE_IDLE, STATE_RECORDING, STATE_FAULT
from groundstation.storage_panel import StorageModel
from groundstation.telemetry import RecordDecoder, TelemetryModel, CHANNELS, THRESHOLDS
from groundstation.integrity import IntegrityChecker
from groundstation.session import Session
from groundstation.stats import ChannelStats
from groundstation.timeline import StateTimeline
from groundstation.alert_log import AlertLog
from groundstation.link_quality import LinkQualityMonitor
from groundstation import export

try:
    import matplotlib.pyplot as plt
    from matplotlib.animation import FuncAnimation
    from groundstation.chart import RollingChart
    _HAVE_CHART = True
except ImportError:
    _HAVE_CHART = False


DEFAULT_REPLAY_N = 120          # Appendix A: default replay request
ERASE_MAGIC_HINT = 0xDEADC0DE   # Appendix A: erase magic (shown, never auto-filled)
QUALITY_WINDOW_S = 5.0          # FR-GS-17: recompute link quality every 5 s
HEARTBEAT_TIMEOUT_S = 15.0      # Appendix A / FR-GS-18


class GroundStation:
    def __init__(self, port: str, baud: int = 115200, poll_interval_s: float = 5.0,
                 chart_capacity: int = 120, sessions_dir: str = "sessions",
                 headless: bool = False):
        self.port = port
        self.baud = baud
        self.headless = headless or not _HAVE_CHART
        self.sessions_dir = sessions_dir

        self.link = SerialLink()
        self.sender = CommandSender()
        self.poller = StatusPoller(self.link, self.sender, poll_interval_s)

        self.device_state = DeviceStateModel()
        self.storage = StorageModel()
        self.telemetry = TelemetryModel()
        self.integrity = IntegrityChecker()
        self.alert_log = AlertLog()
        self.timeline = StateTimeline()
        self.link_quality = LinkQualityMonitor()
        self.channel_stats = {name: ChannelStats() for name in CHANNELS}

        self.chart = None
        if not self.headless:
            self.chart = RollingChart(capacity=chart_capacity)
            self.chart.set_thresholds(THRESHOLDS)

        self.session: Session | None = None

        self._stop_event = threading.Event()
        self._record_count = 0
        self._last_raw_seq = None
        self._alert_active: dict[str, bool] = {}
        self._start_wall = time.time()
        self._quality_rx_baseline = 0
        self._last_crc_err_count = 0
        self._last_sync_err_count = 0
        self._last_link_state = None
        self._last_heartbeat_alert = 0.0

    # ------------------------------------------------------------------ setup

    def connect(self) -> None:
        try:
            self.link.connect(self.port, self.baud)
        except serial.SerialException as exc:
            print(f"Could not open {self.port}: {exc}")
            from serial.tools import list_ports
            print("Available ports:")
            for p in list_ports.comports():
                print(f"  {p.device} - {p.description}")
            raise SystemExit(1)

        print(f"Connected to {self.port} @ {self.baud} baud")
        self._last_link_state = self.link.state

        self.session = Session.on_connect(self.port, base_dir=self.sessions_dir)
        print(f"Session: {self.session.path}")

        self.poller.start()

    def close(self) -> None:
        self.poller.stop()
        if self.session is not None:
            self.session.close()
        self.link.disconnect()

    def request_stop(self) -> None:
        self._stop_event.set()
        if _HAVE_CHART:
            try:
                plt.close("all")
            except Exception:
                pass

    # -------------------------------------------------------------- commands

    def _log_tx(self, frame_type: FrameType, payload: bytes = b"") -> None:
        if self.session is not None:
            self.session.append_raw_frame("TX", encode(Frame(type=frame_type, payload=payload)), time.time())

    def _send_guarded(self, frame_type: FrameType, payload: bytes, send_fn) -> None:
        if not self.device_state.command_allowed(frame_type):
            msg = f"{frame_type.name} rejected locally: not valid from {self.device_state.current_state_name}"
            print(f"  ! {msg}")
            self.alert_log.add("GS_EVENT", time.time(), self._record_count, msg)
            return
        self._log_tx(frame_type, payload)
        send_fn()

    def cmd_start(self) -> None:
        self._send_guarded(FrameType.CmdStart, b"", lambda: self.sender.send_start(self.link))

    def cmd_stop(self) -> None:
        self._send_guarded(FrameType.CmdStop, b"", lambda: self.sender.send_stop(self.link))

    def cmd_status(self) -> None:
        self._send_guarded(FrameType.CmdStatus, b"", lambda: self.sender.send_status(self.link))

    def cmd_replay(self, n: int) -> None:
        self._send_guarded(FrameType.CmdReplay, struct.pack("<H", n),
                            lambda: self.sender.send_replay(self.link, n))

    def cmd_erase(self, magic: int) -> None:
        self._send_guarded(FrameType.CmdErase, struct.pack("<I", magic),
                            lambda: self.sender.send_erase(self.link, magic))

    # ------------------------------------------------------------- receiving

    def _reader_loop(self) -> None:
        last_quality_calc = time.time()
        while not self._stop_event.is_set():
            frame = self.link.receive_frame()
            now = time.time()

            if frame is not None:
                self._handle_frame(frame, now)
            else:
                time.sleep(0.005)

            self._poll_link_errors(now)

            if now - last_quality_calc >= QUALITY_WINDOW_S:
                total = max(0, self.link.rx_count - self._quality_rx_baseline)
                self.link_quality.compute_score(total)
                self._quality_rx_baseline = self.link.rx_count
                last_quality_calc = now

            if (self.link_quality.check_heartbeat(now)
                    and now - self._last_heartbeat_alert > HEARTBEAT_TIMEOUT_S):
                self.alert_log.add("HEARTBEAT_TIMEOUT", now, self._record_count,
                                    "no valid STATUS or RECORD frame for 15s")
                self._last_heartbeat_alert = now

    def _poll_link_errors(self, now: float) -> None:
        if self.link.crc_error_count != self._last_crc_err_count:
            added = self.link.crc_error_count - self._last_crc_err_count
            self.link_quality.record_error("CRC", added)
            self.link_quality.record_frame(now, is_valid=False)
            self.alert_log.add("CRC_ERROR", now, self._record_count, f"{added} frame CRC error(s) on link")
            self._last_crc_err_count = self.link.crc_error_count

        if self.link.sync_error_count != self._last_sync_err_count:
            added = self.link.sync_error_count - self._last_sync_err_count
            self.link_quality.record_error("SYNC", added)
            self.link_quality.record_frame(now, is_valid=False)
            self.alert_log.add("SYNC_ERROR", now, self._record_count, f"{added} frame sync error(s) on link")
            self._last_sync_err_count = self.link.sync_error_count

        if self.link.state != self._last_link_state:
            self.alert_log.add("GS_EVENT", now, self._record_count, f"link state -> {self.link.state.name}")
            self._last_link_state = self.link.state

    def _handle_frame(self, frame: Frame, now: float) -> None:
        self.link_quality.record_frame(now, is_valid=True)
        if self.session is not None:
            self.session.append_raw_frame("RX", encode(frame), now)

        if frame.type == FrameType.Record:
            self._handle_record(frame, now)
        elif frame.type == FrameType.Status:
            self._handle_status(frame, now)
        elif frame.type == FrameType.Nack:
            self._handle_nack(frame, now)
        elif frame.type == FrameType.Ack:
            print("  ACK")

    def _handle_record(self, frame: Frame, now: float) -> None:
        rec = RecordDecoder.decode(frame.payload)
        record_ok = self.integrity.check_frame(frame)

        gap = self.integrity.check_sequence(rec.seq, self._last_raw_seq)
        self._last_raw_seq = rec.seq
        if gap:
            self.link_quality.record_error("GAP", 1)
            self.alert_log.add("SEQ_GAP", now, self._record_count, f"gap={gap} before seq={rec.seq}")
            if self.chart:
                self.chart.gap_notch(rec.seq, gap)

        if not record_ok:
            self.alert_log.add("CRC_ERROR", now, self._record_count,
                                f"record CRC mismatch at seq={rec.seq} (storage corruption)")

        self.telemetry.ingest(rec)
        self._update_channel_stats(rec)
        self._log_alert_transitions(rec, now)

        if self.chart:
            self.chart.update(rec)
        if self.session is not None:
            self.session.append_record(rec, now)

        if self.device_state.current_state == STATE_RECORDING:
            self.storage.increment_live_records()
        else:
            self.storage.increment_replay_records()

        self._record_count += 1
        self.device_state.set_session_seq(self._record_count)

        print(self._format_record(rec, record_ok))

    def _update_channel_stats(self, rec) -> None:
        for name, (attr, mask, _color) in CHANNELS.items():
            active = bool(rec.alert_bits & mask)
            self.channel_stats[name].update(getattr(rec, attr), rec.seq, rec.device_time_s, active)

    def _log_alert_transitions(self, rec, now: float) -> None:
        for name, (_attr, mask, _color) in CHANNELS.items():
            active = bool(rec.alert_bits & mask)
            was_active = self._alert_active.get(name, False)
            if active != was_active:
                category = "ALERT_ACTIVE" if active else "ALERT_CLEAR"
                self.alert_log.add(category, now, self._record_count,
                                    f"{name} alert {'set' if active else 'cleared'}")
                self.timeline.on_alert(name, active, rec.seq, now)
                self._alert_active[name] = active

    def _handle_status(self, frame: Frame, now: float) -> None:
        prev_total = self.storage.total_records
        prev_state = self.device_state.current_state

        self.device_state.update_from_status(frame)
        self.storage.update_from_status(frame)

        # prev_total starts at 0 (StorageModel default), so this never
        # false-triggers on the very first STATUS frame of a session.
        if self.storage.total_records < prev_total:
            self.alert_log.add("REBOOT", now, self._record_count,
                                f"REBOOT_DETECTED: total_records dropped {prev_total} -> {self.storage.total_records}")
            self.timeline.on_reboot(self._record_count, now)
            if self.chart:
                self.chart.reboot_marker(self._last_raw_seq or 0)
            self.cmd_status()

        if self.device_state.current_state != prev_state:
            from_name = DeviceStateModel.state_name(prev_state)
            to_name = self.device_state.current_state_name
            self.alert_log.add("STATE_TRANSITION", now, self._record_count, f"{from_name} -> {to_name}")
            self.timeline.on_state_change(from_name, to_name, now, self._record_count)
            if self.chart:
                self.chart.state_marker(self._last_raw_seq or 0, to_name)

        print(f"  STATUS {self._format_status()}")

    def _handle_nack(self, frame: Frame, now: float) -> None:
        code = NackCode(frame.payload[0]) if frame.payload else None
        name = code.name if code is not None else "Unknown"
        self.alert_log.add("NACK", now, self._record_count, name)
        print(f"  NACK {name}")

    def _format_record(self, rec, record_ok: bool) -> str:
        crc_flag = "OK" if record_ok else "CORRUPT"
        return (f"  REC seq={rec.seq:5d} lm35={rec.lm35_celsius:5.1f}C "
                f"dht={rec.dht_temp_celsius:5.1f}C/{rec.dht_humidity:5.1f}% "
                f"light={rec.light_normalized:.3f} pot={rec.pot_normalized:.3f} "
                f"alerts=0x{rec.alert_bits:02X} faults=0x{rec.fault_bits:02X} crc={crc_flag}")

    def _format_status(self) -> str:
        s = self.storage
        return (f"state={self.device_state.current_state_name} sd_mounted={s.sd_mounted} "
                f"file={s.current_file}/{s.file_count} total={s.total_records} "
                f"wraps={s.wrap_count} records_in_file={s.records_in_file} "
                f"live={s.live_record_count} replay={s.replay_record_count}")

    # -------------------------------------------------------------- commands ui

    def _print_stats(self) -> None:
        elapsed = max(1e-6, time.time() - self._start_wall)
        for name, cs in self.channel_stats.items():
            print(f"  {name:9s} n={cs.n:5d} min={cs.min_val:8.3f} max={cs.max_val:8.3f} "
                  f"mean={cs.mean:8.3f} stddev={cs.stddev:7.3f} "
                  f"alert_activations={cs.alert_activations:3d} in_alert={cs.pct_in_alert(elapsed):5.1f}%")

    def _print_alerts(self) -> None:
        for e in self.alert_log.entries[-20:]:
            print(f"  [{e.wall_time:.2f}s] seq={e.session_seq:04d} {e.category:<18} {e.message}")

    def _print_help(self) -> None:
        print("  start | stop | status | replay [N] | erase <magic>")
        print(f"  export [dir]           write session.csv, raw_frames.txt, alert_log.txt, timeline.txt")
        print(f"  stats | alerts | quality")
        print(f"  quit / exit")
        print(f"  (replay default N={DEFAULT_REPLAY_N}; erase magic is 0x{ERASE_MAGIC_HINT:08X})")

    def export_artifacts(self, out_dir: str) -> None:
        out = Path(out_dir)
        out.mkdir(parents=True, exist_ok=True)
        if self.session is not None:
            export.session_to_csv(self.session, out / "session.csv")
            export.raw_frame_log_to_text(self.session, out / "raw_frames.txt")
        export.alert_log_to_text(self.alert_log, out / "alert_log.txt")
        export.timeline_to_text(self.timeline, out / "timeline.txt")
        print(f"  exported session.csv, raw_frames.txt, alert_log.txt, timeline.txt to {out}/")

    def _dispatch_command(self, line: str) -> None:
        parts = line.split()
        cmd, args = parts[0].lower(), parts[1:]
        try:
            if cmd == "start":
                self.cmd_start()
            elif cmd == "stop":
                self.cmd_stop()
            elif cmd == "status":
                self.cmd_status()
            elif cmd == "replay":
                self.cmd_replay(int(args[0]) if args else DEFAULT_REPLAY_N)
            elif cmd == "erase":
                if not args:
                    print(f"  usage: erase <magic>   (expected 0x{ERASE_MAGIC_HINT:08X})")
                    return
                self.cmd_erase(int(args[0], 0))
            elif cmd == "export":
                self.export_artifacts(args[0] if args else "docs/demo")
            elif cmd == "quality":
                lq = self.link_quality
                print(f"  link quality: {lq.quality_pct:.0f}% (degraded={lq.degraded} poor={lq.poor})")
            elif cmd == "stats":
                self._print_stats()
            elif cmd == "alerts":
                self._print_alerts()
            elif cmd in ("quit", "exit"):
                self.request_stop()
            elif cmd in ("help", "?"):
                self._print_help()
            else:
                print(f"  unknown command: {cmd!r} (try 'help')")
        except ValueError as exc:
            print(f"  error: {exc}")

    def _command_loop(self) -> None:
        print("Type 'help' for a list of commands.")
        while not self._stop_event.is_set():
            try:
                line = input("kern-lite> ").strip()
            except (EOFError, KeyboardInterrupt):
                self.request_stop()
                return
            if line:
                self._dispatch_command(line)

    # ------------------------------------------------------------------ chart

    def _build_chart_figure(self):
        fig, axes = plt.subplots(len(CHANNELS), 1, figsize=(10, 9), sharex=True)
        ax_dict = dict(zip(CHANNELS.keys(), axes))
        axes[-1].set_xlabel("sequence number")
        fig.canvas.mpl_connect("close_event", lambda _evt: self.request_stop())
        return fig, ax_dict

    def _chart_tick(self, _frame, ax_dict, fig) -> None:
        if not self.chart or not self.chart.records:
            return
        self.chart.render(ax_dict)
        fig.suptitle(
            f"KERN-LITE  state={self.device_state.current_state_name}  "
            f"records={self._record_count}  quality={self.link_quality.quality_pct:.0f}%",
            y=0.995)

    # -------------------------------------------------------------------- run

    def run(self) -> None:
        self.connect()
        threading.Thread(target=self._reader_loop, daemon=True).start()
        threading.Thread(target=self._command_loop, daemon=True).start()

        if self.chart is not None:
            fig, ax_dict = self._build_chart_figure()
            anim = FuncAnimation(fig, self._chart_tick, fargs=(ax_dict, fig),
                                  interval=500, cache_frame_data=False)
            plt.show()
        else:
            while not self._stop_event.is_set():
                time.sleep(0.2)

        self.close()


def build_arg_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description="KERN-LITE ground station")
    ap.add_argument("--port", required=True, help="serial port, e.g. COM4 or /dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--poll-interval", type=float, default=5.0,
                     help="CMD_STATUS heartbeat poll period in seconds (default 5)")
    ap.add_argument("--chart-capacity", type=int, default=120,
                     help="records kept in the rolling chart window (default 120)")
    ap.add_argument("--sessions-dir", default="sessions")
    ap.add_argument("--no-chart", action="store_true", help="run headless, without the matplotlib window")
    return ap


def main() -> None:
    args = build_arg_parser().parse_args()

    if args.no_chart is False and not _HAVE_CHART:
        print("matplotlib not available: running headless (install matplotlib for the rolling chart).")

    gs = GroundStation(
        port=args.port, baud=args.baud, poll_interval_s=args.poll_interval,
        chart_capacity=args.chart_capacity, sessions_dir=args.sessions_dir,
        headless=args.no_chart,
    )
    try:
        gs.run()
    except KeyboardInterrupt:
        gs.request_stop()
        gs.close()


if __name__ == "__main__":
    main()
