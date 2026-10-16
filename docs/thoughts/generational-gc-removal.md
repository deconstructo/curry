# Why the generational GC was removed (v1.27.0)

*Written 2026-10-16. Decision record for PR #290.*

In v1.27.0 curry has one garbage collector: Boehm GC, which is
conservative and never moves objects. The experimental generational
collector (`--gc generational`, `src/gc_gen.c`) was removed. This document
says what it was, what was wrong with it, what we measured, what we
considered instead, and where garbage collection goes from here.

## Summary

- The generational collector moved objects that other threads could still
  see, so programs using actors could read freed memory (#226). Every
  curry program has several threads (the parallel-`map` work pool starts
  at startup), so no program was safe from this.
- It only worked if every piece of C code followed a set of rules about
  holding Scheme values. When a rule was missed, the program silently
  read the wrong data. Between early September and mid-October 2026,
  fourteen issues of this kind were filed and fixed one by one, and more
  kept appearing (#285, #286).
- It was not faster than Boehm in the benchmarks we had, and in one test
  it used about 9 times as much memory.
- Making it correct would have meant redesigning it, not fixing it. The
  better design for curry's goal (no pause of all threads at once) keeps
  objects in place, so we removed it and kept Boehm.

## What the generational collector was

It first shipped in v1.5.0 as an opt-in experiment, and was refined
through many releases after that. The design:

- Each thread had a small **nursery**: a block of memory where new
  objects were allocated by bumping a pointer, which is very fast.
- When a nursery was full, a **minor collection** copied the objects that
  were still in use into the old generation (managed by Boehm) and
  reused the nursery.
- Because objects moved, the collector had to find and update **every
  pointer** to a moved object. It found them through:
  - the collecting thread's own VM stack and a "shadow stack" of C
    variables registered with `GC_AUTOFRAME`;
  - a "write barrier": every store of a new object into an old one had to
    go through `gc_wb_slot` or `GC_WB`, which recorded the location;
  - a rule that C code must not run a collection while it holds Scheme
    values in plain C variables (`gc_inhibit_minor()` /
    `gc_resume_minor()`, about 90 places in the code).

## What was wrong

### 1. Objects moved while other threads still used them

A minor collection ran on one thread. It copied objects (including
objects in other threads' nurseries) and then updated only the pointers
that thread knew about. Other threads that held the same objects kept the
old addresses. That memory was then reused for new objects, so those
threads read unrelated data.

Objects reach another thread in many ordinary ways: a message sent to an
actor, a global variable, a vector both threads use, a closure passed to
`spawn`, or a parallel `map`. We confirmed the failure both ways:

| Test | Result with the generational collector |
|---|---|
| Actors sending to actors (#226 repro), 4K nursery | 9–11 of 15 runs crashed or printed corrupt data |
| Main thread sending to 4 actors | 5 of 5 runs gave wrong data |
| Same, with only the main thread having a nursery | still 5 of 5 wrong |
| Boehm only | 0 failures |

The third line matters: limiting nurseries to one thread did not help,
because objects still move under other threads that hold them. And since
the work-pool threads exist from startup, there is no "single-threaded
program" case where it would have been safe.

Fixing this properly requires one of:

- stopping **all** threads for every minor collection and updating every
  thread's pointers, including pointers held in C variables of primitives
  that are running at that moment (which needs a new technique,
  "mostly-copying" collection, to handle those safely); or
- giving each thread a private heap and copying an object out before it
  can be shared, which needs a check on every store into shared data
  (curry lets actors share mutable data: globals, vectors, TVars).

Both are redesigns, not fixes.

### 2. Correctness depended on rules spread over the whole codebase

A moving collector must know about every pointer to every object it
moves. In curry it relied on conventions that every C function holding
Scheme values had to follow, in about 69,000 lines of C. When one place
missed a rule, nothing failed at that place: a later collection silently
left a pointer to the old copy.

The issue history shows the pattern. These were all found and fixed one
at a time: #144, #146, #150, #198, #200, #203, #205, #208, #213, #215,
#217, #220, #222, #223. Then, during this investigation:

- **#285**: the write-barrier record had a fixed size (4,096 entries).
  When it filled up, the collector dropped every recorded location, so a
  plain single-threaded loop of `vector-set!` corrupted a global variable.
  Fixed in #287, before we decided to remove the collector.
- **#286**: three ordinary test suites (sexagesimal, syntax_rules,
  numeric_ext) failed on one thread, probably because some macro-expander
  code stored values without the write barrier.

### 3. No measured benefit

| Measurement | Boehm | Generational |
|---|---|---|
| `fib(28)` (earlier benchmarks) | baseline | 6% slower |
| `count-down(1M)` (earlier benchmarks) | baseline | 9% slower |
| Building 200 short-lived 100,000-element lists, then a 2M-element list | 110 MB peak | about 1 GB peak |

Boehm's allocation is already thread-local and fast, so the nursery's
main advantage did not show up in curry's workloads.

## What we considered

1. **Keep only Boehm and improve it.** Low risk; improvements help every
   user. Pauses still stop all threads.
2. **Redesign: stop all threads for every minor collection** (the OCaml 5
   and GHC approach), with "mostly-copying" handling of C variables.
   Correct, but weeks of work, and every minor collection pauses every
   thread.
3. **Per-thread heaps, copy an object out before it is shared.** Needs a
   check on every store into shared data; very invasive in curry.

We chose 1. The goal for curry's garbage collection is **no pause of all
threads at once**. A collector that moves objects can only do that with a
check on every pointer read, which would touch nearly all of curry's C
code. A collector that never moves objects can do it with per-thread
handshakes, which is a much better fit. So the generational design was
not a step towards the goal either.

## What changed in v1.27.0

Removed:

- `src/gc_gen.c` and the two older unused backends (`gc_generational.c`,
  `gc_semispace.c`, never compiled).
- The rules that only a moving collector needed: `GC_AUTOFRAME` and the
  shadow stack, `gc_inhibit_minor`/`gc_resume_minor`, the "ext scanner"
  callbacks, and the calls the LLVM JIT emitted around every call.
- The minor-collection and safe-point checks between every bytecode
  instruction.

Kept, because a future non-moving concurrent collector needs them:

- The write barrier (`GC_WB`, `gc_wb_slot`, `gc_wb_slot_atomic_relaxed`).
  Today these are plain stores. Keep using them for every store of a
  pointer into an existing object.
- `gc_thread_park()` / `gc_thread_unpark()` (and `curry_gc_thread_park()`
  / `curry_gc_thread_unpark()` for C modules) around blocking calls.

Visible to users:

- `--gc generational` and `--gc-nursery-size` now exit with an error that
  says the backend was removed. `--gc boehm` still works.
- `(gc-mode)` always returns `boehm`.
- `(gc-stats)` no longer has `minor-count`, `minor-total-us`,
  `minor-max-us`, `pause-ring-us` or `nursery-used`. It now reports
  Boehm's stop-the-world pauses: `stw-count`, `stw-total-ns`,
  `stw-max-ns`, `stw-ring-ns` (last 1024 pauses), plus `major-total-ns`,
  `major-max-ns`, `incremental` and `parallel-markers`.

For C code and modules:

- Objects never move. C code may keep Scheme values in local variables,
  in GC-allocated structs and in globals without registering them.
- Memory from `malloc`, C++ `new`, and thread-local variables is still not
  scanned by Boehm; see `docs/reference/gc.md` for `gc_register_root`.

The removal took out about 6,500 lines and added about 700 (most of the
additions are documentation). `src/gc.c` went from 500 to 213 lines and
`src/gc.h` from 679 to 190.

## Pause times with Boehm

Measured on an Apple M1 Max (10 cores), Boehm 8.2.12, median of 5 runs.
"Max pause" is the longest time all threads were stopped.

| Workload | default | `GC_ENABLE_INCREMENTAL=1 GC_PAUSE_TIME_TARGET=5` | `GC_MARKERS=1` |
|---|---|---|---|
| short-lived allocation, small heap | 1.3 ms max, 4.1 s | 1.6 ms, 4.8 s | 1.3 ms, 4.1 s |
| ~3M live pairs, 314 MB heap | 91 ms max, 4.3 s | 5.1 ms, 4.3 s | 68 ms, 4.3 s |
| old objects pointing to new ones | 11.8 ms max, 1.5 s | 5.1 ms, 1.7 s | 13.5 ms, 1.5 s |
| 6 actors allocating | 0.8 ms max, 1.24 s | 1.6 ms, 1.94 s | 1.5 ms, 0.59 s |
| parallel `map` | 1.1 ms max, 5.65 s | 2.8 ms, 9.30 s | 2.4 ms, 2.28 s |

- Pauses grow with the amount of live data.
- Incremental mode with a 5 ms target cuts long pauses on large heaps by
  more than 10×, but makes programs with many threads 55–80% slower.
- With many busy threads, one marker thread was 2–2.5× faster than
  Boehm's default of one per core.

Nothing was changed by default; these settings are environment variables
Boehm reads at startup. See `docs/reference/gc.md`.

## Where garbage collection goes from here

The goal is collection without stopping all threads at once. The plan,
each step useful on its own:

1. Remove the generational collector. **Done in v1.27.0.**
2. Audit the write barrier: route every pointer store into an existing
   object through `GC_WB`/`gc_wb_slot`, and add a debug mode that finds
   stores that bypass it.
3. Write our own non-moving collector, still stopping all threads:
   memory divided into segments of one object size each (BiBOP, see
   `docs/thoughts/performance-chez-kaappi.md` §3.2), per-thread
   allocation, conservative stack scanning, finalizers and weak links.
   It replaces Boehm.
4. Mark while threads keep running, with short pauses only at the start
   and end of each collection (pauses that do not grow with the heap, as
   in Go's collector).
5. Replace those pauses with per-thread handshakes, so that all threads
   are never stopped together.

Steps 2–5 have not started.

## References

- Issues: #226 (cross-thread movement), #285 (write-barrier overflow),
  #286 (single-threaded failures), and the earlier list in section 2.
- PRs: #287 (fix for #285), #290 (removal).
- `docs/reference/gc.md` — the collector as it is now.
- `docs/gc-moving-design.md` — the original design, kept as history.
