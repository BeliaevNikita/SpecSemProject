#!/bin/sh

set -eu

MODE="${1:-worker}"
PORT="${2:-1340}"
NUM_POINTS="${3:-1000000}"
CORES="${4:-1}"
NUM_CLIENTS=3
CONNECT_TIMEOUT_MS="${CONNECT_TIMEOUT_MS:-120000}"
COMPUTE_TIMEOUT_MS="${COMPUTE_TIMEOUT_MS:-120000}"
CLIENT_PIDS=""
WORKER_ID=0

cleanup()
{
    for pid in $CLIENT_PIDS; do
        kill "$pid" 2>/dev/null || true
    done
}

trap cleanup INT TERM EXIT

echo "failure-test: mode=$MODE port=$PORT"

while [ "$WORKER_ID" -lt "$NUM_CLIENTS" ]; do
    WORKER_LOG="build/failure_${MODE}_worker_${WORKER_ID}.log"
    if [ "$MODE" = "worker" ] && [ "$WORKER_ID" -eq 1 ]; then
        INTEGRAL_FAIL_WORKER_ID="$WORKER_ID" ./build/client 127.0.0.1 "$PORT" "$CORES" "$WORKER_ID" "$CONNECT_TIMEOUT_MS" "$COMPUTE_TIMEOUT_MS" > "$WORKER_LOG" 2>&1 &
    else
        ./build/client 127.0.0.1 "$PORT" "$CORES" "$WORKER_ID" "$CONNECT_TIMEOUT_MS" "$COMPUTE_TIMEOUT_MS" > "$WORKER_LOG" 2>&1 &
    fi
    CLIENT_PIDS="$CLIENT_PIDS $!"
    WORKER_ID=$((WORKER_ID + 1))
done

sleep 1

set +e
if [ "$MODE" = "server" ]; then
    INTEGRAL_FAIL_SERVER_AFTER_TASKS=1 ./build/integral2 0 0 3.141592653589793 "$NUM_POINTS" "$NUM_CLIENTS" "$PORT"
else
    ./build/integral2 0 0 3.141592653589793 "$NUM_POINTS" "$NUM_CLIENTS" "$PORT"
fi
STATUS="$?"
set -e

echo "failure-test: worker summary"
for log in build/failure_"$MODE"_worker_*.log; do
    grep -E "connected|task|failure|unable|invalid|Broken pipe" "$log" || true
done

if [ "$STATUS" -eq 0 ]; then
    echo "failure-test: expected integral2/server failure, got success" >&2
    exit 1
fi

echo "failure-test: detected expected $MODE failure"

trap - INT TERM EXIT
cleanup
