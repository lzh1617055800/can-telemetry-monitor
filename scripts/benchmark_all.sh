#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/benchmark-release}"
RESULT_DIR="${RESULT_DIR:-${ROOT_DIR}/benchmarks/$(date +%Y%m%d-%H%M%S)}"
CAN_INTERFACE="${CAN_INTERFACE:-vcan0}"
HTTP_URL="${HTTP_URL:-http://127.0.0.1:8080}"
REST_PATH="${REST_PATH:-/api/can/frames?after=0&limit=100}"
REST_FLOOD_FRAMES="${REST_FLOOD_FRAMES:-60000}"
REST_FLOOD_RATE="${REST_FLOOD_RATE:-2000}"
SSE_FRAMES="${SSE_FRAMES:-1000}"
SSE_RATE="${SSE_RATE:-1000}"
SSE_CLIENTS="${SSE_CLIENTS:-10}"

mkdir -p "${RESULT_DIR}"

fail_missing=0
for command_name in cmake ctest curl python3 valgrind; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo "missing command: ${command_name}" >&2
        fail_missing=1
    fi
done

if ! command -v wrk >/dev/null 2>&1 && ! command -v ab >/dev/null 2>&1; then
    echo "missing command: wrk or ab" >&2
    fail_missing=1
fi

if [[ "${fail_missing}" -ne 0 ]]; then
    echo "Install benchmark dependencies first:" >&2
    echo "sudo apt update && sudo apt install -y valgrind wrk python3 curl can-utils" >&2
    exit 2
fi

echo "[1/7] Configure Release build"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON \
    -DBUILD_QML_CLIENT=OFF \
    >"${RESULT_DIR}/configure.log" 2>&1

echo "[2/7] Build and run tests"
cmake --build "${BUILD_DIR}" -j"$(nproc)" \
    >"${RESULT_DIR}/build.log" 2>&1
ctest --test-dir "${BUILD_DIR}" --output-on-failure \
    >"${RESULT_DIR}/ctest.log" 2>&1

echo "[3/7] Prepare ${CAN_INTERFACE}"
"${ROOT_DIR}/scripts/vcan_up.sh" >"${RESULT_DIR}/vcan.log" 2>&1

echo "[4/7] Valgrind CAN loopback"
set +e
valgrind --leak-check=full \
    --show-leak-kinds=all \
    --error-exitcode=99 \
    "${BUILD_DIR}/can_loopback_demo" \
    >"${RESULT_DIR}/valgrind.log" 2>&1
VALGRIND_STATUS=$?
set -e

SERVER_PID=""
CAN_FLOOD_PID=""
HTTP_VALGRIND_PID=""

stop_server()
{
    if [[ -n "${SERVER_PID}" ]]; then
        kill -INT "${SERVER_PID}" 2>/dev/null || true
        set +e
        wait "${SERVER_PID}" 2>/dev/null
        local status=$?
        set -e
        SERVER_PID=""
        return "${status}"
    fi
    return 0
}

stop_flood()
{
    if [[ -n "${CAN_FLOOD_PID}" ]] &&
       kill -0 "${CAN_FLOOD_PID}" 2>/dev/null; then
        kill -TERM "${CAN_FLOOD_PID}" 2>/dev/null || true
        wait "${CAN_FLOOD_PID}" 2>/dev/null || true
    fi
    CAN_FLOOD_PID=""
}

cleanup_all()
{
    stop_flood || true
    stop_server || true
    if [[ -n "${HTTP_VALGRIND_PID}" ]] &&
       kill -0 "${HTTP_VALGRIND_PID}" 2>/dev/null; then
        kill -INT "${HTTP_VALGRIND_PID}" 2>/dev/null || true
        wait "${HTTP_VALGRIND_PID}" 2>/dev/null || true
    fi
}
trap cleanup_all EXIT

