import argparse
import struct
import sys
import time
import zlib
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Optional

from groundstation.commands import CommandSender
from groundstation.frame import FrameType
from groundstation.link import SerialLink


ERASE_MAGIC = 0xDEADC0DE
RECORD_SIZE = 32
RECORD_CRC_OFFSET = 28
SEQ_OFFSET = 6


@dataclass
class StatusSnapshot:
    state: int
    sd_mounted: int
    file_count: int
    current_file: int
    total_records: int
    wrap_count: int
    records_in_file: int

    @classmethod
    def from_frame(cls, frame):
        return cls(
            state=int(frame.state),
            sd_mounted=int(frame.sd_mounted),
            file_count=int(frame.file_count),
            current_file=int(frame.current_file),
            total_records=int(frame.total_records),
            wrap_count=int(frame.wrap_count),
            records_in_file=int(frame.records_in_file),
        )


@dataclass
class RecordTracker:
    name: str
    count: int = 0
    first_seq: Optional[int] = None
    last_seq: Optional[int] = None
    crc_errors: int = 0
    malformed_records: int = 0
    sequence_gaps: list[str] = field(default_factory=list)

    def ingest(self, payload: bytes) -> None:
        self.count += 1

        if len(payload) != RECORD_SIZE:
            self.malformed_records += 1
            return


        seq = struct.unpack_from("<H", payload, SEQ_OFFSET)[0]
        expected_crc = struct.unpack_from("<I", payload, RECORD_CRC_OFFSET)[0]
        actual_crc = zlib.crc32(payload[:RECORD_CRC_OFFSET]) & 0xFFFFFFFF

        if expected_crc != actual_crc:
            self.crc_errors += 1

        if self.first_seq is None:
            self.first_seq = seq

        if self.last_seq is not None:
            expected_seq = (self.last_seq + 1) & 0xFFFF
            if seq != expected_seq:
                gap = (seq - expected_seq) & 0xFFFF
                self.sequence_gaps.append(
                    f"expected={expected_seq}, received={seq}, gap={gap}"
                )

        self.last_seq = seq


@dataclass
class Check:
    name: str
    passed: bool
    details: str


