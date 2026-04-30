#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "${SCRIPT_DIR}/lib/common.sh"

idf_version="${IDF_VERSION:-${DEFAULT_IDF_VERSION}}"

usage() {
  cat <<EOF
Usage: ./scripts/setup-esp-idf.sh [options]

Options:
  --idf-path DIR      ESP-IDF checkout path. Defaults to IDF_PATH or ~/esp/esp-idf.
  --idf-version TAG   ESP-IDF git tag to clone when no checkout exists. Defaults to ${DEFAULT_IDF_VERSION}.
  -h, --help          Show this help text.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --idf-path)
      [[ $# -ge 2 ]] || fail "--idf-path requires a directory"
      export IDF_PATH="$2"
      shift 2
      ;;
    --idf-version)
      [[ $# -ge 2 ]] || fail "--idf-version requires a git tag"
      idf_version="$2"
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

require_cmd "${PYTHON_BIN}"
require_cmd git

if [[ -n "${IDF_EXPORT_SCRIPT:-}" ]]; then
  reject_packaged_idf_path "${IDF_EXPORT_SCRIPT}"
fi

install_path="${IDF_PATH:-${DEFAULT_IDF_PATH}}"
reject_packaged_idf_path "${install_path}"

if [[ -z "${IDF_EXPORT_SCRIPT:-}" && ! -f "${install_path}/export.sh" ]]; then
  log "ESP-IDF checkout not found; cloning ${idf_version} into ${install_path}"
  mkdir -p "$(dirname "${install_path}")"
  git clone --recursive --branch "${idf_version}" https://github.com/espressif/esp-idf.git "${install_path}"
fi

export_script="$(find_idf_export_script)"
export IDF_PATH="$(cd "$(dirname "${export_script}")" && pwd)"

[[ -f "${IDF_PATH}/install.sh" ]] || fail "missing ${IDF_PATH}/install.sh"
[[ -f "${IDF_PATH}/export.sh" ]] || fail "missing ${IDF_PATH}/export.sh"

log "installing ESP-IDF tools for esp32 from ${IDF_PATH}"
bash "${IDF_PATH}/install.sh" esp32

log "verifying ESP-IDF build tooling"
# shellcheck disable=SC1090
source "${IDF_PATH}/export.sh" >/dev/null
if [[ -n "${IDF_PYTHON_ENV_PATH:-}" && -x "${IDF_PYTHON_ENV_PATH}/bin/python" ]]; then
  IDF_PYTHON_BIN="${IDF_PYTHON_ENV_PATH}/bin/python"
fi
"${IDF_PYTHON_BIN}" "${IDF_PATH}/tools/idf.py" --version

cat <<EOF

Setup complete.

Next steps:
1. Export ESP-IDF in new shells:
   source "${IDF_PATH}/export.sh"
2. Build:
   ./scripts/build-firmware.sh
3. Flash:
   ./scripts/flash-firmware.sh --port /dev/ttyUSB0
EOF
