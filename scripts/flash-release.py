#!/usr/bin/env python3

import argparse
import hashlib
import subprocess
import sys
from pathlib import Path


VARIANTS = {
    "oled": "*-oled-merged.bin",
    "lcd": "*-ideaspark-19-lcd-merged.bin",
}


def fail(message):
    print(f"flash-release.py: error: {message}", file=sys.stderr)
    return 1


def run(args):
    return subprocess.run(args, check=False).returncode


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_checksums(bundle_dir):
    checksums_path = bundle_dir / "SHA256SUMS"
    if not checksums_path.exists():
        return None

    checksums = {}
    with checksums_path.open("r", encoding="utf-8") as file:
        for line_number, line in enumerate(file, start=1):
            line = line.strip()
            if not line:
                continue
            parts = line.split(None, 1)
            if len(parts) != 2 or len(parts[0]) != 64:
                raise RuntimeError(f"invalid SHA256SUMS line {line_number}")
            filename = parts[1].lstrip("*")
            checksums[filename] = parts[0].lower()
    return checksums


def verify_checksum(image, bundle_dir):
    checksums = load_checksums(bundle_dir)
    if checksums is None:
        print("SHA256SUMS not found; skipping checksum verification.")
        return

    expected = checksums.get(image.name)
    if expected is None:
        raise RuntimeError(f"SHA256SUMS has no entry for {image.name}")

    actual = sha256_file(image)
    if actual != expected:
        raise RuntimeError(
            f"checksum mismatch for {image.name}: expected {expected}, got {actual}"
        )
    print(f"Checksum OK: {image.name}")


def find_image(bundle_dir, variant):
    matches = sorted(bundle_dir.glob(VARIANTS[variant]))
    if not matches:
        raise FileNotFoundError(f"could not find {VARIANTS[variant]} in {bundle_dir}")
    if len(matches) > 1:
        raise RuntimeError(f"found multiple {variant} images: {', '.join(path.name for path in matches)}")
    return matches[0]


def list_images(bundle_dir):
    images = sorted(bundle_dir.glob("*.bin"))
    merged = [path for path in images if path.name.endswith("-merged.bin")]
    return merged or images


def choose_image(bundle_dir):
    images = list_images(bundle_dir)
    if not images:
        raise FileNotFoundError(f"could not find any .bin images in {bundle_dir}")

    print("Available firmware images:")
    for index, image in enumerate(images, start=1):
        print(f"  {index}. {image.name}")

    if len(images) == 1:
        return images[0]

    while True:
        choice = input(f"Select image [1-{len(images)}]: ").strip()
        if choice.isdigit():
            index = int(choice)
            if 1 <= index <= len(images):
                return images[index - 1]
        print("Enter one of the listed numbers.")


def list_ports():
    try:
        from serial.tools import list_ports
    except ImportError:
        return fail("pyserial is unavailable; install esptool with: python -m pip install esptool")

    ports = list(list_ports.comports())
    if not ports:
        print("No serial ports found.")
        return 0

    for port in ports:
        description = port.description or "serial device"
        print(f"{port.device}\t{description}")
    return 0


def ensure_esptool():
    result = subprocess.run(
        [sys.executable, "-m", "esptool", "version"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return result.returncode == 0


def main():
    parser = argparse.ArgumentParser(
        description="Flash a bundled M45 Core ESP32 release image.",
    )
    parser.add_argument("--variant", choices=sorted(VARIANTS), help="firmware variant to flash without prompting")
    parser.add_argument("--port", help="serial port, such as COM4, /dev/ttyUSB0, or /dev/cu.usbserial-0001")
    parser.add_argument("--baud", default="460800", help="serial baud rate")
    parser.add_argument("--list-ports", action="store_true", help="list detected serial ports and exit")
    parser.add_argument("--image", type=Path, help="flash this merged image instead of auto-selecting from the bundle")
    parser.add_argument("--skip-checksum", action="store_true", help="flash without checking SHA256SUMS")
    parser.add_argument("--no-compress", action="store_true", help="disable esptool write compression")
    args = parser.parse_args()

    if args.list_ports:
        return list_ports()

    if not args.port:
        return fail("missing --port; use --list-ports to show detected ports")

    if not ensure_esptool():
        return fail("esptool is unavailable; install it with: python -m pip install esptool")

    bundle_dir = Path(__file__).resolve().parent
    try:
        if args.image:
            image = args.image.resolve()
        elif args.variant:
            image = find_image(bundle_dir, args.variant)
        else:
            image = choose_image(bundle_dir)
    except (FileNotFoundError, RuntimeError) as exc:
        return fail(str(exc))

    if not args.skip_checksum:
        try:
            verify_checksum(image, bundle_dir)
        except RuntimeError as exc:
            return fail(str(exc))

    command = [
        sys.executable,
        "-m",
        "esptool",
        "--chip",
        "esp32",
        "--port",
        args.port,
        "--baud",
        args.baud,
        "write_flash",
    ]
    if not args.no_compress:
        command.append("-z")
    command.extend(["0x0", str(image)])

    print(f"Flashing {image.name} to {args.port}...")
    return run(command)


if __name__ == "__main__":
    raise SystemExit(main())
