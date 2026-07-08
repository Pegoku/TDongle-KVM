#!/usr/bin/env python3
"""Send periodic desktop screenshots to TDongle-KVM over USB serial."""

from __future__ import annotations

import argparse
import io
import struct
import time
from typing import Optional

import serial
from PIL import Image
from serial.tools import list_ports
from mss import mss


MAGIC = b"TDKVIMG1"


def find_port() -> Optional[str]:
    ports = list(list_ports.comports())
    if len(ports) == 1:
        return ports[0].device

    for port in ports:
        text = " ".join(
            str(value).lower()
            for value in (port.device, port.description, port.manufacturer, port.product)
            if value
        )
        if "esp32" in text or "usb jtag" in text or "cdc" in text:
            return port.device

    return None


def capture_jpeg(screen: mss, monitor_index: int, max_width: int, quality: int) -> bytes:
    monitor = screen.monitors[monitor_index]
    shot = screen.grab(monitor)
    image = Image.frombytes("RGB", shot.size, shot.rgb)

    if max_width > 0 and image.width > max_width:
        height = round(image.height * (max_width / image.width))
        image = image.resize((max_width, height), Image.Resampling.LANCZOS)

    output = io.BytesIO()
    image.save(output, format="JPEG", quality=quality, optimize=True)
    return output.getvalue()


def send_frame(port: serial.Serial, jpeg: bytes) -> None:
    port.write(MAGIC)
    port.write(struct.pack("<I", len(jpeg)))
    port.write(jpeg)
    port.flush()


def main() -> int:
    parser = argparse.ArgumentParser(description="Send screenshots to TDongle-KVM over USB serial.")
    parser.add_argument("--port", help="Serial port, for example /dev/ttyACM0 or COM5.")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate. USB CDC ignores this on most systems.")
    parser.add_argument("--interval", type=float, default=1.0, help="Seconds between screenshots.")
    parser.add_argument("--monitor", type=int, default=1, help="mss monitor index. 1 is usually the primary display.")
    parser.add_argument("--max-width", type=int, default=960, help="Resize screenshots wider than this before sending.")
    parser.add_argument("--quality", type=int, default=45, help="JPEG quality, 1-95.")
    args = parser.parse_args()

    port_name = args.port or find_port()
    if not port_name:
        print("No serial port found. Pass --port explicitly.")
        return 2

    with serial.Serial(port_name, args.baud, timeout=1, write_timeout=5) as port, mss() as screen:
        if args.monitor < 0 or args.monitor >= len(screen.monitors):
            print(f"Monitor index {args.monitor} is invalid. Available: 0..{len(screen.monitors) - 1}")
            return 2

        print(f"Sending screenshots to {port_name}. Press Ctrl+C to stop.")
        while True:
            started = time.monotonic()
            jpeg = capture_jpeg(screen, args.monitor, args.max_width, args.quality)
            send_frame(port, jpeg)
            print(f"sent {len(jpeg)} bytes")
            elapsed = time.monotonic() - started
            time.sleep(max(0.0, args.interval - elapsed))


if __name__ == "__main__":
    raise SystemExit(main())
