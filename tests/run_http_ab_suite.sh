#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-18080}"
IO_THREADS="${IO_THREADS:-8}"
ACCEPT_THREADS="${ACCEPT_THREADS:-1}"
CLIENT_THREADS="${CLIENT_THREADS:-8}"
WRK_DURATION="${WRK_DURATION:-120s}"
WRK_TIMEOUT="${WRK_TIMEOUT:-5s}"
ROUNDS="${ROUNDS:-10}"
SERVER_WARMUP_SEC="${SERVER_WARMUP_SEC:-1}"
REQUEST_WARMUP_SEC="${REQUEST_WARMUP_SEC:-20}"
KEEPALIVE_TIMEOUT_MS="${KEEPALIVE_TIMEOUT_MS:-5000}"
KEEPALIVE_MAX_REQUESTS="${KEEPALIVE_MAX_REQUESTS:-0}"
MAX_CONNECTIONS="${MAX_CONNECTIONS:-0}"
SERVER_STOP_GRACE_SEC="${SERVER_STOP_GRACE_SEC:-3}"
SERVER_CPUSET="${SERVER_CPUSET:-}"
CLIENT_CPUSET="${CLIENT_CPUSET:-}"
RESULT_DIR="${1:-${ROOT_DIR}/benchmarks/http_ab_suite_$(date +%Y%m%d_%H%M%S)}"

BUILD_LOCAL_DIR="${ROOT_DIR}/build-ab-local"
BUILD_UPSTREAM_DIR="${ROOT_DIR}/build-ab-upstream"

SERVER_PID=""
SERVER_LOG=""
ACTIVE_IMPL=""
ACTIVE_PROTOCOL=""

mkdir -p "${RESULT_DIR}"
SUMMARY_CSV="${RESULT_DIR}/summary.csv"
SUMMARY_MD="${RESULT_DIR}/summary.md"

echo "impl,protocol,round,case_name,endpoint,body_mode,conn_mode,client_threads,connections,duration,total_requests,rps,avg_latency_ms,p99_latency_ms,transfer_per_sec,socket_errors,cpu_user_pct,cpu_sys_pct,rss_kb,ctx_voluntary,ctx_nonvoluntary,raw_output" > "${SUMMARY_CSV}"
cat > "${SUMMARY_MD}" <<'EOF'
# HTTP A/B Benchmark Summary

| Impl | Protocol | Round | Case | Endpoint | Body | Conn | Threads | Conns | Duration | Requests | RPS | Avg Lat (ms) | P99 (ms) | Transfer/s | Socket Errors | CPU usr% | CPU sys% | RSS KB | Ctx Vol | Ctx NonVol |
| --- | --- | ---: | --- | --- | --- | --- | ---: | ---: | --- | ---: | ---: | ---: | ---: | --- | --- | ---: | ---: | ---: | ---: | ---: |
EOF

HTTP_HASH_BASELINE=""
CLK_TCK="$(getconf CLK_TCK)"

CASES=(
  "ping_c64|/ping|none|keepalive|64|"
  "ping_c256|/ping|none|keepalive|256|"
  "ping_c1024|/ping|none|keepalive|1024|"
  "echo_content_length_c256|/echo|content-length|keepalive|256|${ROOT_DIR}/tests/wrk_echo_raw.lua"
  "echo_chunked_c256|/echo|chunked|keepalive|256|${ROOT_DIR}/tests/wrk_echo_chunked.lua"
  "ping_short_c256|/ping|none|short|256|"
  "echo_short_c256|/echo|content-length|short|256|${ROOT_DIR}/tests/wrk_echo_raw_close.lua"
)

require_cmd() {
  local name="$1"
  if ! command -v "${name}" >/dev/null 2>&1; then
    echo "[ERROR] required command not found: ${name}" >&2
    exit 1
  fi
}

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

duration_to_seconds() {
  local value="$1"
  if [[ "${value}" =~ ^([0-9]+)s$ ]]; then
    echo "${BASH_REMATCH[1]}"
    return 0
  fi
  if [[ "${value}" =~ ^([0-9]+)$ ]]; then
    echo "${BASH_REMATCH[1]}"
    return 0
  fi
  echo "invalid WRK_DURATION format: ${value}, expected <number>s" >&2
  exit 1
}

