from dataclasses import dataclass
from typing import List, Set


@dataclass
class LogEntry:
    """
    Represents a single event in the system journal.
    """
    category: str  # E.g., 'CRC_ERROR', 'ALERT_ACTIVE'
    wall_time: float  # Seconds since start
    session_seq: int  # The sequence number when it happened
    message: str  # Human readable description


class AlertLog:
    CATEGORIES = {
        "ALERT_ACTIVE",
        "ALERT_CLEAR",
        "STATE_TRANSITION",
        "NACK",
        "CRC_ERROR",
        "SYNC_ERROR",
        "SEQ_GAP",
        "REBOOT",
        "HEARTBEAT_TIMEOUT",
        "GS_EVENT"
    }

    def __init__(self):
        self.entries: List[LogEntry] = []

    def add(self, category: str, wall_time: float, session_seq: int, message: str) -> None:
        """
        Adds a new event to the log.
        """
        # Fallback to a general event if someone typed a wrong category
        if category not in self.CATEGORIES:
            category = "GS_EVENT"

        new_entry = LogEntry(category, wall_time, session_seq, message)
        self.entries.append(new_entry)

    def filter(self, category_set: Set[str]) -> List[LogEntry]:
        """
        Returns only logs that match the requested categories.
        """
        # List comprehension used for quick filtering
        return [entry for entry in self.entries if entry.category in category_set]

    def sort_by_time(self) -> None:
        """
        Sorts the entire log chronologically.
        """
        self.entries.sort(key=lambda entry: entry.wall_time)

    def export_text(self, path: str) -> None:
        """
        Saves the log to a readable text file.
        """
        self.sort_by_time()
        with open(path, 'w') as f:
            f.write("=== SYSTEM ALERT & EVENT LOG ===\n\n")
            for e in self.entries:
                f.write(f"[{e.wall_time:07.2f}s] SEQ:{e.session_seq:04d} | {e.category:<18} | {e.message}\n")