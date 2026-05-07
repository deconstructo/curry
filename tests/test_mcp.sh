#!/usr/bin/env bash
# MCP module tests — covers both transports.
#
# Usage: test_mcp.sh <curry-binary> <mods-dir>
#   curry-binary  path to the curry executable
#   mods-dir      directory containing mcp.so (CURRY_MODULE_PATH)
#
# Exits 0 on all-pass, 1 on any failure.

set -euo pipefail

CURRY="${1:?usage: test_mcp.sh <curry> <mods-dir>}"
MOD_PATH="${2:?usage: test_mcp.sh <curry> <mods-dir>}"
export CURRY_MODULE_PATH="$MOD_PATH"

pass=0
fail=0

check() {
    local label="$1" actual="$2" expected="$3"
    if [ "$actual" = "$expected" ]; then
        echo "PASS: $label"
        (( pass++ )) || true
    else
        echo "FAIL: $label"
        echo "  expected: $expected"
        echo "  got:      $actual"
        (( fail++ )) || true
    fi
}

check_contains() {
    local label="$1" haystack="$2" needle="$3"
    if echo "$haystack" | grep -qF -- "$needle"; then
        echo "PASS: $label"
        (( pass++ )) || true
    else
        echo "FAIL: $label (string not found)"
        echo "  looking for: $needle"
        echo "  in: $haystack"
        (( fail++ )) || true
    fi
}

# ---- shared server script ----

SERVER_SCM=$(mktemp /tmp/mcp_test_XXXXXX.scm)
trap 'rm -f "$SERVER_SCM"' EXIT

cat > "$SERVER_SCM" << 'SCHEME'
(import (curry mcp))

