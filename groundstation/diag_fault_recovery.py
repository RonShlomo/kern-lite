"""
groundstation/diag_fault_recovery.py - A6.1 hardware test: SD write-fail -> Fault -> FaultCleared.

Boots the device, starts Recording, then polls STATUS once per second for the given
duration, printing state/sd_mounted/total_records every time and flagging any state
change. Meant to be run once, watching the terminal live while you physically pull and
reinsert the SD card mid-run:

    python -m groundstation.diag_fault_recovery --port COM4 --duration 60
"""
import argparse
import time

from .link import SerialLink
from .commands import CommandSender
from .frame import FrameType
from .cli import describe_status


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--duration", type=float, default=60.0)
    args = ap.parse_args()

    link = SerialLink()
    link.connect(args.port)
    sender = CommandSender()

    print(f"Connected to {args.port}, waiting for boot...")
    time.sleep(2.0)

    sender.send_start(link)
    print("Sent START.\n")

    t_end = time.time() + args.duration
    t_next_poll = 0.0
    last_state = None

    try:
        while time.time() < t_end:
            now = time.time()
            if now >= t_next_poll:
                sender.send_status(link)
                t_next_poll = now + 1.0

            f = link.receive_frame()
            if f is None:
                time.sleep(0.01)
                continue

            ts = time.strftime("%H:%M:%S")
            if f.type == FrameType.Status:
                state = f.payload[0]
                marker = "  <-- STATE CHANGED" if state != last_state else ""
                last_state = state
                print(f"[{ts}] {describe_status(f)}{marker}")
            elif f.type == FrameType.Ack:
                print(f"[{ts}]   ACK")
            elif f.type == FrameType.Nack:
                print(f"[{ts}]   NACK")
            # RECORD frames are ignored here -- this script is about state, not sensor data.
    finally:
        link.disconnect()

    print("\nDone.")


if __name__ == "__main__":
    main()
