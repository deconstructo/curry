# Introspection uplift — plan

*Drafted 2026-08-31. Status: planning only, nothing implemented yet.*

## Why

Compared against `cill_spec.pdf` §13 (Introspection) after reading it and
finding it "nice and clean." Unlike concurrency, this one lines up almost
exactly with a phase curry's own roadmap already sketched —
`docs/roadmap.md` Phase 8 ("Introspection + Profiling + SIMD") already
contains a design sketch that reads nearly identically to Cill's §13, which
means this uplift is mostly about *sequencing and scoping* work already
planned, not discovering new scope.

**Important scope note to avoid duplicated planning:** Phase 8's sketch
bundles the sampling profiler, event tracing, and allocation/concurrency
profilers in with introspection proper. Those four are already fully
planned in `docs/thoughts/profiling-uplift-plan.md` (its Phases 2, 5, 6, 7)
— don't re-plan them here. This document covers what's left after
subtracting that overlap: object/heap introspection, compiler introspection,
actor/thread/tvar introspection, REPL integration, and macro/symbolic-algebra
introspection.

## Current state — verified against source

| Cill §13 concept | curry equivalent | Status |
|---|---|---|
| Interactive debugger (breakpoints, step/next/finish/continue, `bt`, locals) | `,break`/`,unbreak`/`,breaks`/`,debug`, `-b` CLI flag, `(breakpoint)` (`docs/reference/debugger.md`) | ✓ complete, shipped v1.8.0 |
| `(gc-stats)` | `gc-stats`/`gc-stats-reset!` (`src/builtins.c`) — pause-ring, nursery usage | ✓ exists, but only meaningful under `--gc generational` (opt-in, experimental); reports little of interest under the default Boehm backend |
| Basic call-count/timing profiler | `(curry profiling)` levels 0-3 | ✓ complete (see profiling-uplift-plan.md for everything beyond this) |
| `list-rules` (rule-system introspection) | `list-rules` (`src/builtins_curry.c`) | ✓ exists |
| `(type-of x)`, `(size-of x)`, `(gc-generation x)`, `(references x)`, `(referenced-by x)` | — | ✗ missing |
| `heap-walk`, `heap-snapshot`, `retention-path`, `heap-mark`/`heap-diff` | — | ✗ missing |
| `compile->ast/hir/mir/llvm/asm`, `disassemble`, `optimisations-applied`, `inferred-type` | — | ✗ missing (the debugger can show source-level state at a breakpoint, but nothing dumps intermediate compiler representations) |
| `list-actors`, `actor-mailbox`, `actor-trace` | `actor-stats` gives per-actor mailbox depth/message counts/age *given a handle already in hand* — no enumeration of all live actors | ~ partial — the per-actor data exists, the "list them all" and "recent message history" pieces don't |
| `list-threads`, `thread-stack-trace`, `pool-info` | — | ✗ missing |
| `list-tvars`, `tvar-info` (value/waiters/conflict-count) | — | ✗ missing (`src/stm.c` tracks live `n_waiters` per tvar internally but nothing is exposed to Scheme, and no cumulative conflict count is tracked at all — same gap noted in `concurrency-uplift-plan.md`) |
| REPL: `,profile`, `,trace`, `,expand`, `,asm`, `,heap`, `,actors`, `,gc`, `,threads` | `,profile` and the debugger commands exist; `,gc`-equivalent info is folded into an existing `,vm` command (`src/main.c`) rather than a dedicated `,gc` | ~ partial — `,trace`/`,expand`/`,asm`/`,heap`/`,actors`/`,threads` don't exist |
| `macro?`, `macro-expander`, `macroexpand-1`, `macroexpand` | — | ✗ missing entirely — no way to inspect what a macro expands to short of triggering the actual expansion by running the code |
| `expr-head`/`expr-args`/`expr-depth`/`expr-atoms`/`expr-ops`, `rule-applications`, `explain-simplify` | `list-rules` exists; the rest don't | ~ partial |

## Prioritization

Four natural clusters, ordered by value and how much they unblock other
work already in flight:

