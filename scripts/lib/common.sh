#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PYTHON_BIN="${PYTHON:-python3}"
IDF_PYTHON_BIN="${PYTHON_BIN}"
DEFAULT_IDF_VERSION="${DEFAULT_IDF_VERSION:-v5.5.3}"
DEFAULT_IDF_PATH="${DEFAULT_IDF_PATH:-${HOME}/esp/esp-idf}"

log() {
  printf '[scripts] %s\n' "$1"
}

fail() {
  printf '[scripts] error: %s\n' "$1" >&2
  exit 1
}

require_file() {
  [[ -e "$1" ]] || fail "missing required file: $1"
}

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || fail "missing required command: $1"
}

clean_build_outputs() {
  rm -rf "${ROOT_DIR}/build" "${ROOT_DIR}"/build-*
}

build_cache_uses_different_idf() {
  local build_dir="$1"
  local cache_file
  local idf_source

  [[ -d "${ROOT_DIR}/${build_dir}" ]] || return 1
  while IFS= read -r cache_file; do
    idf_source="$(sed -n 's/^esp-idf_SOURCE_DIR:STATIC=//p' "${cache_file}" | head -n 1)"
    if [[ -n "${idf_source}" && "${idf_source}" != "${IDF_PATH}" ]]; then
      return 0
    fi
  done < <(find "${ROOT_DIR}/${build_dir}" -name CMakeCache.txt -type f)

  return 1
}

reject_packaged_idf_path() {
  local idf_path="$1"
  case "${idf_path}" in
    *"/.platformio/packages/framework-espidf"|*"/.platformio/packages/framework-espidf/"*|*"/.platformio/packages/framework-espidf/export.sh")
      fail "PlatformIO-packaged ESP-IDF is not supported; install official ESP-IDF at ${DEFAULT_IDF_PATH} or set IDF_PATH/IDF_EXPORT_SCRIPT"
      ;;
  esac
}

find_idf_export_script() {
  local export_script="${IDF_EXPORT_SCRIPT:-}"

  if [[ -z "${export_script}" && -n "${IDF_PATH:-}" && -f "${IDF_PATH}/export.sh" ]]; then
    export_script="${IDF_PATH}/export.sh"
  fi
  if [[ -z "${export_script}" && -f "${DEFAULT_IDF_PATH}/export.sh" ]]; then
    export_script="${DEFAULT_IDF_PATH}/export.sh"
  fi
  [[ -n "${export_script}" && -f "${export_script}" ]] || fail "ESP-IDF export.sh not found; run ./scripts/setup-esp-idf.sh or set IDF_PATH/IDF_EXPORT_SCRIPT"
  reject_packaged_idf_path "${export_script}"
  printf '%s\n' "${export_script}"
}

export_idf_env() {
  require_cmd "${PYTHON_BIN}"

  local export_script
  export_script="$(find_idf_export_script)"

  local export_dir
  export_dir="$(cd "$(dirname "${export_script}")" && pwd)"
  if [[ -f "${export_dir}/tools/idf.py" && ! -x "${export_dir}/tools/idf.py" ]]; then
    chmod u+x "${export_dir}/tools/idf.py"
  fi

  # shellcheck disable=SC1090
  source "${export_script}" >/dev/null

  if [[ -n "${IDF_PYTHON_ENV_PATH:-}" && -x "${IDF_PYTHON_ENV_PATH}/bin/python" ]]; then
    IDF_PYTHON_BIN="${IDF_PYTHON_ENV_PATH}/bin/python"
  else
    IDF_PYTHON_BIN="${PYTHON_BIN}"
  fi
}

run_idf() {
  export_idf_env
  "${IDF_PYTHON_BIN}" "${IDF_PATH}/tools/idf.py" "$@"
}

run_idf_build_with_size() {
  generate_build_info_for_args "$@"
  run_idf "$@" build
  run_idf "$@" size
  print_human_size_summary "$@"
}

generate_build_info_for_args() {
  local build_dir="build"
  local previous=""

  for arg in "$@"; do
    if [[ "${previous}" == "-B" ]]; then
      build_dir="${arg}"
      break
    fi
    previous="${arg}"
  done

  mkdir -p "${ROOT_DIR}/${build_dir}/esp-idf/src"
  "${PYTHON_BIN}" "${ROOT_DIR}/scripts/tools/generate_build_info.py" \
    "${ROOT_DIR}/${build_dir}/esp-idf/src/build_info.h" \
    "${ROOT_DIR}"
}

print_human_size_summary() {
  local build_dir="build"
  local previous=""

  for arg in "$@"; do
    if [[ "${previous}" == "-B" ]]; then
      build_dir="${arg}"
      break
    fi
    previous="${arg}"
  done

  if [[ -z "${IDF_PATH:-}" || -z "${IDF_PYTHON_BIN:-}" ]]; then
    export_idf_env
  fi
  "${IDF_PYTHON_BIN}" "${ROOT_DIR}/scripts/tools/size-summary.py" "${ROOT_DIR}/${build_dir}"
}

list_serial_ports() {
  export_idf_env
  "${IDF_PYTHON_BIN}" -m serial.tools.list_ports
}

print_idf_info() {
  export_idf_env
  log "IDF_PATH=${IDF_PATH}"
  "${IDF_PYTHON_BIN}" "${IDF_PATH}/tools/idf.py" --version
}
