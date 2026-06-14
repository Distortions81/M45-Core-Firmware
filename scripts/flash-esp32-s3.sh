#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "${SCRIPT_DIR}/lib/common.sh"

usage() {
  cat <<'EOF'
Usage: ./scripts/flash-esp32-s3.sh [options]

Build and flash the ESP32-S3 software-miner compatibility target.

Options:
  --port PORT      Upload through a specific serial port, such as /dev/ttyACM0.
  --build-dir DIR  ESP-IDF build directory. Defaults to build-s3.
  --monitor        Open the serial monitor after a successful upload.
  --clean          Remove previous ESP32-S3 build outputs before flashing.
  -h, --help       Show this help text.
EOF
}

build_dir="build-s3"
upload_port=""
monitor=false
clean=false

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
      shift 2
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

if [[ "${clean}" == "true" ]]; then
  log "cleaning previous ESP32-S3 build outputs"
  rm -rf "${ROOT_DIR:?}/${build_dir}" "${ROOT_DIR}/sdkconfig.s3" "${ROOT_DIR}/sdkconfig.s3.old"
fi

export_idf_env
if build_cache_uses_different_idf "${build_dir}"; then
  log "cleaning ${build_dir}; cached ESP-IDF path differs from ${IDF_PATH}"
  rm -rf "${ROOT_DIR:?}/${build_dir}"
fi

idf_args=(
  -B "${build_dir}"
  -DSDKCONFIG=sdkconfig.s3
  -DIDF_TARGET=esp32s3
  -DAPP_DISPLAY_IDEASPARK_ESP32_19_LCD=0
)
if [[ -n "${upload_port}" ]]; then
  idf_args+=(-p "${upload_port}")
fi

log "building and flashing ESP32-S3 software-miner firmware from ${build_dir}"
generate_build_info_for_args "${idf_args[@]}"
run_idf "${idf_args[@]}" flash

log "checking memory usage for ${build_dir}"
run_idf "${idf_args[@]}" size
print_human_size_summary "${idf_args[@]}"

if [[ "${monitor}" == "true" ]]; then
  log "opening serial monitor"
  run_idf "${idf_args[@]}" monitor
fi
