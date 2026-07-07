import struct
from groundstation.frame import Frame, FrameType


class CommandSender:
    def send_start(self, link):
        link.send_frame(Frame(type=FrameType.CmdStart))

    def send_stop(self, link):
        link.send_frame(Frame(type=FrameType.CmdStop))

    def send_status(self, link):
        link.send_frame(Frame(type=FrameType.CmdStatus))

    def send_replay(self, link, n: int):
        link.send_frame(Frame(type=FrameType.CmdReplay,
                              payload=struct.pack("<H", n)))

    def send_erase(self, link, magic: int):
        link.send_frame(Frame(type=FrameType.CmdErase,
                              payload=struct.pack("<I", magic)))