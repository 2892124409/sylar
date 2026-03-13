#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
RESULTS_ROOT="${ROOT_DIR}/benchmarks/http/results"
SERVER_BIN="${ROOT_DIR}/bin/http_bench_server"
RUN_SCRIPT="${ROOT_DIR}/benchmarks/http/run.sh"
MATRIX_SCRIPT="${ROOT_DIR}/benchmarks/http/run_matrix_bench.sh"
SUMMARY_SCRIPT="${ROOT_DIR}/benchmarks/http/summarize_bench.py"
STATUS_LUA="${ROOT_DIR}/benchmarks/http/status_count.lua"
POST_STATUS_LUA="${ROOT_DIR}/benchmarks/http/post_status.lua"

HOST=${HOST:-127.0.0.1}
PORT=${PORT:-18080}
BUILD_DIR=${BUILD_DIR:-"${ROOT_DIR}/build-release"}
BUILD_JOBS=${BUILD_JOBS:-8}
FULL_BENCH_TAG=${FULL_BENCH_TAG:-release_full_pipeline}
SERVER_IO_THREADS=${SERVER_IO_THREADS:-8}
SERVER_ACCEPT_THREADS=${SERVER_ACCEPT_THREADS:-2}
SERVER_SESSION_ENABLED=${SERVER_SESSION_ENABLED:-0}
SERVER_FIBER_STACK_SIZE=${SERVER_FIBER_STACK_SIZE:-1048576}

STABILITY_EDGE_RUNS=${STABILITY_EDGE_RUNS:-10}
STABILITY_THROUGHPUT_RUNS=${STABILITY_THROUGHPUT_RUNS:-3}
STABILITY_THROUGHPUT_THREADS=${STABILITY_THROUGHPUT_THREADS:-4}
STABILITY_THROUGHPUT_CONNECTIONS=${STABILITY_THROUGHPUT_CONNECTIONS:-256}
STABILITY_THROUGHPUT_DURATION=${STABILITY_THROUGHPUT_DURATION:-60s}
EDGE_MAX_CONN=${EDGE_MAX_CONN:-16}
EDGE_SLEEP_MS=${EDGE_SLEEP_MS:-1500}
EDGE_BLOCKED_DURATION=${EDGE_BLOCKED_DURATION:-5s}
EDGE_HOLD_DURATION=${EDGE_HOLD_DURATION:-3s}
EDGE_PROBE_DURATION=${EDGE_PROBE_DURATION:-1s}
EDGE_WARMUP_SEC=${EDGE_WARMUP_SEC:-1}

BASELINE_WRK_THREADS=${BASELINE_WRK_THREADS:-4}
BASELINE_WRK_CONNECTIONS=${BASELINE_WRK_CONNECTIONS:-256}
BASELINE_WRK_DURATION=${BASELINE_WRK_DURATION:-10s}
MATRIX_WRK_THREADS=${MATRIX_WRK_THREADS:-4}
MATRIX_WRK_DURATION=${MATRIX_WRK_DURATION:-10s}

SWEEP_THREADS=${SWEEP_THREADS:-4}
SWEEP_DURATION=${SWEEP_DURATION:-15s}
SWEEP_SHARED_STACK=${SWEEP_SHARED_STACK:-0}
SWEEP_FIBER_POOL=${SWEEP_FIBER_POOL:-1}
SWEEP_USE_CALLER=${SWEEP_USE_CALLER:-1}
SWEEP_PING_CONNECTIONS=${SWEEP_PING_CONNECTIONS:-"64 128 256 512 1024"}
SWEEP_PROFILE_CONNECTIONS=${SWEEP_PROFILE_CONNECTIONS:-"64 128 256 512"}
SWEEP_UPLOAD_CONNECTIONS=${SWEEP_UPLOAD_CONNECTIONS:-"64 128 256 512"}
SWEEP_DOWNLOAD_CONNECTIONS=${SWEEP_DOWNLOAD_CONNECTIONS:-"20 50 100 200"}

