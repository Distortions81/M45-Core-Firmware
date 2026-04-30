# Flashing M45 Core Firmware

## Browser Flashing

Open `flash.html` from the project GitHub Pages site in Chrome or Edge on a desktop computer.
The page uses ESP Web Tools and flashes the latest GitHub release.

## Release Zip Flashing

Download `m45-core-<tag>-flash-bundle.zip` from the GitHub release and extract it.
Install esptool once:

```sh
python -m pip install esptool
```

List serial ports:

```sh
python flash-release.py --list-ports
```

Flash firmware and choose the image from the list:

```sh
python flash-release.py --port /dev/ttyUSB0
```

The helper checks the selected image against `SHA256SUMS` before flashing.

You can also skip the prompt:

```sh
python flash-release.py --variant oled --port /dev/ttyUSB0
python flash-release.py --variant lcd --port /dev/ttyUSB0
```

On Windows, the port usually looks like `COM4`. On macOS, it usually looks like
`/dev/cu.usbserial-0001` or `/dev/cu.SLAB_USBtoUART`.

If flashing does not start, hold the board BOOT button while connecting or while
the script is trying to connect.
