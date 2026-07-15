"""
groundstation/cli.py - manual command tool for board testing.

Usage (from repo root, Windows PowerShell):
    python -m groundstation.cli --port COM4 start
    python -m groundstation.cli --port COM4 stop
    python -m groundstation.cli --port COM4 status
    python -m groundstation.cli --port COM4 replay 60
    python -m groundstation.cli --port COM4 erase 0xDEADC0DE
    python -m groundstation.cli --port COM4 listen 10   # just listen for 10 s

After sending a command it listens for `--watch` seconds (default 5) and
prints every frame that arrives: STATUS decoded, RECORD count + a sample,
ACK/NACK explicitly.
"""
import argparse
import struct
import time

from .link import SerialLink
from .commands import CommandSender
from .frame import Frame, FrameType, NackCode


FRESULT_NAMES = {
    0: "FR_OK", 1: "FR_DISK_ERR", 2: "FR_INT_ERR", 3: "FR_NOT_READY",
    4: "FR_NO_FILE", 5: "FR_NO_PATH", 6: "FR_INVALID_NAME", 7: "FR_DENIED",
    8: "FR_EXIST", 9: "FR_INVALID_OBJECT", 10: "FR_WRITE_PROTECTED",
    11: "FR_INVALID_DRIVE", 12: "FR_NOT_ENABLED", 13: "FR_NO_FILESYSTEM",
    14: "FR_MKFS_ABORTED", 15: "FR_TIMEOUT", 16: "FR_LOCKED",
    17: "FR_NOT_ENOUGH_CORE", 18: "FR_TOO_MANY_OPEN_FILES", 19: "FR_INVALID_PARAMETER",
}


def describe_status(f: Frame) -> str:
    p = f.payload
    state, sd, fc, cf = p[0], p[1], p[2], p[3]
    total, wraps = struct.unpack_from("<II", p, 4)
    rif = struct.unpack_from("<H", p, 12)[0]
    names = {0: "Idle", 1: "Recording", 2: "Fault"}
    base = (f"STATUS state={state}({names.get(state,'?')}) sd={sd} files={fc} "
            f"current_file={cf} total={total} wraps={wraps} records_in_file={rif}")

    return base


def listen(link: SerialLink, seconds: float) -> None:
    t_end = time.time() + seconds
    n_records = 0
    first_rec = last_rec = None
    while time.time() < t_end:
        f = link.receive_frame()
        if f is None:
            time.sleep(0.005)
            continue
        if f.type == FrameType.Record:
            n_records += 1
            seq = struct.unpack_from("<H", f.payload, 6)[0]
            if first_rec is None:
                first_rec = seq
            last_rec = seq
        elif f.type == FrameType.Status:
            print(" ", describe_status(f))
        elif f.type == FrameType.Ack:
            print("  ACK")
        elif f.type == FrameType.Nack:
            print(f"  NACK {NackCode(f.payload[0]).name}")
        else:
            print(f"  frame type=0x{int(f.type):02X} len={len(f.payload)}")
    if n_records:
        print(f"  RECORDs: {n_records} received, seq {first_rec}..{last_rec}")
    print(f"  counters: rx={link.rx_count} tx={link.tx_count} "
          f"crc_err={link.crc_error_count} sync_err={link.sync_error_count}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--watch", type=float, default=5.0,
                    help="seconds to listen after sending (default 5)")
    ap.add_argument("command", choices=["start", "stop", "status", "replay",
                                        "erase", "listen"])
    ap.add_argument("arg", nargs="?", default=None,
                    help="N for replay, magic for erase, seconds for listen")
    args = ap.parse_args()

    link = SerialLink()
    link.connect(args.port)
    print(f"Connected to {args.port}")
    sender = CommandSender()

    try:
        if args.command == "start":
            sender.send_start(link)
        elif args.command == "stop":
            sender.send_stop(link)
        elif args.command == "status":
            sender.send_status(link)
        elif args.command == "replay":
            n = int(args.arg) if args.arg else 120
            sender.send_replay(link, n)
        elif args.command == "erase":
            magic = int(args.arg, 0) if args.arg else 0
            sender.send_erase(link, magic)
        elif args.command == "listen":
            listen(link, float(args.arg) if args.arg else 10.0)
            return

        listen(link, args.watch)
    finally:
        link.disconnect()


if __name__ == "__main__":
    main()
