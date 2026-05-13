#!/bin/sh

set -eu

MODE="${1:-worker}"
PORT="${2:-1350}"
NUM_POINTS="${3:-100000}"
CORES="${4:-1}"
NUM_CLIENTS=3
CONNECT_TIMEOUT_MS="${CONNECT_TIMEOUT_MS:-120000}"
COMPUTE_TIMEOUT_MS="${COMPUTE_TIMEOUT_MS:-120000}"
SERVER_TIMEOUT_MS="${SERVER_TIMEOUT_MS:-120000}"

WORKER_LOG="build/library_failure_${MODE}_workers.log"
SERVER_LOG="build/library_failure_${MODE}_server.log"

cleanup()
{
    if [ "${WORKERS_PID:-}" ]; then
        kill "$WORKERS_PID" 2>/dev/null || true
    fi
}

trap cleanup INT TERM EXIT

echo "library-failure-test: mode=$MODE port=$PORT"

set +e
if [ "$MODE" = "worker" ]; then
    INTEGRAL_FAIL_WORKER_ID=1 ./build/library_workers 127.0.0.1 "$PORT" "$CONNECT_TIMEOUT_MS" "$COMPUTE_TIMEOUT_MS" "$CORES" "$CORES" "$CORES" > "$WORKER_LOG" 2>&1 &
else
    ./build/library_workers 127.0.0.1 "$PORT" "$CONNECT_TIMEOUT_MS" "$COMPUTE_TIMEOUT_MS" "$CORES" "$CORES" "$CORES" > "$WORKER_LOG" 2>&1 &
fi
WORKERS_PID="$!"
set -e

sleep 1

set +e
if [ "$MODE" = "server" ]; then
    INTEGRAL_FAIL_SERVER_AFTER_TASKS=1 ./build/library_server 0 0 3.141592653589793 "$NUM_POINTS" "$NUM_CLIENTS" "$PORT" "$SERVER_TIMEOUT_MS" > "$SERVER_LOG" 2>&1
else
    ./build/library_server 0 0 3.141592653589793 "$NUM_POINTS" "$NUM_CLIENTS" "$PORT" "$SERVER_TIMEOUT_MS" > "$SERVER_LOG" 2>&1
fi
SERVER_STATUS="$?"

wait "$WORKERS_PID"
WORKERS_STATUS="$?"
set -e

echo "library-failure-test: server log"
grep -E "result task|invalid result|injected failure|Integral|elapsed|server:" "$SERVER_LOG" || true

echo "library-failure-test: worker log"
grep -E "connected|task|injected failure|unable|failed" "$WORKER_LOG" || true

if [ "$SERVER_STATUS" -eq 0 ]; then
    echo "library-failure-test: expected server-side failure, got success" >&2
    exit 1
fi

if [ "$WORKERS_STATUS" -eq 0 ]; then
    echo "library-failure-test: expected worker launcher to report at least one worker failure" >&2
    exit 1
fi

if [ "$MODE" = "worker" ]; then
    grep -q "client: injected failure after receiving task, worker_id=1" "$WORKER_LOG"
    grep -q "client.*task 0" "$WORKER_LOG"
    grep -q "client.*task 2" "$WORKER_LOG"
    grep -q "server: result task 0" "$SERVER_LOG"
    grep -q "server: result task 2" "$SERVER_LOG"
    grep -q "server: invalid result from worker 1" "$SERVER_LOG"
else
    grep -q "server: injected failure after sending tasks" "$SERVER_LOG"
    grep -q "client: unable to read completion ack from server" "$WORKER_LOG"
fi

trap - INT TERM EXIT
cleanup

echo "library-failure-test: detected expected $MODE failure"
