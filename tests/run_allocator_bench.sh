#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
BIN_PATH="${BUILD_DIR}/bin/test_benchmark_tcp_allocator"

RAW_OUT="${ROOT_DIR}/result_allocator_matrix_raw.csv"
AVG_OUT="${ROOT_DIR}/result_allocator_matrix_avg.csv"
FAIL_OUT="${ROOT_DIR}/result_allocator_matrix_failures.csv"

IO_THREADS="${IO_THREADS:-${THREADS:-2,4,8}}"
MODES="${MODES:-a,b}"
WORKLOADS="${WORKLOADS:-persistent,short}"
REPEAT="${REPEAT:-3}"
RUN_TIMEOUT_SEC="${RUN_TIMEOUT_SEC:-120}"

CONNECTIONS="${CONNECTIONS:-64}"
REQUESTS_PER_CONN="${REQUESTS_PER_CONN:-200}"
SHORT_TOTAL_REQUESTS="${SHORT_TOTAL_REQUESTS:-10000}"
PAYLOAD_BYTES="${PAYLOAD_BYTES:-256}"
STARTUP_DELAY_MS="${STARTUP_DELAY_MS:-80}"

find_lib() {
    local name="$1"
    local path=""

    if command -v ldconfig >/dev/null 2>&1; then
        path="$(ldconfig -p | awk -v n="$name" '$1 ~ n {print $NF; exit}')"
    fi

    if [[ -z "${path}" ]]; then
        local dirs=(
            /usr/lib/x86_64-linux-gnu
            /usr/lib64
            /usr/lib
            /lib/x86_64-linux-gnu
            /lib64
            /lib
        )
        for d in "${dirs[@]}"; do
            [[ -d "${d}" ]] || continue
            local found
            found="$(ls "${d}" 2>/dev/null | grep -E "^${name}" | head -n 1 || true)"
            if [[ -n "${found}" ]]; then
                path="${d}/${found}"
                break
            fi
        done
    fi

    echo "${path}"
}

JEMALLOC_LIB="${JEMALLOC_LIB:-$(find_lib 'libjemalloc\\.so')}"
TCMALLOC_LIB="${TCMALLOC_LIB:-$(find_lib 'libtcmalloc(_minimal)?\\.so')}"

if [[ -z "${JEMALLOC_LIB}" || ! -f "${JEMALLOC_LIB}" ]]; then
    echo "[ERROR] jemalloc not found. Set JEMALLOC_LIB=/abs/path/to/libjemalloc.so" >&2
    exit 1
fi
if [[ -z "${TCMALLOC_LIB}" || ! -f "${TCMALLOC_LIB}" ]]; then
    echo "[ERROR] tcmalloc not found. Set TCMALLOC_LIB=/abs/path/to/libtcmalloc*.so" >&2
    exit 1
fi

echo "[INFO] JEMALLOC_LIB=${JEMALLOC_LIB}"
echo "[INFO] TCMALLOC_LIB=${TCMALLOC_LIB}"

echo "[INFO] Configuring build..."
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
echo "[INFO] Building benchmark target..."
cmake --build "${BUILD_DIR}" -j"$(nproc)" --target test_benchmark_tcp_allocator

echo "allocator,mode,workload,io_threads,run_id,total_requests,total_ms,req_per_sec,p50_us,p95_us,p99_us,max_us,errors,status,exit_code" >"${RAW_OUT}"
echo "allocator,mode,workload,io_threads,run_id,status,exit_code,stderr_hint" >"${FAIL_OUT}"

expected_total_requests() {
    local workload="$1"
    if [[ "${workload}" == "persistent" ]]; then
        echo $((CONNECTIONS * REQUESTS_PER_CONN))
    else
        echo "${SHORT_TOTAL_REQUESTS}"
    fi
}

append_failure_row() {
    local allocator="$1"
    local mode="$2"
    local workload="$3"
    local io_threads="$4"
    local run_id="$5"
    local status="$6"
    local exit_code="$7"
    local stderr_hint="$8"

    local total_requests
    total_requests="$(expected_total_requests "${workload}")"

    echo "${allocator},${mode},${workload},${io_threads},${run_id},${total_requests},0.000,0.000,0,0,0,0,${total_requests},${status},${exit_code}" >>"${RAW_OUT}"
    echo "${allocator},${mode},${workload},${io_threads},${run_id},${status},${exit_code},${stderr_hint}" >>"${FAIL_OUT}"
}

append_failure_meta() {
    local allocator="$1"
    local mode="$2"
    local workload="$3"
    local io_threads="$4"
    local run_id="$5"
    local status="$6"
    local exit_code="$7"
    local stderr_hint="$8"
    echo "${allocator},${mode},${workload},${io_threads},${run_id},${status},${exit_code},${stderr_hint}" >>"${FAIL_OUT}"
}

