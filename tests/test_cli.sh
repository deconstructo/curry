#!/usr/bin/env bash
# test_cli.sh — CLI feature tests.
#
# Usage: test_cli.sh <curry-binary>
#   curry-binary  path to the curry executable
#
# Covers: shebang in .scm, -c compilation, -o custom output, -x executable
# flag, combined getopt flags, magic-byte detection for extension-less files.
#
# Exits 0 on all-pass, 1 on any failure.

set -euo pipefail

CURRY="${1:?usage: test_cli.sh <curry>}"
# Put the build-tree curry binary first in PATH so #!/usr/bin/env curry picks it up
export PATH="$(dirname "$CURRY"):$PATH"

pass=0
fail=0

check() {
    local label="$1" actual="$2" expected="$3"
    if [ "$actual" = "$expected" ]; then
        echo "PASS: $label"
        (( pass++ )) || true
    else
        echo "FAIL: $label"
        echo "  expected: $(printf '%q' "$expected")"
        echo "  got:      $(printf '%q' "$actual")"
        (( fail++ )) || true
    fi
}

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

check_file_exists() {
    local label="$1" path="$2"
    if [ -f "$path" ]; then
        echo "PASS: $label"
        (( pass++ )) || true
    else
        echo "FAIL: $label (file not found: $path)"
        (( fail++ )) || true
    fi
}

check_executable() {
    local label="$1" path="$2"
    if [ -x "$path" ]; then
        echo "PASS: $label"
        (( pass++ )) || true
    else
        echo "FAIL: $label (file not executable: $path)"
        (( fail++ )) || true
    fi
}

check_has_shebang() {
    local label="$1" path="$2"
    # Check first 2 bytes are '#!' without touching binary data
    if [ "$(dd if="$path" bs=2 count=1 2>/dev/null | od -A n -t x1 | tr -d ' \n')" = "2321" ]; then
        echo "PASS: $label"
        (( pass++ )) || true
    else
        echo "FAIL: $label (no shebang in: $path)"
        (( fail++ )) || true
    fi
}

TMPDIR_CLI=$(mktemp -d /tmp/curry_cli_test_XXXXXX)
trap 'rm -rf "$TMPDIR_CLI"' EXIT

# ─── helpers ──────────────────────────────────────────────────────────────────

# A minimal Scheme script used throughout
HELLO_SCM="$TMPDIR_CLI/hello.scm"
cat > "$HELLO_SCM" << 'SCHEME'
(display "hello")
(newline)
SCHEME

# A script with a shebang line
SHEBANG_SCM="$TMPDIR_CLI/shebang.scm"
cat > "$SHEBANG_SCM" << 'SCHEME'
#!/usr/bin/env curry
(display "shebang-ok")
(newline)
SCHEME

# ─── Basic invocation ─────────────────────────────────────────────────────────

out=$("$CURRY" "$HELLO_SCM")
check "-e-less script" "$out" "hello"

out=$("$CURRY" -e '(display "eval-ok") (newline)')
check "-e expression" "$out" "eval-ok"

out=$("$CURRY" -v 2>&1 || true)
check_contains "-v shows version" "$out" "Curry"

# ─── Shebang handling in .scm files ───────────────────────────────────────────

out=$("$CURRY" "$SHEBANG_SCM")
check "shebang line is ignored by reader" "$out" "shebang-ok"

# Shebang mid-script should not be special (only #! at start of file)
MID_SHEBANG="$TMPDIR_CLI/mid_shebang.scm"
cat > "$MID_SHEBANG" << 'SCHEME'
(display "before")
; the next line starts with # but NOT at position 0
SCHEME
out=$("$CURRY" "$MID_SHEBANG")
check "non-leading # is a comment" "$out" "before"

# ─── -c: compile to .scc ─────────────────────────────────────────────────────

"$CURRY" -c "$HELLO_SCM"
SCC_DEFAULT="$TMPDIR_CLI/hello.scc"
check_file_exists "-c produces .scc alongside source" "$SCC_DEFAULT"

# Default .scc should NOT be executable (it's a library artifact)
if [ -x "$SCC_DEFAULT" ]; then
    echo "FAIL: -c without -x should not set +x on output"
    (( fail++ )) || true
else
    echo "PASS: -c without -x leaves output non-executable"
    (( pass++ )) || true
fi

# Default .scc should NOT have a shebang
first_bytes=$(head -c 8 "$SCC_DEFAULT" | xxd -p 2>/dev/null || od -A n -t x1 -N 8 "$SCC_DEFAULT" | tr -d ' \n')
if printf '%s' "$first_bytes" | grep -qi '2321'; then
    echo "FAIL: -c without -x should not prepend shebang"
    (( fail++ )) || true
else
    echo "PASS: -c without -x has no shebang"
    (( pass++ )) || true
fi

# Running the .scc directly should produce same output
out=$("$CURRY" "$SCC_DEFAULT")
check "run .scc directly produces correct output" "$out" "hello"

# ─── -c -o: custom output path ────────────────────────────────────────────────

CUSTOM_SCC="$TMPDIR_CLI/myprog.scc"
"$CURRY" -c "$HELLO_SCM" -o "$CUSTOM_SCC"
check_file_exists "-c -o produces file at custom path" "$CUSTOM_SCC"

