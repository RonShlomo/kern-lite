from enum import IntEnum
import time
from typing import Optional
import serial
import threading
from groundstation.frame import Frame, encode, Decoder, DecodeResult, FrameType
from collections import deque


class ConnectionState(IntEnum):
    CONNECTED = 0
    DISCONNECTED = 1
    RECONNECTING = 2


class SerialLink:
    def __init__(self):
        self.ser = None
        self.state = ConnectionState.DISCONNECTED
        self.tx_count = 0
        self.rx_count = 0
        self.crc_error_count = 0
        self.sync_error_count = 0
        self.nack_count = 0
        self.last_latency_ms = 0
        self.cmd_count = 0
        self.rx_buffer = bytearray()
        self.cmd_type = None
        self.time_stamp = None
        self.decoder = Decoder()
        self.latency_samples = deque(maxlen=20)

        # reconnect machinery
        self._port = None
        self._baud = 115200
        self._stop_event = threading.Event()   # create it here, not None
        self._reconnect_thread = None

    def connect(self, port: str, baud: int = 115200):
        self._port = port
        self._baud = baud
        self.ser = serial.Serial(port=port, baudrate=baud, timeout=0.05)
        self.state = ConnectionState.CONNECTED

        if self._reconnect_thread is None:
            self._stop_event.clear()
            self._reconnect_thread = threading.Thread(
                target=self._reconnect_loop, daemon=True)
            self._reconnect_thread.start()

    def disconnect(self):
        self._stop_event.set()
        self._reconnect_thread = None
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.ser = None
        self.state = ConnectionState.DISCONNECTED

    def _on_link_error(self):
        """Mark the link broken and drop the dead port object."""
        self.state = ConnectionState.RECONNECTING
        try:
            if self.ser:
                self.ser.close()
        except serial.SerialException:
            pass
        self.ser = None

    def _reconnect_loop(self):
        while not self._stop_event.wait(2.0):
            if self.state != ConnectionState.RECONNECTING:
                continue
            try:
                self.ser = serial.Serial(
                    port=self._port, baudrate=self._baud, timeout=0.05)
                self.state = ConnectionState.CONNECTED
            except serial.SerialException:
                pass

    def send_frame(self, frame: Frame):
        if self.ser and self.ser.is_open:
            try:
                data = encode(frame)
                self.ser.write(data)
                self.tx_count += 1
                self.time_stamp = time.time()
                self.cmd_type = frame.type
                self.cmd_count += 1
            except serial.SerialException:
                self._on_link_error()

    def receive_frame(self) -> Optional[Frame]:
        if self.ser and self.ser.is_open:
            try:
                data = self.ser.read(self.ser.in_waiting or 1)
                self.rx_buffer.extend(data)
            except serial.SerialException:
                self._on_link_error()

        while self.rx_buffer:
            b = self.rx_buffer.pop(0)
            result = self.decoder.feed(b)

            if result == DecodeResult.FrameReady:
                self.rx_count += 1

                if self.time_stamp is not None:
                    self.last_latency_ms = (time.time() - self.time_stamp) * 1000
                    self.latency_samples.append(self.last_latency_ms)
                    self.time_stamp = None
                    self.cmd_type = None

                frame = self.decoder.frame()
                if frame.type == FrameType.Nack:
                    self.nack_count += 1
                return frame

            elif result == DecodeResult.CrcError:
                self.crc_error_count += 1
            elif result == DecodeResult.SyncError:
                self.sync_error_count += 1

        return None

    @property
    def rolling_avg_latency_ms(self) -> float:
        if not self.latency_samples:
            return 0.0
        return sum(self.latency_samples) / len(self.latency_samples)

    @property
    def nack_rate(self) -> float:
        if self.cmd_count == 0:
            return 0.0
        return self.nack_count / self.cmd_count