run_single_case() {
    local allocator="$1"
    local preload_lib="$2"
    local mode="$3"
    local workload="$4"
    local io_threads="$5"
    local run_id="$6"

    local out_file
    local err_file
    out_file="$(mktemp)"
    err_file="$(mktemp)"

    echo "[RUN] allocator=${allocator} mode=${mode} workload=${workload} io_threads=${io_threads} run_id=${run_id}"

    local rc
    set +e
    if [[ -n "${preload_lib}" ]]; then
        LD_PRELOAD="${preload_lib}" timeout --signal=TERM --kill-after=5 "${RUN_TIMEOUT_SEC}" "${BIN_PATH}" \
            --allocator_label="${allocator}" \
            --mode="${mode}" \
            --workload="${workload}" \
            --io_threads="${io_threads}" \
            --repeat=1 \
            --run_id="${run_id}" \
            --payload_bytes="${PAYLOAD_BYTES}" \
            --connections="${CONNECTIONS}" \
            --requests_per_conn="${REQUESTS_PER_CONN}" \
            --short_total_requests="${SHORT_TOTAL_REQUESTS}" \
            --startup_delay_ms="${STARTUP_DELAY_MS}" >"${out_file}" 2>"${err_file}"
        rc=$?
    else
        timeout --signal=TERM --kill-after=5 "${RUN_TIMEOUT_SEC}" "${BIN_PATH}" \
            --allocator_label="${allocator}" \
            --mode="${mode}" \
            --workload="${workload}" \
            --io_threads="${io_threads}" \
            --repeat=1 \
            --run_id="${run_id}" \
            --payload_bytes="${PAYLOAD_BYTES}" \
            --connections="${CONNECTIONS}" \
            --requests_per_conn="${REQUESTS_PER_CONN}" \
            --short_total_requests="${SHORT_TOTAL_REQUESTS}" \
            --startup_delay_ms="${STARTUP_DELAY_MS}" >"${out_file}" 2>"${err_file}"
        rc=$?
    fi
    set -e

    local data_line
    data_line="$(tail -n +2 "${out_file}" | head -n 1 || true)"

    if [[ ${rc} -eq 0 && -n "${data_line}" ]]; then
        echo "${data_line}" >>"${RAW_OUT}"

        local row_status
        local row_exit
        row_status="$(echo "${data_line}" | awk -F, '{print $14}')"
        row_exit="$(echo "${data_line}" | awk -F, '{print $15}')"
        if [[ "${row_status}" != "ok" || "${row_exit}" != "0" ]]; then
            local hint
            hint="$(head -n 1 "${err_file}" | tr ',' ';' | tr '\n' ' ' | sed 's/[[:space:]]\+/ /g')"
            append_failure_meta "${allocator}" "${mode}" "${workload}" "${io_threads}" "${run_id}" "${row_status}" "${row_exit}" "${hint}"
        fi
    else
        local status="exit_nonzero"
        if [[ ${rc} -eq 124 ]]; then
            status="timeout"
        elif [[ ${rc} -eq 137 ]]; then
            status="timeout_killed"
        elif [[ ${rc} -eq 139 ]]; then
            status="crash"
        fi
        local hint
        hint="$(head -n 1 "${err_file}" | tr ',' ';' | tr '\n' ' ' | sed 's/[[:space:]]\+/ /g')"
        append_failure_row "${allocator}" "${mode}" "${workload}" "${io_threads}" "${run_id}" "${status}" "${rc}" "${hint}"
    fi

    rm -f "${out_file}" "${err_file}"
}

run_allocator_matrix() {
    local allocator="$1"
    local preload_lib="$2"
    IFS=',' read -r -a mode_arr <<<"${MODES}"
    IFS=',' read -r -a workload_arr <<<"${WORKLOADS}"
    IFS=',' read -r -a io_arr <<<"${IO_THREADS}"

    for mode in "${mode_arr[@]}"; do
        for workload in "${workload_arr[@]}"; do
            for io_threads in "${io_arr[@]}"; do
                for ((run_id = 1; run_id <= REPEAT; ++run_id)); do
                    run_single_case "${allocator}" "${preload_lib}" "${mode}" "${workload}" "${io_threads}" "${run_id}"
                done
            done
        done
    done
}

run_allocator_matrix "baseline" ""
run_allocator_matrix "jemalloc" "${JEMALLOC_LIB}"
run_allocator_matrix "tcmalloc" "${TCMALLOC_LIB}"

tmp_avg="$(mktemp)"
awk -F, '
NR == 1 { next }
$14 == "ok" {
    key = $1 FS $2 FS $3 FS $4
    cnt[key] += 1
    req[key] += $6
    ms[key]  += $7
    qps[key] += $8
    p50[key] += $9
    p95[key] += $10
    p99[key] += $11
    mx[key]  += $12
    err[key] += $13
}
END {
    print "allocator,mode,workload,io_threads,runs_ok,avg_total_requests,avg_total_ms,avg_req_per_sec,avg_p50_us,avg_p95_us,avg_p99_us,avg_max_us,avg_errors"
    for (k in cnt) {
        split(k, a, FS)
        c = cnt[k]
        printf "%s,%s,%s,%s,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", \
            a[1], a[2], a[3], a[4], c, \
            req[k] / c, ms[k] / c, qps[k] / c, \
            p50[k] / c, p95[k] / c, p99[k] / c, mx[k] / c, err[k] / c
    }
}' "${RAW_OUT}" >"${tmp_avg}"

{
    head -n 1 "${tmp_avg}"
    tail -n +2 "${tmp_avg}" | sort -t, -k1,1 -k2,2 -k3,3 -k4,4n
} >"${AVG_OUT}"
rm -f "${tmp_avg}"

echo "[INFO] Done."
echo "[INFO] raw output: ${RAW_OUT}"
echo "[INFO] avg output: ${AVG_OUT}"
echo "[INFO] failures: ${FAIL_OUT}"