out=$("$CURRY" "$CUSTOM_SCC")
check "run custom-named .scc produces correct output" "$out" "hello"

# -o before -c (flags in reverse order) should also work
CUSTOM_SCC2="$TMPDIR_CLI/myprog2.scc"
"$CURRY" -o "$CUSTOM_SCC2" -c "$HELLO_SCM"
check_file_exists "-o before -c also works" "$CUSTOM_SCC2"

out=$("$CURRY" "$CUSTOM_SCC2")
check "output of -o-first compilation is correct" "$out" "hello"

# ─── -c -x: executable flag ───────────────────────────────────────────────────

EXEC_SCC="$TMPDIR_CLI/exec.scc"
"$CURRY" -c "$HELLO_SCM" -o "$EXEC_SCC" -x
check_file_exists "-c -o -x produces output file" "$EXEC_SCC"
check_executable "-c -x sets +x on output" "$EXEC_SCC"
check_has_shebang "-c -x prepends shebang" "$EXEC_SCC"

out=$("$CURRY" "$EXEC_SCC")
check "executable .scc runs correctly via curry" "$out" "hello"

out=$("$EXEC_SCC")
check "executable .scc runs directly (no curry prefix)" "$out" "hello"

# ─── Combined getopt flags ─────────────────────────────────────────────────────

COMBINED_SCC="$TMPDIR_CLI/combined.scc"

# -xc FILE  (combined short flags, -c takes argument)
"$CURRY" -xc "$HELLO_SCM" -o "$COMBINED_SCC"
check_file_exists "-xc combined flag produces output" "$COMBINED_SCC"
check_executable "-xc sets +x" "$COMBINED_SCC"

COMBINED_SCC2="$TMPDIR_CLI/combined2.scc"
# -x -o PATH -c FILE
"$CURRY" -x -o "$COMBINED_SCC2" -c "$HELLO_SCM"
check_file_exists "-x -o PATH -c FILE works" "$COMBINED_SCC2"
check_executable "-x -o -c combined sets +x" "$COMBINED_SCC2"
out=$("$CURRY" "$COMBINED_SCC2")
check "-x -o -c output is correct" "$out" "hello"

# ─── Magic-byte detection: extension-less .scc files ─────────────────────────

NOEXT="$TMPDIR_CLI/myprog"
"$CURRY" -c "$HELLO_SCM" -o "$NOEXT" -x
check_file_exists "extension-less output created" "$NOEXT"
check_executable "extension-less output is executable" "$NOEXT"

# Run via curry (magic-byte probe should detect it as .scc)
out=$("$CURRY" "$NOEXT")
check "curry detects extension-less .scc by magic bytes" "$out" "hello"

# Run directly (shebang makes it executable)
out=$("$NOEXT")
check "extension-less .scc runs directly" "$out" "hello"

# ─── -l: load file before REPL expression ─────────────────────────────────────

DEFN_SCM="$TMPDIR_CLI/defn.scm"
cat > "$DEFN_SCM" << 'SCHEME'
(define (greet who) (string-append "hi " who))
SCHEME

out=$("$CURRY" -l "$DEFN_SCM" -e '(display (greet "world")) (newline)')
check "-l loads file before -e expression" "$out" "hi world"

# -l runs via the tree-walker (eval.c), not the compiler — regression test
# for a bug found in a full-codebase audit: the tree-walker's function-
# application dispatch used a fixed 64-slot array for evaluated arguments,
# and its loop bound (`argc < 64`) stopped EVALUATING argument expressions
# past the 64th, silently dropping their side effects too, with no error.
MANYARGS_SCM="$TMPDIR_CLI/manyargs.scm"
python3 - "$MANYARGS_SCM" << 'PYEOF'
import sys
path = sys.argv[1]
n = 70
marks = " ".join(f"(mark! {i})" for i in range(n))
with open(path, "w") as f:
    f.write("(define side-effects (make-vector %d #f))\n" % n)
    f.write("(define (mark! i) (vector-set! side-effects i #t) i)\n")
    f.write("(define (f . xs) (length xs))\n")
    f.write("(define result (f %s))\n" % marks)
    f.write("(define missed 0)\n")
    f.write("(let loop ((i 0)) (when (< i %d) (unless (vector-ref side-effects i) (set! missed (+ missed 1))) (loop (+ i 1))))\n" % n)
PYEOF
out=$("$CURRY" -l "$MANYARGS_SCM" -e '(display result) (display " ") (display missed) (newline)')
check "-l (tree-walker): >64-arg call preserves count and all side effects" "$out" "70 0"

# ─── Script receives arguments ────────────────────────────────────────────────

ARGS_SCM="$TMPDIR_CLI/args.scm"
cat > "$ARGS_SCM" << 'SCHEME'
; command-line-args includes the script path as first element
; Display only the script args (skip first element which is the script path)
(for-each (lambda (a) (display a) (display " ")) (cdr command-line-args))
(newline)
SCHEME

out=$("$CURRY" "$ARGS_SCM" foo bar baz)
check "script args available via command-line-args" "$out" "foo bar baz "