compute_http_hash() {
  find "${ROOT_DIR}/src/http" -type f \( -name "*.h" -o -name "*.cc" \) \
    | LC_ALL=C sort \
    | xargs sha256sum \
    | sha256sum \
    | awk '{print $1}'
}

ensure_http_unchanged() {
  local now_hash
  now_hash="$(compute_http_hash)"
  if [[ "${HTTP_HASH_BASELINE}" != "${now_hash}" ]]; then
    echo "[ERROR] src/http changed during benchmark run (expected ${HTTP_HASH_BASELINE}, got ${now_hash})" >&2
    exit 1
  fi
}

build_variant() {
  local impl="$1"
  local build_dir="$2"
  local cmake_variant="$3"

  cmake -S "${ROOT_DIR}" -B "${build_dir}" -DSYLAR_NET_VARIANT="${cmake_variant}"
  cmake --build "${build_dir}" -j"$(nproc)" --target http_bench_server
}

generate_tls_cert() {
  local cert_dir="${RESULT_DIR}/certs"
  mkdir -p "${cert_dir}"
  local cert_file="${cert_dir}/server.crt"
  local key_file="${cert_dir}/server.key"
  openssl req -x509 -newkey rsa:2048 -sha256 -nodes \
    -keyout "${key_file}" \
    -out "${cert_file}" \
    -days 3650 \
    -subj "/CN=localhost" >/dev/null 2>&1
  echo "${cert_file}|${key_file}"
}

stop_server() {
  if [[ -n "${SERVER_PID}" ]]; then
    kill -TERM "${SERVER_PID}" >/dev/null 2>&1 || true
    local wait_s=0
    while kill -0 "${SERVER_PID}" >/dev/null 2>&1; do
      if (( wait_s >= SERVER_STOP_GRACE_SEC * 10 )); then
        echo "[WARN] force killing stuck server pid=${SERVER_PID} impl=${ACTIVE_IMPL} protocol=${ACTIVE_PROTOCOL}" >&2
        kill -KILL "${SERVER_PID}" >/dev/null 2>&1 || true
        break
      fi
      sleep 0.1
      wait_s=$((wait_s + 1))
    done
    wait "${SERVER_PID}" >/dev/null 2>&1 || true
    SERVER_PID=""
  fi
}

wait_server_ready() {
  local protocol="$1"
  local curl_opts=(-fsS)
  if [[ "${protocol}" == "https" ]]; then
    curl_opts+=(-k)
  fi
  for _ in $(seq 1 120); do
    if curl "${curl_opts[@]}" "${protocol}://${HOST}:${PORT}/ping" >/dev/null 2>&1; then
      sleep "${SERVER_WARMUP_SEC}"
      return 0
    fi
    sleep 0.25
  done
  return 1
}

start_server() {
  local impl="$1"
  local protocol="$2"
  local cert_file="$3"
  local key_file="$4"

  ensure_http_unchanged
  stop_server

  local build_dir bin_path tls_flag
  if [[ "${impl}" == "local" ]]; then
    build_dir="${BUILD_LOCAL_DIR}"
  else
    build_dir="${BUILD_UPSTREAM_DIR}"
  fi
  bin_path="${build_dir}/bin/http_bench_server"

  tls_flag=0
  if [[ "${protocol}" == "https" ]]; then
    tls_flag=1
  fi

  SERVER_LOG="${RESULT_DIR}/server_${impl}_${protocol}.log"
  ACTIVE_IMPL="${impl}"
  ACTIVE_PROTOCOL="${protocol}"
  local -a server_cmd=()
  if [[ -n "${SERVER_CPUSET}" ]]; then
    server_cmd+=(taskset -c "${SERVER_CPUSET}")
  fi
  server_cmd+=(env "LD_LIBRARY_PATH=${build_dir}/lib:${LD_LIBRARY_PATH:-}" "${bin_path}")
  "${server_cmd[@]}" \
    --host "${HOST}" \
    --port "${PORT}" \
    --io-threads "${IO_THREADS}" \
    --accept-threads "${ACCEPT_THREADS}" \
    --session-enabled 0 \
    --keepalive-timeout-ms "${KEEPALIVE_TIMEOUT_MS}" \
    --keepalive-max-requests "${KEEPALIVE_MAX_REQUESTS}" \
    --max-connections "${MAX_CONNECTIONS}" \
    --tls "${tls_flag}" \
    --cert-file "${cert_file}" \
    --key-file "${key_file}" \
    > "${SERVER_LOG}" 2>&1 &

  SERVER_PID=$!
  if ! wait_server_ready "${protocol}"; then
    echo "[ERROR] server failed to start: impl=${impl} protocol=${protocol} log=${SERVER_LOG}" >&2
    cat "${SERVER_LOG}" >&2 || true
    exit 1
  fi
}

