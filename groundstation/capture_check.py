import argparse
import struct
import time

try:
    from .crc import crc32
    from .frame import FrameType
    from .link import SerialLink
    from .telemetry import RecordDecoder
    from .commands import CommandSender
except ImportError:
    from crc import crc32
    from frame import FrameType
    from link import SerialLink
    from telemetry import RecordDecoder
    from commands import CommandSender


def check_record_crc(payload: bytes) -> bool:
    stored = struct.unpack_from("<I", payload, 28)[0]
    return crc32(payload[0:28]) == stored


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--seconds", type=float, default=30.0)
    args = parser.parse_args()

    link = SerialLink()
    link.connect(args.port)

    sender = CommandSender()
    sender.send_start(link)
    time.sleep(0.5)

    records = 0
    seq_gaps = 0
    crc_fails = 0
    last_seq = None
    deadline = time.time() + args.seconds

    print(f"Capturing for {args.seconds:.0f} s on {args.port} ...")

    try:
        while time.time() < deadline:
            frame = link.receive_frame()
            if frame is None:
                time.sleep(0.005)  # avoid busy-spin when the buffer is empty
                continue

            if frame.type != FrameType.Record:
                if frame.type == FrameType.Nack:
                    print(f"<< NACK code=0x{frame.payload[0]:02X}")
                elif frame.type == FrameType.Status:
                    print(f"<< STATUS state={frame.payload[0]}")
                else:
                    print(f"<< {frame.type.name}")
                continue

            records += 1
            rec = RecordDecoder.decode(frame.payload)

            # --- automated check 1: record-level CRC ---
            crc_ok = check_record_crc(frame.payload)
            if not crc_ok:
                crc_fails += 1

            # --- automated check 2: seq monotonicity (mod 65536) ---
            gap = ""
            if last_seq is not None:
                expected = (last_seq + 1) % 65536
                if rec.seq != expected:
                    seq_gaps += 1
                    gap = f"  << SEQ GAP (expected {expected})"
            last_seq = rec.seq

            # --- live display (task 2) ---
            print(
                f"seq={rec.seq:5d}  "
                f"lm35={rec.lm35_celsius:6.1f}C  "
                f"dht={rec.dht_temp_celsius:5.1f}C/{rec.dht_humidity:5.1f}%  "
                f"light={rec.light_normalized:.3f}  "
                f"pot={rec.pot_normalized:.3f}  "
                f"alerts=0x{rec.alert_bits:02X} faults=0x{rec.fault_bits:02X}  "
                f"crc={'OK' if crc_ok else 'FAIL'}{gap}"
            )
    finally:
        link.disconnect()

    # --- gate summary ---
    print("\n----- capture summary -----")
    print(f"records received : {records}")
    print(f"sequence gaps    : {seq_gaps}")
    print(f"record CRC fails : {crc_fails}")
    print(f"link crc errors  : {link.crc_error_count}")
    print(f"link sync errors : {link.sync_error_count}")

    gate_pass = records > 0 and seq_gaps == 0 and crc_fails == 0 and link.crc_error_count == 0
    print(f"\nDAY 3 GATE: {'PASS' if gate_pass else 'FAIL'}")


if __name__ == "__main__":
    main()