start_server()
{
    local log_file="$1"
    CAN_INTERFACE="${CAN_INTERFACE}" \
        "${BUILD_DIR}/http_server" \
        >"${log_file}" 2>&1 &
    SERVER_PID=$!

    local ready=0
    for attempt in $(seq 1 50); do
        if curl -fsS "${HTTP_URL}/api/can/stats" \
            >"${RESULT_DIR}/readiness.json" 2>/dev/null; then
            ready=1
            break
        fi
        sleep 0.1
    done

    if [[ "${ready}" -ne 1 ]]; then
        echo "HTTP server did not become ready" >&2
        cat "${log_file}" >&2 || true
        return 1
    fi
}

echo "[5/7] REST mixed read/write pressure"
start_server "${RESULT_DIR}/http_server_rest.log"

set +e
python3 "${ROOT_DIR}/scripts/can_flood.py" \
    --interface "${CAN_INTERFACE}" \
    --id 0x5A0 \
    --frames "${REST_FLOOD_FRAMES}" \
    --rate "${REST_FLOOD_RATE}" \
    >"${RESULT_DIR}/rest_can_flood.json" 2>&1 &
CAN_FLOOD_PID=$!
sleep 0.2

if command -v wrk >/dev/null 2>&1; then
    wrk -t4 -c100 -d30s --latency \
        "${HTTP_URL}${REST_PATH}" \
        | tee "${RESULT_DIR}/rest_wrk.log"
    REST_STATUS=${PIPESTATUS[0]}
else
    ab -n 10000 -c 100 \
        "${HTTP_URL}${REST_PATH}" \
        | tee "${RESULT_DIR}/rest_ab.log"
    REST_STATUS=${PIPESTATUS[0]}
fi

wait "${CAN_FLOOD_PID}" 2>/dev/null
REST_FLOOD_STATUS=$?
CAN_FLOOD_PID=""

REST_SAMPLE_STATUS=0
curl -fsS "${HTTP_URL}${REST_PATH}" \
    >"${RESULT_DIR}/rest_frames_sample.json" 2>/dev/null || \
    REST_SAMPLE_STATUS=$?
if [[ "${REST_SAMPLE_STATUS}" -eq 0 ]]; then
    python3 - "${RESULT_DIR}/rest_frames_sample.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as sample_file:
    payload = json.load(sample_file)

frames = payload.get("frames")
if not isinstance(frames, list) or not frames:
    raise SystemExit("REST sample is empty; mixed read/write test is invalid")
PY
    REST_SAMPLE_STATUS=$?
fi
set -e

set +e
stop_server
REST_SERVER_STATUS=$?
set -e

echo "[6/7] Multi-client SSE pressure"
start_server "${RESULT_DIR}/http_server_sse.log"

set +e
python3 "${ROOT_DIR}/scripts/sse_stress.py" \
    --interface "${CAN_INTERFACE}" \
    --frames "${SSE_FRAMES}" \
    --rate "${SSE_RATE}" \
    --clients "${SSE_CLIENTS}" \
    | tee "${RESULT_DIR}/sse_stress.json"
SSE_STATUS=${PIPESTATUS[0]}
set -e

set +e
stop_server
SSE_SERVER_STATUS=$?
set -e

echo "[7/7] Full HTTP Valgrind smoke test, including SSE"
set +e
CAN_INTERFACE="${CAN_INTERFACE}" \
    valgrind --leak-check=full \
    --show-leak-kinds=all \
    --error-exitcode=99 \
    "${BUILD_DIR}/http_server" \
    >"${RESULT_DIR}/http_valgrind.log" 2>&1 &
HTTP_VALGRIND_PID=$!
set -e

HTTP_VALGRIND_READY=0
for attempt in $(seq 1 50); do
    if curl -fsS "${HTTP_URL}/api/can/stats" \
        >"${RESULT_DIR}/http_valgrind_readiness.json" 2>/dev/null; then
        HTTP_VALGRIND_READY=1
        break
    fi
    sleep 0.1
done

