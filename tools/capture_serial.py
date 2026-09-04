#!/usr/bin/env python3
"""Reset an ESP32 USB serial device and capture its boot log."""

import argparse
import time

import serial


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("port")
    parser.add_argument("--seconds", type=float, default=8.0)
    args = parser.parse_args()

    device = serial.Serial(args.port, 115200, timeout=0.2)
    try:
        device.dtr = False
        device.rts = False
        time.sleep(0.05)
        device.rts = True
        time.sleep(0.1)
        device.rts = False
        deadline = time.monotonic() + args.seconds
        chunks = []
        while time.monotonic() < deadline:
            data = device.read(4096)
            if data:
                chunks.append(data)
        print(b"".join(chunks).decode("utf-8", errors="replace"), end="")
    finally:
        device.close()


if __name__ == "__main__":
    main()
