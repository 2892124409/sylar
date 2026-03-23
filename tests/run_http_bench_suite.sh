#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
BIN_PATH="${BUILD_DIR}/bin/http_bench_server"
PORT="${PORT:-18080}"
HOST="${HOST:-127.0.0.1}"
CLIENT_THREADS="${CLIENT_THREADS:-8}"
WRK_DURATION="${WRK_DURATION:-5s}"
WRK_TIMEOUT="${WRK_TIMEOUT:-5s}"
SERVER_WARMUP_SEC="${SERVER_WARMUP_SEC:-1}"
RESULT_DIR="${1:-${ROOT_DIR}/benchmarks/http_suite_$(date +%Y%m%d_%H%M%S)}"

mkdir -p "${RESULT_DIR}"
SUMMARY_CSV="${RESULT_DIR}/summary.csv"
SUMMARY_MD="${RESULT_DIR}/summary.md"

echo "tool,case_name,endpoint,body_mode,conn_mode,io_threads,session_enabled,client_threads,connections,duration_or_requests,total_requests,rps,avg_latency_ms,p99_latency_ms,transfer_per_sec,errors,raw_output" > "${SUMMARY_CSV}"
cat > "${SUMMARY_MD}" <<'EOF'
# HTTP Benchmark Summary

| Tool | Case | Endpoint | Body | Conn | IO Threads | Session | Client Threads | Connections | Duration/Requests | Total Requests | RPS | Avg Latency (ms) | P99 (ms) | Transfer/s | Errors |
| --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | --- | --- |
EOF

SERVER_PID=""
SERVER_LOG=""

latency_to_ms() {
  local value="$1"
  awk -v value="${value}" '
    function trim(v) { sub(/^[[:space:]]+/, "", v); sub(/[[:space:]]+$/, "", v); return v }
    BEGIN {
      value = trim(value)
      if (value == "" || value == "NA") {
        print "NA"
      } else if (value ~ /us$/) {
        sub(/us$/, "", value)
        printf "%.3f", value / 1000.0
      } else if (value ~ /ms$/) {
        sub(/ms$/, "", value)
        printf "%.3f", value + 0
      } else if (value ~ /s$/) {
        sub(/s$/, "", value)
        printf "%.3f", value * 1000.0
      } else {
        printf "%.3f", value + 0
      }
    }'
}

append_summary() {
  local tool="$1"
  local case_name="$2"
  local endpoint="$3"
  local body_mode="$4"
  local conn_mode="$5"
  local io_threads="$6"
  local session_enabled="$7"
  local client_threads="$8"
  local connections="$9"
  local duration_or_requests="${10}"
  local total_requests="${11}"
  local rps="${12}"
  local avg_latency_ms="${13}"
  local p99_latency_ms="${14}"
  local transfer_per_sec="${15}"
  local errors="${16}"
  local raw_output="${17}"

  echo "${tool},${case_name},${endpoint},${body_mode},${conn_mode},${io_threads},${session_enabled},${client_threads},${connections},${duration_or_requests},${total_requests},${rps},${avg_latency_ms},${p99_latency_ms},${transfer_per_sec},${errors},${raw_output}" >> "${SUMMARY_CSV}"
  printf '| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |\n' \
    "${tool}" "${case_name}" "${endpoint}" "${body_mode}" "${conn_mode}" "${io_threads}" \
    "${session_enabled}" "${client_threads}" "${connections}" "${duration_or_requests}" \
    "${total_requests}" "${rps}" "${avg_latency_ms}" "${p99_latency_ms}" "${transfer_per_sec}" \
    "${errors}" >> "${SUMMARY_MD}"
}

build_bench_server() {
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}"
  cmake --build "${BUILD_DIR}" -j"$(nproc)" --target http_bench_server
}

stop_server() {
  if [[ -n "${SERVER_PID}" ]]; then
    kill -TERM "${SERVER_PID}" >/dev/null 2>&1 || true
    wait "${SERVER_PID}" >/dev/null 2>&1 || true
    SERVER_PID=""
  fi
}