### 1. Macro introspection — highest value, smallest scope
`macro?`, `macroexpand-1`, `macroexpand`, and a REPL `,expand` command. This
is squarely the single most-requested-in-practice piece: this session alone
hit the non-hygienic-macro-across-`define-library`-boundaries bug class
repeatedly (every SRFI shim needing hand-traced re-exports), and every one
of those debugging sessions would have been faster with `(macroexpand
'(some-macro-call ...))` available instead of tracing `syntax-rules`
templates by hand. This is also the cheapest item on the list: curry's
`syntax_rules.c` already has the expansion logic; this is exposing an
existing internal step as a callable primitive, not building new expansion
machinery.
*Estimate: 3–5 days.*

### 2. Actor/thread/tvar enumeration — small, unblocks the concurrency profiler
`list-actors`, `list-threads`, `list-tvars`, `tvar-info`. Most of the
per-object data already exists (`actor-stats` per actor, `n_waiters` per
tvar internally) — what's missing is a global registry to enumerate *all*
live instances of each. This is also a direct prerequisite for
`profiling-uplift-plan.md`'s Phase 5 concurrency profiler, which needs to
iterate "all actors" and "all tvars" to aggregate anything.
*Estimate: 1–1.5 weeks* (actors and tvars need a tracked global list with
proper lifecycle cleanup on exit/GC — not just exposing existing per-object
fields).

### 3. Object/heap introspection — medium, GC-backend-sensitive
`type-of`, `size-of`, `gc-generation`, `references`/`referenced-by`,
`heap-walk`, `heap-snapshot`, `retention-path`, `heap-diff`. `type-of` and
`size-of` are cheap (every heap object already carries a type tag and a
known size per `Hdr`). `gc-generation` only means anything under
`--gc generational` (Boehm has no generations) — needs to report something
sensible (e.g. `'not-applicable` or the backend name) under the default
backend rather than a wrong or crashing answer. `heap-walk`/`heap-snapshot`
need a full-heap traversal, which Boehm supports (`GC_enumerate_reachable_
objects_inner` or similar) but the generational backend's own walk would
need separate code — this is real, GC-backend-conditional work, not a
single implementation. `retention-path` ("why is this object alive") is the
hardest item here — it requires either a reverse-pointer index maintained
continuously (real overhead) or an on-demand full-heap trace from roots
(cheap when idle, potentially slow when actually asked, which is the
correct tradeoff for a diagnostic tool used rarely).
*Estimate: 2–3 weeks*, with `retention-path` as the long pole.

### 4. Compiler introspection — medium, ties into the eval-elimination migration
`compile->ast/hir/mir/llvm/asm`, `disassemble`, `optimisations-applied`,
`inferred-type`. curry's pipeline is Reader → AST → (tree-walk `eval()` OR
compiled VM bytecode OR JIT-native) — not the clean five-stage HIR/MIR/LLVM
pipeline Cill's spec assumes, so `compile->hir`/`compile->mir` don't have
an obvious direct equivalent; `compile->ast` and `disassemble` (VM bytecode
disassembly) map cleanly, and `compile->llvm`/`compile->asm` only make
sense when `BUILD_LLVM=ON`. This is the item most worth building *after*
`profiling-uplift-plan.md`'s Phase 3 (tier attribution) rather than before
— tier attribution already needs to distinguish which pipeline stage
handled a given call, and `compile->ast`/`disassemble` are natural
byproducts of building that distinction cleanly, not a separate effort.
*Estimate: 2–3 weeks, sequence after profiling Phase 3.*

## Symbolic algebra introspection — smaller, self-contained, not sequenced above

`expr-head`, `expr-args`, `expr-depth`, `expr-atoms`, `expr-ops`,
`rule-applications`, `explain-simplify`. These operate entirely within
`src/symbolic.c`'s existing expression-tree representation and don't depend
on anything else in this document — `expr-head`/`expr-args`/`expr-depth`
are near-trivial tree accessors, `expr-atoms`/`expr-ops` are a tree walk
collecting a set, and `rule-applications`/`explain-simplify` need
`simplify` to record which rules fired during a given call (a small
addition to the existing rewrite loop, not a redesign of it). Can be done
independently, any time, by whoever's already touching the CAS.
*Estimate: 1–1.5 weeks.*

