#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "${SCRIPT_DIR}/lib/common.sh"

usage() {
  cat <<'EOF'
Usage: ./scripts/flash-firmware.sh [options]

Options:
  --port PORT    Upload through a specific serial port, such as /dev/ttyUSB0.
  --build-dir DIR  ESP-IDF build directory. Defaults to build.
  --ideaspark-19-lcd
                 Flash firmware for ideaspark ESP32 1.9 inch ST7789 LCD board.
  --lcd-debug-dirty-rects
                 Draw red/cyan LCD dirty-rectangle outlines for debugging.
  --test-block-found
                 Seed a synthetic block candidate alert at boot.
  --monitor      Open the serial monitor after a successful upload.
  --clean        Remove previous ESP-IDF build outputs before flashing.
  -h, --help     Show this help text.
EOF
}

build_dir="build"
upload_port=""
monitor=false
clean=false
build_dir_set=false
ideaspark_19_lcd=false
lcd_debug_dirty_rects=false
test_block_found=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --port)
      [[ $# -ge 2 ]] || fail "--port requires a serial port path"
      upload_port="$2"
      shift 2
      ;;
    --build-dir)
      [[ $# -ge 2 ]] || fail "--build-dir requires a directory"
      build_dir="$2"
      build_dir_set=true
      shift 2
      ;;
    --ideaspark-19-lcd)
      ideaspark_19_lcd=true
      shift
      ;;
    --lcd-debug-dirty-rects)
      ideaspark_19_lcd=true
      lcd_debug_dirty_rects=true
      shift
      ;;
    --test-block-found)
      test_block_found=true
      shift
      ;;
    --monitor)
      monitor=true
      shift
      ;;
    --clean)
      clean=true
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      fail "unknown argument: $1"
      ;;
  esac
done

cd "${ROOT_DIR}"

if [[ "${ideaspark_19_lcd}" == "true" && "${build_dir_set}" == "false" ]]; then
  build_dir="build-ideaspark"
fi
if [[ "${test_block_found}" == "true" && "${build_dir_set}" == "false" ]]; then
  build_dir="${build_dir}-test-block"
fi

if [[ "${clean}" == "true" ]]; then
  log "cleaning previous build outputs"
  rm -rf "${ROOT_DIR}/${build_dir}"
fi

idf_args=(-B "${build_dir}")
if [[ "${ideaspark_19_lcd}" == "true" ]]; then
  idf_args+=(-DAPP_DISPLAY_IDEASPARK_ESP32_19_LCD=1)
else
  idf_args+=(-DAPP_DISPLAY_IDEASPARK_ESP32_19_LCD=0)
fi
if [[ "${lcd_debug_dirty_rects}" == "true" ]]; then
  idf_args+=(-DAPP_LCD_DEBUG_DIRTY_RECTS=1)
else
  idf_args+=(-DAPP_LCD_DEBUG_DIRTY_RECTS=0)
fi
if [[ "${test_block_found}" == "true" ]]; then
  idf_args+=(-DAPP_TEST_BLOCK_FOUND=1)
else
  idf_args+=(-DAPP_TEST_BLOCK_FOUND=0)
fi

if [[ -n "${upload_port}" ]]; then
  idf_args+=(-p "${upload_port}")
fi

log "flashing ESP-IDF firmware from ${build_dir}"
generate_build_info_for_args "${idf_args[@]}"
run_idf "${idf_args[@]}" flash

log "checking memory usage for ${build_dir}"
run_idf "${idf_args[@]}" size
print_human_size_summary "${idf_args[@]}"

if [[ "${monitor}" == "true" ]]; then
  log "opening serial monitor"
  run_idf "${idf_args[@]}" monitor
fi
