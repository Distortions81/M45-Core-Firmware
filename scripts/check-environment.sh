#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "${SCRIPT_DIR}/lib/common.sh"

usage() {
  cat <<'EOF'
Usage: ./scripts/check-environment.sh [options]

Options:
  --require-official-checkout  Fail unless IDF_PATH looks like an official ESP-IDF checkout.
  -h, --help                   Show this help text.
EOF
}

require_official_checkout=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --require-official-checkout)
      require_official_checkout=true
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

log "checking ESP-IDF environment"
print_idf_info
if [[ "${require_official_checkout}" == "true" ]]; then
  [[ -d "${IDF_PATH}/.git" ]] || fail "IDF_PATH does not look like an official ESP-IDF git checkout: ${IDF_PATH}"
fi

log "checking required project files"
require_file "${ROOT_DIR}/CMakeLists.txt"
require_file "${ROOT_DIR}/src/CMakeLists.txt"
require_file "${ROOT_DIR}/src/idf_main.c"
require_file "${ROOT_DIR}/sdkconfig.defaults"

log "checking helper tools"
require_cmd python3
require_cmd mktemp
pycache_dir="$(mktemp -d)"
trap 'rm -rf "${pycache_dir}"' EXIT
PYTHONPYCACHEPREFIX="${pycache_dir}" python3 -m py_compile "${ROOT_DIR}/scripts/tools/"*.py

log "ok"