HTTP_VALGRIND_SSE_STATUS=1
if [[ "${HTTP_VALGRIND_READY}" -eq 1 ]]; then
    set +e
    python3 "${ROOT_DIR}/scripts/sse_stress.py" \
        --interface "${CAN_INTERFACE}" \
        --frames 100 \
        --rate 25 \
        --clients 2 \
        --timeout 30 \
        >"${RESULT_DIR}/http_valgrind_sse.json" 2>&1
    HTTP_VALGRIND_SSE_STATUS=$?
    set -e

    curl -fsS "${HTTP_URL}/api/can/frames?after=0&limit=100" \
        >"${RESULT_DIR}/http_valgrind_frames.json"
fi

kill -INT "${HTTP_VALGRIND_PID}" 2>/dev/null || true
set +e
wait "${HTTP_VALGRIND_PID}" 2>/dev/null
HTTP_VALGRIND_STATUS=$?
set -e
HTTP_VALGRIND_PID=""

overall_status=0
for status in \
    "${VALGRIND_STATUS}" \
    "${REST_STATUS}" \
    "${REST_FLOOD_STATUS}" \
    "${REST_SAMPLE_STATUS}" \
    "${REST_SERVER_STATUS}" \
    "${SSE_STATUS}" \
    "${SSE_SERVER_STATUS}" \
    "${HTTP_VALGRIND_SSE_STATUS}" \
    "${HTTP_VALGRIND_STATUS}"; do
    if [[ "${status}" -ne 0 ]]; then
        overall_status=1
    fi
done

{
    echo "result_dir=${RESULT_DIR}"
    echo "valgrind_exit_code=${VALGRIND_STATUS}"
    echo "rest_exit_code=${REST_STATUS}"
    echo "rest_flood_exit_code=${REST_FLOOD_STATUS}"
    echo "rest_sample_exit_code=${REST_SAMPLE_STATUS}"
    echo "rest_server_exit_code=${REST_SERVER_STATUS}"
    echo "sse_exit_code=${SSE_STATUS}"
    echo "sse_server_exit_code=${SSE_SERVER_STATUS}"
    echo "http_valgrind_sse_exit_code=${HTTP_VALGRIND_SSE_STATUS}"
    echo "http_valgrind_exit_code=${HTTP_VALGRIND_STATUS}"
    echo "rest_url=${HTTP_URL}${REST_PATH}"
    echo "rest_flood_frames=${REST_FLOOD_FRAMES}"
    echo "rest_flood_rate=${REST_FLOOD_RATE}"
    echo "sse_frames_per_client=${SSE_FRAMES}"
    echo "sse_rate=${SSE_RATE}"
    echo "sse_clients=${SSE_CLIENTS}"
    echo ""
    echo "CAN Valgrind summary:"
    grep -E "ERROR SUMMARY|definitely lost|indirectly lost|total heap usage" \
        "${RESULT_DIR}/valgrind.log" || true
    echo ""
    echo "HTTP Valgrind summary:"
    grep -E "ERROR SUMMARY|definitely lost|indirectly lost|total heap usage" \
        "${RESULT_DIR}/http_valgrind.log" || true
    echo ""
    echo "REST report:"
    if [[ -f "${RESULT_DIR}/rest_wrk.log" ]]; then
        grep -E "Requests/sec|Latency|Non-2xx|Socket errors" \
            "${RESULT_DIR}/rest_wrk.log" || true
    else
        grep -E "Requests per second|Failed requests|Time per request" \
            "${RESULT_DIR}/rest_ab.log" || true
    fi
    echo ""
    echo "REST CAN producer:"
    cat "${RESULT_DIR}/rest_can_flood.json"
    echo ""
    echo "Multi-client SSE report:"
    cat "${RESULT_DIR}/sse_stress.json"
    echo ""
    echo "HTTP Valgrind SSE report:"
    cat "${RESULT_DIR}/http_valgrind_sse.json" 2>/dev/null || true
} | tee "${RESULT_DIR}/summary.txt"

echo ""
echo "All benchmark files are in: ${RESULT_DIR}"
exit "${overall_status}"
