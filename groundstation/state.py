from __future__ import annotations

import time
from dataclasses import dataclass, field
from typing import Optional, NamedTuple

try:
    from .frame import Frame, FrameType
except ImportError:
    from frame import Frame, FrameType


STATE_IDLE = 0
STATE_RECORDING = 1
STATE_FAULT = 2

CMD_START = 0x01
CMD_STOP = 0x02
CMD_STATUS = 0x03
CMD_REPLAY = 0x04
CMD_ERASE = 0x06


class TransitionEntry(NamedTuple):
    wall_time: float
    session_seq: int
    from_state: str
    to_state: str
    duration_in_prev: float

@dataclass
class DeviceStateModel:
    current_state: int = STATE_IDLE
    previous_state: int = STATE_IDLE
    sd_mounted: bool = False

    transitions: list[TransitionEntry] = field(default_factory=list)

    # This represents the current record/session position known by the GS.
    # Later, when telemetry.py is wired, the receive path can update this.
    session_seq: int = 0

    last_status_wall_time: Optional[float] = None
    _state_entered_at: float = field(default_factory=time.time)

    @staticmethod
    def state_name(code: int) -> str:
        names = {
            STATE_IDLE: "Idle",
            STATE_RECORDING: "Recording",
            STATE_FAULT: "Fault",
        }
        return names.get(int(code), f"Unknown({int(code)})")

    @property
    def current_state_name(self) -> str:
        return self.state_name(self.current_state)

    @property
    def previous_state_name(self) -> str:
        return self.state_name(self.previous_state)

    def update_from_status(self, status_frame: Frame) -> None:
        payload = self._payload_bytes(status_frame)

        if len(payload) < 2:
            raise ValueError("STATUS payload must contain at least state and sd_mounted bytes")

        new_state = int(payload[0])
        self.sd_mounted = bool(payload[1])

        wall_time = time.time()
        self.last_status_wall_time = wall_time

        if new_state == self.current_state:
            self.previous_state = self.current_state
            return

        old_state = self.current_state
        duration_in_prev = max(0.0, wall_time - self._state_entered_at)

        self.previous_state = old_state
        self.current_state = new_state

        self.transitions.append(TransitionEntry
            (
                wall_time = time.time(),
                session_seq = int(self.session_seq),
                from_state = self.state_name(old_state),
                to_state = self.state_name(new_state),
                duration_in_prev = duration_in_prev,
            )
        )

        self._state_entered_at = wall_time

    def command_allowed(self, cmd: FrameType) -> bool:
        cmd_value = self._enum_value(cmd)

        if cmd_value == CMD_STATUS:
            return True

        if cmd_value == CMD_START:
            return self.current_state == STATE_IDLE

        if cmd_value == CMD_STOP:
            return self.current_state == STATE_RECORDING

        if cmd_value == CMD_REPLAY:
            return self.current_state == STATE_IDLE and self.sd_mounted

        if cmd_value == CMD_ERASE:
            return self.current_state == STATE_IDLE

        return False

    def set_session_seq(self, seq: int) -> None:
        self.session_seq = int(seq)

    @staticmethod
    def _enum_value(value) -> int:
        if hasattr(value, "value"):
            return int(value.value)
        return int(value)

    @staticmethod
    def _payload_bytes(frame: Frame) -> bytes:
        payload = getattr(frame, "payload", b"")

        if isinstance(payload, bytes):
            data = payload
        else:
            data = bytes(payload)

        # If your Frame class has len, respect it.
        frame_len = getattr(frame, "len", None)
        if frame_len is not None:
            data = data[: int(frame_len)]

        return data