class Phase5IntegrationTest:
    def __init__(self, port: str, duration_s: float, replay_count: int):
        self.port = port
        self.duration_s = duration_s
        self.replay_count = replay_count
        self.link = SerialLink()
        self.commands = CommandSender()
        self.checks: list[Check] = []
        self.lines: list[str] = []
        self.initial_status: Optional[StatusSnapshot] = None
        self.stop_status: Optional[StatusSnapshot] = None
        self.final_status: Optional[StatusSnapshot] = None
        self.live = RecordTracker("live")
        self.replay = RecordTracker("replay")
        self.trailing_records_after_stop = 0

    def log(self, message: str = "") -> None:
        print(message)
        self.lines.append(message)

    def add_check(self, name: str, passed: bool, details: str) -> None:
        self.checks.append(Check(name, passed, details))
        result = "PASS" if passed else "FAIL"
        self.log(f"[{result}] {name}: {details}")

    def receive_one(self):
        frame = self.link.receive_frame()
        if frame is None:
            time.sleep(0.002)
        return frame

    def print_status(self, label: str, status: StatusSnapshot) -> None:
        self.log(f"{label}:")
        self.log(f"  state={status.state}")
        self.log(f"  sd_mounted={status.sd_mounted}")
        self.log(f"  file_count={status.file_count}")
        self.log(f"  current_file={status.current_file}")
        self.log(f"  total_records={status.total_records}")
        self.log(f"  wrap_count={status.wrap_count}")
        self.log(f"  records_in_file={status.records_in_file}")

    def wait_for_status(self, timeout_s: float, expected_state: Optional[int] = None):
        deadline = time.monotonic() + timeout_s

        while time.monotonic() < deadline:
            frame = self.receive_one()
            if frame is None:
                continue

            if frame.type == FrameType.Status:
                status = StatusSnapshot.from_frame(frame)
                if expected_state is None or status.state == expected_state:
                    return status

            elif frame.type == FrameType.Nack:
                raise RuntimeError(f"NACK received: {frame.payload.hex()}")

        return None

    def wait_for_ack_and_status(
        self,
        timeout_s: float,
        expected_state: int,
        tracker: Optional[RecordTracker] = None,
    ):
        deadline = time.monotonic() + timeout_s
        ack_received = False
        status = None

        while time.monotonic() < deadline:
            frame = self.receive_one()
            if frame is None:
                continue

            if frame.type == FrameType.Record and tracker is not None:
                tracker.ingest(frame.payload)

            elif frame.type == FrameType.Status:
                candidate = StatusSnapshot.from_frame(frame)
                if candidate.state == expected_state:
                    status = candidate

            elif frame.type == FrameType.Ack:
                ack_received = True

            elif frame.type == FrameType.Nack:
                raise RuntimeError(f"NACK received: {frame.payload.hex()}")

            if ack_received and status is not None:
                return ack_received, status

        return ack_received, status

    def wait_for_ack(self, timeout_s: float, tracker: Optional[RecordTracker] = None):
        deadline = time.monotonic() + timeout_s

        while time.monotonic() < deadline:
            frame = self.receive_one()
            if frame is None:
                continue

            if frame.type == FrameType.Record and tracker is not None:
                tracker.ingest(frame.payload)

            elif frame.type == FrameType.Ack:
                return True

            elif frame.type == FrameType.Nack:
                raise RuntimeError(f"NACK received: {frame.payload.hex()}")

        return False

    def capture_live_records(self) -> None:
        self.log(f"\nCapturing live RECORD frames for {self.duration_s:.1f} seconds...")
        deadline = time.monotonic() + self.duration_s

        while time.monotonic() < deadline:
            frame = self.receive_one()
            if frame is None:
                continue

            if frame.type == FrameType.Record:
                self.live.ingest(frame.payload)

            elif frame.type == FrameType.Nack:
                raise RuntimeError(f"NACK received during live capture: {frame.payload.hex()}")

        self.log(
            f"Live capture complete: count={self.live.count}, "
            f"first_seq={self.live.first_seq}, last_seq={self.live.last_seq}"
        )

    def drain_after_stop(self, duration_s: float = 0.5) -> None:
        deadline = time.monotonic() + duration_s
        before = self.live.count

        while time.monotonic() < deadline:
            frame = self.receive_one()
            if frame is None:
                continue

            if frame.type == FrameType.Record:
                self.live.ingest(frame.payload)

            elif frame.type == FrameType.Nack:
                raise RuntimeError(f"NACK received after STOP: {frame.payload.hex()}")

        self.trailing_records_after_stop = self.live.count - before

    def capture_replay(self) -> bool:
        timeout_s = max(10.0, self.replay_count * 0.25 + 5.0)
        deadline = time.monotonic() + timeout_s
        ack_received = False

        while time.monotonic() < deadline:
            frame = self.receive_one()
            if frame is None:
                continue

            if frame.type == FrameType.Record:
                self.replay.ingest(frame.payload)

            elif frame.type == FrameType.Ack:
                ack_received = True
                break

            elif frame.type == FrameType.Nack:
                raise RuntimeError(f"NACK received during REPLAY: {frame.payload.hex()}")

        return ack_received

    def run(self) -> int:
        self.log("KERN-LITE Phase 5 Live Integration Test")
        self.log(f"Port: {self.port}")
        self.log(f"Live duration: {self.duration_s:.1f} s")
        self.log(f"Replay count: {self.replay_count}")
        self.log("")

        try:
            self.link.connect(self.port)

            self.log("Requesting initial STATUS...")
            self.commands.send_status(self.link)
            self.initial_status = self.wait_for_status(3.0)

            self.add_check(
                "Initial STATUS received",
                self.initial_status is not None,
                "STATUS frame received" if self.initial_status else "No STATUS frame received",
            )
            if self.initial_status is None:
                return self.finish()

            self.print_status("Initial STATUS", self.initial_status)
            self.add_check(
                "Device starts in Idle",
                self.initial_status.state == 0,
                f"state={self.initial_status.state}",
            )
            self.add_check(
                "SD card mounted",
                self.initial_status.sd_mounted == 1,
                f"sd_mounted={self.initial_status.sd_mounted}",
            )

            if self.initial_status.state != 0 or self.initial_status.sd_mounted != 1:
                return self.finish()

            self.log("\nSending START...")
            self.commands.send_start(self.link)
            start_ack, start_status = self.wait_for_ack_and_status(
                5.0, expected_state=1, tracker=self.live
            )
            self.add_check("START ACK", start_ack, f"ack_received={start_ack}")
            self.add_check(
                "START enters Recording",
                start_status is not None,
                "state=1 received" if start_status else "No STATUS state=1 received",
            )
            if not start_ack or start_status is None:
                return self.finish()

            self.capture_live_records()

            expected_live = self.duration_s * 10.0
            lower_bound = max(1, int(expected_live * 0.80))
            upper_bound = int(expected_live * 1.20) + 2
            self.add_check(
                "Live record count",
                lower_bound <= self.live.count <= upper_bound,
                f"received={self.live.count}, expected_range={lower_bound}..{upper_bound}",
            )
            self.add_check(
                "Live record CRC",
                self.live.crc_errors == 0 and self.live.malformed_records == 0,
                f"crc_errors={self.live.crc_errors}, malformed={self.live.malformed_records}",
            )
            self.add_check(
                "Live sequence continuity",
                len(self.live.sequence_gaps) == 0,
                (
                    "no gaps"
                    if not self.live.sequence_gaps
                    else "; ".join(self.live.sequence_gaps[:5])
                ),
            )

            self.log("\nSending STOP...")
            self.commands.send_stop(self.link)
            stop_ack, self.stop_status = self.wait_for_ack_and_status(
                5.0, expected_state=0, tracker=self.live
            )
            self.add_check("STOP ACK", stop_ack, f"ack_received={stop_ack}")
            self.add_check(
                "STOP enters Idle",
                self.stop_status is not None,
                "state=0 received" if self.stop_status else "No STATUS state=0 received",
            )
            if self.stop_status is None:
                self.commands.send_status(self.link)
                self.stop_status = self.wait_for_status(3.0, expected_state=0)

            if self.stop_status is not None:
                self.print_status("STATUS after STOP", self.stop_status)

            self.drain_after_stop()
            self.add_check(
                "Streaming stops",
                self.trailing_records_after_stop <= 2,
                f"trailing_records={self.trailing_records_after_stop}",
            )

            if self.initial_status is not None and self.stop_status is not None:
                written_delta = (
                    self.stop_status.total_records - self.initial_status.total_records
                )
                difference = abs(written_delta - self.live.count)
                tolerance = max(3, int(self.live.count * 0.05))
                self.add_check(
                    "Stored versus received records",
                    difference <= tolerance,
                    (
                        f"stored_delta={written_delta}, live_received={self.live.count}, "
                        f"difference={difference}, tolerance={tolerance}"
                    ),
                )

            self.log(f"\nSending REPLAY {self.replay_count}...")
            self.commands.send_replay(self.link, self.replay_count)
            replay_ack = self.capture_replay()
            self.add_check("REPLAY ACK", replay_ack, f"ack_received={replay_ack}")
            self.add_check(
                "REPLAY record count",
                self.replay.count == self.replay_count,
                f"received={self.replay.count}, expected={self.replay_count}",
            )
            self.add_check(
                "Replay record CRC",
                self.replay.crc_errors == 0 and self.replay.malformed_records == 0,
                f"crc_errors={self.replay.crc_errors}, malformed={self.replay.malformed_records}",
            )
            self.add_check(
                "Replay sequence continuity",
                len(self.replay.sequence_gaps) == 0,
                (
                    "no gaps"
                    if not self.replay.sequence_gaps
                    else "; ".join(self.replay.sequence_gaps[:5])
                ),
            )

            if self.live.last_seq is not None and self.replay.last_seq is not None:
                self.add_check(
                    "Replay ends at latest record",
                    self.replay.last_seq == self.live.last_seq,
                    (
                        f"replay_last_seq={self.replay.last_seq}, "
                        f"live_last_seq={self.live.last_seq}"
                    ),
                )

            self.log("\nSending ERASE...")
            self.commands.send_erase(self.link, ERASE_MAGIC)
            erase_ack = self.wait_for_ack(10.0)
            self.add_check("ERASE ACK", erase_ack, f"ack_received={erase_ack}")

            self.log("Requesting final STATUS...")
            self.commands.send_status(self.link)
            self.final_status = self.wait_for_status(5.0)
            self.add_check(
                "Final STATUS received",
                self.final_status is not None,
                "STATUS frame received" if self.final_status else "No STATUS frame received",
            )

            if self.final_status is not None:
                self.print_status("Final STATUS", self.final_status)
                self.add_check(
                    "ERASE resets total_records",
                    self.final_status.total_records == 0,
                    f"total_records={self.final_status.total_records}",
                )
                self.add_check(
                    "ERASE resets ring position",
                    (
                        self.final_status.current_file == 0
                        and self.final_status.wrap_count == 0
                        and self.final_status.records_in_file == 0
                    ),
                    (
                        f"current_file={self.final_status.current_file}, "
                        f"wrap_count={self.final_status.wrap_count}, "
                        f"records_in_file={self.final_status.records_in_file}"
                    ),
                )
                self.add_check(
                    "Device remains Idle after ERASE",
                    self.final_status.state == 0,
                    f"state={self.final_status.state}",
                )
                self.add_check(
                    "SD remains mounted after ERASE",
                    self.final_status.sd_mounted == 1,
                    f"sd_mounted={self.final_status.sd_mounted}",
                )

        except KeyboardInterrupt:
            self.log("\nTest interrupted by user.")
            self.add_check("Test completed", False, "Interrupted by user")
        except Exception as exc:
            self.log(f"\nERROR: {exc}")
            self.add_check("Unexpected error", False, str(exc))
        finally:
            self.link.disconnect()

        return self.finish()

    def finish(self) -> int:
        self.log("\nSummary")
        passed = sum(1 for check in self.checks if check.passed)
        failed = sum(1 for check in self.checks if not check.passed)

        for check in self.checks:
            result = "PASS" if check.passed else "FAIL"
            self.log(f"{result}: {check.name} - {check.details}")

        self.log("")
        self.log(f"Passed: {passed}")
        self.log(f"Failed: {failed}")
        self.log(f"Overall result: {'PASS' if failed == 0 else 'FAIL'}")

        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        report_path = Path(f"phase5_integration_report_{timestamp}.txt")
        report_path.write_text("\n".join(self.lines) + "\n", encoding="utf-8")
        print(f"\nReport saved to: {report_path.resolve()}")

        return 0 if failed == 0 else 1


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run the KERN-LITE Phase 5 live integration test."
    )
    parser.add_argument("--port", default="COM9")
    parser.add_argument("--duration", type=float, default=30.0)
    parser.add_argument("--replay", type=int, default=60)
    parser.add_argument(
        "--yes",
        action="store_true",
        help="Skip the erase confirmation prompt.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if args.duration <= 0:
        print("Duration must be greater than zero.")
        return 2

    if args.replay < 0 or args.replay > 65535:
        print("Replay count must be between 0 and 65535.")
        return 2

    if not args.yes:
        answer = input(
            "This test will erase all KERN-LITE log files at the end. Continue? [y/N]: "
        ).strip().lower()
        if answer not in {"y", "yes"}:
            print("Test cancelled.")
            return 2

    test = Phase5IntegrationTest(
        port=args.port,
        duration_s=args.duration,
        replay_count=args.replay,
    )
    return test.run()


if __name__ == "__main__":
    sys.exit(main())