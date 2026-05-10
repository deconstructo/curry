#!/usr/bin/env bash
# test_mqtt.sh — Start ephemeral Mosquitto brokers, run mqtt_tests.scm, clean up.
# Usage: test_mqtt.sh <curry-binary> <mod-path>
#
# Starts:
#   • A plain MQTT broker on port 11883 for the main test suite.
#   • A TLS broker on port 18883 when openssl is available.
#   • A second plain broker on port 11884 for the disconnect-event test;
#     this broker is killed mid-test after the Scheme process signals readiness.
#
# If mosquitto is not installed the script prints SKIP and exits 0.

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
DISCO_PORT=11884

cleanup() {
    for pidfile in "$WORK"/plain.pid "$WORK"/tls.pid "$WORK"/disco.pid; do
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

# ---- Disconnect-event broker (killed mid-test) ----
cat > "$WORK/disco.conf" <<EOF
listener $DISCO_PORT 127.0.0.1
allow_anonymous true
pid_file $WORK/disco.pid
log_type none
EOF
mosquitto -c "$WORK/disco.conf" -d
sleep 0.4
DISCO_ACTUAL=$DISCO_PORT

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

# ---- Run the test suite ----
# The disconnect-event test writes "READY\n" to stdout when it has connected to
# the disco broker.  We read that signal, kill the broker, and let the test
# observe the disconnect event.  Everything else goes through as normal output.

CURRY_MODULE_PATH="$MOD_PATH" \
    "$CURRY" "$SCRIPT_DIR/mqtt_tests.scm" \
    "$PLAIN_PORT" "$TLS_PORT" "$TLS_CA" "$DISCO_ACTUAL" \
| while IFS= read -r line; do
    if [ "$line" = "READY" ]; then
        # Kill the disconnect-event broker so the Scheme side sees conn_lost
        [ -f "$WORK/disco.pid" ] && kill "$(cat "$WORK/disco.pid")" 2>/dev/null || true
        rm -f "$WORK/disco.pid"
    else
        echo "$line"
    fi
done

# Propagate the Scheme exit code (the pipe hides it; use PIPESTATUS)
exit "${PIPESTATUS[0]}"
