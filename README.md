## M45-Core-Firmware

- Average mining speed: 620 kH/s, April 2026.
- 740 KiB binary
- 108 KiB IRAM, 62 KiB DRAM
- Source size: roughly 10k LoC
- Hardware: ESP32-WROOM-32

| OLED Display | ideaspark 1.9 inch LCD | Web Stats | Web Settings |
| --- | --- | --- | --- |
| ![M45 Core OLED display](oled.png) | ![M45 Core ideaspark 1.9 inch LCD display](lcd.png) | ![M45 Core stats web UI](stats.png) | ![M45 Core settings web UI](settings.png) |

## Requirements

- Official ESP-IDF checkout, tested with ESP-IDF v5.5.3.
- Python 3.
- Pillow only when building LCD image/font assets for the ideaspark LCD target.

Install or verify ESP-IDF:

```sh
./scripts/setup-esp-idf.sh
./scripts/check-environment.sh
```

## Build

Default firmware:

```sh
./scripts/build-firmware.sh
```

ideaspark ESP32 1.9 inch LCD firmware:

```sh
./scripts/build-firmware.sh --ideaspark-19-lcd
```