start_server() {
  local io_threads="$1"
  local session_enabled="$2"
  SERVER_LOG="${RESULT_DIR}/server_io${io_threads}_session${session_enabled}.log"
  LD_LIBRARY_PATH="${BUILD_DIR}/lib" "${BIN_PATH}" \
    --host "${HOST}" \
    --port "${PORT}" \
    --io-threads "${io_threads}" \
    --accept-threads 1 \
    --session-enabled "${session_enabled}" \
    --keepalive-timeout-ms 5000 \
    --keepalive-max-requests 0 \
    > "${SERVER_LOG}" 2>&1 &
  SERVER_PID=$!

  for _ in $(seq 1 80); do
    if curl -fsS "http://${HOST}:${PORT}/ping" >/dev/null 2>&1; then
      sleep "${SERVER_WARMUP_SEC}"
      return 0
    fi
    sleep 0.25
  done

  echo "[ERROR] server failed to start, log=${SERVER_LOG}" >&2
  cat "${SERVER_LOG}" >&2 || true
  return 1
}

run_wrk_case() {
  local case_name="$1"
  local endpoint="$2"
  local body_mode="$3"
  local conn_mode="$4"
  local io_threads="$5"
  local session_enabled="$6"
  local connections="$7"
  local script_path="${8:-}"

  local raw_out="${RESULT_DIR}/${case_name}.wrk.out"
  local -a cmd=(wrk -t"${CLIENT_THREADS}" -c"${connections}" -d"${WRK_DURATION}" --timeout "${WRK_TIMEOUT}" --latency)
  if [[ "${conn_mode}" == "short" ]]; then
    cmd+=(-H "Connection: close")
  fi
  if [[ -n "${script_path}" ]]; then
    cmd+=(-s "${script_path}")
  fi
  cmd+=("http://${HOST}:${PORT}${endpoint}")

  "${cmd[@]}" | tee "${raw_out}" >/dev/null

  local total_requests rps avg_raw p99_raw avg_ms p99_ms transfer errors
  total_requests="$(awk '/requests in/ {gsub(/,/, "", $1); print $1; exit}' "${raw_out}")"
  rps="$(awk '/Requests\/sec:/ {print $2; exit}' "${raw_out}")"
  avg_raw="$(awk '/Latency/ && $2 ~ /[0-9]/ {print $2; exit}' "${raw_out}")"
  p99_raw="$(awk '$1 == "99%" {print $2; exit}' "${raw_out}")"
  avg_ms="$(latency_to_ms "${avg_raw}")"
  p99_ms="$(latency_to_ms "${p99_raw}")"
  transfer="$(awk '/Transfer\/sec:/ {print $2; exit}' "${raw_out}")"
  errors="$(awk -F': ' '/Socket errors:/ {print $2; exit}' "${raw_out}")"
  if [[ -z "${errors}" ]]; then
    errors="0"
  fi

  append_summary "wrk" "${case_name}" "${endpoint}" "${body_mode}" "${conn_mode}" \
    "${io_threads}" "${session_enabled}" "${CLIENT_THREADS}" "${connections}" "${WRK_DURATION}" \
    "${total_requests}" "${rps}" "${avg_ms}" "${p99_ms}" "${transfer}" "${errors}" "${raw_out}"
}

trap stop_server EXIT

build_bench_server

echo "[INFO] results -> ${RESULT_DIR}"

for io_threads in 1 4 8 16; do
  start_server "${io_threads}" 0
  run_wrk_case "wrk_ping_io${io_threads}_c256" "/ping" "none" "keepalive" "${io_threads}" 0 256
  stop_server
done

start_server 8 0
run_wrk_case "wrk_ping_c64" "/ping" "none" "keepalive" 8 0 64
run_wrk_case "wrk_ping_c256" "/ping" "none" "keepalive" 8 0 256
run_wrk_case "wrk_ping_c1024" "/ping" "none" "keepalive" 8 0 1024
run_wrk_case "wrk_echo_content_length_c256" "/echo" "content-length" "keepalive" 8 0 256 "${ROOT_DIR}/tests/wrk_echo_raw.lua"
run_wrk_case "wrk_echo_chunked_c256" "/echo" "chunked" "keepalive" 8 0 256 "${ROOT_DIR}/tests/wrk_echo_chunked.lua"
stop_server

start_server 8 1
run_wrk_case "wrk_ping_session_on_c256" "/ping" "none" "keepalive" 8 1 256
stop_server

start_server 8 0
run_wrk_case "wrk_ping_short_c256" "/ping" "none" "short" 8 0 256
run_wrk_case "wrk_echo_short_c256" "/echo" "content-length" "short" 8 0 256 "${ROOT_DIR}/tests/wrk_echo_raw_close.lua"
stop_server

echo "[INFO] summary csv: ${SUMMARY_CSV}"
echo "[INFO] summary md: ${SUMMARY_MD}"
