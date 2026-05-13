#!/bin/sh

set -eu

MODE="${1:-client}"
PORT="${2:-1355}"
FAIL_TIMEOUT_MS="${FAIL_TIMEOUT_MS:-300}"

CLIENT_LOG="build/timeout_client.log"
SERVER_LOG="build/timeout_server.log"

echo "timeout-test: mode=$MODE port=$PORT"

if [ "$MODE" = "client" ]; then
    set +e
    ./build/client 127.0.0.1 "$PORT" 1 0 "$FAIL_TIMEOUT_MS" "$FAIL_TIMEOUT_MS" > "$CLIENT_LOG" 2>&1
    STATUS="$?"
    set -e

    grep -q "client: connection timeout" "$CLIENT_LOG"
    if [ "$STATUS" -eq 0 ]; then
        echo "timeout-test: expected client connection timeout failure" >&2
        exit 1
    fi

    grep -E "wait for server|connection timeout" "$CLIENT_LOG" || true
    echo "timeout-test: client connection timeout detected"
elif [ "$MODE" = "server" ]; then
    set +e
    ./build/library_server 0 0 3.141592653589793 10000 3 "$PORT" "$FAIL_TIMEOUT_MS" > "$SERVER_LOG" 2>&1
    STATUS="$?"
    set -e

    grep -q "server: timeout while waiting for workers" "$SERVER_LOG"
    if [ "$STATUS" -eq 0 ]; then
        echo "timeout-test: expected server worker wait timeout failure" >&2
        exit 1
    fi

    grep -E "listening|timeout while waiting" "$SERVER_LOG" || true
    echo "timeout-test: server worker wait timeout detected"
else
    echo "Usage: $0 client|server [port]" >&2
    exit 1
fi
