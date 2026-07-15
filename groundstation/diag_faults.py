"""
groundstation/diag_faults.py - one-off diagnostic for the A6.1 LED hardening work.

Listens for RECORD frames (doesn't send START -- the board should already be
recording) and decodes fault_bits/alert_bits from each one, tallying how often
each named fault appears. Run:

    python -m groundstation.diag_faults --port COM4 --duration 15
"""
import argparse
import struct
import time

from .link import SerialLink
from .commands import CommandSender
from .frame import FrameType

FAULT_NAMES = {
    0x01: "LM35_RANGE",
    0x02: "DHT_TIMEOUT",
    0x04: "DHT_BADDATA",
    0x08: "LIGHT_STUCK",
    0x10: "POT_STUCK",
}

# SensorRecord layout (firmware/storage/sensor_record.hpp), packed, 32 bytes:
# timestamp(I) ms(H) seq(H) lm35_c(h) dht_temp_c(h) dht_hum(H) light(H) pot(H)
# alert_bits(B) state(B) fault_bits(B) reserved(7s) crc32(I)
RECORD_FMT = "<IHHhhHHHBBB7sI"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--duration", type=float, default=15.0)
    args = ap.parse_args()

    link = SerialLink()
    link.connect(args.port)
    # [Claude] added: opening a new serial connection resets this board (DTR toggles reset),
    # so the device reboots to Idle right as we connect -- give it time to boot and remount
    # the SD card, then explicitly start Recording ourselves instead of assuming a previous
    # script's session is still active (it never is).
    print(f"Connected to {args.port}, waiting for boot...")
    time.sleep(2.0)
    CommandSender().send_start(link)
    print(f"Sent START, listening {args.duration}s for RECORD frames...")

    fault_counts = {name: 0 for name in FAULT_NAMES.values()}
    n_records = 0
    last_fault_bits = None
    last_light = None

    t_end = time.time() + args.duration
    try:
        while time.time() < t_end:
            f = link.receive_frame()
            if f is None:
                time.sleep(0.005)
                continue
            if f.type != FrameType.Record:
                continue

            n_records += 1
            (timestamp, ms, seq, lm35_c, dht_temp_c, dht_hum, light, pot,
             alert_bits, state, fault_bits, _reserved, crc32) = struct.unpack_from(
                RECORD_FMT, f.payload)

            last_fault_bits = fault_bits
            last_light = light

            for bit, name in FAULT_NAMES.items():
                if fault_bits & bit:
                    fault_counts[name] += 1

            if fault_bits != 0:
                names = [n for b, n in FAULT_NAMES.items() if fault_bits & b]
                print(f"  seq={seq} fault_bits=0x{fault_bits:02X} ({','.join(names)}) "
                      f"lm35_c={lm35_c/10:.1f} light={light} pot={pot} "
                      f"dht_temp_c={dht_temp_c/10:.1f} dht_hum={dht_hum/10:.1f}")
    finally:
        link.disconnect()

    print(f"\n{n_records} RECORD frames seen over {args.duration}s")
    last_fb_str = f"0x{last_fault_bits:02X}" if last_fault_bits is not None else "n/a"
    print(f"last fault_bits={last_fb_str} last light={last_light}")
    print("Fault tallies:")
    for name, count in fault_counts.items():
        pct = (100.0 * count / n_records) if n_records else 0.0
        print(f"  {name}: {count}/{n_records} ({pct:.0f}%)")


if __name__ == "__main__":
    main()
