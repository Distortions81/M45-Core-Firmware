#!/usr/bin/env python3

import json
import os
import subprocess
import sys
from pathlib import Path


def fail(message):
    print(f"[scripts] error: {message}", file=sys.stderr)
    return 1


def human_size(bytes_count):
    value = float(bytes_count)
    units = ["B", "KiB", "MiB", "GiB"]
    unit = units[0]
    for unit in units:
        if value < 1024.0 or unit == units[-1]:
            break
        value /= 1024.0
    if unit == "B":
        return f"{int(value)} {unit}"
    return f"{value:.2f} {unit}"


def percent(used, total):
    if total <= 0:
        return "n/a"
    return f"{used / total * 100.0:.1f}%"


def parse_partition_size(text):
    text = text.strip()
    if text.startswith("0x"):
        return int(text, 16)
    suffix = text[-1:].upper()
    multiplier = 1
    if suffix == "K":
        multiplier = 1024
        text = text[:-1]
    elif suffix == "M":
        multiplier = 1024 * 1024
        text = text[:-1]
    return int(text, 10) * multiplier


def load_smallest_app_partition(build_dir):
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        return None
    partition_bin = build_dir / "partition_table" / "partition-table.bin"
    gen_part = Path(idf_path) / "components" / "partition_table" / "gen_esp32part.py"
    if not partition_bin.exists() or not gen_part.exists():
        return None

    result = subprocess.run(
        [sys.executable, str(gen_part), str(partition_bin)],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    sizes = []
    for line in result.stdout.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "," not in line:
            continue
        fields = [field.strip() for field in line.split(",")]
        if len(fields) >= 5 and fields[1] == "app":
            sizes.append(parse_partition_size(fields[4]))
    return min(sizes) if sizes else None


def main():
    if len(sys.argv) != 2:
        return fail("usage: size-summary.py BUILD_DIR")

    build_dir = Path(sys.argv[1])
    if not build_dir.is_dir():
        return fail(f"build directory not found: {build_dir}")

    maps = sorted(path for path in build_dir.glob("*.map") if path.is_file())
    bins = sorted(path for path in build_dir.glob("*.bin") if path.is_file())
    if not maps:
        return fail(f"app map file not found in {build_dir}")
    if not bins:
        return fail(f"app binary not found in {build_dir}")

    size_result = subprocess.run(
        [sys.executable, "-m", "esp_idf_size", "--format", "json", str(maps[0])],
        check=True,
        stdout=subprocess.PIPE,
        text=True,
    )
    size_data = json.loads(size_result.stdout)

    app_bin_size = bins[0].stat().st_size
    app_partition_size = load_smallest_app_partition(build_dir)
    print("[scripts] human-readable size summary")
    if app_partition_size is not None:
        app_free = app_partition_size - app_bin_size
        print(
            f"  App binary: {human_size(app_bin_size)} of {human_size(app_partition_size)} "
            f"({percent(app_bin_size, app_partition_size)} used, {human_size(app_free)} free)"
        )
    else:
        print(f"  App binary: {human_size(app_bin_size)}")

    flash_data = size_data.get("flash_rodata", 0) + size_data.get("flash_other", 0)
    print(f"  Total image: {human_size(size_data['total_size'])}")
    print(f"  Flash code:  {human_size(size_data['flash_code'])}")
    print(f"  Flash data:  {human_size(flash_data)}")
    print(
        f"  IRAM:        {human_size(size_data['used_iram'])} of {human_size(size_data['iram_total'])} "
        f"({percent(size_data['used_iram'], size_data['iram_total'])} used, "
        f"{human_size(size_data['iram_remain'])} free)"
    )
    print(
        f"  DRAM:        {human_size(size_data['used_dram'])} of {human_size(size_data['dram_total'])} "
        f"({percent(size_data['used_dram'], size_data['dram_total'])} used, "
        f"{human_size(size_data['dram_remain'])} free)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
