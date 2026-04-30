# Flashing M45 Core Firmware

## Browser Flashing

Open `flash.html` from the project GitHub Pages site in Chrome or Edge on a desktop computer.
The page flashes the latest GitHub release with Web Serial. It verifies release
checksums before enabling the flash button, writes the firmware at a faster
serial speed after connecting, and can either keep settings or reset only the
settings area. In keep-settings mode it follows the same flash layout as
`idf.py flash`: bootloader at `0x1000`, partition table at `0x8000`, and app at
`0x10000`.

## Local Flashing

Download the OLED or LCD `.bin` image from the GitHub release.
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

The browser flasher is recommended when you want to preserve WiFi and pool
settings. It splits the release image in the browser and skips the settings
range at `0x9000..0xEFFF`.

On Windows, the port usually looks like `COM4`. On macOS, it usually looks like
`/dev/cu.usbserial-0001` or `/dev/cu.SLAB_USBtoUART`.

If flashing does not start, hold the board BOOT button while connecting or while
the script is trying to connect.
