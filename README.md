## M45 Core Firmware

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![GitHub release](https://img.shields.io/github/v/release/Distortions81/M45-Core-Firmware?sort=semver)](https://github.com/Distortions81/M45-Core-Firmware/releases)
[![Release firmware](https://github.com/Distortions81/M45-Core-Firmware/actions/workflows/release-firmware.yml/badge.svg)](https://github.com/Distortions81/M45-Core-Firmware/actions/workflows/release-firmware.yml)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5.3-red)](https://github.com/espressif/esp-idf/releases/tag/v5.5.3)
[![Target: ESP32](https://img.shields.io/badge/target-ESP32-blue)](https://www.espressif.com/en/products/socs/esp32)

M45 Core Firmware is ESP-IDF firmware for classic ESP32 boards that run a
plain-TCP Stratum SHA-256 miner, local web setup UI, and either a small OLED or
ideaspark LCD status display.

Currently, on target hardware this averages about 620 Kh/s

[Open the browser firmware flasher](https://distortions81.github.io/M45-Core-Firmware/)
or read [flashing details](docs/FLASHING.md).

## Supported Hardware

- Chip: ESP32-WROOM-32 or another classic ESP32 target with hardware SHA.
- Default display: SSD1306 128x64 I2C OLED at address `0x3c`.
- OLED pins: SDA `GPIO5`, SCL `GPIO4`, reset `GPIO16`.
- Unsupported OLED lookalikes: ESP32 OLED boards wired to SDA `GPIO21` and
  SCL `GPIO22` need a custom build or firmware port.
- LCD build: ideaspark ESP32 1.9 inch LCD with ST7789 controller, 320x170.
- Unsupported LCD lookalikes: ESP32-C3, ESP32-S3, and ESP32-1732S019 boards
  need a separate firmware port and cannot run the current release binaries.
- Flash: 4 MB ESP32 flash layout with bootloader at `0x1000`, partition table
  at `0x8000`, settings/NVS at `0x9000..0xEFFF`, and app at `0x10000`.

| OLED Display | ideaspark 1.9 inch LCD | Web Stats | Web Settings |
| --- | --- | --- | --- |
| ![M45 Core OLED display](oled.png) | ![M45 Core ideaspark 1.9 inch LCD display](lcd.png) | ![M45 Core stats web UI](stats.png) | ![M45 Core settings web UI](settings.png) |

[Example block-found screens](https://www.youtube.com/watch?v=FBJSWs7Cxi0)

## Buy Links

Prices change by region, shipping destination, selected variant, coupons, and
account state. Check each listing directly before ordering.

| Firmware build | Source | Listing | Price | Note |
| --- | --- | --- | ---: | --- |
| OLED/default | Amazon | [SSD1306 128x64 I2C OLED](https://www.amazon.com/dp/B0BFDHWZB8) | US $11.99 | Supported OLED board. |
| LCD | Amazon | [ideaspark ESP32 1.9 inch LCD](https://www.amazon.com/dp/B0D6QXC813) | US $15.99 | Supported ideaspark LCD board. |

## Firmware Features

- First-boot setup access point with captive portal helpers and Wi-Fi scanning.
- Web UI for live mining stats, pool settings, display settings, reboot, and
  factory reset.
- Display pages for setup QR, Wi-Fi/IP status, pool status, hashrate, shares,
  best share, and block-found alerts.
- Primary and backup Stratum pool support with configurable host, port, worker,
  wallet, and suggested difficulty.
- Coinbase payout visibility in the stats UI when pool job data can be checked.
- CPU performance mode, display sleep, brightness, flip, theme, and LCD text
  outline controls.
- Browser flasher that verifies release checksums and can preserve existing
  Wi-Fi and pool settings during upgrades.

## Current Build Snapshot

These figures are from a local `v0.0.5` build with ESP-IDF `v5.5.3`.

| Build | App binary | IRAM | DRAM |
| --- | ---: | ---: | ---: |
| OLED/default | 725.41 KiB | 107.97 KiB | 52.37 KiB |
| ideaspark LCD | 956.97 KiB | 112.08 KiB | 107.89 KiB |
| synthetic benchmark | 701.58 KiB | 84.90 KiB | 51.77 KiB |

Host-visible source and web/script support code is roughly 15k lines.

## Requirements

- Official ESP-IDF checkout, tested with ESP-IDF `v5.5.3`.
- Python 3.
- `git` for the ESP-IDF setup helper.
- Pillow only when building LCD image/font assets for the ideaspark LCD target.
- Chrome or Edge on desktop for Web Serial browser flashing.

PlatformIO-packaged ESP-IDF is intentionally rejected by the helper scripts.
Use the official ESP-IDF checkout, or set `IDF_PATH` / `IDF_EXPORT_SCRIPT` to
one.

## Quick Start

Install or verify ESP-IDF:

```sh
./scripts/setup-esp-idf.sh
./scripts/check-environment.sh
```

Build the default OLED firmware:

```sh
./scripts/build-firmware.sh
```

Build the ideaspark LCD firmware:

```sh
./scripts/build-firmware.sh --ideaspark-19-lcd
```

Flash through ESP-IDF:

```sh
./scripts/flash-firmware.sh --port /dev/ttyUSB0
```

Use `COM4`-style ports on Windows and `/dev/cu.*` ports on macOS.

## Device Setup

On first boot, the device starts an open setup Wi-Fi network named like
`m-ABCD`. Join that network and open the shown address, usually `10.x.x.1`.
The setup page scans for nearby Wi-Fi networks, tests the selected credentials,
saves them, and reboots.

After the device joins your Wi-Fi, open the IP shown on the display. The web UI
serves:

- `/` or `/stats`: live hashrate, pool, share, payout, memory, and build data.
- `/settings`: display, performance, pool, wallet, reboot, and factory reset.
- `/wifi`: change Wi-Fi after setup.
- `/licenses`: bundled license texts and notices.
- `/health`: plain `ok` health check.

The BOOT button advances display pages. Hold BOOT for 5 seconds at startup or
while running to factory reset settings.

More device-use details are in [Device usage](docs/USAGE.md).

## Development Notes

- User-facing shell commands live in [scripts](scripts/README.md).
- Web UI source lives in `web/` and is embedded during the CMake build.
- Firmware source lives in `src/`; large modules are split into `.inc` files
  and registered as object dependencies in `src/CMakeLists.txt`.
- Release builds create merged flash images for OLED and LCD variants, attach
  them to GitHub releases, and update the GitHub Pages flasher manifest.
- TLS is disabled in the current sdkconfig to reduce memory pressure. See
  [TLS support notes](TLS-support.md) for design work if TLS Stratum support is
  revisited.

Useful development builds:

```sh
./scripts/build-firmware.sh --synthetic
./scripts/build-firmware.sh --test-block-found
./scripts/build-firmware.sh --ideaspark-19-lcd --lcd-debug-dirty-rects
```

## License

Project-owned firmware, scripts, web UI, and documentation are MIT licensed.
See [LICENSE](LICENSE), [ATTRIBUTION.md](ATTRIBUTION.md), and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
