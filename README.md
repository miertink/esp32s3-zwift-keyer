# BLE Remote -> USB HID Bridge (Zwift Navigation Remote)

Bridge that reads a BLE handlebar remote (DQX-Q7) and re-emits it to the PC
as a USB HID keyboard, to navigate the Zwift interface (arrows / Enter /
Esc).

Full specification (hardware, BLE protocol, key map, roadmap, and known
risks): see [project.md](project.md).

The button->key map and double-tap timing are fixed at compile time in
[include/config.h](include/config.h) — there is no runtime or web-based
config.

## Environment

- **Board:** ESP32-S3 DevKit N16R8
- **Toolchain:** PlatformIO (`platform = espressif32`, `framework = arduino`)
- **Libs:** NimBLE-Arduino, TinyUSB (built into the core)

## Build

```sh
pio run                 # compile
pio run -t upload       # flash over UART (COM4)
pio device monitor      # serial log (COM4, 115200)
```

See project.md section 10 for details on the DevKit S3's two USB ports
(flashing UART vs. the native USB port that becomes the HID keyboard).
