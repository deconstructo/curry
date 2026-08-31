# Profiling uplift — plan

*Drafted 2026-08-31. Status: planning only, nothing implemented yet.*

## Why

`(curry profiling)` today is a single mechanism: a global level (0–3) checked
in `eval()`'s hot path, accumulating call counts and (at level ≥2) wall-clock
time per named closure into a fixed hash table (`src/profiling.c`). It answers
"which named function is slow," and only for code that goes through `eval()`
with a level explicitly set beforehand. It cannot answer:

- What is the whole process doing right now, without having instrumented it
  in advance? (no sampling profiler)
- Is this loop running tree-walked, VM-compiled, or JIT-native — and how much
  did each tier cost? (no tier attribution at all)
- Where are allocations coming from, and which ones survive? (no allocation
  profiler; Boehm GC is opaque from Scheme beyond `,gc` forcing a collection)
- How deep are actor mailboxes running under load, and is a specific actor a
  bottleneck? (no concurrency profiler)
- What happened, causally, across threads/actors in the last 10 seconds? (no
  event tracing)

This was written up after comparing against `cill_spec.pdf`, a design
document for a from-scratch Scheme-family sibling project (Cill) that was
prototyped and abandoned (code quality was poor) but whose design doc —
including its profiling section (§12) — was kept because it was clean. Cill's
profiler splits into three simultaneously-active layers (sampling,
zero-cost-when-disabled instrumentation, structured event tracing), plus a
shared GC-stats struct, an allocation profiler compatible with existing
heaptrack/massif visualizers, a concurrency profiler for actors/STM/thread
pool, and an LLVM PGO workflow. The shape is worth adopting. The mechanism
is not worth copying blind — see "Risks in Cill's design" below, both of
which are load-bearing for curry specifically in ways they weren't for Cill.

## Current state (keep, don't replace)

