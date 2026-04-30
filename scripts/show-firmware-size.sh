#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "${SCRIPT_DIR}/lib/common.sh"

usage() {
  cat <<'EOF'
Usage: ./scripts/show-firmware-size.sh [options]

Options:
  --build-dir DIR  Show memory usage for an ESP-IDF build directory.
  -h, --help       Show this help text.
EOF
}

build_dir="build"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      [[ $# -ge 2 ]] || fail "--build-dir requires a directory"
      build_dir="$2"
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

log "checking memory usage for ${build_dir}"
run_idf -B "${build_dir}" size
print_human_size_summary -B "${build_dir}"
