import time

from groundstation.link import SerialLink
from groundstation.commands import CommandSender
from groundstation.frame import FrameType


PORT = "COM9"


def wait_for_frames(link, seconds: float = 1.0) -> None:
    deadline = time.time() + seconds

    while time.time() < deadline:
        frame = link.receive_frame()

        if frame is None:
            continue

        print(f"Received frame: {frame.type}")

        if frame.type == FrameType.Status:
            print(f"  state: {frame.state}")
            print(f"  sd_mounted: {frame.sd_mounted}")
            print(f"  total_records: {frame.total_records}")

        elif frame.type == FrameType.Ack:
            print("  ACK")

        elif frame.type == FrameType.Nack:
            print(f"  NACK payload: {frame.payload.hex()}")


def main() -> None:
    link = SerialLink()
    commands = CommandSender()

    try:
        link.connect(PORT)

        print("Sending STATUS...")
        commands.send_status(link)
        wait_for_frames(link)

        input("Press Enter to send START...")
        commands.send_start(link)
        wait_for_frames(link, 2.0)

        print("The green LED on PC6 should now be on.")
        input("Press Enter to send STOP...")

        commands.send_stop(link)
        wait_for_frames(link, 2.0)

        print("The green LED should now be off.")

    finally:
        link.disconnect()


if __name__ == "__main__":
    main()