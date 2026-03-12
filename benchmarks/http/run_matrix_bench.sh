#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
SERVER_BIN="${ROOT_DIR}/bin/http_bench_server"
POST_STATUS_LUA="${ROOT_DIR}/benchmarks/http/post_status.lua"
STATUS_LUA="${ROOT_DIR}/benchmarks/http/status_count.lua"
RESULTS_ROOT="${ROOT_DIR}/benchmarks/http/results"

HOST=${HOST:-127.0.0.1}
PORT=${PORT:-18080}
WRK_THREADS=${WRK_THREADS:-4}
WRK_DURATION=${WRK_DURATION:-10s}
SERVER_IO_THREADS=${SERVER_IO_THREADS:-4}
SERVER_ACCEPT_THREADS=${SERVER_ACCEPT_THREADS:-1}
SERVER_SESSION_ENABLED=${SERVER_SESSION_ENABLED:-0}
SERVER_PID=""
LOG_INITIALIZED=0

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required command: $1" >&2
    exit 1
  fi
}

ensure_server_bin() {
  if [[ -x "${SERVER_BIN}" ]]; then
    return
  fi
  echo "Building http_bench_server"
  cmake -S "${ROOT_DIR}" -B "${ROOT_DIR}/build" >/dev/null
  cmake --build "${ROOT_DIR}/build" --target http_bench_server -j2 >/dev/null
}

cleanup() {
  if [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" 2>/dev/null; then
    curl -fsS -m 2 "http://${HOST}:${PORT}/__admin/quit" >/dev/null 2>&1 || true
    for _ in $(seq 1 30); do
      if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
        break
      fi
      sleep 0.1
    done
    if kill -0 "${SERVER_PID}" 2>/dev/null; then
      kill "${SERVER_PID}" 2>/dev/null || true
    fi
    wait "${SERVER_PID}" 2>/dev/null || true
  fi
  SERVER_PID=""
}

init_logging() {
  if [[ "${LOG_INITIALIZED}" -eq 1 ]]; then
    return
  fi

  local run_date
  local run_stamp
  run_date=$(date +%F)
  run_stamp=$(date +%Y%m%d_%H%M%S)
  local result_dir="${RESULTS_ROOT}/${run_date}"
  mkdir -p "${result_dir}"

  RESULT_LOG=${RESULT_LOG:-"${result_dir}/matrix_${run_stamp}.log"}
  exec > >(tee "${RESULT_LOG}") 2>&1
  LOG_INITIALIZED=1

  echo "Log file: ${RESULT_LOG}"
}

wait_until_ready() {
  local url="http://${HOST}:${PORT}/ping"
  for _ in $(seq 1 100); do
    if curl -fsS "${url}" >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Server did not become ready: ${url}" >&2
  exit 1
}

start_server() {
  local ss=$1
  local fp=$2
  local uc=$3
  
  cleanup
  "${SERVER_BIN}" \
    --host "${HOST}" \
    --port "${PORT}" \
    --io-threads "${SERVER_IO_THREADS}" \
    --accept-threads "${SERVER_ACCEPT_THREADS}" \
    --session-enabled "${SERVER_SESSION_ENABLED}" \
    --shared-stack "${ss}" \
    --fiber-pool "${fp}" \
    --use-caller "${uc}" &
  SERVER_PID=$!
  wait_until_ready
}

run_bench_case() {
  local ss=$1
  local fp=$2
  local uc=$3
  local label=$4
  shift 4

  echo ""
  echo ">>> ${label}"
  start_server "${ss}" "${fp}" "${uc}"
  wrk --latency "$@"
  cleanup
}

run_matrix() {
  echo "=========================================================="
  echo "      Sylar HTTP Server 8-Combination Matrix Bench        "
  echo "=========================================================="

  for ss in 0 1; do
    for fp in 0 1; do
      for uc in 0 1; do
        echo ""
        echo "----------------------------------------------------------"
        echo "[CONFIG] shared_stack=${ss} | fiber_pool=${fp} | use_caller=${uc}"
        echo "----------------------------------------------------------"
        
        run_bench_case "${ss}" "${fp}" "${uc}" \
          "Baseline: /ping (exact route hot path, C=500, T=${WRK_THREADS}, ${WRK_DURATION})" \
          -t"${WRK_THREADS}" -c500 -d"${WRK_DURATION}" -s "${STATUS_LUA}" "http://${HOST}:${PORT}/ping"

        run_bench_case "${ss}" "${fp}" "${uc}" \
          "Timer Wait: /api/user/profile (20ms hook timer wait, C=1000, T=${WRK_THREADS}, ${WRK_DURATION})" \
          -t"${WRK_THREADS}" -c1000 -d"${WRK_DURATION}" -s "${STATUS_LUA}" "http://${HOST}:${PORT}/api/user/profile"

        run_bench_case "${ss}" "${fp}" "${uc}" \
          "POST & JSON Parse: /api/data/upload (JSON parse, C=500, T=${WRK_THREADS}, ${WRK_DURATION})" \
          -t"${WRK_THREADS}" -c500 -d"${WRK_DURATION}" -s "${POST_STATUS_LUA}" "http://${HOST}:${PORT}/api/data/upload"

        run_bench_case "${ss}" "${fp}" "${uc}" \
          "Large Payload: /api/file/download (1MB static payload, C=50, T=${WRK_THREADS}, ${WRK_DURATION})" \
          -t"${WRK_THREADS}" -c50 -d"${WRK_DURATION}" -s "${STATUS_LUA}" "http://${HOST}:${PORT}/api/file/download"
      done
    done
  done
}

main() {
  init_logging
  require_cmd wrk
  require_cmd curl
  ensure_server_bin
  trap cleanup EXIT INT TERM
  
  run_matrix
}

main "$@"
