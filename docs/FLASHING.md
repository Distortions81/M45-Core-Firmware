# Flashing M45 Core Firmware

## Browser Flashing

Open `flash.html` from the project GitHub Pages site in Chrome or Edge on a desktop computer.
The page uses ESP Web Tools and flashes the latest GitHub release.

## Local Flashing

Download the OLED or LCD merged `.bin` image from the GitHub release.
Install esptool once:

```sh
python -m pip install esptool
```

Flash the OLED image:

```sh
python -m esptool --chip esp32 --port /dev/ttyUSB0 write_flash -z 0x0 m45-core-<tag>-oled-merged.bin
```

Flash the LCD image:

```sh
python -m esptool --chip esp32 --port /dev/ttyUSB0 write_flash -z 0x0 m45-core-<tag>-ideaspark-19-lcd-merged.bin
```

On Windows, the port usually looks like `COM4`. On macOS, it usually looks like
`/dev/cu.usbserial-0001` or `/dev/cu.SLAB_USBtoUART`.

If flashing does not start, hold the board BOOT button while connecting or while
the script is trying to connect.
