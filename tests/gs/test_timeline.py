# how to run: "python -m pytest test_timeline.py -v"

import pytest

import sys
import os

# Add root folder to path so Python finds 'groundstation'
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '../..')))

from groundstation.timeline import StateTimeline


def test_two_recording_bands_have_correct_duration():
    timeline = StateTimeline()

    timeline.on_state_change(
        from_state="Idle",
        to_state="Recording",
        wall_time=0.0,
        session_record_count=0,
    )

    timeline.on_state_change(
        from_state="Recording",
        to_state="Idle",
        wall_time=30.0,
        session_record_count=300,
    )

    timeline.on_state_change(
        from_state="Idle",
        to_state="Recording",
        wall_time=60.0,
        session_record_count=300,
    )

    # Close the second Recording band so its duration can be calculated.
    timeline.on_state_change(
        from_state="Recording",
        to_state="Idle",
        wall_time=90.0,
        session_record_count=600,
    )

    recording_segments = [
        segment
        for segment in timeline.segments
        if segment.state == "Recording"
    ]

    assert len(recording_segments) == 2

    assert recording_segments[0].start_wall == pytest.approx(0.0)
    assert recording_segments[0].end_wall == pytest.approx(30.0)
    assert recording_segments[0].duration_s == pytest.approx(30.0)
    assert recording_segments[0].start_seq == 0
    assert recording_segments[0].end_seq == 300
    assert recording_segments[0].record_count == 300

    assert recording_segments[1].start_wall == pytest.approx(60.0)
    assert recording_segments[1].end_wall == pytest.approx(90.0)
    assert recording_segments[1].duration_s == pytest.approx(30.0)
    assert recording_segments[1].start_seq == 300
    assert recording_segments[1].end_seq == 600
    assert recording_segments[1].record_count == 300


def test_timeline_export_contains_recording_bands_and_reboot(tmp_path):
    timeline = StateTimeline()

    timeline.on_state_change(
        from_state="Idle",
        to_state="Recording",
        wall_time=0.0,
        session_record_count=0,
    )

    timeline.on_state_change(
        from_state="Recording",
        to_state="Idle",
        wall_time=30.0,
        session_record_count=300,
    )

    timeline.on_state_change(
        from_state="Idle",
        to_state="Recording",
        wall_time=60.0,
        session_record_count=300,
    )

    timeline.on_reboot(
        seq=450,
        wall_time=75.0,
    )

    timeline.on_state_change(
        from_state="Recording",
        to_state="Idle",
        wall_time=90.0,
        session_record_count=600,
    )

    output_path = tmp_path / "timeline.txt"
    timeline.export_text(output_path)

    exported = output_path.read_text(encoding="utf-8")

    # Both Recording bands must appear in the human-readable export.
    assert exported.count("Recording") >= 2

    # The reboot must retain its sequence and timestamp information.
    assert "REBOOT" in exported.upper()
    assert "450" in exported
    assert "75" in exported