# Flags after positional arg should NOT be consumed by curry
PASSTHROUGH_SCM="$TMPDIR_CLI/passthrough.scm"
cat > "$PASSTHROUGH_SCM" << 'SCHEME'
; command-line-args = (script-path extra-args...), so length is 1 + n-extra-args
(display (- (length command-line-args) 1))
(newline)
SCHEME
out=$("$CURRY" "$PASSTHROUGH_SCM" --not-a-curry-flag)
check "flags after script path are passed as args, not parsed" "$out" "1"

# ─── define-syntax survives .scc cache-hit reruns ─────────────────────────────
# Regression test for a bug in native (compiler-driven, not tree-eval-punted)
# define-syntax codegen: a top-level macro must still be usable from the
# post-script REPL (-i) on a SECOND run of the same script, once its .scc
# cache exists — not just on the first run, which compiles (and thus
# eagerly registers) the macro fresh.
MACRO_SCM="$TMPDIR_CLI/macro_cache.scm"
cat > "$MACRO_SCM" << 'SCHEME'
(define-syntax my-double
  (syntax-rules ()
    ((_ x) (* 2 x))))
(display (my-double 5))
(newline)
SCHEME
rm -f "$TMPDIR_CLI/macro_cache.scc"
out=$(printf '(display (my-double 21))\n(newline)\n' | "$CURRY" -i "$MACRO_SCM")
check_contains "define-syntax usable from -i REPL on cache-miss run" "$out" "42"
check_file_exists "cache-miss run wrote .scc" "$TMPDIR_CLI/macro_cache.scc"
out=$(printf '(display (my-double 21))\n(newline)\n' | "$CURRY" -i "$MACRO_SCM")
check_contains "define-syntax usable from -i REPL on cache-hit run" "$out" "42"

# ─── (symbolic ...) survives .scc cache-hit reruns ─────────────────────────────
# Regression test: an earlier fix for symbolic's compiler codegen embedded
# the resolved sym-var primitive directly as a bytecode constant to make it
# immune to local shadowing. That crashed .scc serialization (a Primitive
# closes over a C function pointer, which can't be written to a cache file)
# on EVERY run, not just a cache-hit one — so this exercises both a fresh
# compile-and-write and a cache-hit reload.
SYMBOLIC_SCM="$TMPDIR_CLI/symbolic_cache.scm"
cat > "$SYMBOLIC_SCM" << 'SCHEME'
(symbolic x)
(display (symbolic? x))
(newline)
SCHEME
rm -f "$TMPDIR_CLI/symbolic_cache.scc"
out=$("$CURRY" "$SYMBOLIC_SCM")
check "symbolic works on cache-miss run" "$out" "#t"
check_file_exists "symbolic cache-miss run wrote .scc" "$TMPDIR_CLI/symbolic_cache.scc"
out=$("$CURRY" "$SYMBOLIC_SCM")
check "symbolic works on cache-hit run" "$out" "#t"

# ─── define-record-type survives .scc cache-hit reruns ────────────────────────
# Regression test: define-record-type's compiler codegen embeds the built
# RecordType (RTD) as an independent quoted constant in each of the
# constructor/predicate/accessor/mutator closures. In memory that's the
# same pointer referenced four times (harmless); serialized to a .scc file
# and read back, each becomes an INDEPENDENT reconstruction — four
# non-eq? RecordType objects — breaking the predicate's pointer-equality
# check. The cache-MISS run (same process that compiled it) doesn't
# exercise this, since it never round-trips through the file; only a
# cache-HIT run (a fresh process loading the .scc) does.
RECORD_SCM="$TMPDIR_CLI/record_cache.scm"
cat > "$RECORD_SCM" << 'SCHEME'
(define-record-type <point>
  (make-point x y)
  point?
  (x point-x)
  (y point-y))
(define p (make-point 3 4))
(display (point? p))
(display " ")
(display (point-x p))
(newline)
SCHEME
rm -f "$TMPDIR_CLI/record_cache.scc"
out=$("$CURRY" "$RECORD_SCM")
check "define-record-type works on cache-miss run" "$out" "#t 3"
check_file_exists "record cache-miss run wrote .scc" "$TMPDIR_CLI/record_cache.scc"
out=$("$CURRY" "$RECORD_SCM")
check "define-record-type predicate survives cache-hit run" "$out" "#t 3"

# ─── .scc cache: script that exits via (exit) never returns control to the
# read/compile loop's own end-of-loop scc_write() call — same root cause as
# (curry qt6)'s run-event-loop, which calls exit(3) itself once the Qt
# event loop returns and never hands control back to main.c. Without the
# atexit-based fallback, a script ending in (exit) would never get its
# .scc cache written, defeating transparent caching for every such script
# on every single run. ─────────────────────────────────────────────────────
EXIT_SCM="$TMPDIR_CLI/exit_cache.scm"
cat > "$EXIT_SCM" << 'SCHEME'
(display "before-exit")
(newline)
(exit 0)
SCHEME
rm -f "$TMPDIR_CLI/exit_cache.scc"
out=$("$CURRY" "$EXIT_SCM")
check "(exit)-ending script still runs correctly" "$out" "before-exit"
check_file_exists "(exit)-ending script's .scc gets written despite never reaching the loop's own scc_write" "$TMPDIR_CLI/exit_cache.scc"
out=$("$CURRY" --timings "$EXIT_SCM" 2>&1 >/dev/null)
check_contains "(exit)-ending script: second run is a cache HIT" "$out" "HIT"

