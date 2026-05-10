#!/usr/bin/env bash
# test_redis.sh — Start ephemeral Redis instances, run redis_tests.scm, clean up.
# Usage: test_redis.sh <curry-binary> <mod-path>
#
# Starts a plain TCP Redis on port 16379 and, if openssl is available, a
# TLS-only Redis on port 16380 using a fresh self-signed cert.  Both servers
# are killed and the temp directory removed on exit.  If redis-server is not
# installed the script prints a SKIP line and exits 0 so ctest stays green.

set -uo pipefail

CURRY="${1:?Usage: test_redis.sh <curry-binary> <mod-path>}"
MOD_PATH="${2:?}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if ! command -v redis-server &>/dev/null; then
    echo "SKIP: redis-server not found — install Redis to run these tests"
    exit 0
fi

WORK="$(mktemp -d)"
PLAIN_PORT=16379
TLS_PORT=16380

cleanup() {
    local pid
    for pidfile in "$WORK"/plain.pid "$WORK"/tls.pid; do
        if [ -f "$pidfile" ]; then
            pid="$(cat "$pidfile")"
            kill "$pid" 2>/dev/null || true
        fi
    done
    rm -rf "$WORK"
}
trap cleanup EXIT

# ---- Plain TCP Redis ----
redis-server \
    --port         "$PLAIN_PORT" \
    --loglevel     warning \
    --daemonize    yes \
    --logfile      "$WORK/plain.log" \
    --pidfile      "$WORK/plain.pid"
sleep 0.3

# ---- TLS Redis (best-effort; tests skip gracefully if unavailable) ----
TLS_CA=""
if command -v openssl &>/dev/null; then
    openssl req -x509 -newkey rsa:2048 \
        -keyout "$WORK/server.key" \
        -out    "$WORK/server.crt" \
        -days 1 -nodes \
        -subj   "/CN=127.0.0.1" \
        -addext "subjectAltName=IP:127.0.0.1" \
        2>/dev/null \
    && redis-server \
           --port           0 \
           --tls-port       "$TLS_PORT" \
           --tls-cert-file  "$WORK/server.crt" \
           --tls-key-file   "$WORK/server.key" \
           --tls-auth-clients no \
           --loglevel       warning \
           --daemonize      yes \
           --logfile        "$WORK/tls.log" \
           --pidfile        "$WORK/tls.pid" \
    && TLS_CA="$WORK/server.crt" \
    && sleep 0.3 \
    || true   # TLS setup failure is non-fatal
fi

# ---- Run tests ----
CURRY_MODULE_PATH="$MOD_PATH" \
    "$CURRY" "$SCRIPT_DIR/redis_tests.scm" \
    "$PLAIN_PORT" "$TLS_PORT" "$TLS_CA"
