#!/bin/sh

set -eu

FUNC_ID="${1:-0}"
X_BEGIN="${2:-0}"
X_END="${3:-3.141592653589793}"
NUM_POINTS="${4:-30000000}"
PORT="${5:-1338}"
CONNECT_TIMEOUT_MS="${CONNECT_TIMEOUT_MS:-120000}"
COMPUTE_TIMEOUT_MS="${COMPUTE_TIMEOUT_MS:-120000}"
shift 5 2>/dev/null || true

if [ "$#" -eq 0 ]; then
    set -- 1 1 1
fi

NUM_CLIENTS="$#"
CLIENT_PIDS=""
WORKER_ID=0

cleanup()
{
    for pid in $CLIENT_PIDS; do
        kill "$pid" 2>/dev/null || true
    done
}

trap cleanup INT TERM EXIT

echo "script: starting $NUM_CLIENTS workers on port $PORT"
for cores in "$@"; do
    WORKER_LOG="build/worker_${WORKER_ID}.log"
    ./build/client 127.0.0.1 "$PORT" "$cores" "$WORKER_ID" "$CONNECT_TIMEOUT_MS" "$COMPUTE_TIMEOUT_MS" > "$WORKER_LOG" 2>&1 &
    CLIENT_PIDS="$CLIENT_PIDS $!"
    WORKER_ID=$((WORKER_ID + 1))
done

sleep 1

echo "script: starting integral2"
./build/integral2 "$FUNC_ID" "$X_BEGIN" "$X_END" "$NUM_POINTS" "$NUM_CLIENTS" "$PORT"

echo "script: worker summary"
for log in build/worker_*.log; do
    grep -E "connected|task" "$log" || true
done

trap - INT TERM EXIT
cleanup