# A genuine compile/runtime error must NOT leave behind a partial .scc: a
# later unchanged-content run would treat it as a complete, valid HIT and
# silently truncate execution to whatever compiled before the error.
ERR_SCM="$TMPDIR_CLI/err_cache.scm"
cat > "$ERR_SCM" << 'SCHEME'
(display "reached")
(newline)
(car '())
(display "never-reached")
SCHEME
rm -f "$TMPDIR_CLI/err_cache.scc"
"$CURRY" "$ERR_SCM" > /dev/null 2>&1 || true
if [ -f "$TMPDIR_CLI/err_cache.scc" ]; then
    echo "FAIL: a script that errors mid-compile does not write a partial .scc"
    (( fail++ )) || true
else
    echo "PASS: a script that errors mid-compile does not write a partial .scc"
    (( pass++ )) || true
fi

# ─── --timings: read/expand/compile/execute pipeline report ───────────────────
TIMINGS_SCM="$TMPDIR_CLI/timings_test.scm"
cat > "$TIMINGS_SCM" << 'SCHEME'
(define-syntax my-macro
  (syntax-rules ()
    [(my-macro x) (* x x)]))
(display (my-macro 21))
(newline)
SCHEME
rm -f "$TMPDIR_CLI/timings_test.scc"

# Disabled by default: no report, stdout unaffected.
out=$("$CURRY" "$TIMINGS_SCM")
check "no --timings: report absent" "$out" "441"

# Cache-miss run: goes through read+expand+compile+execute for real.
out=$("$CURRY" --timings "$TIMINGS_SCM" 2>&1 >/dev/null)
check_contains "--timings: report header present" "$out" "--timings (ms):"
check_contains "--timings: read line present"    "$out" "read"
check_contains "--timings: expand line present"  "$out" "expand"
check_contains "--timings: compile line present" "$out" "compile"
check_contains "--timings: execute line present" "$out" "execute"
check_contains "--timings: total line present"   "$out" "total"
out=$("$CURRY" --timings "$TIMINGS_SCM" 2>/dev/null)
check "--timings: stdout unaffected by report (report goes to stderr)" "$out" "441"

# Cache-hit run: compiled work should collapse to (near) zero, matching a
# skipped compile stage — this is the whole point of the instrumentation.
out=$("$CURRY" --timings "$TIMINGS_SCM" 2>&1 >/dev/null | grep 'compile')
check_contains "--timings: compile stage near-zero on .scc cache hit" "$out" "0.000"

# ─── .scc cache: content-hash keyed, with HIT/MISS visibility ─────────────────
CACHEHASH_SCM="$TMPDIR_CLI/cachehash_test.scm"
echo '(display (+ 1 2)) (newline)' > "$CACHEHASH_SCM"
rm -f "$TMPDIR_CLI/cachehash_test.scc"

out=$("$CURRY" --timings "$CACHEHASH_SCM" 2>&1 >/dev/null)
check_contains "cache: MISS reported on first (cache-miss) run" "$out" "MISS"
out=$("$CURRY" --timings "$CACHEHASH_SCM" 2>&1 >/dev/null)
check_contains "cache: HIT reported on second (cache-hit) run" "$out" "HIT"

# Content-hash keyed, not mtime: touching mtime with unchanged content must
# stay a HIT (mtime alone doesn't invalidate — the whole point of moving off
# the pre-v4 mtime+size scheme, which git checkout / cp -p / some editors
# can flip without changing content).
touch "$CACHEHASH_SCM"
out=$("$CURRY" --timings "$CACHEHASH_SCM" 2>&1 >/dev/null)
check_contains "cache: HIT survives an mtime touch with unchanged content" "$out" "HIT"

# Changed content must still invalidate the cache. One run does double duty:
# stdout must reflect the new content AND stderr must report MISS — check
# both from the same invocation (a prior plain run would itself consume the
# one available "miss" by recompiling and rewriting the cache).
echo '(display 99) (newline)' >> "$CACHEHASH_SCM"
out=$("$CURRY" --timings "$CACHEHASH_SCM" 2>/tmp/cachehash_stderr.$$)
err=$(cat /tmp/cachehash_stderr.$$); rm -f /tmp/cachehash_stderr.$$
check "cache: changed content is reflected (not served from stale cache)" "$out" "$(printf '3\n99')"
check_contains "cache: MISS reported after content actually changed" "$err" "MISS"

# -e/REPL don't go through the script-file cache at all — no cache line.
out=$("$CURRY" --timings -e '(display 1)' 2>&1 >/dev/null)
if printf '%s' "$out" | grep -q 'cache'; then
    echo "FAIL: --timings -e: no cache line (none expected, no cache decision made)"
    (( fail++ )) || true
else
    echo "PASS: --timings -e: no cache line (none expected, no cache decision made)"
    (( pass++ )) || true
fi

# Regression test: a process-substitution / pipe "source path" (bash's
# curry <(...), used by test_mcp.sh) must not be drained by cache-key
# hashing before the real compile pass gets to read it. Reading a whole
# file to hash it (the v4 content-hash scheme) consumes a one-shot,
# non-seekable stream — src_hash() now refuses non-regular files (checked
# via stat(), which doesn't touch content) rather than hashing them, so
# such inputs are simply never cached instead of silently losing their data.
out=$(printf '' | "$CURRY" <(echo '(display (+ 40 2)) (newline)'))
check "cache: process-substitution source is not drained by hashing" "$out" "42"

# A stale/wrong-format .scc (e.g. left over from an older curry build using
# the pre-v4 mtime+size scheme) must be rejected cleanly — recompiled from
# source, not misread/corrupted/crashed on. Hand-craft one: correct magic,
# wrong format-version byte (garbage after that is never reached, since the
# version check is the very next byte compared).
CACHEVER_SCM="$TMPDIR_CLI/cachever_test.scm"
echo '(display (* 6 7)) (newline)' > "$CACHEVER_SCM"
CACHEVER_SCC="$TMPDIR_CLI/cachever_test.scc"
printf 'CURRYBC\x03garbage-not-a-real-v3-body' > "$CACHEVER_SCC"
out=$("$CURRY" "$CACHEVER_SCM")
check "cache: stale/wrong-version .scc rejected cleanly, recompiles" "$out" "42"
ver_byte=$(od -An -tx1 -j 7 -N 1 "$CACHEVER_SCC" | tr -d ' \n')
check "cache: stale .scc rewritten in the current format version" "$ver_byte" "08"

# Regression: Chunk.src_lambda (procedure-lambda/procedure-arglist's data
# source) used to never be written to .scc at all, so a script's SECOND run
# (a cache HIT, loaded from disk) silently lost it and returned #f, even
# though the exact same script's FIRST run (a cache MISS, compiled fresh in
# memory) had it correctly. Run the identical script twice and confirm both
# runs -- not just the first -- report the real lambda form.
SRCLAMBDA_SCM="$TMPDIR_CLI/srclambda_test.scm"
cat > "$SRCLAMBDA_SCM" <<'EOF'
(define (add x y) (+ x y))
(display (procedure-lambda add))
(newline)
(display (procedure-arglist add))
(newline)
EOF
rm -f "$TMPDIR_CLI/srclambda_test.scc"
out1=$("$CURRY" "$SRCLAMBDA_SCM")
out2=$("$CURRY" "$SRCLAMBDA_SCM")
expected=$'(lambda (x y) (+ x y))\n(x y)'
check "cache: procedure-lambda survives the first (cache MISS) run" "$out1" "$expected"
check "cache: procedure-lambda survives a second (cache HIT) run" "$out2" "$expected"

# -e also reports.
out=$("$CURRY" --timings -e '(display (+ 1 2))' 2>&1 >/dev/null)
check_contains "--timings: works with -e" "$out" "--timings (ms):"

# -c (compile-only) must report real read/compile numbers too, not just
# expand — regression test for a review finding: the -c loop's
# scm_read/compiler_compile calls were originally uninstrumented, so
# --timings -c on a file using a macro silently showed read/compile/execute
# all at 0.000 while expand alone reported nonzero, misleadingly implying
# no real compiler work happened.
CTIMINGS_SCM="$TMPDIR_CLI/ctimings_test.scm"
cat > "$CTIMINGS_SCM" << 'SCHEME'
(define-syntax my-macro
  (syntax-rules ()
    [(my-macro x) (* x x)]))
(display (my-macro 21))
(newline)
SCHEME
rm -f "$TMPDIR_CLI/ctimings_test.scc"
out=$("$CURRY" --timings -c "$CTIMINGS_SCM" -o "$TMPDIR_CLI/ctimings_test.scc" 2>&1 >/dev/null)
check_contains "--timings -c: read line present"    "$out" "read"
read_val=$(printf '%s\n' "$out" | grep 'read' | awk '{print $2}')
compile_val=$(printf '%s\n' "$out" | grep 'compile' | awk '{print $2}')
[ "$read_val" != "0.000" ] && check "--timings -c: read is nonzero" "nonzero" "nonzero" \
    || check "--timings -c: read is nonzero" "$read_val" "nonzero"
[ "$compile_val" != "0.000" ] && check "--timings -c: compile is nonzero" "nonzero" "nonzero" \
    || check "--timings -c: compile is nonzero" "$compile_val" "nonzero"

# --help documents it.
out=$("$CURRY" --help 2>&1 || true)
check_contains "--help documents --timings" "$out" "--timings"

# ─── REPL commands survive a malformed argument (regression) ──────────────────
#
# ,expand/,asm/,break/,unbreak/,debug all read a second form (their
# argument) via scm_read after the top-level ,command read already
# succeeded. That second read used to run with no exception handler
# installed -- a malformed s-expression (e.g. an improper "(1 . . 2)")
# raised past it with nowhere to go, killing the whole REPL process
# instead of printing a read error and continuing. Found by independent
# review while verifying PR #97. If the REPL is still alive afterward,
# the trailing (display "still alive") runs and its output shows up.
for cmd in expand asm break unbreak debug; do
    out=$(printf ',%s (1 . . 2)\n(display "still alive")(newline)\n,quit\n' "$cmd" | "$CURRY" -i 2>&1 || true)
    check_contains "REPL survives malformed argument to ,$cmd" "$out" "still alive"
done

# ─── Malformed let/do/let-syntax/guard binding compiles to a clean error,
#     not a SIGSEGV (regression, issue #124) ──────────────────────────────────
#
# A let/let*/letrec/do/let-syntax/guard binding or clause with the wrong
# shape (not a pair, or a pair missing its init/body) was destructured via
# unchecked vcar/vcdr chains with no check that the binding itself was even
# a pair -- e.g. (let ((a)) 1) SIGSEGV'd the compiler instead of raising a
# catchable error. Three independent implementations had this bug: the
# Tier 2.1 IR pipeline (ir_lower.c/ir_emit.c), the classic bytecode
# compiler (compiler_classic.c), and the tree-walking evaluator (eval.c,
# reachable via the `eval` builtin -- covered separately in
# r7rs_tests.scm, since testing that path doesn't require a subprocess).
# do/let-syntax/guard specifically have NO IR lowering at all, so
# compiler_classic.c's own copy of the fix is these three forms' ONLY live
# compilation path outside eval.c's tree-walker -- this is the one place
# that path can actually be exercised, since a malformed TOP-LEVEL form
# must crash (or not) during the compile phase of a fresh `curry -e`
# invocation, before any runtime `guard` in a long-running test script
# could ever catch it.
#
# A bash pipeline's $? for a process killed by a signal is 128+signal
# (139 for SIGSEGV specifically on every platform this project targets);
# a clean Scheme-level error instead exits 1 via main.c's own error path.
# Asserting the exact clean-error exit code (not just "not 139") also
# catches a fix that silently swallows the error instead of reporting it.
check_no_segv() {
    local label="$1" form="$2"
    set +e
    "$CURRY" -e "$form" >/dev/null 2>&1
    local code=$?
    set -e
    check "$label" "$code" "1"
}
check_no_segv "let: malformed binding compiles to a clean error"          '(let ((a)) 1)'
check_no_segv "named let: malformed binding compiles to a clean error"    '(let loop ((a)) 1)'
check_no_segv "let*: malformed binding compiles to a clean error"         '(let* ((a)) 1)'
check_no_segv "letrec: malformed binding compiles to a clean error"       '(letrec ((a)) 1)'
check_no_segv "letrec*: malformed binding compiles to a clean error"      '(letrec* ((a)) 1)'
check_no_segv "do: malformed var-spec compiles to a clean error"          '(do ((a)) (#t) 1)'
check_no_segv "let-syntax: malformed binding compiles to a clean error"   '(let-syntax ((m)) 1)'
check_no_segv "letrec-syntax: malformed binding compiles to a clean error" '(letrec-syntax ((m)) 1)'
check_no_segv "guard: empty clause compiles to a clean error"             '(guard (e ()) 1)'

# ir_lower_letrec is shared by S_LETREC and S_LETREC_STAR; independent
# code review found it hardcoded "letrec" (not "letrec*") in the raised
# error message even for a letrec*-specific input -- cosmetic, but a
# wrong form name in an error message sent a developer looking at the
# wrong compiler code path. Checked here (not r7rs_tests.scm) because
# the malformed binding is a COMPILE-time error for the whole enclosing
# top-level form -- no runtime `guard` inside the same script could ever
# see it, so this needs the real subprocess-stderr text, same as every
# other compiler-path check in this section.
letrec_star_err=$("$CURRY" -e '(let ((f (lambda () (letrec* ((a)) 1)))) (f))' 2>&1 || true)
check "letrec*: malformed binding names letrec* (not letrec) in the error" \
      "$(echo "$letrec_star_err" | grep -c 'letrec\*: ill-formed special form')" "1"

# Issue #127: compile_cond/compile_case (compiler_classic.c) had the
# identical unchecked-clause-destructure crash as the let/letrec/do
# family above, found by independent security review of the #124/#125
# fix -- missed entirely since it's a different form family, not a
# variant of let. r7rs_tests.scm covers the eval.c (tree-walker) half
# via `eval`; these need a real subprocess compile for the same reason
# every other compiler-path check in this section does.
check_no_segv "cond: empty clause compiles to a clean error"  '(cond ())'
check_no_segv "cond: non-pair clause compiles to a clean error" '(cond 1)'
check_no_segv "case: empty clause compiles to a clean error"  '(case 1 ())'
check_no_segv "case: non-pair clause compiles to a clean error" '(case 1 1)'
out=$("$CURRY" -e '(display (list (cond (#f 1) (#t 2)) (case 2 ((1) (quote one)) ((2) (quote two)))))')
check "cond/case still compile and run correctly" "$out" "(2 two)"

# Confirms ordinary usage of all of these still compiles and runs correctly.
out=$("$CURRY" -e '(display (list (let ((a 1) (b 2)) (+ a b))
                                   (let* ((a 1) (b (+ a 1))) b)
                                   (letrec ((f (lambda (n) (if (= n 0) 1 (* n (f (- n 1))))))) (f 5))
                                   (do ((i 0 (+ i 1)) (s 0 (+ s i))) ((= i 5) s))
                                   (let-syntax ((m (syntax-rules () ((_ x) (+ x 1))))) (m 5))
                                   (guard (e (#t (quote caught))) (raise (quote oops)))))')
check "let/let*/letrec/do/let-syntax/guard still compile and run correctly" "$out" "(3 2 120 10 6 caught)"

# ─── Deeply nested let* compiles to a catchable stack-overflow, not a
#     SIGSEGV (regression, issue #125) ────────────────────────────────────────
#
# ir_emit and ir_emit_inline_call (Tier 2.1 IR pipeline, ir_emit.c)
# recurse into each other once per binding of a flat let*/letrec*/do
# chain -- unlike eval()'s own goto-tail trampoline, nothing here reuses
# a C frame, so a few hundred sequential bindings SIGSEGVs the whole
# process once the real C stack is exhausted, instead of the catchable
# stack-overflow condition eval() already raises for its own equivalent
# unbounded recursion. Fixed by sharing eval()'s own guard
# (check_c_stack_depth, runtime.c) at ir_emit's entry point. Must be
# tested via a real subprocess compile: `eval` exercises eval.c's own
# tree-walking let*, a plain while loop over bindings rather than
# per-binding C recursion, so it can't reach ir_emit.c's bug at all
# (see r7rs_tests.scm's own note at the same point).
#
# Built as literal source text, not generated Scheme, for the same
# reason r7rs_tests.scm's own 254-loop-variable named-let test (issue
# #120) is: this needs to appear as a real top-level form a subprocess
# actually compiles, and a plain bash loop avoids a python/perl
# dependency in this shell-based suite.
#
# 20001 bindings, not ~220 or even 3001: the guard fires at a FRACTION
# of the real per-thread C stack limit (check_c_stack_depth, runtime.c),
# and both the actual stack limit AND how much of it each recursion
# level of ir_emit consumes vary a lot across how this suite is built
# and invoked -- confirmed empirically twice:
#  - an interactive Debug-build shell here defaulted to an 8MB stack,
#    where ~220 bindings was enough to cross the guard's threshold, but
#    CTest's own test-runner process launches this script under a 64MB
#    stack, where 220 (and even 3000) bindings finished cleanly (exit
#    0, guard never fired) -- bumped to 3001, confirmed safe under a
#    64MB stack too.
#  - that 3001 figure then still failed the SAME way (exit 0, no
#    SIGSEGV) under CI's macOS Release build specifically: a Release
#    build's optimizer shrinks ir_emit's own per-call stack frame
#    enough that 3001 recursion levels no longer reaches the guard's
#    threshold at all, on top of the ulimit difference above. 20001 is
#    comfortably past the threshold measured empirically under a
#    Release build AND a 64MB stack simultaneously (triggers reliably
#    above ~8000 in that combination), while staying well under the
#    reader's own unrelated ~50000-element recursion limit (issue
#    #129) so this test keeps testing ir_emit's guard specifically,
#    not the reader's.
bindings=""
names=""
for i in $(seq 0 20000); do
    bindings="$bindings(p$i 1)"
    names="$names p$i"
done
big_letstar="(display (let* ($bindings) (+$names)))"
# Written to a real script file and run as a positional argument, NOT
# passed via -e: this form is ~300KB of source text, comfortably past
# Linux's default single-argument/whole-argv length limits (ARG_MAX) --
# confirmed the hard way when CI's ubuntu runners reported exit 126
# ("argument list too long", bash's own report for an execve() E2BIG)
# on the very first version of this test that used -e. A script file
# has no such limit.
BIG_LETSTAR_SCM="$TMPDIR_CLI/big_letstar.scm"
printf '%s\n' "$big_letstar" > "$BIG_LETSTAR_SCM"
set +e
"$CURRY" "$BIG_LETSTAR_SCM" >/dev/null 2>&1
big_letstar_code=$?
set -e
check "let* with 20001 sequential bindings compiles to a catchable stack-overflow, not a SIGSEGV" "$big_letstar_code" "1"
# Confirms a much smaller, entirely ordinary let* chain (nowhere near
# where the guard fires) still compiles and runs correctly.
out=$("$CURRY" -e '(display (let* ((p0 1) (p1 1) (p2 1) (p3 1) (p4 1) (p5 1) (p6 1) (p7 1) (p8 1) (p9 1)
                                    (p10 1) (p11 1) (p12 1) (p13 1) (p14 1) (p15 1) (p16 1) (p17 1) (p18 1) (p19 1))
                          (+ p0 p1 p2 p3 p4 p5 p6 p7 p8 p9 p10 p11 p12 p13 p14 p15 p16 p17 p18 p19)))')
check "let* with 20 sequential bindings still compiles and runs correctly" "$out" "20"

# ─── Symbolic CAS (sx_simplify/sx_diff/etc, symbolic.c) shares the same
#     stack-depth guard -- deep expression construction raises a
#     catchable stack-overflow instead of a SIGSEGV (issue #134) ────────────
#
# sx_simplify re-walks its ENTIRE argument tree from scratch on every
# call (no memoization of already-simplified subexpressions), so
# building a chain of N nested `(sin ...)` applications one at a time
# (each call re-simplifying the whole existing tree so far) costs
# O(depth-at-guard^2), not O(N) -- confirmed empirically to take 30+
# seconds under a Release build with a real 64MB stack, since the
# guard's threshold (and therefore how deep the chain gets before
# firing) scales with the real available stack. Rather than chase an
# ever-larger N to outrun a bigger stack (as #125/#129/#133's own
# thresholds needed), this test explicitly constrains the CHILD
# process's own stack via `ulimit -s` first, making the guard's
# threshold small, deterministic, and fast regardless of the host
# environment's default stack or build type -- confirmed reliable and
# fast (well under a second) across both Debug and Release builds.
SYMDEEP_SCM="$TMPDIR_CLI/symdeep.scm"
cat > "$SYMDEEP_SCM" << 'SYMDEEP_EOF'
(define x (sym-var 'x))
(define (wrap n e) (if (= n 0) e (wrap (- n 1) (sin e))))
(guard (e (#t (display (error-message e)))) (wrap 3000 x) (display "no-crash"))
SYMDEEP_EOF
set +e
symdeep_out=$(bash -c "ulimit -s 2048; \"$CURRY\" \"$SYMDEEP_SCM\"" 2>&1)
set -e
# Matches on the specific stack-overflow condition's own message
# (via error-message), not just "guard caught something" -- an
# unrelated error (a typo, an unbound name) would otherwise also
# satisfy a bare "did guard's #t clause fire" check.
check_contains "symbolic: deep expression construction raises a catchable stack-overflow, not a SIGSEGV" \
               "$symdeep_out" "call stack overflow"
# Confirms ordinary symbolic construction/simplification/differentiation/
# printing (nowhere near where the guard fires) still works correctly.
sym_out=$("$CURRY" -e '(define x (sym-var (quote x)))
(display (list (sym->string (sym-expr (quote +) (sym-expr (quote sin) x) (sym-expr (quote *) x x)))
               (simplify (sym-expr (quote +) x 0))
               (∂ (sym-expr (quote sin) x) x)))')
check "symbolic: ordinary construction/simplify/diff/printing still work correctly" \
      "$sym_out" "(sin(x) + x^2 x (cos x))"

# ─── define-record-type / syntax-rules crash on malformed input on BOTH
#     the compiled and tree-walked paths (issue #135) ───────────────────────
#
# Unlike every other issue in this series (#124-#132), record_type_build_spec
# and sr_compile_fn are each a single function shared by compiler.c's native
# codegen and eval.c's own tree-walker case -- tests/r7rs_tests.scm's own
# `eval`-based checks already exercise this same shared code, but these
# subprocess checks confirm the actual top-level compiled path too.
check_no_segv "define-record-type: missing ctor/pred compiles to a clean error" \
              '(define-record-type x)'
check_no_segv "define-record-type: name itself a list compiles to a clean error" \
              '(define-record-type (x))'
check_no_segv "define-record-type: non-pair ctor-form compiles to a clean error" \
              '(define-record-type point x point? (x px))'
check_no_segv "define-record-type: non-pair field-spec compiles to a clean error" \
              '(define-record-type point (mk-point x) point? y)'
check_no_segv "syntax-rules: ellipsis identifier with nothing after it compiles to a clean error" \
              '(define-syntax m (syntax-rules x))'
check_no_segv "syntax-rules: empty rule compiles to a clean error" \
              '(define-syntax m (syntax-rules () ()))'
out=$("$CURRY" -e '(define-record-type point (mk-point x y) point? (x point-x) (y point-y set-point-y!))
(define-syntax my-if (syntax-rules () ((_ c t e) (cond (c t) (else e)))))
(display (list (point? (mk-point 1 2)) (point-x (mk-point 1 2)) (my-if #t (quote yes) (quote no))))')
check "define-record-type/syntax-rules still compile and run correctly" "$out" "(#t 1 yes)"

# A second round of independent review found the R6RS branch of
# record_type_build_spec had no validation at all, and that a
# malformed syntax-rules pattern (as opposed to a malformed rule)
# passed definition-time validation but crashed on first USE of the
# macro -- fixed to raise at definition time instead, so these need
# only the bare (define-syntax ...) form, not a call to the macro.
check_no_segv "define-record-type: R6RS field-spec too short compiles to a clean error" \
              '(define-record-type x (fields (mutable)))'
check_no_segv "define-record-type: R6RS non-symbol field name compiles to a clean error" \
              '(define-record-type x (fields 5))'
check_no_segv "define-record-type: non-symbol name compiles to a clean error" \
              '(define-record-type 5 (fields a))'
check_no_segv "define-record-type: name itself a pair compiles to a clean error" \
              '(define-record-type (x) (fields a))'
check_no_segv "syntax-rules: non-pair pattern compiles to a clean error" \
              '(define-syntax m (syntax-rules () (x 1)))'
check_no_segv "syntax-rules: fixnum pattern compiles to a clean error" \
              '(define-syntax m (syntax-rules () (5 1)))'
out2=$("$CURRY" -e '(define-record-type point2 (fields (mutable x) (immutable y) z))
(display (list (point2? (make-point2 1 2 3)) (point2-x (make-point2 1 2 3))))')
check "define-record-type R6RS with valid field specs still compiles and runs correctly" "$out2" "(#t 1)"

# ─── Summary ──────────────────────────────────────────────────────────────────

echo
echo "Results: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
