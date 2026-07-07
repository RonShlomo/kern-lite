import argparse
import time
import serial
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from groundstation.frame import Decoder, DecodeResult, FrameType


CMD_STATUS = bytes.fromhex("ab0300004b6707fdcd")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--count", type=int, default=50)
    parser.add_argument("--timeout", type=float, default=5.0)
    args = parser.parse_args()

    decoder = Decoder()

    status_count = 0
    crc_errors = 0
    sync_errors = 0
    other_frames = 0

    with serial.Serial(args.port, args.baud, timeout=0.02) as ser:
        ser.reset_input_buffer()

        start = time.time()

        for _ in range(args.count):
            ser.write(CMD_STATUS)

        ser.flush()

        deadline = time.time() + args.timeout

        while time.time() < deadline and status_count < args.count:
            chunk = ser.read(256)

            for b in chunk:
                result = decoder.feed(b)

                if result == DecodeResult.FrameReady:
                    frame = decoder.frame()

                    if frame.type == FrameType.Status and len(frame.payload) == 14:
                        status_count += 1
                    else:
                        other_frames += 1

                elif result == DecodeResult.CrcError:
                    crc_errors += 1

                elif result == DecodeResult.SyncError:
                    sync_errors += 1

        elapsed_ms = (time.time() - start) * 1000.0

    print()
    print("PHASE 2 STRESS TEST")
    print("----------------------------")
    print(f"sent CMD_STATUS: {args.count}")
    print(f"received STATUS: {status_count}")
    print(f"crc_errors: {crc_errors}")
    print(f"sync_errors: {sync_errors}")
    print(f"other_frames: {other_frames}")
    print(f"elapsed_ms: {elapsed_ms:.1f}")

    assert status_count == args.count
    assert crc_errors == 0
    assert sync_errors == 0
    assert other_frames == 0

    print()
    print("PASS: all STATUS responses came back valid")


if __name__ == "__main__":
    main()