# Concurrency uplift — plan

*Drafted 2026-08-31. Status: planning only, nothing implemented yet.*

## Why

Compared against `cill_spec.pdf` §6 (Concurrency & Parallelism) after
reading it and finding it "nice and clean." The comparison turned out
better for curry than the profiling/set-theory ones did: curry's
core concurrency primitives are already essentially at parity with Cill's
*design*, just spread across fewer, more consolidated abstractions and with
real gaps only at the edges (a few missing sync primitives, no general
future/task API, and the one genuinely large item — green threads — already
tracked in `docs/roadmap.md` Phase 6 as blocked on an unresolved design
question, not merely unstarted work).

## Current state — verified against source, not memory

| Cill §6 concept | curry equivalent | Status |
|---|---|---|
| Actors (stateful concurrent entities) | `spawn`/`send!`/`receive`/`actor-alive?`/`actor-stats` (`src/actors.c`) | ✓ complete — real detached pthreads, mutex+condvar+ring-buffer mailbox |
| STM (shared mutable state) | `make-tvar`/`tvar-read`/`tvar-write!`/`atomically`/`retry`/`%or-else` (`src/stm.c`) | ✓ complete |
| Channels (CSP, data pipelines) | `make-channel`/`channel-send!`/`channel-recv!`/`channel-close!`/`%channel-try-send`/`%channel-try-recv`/`%channel-blocked?` (`src/channel.c`) | ✓ complete |
| Parallel compute (OS thread pool) | `map`/`reduce` (auto-parallel above `map_par_threshold`), `for-each/par`, `hardware-concurrency` (`src/workpool.c`, Chase-Lev work-stealing) | ✓ complete for map/reduce-shaped work |
| Primitives: mutex, condvar, semaphore | `make-mutex`/`with-mutex`/`mutex-lock!`/`mutex-trylock!`, `make-condvar`/`cond-wait!`/`cond-wait-timeout!`, `make-semaphore`/`sem-wait!`/`sem-post!` (`modules/sync/sync.c`) | ✓ complete |
| Primitives: rwlock, barrier, once | — | ✗ missing |
| Futures (`pool-submit`/`future-get`/`future-then`) as a general task API | — | ✗ missing (only the map/reduce-shaped parallel primitives exist; no way to submit an arbitrary one-off thunk to the pool and get a handle back) |
| Green threads (M:N lightweight concurrency) | — | ✗ not started — `docs/roadmap.md` Phase 6, explicitly blocked (see below) |
| Pluggable scheduler | — | ✗ not started, depends on green threads |
| Hot code reloading | — | ✗ not started, depends on green threads (per roadmap: "requires green threads for the actor protocol") |
| `docs/roadmap.md`'s own summary line | "Actors, STM, CSP channels \| ✓ complete" | matches what's actually in the source |

`(curry sync)`'s `with-mutex`/`make-condvar`/`make-semaphore` are already in
real production use in this codebase, not just registered and untested —
`ros.scm` and `websocket.scm` both build their own higher-level primitives
(a `%with-lock` wrapper, a request/response wait-with-timeout pattern) on
top of them.

## The two small gaps

**1. `rwlock`, `barrier`, `once`.** Cill's spec lists `make-rwlock` (with
`with-read-lock`/`with-write-lock`), `make-barrier`/`barrier-wait`, and
`make-once`/`call-once` as primitives alongside mutex/condvar/semaphore.
curry has none of the three. These are small, well-understood additions to
`modules/sync/sync.c` (`pthread_rwlock_t`, `pthread_barrier_t` — note:
`pthread_barrier_t` is **not available on macOS** libc, unlike Linux glibc;
needs a userspace implementation via mutex+condvar+count, same shape as the
existing semaphore code in the same file — and `once` is a one-shot flag
guarded by a mutex, or `pthread_once_t` where the callback signature fits).
No design risk; this is direct porting work.

