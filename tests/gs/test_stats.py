# how to run: "python -m pytest test_stats.py -v"

import numpy as np
import pytest
import sys
import os

# Add root folder to path so Python finds 'groundstation'
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '../..')))

from groundstation.stats import ChannelStats


def test_incremental_statistics():
    stats = ChannelStats()

    values = [10.0, 20.0, 30.0, 40.0, 50.0]

    for seq, value in enumerate(values):
        stats.update(
            value=value,
            seq=seq,
            timestamp_s=float(seq),
            alert_active=False,
        )

    assert stats.n == 5
    assert stats.mean == pytest.approx(30.0)

    assert stats.min_val == pytest.approx(10.0)
    assert stats.max_val == pytest.approx(50.0)

    # Sample standard deviation: divide by n - 1.
    assert stats.stddev == pytest.approx(15.81, abs=0.01)

    # The incremental calculation must agree with NumPy.
    expected_stddev = np.std(values, ddof=1)
    assert stats.stddev == pytest.approx(expected_stddev, abs=1e-6)


def test_alert_time_accumulation():
    stats = ChannelStats()

    samples = [
        # timestamp, alert_active
        (0.0, False),
        (1.0, True),
        (2.0, True),
        (3.0, True),
        (4.0, False),
    ]

    for seq, (timestamp_s, alert_active) in enumerate(samples):
        stats.update(
            value=25.0,
            seq=seq,
            timestamp_s=timestamp_s,
            alert_active=alert_active,
        )

    assert stats.alert_activations == 1
    assert stats.time_in_alert_s == pytest.approx(2.0)