read_proc_cpu_ticks() {
  local pid="$1"
  awk '{print $14, $15}' "/proc/${pid}/stat"
}

read_proc_ctx_switches() {
  local pid="$1"
  awk '
    /voluntary_ctxt_switches/ {v=$2}
    /nonvoluntary_ctxt_switches/ {nv=$2}
    END {print v+0, nv+0}
  ' "/proc/${pid}/status"
}

read_proc_rss_kb() {
  local pid="$1"
  awk '
    /VmRSS:/ {print $2; found=1}
    END { if(!found) print 0 }
  ' "/proc/${pid}/status"
}

append_summary() {
  local impl="$1"
  local protocol="$2"
  local round="$3"
  local case_name="$4"
  local endpoint="$5"
  local body_mode="$6"
  local conn_mode="$7"
  local client_threads="$8"
  local connections="$9"
  local duration="${10}"
  local total_requests="${11}"
  local rps="${12}"
  local avg_latency_ms="${13}"
  local p99_latency_ms="${14}"
  local transfer_per_sec="${15}"
  local socket_errors="${16}"
  local cpu_user_pct="${17}"
  local cpu_sys_pct="${18}"
  local rss_kb="${19}"
  local ctx_vol="${20}"
  local ctx_nonvol="${21}"
  local raw_output="${22}"

  echo "${impl},${protocol},${round},${case_name},${endpoint},${body_mode},${conn_mode},${client_threads},${connections},${duration},${total_requests},${rps},${avg_latency_ms},${p99_latency_ms},${transfer_per_sec},${socket_errors},${cpu_user_pct},${cpu_sys_pct},${rss_kb},${ctx_vol},${ctx_nonvol},${raw_output}" >> "${SUMMARY_CSV}"
  printf '| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |\n' \
    "${impl}" "${protocol}" "${round}" "${case_name}" "${endpoint}" "${body_mode}" "${conn_mode}" \
    "${client_threads}" "${connections}" "${duration}" "${total_requests}" "${rps}" "${avg_latency_ms}" \
    "${p99_latency_ms}" "${transfer_per_sec}" "${socket_errors}" "${cpu_user_pct}" "${cpu_sys_pct}" \
    "${rss_kb}" "${ctx_vol}" "${ctx_nonvol}" >> "${SUMMARY_MD}"
}

