#!/usr/bin/env bash
# LSP module tests — drives lsp-serve over its real Content-Length-framed
# stdio transport, the same way an editor client would.
#
# Usage: test_lsp.sh <curry-binary> <mods-dir>
#   curry-binary  path to the curry executable
#   mods-dir      directory containing lsp.so (CURRY_MODULE_PATH)
#
# Exits 0 on all-pass, 1 on any failure.

set -euo pipefail

CURRY="${1:?usage: test_lsp.sh <curry> <mods-dir>}"
MOD_PATH="${2:?usage: test_lsp.sh <curry> <mods-dir>}"
export CURRY_MODULE_PATH="$MOD_PATH"

pass=0
fail=0

check_contains() {
    local label="$1" haystack="$2" needle="$3"
    if printf '%s' "$haystack" | grep -qF -- "$needle"; then
        echo "PASS: $label"
        (( pass++ )) || true
    else
        echo "FAIL: $label (string not found)"
        echo "  looking for: $needle"
        echo "  in: $haystack"
        (( fail++ )) || true
    fi
}

check_not_contains() {
    local label="$1" haystack="$2" needle="$3"
    if printf '%s' "$haystack" | grep -qF -- "$needle"; then
        echo "FAIL: $label (string unexpectedly found)"
        echo "  not looking for: $needle"
        echo "  in: $haystack"
        (( fail++ )) || true
    else
        echo "PASS: $label"
        (( pass++ )) || true
    fi
}

# frame JSON  ->  Content-Length-framed JSON-RPC message on stdout
frame() {
    local json="$1" len
    len=$(printf '%s' "$json" | wc -c | tr -d ' ')
    printf 'Content-Length: %s\r\n\r\n%s' "$len" "$json"
}

# run_session MSG...  ->  concatenates each MSG (a JSON-RPC message string)
# as a framed message and pipes the whole stream into one lsp-serve process,
# returning everything written to stdout across the whole session.
run_session() {
    local stream=""
    for msg in "$@"; do
        stream+=$(frame "$msg")
    done
    printf '%s' "$stream" \
        | "$CURRY" -e '(import (curry lsp)) (lsp-serve)' 2>/dev/null
}

INITIALIZE='{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}'
INITIALIZED='{"jsonrpc":"2.0","method":"initialized","params":{}}'
SHUTDOWN='{"jsonrpc":"2.0","method":"shutdown","id":99}'
EXIT='{"jsonrpc":"2.0","method":"exit"}'

# ================================================================
# 1. initialize — capabilities advertised
# ================================================================
echo "=== initialize ==="

INIT_OUT=$(run_session "$INITIALIZE" "$SHUTDOWN" "$EXIT")
check_contains "initialize: textDocumentSync" "$INIT_OUT" '"textDocumentSync":1'
check_contains "initialize: hoverProvider"    "$INIT_OUT" '"hoverProvider":true'
check_contains "initialize: completionProvider" "$INIT_OUT" '"completionProvider"'
check_contains "initialize: serverInfo name"  "$INIT_OUT" '"curry-lsp"'

# ================================================================
# 2. diagnostics — real reader errors, and clearing them
# ================================================================
echo ""
echo "=== diagnostics ==="

didOpen() {
    local uri="$1" text="$2"
    printf '{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"%s","languageId":"scheme","version":1,"text":"%s"}}}' "$uri" "$text"
}

didChange() {
    local uri="$1" text="$2"
    printf '{"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"%s","version":2},"contentChanges":[{"text":"%s"}]}}' "$uri" "$text"
}

BAD_OUT=$(run_session "$INITIALIZE" "$INITIALIZED" \
    "$(didOpen 'file:///t.scm' '(define (f x)')" \
    "$SHUTDOWN" "$EXIT")
check_contains "diagnostics: unterminated define reports read-error" "$BAD_OUT" 'read-error'
check_contains "diagnostics: severity is error (1)" "$BAD_OUT" '"severity":1'

FIXED_OUT=$(run_session "$INITIALIZE" "$INITIALIZED" \
    "$(didOpen 'file:///t.scm' '(define (f x) (+ x 1))')" \
    "$SHUTDOWN" "$EXIT")
