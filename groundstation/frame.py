import struct
from dataclasses import dataclass
from enum import IntEnum

try:
    from .crc import crc32
except ImportError:
    from crc import crc32

STX: int = 0xAB
ETX: int = 0xCD
MAX_PAYLOAD: int = 256
FRAME_OVERHEAD: int = 9

class FrameType(IntEnum):
    CmdStart = 0x01
    CmdStop = 0x02
    CmdStatus = 0x03
    CmdReplay = 0x04
    CmdErase = 0x06
    Record = 0x10
    Status = 0x12
    Ack = 0x20
    Nack = 0x21

class NackCode(IntEnum):
    CrcError=0x01
    BadCommand=0x02
    InvalidState=0x03
    StorageError=0x04
    BadMagic=0x06


@dataclass
class Frame:
    type: FrameType
    payload: bytes = b""
    state: int = 0
    sd_mounted: int = 0
    file_count: int = 0
    current_file: int = 0
    total_records: int = 0
    wrap_count: int = 0
    records_in_file: int = 0

class _State(IntEnum):
    WaitStx = 0
    Type    = 1
    LenLo   = 2
    LenHi   = 3
    Payload = 4
    Crc0    = 5
    Crc1    = 6
    Crc2    = 7
    Crc3    = 8
    WaitEtx = 9

def encode(frame: Frame) -> bytes:
    payload = frame.payload
    if len(payload) > MAX_PAYLOAD:
        raise ValueError(f"payload too large: {len(payload)} > {MAX_PAYLOAD}")

    length = len(payload)
    # CRC covers TYPE + LEN + PAYLOAD
    crc_region = bytes([int(frame.type)]) + struct.pack("<H", length) + payload
    crc = crc32(crc_region)

    out = bytearray()
    out.append(STX)
    out += crc_region
    out += struct.pack("<I", crc)
    out.append(ETX)
    return bytes(out)

class DecodeResult(IntEnum):
    NeedMore   = 0
    FrameReady = 1
    CrcError   = 2
    SyncError  = 3

class Decoder:
    def __init__(self):
        self._frame = Frame(type=FrameType.Ack)
        self.reset()

    def reset(self):
        self._state = _State.WaitStx
        self._type = 0
        self._len = 0
        self._len_lo = 0
        self._payload = bytearray()
        self._crc_bytes = bytearray()

    def frame(self) -> Frame:
        return self._frame

    def feed(self, byte: int) -> DecodeResult:
        s = self._state

        if s == _State.WaitStx:
            if byte == STX:
                self.reset()
                self._state = _State.Type
                return DecodeResult.NeedMore

            return DecodeResult.NeedMore

        if s == _State.Type:
            self._type = byte
            self._state = _State.LenLo
            return DecodeResult.NeedMore

        if s == _State.LenLo:
            self._len_lo = byte
            self._state = _State.LenHi
            return DecodeResult.NeedMore

        if s == _State.LenHi:
            self._len = self._len_lo | (byte << 8)
            
            if self._len > MAX_PAYLOAD:
                self.reset()
                return DecodeResult.SyncError
            
            if self._len == 0:
                self._state = _State.Crc0
            else:
                self._state = _State.Payload
            
            return DecodeResult.NeedMore

        if s == _State.Payload:
            self._payload.append(byte)
            
            if len(self._payload) >= self._len:
                self._state = _State.Crc0
            
            return DecodeResult.NeedMore

        if s in (_State.Crc0, _State.Crc1, _State.Crc2, _State.Crc3):
            self._crc_bytes.append(byte)
            self._state = _State(s + 1)
            return DecodeResult.NeedMore

        if s == _State.WaitEtx:
            self._state = _State.WaitStx
            
            if byte != ETX:
                return DecodeResult.SyncError
            
            received_crc = struct.unpack("<I", bytes(self._crc_bytes))[0]
            crc_region = (bytes([self._type]) + struct.pack("<H", self._len) + bytes(self._payload))
            
            if crc32(crc_region) != received_crc:
                return DecodeResult.CrcError

            self._frame = Frame(type=FrameType(self._type), payload=bytes(self._payload))

            if self._frame.type == FrameType.Status and len(self._frame.payload) >= 14:
                p = self._frame.payload
                self._frame.state = p[0]
                self._frame.sd_mounted = p[1]
                self._frame.file_count = p[2]
                self._frame.current_file = p[3]
                self._frame.total_records, self._frame.wrap_count = struct.unpack_from("<II", p, 4)
                self._frame.records_in_file = struct.unpack_from("<H", p, 12)[0]

            return DecodeResult.FrameReady

        self.reset()
        return DecodeResult.SyncError