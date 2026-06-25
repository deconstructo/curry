#!/usr/bin/env bash
# tools/punish.sh — run the heavy benchmark suite in parallel, looping until ^C
#
# Usage:
#   tools/punish.sh              # 4 parallel workers, all suites
#   tools/punish.sh -j 8         # 8 workers
#   tools/punish.sh -j 2 -s gc   # 2 workers, GC suite only
#   tools/punish.sh -n 3         # stop after 3 rounds (default: infinite)
#
# Each worker runs bench_heavy.scm with a unique --label (worker-N) so
# results can be separated in Grafana.  All workers publish to the same
# MQTT topic so the live dashboard shows the aggregate storm.

set -euo pipefail

CURRY="./build/curry"
BENCH="tests/bench_heavy.scm"
JOBS=4
SUITE="all"
MAX_ROUNDS=0   # 0 = infinite

while [[ $# -gt 0 ]]; do
  case "$1" in
    -j) JOBS="$2";   shift 2 ;;
    -s) SUITE="$2";  shift 2 ;;
    -n) MAX_ROUNDS="$2"; shift 2 ;;
    *)  echo "Unknown option: $1" >&2; exit 1 ;;
  esac
done

if [[ ! -x "$CURRY" ]]; then
  echo "Build not found at $CURRY — run: cmake --build build" >&2
  exit 1
fi

PIDS=()
ROUND=0
ALIVE=1

trap_cleanup() {
  ALIVE=0
  echo ""
  echo ">>> Caught signal — waiting for workers to finish current round..."
  for pid in "${PIDS[@]}"; do
    kill "$pid" 2>/dev/null || true
  done
  wait 2>/dev/null || true
  echo ">>> Done after $ROUND complete rounds."
  exit 0
}
trap trap_cleanup INT TERM

echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║  Curry punishment run                                           ║"
echo "║  workers=$JOBS  suite=$SUITE  max_rounds=${MAX_ROUNDS:-∞}                      ║"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo ""

run_round() {
  local round="$1"
  PIDS=()
  echo "━━━ Round $round — launching $JOBS workers (suite=$SUITE) ━━━"
  for i in $(seq 1 "$JOBS"); do
    "$CURRY" "$BENCH" --suite "$SUITE" --label "worker-$i" &
    PIDS+=($!)
  done
  # Wait for all workers, tolerating individual failures
  local failed=0
  for pid in "${PIDS[@]}"; do
    wait "$pid" || { echo "Worker $pid exited non-zero" >&2; failed=1; }
  done
  PIDS=()
  if [[ $failed -ne 0 ]]; then
    echo "Warning: one or more workers failed in round $round" >&2
  fi
  echo "━━━ Round $round complete ━━━"
  echo ""
}

while [[ $ALIVE -eq 1 ]]; do
  ROUND=$((ROUND + 1))
  run_round "$ROUND"
  if [[ $MAX_ROUNDS -gt 0 && $ROUND -ge $MAX_ROUNDS ]]; then
    echo ">>> Reached $MAX_ROUNDS rounds. Stopping."
    break
  fi
done
