# M45 Core Firmware

ESP-IDF firmware for the M45 Core ESP32 Bitcoin solo-mining controller. It runs
a Wi-Fi setup portal, local web UI, display UI, and Stratum mining client using
the classic ESP32 hardware SHA engine.

## Supported Targets

- Classic ESP32 only.
- Default 128x64 SSD1306 display build.
- Optional ideaspark ESP32 1.9 inch ST7789 LCD build with `--ideaspark-19-lcd`.

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

Flash and monitor:

```sh
./scripts/flash-firmware.sh --port /dev/ttyUSB0 --monitor
```

## First Run

On first boot the device starts a setup access point named like `m-1234`.
Connect to it and open:

```text
http://10.45.0.1/
```

Configure Wi-Fi, pool settings, worker name, and wallet address from the web UI.

## Scripts

Top-level scripts are documented in [scripts/README.md](scripts/README.md).
Generated build directories are ignored and can be removed with:

```sh
./scripts/clean-builds.sh
```

## Licensing

Project-owned firmware, scripts, web UI, and documentation are licensed under
the MIT License. See [LICENSE](LICENSE), [ATTRIBUTION.md](ATTRIBUTION.md), and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for bundled third-party
license notes.
