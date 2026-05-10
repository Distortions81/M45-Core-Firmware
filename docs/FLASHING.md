# Flashing M45 Core Firmware

M45 release binaries are merged ESP32 flash images. Write release `.bin` files
at address `0x0`, or use the browser flasher to split the image and preserve
settings during upgrades.

## Browser Flashing

Open the GitHub Pages flasher in Chrome or Edge on a desktop computer:

https://distortions81.github.io/M45-Core-Firmware/

The page uses Web Serial and downloads the latest GitHub release manifest. It
verifies SHA-256 checksums before enabling the flash button.

1. Connect the ESP32 by USB.
2. Choose `OLED` or `LCD`.
3. Leave `Reset WiFi and pool settings` checked for a clean install.
4. Uncheck it when upgrading and you want to keep existing Wi-Fi, pool, wallet,
   display, and system settings.
5. Click `Flash Firmware` and choose the ESP32 serial device.

When settings are preserved, the flasher follows the ESP-IDF layout and skips
only the NVS settings range:

| Region | Address |
| --- | ---: |
| Bootloader | `0x1000` |
| Partition table | `0x8000` |
| Settings/NVS | `0x9000..0xEFFF` |
| App | `0x10000` |

The browser path is the recommended upgrade flow because it can keep settings
while still replacing the bootloader, partition table, and app.

## Hardware Compatibility

Use the `OLED` image only on classic ESP32 OLED boards with the expected
SSD1306 I2C wiring: SDA `GPIO5`, SCL `GPIO4`, and reset `GPIO16`. Use the
`LCD` image only on the classic ESP32-WROOM-32 ideaspark 1.9 inch ST7789 LCD
board.

Do not flash these release images to ESP32-C3, ESP32-S3, ESP32-1732S019 LCD
boards, or ESP32 OLED boards wired to SDA `GPIO21` and SCL `GPIO22`. Those
boards need a custom build or separate firmware port.

## Local Release Flashing

Download the OLED or LCD `.bin` image from the GitHub release:

- `m45-core-<tag>-oled.bin`
- `m45-core-<tag>-ideaspark-19-lcd.bin`

Install esptool once:

```sh
python -m pip install esptool
```

Flash OLED firmware:

```sh
python -m esptool --chip esp32 --port /dev/ttyUSB0 write_flash -z \
  0x0000 m45-core-<tag>-oled.bin
```

Flash LCD firmware:

```sh
python -m esptool --chip esp32 --port /dev/ttyUSB0 write_flash -z \
  0x0000 m45-core-<tag>-ideaspark-19-lcd.bin
```

This writes the merged image from `0x0`. It does not selectively preserve the
settings range unless you split the image yourself. Use the browser flasher
when preserving Wi-Fi and pool settings matters.

## Local Development Flashing

For source builds, use the ESP-IDF helper script:

```sh
./scripts/flash-firmware.sh --port /dev/ttyUSB0
```

Flash the ideaspark LCD build:

```sh
./scripts/flash-firmware.sh --port /dev/ttyUSB0 --ideaspark-19-lcd
```

Clean before flashing:

```sh
./scripts/flash-firmware.sh --port /dev/ttyUSB0 --clean
```

Open the serial monitor after flashing:

```sh
./scripts/flash-firmware.sh --port /dev/ttyUSB0 --monitor
```

The helper builds as needed, flashes with ESP-IDF, prints ESP-IDF size output,
and prints the project human-readable size summary.

## Serial Ports

Common serial port names:

- Linux: `/dev/ttyUSB0`, `/dev/ttyACM0`
- macOS: `/dev/cu.usbserial-0001`, `/dev/cu.SLAB_USBtoUART`
- Windows: `COM4`

List detected ports:

```sh
./scripts/monitor-serial.sh --list
```

## First Boot

After a clean flash or factory reset, the device starts an open setup Wi-Fi
network named like `m-ABCD`. Join it and open the address shown on the display.
The default setup address is generated from the ESP32 Wi-Fi MAC and is usually
in the `10.x.x.1` range.

The setup page scans nearby Wi-Fi networks, verifies the selected credentials,
saves them, and reboots. After reboot, open the station IP shown on the display
to reach the stats and settings UI.

## Factory Reset

You can erase saved Wi-Fi, pool, wallet, display, and system settings in any of
these ways:

- Use the browser flasher with `Reset WiFi and pool settings` checked.
- Open `/settings` and use `Factory reset`.
- Hold the BOOT button for 5 seconds at startup.
- Hold the BOOT button for 5 seconds while the firmware is running.

## Troubleshooting

- If flashing does not start, hold BOOT while connecting or while the tool is
  trying to connect.
- If the browser does not show the serial device, use Chrome or Edge on a
  desktop computer and connect over HTTPS.
- If Linux cannot open the serial device, add your user to the serial device
  group used by your distribution, then reconnect or log in again.
- If the monitor is quiet, press EN/reset after opening the monitor.
- If the device still shows old Wi-Fi or pool settings after an upgrade, flash
  again with settings reset enabled or factory reset from the web UI.
