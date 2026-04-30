#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "${SCRIPT_DIR}/lib/common.sh"

usage() {
  cat <<'EOF'
Usage: ./scripts/benchmark-web.sh [options] [BASE_URL]

Run a simple ESP32 web interface benchmark from the host. By default it sends
one HTTP GET request per second for 60 seconds against safe web UI/API routes.

Options:
  --duration SECONDS  Benchmark duration. Defaults to 60.
  --interval SECONDS  Delay between request starts. Defaults to 1.
  --timeout SECONDS   Per-request timeout. Defaults to 5.
  --path PATH         Request path to include. Repeat to provide a rotation.
                      Defaults to: /health / /stats /api/stats /api/memory /settings /wifi /licenses
  -h, --help          Show this help text.

Examples:
  ./scripts/benchmark-web.sh
  ./scripts/benchmark-web.sh http://192.168.1.42
  ./scripts/benchmark-web.sh --duration 120 --path /api/stats http://10.45.0.1
EOF
}

duration=60
interval=1
timeout=5
base_url="http://10.45.0.1"
paths=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --duration)
      [[ $# -ge 2 ]] || fail "--duration requires a positive integer"
      [[ "$2" =~ ^[1-9][0-9]*$ ]] || fail "--duration requires a positive integer"
      duration="$2"
      shift 2
      ;;
    --interval)
      [[ $# -ge 2 ]] || fail "--interval requires a positive integer"
      [[ "$2" =~ ^[1-9][0-9]*$ ]] || fail "--interval requires a positive integer"
      interval="$2"
      shift 2
      ;;
    --timeout)
      [[ $# -ge 2 ]] || fail "--timeout requires a positive integer"
      [[ "$2" =~ ^[1-9][0-9]*$ ]] || fail "--timeout requires a positive integer"
      timeout="$2"
      shift 2
      ;;
    --path)
      [[ $# -ge 2 ]] || fail "--path requires a request path"
      [[ "$2" == /* ]] || fail "--path must start with /"
      paths+=("$2")
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    http://*|https://*)
      base_url="${1%/}"
      shift
      ;;
    *)
      fail "unknown argument: $1"
      ;;
  esac
done

require_cmd curl
require_cmd date
require_cmd awk

if [[ ${#paths[@]} -eq 0 ]]; then
  paths=(/health / /stats /api/stats /api/memory /settings /wifi /licenses)
fi

ok=0
failed=0
total_ms=0
min_ms=
max_ms=0

log "benchmarking ${base_url} for ${duration}s; ${interval}s interval, ${timeout}s timeout"
printf 'request,path,http_code,total_ms,bytes,result\n'

start_epoch="$(date +%s)"
deadline_epoch=$((start_epoch + duration))
next_start_epoch="${start_epoch}"
i=0

while [[ "$(date +%s)" -lt "${deadline_epoch}" ]]; do
  i=$((i + 1))
  path="${paths[$(((i - 1) % ${#paths[@]}))]}"
  result="ok"

  curl_output="$(curl \
    --connect-timeout "${timeout}" \
    --max-time "${timeout}" \
    --output /dev/null \
    --silent \
    --show-error \
    --write-out '%{http_code} %{time_total} %{size_download}' \
    "${base_url}${path}" 2>&1)" || result="error"

  if [[ "${result}" == "ok" ]]; then
    read -r http_code total_seconds bytes <<<"${curl_output}"
    elapsed_ms="$(awk -v seconds="${total_seconds}" 'BEGIN { printf "%d", seconds * 1000 }')"
    if [[ -z "${min_ms}" || "${elapsed_ms}" -lt "${min_ms}" ]]; then
      min_ms="${elapsed_ms}"
    fi
    if [[ "${elapsed_ms}" -gt "${max_ms}" ]]; then
      max_ms="${elapsed_ms}"
    fi
    if [[ "${http_code}" =~ ^[23] ]]; then
      total_ms="$(awk -v sum="${total_ms}" -v seconds="${total_seconds}" 'BEGIN { printf "%d", sum + (seconds * 1000) }')"
      ok=$((ok + 1))
    else
      result="http_${http_code}"
      failed=$((failed + 1))
    fi
  else
    http_code=000
    elapsed_ms=0
    bytes=0
    failed=$((failed + 1))
  fi

  printf '%d,%s,%s,%s,%s,%s\n' "${i}" "${path}" "${http_code}" "${elapsed_ms}" "${bytes}" "${result}"

  next_start_epoch=$((next_start_epoch + interval))
  now_epoch="$(date +%s)"
  sleep_seconds=$((next_start_epoch - now_epoch))
  if [[ "${sleep_seconds}" -gt 0 ]]; then
    sleep "${sleep_seconds}"
  fi
done

avg_ms=0
if [[ "${ok}" -gt 0 ]]; then
  avg_ms="$(awk -v sum="${total_ms}" -v count="${ok}" 'BEGIN { printf "%d", sum / count }')"
fi
if [[ -z "${min_ms}" ]]; then
  min_ms=0
fi

log "summary: ok=${ok} failed=${failed} avg_ms=${avg_ms} min_ms=${min_ms} max_ms=${max_ms}"

if [[ "${failed}" -gt 0 ]]; then
  exit 1
fi