check_contains "diagnostics: clean buffer -> empty array" "$FIXED_OUT" '"diagnostics":[]'

CHANGE_OUT=$(run_session "$INITIALIZE" "$INITIALIZED" \
    "$(didOpen 'file:///t.scm' '(define (f x')" \
    "$(didChange 'file:///t.scm' '(define (f x) x)')" \
    "$SHUTDOWN" "$EXIT")
check_contains "diagnostics: didChange re-parses and clears error" "$CHANGE_OUT" '"diagnostics":[]'

# ================================================================
# 3. hover — special forms, builtins, Akkadian synonyms
# ================================================================
echo ""
echo "=== hover ==="

hover() {
    local id="$1" uri="$2" line="$3" character="$4"
    printf '{"jsonrpc":"2.0","id":%s,"method":"textDocument/hover","params":{"textDocument":{"uri":"%s"},"position":{"line":%s,"character":%s}}}' \
        "$id" "$uri" "$line" "$character"
}

# "(define x (display 1))"
#   indices: 1-6 = define, 11-17 = display
HOVER_OUT=$(run_session "$INITIALIZE" "$INITIALIZED" \
    "$(didOpen 'file:///h.scm' '(define x (display 1))')" \
    "$(hover 2 'file:///h.scm' 0 3)" \
    "$(hover 3 'file:///h.scm' 0 13)" \
    "$SHUTDOWN" "$EXIT")
check_contains "hover: define -> special form"        "$HOVER_OUT" 'define** — special form'
check_contains "hover: display -> builtin procedure"  "$HOVER_OUT" 'display** — builtin procedure'

AKK_OUT=$(run_session "$INITIALIZE" "$INITIALIZED" \
    "$(didOpen 'file:///akk.scm' '𒁹 x 5')" \
    "$(hover 2 'file:///akk.scm' 0 0)" \
    "$SHUTDOWN" "$EXIT")
check_contains "hover: cuneiform synonym resolves to canonical form" \
    "$AKK_OUT" 'Akkadian synonym for `define`'

# ================================================================
# 4. completion — generated table + structurally-collected locals
# ================================================================
echo ""
echo "=== completion ==="

completion() {
    local id="$1" uri="$2"
    printf '{"jsonrpc":"2.0","id":%s,"method":"textDocument/completion","params":{"textDocument":{"uri":"%s"},"position":{"line":0,"character":0}}}' \
        "$id" "$uri"
}

COMP_SRC='(define (square x) (* x x)) (let loop ((i 0)) (if (< i 3) (loop (+ i 1)) i)) (define total 0) (lambda (foo . rest) foo)'
COMP_OUT=$(run_session "$INITIALIZE" "$INITIALIZED" \
    "$(didOpen 'file:///c.scm' "$COMP_SRC")" \
    "$(completion 2 'file:///c.scm')" \
    "$SHUTDOWN" "$EXIT")
check_contains "completion: local procedure name (square)"  "$COMP_OUT" '"label":"square"'
check_contains "completion: named-let loop name"             "$COMP_OUT" '"label":"loop"'
check_contains "completion: local variable (total)"          "$COMP_OUT" '"label":"total"'
check_contains "completion: dotted rest arg (rest)"           "$COMP_OUT" '"label":"rest"'
check_contains "completion: static builtin (display)"         "$COMP_OUT" '"label":"display"'
check_contains "completion: static special form (define)"     "$COMP_OUT" '"label":"define"'
check_contains "completion: local var kind is Variable (6)"   "$COMP_OUT" '"label":"total","kind":6'

# ================================================================
# 5. pathological nesting — must degrade gracefully, not crash
# ================================================================
echo ""
echo "=== nesting depth guard ==="

deep_parens() {
    local n="$1" opens="" closes="" i
    for (( i = 0; i < n; i++ )); do opens+="("; closes+=")"; done
    printf '%s%s' "$opens" "$closes"
}

DEEP_TEXT=$(deep_parens 1500)
DEEP_OUT=$(run_session "$INITIALIZE" "$INITIALIZED" \
    "$(didOpen 'file:///deep.scm' "$DEEP_TEXT")" \
    "$SHUTDOWN" "$EXIT")
