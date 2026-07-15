import math
from typing import List


class LinkQualityMonitor:
    def __init__(self):
        # Counters for errors that happened in the CURRENT 5-second window
        self.crc_errors = 0
        self.sync_errors = 0
        self.seq_gaps = 0
        self.nacks = 0

        # For Jitter calculation (time between consecutive frames)
        self.last_frame_time = None
        self.intervals: List[float] = []

        # For Heartbeat checking
        self.last_valid_frame_time = None

        # The final computed states to be exposed to the UI
        self.quality_pct = 100.0
        self.degraded = False
        self.poor = False

    def record_frame(self, wall_time: float, is_valid: bool = True) -> None:
        """
        Called every time a frame (valid or invalid) arrives over UART.
        """
        if is_valid:
            self.last_valid_frame_time = wall_time

        # Calculate time interval from the last frame for jitter analysis
        if self.last_frame_time is not None:
            interval = wall_time - self.last_frame_time
            self.intervals.append(interval)

        self.last_frame_time = wall_time

    def record_error(self, error_type: str, count: int = 1) -> None:
        """
        Increments specific error counters.
        """
        if error_type == "CRC":
            self.crc_errors += count
        elif error_type == "SYNC":
            self.sync_errors += count
        elif error_type == "GAP":
            self.seq_gaps += count
        elif error_type == "NACK":
            self.nacks += count

    def update_rates(self, crc_error_rate: float, sync_error_rate: float, sequence_gap_rate: float, nack_rate: float, jitter_error_rate: float) -> None:
        """
        Directly updates the quality score based on provided error rates.
        This satisfies the analytics tests and centralizes the math.
        """
        weighted_error = (
                (crc_error_rate * 0.30) +
                (sync_error_rate * 0.20) +
                (sequence_gap_rate * 0.25) +
                (nack_rate * 0.15) +
                (jitter_error_rate * 0.10)
        )

        raw_score = 100.0 * (1.0 - weighted_error)

        # Clamp the score between 0 and 100
        self.quality_pct = max(0.0, min(100.0, raw_score))

        # Set flags based on thresholds
        self.degraded = self.quality_pct < 70.0
        self.poor = self.quality_pct < 40.0

    def compute_score(self, total_frames_expected: int) -> float:
        """
        Calculates the 0-100% score based on error rates and weights.
        Should be called every 5 seconds.
        """
        if total_frames_expected <= 0:
            self.update_rates(0.0, 0.0, 0.0, 0.0, 0.0)
            return self.quality_pct

        # Calculate rates for this window
        crc_rate = self.crc_errors / total_frames_expected
        sync_rate = self.sync_errors / total_frames_expected
        gap_rate = self.seq_gaps / total_frames_expected
        nack_rate = self.nacks / total_frames_expected

        # Jitter calculation (Standard deviation of frame intervals)
        jitter_rate = 0.0
        if len(self.intervals) > 1:
            mean_interval = sum(self.intervals) / len(self.intervals)
            variance = sum((x - mean_interval) ** 2 for x in self.intervals) / (len(self.intervals) - 1)
            jitter_stddev = math.sqrt(variance)

            # Assume 0.1s stddev is maximum tolerable jitter (100% penalty)
            jitter_rate = min(jitter_stddev / 0.1, 1.0)

        # Delegate the actual math to our new helper method
        self.update_rates(crc_rate, sync_rate, gap_rate, nack_rate, jitter_rate)

        # Reset the window counters for the next 5-second interval
        self.crc_errors = self.sync_errors = self.seq_gaps = self.nacks = 0
        self.intervals.clear()

        return self.quality_pct

    def check_heartbeat(self, current_time: float) -> bool:
        """
        Returns True if it has been >15s since the last valid frame.
        """
        if self.last_valid_frame_time is None:
            return False
        return (current_time - self.last_valid_frame_time) > 15.0

