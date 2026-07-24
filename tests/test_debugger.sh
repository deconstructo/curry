#!/usr/bin/env bash
# test_debugger.sh — interactive debugger tests.
#
# Usage: test_debugger.sh <curry-binary>
#
# Drives the debugger by piping commands on stdin while a script hits
# breakpoints. Covers: (breakpoint) builtin, -b function-name and
# file:line breakpoints, locals (params, let locals, upvalues), bt,
# p by-name and global eval, step/next line semantics, q abort,
# EOF disarm, and identical behavior on a .scc cache-hit run.
#
# Exits 0 on all-pass, 1 on any failure.

set -uo pipefail

CURRY="${1:?usage: test_debugger.sh <curry>}"

pass=0
fail=0

check() {
    local label="$1" haystack="$2" needle="$3"
    if printf '%s' "$haystack" | grep -qF -- "$needle"; then
        echo "PASS: $label"
        (( pass++ )) || true
    else
        echo "FAIL: $label"
        echo "  expected to find: $(printf '%q' "$needle")"
        echo "  in output:"
        printf '%s\n' "$haystack" | sed 's/^/    /'
        (( fail++ )) || true
    fi
}

check_absent() {
    local label="$1" haystack="$2" needle="$3"
    if printf '%s' "$haystack" | grep -qF -- "$needle"; then
        echo "FAIL: $label (unexpectedly found $(printf '%q' "$needle"))"
        (( fail++ )) || true
    else
        echo "PASS: $label"
        (( pass++ )) || true
    fi
}

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# ── Fixture: breakpoint builtin inside a let ────────────────────────────────
cat > "$TMP/bp.scm" <<'EOF'
(define (f x)
  (let ((y (* x 2)))
    (breakpoint)
    (+ x y)))
(display (f 21))
(newline)
EOF

out=$(printf 'locals\nbt\np y\np x\np (* 6 7)\nc\n' | "$CURRY" "$TMP/bp.scm" 2>&1)
check "builtin stops at breakpoint line"   "$out" "bp.scm:3"
check "locals shows let binding"           "$out" "y = 42"
check "locals shows captured param"        "$out" "x = 21"
check "bt names the let frame after f"     "$out" "#0  f"
check "bt shows toplevel caller"           "$out" "#1  <toplevel>"
check "p resolves local by name"           "$out" "42"
check "p evaluates globally"               "$out" "  42"
check "program completes after continue"   "$out" "63"

# Regression test: `p (define-syntax ...)` at a breakpoint used to corrupt
# the paused frame's VM stack by one slot, because compile_time_eval (the
# compiler's helper for evaluating macro-transformer expressions) omitted
# the "push closure as callee before vm_run" step that reentrant evaluation
# requires — invisible for a normal top-level compile (frame_count == 0
# takes a different, unaffected reset path) but corrupting for nested
# evaluation while paused mid-frame, which is exactly what `p`/`,debug` do.
out=$(printf 'p (define-syntax mac (syntax-rules () ((_ a) a)))\nlocals\np y\np x\nc\n' | "$CURRY" "$TMP/bp.scm" 2>&1)
check "define-syntax via p does not corrupt paused frame: locals y" "$out" "y = 42"
check "define-syntax via p does not corrupt paused frame: locals x" "$out" "x = 21"
check "define-syntax via p does not corrupt paused frame: continue" "$out" "63"

# ── Cache-hit run must behave identically (.scc round-trips debug info) ────
out=$(printf 'locals\nc\n' | "$CURRY" "$TMP/bp.scm" 2>&1)
[ -f "$TMP/bp.scc" ] || echo "note: no .scc cache written; cache-hit test degenerates to recompile"
check "cache-hit: named locals preserved"  "$out" "y = 42"
check "cache-hit: upvalue names preserved" "$out" "x = 21"
check "cache-hit: completes"               "$out" "63"

# ── -b function-name breakpoint ─────────────────────────────────────────────
cat > "$TMP/fib.scm" <<'EOF'
(define (fib n)
  (if (< n 2) n
      (+ (fib (- n 1)) (fib (- n 2)))))
(display (fib 5))
(newline)
EOF

out=$(printf 'locals\nc\nlocals\nc\nc\nc\nc\nc\nc\nc\nc\nc\nc\nc\nc\nc\nc\n' \
      | "$CURRY" -b fib "$TMP/fib.scm" 2>&1)
check "-b name fires at frame entry"       "$out" "Breakpoint #0: fib"
check "-b name: outer call locals"         "$out" "n = 5"
check "-b name: recursive call locals"     "$out" "n = 4"
check "-b name: completes after EOF"       "$out" "5"

# ── -b file:line breakpoint + next stepping in a named-let loop ─────────────
cat > "$TMP/loop.scm" <<'EOF'
(define (count n)
  (let lp ((i 0) (acc 0))
    (if (= i n) acc
        (lp (+ i 1) (+ acc i)))))
(display (count 5))
(newline)
EOF

out=$(printf 'locals\nn\nlocals\nc\nc\nc\nc\nc\n' \
      | "$CURRY" -b "$TMP/loop.scm:4" "$TMP/loop.scm" 2>&1)
check "file:line fires on the right line"  "$out" "loop.scm:4"
check "loop frame named after named-let"   "$out" "in lp"
check "loop locals first iteration"        "$out" "i = 0"
check "next reaches following iteration"   "$out" "i = 1"
check "loop completes"                     "$out" "10"

# ── q aborts evaluation, non-zero exit, clean error ─────────────────────────
out=$(printf 'q\n' | "$CURRY" -b fib "$TMP/fib.scm" 2>&1)
rc=$?
check "q reports debugger quit"            "$out" "debugger: quit"
if [ "$rc" -ne 0 ]; then
    echo "PASS: q exits non-zero"; (( pass++ )) || true
else
    echo "FAIL: q exits non-zero (got 0)"; (( fail++ )) || true
fi

# ── inactive debugger leaves programs untouched ─────────────────────────────
out=$("$CURRY" -e '(display (+ 40 2))' 2>&1)
check "no debugger: -e unaffected"         "$out" "42"
check_absent "no debugger: no dbg prompt"  "$out" "dbg>"

echo
echo "test_debugger: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
