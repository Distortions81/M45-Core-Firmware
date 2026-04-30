# Scripts

Top-level shell scripts are user-facing commands:

- `setup-esp-idf.sh`: install or prepare the ESP-IDF toolchain.
- `check-environment.sh`: verify the ESP-IDF environment and project helper tools.
- `build-firmware.sh`, `flash-firmware.sh`: build and flash firmware.
- `monitor-serial.sh`: open the ESP-IDF serial monitor.
- `show-firmware-size.sh`: print ESP-IDF and human-readable firmware size summaries.
- `clean-builds.sh`: remove generated build directories.
- `benchmark-web.sh`, `benchmark-hashrate.sh`: host-side device benchmarks.

Internal support code lives in subdirectories:

- `lib/`: shared shell helpers.
- `tools/`: Python helpers used by CMake and the shell scripts.
