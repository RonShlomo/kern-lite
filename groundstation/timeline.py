from dataclasses import dataclass
from typing import List, Optional

@dataclass
class StateSegment:
    """Represents a continuous period of time spent in a specific state."""
    state: str
    start_wall: float
    end_wall: Optional[float] = None  # None means this segment is still active
    duration_s: float = 0.0
    start_seq: int = 0
    end_seq: int = 0
    record_count: int = 0


@dataclass
class AlertEvent:
    """
    Represents a point in time when an alert triggered or cleared.
    """
    channel: str
    active: bool
    seq: int
    wall_time: float


@dataclass
class RebootEvent:
    """
    Represents a point in time when the system rebooted.
    """
    seq: int
    wall_time: float


class StateTimeline:
    def __init__(self):
        self.segments: List[StateSegment] = []
        self.alerts: List[AlertEvent] = []
        self.reboots: List[RebootEvent] = []

        # Pointer to the currently active state band
        self._current_segment: Optional[StateSegment] = None

    def on_state_change(self, from_state: str, to_state: str, wall_time: float, session_record_count: int) -> None:
        """
        Called whenever the system transitions from one state to another.
        """
        # Close the previous segment (if one exists)
        if self._current_segment:
            self._current_segment.end_wall = wall_time
            self._current_segment.duration_s = wall_time - self._current_segment.start_wall
            self._current_segment.end_seq = session_record_count
            self._current_segment.record_count = session_record_count - self._current_segment.start_seq

            # Save the completed segment into our history list
            self.segments.append(self._current_segment)

        # Open a new segment for the new state
        self._current_segment = StateSegment(
            state=to_state,
            start_wall=wall_time,
            start_seq=session_record_count
        )

    def on_alert(self, channel: str, active: bool, seq: int, wall_time: float) -> None:
        """
        Appends an alert event to the alert track.
        """
        self.alerts.append(AlertEvent(channel, active, seq, wall_time))

    def on_reboot(self, seq: int, wall_time: float) -> None:
        """
        Records a reboot event.
        """
        self.reboots.append(RebootEvent(seq, wall_time))

    def export_text(self, path: str) -> None:
        """
        Writes the entire timeline history into a human-readable text file.
        """
        # JUNIOR TIP: 'w' means write mode. It will create the file or overwrite it.
        with open(path, 'w') as f:
            f.write("=== GROUND STATION TIMELINE REPORT ===\n\n")

            f.write("--- STATE BANDS ---\n")
            # Write all completed segments
            for seg in self.segments:
                f.write(f"State: {seg.state:<10} | Start: {seg.start_wall:.2f}s | End: {seg.end_wall:.2f}s | "
                        f"Duration: {seg.duration_s:.2f}s | Records: {seg.record_count}\n")

            # Write the currently active segment (which hasn't ended yet)
            if self._current_segment:
                f.write(f"State: {self._current_segment.state:<10} | Start: {self._current_segment.start_wall:.2f}s | "
                        f"(Currently Active) | Start Seq: {self._current_segment.start_seq}\n")

            f.write("\n--- ALERT TRACK ---\n")
            if not self.alerts:
                f.write("No alerts recorded.\n")
            for alert in self.alerts:
                status = "TRIGGERED" if alert.active else "CLEARED"
                f.write(
                    f"Time: {alert.wall_time:.2f}s | Seq: {alert.seq:04d} | Channel: {alert.channel:<8} | {status}\n")

            f.write("\n--- REBOOT EVENTS ---\n")
            if not self.reboots:
                f.write("No reboots recorded.\n")
            for reboot in self.reboots:
                f.write(f"Time: {reboot.wall_time:.2f}s | Seq: {reboot.seq:04d} | SYSTEM REBOOT DETECTED\n")