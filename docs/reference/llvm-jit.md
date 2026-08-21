# LLVM JIT backend

When built with `-DBUILD_LLVM=ON`, Curry adds a tiered native-compilation layer on top of the bytecode VM.

| Procedure | Description |
|-----------|-------------|
| `(curry-llvm-available?)` | `#t` if the JIT backend is compiled in |
| `(jit-compile! proc)` | Force-compile a bytecode closure to native code immediately |
| `(jit-compiled? proc)` | `#t` if the closure has been JIT-compiled |

**Auto-JIT**: any `BcClosure` called ≥ 50 times is automatically compiled to native ARM64 or x86-64 on the next call. The compiled version replaces the bytecode interpreter transparently — no source changes needed.

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
