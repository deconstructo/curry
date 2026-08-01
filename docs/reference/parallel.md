# Parallel map and reduce

`map` and `reduce` automatically parallelise over multiple CPU cores; sequential variants are always available.

| Procedure | Description |
|-----------|-------------|
| `(map f lst)` | Parallel when list exceeds threshold (default: 8 elements) |
| `(map/seq f lst ...)` | Always sequential; full multi-list R7RS `map` |
| `(reduce f identity lst)` | Parallel associative fold |
| `(reduce/seq f identity lst)` | Always sequential |
| `(for-each/par f lst)` | Parallel for-each — opt-in, order unspecified |
| `(map-parallel-threshold)` | Return current threshold |
| `(set-map-parallel-threshold! n)` | Set threshold |
| `(hardware-concurrency)` | Logical CPU count (= pool thread count) |

Backed by a **persistent work-stealing thread pool** (Chase-Lev deques, 4× oversubscription). No per-call thread-spawn overhead — the pool is created once at startup and lives for the process lifetime. Exceptions in worker threads propagate to the caller. `reduce` requires a true identity element; it is not used as a per-thread seed. `for-each` remains sequential; `for-each/par` is the explicit opt-in for independent side effects.
