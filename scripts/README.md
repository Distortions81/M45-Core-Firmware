# Scripts

Shell scripts in this directory are the supported command-line entry points for
local setup, builds, flashing, monitoring, and host-side benchmarks. Run any
script with `--help` for its full option list.

## Environment

- `setup-esp-idf.sh`: clone or prepare the official ESP-IDF checkout. Defaults
  to `~/esp/esp-idf` and ESP-IDF `v5.5.3`.
- `check-environment.sh`: verify ESP-IDF, required project files, and Python
  helper syntax.

The shared helper code rejects PlatformIO-packaged ESP-IDF because this project
expects the official ESP-IDF checkout and export script.

## Build And Flash

- `build-firmware.sh`: build firmware, run ESP-IDF size, and print a
  human-readable size summary.
- `flash-firmware.sh`: build as needed, flash with ESP-IDF, print size output,
  and optionally open the serial monitor.
- `monitor-serial.sh`: list serial ports or open `idf.py monitor`.
- `show-firmware-size.sh`: print ESP-IDF and project size summaries for an
  existing build directory.
- `clean-builds.sh`: remove `build` and `build-*` directories.

Common build variants:

```sh
./scripts/build-firmware.sh
./scripts/build-firmware.sh --ideaspark-19-lcd
./scripts/build-firmware.sh --synthetic
./scripts/build-firmware.sh --test-block-found
```

Common flashing commands:

```sh
./scripts/flash-firmware.sh --port /dev/ttyUSB0
./scripts/flash-firmware.sh --port /dev/ttyUSB0 --ideaspark-19-lcd
./scripts/flash-firmware.sh --port /dev/ttyUSB0 --monitor
```

`build-firmware.sh` and `flash-firmware.sh` normalize generated `sdkconfig`
scheduling settings before invoking ESP-IDF so network and system tasks stay
off the miner hot path.

## Benchmarks

- `benchmark-web.sh`: send a rotation of safe HTTP requests to the device web UI
  and fail if any request errors.
- `benchmark-hashrate.sh`: collect `/api/stats` samples and print a
  conservative advertised hashrate candidate.

Examples:

```sh
./scripts/benchmark-web.sh http://192.168.1.42
./scripts/benchmark-hashrate.sh --samples hashrate.csv http://192.168.1.42
```

## Internal Helpers

- `lib/common.sh`: shared shell helpers for ESP-IDF discovery, policy checks,
  size reporting, and serial port listing.
- `tools/`: Python helpers used by CMake and shell scripts to embed web assets,
  generate build metadata, build LCD assets, enforce sdkconfig policy, and print
  size summaries.