run_case() {
  local impl="$1"
  local protocol="$2"
  local round="$3"
  local case_name="$4"
  local endpoint="$5"
  local body_mode="$6"
  local conn_mode="$7"
  local connections="$8"
  local script_path="$9"

  local duration_sec raw_out total_requests rps avg_raw p99_raw avg_ms p99_ms transfer errors
  local start_user_tick start_sys_tick end_user_tick end_sys_tick
  local start_ctx_v start_ctx_nv end_ctx_v end_ctx_nv rss_kb
  local cpu_user_pct cpu_sys_pct
  local url

  duration_sec="$(duration_to_seconds "${WRK_DURATION}")"
  raw_out="${RESULT_DIR}/${impl}_${protocol}_r${round}_${case_name}.wrk.out"
  url="${protocol}://${HOST}:${PORT}${endpoint}"

  read -r start_user_tick start_sys_tick <<<"$(read_proc_cpu_ticks "${SERVER_PID}")"
  read -r start_ctx_v start_ctx_nv <<<"$(read_proc_ctx_switches "${SERVER_PID}")"

  local -a cmd=(wrk -t"${CLIENT_THREADS}" -c"${connections}" -d"${WRK_DURATION}" --timeout "${WRK_TIMEOUT}" --latency)
  if [[ "${conn_mode}" == "short" ]]; then
    cmd+=(-H "Connection: close")
  fi
  if [[ -n "${script_path}" ]]; then
    cmd+=(-s "${script_path}")
  fi
  cmd+=("${url}")

  if [[ -n "${CLIENT_CPUSET}" ]]; then
    taskset -c "${CLIENT_CPUSET}" "${cmd[@]}" | tee "${raw_out}" >/dev/null
  else
    "${cmd[@]}" | tee "${raw_out}" >/dev/null
  fi

  read -r end_user_tick end_sys_tick <<<"$(read_proc_cpu_ticks "${SERVER_PID}")"
  read -r end_ctx_v end_ctx_nv <<<"$(read_proc_ctx_switches "${SERVER_PID}")"
  rss_kb="$(read_proc_rss_kb "${SERVER_PID}")"

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

  cpu_user_pct="$(awk -v s="${start_user_tick}" -v e="${end_user_tick}" -v hz="${CLK_TCK}" -v sec="${duration_sec}" \
    'BEGIN {dt=e-s; if(sec<=0||hz<=0){print "NA"} else printf "%.2f", (dt/hz)*100/sec}')"
  cpu_sys_pct="$(awk -v s="${start_sys_tick}" -v e="${end_sys_tick}" -v hz="${CLK_TCK}" -v sec="${duration_sec}" \
    'BEGIN {dt=e-s; if(sec<=0||hz<=0){print "NA"} else printf "%.2f", (dt/hz)*100/sec}')"

  append_summary "${impl}" "${protocol}" "${round}" "${case_name}" "${endpoint}" "${body_mode}" "${conn_mode}" \
    "${CLIENT_THREADS}" "${connections}" "${WRK_DURATION}" "${total_requests}" "${rps}" "${avg_ms}" "${p99_ms}" \
    "${transfer}" "${errors}" "${cpu_user_pct}" "${cpu_sys_pct}" "${rss_kb}" \
    "$((end_ctx_v - start_ctx_v))" "$((end_ctx_nv - start_ctx_nv))" "${raw_out}"

  sleep "${REQUEST_WARMUP_SEC}"
}

trap stop_server EXIT

require_cmd cmake
require_cmd wrk
require_cmd curl
require_cmd openssl

HTTP_HASH_BASELINE="$(compute_http_hash)"

build_variant "local" "${BUILD_LOCAL_DIR}" "local"
build_variant "upstream_ref" "${BUILD_UPSTREAM_DIR}" "upstream_ref"

IFS='|' read -r TLS_CERT_FILE TLS_KEY_FILE <<<"$(generate_tls_cert)"

echo "[INFO] result_dir=${RESULT_DIR}"
echo "[INFO] http_hash=${HTTP_HASH_BASELINE}"

for round in $(seq 1 "${ROUNDS}"); do
  if (( round % 2 == 1 )); then
    ORDER=("local" "upstream_ref")
  else
    ORDER=("upstream_ref" "local")
  fi

  for impl in "${ORDER[@]}"; do
    for protocol in "http" "https"; do
      start_server "${impl}" "${protocol}" "${TLS_CERT_FILE}" "${TLS_KEY_FILE}"
      for item in "${CASES[@]}"; do
        IFS='|' read -r case_name endpoint body_mode conn_mode connections script_path <<<"${item}"
        run_case "${impl}" "${protocol}" "${round}" "${case_name}" "${endpoint}" "${body_mode}" "${conn_mode}" "${connections}" "${script_path}"
      done
      stop_server
    done
  done
done

echo "[INFO] benchmark completed"
echo "[INFO] summary csv: ${SUMMARY_CSV}"
echo "[INFO] summary md: ${SUMMARY_MD}"
