#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "${SCRIPT_DIR}/lib/common.sh"

usage() {
  cat <<'EOF'
Usage: ./scripts/monitor-serial.sh [options]

Options:
  --port PORT    Open a specific serial port, such as /dev/ttyUSB0.
  --baud BAUD    Serial baud rate. Defaults to 115200.
  --list         List detected serial devices and exit.
  -h, --help     Show this help text.

Firmware app logs are only enabled in dev builds. Flash with:
  ./scripts/flash-firmware.sh --monitor
EOF
}

serial_port=""
baud="${APP_SERIAL_BAUD:-115200}"
list_devices=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --port)
      [[ $# -ge 2 ]] || fail "--port requires a serial port path"
      serial_port="$2"
      shift 2
      ;;
    --baud)
      [[ $# -ge 2 ]] || fail "--baud requires a baud rate"
      baud="$2"
      shift 2
      ;;
    --list)
      list_devices=true
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

if [[ "${list_devices}" == "true" ]]; then
  list_serial_ports
  exit 0
fi

monitor_args=(-b "${baud}")

if [[ -n "${serial_port}" ]]; then
  monitor_args+=(-p "${serial_port}")
fi

log "opening serial monitor at ${baud} baud"
log "if the monitor is quiet, press EN/reset after it opens"
run_idf "${monitor_args[@]}" monitor
