#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
BIN_PATH="${BUILD_DIR}/bin/test_benchmark_tcp_allocator"

QUICK_OUT="${ROOT_DIR}/result_allocator_matrix_quick.csv"
RAW_OUT="${ROOT_DIR}/result_allocator_matrix_raw.csv"
AVG_OUT="${ROOT_DIR}/result_allocator_matrix_avg.csv"

THREADS="${THREADS:-2,4,8}"
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
            if [[ ! -d "${d}" ]]; then
                continue
            fi
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

run_matrix() {
    local label="$1"
    local preload_lib="$2"
    local repeat="$3"
    local out_file="$4"
    local tmp_file
    tmp_file="$(mktemp)"

    echo "[INFO] Running allocator=${label} repeat=${repeat}"
    if [[ -n "${preload_lib}" ]]; then
        LD_PRELOAD="${preload_lib}" "${BIN_PATH}" \
            --allocator_label="${label}" \
            --mode=all \
            --workload=all \
            --threads="${THREADS}" \
            --repeat="${repeat}" \
            --payload_bytes="${PAYLOAD_BYTES}" \
            --connections="${CONNECTIONS}" \
            --requests_per_conn="${REQUESTS_PER_CONN}" \
            --short_total_requests="${SHORT_TOTAL_REQUESTS}" \
            --startup_delay_ms="${STARTUP_DELAY_MS}" > "${tmp_file}"
    else
        "${BIN_PATH}" \
            --allocator_label="${label}" \
            --mode=all \
            --workload=all \
            --threads="${THREADS}" \
            --repeat="${repeat}" \
            --payload_bytes="${PAYLOAD_BYTES}" \
            --connections="${CONNECTIONS}" \
            --requests_per_conn="${REQUESTS_PER_CONN}" \
            --short_total_requests="${SHORT_TOTAL_REQUESTS}" \
            --startup_delay_ms="${STARTUP_DELAY_MS}" > "${tmp_file}"
    fi

    if [[ ! -f "${out_file}" || ! -s "${out_file}" ]]; then
        cat "${tmp_file}" > "${out_file}"
    else
        tail -n +2 "${tmp_file}" >> "${out_file}"
    fi
    rm -f "${tmp_file}"
}

rm -f "${QUICK_OUT}" "${RAW_OUT}" "${AVG_OUT}"

echo "[INFO] Phase1 quick run (repeat=1)"
run_matrix "baseline" "" 1 "${QUICK_OUT}"
run_matrix "jemalloc" "${JEMALLOC_LIB}" 1 "${QUICK_OUT}"
run_matrix "tcmalloc" "${TCMALLOC_LIB}" 1 "${QUICK_OUT}"

echo "[INFO] Phase2 full run (repeat=3)"
run_matrix "baseline" "" 3 "${RAW_OUT}"
run_matrix "jemalloc" "${JEMALLOC_LIB}" 3 "${RAW_OUT}"
run_matrix "tcmalloc" "${TCMALLOC_LIB}" 3 "${RAW_OUT}"

tmp_avg="$(mktemp)"
awk -F, '
NR == 1 { next }
{
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
    print "allocator,mode,workload,threads_total,runs,avg_total_requests,avg_total_ms,avg_req_per_sec,avg_p50_us,avg_p95_us,avg_p99_us,avg_max_us,avg_errors"
    for (k in cnt) {
        split(k, a, FS)
        c = cnt[k]
        printf "%s,%s,%s,%s,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", \
            a[1], a[2], a[3], a[4], c, \
            req[k] / c, ms[k] / c, qps[k] / c, \
            p50[k] / c, p95[k] / c, p99[k] / c, mx[k] / c, err[k] / c
    }
}' "${RAW_OUT}" > "${tmp_avg}"

{
    head -n 1 "${tmp_avg}"
    tail -n +2 "${tmp_avg}" | sort -t, -k1,1 -k2,2 -k3,3 -k4,4n
} > "${AVG_OUT}"
rm -f "${tmp_avg}"

echo "[INFO] Done."
echo "[INFO] quick output: ${QUICK_OUT}"
echo "[INFO] full raw output: ${RAW_OUT}"
echo "[INFO] full avg output: ${AVG_OUT}"

