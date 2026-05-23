# Profiling module — `(curry profiling)`

The profiling module exposes Curry's built-in runtime profiler to Scheme.
The instrumentation is always compiled into the main binary (`src/profiling.c`);
this module provides the controls and the report procedure.

## Quick start

```scheme
(import (curry profiling))

(profiler-start)      ; level 1: count calls

(define (fib n)
  (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))

(fib 20)

(profiler-report)
; => ((fib . (21891 . 0)) ...)

(profiler-stop)
```

## Profiling levels

| Level | What is recorded |
|-------|-----------------|
| `0` | Off — single branch not taken; effectively zero overhead |
| `1` | Call counts for named closures (both TCO and apply paths) |
| `2` | Wall-clock timing for named closures; TCO sacrificed for non-self-tail calls |
| `3` | Level 2, plus call counts for built-in primitives |

TCO calls (tail-recursive self-calls optimised via `goto tail`) are always
counted at levels 1–3 but never timed — there is no natural exit point on the
TCO path.

### Level 2 — TCO trade-off

At level 2, the evaluator intercepts calls to named closures *before* the
`goto tail` optimisation so that a return address exists and wall-clock time
can be measured.  This is accurate but means those call frames stay on the C
stack for the duration of the call.

To prevent unbounded stack growth in recursive loops, the intercept is skipped
when a closure is already executing itself (i.e. `proc == prof2_current` for
the current thread).  Self-tail-recursive calls fall through to the normal TCO
path and are counted but not individually timed.  Mutually recursive functions
are fully timed because they involve different closure objects.

In practice: level 2 is safe for typical workloads.  If you have a hot
self-tail loop that reaches very deep recursion *and* calls other named
functions in its body, those inner calls will still be timed correctly.

## API

### `(profiler-start [level])` → void

Enable profiling at `level` (default `1`). Also updates the Scheme binding
`**eval-profiler**` so the level is visible to other code.

```scheme
(profiler-start)    ; count calls
(profiler-start 2)  ; count + time
(profiler-start 3)  ; count + time + primitives
```

### `(profiler-stop)` → void

Set the profiling level to 0. Accumulated data is preserved until
`profiler-reset` is called.

### `(profiler-reset)` → void

Clear all accumulated profiling data. Does not change the current level.

### `(profiler-level)` → fixnum

Return the current profiling level (0–3).

### `(profiler-report)` → alist

Return an alist of the form `((name . (calls . ns)) ...)`, sorted by total
call count, descending.

- `name` — interned symbol (the procedure name as defined)
- `calls` — total call count (apply + TCO paths combined)
- `ns` — accumulated wall-clock nanoseconds (non-zero only at level ≥ 2 for
  calls through `apply()`; TCO calls contribute 0 to this field)

## Built-in primitives (no import required)

Two procedures are registered directly in the global environment by
`builtins_register()` and are always available without importing the profiling
module:

| Procedure | Description |
|-----------|-------------|
| `(profiling-report)` | Same as `(profiler-report)` — returns the sorted alist |
| `(profiling-reset)`  | Same as `(profiler-reset)` — clears accumulated data |

These are useful in scripts that set the profiling level via
`**eval-profiler**` directly and do not want to pull in the full module:

```scheme
(set! **eval-profiler** 2)          ; enable timing without importing the module

;; ... your code ...

(for-each (lambda (e) (display e) (newline))
          (profiling-report))

(profiling-reset)
```

The module-level names (`profiler-report`, `profiler-reset`) are thin wrappers
around the same C functions.

## Scheme binding `**eval-profiler**`

The special variable `**eval-profiler**` mirrors the current profiling level.
`profiler-start` and `profiler-stop` keep it in sync. You can also set it
directly:

```scheme
(set! **eval-profiler** 2)
```

This is equivalent to calling `(profiler-start 2)` except that it bypasses
the module and writes directly to the `set!` intercept in `eval.c`.

## Example — timing a workload

```scheme
(import (curry profiling))

(profiler-start 2)

(define (sum-to n)
  (let loop ((i 0) (acc 0))
    (if (= i n) acc (loop (+ i 1) (+ acc i)))))

(for-each sum-to '(100000 200000 300000))

(let ((report (profiler-report)))
  (for-each
    (lambda (entry)
      (let* ((name  (car entry))
             (calls (cadr entry))
             (ns    (cddr entry))
             (ms    (/ ns 1000000.0)))
        (display name) (display ": ")
        (display calls) (display " calls, ")
        (display ms) (display " ms")
        (newline)))
    report))

(profiler-stop)
```

## Example — live HUD in the solar system demo

`examples/solar-system-qt6.scm` embeds a live profiling overlay directly in
the simulation window.  The demo sets level 2 at startup:

```scheme
(set! **eval-profiler** 2)
```

Toggle the overlay with the **Profile HUD \[p\]** sidebar checkbox or by
pressing **`p`**.  While visible, it displays the top 12 hottest named
closures (by accumulated wall-clock time), colour-coded from yellow (hottest)
to grey (coolest), updated every animation frame.  The **Reset Profiler**
button calls `(profiling-reset)` to start a fresh measurement window without
restarting the simulation.

## Example — MCP server

`examples/profiling_mcp.scm` wraps the profiler as MCP tools so Claude Code
(or any MCP client) can start, stop, reset, and query the profiler
interactively during a live session.

```json
{
  "mcpServers": {
    "curry-profiler": {
      "command": "/path/to/build/curry",
      "args":    ["/path/to/examples/profiling_mcp.scm"]
    }
  }
}
```

Available MCP tools: `profiler-start`, `profiler-stop`, `profiler-reset`,
`profiler-report` (returns a formatted table).

## Implementation notes

- The profiling table is a fixed 4096-slot open-addressing hash map keyed on
  interned symbol `val_t` values. Capacity limit: 3072 entries (75% load).
- All table mutations are guarded by a single `pthread_mutex_t`. The hot-path
  check `if (curry_profiling_level)` is a single integer compare; the branch
  predictor predicts not-taken when profiling is off.
- Time is measured with `clock_gettime(CLOCK_MONOTONIC)`.
- The report is built by copying the live table under the lock, releasing the
  lock, sorting the snapshot, then building the Scheme alist — so report
  generation does not block ongoing profiling.