**2. A general future/task API.** curry's parallelism today is
map/reduce/for-each-shaped: you get parallelism by calling one of those
three primitives over a collection. Cill additionally exposes
`pool-submit`/`future-get`/`future-then` (§6.7) — submit one arbitrary
thunk, get a handle back, block or attach a non-blocking continuation later.
This is a genuinely useful shape curry doesn't have: "run this one
computation on the pool while I do something else, then join" doesn't map
cleanly onto `map`/`reduce` when there's only one task, or when the tasks
aren't homogeneous. Should build directly on the existing `src/workpool.c`
work-stealing deque (submission is presumably already close to what
`map`'s internals do per-chunk; a future is the same submission with a
result cell and a condvar instead of a chunk-of-work-then-collect loop).
*Estimate for both together: 1–1.5 weeks.*

## The one large gap: green threads

This is not simply unstarted — `docs/roadmap.md` Phase 6 already documents
a real blocker: the continuation strategy green threads would depend on
(`(yield)`, coroutine frames as GC roots, LLVM coroutine intrinsics per
Cill's own sketch) has never been reconciled with the hybrid `call/cc`
decision already made in `docs/thoughts/performance-chez-kaappi.md` §4.4
(VM frame-stack copying for multi-shot capture, native + `setjmp` for the
escape-only case — explicitly **not** CPS). LLVM coroutines and that
continuation strategy are not obviously the same underlying mechanism, and
nobody has sat down and checked whether they compose or conflict.

This plan does not attempt to resolve that question — it's a real design
decision, not an implementation task, and belongs to whoever picks up
green threads specifically, informed by both documents together. What this
plan does recommend: **don't start implementing green threads without
first writing a short reconciliation note** answering "does the
`call/cc` strategy already committed to support, block, or need
modification for LLVM-coroutine-based green thread frames" — one page,
before any code. Cill's own fiber design (see the earlier Kaappi
comparison in this conversation — a different but related project) is
worth a second look at this point too: Kaappi's fiber model had its own
unresolved edge (the "driving in place" fallback for reentrant native
frames) that's directly relevant to whatever curry's green-thread scheduler
would need to do when a green thread calls into C/FFI code that can't be
suspended generically — the exact same class of problem.

**Recommendation:** treat green threads as out of scope for *this*
uplift. It's real, roadmapped work (Phase 6, still estimated at 5–7 months
combined with hot reload) with a genuine open design question at its root,
not a "port from Cill" task like the rest of this document. Ship the two
small primitive gaps and the future API first — they're immediately useful
and have zero design risk — and revisit green threads as its own
initiative once the continuation-strategy reconciliation note exists.

## Phased plan

### Phase 1 — `rwlock`, `barrier`, `once` in `modules/sync/sync.c`
Direct port, following the existing mutex/condvar/semaphore code's
tag-and-opaque-pointer pattern in the same file. macOS needs a userspace
barrier (no `pthread_barrier_t` in Apple's libc).
*Estimate: 3–5 days.*

### Phase 2 — Future/task API on top of `src/workpool.c`
`pool-submit`, `future-get`, `future-then`, backed by the existing
Chase-Lev work-stealing deque rather than a new pool implementation.
*Estimate: 1 week.*

### Phase 3 (separate initiative, not part of this uplift) — Green threads reconciliation note
One page: does the `call/cc` hybrid strategy in
`performance-chez-kaappi.md` §4.4 support LLVM-coroutine-based green
thread frames, need modification, or rule them out in favor of a different
mechanism? Answer this before any green-thread implementation work starts.
*Estimate: not scoped here — belongs to whoever picks up Phase 6.*

## Non-goals

- **Green threads, pluggable scheduler, hot code reload implementation** —
  real work, already roadmapped, blocked on a design decision this plan
  doesn't attempt to make. See Phase 3 above.
- **STM conflict-rate counters** — belongs to the concurrency profiler
  (see `docs/thoughts/profiling-uplift-plan.md` Phase 5, which should be
  revised to include STM now that this review found STM already exists in
  curry — the profiling plan's Phase 5 note "no STM yet" was written from
  memory rather than checked against source and was wrong; see that
  document's own correction).
