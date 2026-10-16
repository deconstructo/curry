# Garbage Collector Reference

Curry uses the [Boehm–Demers–Weiser](https://www.hboehm.info/gc/) garbage
collector: conservative, non-moving, thread-safe, with parallel marking.

- **Non-moving:** an object stays at the same address for its whole life. A
  C pointer to a live object never goes stale, and `%object-id` is stable.
- **Conservative:** Boehm scans the C stacks, registers, global data and
  GC-allocated memory, and treats any word that looks like a pointer into
  the heap as one. C code can keep Scheme values in local variables, in
  GC-allocated structs and in globals without registering anything.
- **Stop-the-world:** each collection stops all registered threads while it
  marks. Pause length grows with the amount of live data (see
  [Pause times](#pause-times)).

`--gc boehm` is accepted on the command line for compatibility. The
experimental generational backend (`--gc generational`, `--gc-nursery-size`)
was removed: it moved objects that other threads could still see (issue
#226), depended on every C store going through the write barrier (#285,
#286), and measured no faster than Boehm. Passing either option now exits
with an error that says so.

---

## Scheme API

All procedures below are available without any import form.

### `(gc)`

Force a full collection (`GC_gcollect()`). Returns `#<void>`.

### `(gc-mode)`

Always returns the symbol `boehm`.

### `(gc-heap-size)`, `(gc-free-bytes)`, `(gc-total-bytes)`

Current heap size, estimated free bytes in the heap, and total bytes
allocated since the process started (fixnums).

### `(gc-stats)`

An alist of collector statistics. Times are in nanoseconds.

```scheme
(gc-stats)
; ⇒ ((heap-size        . 16769024)
;     (free-bytes       . 4374528)
;     (parallel-markers . 10)
;     (incremental      . #f)
;     (major-count      . 16)
;     (major-total-ns   . 120850000)
;     (major-max-ns     . 11809000)
;     (stw-count        . 16)
;     (stw-total-ns     . 110680000)
;     (stw-max-ns       . 10729000)
;     (stw-ring-ns      . #(612000 598000 ...)))
```

| Key | Meaning |
|---|---|
| `major-count`, `major-total-ns`, `major-max-ns` | Whole collections: count, total and longest duration. Boehm does not report these in incremental mode, so they stay 0 there. |
| `stw-count`, `stw-total-ns`, `stw-max-ns` | Stop-the-world intervals: the pauses every thread sees. In incremental mode one collection is split into several of them. |
| `stw-ring-ns` | The last 1024 stop-the-world durations, oldest first. Sort it for percentiles. |
| `parallel-markers` | Number of marker threads (Boehm uses one per core by default; set with the `GC_MARKERS` environment variable). |
| `incremental` | `#t` when Boehm's incremental mode is on. |

### `(gc-stats-reset!)`

Zero all counters and clear the ring. Call before a benchmark run.

### `(gc-enable-incremental!)`

Switch Boehm to incremental mode: a collection is done in several shorter
stop-the-world steps. See [Pause times](#pause-times) for when this helps
and when it hurts. The environment variable `GC_ENABLE_INCREMENTAL=1` does
the same from process start, and `GC_PAUSE_TIME_TARGET=<ms>` sets the target
step length (Boehm's default is 15 ms).

### `(gc-set-free-space-divisor! n)`

Boehm's free-space divisor (default 3). Higher values collect more often
and keep the heap smaller.

### `(gc-set-max-heap! bytes)`

Cap the heap at `bytes` bytes. `0` means no limit (default). Same as the
`--gc-max-heap N` command-line option (suffixes K/M/G).

---

## Pause times

Measured on an Apple M1 Max (10 cores), Boehm 8.2.12, median of 5 runs per
setting. "max pause" is the longest stop-the-world interval.

| Workload | default | `GC_ENABLE_INCREMENTAL=1 GC_PAUSE_TIME_TARGET=5` | `GC_MARKERS=1` |
|---|---|---|---|
| short-lived allocation, small heap | 1.3 ms max, 4.1 s | 1.6 ms max, 4.8 s | 1.3 ms max, 4.1 s |
| ~3M live pairs, 314 MB heap | **91 ms** max, 4.3 s | **5.1 ms** max, 4.3 s | 68 ms max, 4.3 s |
| old objects pointing to new ones, ~1M live pairs | 11.8 ms max, 1.5 s | 5.1 ms max, 1.7 s | 13.5 ms max, 1.5 s |
| 6 actors allocating | 0.8 ms max, **1.24 s** | 1.6 ms max, 1.94 s | 1.5 ms max, **0.59 s** |
| parallel `map` (work pool) | 1.1 ms max, **5.65 s** | 2.8 ms max, 9.30 s | 2.4 ms max, **2.28 s** |

What this shows:

- Pauses grow with the amount of live data, not with the allocation rate.
- Incremental mode with a 5 ms target cuts long pauses on large heaps by
  more than 10× at about the same total time, but makes programs with many
  threads 55–80% slower (Boehm's incremental mode tracks changed pages with
  memory protection).
- With many Scheme threads (actors, the work pool), one marker thread was
  2–2.5× faster than Boehm's default of one per core, even though each
  pause was longer.

These are measurements of one machine; check your own workload with
`(gc-stats)` before changing settings.

---

## C API

### Allocation

```c
void *gc_alloc(size_t n);                   /* may contain GC pointers   */
void *gc_alloc_atomic(size_t n);            /* pointer-free data         */
void *gc_alloc_pinned(size_t n);            /* same as gc_alloc today     */
void *gc_alloc_pinned_atomic(size_t n);
void *gc_alloc_raw_pinned(size_t n);        /* untyped helper arrays      */
void *gc_alloc_raw_pinned_atomic(size_t n);
```

The scanned variants return zeroed memory; the `_atomic` variants do not,
so initialize every byte. Use the `_atomic` variants for data that never
holds pointers (strings, numbers, byte buffers): Boehm does not scan it, so
it cannot keep other objects alive by accident. The `_pinned` variants mean
"must never move"; they are identical to the others today, but keep using
them for objects with mutexes, GMP data, `FILE *`, `jmp_buf`, or raw C
pointers held from outside the heap.

Macros: `CURRY_NEW(T)`, `CURRY_NEW_FLEX(T, n)`, `CURRY_NEW_ATOM(T)`,
`CURRY_NEW_PINNED(T)`, and their `_FLEX` / `_ATOM` combinations.

Memory from `malloc`, C++ `new`, and thread-local storage is **not scanned**.
A Scheme value stored only there can be collected while still in use.

### Root registration

```c
void gc_register_root(void *slot);
void gc_register_root_val(void *slot, val_t v);  /* store v, then register */
void gc_unregister_root(void *slot);
```

Registration keeps a copy of the slot's value **at the moment of
registration** alive, until `gc_unregister_root`. It does not follow later
changes to the slot. So:

- A slot in a global, a static array, a GC-allocated struct, or
  `GC_MALLOC_UNCOLLECTABLE` memory needs no registration: Boehm scans it.
- A slot in `malloc`/`new` memory that is set once and never changed:
  register it after storing the value (or use `gc_register_root_val`).
- A slot in `malloc`/`new` memory whose value changes: keep each new value
  reachable some other way (for example a list held in a global, as the Qt6
  module's `keep_alive` does), or move the slot into GC memory.

`gc_pin` / `gc_unpin` / `gc_val_pin` / `gc_val_unpin` are no-ops, kept for
modules that call them.

### Threads

Call `gc_register_thread()` at the start of every thread that touches
Scheme values and `gc_unregister_thread()` on its exit path. Threads
created with `pthread_create` in a file that includes `gc.h` go through
Boehm's `GC_pthread_create` redirect.

### Write barrier

```c
GC_WB(obj, field, val);                  /* obj->field = val */
gc_wb_slot(&slot, val);                  /* slot = val       */
gc_wb_slot_atomic_relaxed(&slot, val);   /* atomic store, for slots read
                                            without a lock by other threads */
```

Today these are plain stores. Keep using them for every store of a pointer
into an object that already exists: a concurrent collector needs a barrier
on exactly these stores (see [Future work](#future-work)).

### Blocking calls

```c
gc_thread_park();      /* immediately before a long or unbounded blocking call */
gc_thread_unpark();    /* immediately after it returns */
```

No-ops today. A collector that pauses threads at safe points would scan a
parked thread on its behalf instead of waiting for it. Never call
`gc_thread_unpark()` while holding a lock that another thread may need to
reach its own safe point (see `mailbox_push` in `src/actors.c`). C modules
use `curry_gc_thread_park()` / `curry_gc_thread_unpark()` from `curry.h`.

---

## Future work

The goal is collection without stopping all threads at once. The plan,
from the discussion that led to removing the generational backend:

1. **Write-barrier audit and checker.** Route every pointer store into an
   existing object through `GC_WB` / `gc_wb_slot`, and add a debug mode that
   finds stores that bypass it.
2. **Own non-moving collector, stop-the-world.** Segment-based allocator
   (BiBOP, see `docs/thoughts/performance-chez-kaappi.md` §3.2), per-thread
   allocation, conservative stack scanning, finalizers and weak links;
   replaces Boehm.
3. **Concurrent marking** with short start/end pauses that do not grow with
   the heap (as in Go).
4. **Per-thread handshakes instead of those pauses** (on-the-fly
   collection): no point at which all threads are stopped together.
5. Optionally, generational collection without moving objects (sticky mark
   bits).

Separately, the object header has an unused 8-byte `fwd` field
(`src/object.h`). Removing it would make every object 8 bytes smaller (a
pair would go from Boehm's 48-byte size class to 32).
