# Profiling

Curry has a built-in call profiler.  It instruments named closure calls and
(optionally) primitive calls as they execute, with no changes to your code.
The profiler is always compiled into the binary; the only overhead when it is
off is a single integer compare per call that the branch predictor will
predict not-taken.

---

## Quick start

```scheme
; In the REPL — enable, run something, inspect
(set! **eval-profiler** 1)
(fib 30)
,profile
(set! **eval-profiler** 0)
```

Or via the `(curry profiling)` module for programmatic control:

```scheme
(import (curry profiling))

(profiler-start 2)            ; level 2 = call counts + wall-clock timing
(my-expensive-function)
(profiler-stop)

(profiler-report/top 10)      ; top 10 hottest functions
```

---

## Profiling levels

The profiler has four levels, controlled by `**eval-profiler**` or
`(profiler-start level)`:

| Level | What is counted | Timing? |
|-------|----------------|---------|
| `0` | Off (default) | No |
| `1` | Named closure calls | No |
| `2` | Named closure calls | Yes — wall-clock ns via `clock_gettime` |
| `3` | Named closures + primitives | Yes |

Level 1 is very cheap — a hashtable increment per named call.  Level 2 adds
a `clock_gettime(CLOCK_MONOTONIC)` pair around each non-TCO call.  Level 3
also profiles primitive procedure calls (builtins like `+`, `cons`, `map`),
which adds noticeable overhead and produces very large reports.

**TCO calls are counted but not timed** at all levels.  A tail call that
jumps to `tail:` in the evaluator has no natural exit point, so timing is
structurally impossible without CPS transformation.  The call count is still
accurate.

---

## Scheme API

### `**eval-profiler**`

A special global variable (note the `**` sigils).  Set it with `set!` — the
evaluator intercepts writes and updates the C-level `curry_profiling_level`
immediately:

```scheme
(set! **eval-profiler** 1)   ; enable at level 1
(set! **eval-profiler** 0)   ; disable
```

This is the lowest-friction way to toggle profiling from the REPL.

---

### `(import (curry profiling))`

Imports the programmatic profiling API.  All procedures below require this
import.

---

### `(profiler-start [level])`

Enable the profiler at `level` (default 1).  Equivalent to
`(set! **eval-profiler** level)` but also available outside the REPL.

```scheme
(profiler-start)     ; level 1
(profiler-start 2)   ; level 2 — timing enabled
(profiler-start 3)   ; level 3 — primitives included
```

---

### `(profiler-stop)`

Disable the profiler.  Accumulated data is retained until `(profiler-reset)`.

---

### `(profiler-reset)`

Clear all accumulated call counts and timing data.  Call before a benchmark
run to isolate measurements.

---

### `(profiler-level)`

Return the current profiling level as a fixnum.

```scheme
(profiler-level)   ; ⇒ 0 | 1 | 2 | 3
```

---

### `(profiler-report)`

Return a complete call profile as an alist, sorted by call count descending:

```scheme
(profiler-report)
; ⇒ ((fib     . (1973167 . 842301400))
;     (tak     . (234256  . 0))
;     (my-fn   . (1024    . 12500000))
;     ...)
```

Each entry is `(name . (call-count . total-ns))`.  `total-ns` is 0 for
functions that were only called on TCO paths or when timing was off
(level 1).

---

### `(profiler-report/top n)`

Like `(profiler-report)` but returns only the top `n` entries by call count.
Pass `0` or omit to get all entries.

```scheme
(profiler-report/top 5)   ; five most-called functions
```

---

## REPL `,profile` command

Inside the REPL, `,profile` prints a formatted table without needing to
import anything:

```
> (set! **eval-profiler** 1)
> (fib 30)
832040
> ,profile
function                                     calls      ns-total
--------                                     -----      --------
fib                                        1973167             0
```

`ns-total` is `0` at level 1 (no timing).  Set level 2 first to see
wall-clock data:

```
> (set! **eval-profiler** 2)
> (fib 30)
832040
> ,profile
function                                     calls      ns-total
--------                                     -----      --------
fib                                        1973167     734201300
```

---

## Typical workflow

### Find where time goes in an unknown program

```scheme
(import (curry profiling))

(profiler-start 2)
(load "my-program.scm")
(profiler-stop)

; Print the ten hottest functions
(for-each
  (lambda (entry)
    (display (car entry)) (display " — ")
    (display (cadr entry)) (display " calls, ")
    (display (quotient (cddr entry) 1000000)) (display " ms")
    (newline))
  (profiler-report/top 10))
```

### Compare two implementations

```scheme
(import (curry profiling))

(define (bench thunk label)
  (profiler-reset)
  (profiler-start 2)
  (thunk)
  (profiler-stop)
  (display label) (newline)
  (for-each (lambda (e) (display e) (newline))
            (profiler-report/top 5)))

(bench (lambda () (fib-recursive 30)) "recursive:")
(bench (lambda () (fib-iterative 30)) "iterative:")
```

### Continuous profiling in a long run (torture test pattern)

```scheme
(import (curry profiling))

(define (run-with-profile thunk label iterations)
  (profiler-reset)
  (profiler-start 2)
  (let loop ((i iterations))
    (when (> i 0) (thunk) (loop (- i 1))))
  (profiler-stop)
  (display (string-append "── " label " ──\n"))
  (for-each
    (lambda (e)
      (let ((calls (cadr e))
            (ns    (cddr e)))
        (display (string-append
          (symbol->string (car e)) "\t"
          (number->string calls) " calls\t"
          (number->string (quotient ns 1000)) " µs\n"))))
    (profiler-report/top 20)))
```

This is the pattern used in `tests/bench_torture.scm`, which publishes the
profile report to Grafana via MQTT after each torture phase.

---

## What is and is not profiled

**Profiled at level 1+:**
- Named closures defined with `define` at any scope
- Named closures created with `let` + `define` (if the name is known at
  compile time)

**Not profiled:**
- Anonymous lambdas `(lambda (x) ...)` with no binding name
- C-level primitives (unless level 3)
- Tail calls via TCO — counted but not timed (see above)
- VM-compiled bytecode paths follow the same rules as the tree-walker

**Notes on ns-total accuracy:**
- Includes time spent in callees (not exclusive time)
- Recursive functions accumulate time for all frames, so `fib`'s ns-total
  covers the entire call tree
- Multithreaded programs accumulate wall-clock time across all threads
  independently (no locking on the profiling counters; minor overcounting
  is possible under heavy contention)
