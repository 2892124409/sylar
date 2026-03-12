#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
SERVER_BIN="${ROOT_DIR}/bin/http_bench_server"
MIXED_LUA="${ROOT_DIR}/benchmarks/http/mixed.lua"
STATUS_LUA="${ROOT_DIR}/benchmarks/http/status_count.lua"
RESULTS_ROOT="${ROOT_DIR}/benchmarks/http/results"

HOST=${HOST:-127.0.0.1}
PORT=${PORT:-18080}
WRK_THREADS=${WRK_THREADS:-4}
WRK_CONNECTIONS=${WRK_CONNECTIONS:-64}
WRK_DURATION=${WRK_DURATION:-15s}
SERVER_IO_THREADS=${SERVER_IO_THREADS:-4}
SERVER_ACCEPT_THREADS=${SERVER_ACCEPT_THREADS:-1}
SERVER_SESSION_ENABLED=${SERVER_SESSION_ENABLED:-0}
EDGE_MAX_CONN=${EDGE_MAX_CONN:-16}
EDGE_SLEEP_MS=${EDGE_SLEEP_MS:-2000}
EDGE_BLOCKED_DURATION=${EDGE_BLOCKED_DURATION:-5s}
EDGE_HOLD_DURATION=${EDGE_HOLD_DURATION:-10s}
EDGE_PROBE_DURATION=${EDGE_PROBE_DURATION:-5s}
EDGE_WARMUP_SEC=${EDGE_WARMUP_SEC:-1}
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
  local scenario=$1
  if [[ "${LOG_INITIALIZED}" -eq 1 ]]; then
    return
  fi

  local run_date
  local run_stamp
  run_date=$(date +%F)
  run_stamp=$(date +%Y%m%d_%H%M%S)
  local result_dir="${RESULTS_ROOT}/${run_date}"
  mkdir -p "${result_dir}"

  RESULT_LOG=${RESULT_LOG:-"${result_dir}/run_${scenario}_${run_stamp}.log"}
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
  local max_connections=${1:-0}
  cleanup
  "${SERVER_BIN}" \
    --host "${HOST}" \
    --port "${PORT}" \
    --io-threads "${SERVER_IO_THREADS}" \
    --accept-threads "${SERVER_ACCEPT_THREADS}" \
    --max-connections "${max_connections}" \
    --session-enabled "${SERVER_SESSION_ENABLED}" \
    --keepalive-timeout-ms 5000 \
    --keepalive-max-requests 0 &
  SERVER_PID=$!
  wait_until_ready
}

run_throughput() {
  echo "== throughput =="
  wrk --latency -t"${WRK_THREADS}" -c"${WRK_CONNECTIONS}" -d"${WRK_DURATION}" "http://${HOST}:${PORT}/ping"
}

run_mixed() {
  echo "== mixed =="
  wrk --latency -t"${WRK_THREADS}" -c"${WRK_CONNECTIONS}" -d"${WRK_DURATION}" -s "${MIXED_LUA}" "http://${HOST}:${PORT}"
}

run_edge_blocked() {
  echo "== edge: blocked =="
  wrk --latency -t2 -c16 -d"${EDGE_BLOCKED_DURATION}" -s "${STATUS_LUA}" "http://${HOST}:${PORT}/blocked"
}

run_edge_conn_limit() {
  echo "== edge: conn-limit =="
  start_server "${EDGE_MAX_CONN}"
  wrk -t2 -c"${EDGE_MAX_CONN}" -d"${EDGE_HOLD_DURATION}" "http://${HOST}:${PORT}/sleep/${EDGE_SLEEP_MS}" >/dev/null 2>&1 &
  local hold_pid=$!
  sleep "${EDGE_WARMUP_SEC}"
  wrk --latency -t2 -c8 -d"${EDGE_PROBE_DURATION}" -s "${STATUS_LUA}" "http://${HOST}:${PORT}/ping"
  wait "${hold_pid}" || true
}

run_edge() {
  start_server 0
  run_edge_blocked
  cleanup
  run_edge_conn_limit
}

main() {
  local scenario=${1:-all}
  init_logging "${scenario}"
  require_cmd wrk
  require_cmd curl
  ensure_server_bin
  trap cleanup EXIT INT TERM

  case "${scenario}" in
    throughput)
      start_server 0
      run_throughput
      ;;
    mixed)
      start_server 0
      run_mixed
      ;;
    edge)
      run_edge
      ;;
    all)
      start_server 0
      run_throughput
      cleanup
      start_server 0
      run_mixed
      cleanup
      start_server 0
      run_edge_blocked
      cleanup
      run_edge_conn_limit
      ;;
    *)
      echo "Usage: $0 [throughput|mixed|edge|all]" >&2
      exit 1
      ;;
  esac
}

main "$@"