RUN_DATE=$(date +%F)
RUN_DIR="${RESULTS_ROOT}/${RUN_DATE}/${FULL_BENCH_TAG}"
PHASE1_DIR="${RUN_DIR}/phase1"
PHASE3_DIR="${RUN_DIR}/phase3"
PIPELINE_LOG="${RUN_DIR}/pipeline.log"
PHASE1_AGGREGATE_LOG="${RUN_DIR}/phase1_stability.log"
PHASE2_BASELINE_LOG="${RUN_DIR}/phase2_baseline.log"
PHASE2_MATRIX_LOG="${RUN_DIR}/phase2_matrix.log"
PHASE3_AGGREGATE_LOG="${RUN_DIR}/phase3_sweep.log"

SERVER_PID=""
PHASE1_LOGS=()
PHASE3_LOGS=()

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required command: $1" >&2
    exit 1
  fi
}

assert_file() {
  if [[ ! -f "$1" ]]; then
    echo "Missing required file: $1" >&2
    exit 1
  fi
}

cleanup() {
  if [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" 2>/dev/null; then
    # 正式流水线不把 stop-path 崩溃混入数据，统一用强制回收结束 server。
    kill -KILL "${SERVER_PID}" 2>/dev/null || true
    wait "${SERVER_PID}" 2>/dev/null || true
    sleep 0.1
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
  local max_connections=$1
  local shared_stack=$2
  local fiber_pool=$3
  local use_caller=$4

  cleanup
  "${SERVER_BIN}" \
    --host "${HOST}" \
    --port "${PORT}" \
    --io-threads "${SERVER_IO_THREADS}" \
    --accept-threads "${SERVER_ACCEPT_THREADS}" \
    --max-connections "${max_connections}" \
    --session-enabled "${SERVER_SESSION_ENABLED}" \
    --fiber-stack-size "${SERVER_FIBER_STACK_SIZE}" \
    --shared-stack "${shared_stack}" \
    --fiber-pool "${fiber_pool}" \
    --use-caller "${use_caller}" \
    --keepalive-timeout-ms 5000 \
    --keepalive-max-requests 0 &
  SERVER_PID=$!
  wait_until_ready
}

ensure_run_dir() {
  if [[ -e "${RUN_DIR}" ]]; then
    echo "Result directory already exists: ${RUN_DIR}" >&2
    echo "Override FULL_BENCH_TAG to keep runs separate." >&2
    exit 1
  fi

  mkdir -p "${PHASE1_DIR}" "${PHASE3_DIR}"
  exec > >(tee "${PIPELINE_LOG}") 2>&1

  echo "Run directory: ${RUN_DIR}"
  echo "Pipeline log: ${PIPELINE_LOG}"
}

ensure_release_build() {
  echo ""
  echo "===== Preflight: release build ====="
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build "${BUILD_DIR}" --target http_bench_server test_http_server test_memory_pool -j"${BUILD_JOBS}"

  if [[ ! -x "${SERVER_BIN}" ]]; then
    echo "Expected release build to produce ${SERVER_BIN}" >&2
    exit 1
  fi

  "${ROOT_DIR}/bin/test_memory_pool"
  "${ROOT_DIR}/bin/test_http_server" >/dev/null
}

append_aggregate_log() {
  local aggregate_log=$1
  shift
  : > "${aggregate_log}"
  for log_file in "$@"; do
    {
      echo "===== $(basename "${log_file}") ====="
      cat "${log_file}"
      echo ""
    } >> "${aggregate_log}"
  done
}

extract_status_line() {
  local log_file=$1
  local line_number=$2
  grep -E "status_1xx=|status_2xx=|status_3xx=|status_4xx=|status_5xx=|status_other=" "${log_file}" | sed -n "${line_number}p"
}

extract_status_value() {
  local line=$1
  local key=$2
  echo "${line}" | sed -n "s/.*${key}=\\([0-9][0-9]*\\).*/\\1/p"
}

validate_no_crash() {
  local log_file=$1
  if grep -Eq "Segmentation fault|core dumped" "${log_file}"; then
    echo "Detected crash in ${log_file}" >&2
    exit 1
  fi
}

validate_edge_log() {
  local log_file=$1
  validate_no_crash "${log_file}"

  local blocked_line
  local conn_line
  blocked_line=$(extract_status_line "${log_file}" 1)
  conn_line=$(extract_status_line "${log_file}" 2)

  if [[ -z "${blocked_line}" || -z "${conn_line}" ]]; then
    echo "Missing status summary in ${log_file}" >&2
    exit 1
  fi

  local blocked_4xx blocked_5xx conn_4xx conn_5xx
  blocked_4xx=$(extract_status_value "${blocked_line}" "status_4xx")
  blocked_5xx=$(extract_status_value "${blocked_line}" "status_5xx")
  conn_4xx=$(extract_status_value "${conn_line}" "status_4xx")
  conn_5xx=$(extract_status_value "${conn_line}" "status_5xx")

  if [[ "${blocked_4xx:-0}" -le 0 || "${blocked_5xx:-0}" -ne 0 ]]; then
    echo "Unexpected blocked status distribution in ${log_file}: ${blocked_line}" >&2
    exit 1
  fi

  if [[ "${conn_5xx:-0}" -le 0 || "${conn_4xx:-0}" -ne 0 ]]; then
    echo "Unexpected conn-limit status distribution in ${log_file}: ${conn_line}" >&2
    exit 1
  fi
}

validate_throughput_log() {
  local log_file=$1
  validate_no_crash "${log_file}"

  local rps
  rps=$(sed -n 's/^Requests\/sec:[[:space:]]*//p' "${log_file}" | tail -n1 | tr -d ' ')
  if [[ -z "${rps}" ]]; then
    echo "Missing Requests/sec in ${log_file}" >&2
    exit 1
  fi

  awk -v value="${rps}" 'BEGIN { exit !(value > 0) }' || {
    echo "Invalid Requests/sec in ${log_file}: ${rps}" >&2
    exit 1
  }
}

run_phase1() {
  echo ""
  echo "===== Phase 1: stability regression ====="

  local log_file
  local run_index

  for run_index in $(seq 1 "${STABILITY_EDGE_RUNS}"); do
    log_file="${PHASE1_DIR}/edge_run_$(printf '%02d' "${run_index}").log"
    echo ""
    echo "[Phase1][edge] run ${run_index}/${STABILITY_EDGE_RUNS}: ${log_file}"
    RESULT_LOG="${log_file}" \
      HOST="${HOST}" \
      PORT="${PORT}" \
      SERVER_IO_THREADS="${SERVER_IO_THREADS}" \
      SERVER_ACCEPT_THREADS="${SERVER_ACCEPT_THREADS}" \
      SERVER_SESSION_ENABLED="${SERVER_SESSION_ENABLED}" \
      EDGE_MAX_CONN="${EDGE_MAX_CONN}" \
      EDGE_SLEEP_MS="${EDGE_SLEEP_MS}" \
      EDGE_BLOCKED_DURATION="${EDGE_BLOCKED_DURATION}" \
      EDGE_HOLD_DURATION="${EDGE_HOLD_DURATION}" \
      EDGE_PROBE_DURATION="${EDGE_PROBE_DURATION}" \
      EDGE_WARMUP_SEC="${EDGE_WARMUP_SEC}" \
      "${RUN_SCRIPT}" edge
    validate_edge_log "${log_file}"
    PHASE1_LOGS+=("${log_file}")
  done

  for run_index in $(seq 1 "${STABILITY_THROUGHPUT_RUNS}"); do
    log_file="${PHASE1_DIR}/throughput_run_$(printf '%02d' "${run_index}").log"
    echo ""
    echo "[Phase1][throughput] run ${run_index}/${STABILITY_THROUGHPUT_RUNS}: ${log_file}"
    RESULT_LOG="${log_file}" \
      HOST="${HOST}" \
      PORT="${PORT}" \
      WRK_THREADS="${STABILITY_THROUGHPUT_THREADS}" \
      WRK_CONNECTIONS="${STABILITY_THROUGHPUT_CONNECTIONS}" \
      WRK_DURATION="${STABILITY_THROUGHPUT_DURATION}" \
      SERVER_IO_THREADS="${SERVER_IO_THREADS}" \
      SERVER_ACCEPT_THREADS="${SERVER_ACCEPT_THREADS}" \
      SERVER_SESSION_ENABLED="${SERVER_SESSION_ENABLED}" \
      "${RUN_SCRIPT}" throughput
    validate_throughput_log "${log_file}"
    PHASE1_LOGS+=("${log_file}")
  done

  append_aggregate_log "${PHASE1_AGGREGATE_LOG}" "${PHASE1_LOGS[@]}"
}

validate_phase2_baseline_log() {
  local log_file=$1
  validate_no_crash "${log_file}"

  for scenario in "== throughput ==" "== mixed ==" "== edge: blocked ==" "== edge: conn-limit =="; do
    if ! grep -Fq "${scenario}" "${log_file}"; then
      echo "Missing scenario marker '${scenario}' in ${log_file}" >&2
      exit 1
    fi
  done
}

validate_phase2_matrix_log() {
  local log_file=$1
  validate_no_crash "${log_file}"

  local config_count requests_count
  config_count=$(grep -c '^\[CONFIG\]' "${log_file}")
  requests_count=$(grep -c '^Requests/sec:' "${log_file}")

  if [[ "${config_count}" -ne 8 ]]; then
    echo "Expected 8 matrix configs in ${log_file}, found ${config_count}" >&2
    exit 1
  fi

  if [[ "${requests_count}" -ne 32 ]]; then
    echo "Expected 32 matrix Requests/sec entries in ${log_file}, found ${requests_count}" >&2
    exit 1
  fi
}

run_phase2() {
  echo ""
  echo "===== Phase 2: baseline and matrix ====="

  RESULT_LOG="${PHASE2_BASELINE_LOG}" \
    HOST="${HOST}" \
    PORT="${PORT}" \
    WRK_THREADS="${BASELINE_WRK_THREADS}" \
    WRK_CONNECTIONS="${BASELINE_WRK_CONNECTIONS}" \
    WRK_DURATION="${BASELINE_WRK_DURATION}" \
    SERVER_IO_THREADS="${SERVER_IO_THREADS}" \
    SERVER_ACCEPT_THREADS="${SERVER_ACCEPT_THREADS}" \
    SERVER_SESSION_ENABLED="${SERVER_SESSION_ENABLED}" \
    EDGE_MAX_CONN="${EDGE_MAX_CONN}" \
    EDGE_SLEEP_MS="${EDGE_SLEEP_MS}" \
    EDGE_BLOCKED_DURATION="${EDGE_BLOCKED_DURATION}" \
    EDGE_HOLD_DURATION="${EDGE_HOLD_DURATION}" \
    EDGE_PROBE_DURATION="${EDGE_PROBE_DURATION}" \
    EDGE_WARMUP_SEC="${EDGE_WARMUP_SEC}" \
    "${RUN_SCRIPT}" all
  validate_phase2_baseline_log "${PHASE2_BASELINE_LOG}"

  RESULT_LOG="${PHASE2_MATRIX_LOG}" \
    HOST="${HOST}" \
    PORT="${PORT}" \
    WRK_THREADS="${MATRIX_WRK_THREADS}" \
    WRK_DURATION="${MATRIX_WRK_DURATION}" \
    SERVER_IO_THREADS="${SERVER_IO_THREADS}" \
    SERVER_ACCEPT_THREADS="${SERVER_ACCEPT_THREADS}" \
    SERVER_SESSION_ENABLED="${SERVER_SESSION_ENABLED}" \
    "${MATRIX_SCRIPT}"
  validate_phase2_matrix_log "${PHASE2_MATRIX_LOG}"
}

run_sweep_case() {
  local endpoint_key=$1
  local path=$2
  local connections=$3
  local lua_script=$4
  local log_file="${PHASE3_DIR}/${endpoint_key}_c${connections}.log"

  {
    echo ">>> SWEEP endpoint=${endpoint_key} path=${path} connections=${connections} threads=${SWEEP_THREADS} duration=${SWEEP_DURATION}"
    echo "[CONFIG] shared_stack=${SWEEP_SHARED_STACK} | fiber_pool=${SWEEP_FIBER_POOL} | use_caller=${SWEEP_USE_CALLER}"
    start_server 0 "${SWEEP_SHARED_STACK}" "${SWEEP_FIBER_POOL}" "${SWEEP_USE_CALLER}"

    if [[ -n "${lua_script}" ]]; then
      wrk --latency -t"${SWEEP_THREADS}" -c"${connections}" -d"${SWEEP_DURATION}" -s "${lua_script}" "http://${HOST}:${PORT}${path}"
    else
      wrk --latency -t"${SWEEP_THREADS}" -c"${connections}" -d"${SWEEP_DURATION}" "http://${HOST}:${PORT}${path}"
    fi

    cleanup
  } > >(tee "${log_file}") 2>&1

  validate_no_crash "${log_file}"
  PHASE3_LOGS+=("${log_file}")
}

run_phase3() {
  echo ""
  echo "===== Phase 3: performance sweep ====="

  local connections

  for connections in ${SWEEP_PING_CONNECTIONS}; do
    run_sweep_case "ping" "/ping" "${connections}" "${STATUS_LUA}"
  done

  for connections in ${SWEEP_PROFILE_CONNECTIONS}; do
    run_sweep_case "profile" "/api/user/profile" "${connections}" "${STATUS_LUA}"
  done

  for connections in ${SWEEP_UPLOAD_CONNECTIONS}; do
    run_sweep_case "upload" "/api/data/upload" "${connections}" "${POST_STATUS_LUA}"
  done

  for connections in ${SWEEP_DOWNLOAD_CONNECTIONS}; do
    run_sweep_case "download" "/api/file/download" "${connections}" "${STATUS_LUA}"
  done

  append_aggregate_log "${PHASE3_AGGREGATE_LOG}" "${PHASE3_LOGS[@]}"
}

run_phase4() {
  echo ""
  echo "===== Phase 4: summary artifacts ====="
  python3 "${SUMMARY_SCRIPT}" "${RUN_DIR}"
}

print_help() {
  cat <<'EOF'
Usage: benchmarks/http/run_full_bench.sh

Runs the 4-phase HTTP benchmark pipeline:
  1. Stability regression
  2. Baseline + matrix
  3. Performance sweep
  4. Summary + interview notes

Key environment overrides:
  FULL_BENCH_TAG
  BUILD_DIR
  BUILD_JOBS
  SERVER_IO_THREADS
  SERVER_ACCEPT_THREADS
  STABILITY_EDGE_RUNS
  STABILITY_THROUGHPUT_RUNS
  STABILITY_THROUGHPUT_DURATION
  BASELINE_WRK_DURATION
  MATRIX_WRK_DURATION
  SWEEP_DURATION
  SWEEP_PING_CONNECTIONS
  SWEEP_PROFILE_CONNECTIONS
  SWEEP_UPLOAD_CONNECTIONS
  SWEEP_DOWNLOAD_CONNECTIONS
EOF
}

main() {
  if [[ "${1:-}" == "--help" ]]; then
    print_help
    return 0
  fi

  require_cmd cmake
  require_cmd curl
  require_cmd python3
  require_cmd wrk
  assert_file "${RUN_SCRIPT}"
  assert_file "${MATRIX_SCRIPT}"
  assert_file "${SUMMARY_SCRIPT}"
  assert_file "${STATUS_LUA}"
  assert_file "${POST_STATUS_LUA}"

  ensure_run_dir
  trap cleanup EXIT INT TERM

  ensure_release_build
  run_phase1
  run_phase2
  run_phase3
  run_phase4

  echo ""
  echo "Pipeline complete. Artifacts:"
  echo "  ${RUN_DIR}/summary.json"
  echo "  ${RUN_DIR}/summary.md"
  echo "  ${RUN_DIR}/interview_notes.md"
}

main "$@"
