# BLE Remote -> USB HID Bridge (Zwift Navigation Remote)

Ponte que le um controle de guidao BLE (DQX-Q7) e o reemite ao PC como
teclado USB HID, para navegar a interface do Zwift (setas / Enter / Esc).

Especificacao completa (hardware, protocolo BLE, mapa de teclas, roadmap
e riscos conhecidos): ver [project.md](project.md).

## Ambiente

- **Board:** ESP32-S3 DevKit N16R8
- **Toolchain:** PlatformIO (`platform = espressif32`, `framework = arduino`)
- **Libs:** NimBLE-Arduino, ArduinoJson v7, TinyUSB (embutido no core)

## Build

```sh
pio run                 # compila
pio run -t upload       # grava via UART (COM4)
pio run -t uploadfs     # grava /data/config.json no LittleFS
pio device monitor      # log serial (COM4, 115200)
```

Ver project.md secao 10 para detalhes sobre as duas portas USB do DevKit S3
(UART de gravacao vs. USB nativa que vira teclado HID).
