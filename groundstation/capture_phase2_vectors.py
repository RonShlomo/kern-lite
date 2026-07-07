import argparse
import time
import serial


CMD_STATUS = bytes.fromhex("AB 03 00 00 4B 67 07 FD CD")
CMD_UNKNOWN = bytes.fromhex("AB FF 00 00 FF ED D9 41 CD")


def read_response(ser, timeout_s=2.0):
    data = bytearray()
    deadline = time.time() + timeout_s

    while time.time() < deadline:
        chunk = ser.read(128)
        if chunk:
            data.extend(chunk)

            # Usually one full frame ends with ETX = 0xCD.
            # STATUS frame is 23 bytes, NACK frame is usually 10 bytes.
            if 0xCD in data:
                return bytes(data)

    return bytes(data)


def print_hex(label, data):
    print()
    print(label)
    print("-" * len(label))
    print(f"bytes={len(data)}")
    print(data.hex(" ").upper())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    with serial.Serial(args.port, args.baud, timeout=0.05) as ser:
        ser.reset_input_buffer()

        print("Sending CMD_STATUS...")
        ser.write(CMD_STATUS)
        ser.flush()
        status_response = read_response(ser)
        print_hex("CAPTURED STATUS RESPONSE", status_response)

        time.sleep(0.2)
        ser.reset_input_buffer()

        print()
        print("Sending unknown opcode 0xFF...")
        ser.write(CMD_UNKNOWN)
        ser.flush()
        nack_response = read_response(ser)
        print_hex("CAPTURED NACK RESPONSE", nack_response)


if __name__ == "__main__":
    main()