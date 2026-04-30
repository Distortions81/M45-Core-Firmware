#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "${SCRIPT_DIR}/lib/common.sh"

usage() {
  cat <<'EOF'
Usage: ./scripts/benchmark-hashrate.sh [options] [BASE_URL]

Run a long host-side hashrate benchmark against /api/stats. The default run
collects 100 post-warmup samples over 30 minutes and prints a conservative
advertised hashrate candidate.

Options:
  --duration SECONDS    Total benchmark duration. Defaults to 1800.
  --warmup SECONDS      Initial samples to ignore. Defaults to 300.
  --interval SECONDS    Delay between samples. Defaults to 15.
  --timeout SECONDS     Per-request timeout. Defaults to 5.
  --min-samples COUNT   Minimum valid post-warmup samples. Defaults to 100.
  --round-step HPS      Round advertised value down by this step. Defaults to 5000.
  --samples FILE        Write raw sample CSV to FILE.
  -h, --help            Show this help text.

Examples:
  ./scripts/benchmark-hashrate.sh http://192.168.8.141
  ./scripts/benchmark-hashrate.sh --duration 3600 --warmup 300 --samples hashrate.csv http://192.168.8.141
EOF
}

duration=1800
warmup=300
interval=15
timeout=5
min_samples=100
round_step=5000
base_url="http://10.45.0.1"
samples_file=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --duration)
      [[ $# -ge 2 ]] || fail "--duration requires a positive integer"
      [[ "$2" =~ ^[1-9][0-9]*$ ]] || fail "--duration requires a positive integer"
      duration="$2"
      shift 2
      ;;
    --warmup)
      [[ $# -ge 2 ]] || fail "--warmup requires a non-negative integer"
      [[ "$2" =~ ^[0-9]+$ ]] || fail "--warmup requires a non-negative integer"
      warmup="$2"
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
    --min-samples)
      [[ $# -ge 2 ]] || fail "--min-samples requires a positive integer"
      [[ "$2" =~ ^[1-9][0-9]*$ ]] || fail "--min-samples requires a positive integer"
      min_samples="$2"
      shift 2
      ;;
    --round-step)
      [[ $# -ge 2 ]] || fail "--round-step requires a positive integer"
      [[ "$2" =~ ^[1-9][0-9]*$ ]] || fail "--round-step requires a positive integer"
      round_step="$2"
      shift 2
      ;;
    --samples)
      [[ $# -ge 2 ]] || fail "--samples requires a file path"
      samples_file="$2"
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

[[ "${warmup}" -lt "${duration}" ]] || fail "--warmup must be less than --duration"

require_cmd awk
require_cmd curl
require_cmd date
require_cmd mktemp
require_cmd sort

values_file="$(mktemp)"
sorted_values_file="$(mktemp)"
cleanup() {
  rm -f "${values_file}" "${sorted_values_file}"
}
trap cleanup EXIT

format_hps() {
  awk -v value="$1" '
    function hps(v) {
      if (v >= 1000000) {
        return sprintf("%.3f MH/s", v / 1000000)
      }
      if (v >= 1000) {
        return sprintf("%.1f kH/s", v / 1000)
      }
      return sprintf("%.0f H/s", v)
    }
    BEGIN { print hps(value + 0) }
  '
}

if [[ -n "${samples_file}" ]]; then
  printf 'sample,elapsed_s,phase,pool_status,shares_per_min,hashes_per_sec,hashrate_average,current_difficulty,submitted_shares,rejected_shares,result\n' >"${samples_file}"
fi

sample_window=$((duration - warmup))
target_samples=$(((sample_window + interval - 1) / interval))
log "hashrate benchmark ${base_url}/api/stats for ${duration}s; warmup=${warmup}s interval=${interval}s target_samples=${target_samples}"

start_epoch="$(date +%s)"
deadline_epoch=$((start_epoch + duration))
next_start_epoch="${start_epoch}"
sample=0
valid_samples=0
warmup_samples=0
invalid_samples=0
failed_requests=0
not_connected_samples=0
last_submitted=0
last_rejected=0

while [[ "$(date +%s)" -lt "${deadline_epoch}" ]]; do
  sample=$((sample + 1))
  now_epoch="$(date +%s)"
  elapsed_s=$((now_epoch - start_epoch))
  phase="sample"
  if [[ "${elapsed_s}" -lt "${warmup}" ]]; then
    phase="warmup"
    warmup_samples=$((warmup_samples + 1))
  fi

  result="ok"
  response="$(curl \
    --connect-timeout "${timeout}" \
    --max-time "${timeout}" \
    --silent \
    --show-error \
    "${base_url}/api/stats" 2>&1)" || result="request_failed"

  uptime=""
  pool_status=""
  endpoint=""
  shares_per_min="0"
  hashes_per_sec="0"
  hashrate_average=""
  current_difficulty="0"
  submitted_shares="0"
  rejected_shares="0"

  if [[ "${result}" == "ok" ]]; then
    IFS=, read -r uptime pool_status endpoint shares_per_min hashes_per_sec hashrate_average current_difficulty submitted_shares rejected_shares _ <<<"${response}"
    if [[ ! "${hashes_per_sec}" =~ ^[0-9]+$ ||
          ! "${submitted_shares}" =~ ^[0-9]+$ ||
          ! "${rejected_shares}" =~ ^[0-9]+$ ]]; then
      result="bad_stats"
    fi
  fi

  if [[ "${result}" == "ok" ]]; then
    last_submitted="${submitted_shares}"
    last_rejected="${rejected_shares}"
    if [[ "${phase}" == "sample" ]]; then
      if [[ "${pool_status}" == "Connected" && "${hashes_per_sec}" -gt 0 ]]; then
        printf '%s\n' "${hashes_per_sec}" >>"${values_file}"
        valid_samples=$((valid_samples + 1))
        log "sample ${valid_samples}/${target_samples}: $(format_hps "${hashes_per_sec}") shares=${submitted_shares} errors=${rejected_shares}"
      else
        result="not_connected"
        not_connected_samples=$((not_connected_samples + 1))
      fi
    fi
  else
    failed_requests=$((failed_requests + 1))
  fi

  if [[ "${phase}" == "sample" && "${result}" != "ok" ]]; then
    invalid_samples=$((invalid_samples + 1))
    log "ignored sample ${sample}: ${result}"
  fi

  if [[ -n "${samples_file}" ]]; then
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
      "${sample}" \
      "${elapsed_s}" \
      "${phase}" \
      "${pool_status}" \
      "${shares_per_min}" \
      "${hashes_per_sec}" \
      "${hashrate_average}" \
      "${current_difficulty}" \
      "${submitted_shares}" \
      "${rejected_shares}" \
      "${result}" >>"${samples_file}"
  fi

  next_start_epoch=$((next_start_epoch + interval))
  now_epoch="$(date +%s)"
  sleep_seconds=$((next_start_epoch - now_epoch))
  if [[ "${sleep_seconds}" -gt 0 ]]; then
    sleep "${sleep_seconds}"
  fi
done

sort -n "${values_file}" >"${sorted_values_file}"
valid_count="$(awk 'END { print NR + 0 }' "${sorted_values_file}")"
if [[ "${valid_count}" -lt "${min_samples}" ]]; then
  fail "only ${valid_count} valid samples; need at least ${min_samples}. failed_requests=${failed_requests} not_connected=${not_connected_samples}"
fi

awk \
  -v warmup_samples="${warmup_samples}" \
  -v invalid_samples="${invalid_samples}" \
  -v failed_requests="${failed_requests}" \
  -v not_connected_samples="${not_connected_samples}" \
  -v last_submitted="${last_submitted}" \
  -v last_rejected="${last_rejected}" \
  -v round_step="${round_step}" '
  function hps(v) {
    if (v >= 1000000) {
      return sprintf("%.3f MH/s", v / 1000000)
    }
    if (v >= 1000) {
      return sprintf("%.1f kH/s", v / 1000)
    }
    return sprintf("%.0f H/s", v)
  }
  function percentile(p, idx) {
    if (n <= 0) {
      return 0
    }
    idx = int((p * n + 99) / 100)
    if (idx < 1) {
      idx = 1
    }
    if (idx > n) {
      idx = n
    }
    return value[idx]
  }
  {
    ++n
    value[n] = $1 + 0
    sum += value[n]
    sumsq += value[n] * value[n]
    if (n == 1 || value[n] < min) {
      min = value[n]
    }
    if (n == 1 || value[n] > max) {
      max = value[n]
    }
  }
  END {
    mean = sum / n
    variance = (sumsq / n) - (mean * mean)
    if (variance < 0) {
      variance = 0
    }
    stddev = sqrt(variance)
    lower95 = mean - (1.96 * stddev / sqrt(n))
    p05 = percentile(5)
    p10 = percentile(10)
    p50 = percentile(50)
    p90 = percentile(90)
    p95 = percentile(95)
    safe = lower95
    if (p10 < safe) {
      safe = p10
    }
    if (safe < 0) {
      safe = 0
    }
    safe_rounded = int(safe / round_step) * round_step
    error_pct = 0
    if (last_submitted > 0) {
      error_pct = (last_rejected * 100.0) / last_submitted
    }

    printf "\nHashrate benchmark summary\n"
    printf "  valid samples:          %d\n", n
    printf "  ignored warmup samples: %d\n", warmup_samples
    printf "  invalid samples:        %d (request failures=%d, not connected=%d)\n", invalid_samples, failed_requests, not_connected_samples
    printf "  mean:                   %s\n", hps(mean)
    printf "  median:                 %s\n", hps(p50)
    printf "  p10 / p5:               %s / %s\n", hps(p10), hps(p05)
    printf "  p90 / p95:              %s / %s\n", hps(p90), hps(p95)
    printf "  min / max:              %s / %s\n", hps(min), hps(max)
    printf "  95%% lower mean bound:   %s\n", hps(lower95)
    printf "  safe advertised value:  %s\n", hps(safe_rounded)
    printf "  share errors:           %.1f%% (%d of %d)\n", error_pct, last_rejected, last_submitted
    printf "\nMethod: safe advertised value is min(p10, 95%% lower mean bound), rounded down to the nearest %s.\n", hps(round_step)
  }
' "${sorted_values_file}"