## REPL integration

`,expand` falls out of cluster 1 above directly. `,actors`/`,threads` fall
out of cluster 2. `,heap` falls out of cluster 3. `,asm` falls out of
cluster 4. None of these need separate design — each is a thin REPL-command
wrapper around the corresponding primitive once it exists, following the
existing pattern for `,profile`/`,break`/etc. in `src/main.c`. Budget
roughly 2–3 days total once the underlying primitives exist, not a
separate phase.

## Phased plan (recommended order)

1. **Macro introspection** (`macroexpand`/`macroexpand-1`/`macro?`,
   `,expand`) — cheapest, highest immediate debugging value. **Done**
   (branch `vm-disassemble-repl-asm`, pending review/merge as of
   2026-08-31): `macro?`, `macroexpand-1`, `macroexpand` added to
   `src/builtins.c`, GLOBAL_ENV-only exactly as scoped above; `,expand`
   REPL command added to `src/main.c`.
2. **Actor/thread/tvar enumeration** (`list-actors`/`list-threads`/
   `list-tvars`/`tvar-info`) — unblocks `profiling-uplift-plan.md` Phase 5.
   **`list-actors`/`,actors` done** (same branch): a new fixed-4096-slot
   global registry in `src/actors.c`, populated on successful spawn and
   cleared on the actor's own exit path. `list-threads`/`list-tvars`/
   `tvar-info` still outstanding — no green threads to list yet, and
   `src/stm.c` has no equivalent tvar registry (would follow the same
   pattern as the actor one).
3. **Symbolic algebra introspection** — independent, can slot in anywhere.
   Not started.
4. **Object/heap introspection** — GC-backend-conditional, do after 1–3 are
   proven out so the same "how do we expose internal state safely" pattern
   is established. **`type-of` done** (same branch: a builtin mapping
   `ObjType`/immediate tags to a symbol, per-object only — `size-of`,
   `gc-generation`, `references`/`referenced-by`, `heap-walk`,
   `heap-snapshot`, `retention-path` still outstanding, the GC-backend-
   conditional part of this item).
5. **Compiler introspection** — sequence after `profiling-uplift-plan.md`
   Phase 3 (tier attribution), since they share the same underlying "which
   pipeline stage did this" bookkeeping. **`disassemble`/`,asm` done ahead
   of schedule** (same branch) — this was originally planned to wait for
   tier attribution, but turned out to be self-contained: it exposes
   `chunk.c`'s pre-existing internal `chunk_disasm` (previously stderr-only,
   used by `compiler_ir_checks.c`'s codegen-comparison tool) rather than
   needing any new tier-attribution bookkeeping. In the course of wiring
   this up, found and fixed a real, independent bug in that pre-existing
   disassembler: `OP_CLOSURE`'s variable-length upvalue-table encoding was
   never skipped, desyncing every later instruction in the chunk for any
   closure with at least one upvalue — filed as
   [issue #96](https://github.com/deconstructo/curry/issues/96), fixed in
   the same branch. `compile->ast/hir/mir/llvm` still outstanding and
   still correctly sequenced after tier attribution.

## Total estimate

Roughly 7–10.5 weeks across all five items, most of it parallelizable
(items 1, 2, and 3 have no dependencies on each other and could be done
concurrently by different people/sessions; only item 5's sequencing after
`profiling-uplift-plan.md` Phase 3 is a hard ordering constraint).

## Non-goals

- **Re-planning the sampling profiler, event tracing, or allocation/
  concurrency profilers** — fully covered by `docs/thoughts/profiling-uplift-plan.md`.
- **LLVM SIMD tower** (`docs/roadmap.md` Phase 8's fourth bullet) — a
  numeric-performance project, not introspection; out of scope here.
- **A full HIR/MIR-equivalent intermediate representation** — curry's
  pipeline doesn't have Cill's five-stage shape and isn't getting
  restructured to match it just to make `compile->hir`/`compile->mir`
  literal; `compile->ast` and VM bytecode disassembly are the honest
  curry-shaped equivalents.