check_contains "deep nesting: rejected with a diagnostic, not a crash" \
    "$DEEP_OUT" 'nesting too deep to analyze'
check_contains "deep nesting: server survives to shutdown" \
    "$DEEP_OUT" '"id":99,"result":null'

# The depth guard is a textual pre-scan, not a real parse — it must not be
# fooled by #\" or #\; character literals into thinking those bytes open a
# string or line comment (which would desync its counter from what the real
# reader does and let pathological nesting slip through uncounted).
json_escape() {
    local s="$1"
    s="${s//\\/\\\\}"
    s="${s//\"/\\\"}"
    printf '%s' "$s"
}

didOpen_raw() {
    local uri="$1" text="$2"
    printf '{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"%s","languageId":"scheme","version":1,"text":"%s"}}}' \
        "$uri" "$(json_escape "$text")"
}

CHARLIT_QUOTE_TEXT="(define x #\\\" $(deep_parens 1500))"
CHARLIT_QUOTE_OUT=$(run_session "$INITIALIZE" "$INITIALIZED" \
    "$(didOpen_raw 'file:///charlit1.scm' "$CHARLIT_QUOTE_TEXT")" \
    "$SHUTDOWN" "$EXIT")
check_contains "deep nesting: #\\\" character literal can't bypass the guard" \
    "$CHARLIT_QUOTE_OUT" 'nesting too deep to analyze'
check_contains "deep nesting: server survives #\\\" bypass attempt" \
    "$CHARLIT_QUOTE_OUT" '"id":99,"result":null'

CHARLIT_SEMI_TEXT="(define x #\\; $(deep_parens 1500))"
CHARLIT_SEMI_OUT=$(run_session "$INITIALIZE" "$INITIALIZED" \
    "$(didOpen_raw 'file:///charlit2.scm' "$CHARLIT_SEMI_TEXT")" \
    "$SHUTDOWN" "$EXIT")
check_contains "deep nesting: #\\; character literal can't bypass the guard" \
    "$CHARLIT_SEMI_OUT" 'nesting too deep to analyze'
check_contains "deep nesting: server survives #\\; bypass attempt" \
    "$CHARLIT_SEMI_OUT" '"id":99,"result":null'

# Sanity: ordinary character literals (including of paren characters
# themselves) must still parse as clean, non-flagged documents.
NORMAL_CHARLIT_TEXT='(define x #\a) (display #\() (display #\))'
NORMAL_CHARLIT_OUT=$(run_session "$INITIALIZE" "$INITIALIZED" \
    "$(didOpen_raw 'file:///charlit3.scm' "$NORMAL_CHARLIT_TEXT")" \
    "$SHUTDOWN" "$EXIT")
check_contains "normal character literals -> no false positive" \
    "$NORMAL_CHARLIT_OUT" '"diagnostics":[]'

# ================================================================
# 6. didClose — document no longer available afterward
# ================================================================
echo ""
echo "=== didClose ==="

didClose() {
    local uri="$1"
    printf '{"jsonrpc":"2.0","method":"textDocument/didClose","params":{"textDocument":{"uri":"%s"}}}' "$uri"
}

CLOSE_OUT=$(run_session "$INITIALIZE" "$INITIALIZED" \
    "$(didOpen 'file:///close.scm' '(define x 1)')" \
    "$(didClose 'file:///close.scm')" \
    "$(hover 2 'file:///close.scm' 0 1)" \
    "$SHUTDOWN" "$EXIT")
check_contains "didClose: hover on closed doc returns null result" \
    "$CLOSE_OUT" '"id":2,"result":null'

# ================================================================
# 7. unknown method — proper JSON-RPC error, not silence or a crash
# ================================================================
echo ""
echo "=== unknown method ==="

UNKNOWN_OUT=$(run_session "$INITIALIZE" \
    '{"jsonrpc":"2.0","id":2,"method":"textDocument/noSuchThing","params":{}}' \
    "$SHUTDOWN" "$EXIT")
check_contains "unknown method -> JSON-RPC method-not-found (-32601)" \
    "$UNKNOWN_OUT" '-32601'

# ================================================================
# Summary
# ================================================================
echo ""
echo "Results: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