(mcp-tool "add" "Add two integers"
  '((a . ((type . "integer") (description . "First operand")))
    (b . ((type . "integer") (description . "Second operand"))))
  (lambda (args)
    (let ((a (cdr (assq 'a args)))
          (b (cdr (assq 'b args))))
      (mcp-text (number->string (+ a b))))))

(mcp-tool "greet" "Return a greeting"
  '((name . ((type . "string")  (description . "Person to greet")))
    (lang . ((type . "string")  (description . "Language") (default . "en"))))
  (lambda (args)
    (let ((n (cdr (assq 'name args)))
          (lp (assq 'lang args)))
      (let ((l (if lp (cdr lp) "en")))
        (mcp-text (string-append "Hello, " n " [" l "]"))))))

(mcp-resource "info://version" "Server version" (lambda (uri) (mcp-text "0.1")))
SCHEME

# ---- helper: run a single JSON-RPC line through stdio and return response ----

stdio_rpc() {
    echo "$1" | CURRY_MODULE_PATH="$MOD_PATH" "$CURRY" - <<INNER 2>/dev/null
$(cat "$SERVER_SCM")
(mcp-serve)
INNER
}

# ================================================================
# 1. stdio transport
# ================================================================
echo ""
echo "=== stdio transport ==="

# initialize
INIT_OUT=$(printf '{"jsonrpc":"2.0","method":"initialize","id":1,"params":{"protocolVersion":"2024-11-05","capabilities":{}}}\n' \
    | CURRY_MODULE_PATH="$MOD_PATH" "$CURRY" <(cat "$SERVER_SCM"; echo '(mcp-serve)') 2>/dev/null)
check_contains "stdio: initialize has protocolVersion" "$INIT_OUT" '"protocolVersion"'
check_contains "stdio: initialize has serverInfo"     "$INIT_OUT" '"serverInfo"'

# tools/list
TOOLS_OUT=$(printf '{"jsonrpc":"2.0","method":"tools/list","id":2,"params":{}}\n' \
    | CURRY_MODULE_PATH="$MOD_PATH" "$CURRY" <(cat "$SERVER_SCM"; echo '(mcp-serve)') 2>/dev/null)
check_contains "stdio: tools/list contains add"    "$TOOLS_OUT" '"add"'
check_contains "stdio: tools/list contains greet"  "$TOOLS_OUT" '"greet"'
check_contains "stdio: tools/list required array"  "$TOOLS_OUT" '"required"'

# tools/call — add
ADD_OUT=$(printf '{"jsonrpc":"2.0","method":"tools/call","id":3,"params":{"name":"add","arguments":{"a":17,"b":25}}}\n' \
    | CURRY_MODULE_PATH="$MOD_PATH" "$CURRY" <(cat "$SERVER_SCM"; echo '(mcp-serve)') 2>/dev/null)
check_contains "stdio: add 17+25 = 42" "$ADD_OUT" '"42"'

# tools/call — greet with default lang
GREET_OUT=$(printf '{"jsonrpc":"2.0","method":"tools/call","id":4,"params":{"name":"greet","arguments":{"name":"World"}}}\n' \
    | CURRY_MODULE_PATH="$MOD_PATH" "$CURRY" <(cat "$SERVER_SCM"; echo '(mcp-serve)') 2>/dev/null)
check_contains "stdio: greet default lang" "$GREET_OUT" 'Hello, World [en]'

# tools/call — greet with explicit lang
GREET2_OUT=$(printf '{"jsonrpc":"2.0","method":"tools/call","id":5,"params":{"name":"greet","arguments":{"name":"Monde","lang":"fr"}}}\n' \
    | CURRY_MODULE_PATH="$MOD_PATH" "$CURRY" <(cat "$SERVER_SCM"; echo '(mcp-serve)') 2>/dev/null)
check_contains "stdio: greet explicit lang" "$GREET2_OUT" 'Hello, Monde [fr]'

# resources/list
RES_OUT=$(printf '{"jsonrpc":"2.0","method":"resources/list","id":6,"params":{}}\n' \
    | CURRY_MODULE_PATH="$MOD_PATH" "$CURRY" <(cat "$SERVER_SCM"; echo '(mcp-serve)') 2>/dev/null)
check_contains "stdio: resources/list has uri" "$RES_OUT" 'info://version'

# resources/read
RESREAD_OUT=$(printf '{"jsonrpc":"2.0","method":"resources/read","id":7,"params":{"uri":"info://version"}}\n' \
    | CURRY_MODULE_PATH="$MOD_PATH" "$CURRY" <(cat "$SERVER_SCM"; echo '(mcp-serve)') 2>/dev/null)
check_contains "stdio: resources/read value" "$RESREAD_OUT" '"0.1"'

# unknown method — should get error -32601
UNK_OUT=$(printf '{"jsonrpc":"2.0","method":"no/such/method","id":8,"params":{}}\n' \
    | CURRY_MODULE_PATH="$MOD_PATH" "$CURRY" <(cat "$SERVER_SCM"; echo '(mcp-serve)') 2>/dev/null)
check_contains "stdio: unknown method → -32601" "$UNK_OUT" '-32601'

# ================================================================
# 2. SSE transport
# ================================================================
echo ""
echo "=== SSE transport ==="

if ! command -v curl &>/dev/null; then
    echo "SKIP: curl not found — skipping SSE tests"
else
    SSE_PORT=19873

    # Start server in background
    SSE_SCM=$(mktemp /tmp/mcp_sse_XXXXXX.scm)
    trap 'rm -f "$SERVER_SCM" "$SSE_SCM"' EXIT
    cat "$SERVER_SCM" > "$SSE_SCM"
    echo "(mcp-serve-sse $SSE_PORT)" >> "$SSE_SCM"

    CURRY_MODULE_PATH="$MOD_PATH" "$CURRY" "$SSE_SCM" &
    SRV_PID=$!
    # Give it a moment to bind
    sleep 0.4

    cleanup_sse() { kill "$SRV_PID" 2>/dev/null || true; wait "$SRV_PID" 2>/dev/null || true; }
    trap 'rm -f "$SERVER_SCM" "$SSE_SCM"; cleanup_sse' EXIT

    # Open SSE stream, capture endpoint event, keep curl running in background
    SSE_TMP=$(mktemp /tmp/sse_stream_XXXXXX.txt)
    trap 'rm -f "$SERVER_SCM" "$SSE_SCM" "$SSE_TMP"; cleanup_sse' EXIT

    curl -s -N "http://localhost:$SSE_PORT/sse" > "$SSE_TMP" &
    CURL_PID=$!
    sleep 0.3

    ENDPOINT=$(grep "sessionId=" "$SSE_TMP" | head -1 | sed 's/data: //' | tr -d '\r\n')
    SESSID=$(echo "$ENDPOINT" | sed 's|.*sessionId=||')

    check_contains "SSE: endpoint event received" "$ENDPOINT" "sessionId="

    # initialize
    curl -s -X POST "http://localhost:$SSE_PORT/message?sessionId=$SESSID" \
        -H "Content-Type: application/json" \
        -d '{"jsonrpc":"2.0","method":"initialize","id":1,"params":{"protocolVersion":"2024-11-05","capabilities":{}}}' \
        > /dev/null
    sleep 0.2
    SSE_SO_FAR=$(cat "$SSE_TMP")
    check_contains "SSE: initialize response in stream"   "$SSE_SO_FAR" '"protocolVersion"'
    check_contains "SSE: initialize id=1 matched"         "$SSE_SO_FAR" '"id":1'

    # tools/list
    curl -s -X POST "http://localhost:$SSE_PORT/message?sessionId=$SESSID" \
        -H "Content-Type: application/json" \
        -d '{"jsonrpc":"2.0","method":"tools/list","id":2,"params":{}}' \
        > /dev/null
    sleep 0.2
    SSE_SO_FAR=$(cat "$SSE_TMP")
    check_contains "SSE: tools/list has add"   "$SSE_SO_FAR" '"add"'
    check_contains "SSE: tools/list has greet" "$SSE_SO_FAR" '"greet"'

    # tools/call — add
    curl -s -X POST "http://localhost:$SSE_PORT/message?sessionId=$SESSID" \
        -H "Content-Type: application/json" \
        -d '{"jsonrpc":"2.0","method":"tools/call","id":3,"params":{"name":"add","arguments":{"a":100,"b":23}}}' \
        > /dev/null
    sleep 0.2
    SSE_SO_FAR=$(cat "$SSE_TMP")
    check_contains "SSE: add 100+23 = 123" "$SSE_SO_FAR" '"123"'

    # tools/call — greet
    curl -s -X POST "http://localhost:$SSE_PORT/message?sessionId=$SESSID" \
        -H "Content-Type: application/json" \
        -d '{"jsonrpc":"2.0","method":"tools/call","id":4,"params":{"name":"greet","arguments":{"name":"SSE","lang":"de"}}}' \
        > /dev/null
    sleep 0.2
    SSE_SO_FAR=$(cat "$SSE_TMP")
    check_contains "SSE: greet result" "$SSE_SO_FAR" 'Hello, SSE [de]'

    # Two independent sessions get independent streams
    SSE_TMP2=$(mktemp /tmp/sse_stream2_XXXXXX.txt)
    trap 'rm -f "$SERVER_SCM" "$SSE_SCM" "$SSE_TMP" "$SSE_TMP2"; cleanup_sse' EXIT
    curl -s -N "http://localhost:$SSE_PORT/sse" > "$SSE_TMP2" &
    CURL_PID2=$!
    sleep 0.3
    SESSID2=$(grep "sessionId=" "$SSE_TMP2" | head -1 | sed 's|.*sessionId=||' | tr -d '\r\n')

    [ "$SESSID" != "$SESSID2" ]
    check "SSE: two sessions have distinct IDs" "$([ "$SESSID" != "$SESSID2" ] && echo yes || echo no)" "yes"

    # Send to session 1; only session 1 stream should get the response
    BEFORE=$(wc -c < "$SSE_TMP2")
    curl -s -X POST "http://localhost:$SSE_PORT/message?sessionId=$SESSID" \
        -H "Content-Type: application/json" \
        -d '{"jsonrpc":"2.0","method":"tools/call","id":5,"params":{"name":"add","arguments":{"a":1,"b":1}}}' \
        > /dev/null
    sleep 0.2
    AFTER=$(wc -c < "$SSE_TMP2")
    check "SSE: response to session 1 not echoed to session 2" "$BEFORE" "$AFTER"

    # CORS preflight
    CORS_STATUS=$(curl -s -o /dev/null -w "%{http_code}" -X OPTIONS \
        -H "Origin: http://localhost" \
        "http://localhost:$SSE_PORT/sse")
    check "SSE: OPTIONS → 204" "$CORS_STATUS" "204"

    # 404 for unknown path
    STATUS_404=$(curl -s -o /dev/null -w "%{http_code}" "http://localhost:$SSE_PORT/nope")
    check "SSE: unknown path → 404" "$STATUS_404" "404"

    kill "$CURL_PID" "$CURL_PID2" 2>/dev/null
    wait "$CURL_PID" "$CURL_PID2" 2>/dev/null || true
    cleanup_sse
fi

# ================================================================
# Summary
# ================================================================
echo ""
echo "Results: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
