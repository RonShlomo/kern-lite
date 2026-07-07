import argparse
import struct
import sys
import time
import zlib

import serial

from groundstation.commands import CommandSender
from groundstation.link import SerialLink
from groundstation.frame import FrameType, NackCode

STATE_NAMES = {0: "Idle", 1: "Recording", 2: "Fault"}


def wait_for(link, wanted_type, timeout_s=2.0):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        f = link.receive_frame()
        if f is not None and f.type == wanted_type:
            return f
    return None


def send_raw_bad_opcode(link):
    """Opcode 0xFF isn't in FrameType, so build the frame bytes by hand."""
    region = bytes([0xFF]) + struct.pack("<H", 0)
    raw = (bytes([0xAB]) + region
           + struct.pack("<I", zlib.crc32(region) & 0xFFFFFFFF)
           + bytes([0xCD]))
    link.ser.write(raw)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    args = parser.parse_args()

    link = SerialLink()
    try:
        link.connect(args.port)
    except serial.SerialException:
        from serial.tools import list_ports
        print(f"Could not open {args.port}. Available ports:")
        for p in list_ports.comports():
            print(f"  {p.device} - {p.description}")
        sys.exit(1)

    print(f"Connected to {args.port}")

    # --- Check 1: CMD_STATUS -> STATUS ---
    CommandSender().send_status(link)
    status = wait_for(link, FrameType.Status)
    if status is None:
        print("FAIL: no STATUS within 2 s")
        print(f"tx={link.tx_count} rx={link.rx_count} "
              f"crc_err={link.crc_error_count} sync_err={link.sync_error_count}")
        link.disconnect()
        sys.exit(1)

    (state, sd_mounted, file_count, current_file,
     total_records, wrap_count, records_in_file) = struct.unpack_from(
        "<BBBBIIH", status.payload)

    print("STATUS received:")
    print(f"  state           : {state} ({STATE_NAMES.get(state, '?')})")
    print(f"  sd_mounted      : {sd_mounted}")
    print(f"  file_count      : {file_count}")
    print(f"  current_file    : {current_file}")
    print(f"  total_records   : {total_records}")
    print(f"  wrap_count      : {wrap_count}")
    print(f"  records_in_file : {records_in_file}")
    print(f"  latency         : {link.last_latency_ms:.1f} ms")

    # --- Check 2: unknown opcode 0xFF -> NACK BadCommand ---
    send_raw_bad_opcode(link)
    nack = wait_for(link, FrameType.Nack)
    if nack is not None and nack.payload[0] == NackCode.BadCommand:
        print("NACK check: opcode 0xFF -> NACK BadCommand  OK")
    else:
        print("NACK check FAILED:", nack)

    print(f"counters: tx={link.tx_count} rx={link.rx_count} "
          f"crc_err={link.crc_error_count} sync_err={link.sync_error_count} "
          f"nack={link.nack_count}")

    link.disconnect()


if __name__ == "__main__":
    main()