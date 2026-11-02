#!/bin/bash
# Usage: run_with_hang_dump.sh SECONDS COMMAND [ARGS...]
#
# Runs COMMAND. If it is still running after SECONDS, prints a stack of
# every thread (gdb on Linux, sample on macOS) to stderr, kills it, and
# exits 124. Used for tests that hang rarely (issue #312): a plain CTest
# TIMEOUT kills the process and leaves nothing to diagnose.
limit=$1; shift
"$@" &
pid=$!
for ((t = 0; t < limit; t++)); do
    kill -0 "$pid" 2>/dev/null || { wait "$pid"; exit $?; }
    sleep 1
done
echo "run_with_hang_dump: pid $pid still running after ${limit}s: $*" >&2
if command -v gdb >/dev/null 2>&1; then
    gdb_cmd=(gdb -p "$pid" -batch -ex 'set pagination off' -ex 'thread apply all bt')
    "${gdb_cmd[@]}" >&2 2>&1 || sudo -n "${gdb_cmd[@]}" >&2 2>&1
elif command -v sample >/dev/null 2>&1; then
    sample "$pid" 3 >&2 2>&1
else
    echo "run_with_hang_dump: neither gdb nor sample is installed" >&2
fi
kill -9 "$pid" 2>/dev/null
wait "$pid" 2>/dev/null
exit 124
