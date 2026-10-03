# CYD Headless v1

Phone-first, screenless firmware for the ESP32-2432S028 (CYD) based on the pinout already used in the device setup.

## What is included

- Phone Web UI over the ESP32 access point
- Serial CLI at 115200 8N1
- Wi-Fi AP + STA connection
- Wi-Fi scanning (SSID, RSSI, channel, encryption)
- Device info / uptime / free heap
- Safe GPIO read/write on 21, 22, 27 (and read-only 35)
- nRF24 SPI presence check using SCK=18, MISO=19, MOSI=23, CSN=27, CE=22
- Reboot and saved-Wi-Fi reset

## Not included

This build intentionally does not implement Wi-Fi deauthentication/jamming, credential capture, BadUSB payloads, or other disruptive/offensive features.

## Default phone connection

1. Flash the firmware.
2. Connect the phone to Wi-Fi AP `CYD-CTRL`.
3. Open `http://192.168.4.1` in a browser.
4. Serial is 115200 8N1.

The AP password is defined in `src/main.cpp` and can be changed before compiling.

## Build

Use PlatformIO with the `cyd_headless` environment. Target board is `esp32dev`, 4 MB flash.

## Important hardware note

The nRF24 check uses GPIO 22 and GPIO 27 as CE/CSN. Do not use those same pins for another peripheral at the same time.
