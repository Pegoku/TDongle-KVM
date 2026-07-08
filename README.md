# TDongle-KVM

Firmware for a LilyGo T-Dongle S3 that creates a Wi-Fi access point and browser UI for controlling the USB host computer as a keyboard and mouse. It behaves like a no-video KVM: the dongle is plugged into the target computer over USB, and another device connects to the dongle AP to send keyboard/mouse events.

## Features

- Standalone Wi-Fi AP with captive-portal style redirects.
- Browser UI served directly from the dongle.
- USB HID keyboard events, text typing, and common shortcuts.
- USB HID mouse movement, clicks, and wheel scrolling.
- Optional screenshot preview in the browser UI when a host-side screenshot sender is running.
- PlatformIO/Arduino C++ firmware for ESP32-S3.

## Build and Flash

Install [PlatformIO](https://platformio.org/) and connect the T-Dongle S3 over USB.

```sh
pio run
pio run -t upload
pio device monitor
```

The project uses the generic `esp32-s3-devkitc-1` PlatformIO board because the T-Dongle S3 is an ESP32-S3 board and the firmware only needs Wi-Fi plus native USB HID.

## Use

1. Flash the firmware.
2. Plug the T-Dongle S3 into the computer you want to control.
3. Connect a phone/laptop to Wi-Fi:
   - SSID: `TDongle-KVM`
   - Password: `tdongle123`
4. Open `http://192.168.4.1/`. Most devices should also be redirected by the captive portal.
5. Press `Start Control` in the web UI to capture keyboard and mouse together.
6. Press `Shift+Esc` to quit control mode and release all keys/buttons.

## Screen Preview

The T-Dongle S3 cannot read the target computer's display through USB HID. Screenshot preview therefore needs a small program running on the computer being controlled. The script captures the desktop once per second, compresses it to JPEG, and sends it to the dongle over USB serial. The dongle then serves the latest frame in the web UI.

Install the Python dependencies on the controlled computer:

```sh
python -m pip install -r scripts/requirements.txt
```

Run the sender:

```sh
python scripts/screenshot_sender.py
```

If auto-detection does not pick the dongle serial port, pass it explicitly:

```sh
python scripts/screenshot_sender.py --port /dev/ttyACM0
python scripts/screenshot_sender.py --port COM5
```

Useful options:

```sh
python scripts/screenshot_sender.py --interval 1 --max-width 960 --quality 45
```

For true no-software video, this project would need additional hardware such as an HDMI capture path. The T-Dongle S3 alone is suitable for USB HID control and low-rate preview frames, not full KVM video capture.

## Notes

- Keyboard mapping is best with a US keyboard layout on the host computer.
- Control mode uses browser pointer lock when available. Browsers may also exit pointer lock with `Esc`; `Shift+Esc` is the intended release shortcut because it also tells the dongle to release all HID inputs.
- The browser must keep the page focused to send key release events correctly. The firmware also releases all keys when requested from the UI or when control mode exits.
- This is intentionally local-only. Anyone with the AP password can control the attached computer, so change the credentials in `src/main.cpp` before using it around other people.

## Repository Description

LilyGo T-Dongle S3 firmware for browser-based remote USB keyboard and mouse control over a local Wi-Fi AP.
