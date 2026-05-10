#!/usr/bin/env bash
# test_mqtt.sh — Start an ephemeral Mosquitto broker, run mqtt_tests.scm, clean up.
# Usage: test_mqtt.sh <curry-binary> <mod-path>
#
# Starts a plain MQTT broker on port 11883 and, when openssl is available,
# a TLS broker on port 18883 with a fresh self-signed cert.  Both are killed
# on exit.  If mosquitto is not installed the script prints a SKIP line and
# exits 0 so ctest stays green.

set -uo pipefail

CURRY="${1:?Usage: test_mqtt.sh <curry-binary> <mod-path>}"
MOD_PATH="${2:?}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if ! command -v mosquitto &>/dev/null; then
    echo "SKIP: mosquitto not found — install it to run MQTT tests"
    exit 0
fi

WORK="$(mktemp -d)"
PLAIN_PORT=11883
TLS_PORT=18883

cleanup() {
    local pid
    for pidfile in "$WORK"/plain.pid "$WORK"/tls.pid; do
        [ -f "$pidfile" ] && kill "$(cat "$pidfile")" 2>/dev/null || true
    done
    rm -rf "$WORK"
}
trap cleanup EXIT

# ---- Plain broker ----
cat > "$WORK/plain.conf" <<EOF
listener $PLAIN_PORT 127.0.0.1
allow_anonymous true
pid_file $WORK/plain.pid
log_type none
EOF
mosquitto -c "$WORK/plain.conf" -d
sleep 0.4

# ---- TLS broker (best-effort) ----
TLS_CA=""
if command -v openssl &>/dev/null; then
    openssl req -x509 -newkey rsa:2048 \
        -keyout "$WORK/broker.key" \
        -out    "$WORK/broker.crt" \
        -days 1 -nodes \
        -subj   "/CN=127.0.0.1" \
        -addext "subjectAltName=IP:127.0.0.1" \
        2>/dev/null \
    && cat > "$WORK/tls.conf" <<EOF
listener $TLS_PORT 127.0.0.1
allow_anonymous true
cafile $WORK/broker.crt
certfile $WORK/broker.crt
keyfile $WORK/broker.key
require_certificate false
pid_file $WORK/tls.pid
log_type none
EOF
    mosquitto -c "$WORK/tls.conf" -d \
    && TLS_CA="$WORK/broker.crt" \
    && sleep 0.4 \
    || true
fi

# ---- Run tests ----
CURRY_MODULE_PATH="$MOD_PATH" \
    "$CURRY" "$SCRIPT_DIR/mqtt_tests.scm" \
    "$PLAIN_PORT" "$TLS_PORT" "$TLS_CA"
