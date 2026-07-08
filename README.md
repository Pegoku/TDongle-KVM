# TDongle-KVM

Firmware for a LilyGo T-Dongle S3 that creates a Wi-Fi access point and browser UI for controlling the USB host computer as a keyboard and mouse. It behaves like a no-video KVM: the dongle is plugged into the target computer over USB, and another device connects to the dongle AP to send keyboard/mouse events.

## Features

- Standalone Wi-Fi AP with captive-portal style redirects.
- Browser UI served directly from the dongle.
- USB HID keyboard events, text typing, and common shortcuts.
- USB HID mouse movement, clicks, and wheel scrolling.
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
5. Click the keyboard capture area before typing.

## Notes

- Keyboard mapping is best with a US keyboard layout on the host computer.
- The browser must keep the page focused to send key release events correctly. The firmware also releases all keys when requested from the UI.
- This is intentionally local-only. Anyone with the AP password can control the attached computer, so change the credentials in `src/main.cpp` before using it around other people.

## Repository Description

LilyGo T-Dongle S3 firmware for browser-based remote USB keyboard and mouse control over a local Wi-Fi AP.