- `profiler-start`/`-stop`/`-reset`/`-level`/`-report`, `**eval-profiler**`,
  `profiling-report`/`profiling-reset` builtins — level 1–3 call-count/timing
  via a single integer-compare hot-path check. Cheap, works, has a live demo
  (`examples/solar-system-qt6.scm`'s HUD) and an MCP wrapper
  (`examples/profiling_mcp.scm`). This stays as the "level 2/3 instrumentation"
  layer below, essentially unchanged — it already does what Cill calls
  "instrumentation — counters and timers," just scoped to named closures
  rather than arbitrary code regions.
- `--timings` CLI flag — read/expand/compile/execute pipeline breakdown +
  cache HIT/MISS, printed once to stderr on exit. Useful, but currently
  disconnected from the profiling module's data model (see Phase 4).
- `,gc` REPL command, `(curry profiling)` module — no GC-stats introspection
  beyond forcing a collection.

## Risks in Cill's design, and how curry should differ

Both of these are gaps in the spec, not the implementation quality problem
Cill actually had — they're the parts a clean-looking design doc can gloss
over with one line each.

**1. Signal-based sampling across real OS threads.** Cill's spec says
"installs a SIGPROF handler; samples the call stack" — fine for a
single-threaded target, but curry's actors (`src/actors.c`) are real
detached pthreads, not green threads under one scheduler. A process-wide
`setitimer(ITIMER_PROF, ...)` only delivers `SIGPROF` to *one* thread at a
time (implementation-defined which). Getting a representative sample across
N actor threads needs either:
  - `timer_create` + `SIGEV_THREAD_ID` per registered pthread (Linux-only,
    no macOS equivalent — `SIGEV_THREAD_ID` is a Linux extension), or
  - a round-robin scheme where each active thread is signalled in turn from
    a dedicated sampler thread (`pthread_kill(tid, SIGPROF)`), portable but
    lower effective sampling rate per thread as thread count grows, or
  - per-thread opt-in: an actor registers itself with the sampler
    (mirrors how `gc_register_thread()` already works), and the sampler
    round-robins only over registered threads.

  The third option reuses an existing pattern in the codebase
  (`gc_register_thread()` at pthread start) and is the one to prototype
  first — bounded, portable, and it degrades gracefully (unregistered
  threads just don't get sampled, rather than corrupting state).

**2. Frame-pointer-based unwinding through FFI.** Cill's spec requires
`-fno-omit-frame-pointer`-equivalent everywhere and stops there. curry links
against GMP, MPFR, libgit2, Qt6, PLplot, OpenSSL, none of which you control
the build flags for. A sample landing inside any of those calls unwinds into
garbage if the walker assumes frame pointers hold everywhere. Options:
  - Frame-pointer walk for curry's own compiled/interpreted frames, with a
    conservative "stop at first foreign-looking frame" fallback (attribute
    the sample to the FFI call site instead of misattributing frames beneath
    it) — cheapest, matches what most embedded-VM profilers actually do.
  - `libunwind`-based DWARF unwinding — correct through foreign frames, but
    slower per-sample and (if `BUILD_LLVM=OFF` or on a from-scratch
    platform) an extra dependency to add to an already-long optional-module
    list.

  Recommendation: frame-pointer walk with the foreign-frame fallback for v1.
  It costs nothing new to build (frame pointers are already required for the
  interactive debugger's `bt`, per `docs/reference/debugger.md`) and the
  failure mode (a sample attributed to "inside libblas" instead of a precise
  foreign line) is acceptable for a first cut.

## curry-specific additions not in Cill's spec at all

Cill compiles uniformly through LLVM — there's no tree-walker/VM split to
profile. curry has exactly that split, and it's where the known, open
performance defect lives.

**Tier attribution.** curry code executes via three different paths:
tree-walked `eval()`, compiled VM bytecode, and JIT-compiled native (LLVM
backend, tiered). The confirmed defect ("`define-library` hot loops ~3-6x
slower," tracked against the eval-elimination migration) is entirely a
question of which tier a given call took and for how long. No current tool
can answer "did this hot function ever get JIT-promoted" or "what fraction
of this run's wall-clock time was tree-walked vs VM vs native." This is the
single highest-value addition here — it's not a nice-to-have parallel to
Cill's design, it's the tool the eval-elimination work is currently missing.
Concretely: tag each profiler-table entry (or trace event) with the tier the
call executed under, sourced from whatever dispatch already distinguishes
these paths internally (`eval()`'s own call path vs `vm.c`'s dispatch vs the
LLVM JIT tier-promotion bookkeeping already used by `--timings`).

**`.scc` cache visibility folded into the profiling model.** `--timings`
already computes read/expand/compile/execute + cache HIT/MISS but only ever
prints it once, to stderr, on process exit. Exposing it as
`(profiling-report)` data (or a sibling procedure) means a script's
cold-start cost and steady-state execution cost show up in one place instead
of two disconnected mechanisms.

**Correction (2026-08-31): STM already exists.** An earlier draft of this
plan claimed curry had no STM yet — that was written from memory, not
checked against source, and was wrong. `src/stm.c` implements
`make-tvar`/`tvar-read`/`tvar-write!`/`atomically`/`retry`/`%or-else` and
`docs/roadmap.md` already lists "Actors, STM, CSP channels" as complete
(confirmed while writing `docs/thoughts/concurrency-uplift-plan.md`). The
concurrency-profiler phase (5) below should therefore include STM
conflict/retry counters alongside actor mailbox depth and thread-pool
utilization, matching Cill's original scope — not a reduced, actor-only
version. `tvar-info`'s conflict-count field specifically will need new
bookkeeping: `src/stm.c` currently tracks `n_waiters` per tvar but not a
cumulative conflict count, so that counter doesn't fall out for free.

## Phased plan

Ordered by value/cost, not by Cill's own section numbering. Each phase should
get its own PR, fresh branch off `origin/main`, independent code+security
review, full `ctest --clear-cache` run, per the usual project workflow.

### Phase 1 — GC stats surface (cheapest, unblocks everything else)
Expose a stats struct from the GC layer: heap used/capacity, collection
counts, total allocated bytes. Boehm exposes most of this already
(`GC_get_heap_size`, `GC_get_free_bytes`, `GC_get_total_bytes`,
`GC_gc_no`) — this is mostly plumbing, not new instrumentation.
`(gc-stats)` → alist. Also add a `(gc-on-collection thunk)` hook (Cill §12.4)
so other profiling layers (esp. tracing, phase 3) can emit a GC event without
polling.
*Estimate: 2–4 days.*

### Phase 2 — Sampling profiler
SIGPROF/`setitimer`-based, with the per-thread-registration approach from
"Risks" above (reuse the `gc_register_thread()` pattern — a
`profiler_register_thread()` call at actor/pthread start). Frame-pointer walk
with foreign-frame fallback. Output: flat text (top-N) and flamegraph SVG,
matching Cill's `profiler-report 'flamegraph "out.svg"` shape since it's a
well-understood, tool-agnostic format (no new visualizer to build).
*Estimate: 1.5–2 weeks — the riskiest phase; the cross-thread signal delivery
and stack-walking-through-FFI pieces are genuinely new engineering, not
plumbing.*

### Phase 3 — Tier attribution
Tag existing profiler-table entries / new trace events with which
execution tier (tree-walk / VM / JIT-native) handled each call. Feeds
directly into the eval-elimination migration's own tracking. Depends on
nothing above; can be done in parallel with Phase 2 if useful, but sequenced
after GC stats since both phases 1 and 3 touch `profiler-report`'s alist
shape and should agree on a schema before Phase 2 adds a second report
format (flamegraph) on top.
*Estimate: 1–2 weeks, mostly in `compiler.c`/`vm.c`/`eval.c` call sites plus
whatever the LLVM tier-promotion bookkeeping already tracks for `--timings`.*

### Phase 4 — `.scc`/`--timings` unification
Fold `--timings`'s read/expand/compile/execute/cache-HIT-or-MISS breakdown
into the profiling module's data model instead of a separate stderr-only
report.
*Estimate: 2–3 days.*

### Phase 5 — Concurrency profiler (actors + STM + thread pool)
Actor mailbox depth (already exposed per-actor via `actor-stats`'s
`mailbox-depth` field — mostly a "list all actors and aggregate" pass, not
new instrumentation), message rate, thread-pool (`src/workpool.c`)
utilization (active/idle/queue depth — the Chase-Lev work-stealing deque
likely already has enough bookkeeping to expose this cheaply), and STM
conflict/retry rate (needs new cumulative-counter bookkeeping in
`src/stm.c`, which currently tracks only live `n_waiters`, not conflict
history — see the correction above).
*Estimate: 1.5–2 weeks (revised up slightly to include STM counters).*

### Phase 6 — Allocation profiler
Track allocation call sites (source location, type, count, bytes,
survivors). Harder with Boehm's conservative collector than with a precise
moving GC (no natural per-object "site" tag) — likely needs a thin wrapper
macro around `GC_NEW`/`GC_NEW_ATOM`/etc. that captures `__FILE__`/`__LINE__`
into a side table, active only when this profiler is enabled (so the normal
allocation macros stay untouched otherwise). Output compatible with
heaptrack/massif per Cill's approach, so an existing visualizer can be reused
instead of building one.
*Estimate: 2–3 weeks — the GC-conservativeness mismatch makes this the
second-riskiest phase after sampling.*

### Phase 7 — Event tracing
Structured timeline (name, timestamp, thread/actor id, key-value tags),
exportable to Perfetto. Recommend the Chrome/Perfetto JSON trace format over
building a Tracy (`tracy_client`) integration first — it's a plain JSON
array, no new C dependency, and Perfetto's UI reads it natively; a
Tracy-specific real-time integration can be a later addition if live
(not post-hoc) tracing turns out to matter.
*Estimate: 1–2 weeks.*

### Explicitly deferred / non-goals for this uplift
- **LLVM PGO workflow** (Cill §12.7) — curry's LLVM backend is an optional
  JIT-tiering module, not the sole compilation path the way it is for Cill;
  a PGO build workflow is real value but is a build-system project of its
  own, not part of *profiling* per se. Revisit once Phase 3 (tier
  attribution) shows whether PGO would actually move the needle for curry's
  JIT tier specifically.
- **Windows sampling** — curry's current platform targets are Linux/macOS;
  SIGPROF-based sampling has no Windows equivalent story and isn't worth
  designing around speculatively.

## Total estimate

Roughly 8.5–12.5 weeks sequential for all seven phases, dominated by Phases
2 and 6 (the two where curry's actual constraints — real OS-thread actors,
conservative GC — make the naive Cill-spec version not directly portable).
Phases 1, 3, 4 are the cheap, low-risk, high-value slice and should go first
regardless of how much of the rest gets built.

## Open decisions before starting Phase 2

- Per-thread sampler registration API shape — mirror `gc_register_thread()`
  exactly, or fold into it (one registration call serves both GC and
  profiler)?
- Flamegraph SVG generation — hand-roll (matches Cill's zero-new-dependency
  approach) or shell out to Brendan Gregg's `flamegraph.pl`-equivalent
  logic reimplemented in C? Hand-rolling a basic version is a well-known,
  bounded algorithm (collapse stacks → rectangles) — recommend hand-rolling
  to avoid a Perl dependency in the build.
