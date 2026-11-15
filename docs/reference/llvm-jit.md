# LLVM JIT backend

When built with `-DBUILD_LLVM=ON`, Curry adds a tiered native-compilation layer on top of the bytecode VM.

| Procedure | Description |
|-----------|-------------|
| `(curry-llvm-available?)` | `#t` if the JIT backend is compiled in |
| `(jit-compile! proc)` | Force-compile a bytecode closure to native code immediately |
| `(jit-compiled? proc)` | `#t` if `proc` has been compiled to native code (while the debugger or the profiler is active, calls still use bytecode) |
| `(jit-refusal-reason proc)` | A string saying why `proc` is not and will not be compiled; `#f` if it is compiled or has not been tried yet |

**Auto-JIT**: any `BcClosure` called ≥ 50 times is automatically compiled to native ARM64 or x86-64 on the next call. The compiled version replaces the bytecode interpreter transparently — no source changes needed.

## What is not compiled

The JIT refuses some procedures and leaves them on the bytecode VM. They
still run correctly, only slower. `(jit-refusal-reason proc)` gives the
reason. The current refusals:

- a closure that captures variables from an enclosing scope;
- a procedure defined inside a `define-library` (every SRFI and every
  `(curry X)` module);
- a procedure that uses a macro, or a special form the code generator does
  not implement (`quasiquote`, `let-values`, `guard`, `parameterize`,
  `case-lambda`, and others);
- a named let that calls itself outside tail position;
- a procedure whose source form was not saved with its bytecode.

A procedure is only tried after 50 calls, so a long loop inside a procedure
that is called once is not compiled either.

## Environment variables

| Variable | Effect |
|----------|--------|
| `CURRY_JIT=on` | Default: compile a closure after 50 calls |
| `CURRY_JIT=off` | Never compile; `jit-compile!` does nothing, and `curry-jit-call` / `curry-jit-eval` run on bytecode |
| `CURRY_JIT=force` | Compile on the first call. For testing: everything the JIT accepts runs as native code from the start |
| `CURRY_JIT_LOG=1` | At exit, print to stderr how many chunks were compiled and how many were refused, by reason |
| `CURRY_JIT_LOG=2` | Also print one line for each chunk when it is compiled or refused |

To check a program for JIT bugs, run it with `CURRY_JIT=off` and with
`CURRY_JIT=force` and compare the output. `ctest` does this for a set of
core suites (`jit_force_*`); `CURRY_JIT=force ctest --test-dir build` does it
for all of them.

## Speed

Typical speedups (Apple M-series, recursive and loop-heavy code):

| Benchmark | Bytecode | JIT | Speedup |
|-----------|----------|-----|---------|
| `(fib 25)` recursive | 54 ms | 9.2 ms | **5.9×** |
| `named-let` tight loop (10 000 iters) | 2.3 ms | 0.16 ms | **14×** |
| flonum arithmetic loop | 3.9 ms | 3.4 ms | 1.1× |

See [`../../examples/bench_jit.scm`](../../examples/bench_jit.scm) to run the comparison on your machine.

Build with JIT + Qt6 + all optional modules:

```bash
cmake -B build-release -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_LLVM=ON \
  -DBUILD_MODULE_QT6=ON \
  -DCMAKE_PREFIX_PATH="$(brew --prefix llvm);$(brew --prefix qt@6)"
cmake --build build-release -j$(sysctl -n hw.logicalcpu)
```

(`qt@6`'s prefix lacks `Qt6Config.cmake`; CMake falls back to `qtbase` automatically — see [`module-qt6.md`](module-qt6.md).)

## See also

- [`benchmarking.md`](benchmarking.md) — full benchmark suites and real-time Grafana monitoring
- [`gc.md`](gc.md) — the two GC backends this JIT layer runs alongside
