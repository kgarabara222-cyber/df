# CYD Headless PlatformIO project

Target: ESP32-WROOM / CYD 2432S028, 4 MB flash.

## Build
1. Open this folder in VS Code + PlatformIO.
2. Select environment `cyd_2432s028`.
3. Run `PlatformIO: Build`.

The generated binaries are placed in:
`.pio/build/cyd_2432s028/`

Important:
- This project is configured as a normal ESP32 Arduino application.
- It does NOT require the damaged CYD display to operate.
- Do not erase flash before flashing unless you specifically need to erase old data.
- For Web Flasher, use the actual generated firmware artifacts from the build rather than renaming files manually.
