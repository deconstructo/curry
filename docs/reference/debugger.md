# Interactive Debugger

Curry ships a gdb-style interactive debugger for bytecode-compiled code:
breakpoints, single-stepping, backtraces, and live variable inspection,
driven from the terminal.

## Setting breakpoints

Three ways to arm the debugger:

**From the REPL** — comma commands:

```
> ,break fib              ; stop whenever fib is entered
> ,break myfile.scm:12    ; stop at the first instruction of that line
> ,breaks                 ; list breakpoints with their indices
> ,unbreak 0              ; remove breakpoint #0
> ,debug (fib 10)         ; run an expression under single-step from
                          ; its first instruction
```

**From the command line** — the `-b` flag (repeatable), useful for scripts:

```
$ curry -b fib -b utils.scm:33 script.scm
```

**From source** — the `(breakpoint)` procedure stops at the next VM
instruction after it returns, like JavaScript's `debugger;`:

```scheme
(define (solve grid)
  (breakpoint)          ; drop into the debugger here
  ...)
```

File:line breakpoints match either the full path or just the basename, so
`,break sexagesimal.scm:40` works regardless of how the file was invoked.
Function-name breakpoints fire at frame entry — for a recursive function
that means every call, including recursive ones.

## The debug prompt

When a breakpoint or step boundary hits, curry prints the location and
source line, then reads commands from stdin:

```
fib.scm:2: in fib
     2 |   (if (< n 2) n
dbg> locals
  n = 5
dbg> bt
  #0  fib                      fib.scm:2
  #1  <toplevel>               fib.scm:4
dbg>
```

| Command | Effect |
|---|---|
| `s`, `step` | stop at the next source line, entering calls |
| `n`, `next` | stop at the next source line, stepping over calls |
| `f`, `finish` | run until the current function returns |
| `c`, `continue` | run until the next breakpoint |
| `bt` | backtrace of the paused call stack |
| `locals` | live local variables of the current frame, by name, including captured (upvalue) variables |
| `p <expr>` | print a live local/captured variable by name, or evaluate any expression in the global environment |
| `break <spec>` | add a breakpoint (function name or `file:line`) |
| `unbreak <n>` | remove breakpoint #n |
| `breaks` | list breakpoints |
| `h`, `help` | command summary |
| `q`, `quit` | abort the paused evaluation and return to the REPL (or exit a script with a `debugger: quit` error) |

An empty line repeats the previous command (gdb convention). EOF on stdin
disarms the debugger and lets the program run to completion — piped input
that runs out of commands never wedges a script.

Errors raised by a `p` expression are caught, printed, and the paused
program's VM state is fully restored — a typo at the prompt cannot corrupt
the program being debugged.

## Stepping semantics

A "step boundary" is a change of source line or entry into a frame. Frame
entry counts so that single-line loops — named-`let` loops and self tail
calls whose whole body sits on one line — still stop once per iteration.
`next` only stops at boundaries at the same or shallower call depth;
`finish` stops as soon as the call depth drops below the paused frame's.

## How it works, and what it costs

The VM checks one global flag per instruction dispatch, at the same
safepoint as the minor-GC check — a predicted-not-taken branch when the
debugger is inactive, nothing more. There is no separate debug build.

While any breakpoint is set or a stepping mode is armed:

- The tiered JIT is bypassed (JIT-compiled code doesn't pass through the
  dispatch loop, so breakpoints couldn't fire in it). Closures already
  promoted to native code before the debugger was armed also stop being
  dispatched natively.
- Compiled `.scc` files (format v3+) carry the full debug metadata —
  local-variable names, scope ranges, upvalue names — so a script running
  from its transparent bytecode cache debugs identically to a fresh
  compile. Pre-v3 caches are invalidated automatically.

## Limitations

- **Tree-walker code is invisible.** Code executed by the tree-walking
  evaluator — `(load ...)`ed files, `tree-eval` passthrough forms —
  never enters the VM dispatch loop, so breakpoints don't fire there.
  Run files as scripts (`curry file.scm`) or paste definitions into the
  REPL to debug them.
- **Main thread only.** Actor threads never stop at breakpoints; a
  breakpoint hit inside an actor is ignored rather than contending with
  the REPL for stdin.
- `p <expr>` evaluates in the global environment; only bare names of
  live locals and captured variables resolve to frame values. Compound
  expressions cannot reference locals yet.
- No watchpoints or conditional breakpoints yet.
