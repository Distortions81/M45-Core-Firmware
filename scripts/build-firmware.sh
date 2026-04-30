#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "${SCRIPT_DIR}/lib/common.sh"

usage() {
  cat <<'EOF'
Usage: ./scripts/build-firmware.sh [options]

Options:
  --build-dir DIR  ESP-IDF build directory. Defaults to build.
  --clean          Remove the selected build directory before building.
  --synthetic      Build no-Wi-Fi/no-OLED hardware SHA benchmark firmware.
  --ideaspark-19-lcd
                   Build for ideaspark ESP32 1.9 inch ST7789 LCD board.
  --lcd-debug-dirty-rects
                   Draw red/cyan LCD dirty-rectangle outlines for debugging.
  --synthetic-hashes-per-task N
                   Override fixed hashes per benchmark worker task.
  --mine-batch N   Override runtime Stratum nonce batch size.
  --yield-batches N
                   Override runtime Stratum batches between task yields.
  -h, --help       Show this help text.
EOF
}

build_dir="build"
build_dir_set=false
clean=false
synthetic=false
ideaspark_19_lcd=false
lcd_debug_dirty_rects=false
synthetic_hashes_per_task=""
mine_batch=""
yield_batches=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      [[ $# -ge 2 ]] || fail "--build-dir requires a directory"
      build_dir="$2"
      build_dir_set=true
      shift 2
      ;;
    --clean)
      clean=true
      shift
      ;;
    --synthetic|--synthetic-no-wifi-no-oled)
      synthetic=true
      shift
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
    --synthetic-hashes-per-task)
      [[ $# -ge 2 ]] || fail "--synthetic-hashes-per-task requires a positive integer"
      [[ "$2" =~ ^[1-9][0-9]*$ ]] || fail "--synthetic-hashes-per-task requires a positive integer"
      synthetic=true
      synthetic_hashes_per_task="$2"
      shift 2
      ;;
    --mine-batch)
      [[ $# -ge 2 ]] || fail "--mine-batch requires a positive integer"
      [[ "$2" =~ ^[1-9][0-9]*$ ]] || fail "--mine-batch requires a positive integer"
      mine_batch="$2"
      shift 2
      ;;
    --yield-batches)
      [[ $# -ge 2 ]] || fail "--yield-batches requires a non-negative integer"
      [[ "$2" =~ ^[0-9]+$ ]] || fail "--yield-batches requires a non-negative integer"
      yield_batches="$2"
      shift 2
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

if [[ "${synthetic}" == "true" && "${build_dir_set}" == "false" ]]; then
  build_dir="build-synthetic"
fi
if [[ "${ideaspark_19_lcd}" == "true" && "${build_dir_set}" == "false" ]]; then
  build_dir="build-ideaspark"
fi

if [[ "${clean}" == "true" ]]; then
  log "cleaning previous build outputs"
  rm -rf "${ROOT_DIR:?}/${build_dir}"
fi

export_idf_env
if build_cache_uses_different_idf "${build_dir}"; then
  log "cleaning ${build_dir}; cached ESP-IDF path differs from ${IDF_PATH}"
  rm -rf "${ROOT_DIR:?}/${build_dir}"
fi

idf_args=(-B "${build_dir}")
if [[ "${synthetic}" == "true" ]]; then
  idf_args+=(-DAPP_SYNTHETIC_NO_WIFI_NO_OLED=1 -DAPP_ENABLE_SERIAL_LOGS=1)
fi
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
if [[ -n "${synthetic_hashes_per_task}" ]]; then
  idf_args+=(-DAPP_SYNTHETIC_HASHES_PER_TASK="${synthetic_hashes_per_task}")
fi
if [[ -n "${mine_batch}" ]]; then
  idf_args+=(-DAPP_STRATUM_MINE_BATCH="${mine_batch}")
fi
if [[ -n "${yield_batches}" ]]; then
  idf_args+=(-DAPP_STRATUM_MINE_YIELD_BATCHES="${yield_batches}")
fi

log "building firmware with ESP-IDF in ${build_dir}"
run_idf_build_with_size "${idf_args[@]}"
