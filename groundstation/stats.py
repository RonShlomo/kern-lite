import math

class ChannelStats:
    """
    Maintains incremental statistics for a single sensor channel.
    This avoids storing all historical data in memory.
    """

    def __init__(self):
        # Basic counters
        self.n = 0

        # Min/Max tracking (initialized to extreme opposites)
        self.min_val = float('inf')
        self.min_seq = 0

        self.max_val = float('-inf')
        self.max_seq = 0

        # Welford's algorithm variables for mean and variance
        self.mean = 0.0
        self.M2 = 0.0

        # Alert tracking
        self.alert_activations = 0
        self.time_in_alert_s = 0.0

        # Internal state to track transitions and time differences
        self._is_in_alert = False
        self._last_timestamp_s = None

    def update(self, value: float, seq: int, timestamp_s: float, alert_active: bool):
        """
        Updates the statistics with a new incoming value.
        """
        self.n += 1

        # Update Min and Max
        if value < self.min_val:
            self.min_val = value
            self.min_seq = seq

        if value > self.max_val:
            self.max_val = value
            self.max_seq = seq

        # Welford's Online Algorithm for Mean and Variance
        delta = value - self.mean
        self.mean += delta / self.n
        delta2 = value - self.mean
        self.M2 += delta * delta2

        # Alert Tracking (Transitions and Duration)
        if self._last_timestamp_s is not None:
            time_diff = timestamp_s - self._last_timestamp_s
            # FIX: Only add time if it was in alert previously AND is still in alert now!
            if self._is_in_alert and alert_active:
                self.time_in_alert_s += time_diff

        # Check if we just ENTERED an alert state (transition from False to True)
        if alert_active and not self._is_in_alert:
            self.alert_activations += 1

        # Update our state tracker for the next round
        self._is_in_alert = alert_active
        self._last_timestamp_s = timestamp_s

    @property
    def stddev(self) -> float:
        """
        Calculates and returns the current standard deviation.
        """
        # Variance requires at least 2 data points (n-1)
        if self.n < 2:
            return 0.0

        variance = self.M2 / (self.n - 1)
        return math.sqrt(variance)

    def pct_in_alert(self, session_duration_s: float) -> float:
        """
        Calculates what percentage of the session time was spent in alert.
        """
        if session_duration_s <= 0:
            return 0.0

        return (self.time_in_alert_s / session_duration_s) * 100.0