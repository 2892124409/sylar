#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
SERVER_BIN="${ROOT_DIR}/bin/http_bench_server"
POST_LUA="${ROOT_DIR}/benchmarks/http/post.lua"

HOST=${HOST:-127.0.0.1}
PORT=${PORT:-18080}
SERVER_PID=""

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
    kill "${SERVER_PID}" 2>/dev/null || true
    wait "${SERVER_PID}" 2>/dev/null || true
  fi
  SERVER_PID=""
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
    --io-threads 4 \
    --accept-threads 1 \
    --shared-stack "${ss}" \
    --fiber-pool "${fp}" \
    --use-caller "${uc}" &
  SERVER_PID=$!
  wait_until_ready
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
        
        start_server "${ss}" "${fp}" "${uc}"

        echo ">>> Baseline: /ping (纯 CPU 路由, C=500, T=4, 10s)"
        wrk -t4 -c500 -d10s "http://${HOST}:${PORT}/ping" | grep -E "Requests/sec|Latency"
        
        echo ""
        echo ">>> IO Intensive: /api/user/profile (20ms 延时, C=1000, T=4, 10s)"
        wrk -t4 -c1000 -d10s "http://${HOST}:${PORT}/api/user/profile" | grep -E "Requests/sec|Latency"

        echo ""
        echo ">>> POST & Parse: /api/data/upload (JSON POST, C=500, T=4, 10s)"
        wrk -t4 -c500 -d10s -s "${POST_LUA}" "http://${HOST}:${PORT}/api/data/upload" | grep -E "Requests/sec|Latency"

        echo ""
        echo ">>> Large Payload: /api/file/download (1MB 返回, C=50, T=4, 10s)"
        wrk -t4 -c50 -d10s "http://${HOST}:${PORT}/api/file/download" | grep -E "Requests/sec|Latency"

        cleanup
      done
    done
  done
}

main() {
  require_cmd wrk
  require_cmd curl
  ensure_server_bin
  trap cleanup EXIT INT TERM
  
  run_matrix
}

main "$@"
