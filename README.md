# M45 Core Firmware

Firmware for the M45 Core ESP32-WROOM-32 with a 128x64 SSD1306 OLED display,
plus an optional ideaspark ESP32 1.9 inch ST7789 LCD build.

## Web UI

The local web UI shows live mining stats, connection status, memory details,
and device settings from a phone or desktop browser.

| Stats | Settings |
| --- | --- |
| ![M45 Core stats web UI](stats.png) | ![M45 Core settings web UI](settings.png) |

## Display

The default build drives a compact 128x64 OLED status display for hashrate,
pool status, uptime, and setup details.

![M45 Core OLED display](oled.png)

## First Run

On first boot the device starts a setup access point named like `m-1234`.
Connect to it and open:

```text
http://10.45.0.1/
```

Configure Wi-Fi, pool settings, worker name, and wallet address from the web UI.

## Supported Targets

- ESP32-WROOM-32 / classic ESP32 only.
- Default 128x64 SSD1306 display build.
- Optional ideaspark ESP32 1.9 inch ST7789 LCD build with `--ideaspark-19-lcd`.

## Performance

- Average mining speed: 620 kH/s, measured April 30, 2026.
- Source size: roughly 11k nonblank lines of project code.
- Default ESP32 build footprint: about 740 KiB app binary in a 1.46 MiB app
  partition, with about 108 KiB IRAM and 62 KiB DRAM used.

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
