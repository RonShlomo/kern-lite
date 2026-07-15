# how to run: "python -m pytest test_link_quality.py -v"

import pytest
import logging
import sys
import os

# Add root folder to path so Python finds 'groundstation'
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '../..')))

from groundstation.commands import StatusPoller
from groundstation.link_quality import LinkQualityMonitor


def test_clean_link_has_perfect_quality():
    monitor = LinkQualityMonitor()

    monitor.update_rates(
        crc_error_rate=0.0,
        sync_error_rate=0.0,
        sequence_gap_rate=0.0,
        nack_rate=0.0,
        jitter_error_rate=0.0,
    )

    assert monitor.quality_pct == pytest.approx(100.0)
    assert monitor.degraded is False
    assert monitor.poor is False


def test_quality_uses_documented_weights():
    monitor = LinkQualityMonitor()

    monitor.update_rates(
        crc_error_rate=0.10,
        sync_error_rate=0.05,
        sequence_gap_rate=0.05,
        nack_rate=0.0,
        jitter_error_rate=0.0,
    )

    # Weighted error:
    # 0.10 * 0.30 +
    # 0.05 * 0.20 +
    # 0.05 * 0.25 = 0.0525
    #
    # Quality = 100 * (1 - 0.0525) = 94.75
    assert monitor.quality_pct == pytest.approx(94.75)


def test_significant_errors_degrade_link_quality():
    monitor = LinkQualityMonitor()

    monitor.update_rates(
        crc_error_rate=0.60,
        sync_error_rate=0.40,
        sequence_gap_rate=0.40,
        nack_rate=0.20,
        jitter_error_rate=0.20,
    )

    assert monitor.quality_pct < 70.0
    assert monitor.degraded is True
    assert monitor.poor is False


class FakeLink:
    connection_state = "connected"


class FakeCommandSender:
    def __init__(self):
        self.status_send_count = 0

    def send_status(self, link):
        self.status_send_count += 1


def test_reboot_detector_reissues_status(caplog):
    link = FakeLink()
    command_sender = FakeCommandSender()

    poller = StatusPoller(
        link=link,
        command_sender=command_sender,
        poll_interval_s=5.0,
    )

    # First STATUS establishes the previous record count.
    poller.process_incoming_status(1000)

    assert poller.last_total_records == 1000
    assert command_sender.status_send_count == 0

    # A large drop in total_records indicates a probable reboot.
    with caplog.at_level(logging.WARNING, logger="StatusPoller"):
        poller.process_incoming_status(3)

    assert "REBOOT_DETECTED" in caplog.text
    assert command_sender.status_send_count == 1
    assert poller.last_total_records == 3