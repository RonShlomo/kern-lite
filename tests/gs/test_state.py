import time

from groundstation.frame import Frame, FrameType
from groundstation.state import DeviceStateModel


def _frame_type(name_candidates, value):
    for name in name_candidates:
        if hasattr(FrameType, name):
            return getattr(FrameType, name)
    return FrameType(value)


STATUS = _frame_type(["STATUS", "Status"], 0x12)
CMD_START = _frame_type(["CMD_START", "CmdStart"], 0x01)
CMD_STOP = _frame_type(["CMD_STOP", "CmdStop"], 0x02)
CMD_STATUS = _frame_type(["CMD_STATUS", "CmdStatus"], 0x03)
CMD_REPLAY = _frame_type(["CMD_REPLAY", "CmdReplay"], 0x04)
CMD_ERASE = _frame_type(["CMD_ERASE", "CmdErase"], 0x06)


def make_status_frame(state: int, sd_mounted: int = 1) -> Frame:
    payload = bytes([
        state,          # state
        sd_mounted,     # sd_mounted
        4,              # file_count
        0,              # current_file
    ])

    payload += (0).to_bytes(4, "little")  # total_records
    payload += (0).to_bytes(4, "little")  # wrap_count
    payload += (0).to_bytes(2, "little")  # records_in_file

    return Frame(type=STATUS, payload=payload)


def test_state_name():
    model = DeviceStateModel()

    assert model.state_name(0) == "Idle"
    assert model.state_name(1) == "Recording"
    assert model.state_name(2) == "Fault"


def test_status_updates_state_and_sd_mounted():
    model = DeviceStateModel()

    model.update_from_status(make_status_frame(0, sd_mounted=1))

    assert model.current_state == 0
    assert model.current_state_name == "Idle"
    assert model.sd_mounted is True


def test_state_transitions_are_logged():
    model = DeviceStateModel()

    model.update_from_status(make_status_frame(0))
    time.sleep(0.001)

    model.update_from_status(make_status_frame(1))
    time.sleep(0.001)

    model.update_from_status(make_status_frame(0))
    time.sleep(0.001)

    model.update_from_status(make_status_frame(2))

    assert len(model.transitions) == 3

    assert model.transitions[0][2] == "Idle"
    assert model.transitions[0][3] == "Recording"

    assert model.transitions[1][2] == "Recording"
    assert model.transitions[1][3] == "Idle"

    assert model.transitions[2][2] == "Idle"
    assert model.transitions[2][3] == "Fault"

    for transition in model.transitions:
        duration_in_prev = transition[4]
        assert duration_in_prev >= 0.0


def test_command_gating_in_idle_with_sd_mounted():
    model = DeviceStateModel()
    model.update_from_status(make_status_frame(0, sd_mounted=1))

    assert model.command_allowed(CMD_START) is True
    assert model.command_allowed(CMD_STOP) is False
    assert model.command_allowed(CMD_REPLAY) is True
    assert model.command_allowed(CMD_ERASE) is True
    assert model.command_allowed(CMD_STATUS) is True


def test_command_gating_in_idle_without_sd_mounted():
    model = DeviceStateModel()
    model.update_from_status(make_status_frame(0, sd_mounted=0))

    assert model.command_allowed(CMD_START) is True
    assert model.command_allowed(CMD_STOP) is False
    assert model.command_allowed(CMD_REPLAY) is False
    assert model.command_allowed(CMD_ERASE) is True
    assert model.command_allowed(CMD_STATUS) is True


def test_command_gating_in_recording():
    model = DeviceStateModel()
    model.update_from_status(make_status_frame(1, sd_mounted=1))

    assert model.command_allowed(CMD_START) is False
    assert model.command_allowed(CMD_STOP) is True
    assert model.command_allowed(CMD_REPLAY) is False
    assert model.command_allowed(CMD_ERASE) is False
    assert model.command_allowed(CMD_STATUS) is True


def test_command_gating_in_fault():
    model = DeviceStateModel()
    model.update_from_status(make_status_frame(2, sd_mounted=1))

    assert model.command_allowed(CMD_START) is False
    assert model.command_allowed(CMD_STOP) is False
    assert model.command_allowed(CMD_REPLAY) is False
    assert model.command_allowed(CMD_ERASE) is False
    assert model.command_allowed(CMD_STATUS) is True