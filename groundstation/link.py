from enum import IntEnum
import time
from typing import Optional
import serial
import threading
from groundstation.frame import Frame, encode, Decoder, DecodeResult, FrameType, NackCode
from collections import deque
import logging


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


    # B5.3
    logger = logging.getLogger("LinkDispatcher")
    class FrameDispatcher:
        def __init__(self, integrity_checker, telemetry_model, session, device_state, storage_model):
            # Initializes the dispatcher with all the UI models and logic handlers.
            self.integrity = integrity_checker
            self.telemetry = telemetry_model
            self.device_state = device_state
            self.storgae = storage_model

        def dispatch_frame(self, frame):
            # Routes a valid  Frame object to the correct handling logic based on its type
            # This function is called immediately after the decoder successfully reconstructs a frame
            if frame.type == FrameType.Record:
                # first, check if the record's internal CRC is corrupted
                if not self.integrity.check_frame(frame):
                    # If corrupted, the IntegrityChecker already logged a warning. We drop the packet.
                    return

                # if valid, pass the raw frame payload to the telemetry model to decode and display
                record = self.telemetry.decode(frame.payload)
                self.telemetry.ingest(record)

                # save the binary record to our session file with the current PC timestamp
                current_wall_time = time.time()
                self.session.append_record(record, current_wall_time)

                # Determine if this is a live stream or a replay based on the device state
                if self.device_state.current_state == 1:
                    self.storage.live_record_count += 1
                else:
                    self.storage.replay_record_count += 1

            elif frame.type == FrameType.Status:
                # Send the frame to both models so they can update their UI variables
                self.device_state.update_from_status(frame)
                self.storgae.update_from_status(frame)

            # Handle incoming nacks (errors)
            elif frame.type == FrameType.Nack:
                # The payload of a NACK is exactly 1 byte representing the error code
                if len(frame.payload) >= 1:
                    error_code_value = frame.payload[0]
                    try:
                        # Convert the raw number into our human-readable Enum
                        error_enum = NackCode(error_code_value)
                        logger.error(f"Command Rejected! NACK received: {error_enum.name}")
                        # Note: You would also update your Command Panel UI alert history here
                    except ValueError:
                        logger.error(f"Received unknown NACK code: {error_code_value}")


