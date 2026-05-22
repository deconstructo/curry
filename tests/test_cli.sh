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

# ─── Summary ──────────────────────────────────────────────────────────────────

echo
echo "Results: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
