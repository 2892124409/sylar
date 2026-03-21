#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

ITERATIONS="${ITERATIONS:-10000000}"
REPEAT="${REPEAT:-5}"
WARMUP="${WARMUP:-1}"
STACK_SIZE="${STACK_SIZE:-131072}"
CPU_CORE="${CPU_CORE:-0}"
BUILD_TYPE="${BUILD_TYPE:-Release}"

run_backend() {
    local backend="$1"
    local build_dir="${ROOT_DIR}/build_ctx_${backend}"
    local bin_path="${build_dir}/bin/test_context_switch_pingpong"
    local log_path="${ROOT_DIR}/result_context_switch_${backend}.txt"

    echo "[INFO] configure backend=${backend}" >&2
    cmake -S "${ROOT_DIR}" -B "${build_dir}" \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -DSYLAR_FIBER_CONTEXT_BACKEND="${backend}" >&2

    echo "[INFO] build benchmark target backend=${backend}" >&2
    cmake --build "${build_dir}" -j"$(nproc)" --target test_context_switch_pingpong >&2

    local -a cmd=(
        "${bin_path}"
        "--iterations=${ITERATIONS}"
        "--repeat=${REPEAT}"
        "--warmup=${WARMUP}"
        "--stack-size=${STACK_SIZE}"
    )

    if command -v taskset >/dev/null 2>&1; then
        cmd=(taskset -c "${CPU_CORE}" "${cmd[@]}")
    else
        echo "[WARN] taskset not found, run without CPU pinning" >&2
    fi

    echo "[INFO] run backend=${backend}" >&2
    local output
    output="$("${cmd[@]}")"
    printf '%s\n' "${output}" | tee "${log_path}" >&2

    local result_line
    result_line="$(printf '%s\n' "${output}" | awk '/^RESULT / { print; exit }')"
    if [[ -z "${result_line}" ]]; then
        echo "[ERROR] RESULT line not found for backend=${backend}" >&2
        return 1
    fi

    local avg_total_ms avg_baseline_ms avg_ns avg_ns_net
    avg_total_ms="$(printf '%s\n' "${result_line}" | awk '{for(i=1;i<=NF;i++){if($i ~ /^avg_total_ms=/){split($i,a,"=");print a[2]}}}')"
    avg_baseline_ms="$(printf '%s\n' "${result_line}" | awk '{for(i=1;i<=NF;i++){if($i ~ /^avg_baseline_ms=/){split($i,a,"=");print a[2]}}}')"
    avg_ns="$(printf '%s\n' "${result_line}" | awk '{for(i=1;i<=NF;i++){if($i ~ /^avg_ns_per_switch=/){split($i,a,"=");print a[2]}}}')"
    avg_ns_net="$(printf '%s\n' "${result_line}" | awk '{for(i=1;i<=NF;i++){if($i ~ /^avg_ns_per_switch_net=/){split($i,a,"=");print a[2]}}}')"

    if [[ -z "${avg_total_ms}" || -z "${avg_baseline_ms}" || -z "${avg_ns}" || -z "${avg_ns_net}" ]]; then
        echo "[ERROR] failed to parse RESULT metrics for backend=${backend}" >&2
        return 1
    fi

    echo "${avg_total_ms},${avg_baseline_ms},${avg_ns},${avg_ns_net}"
}

IFS=',' read -r u_total u_base u_ns u_ns_net <<< "$(run_backend ucontext)"
IFS=',' read -r a_total a_base a_ns a_ns_net <<< "$(run_backend libco_asm)"

speedup_raw="$(awk -v u="${u_ns}" -v a="${a_ns}" 'BEGIN{ if (a == 0) print "inf"; else printf "%.3f", u / a }')"
speedup_net="$(awk -v u="${u_ns_net}" -v a="${a_ns_net}" 'BEGIN{ if (a == 0) print "inf"; else printf "%.3f", u / a }')"

echo
echo "[ucontext]"
echo "total time: ${u_total} ms"
echo "ns per switch: ${u_ns}"
echo "baseline loop: ${u_base} ms"
echo "ns per switch(net): ${u_ns_net}"
echo
echo "[asm]"
echo "total time: ${a_total} ms"
echo "ns per switch: ${a_ns}"
echo "baseline loop: ${a_base} ms"
echo "ns per switch(net): ${a_ns_net}"
echo
echo "speedup(raw): ${speedup_raw}x"
echo "speedup(net): ${speedup_net}x"
echo "speedup: ${speedup_net}x"
