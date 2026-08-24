# Performance Lessons from Chez Scheme and Kaappi

**Document version 2 — 2026-08-18** (v1 was 2026-07-16). Verified against
curry v1.21.0 / `main`.

*Design recommendations for curry, 2026-07-16, updated 2026-08-18*

This document surveys the two most instructive reference points for curry's
performance work — Chez Scheme (the gold standard for native-compiled Scheme,
~40 years of engineering) and Kaappi (a 2026 R7RS-small implementation in Zig
whose architecture is strikingly close to curry's: bytecode VM + LLVM backend +
generational-GC ambitions) — and turns them into a prioritized development
plan. Each recommendation is grounded in what curry actually has on `main`
today, verified against the source.

**2026-08-18 update**: Tier 0 (both items) and Tier 1's item 1.4 have
shipped since July. Separately, the eval-elimination migration (tracked in
project memory, not this doc originally) made direct, if not originally
itemized, progress on Tier 4's stated prerequisite — "shrink `tree-eval`
passthrough first" — `define-library`/R6RS-library bodies now compile and
run through the VM (`vm_eval`) instead of being tree-walked, and the LLVM
JIT-compile-failure fallback (§4.3) now falls back to the VM instead of the
tree-walker too. See the callouts inline below for specifics; Tier 2 (the
IR layer) and Tier 3 (GC) are unchanged — no IR exists yet, and
`gc-rewrite` is still an unmerged side branch.

---

## 1. Where curry stands today

Facts checked against `main`, since they frame everything below (July 2026
column preserved for reference; August column re-verified against source):

| Subsystem | July 2026 | August 2026 |
|-----------|-----------|-------------|
| Execution | Stack-based bytecode VM (`src/vm.c`, computed-goto dispatch) with a tree-walking `eval` (`src/eval.c`) reached via `(tree-eval '<form>)` passthrough for forms the compiler doesn't handle (`src/compiler.c:1233`) | Same VM/dispatch. The `tree-eval` punt list is down to exactly 3 forms (`import`/`define-library`/`library`, `src/compiler.c:2408`) — `define-syntax`, `receive`, `define-record-type`, `syntax-rules`, and the CAS-adjacent forms all now have native `compile_*` codegen. `define-library`/R6RS-library *bodies* also no longer tree-walk: `modules.c` compiles and runs each body form through the VM (`vm_eval`), not `eval()` — real progress on §4.4's stated prerequisite, "shrink `tree-eval` passthrough first," though `import`/`define-library`/`library` themselves are still punted (they're not compiler-native forms yet) |
| Arithmetic | Inline fixnum fast path with `__builtin_add_overflow` in `OP_ADD` (`src/vm.c:507`); everything else calls the `num_*` tower (GMP-backed) | Unchanged |
| Flonums | Heap-allocated `T_FLONUM` objects — every flonum-producing op allocates | Unchanged |
| Globals | Versioned inline cache per chunk (`GlobCacheEntry`, `src/vm.c:435`); any global `set!` bumps `root->version`, invalidating all entries (full invalidation — the safe design; see §4.6) | Unchanged, but generalized: the cache now validates against an arbitrary per-chunk `target_env` root frame (not hardcoded `GLOBAL_ENV`), so a `define-library` body's own isolated environment gets the same caching — same invalidation semantics, wider applicability |
| Calls | `OP_CALL` / `OP_TAIL_CALL`; tiered JIT hot-swap to native code in `OP_CALL` (`src/vm.c:707`); no fused or self-recursive call opcodes | Unchanged — Tier 1.1/1.2 (self-tail-call, fused global-call opcodes) not started. One correctness fix landed that affects tail-call behavior generally: `compile_cond` previously only tail-call-optimized a `cond` clause's body when it was the textually *last* clause — any self-recursive call sitting in a non-last clause silently grew the stack every iteration instead of reusing the frame. Fixed; not a superinstruction, but closes a real gap in existing TCO correctness |
| LLVM | ORC JIT (`src/llvm/`), statepoint-based GC strategy registered, stack-map emission on, `gc_args` empty until precise GC lands; minor GC inhibited across statepoints | Unchanged, plus: chunks compiled against a non-`GLOBAL_ENV` `target_env` are now explicitly excluded from JIT promotion (`maybe_jit_bcc`, `src/runtime.c`) — the JIT's native codegen (`src/llvm/jit.cpp`) still hardcodes `GLOBAL_ENV` for global lookups, so this is a correctness guard, not a new capability. Separately, `curry_jit_eval_expr`'s JIT-compile-failure fallback (§4.3) now calls `vm_eval()` instead of tree-walking — exactly the swap this document's own §4.3/Tier-4 discussion anticipated |
| GC | Boehm (conservative) baseline; generational/semispace attempts reverted; `gc-rewrite` branch at M1 (safepoint at `L_DISPATCH`) | Unchanged — `gc-rewrite` still an unmerged side branch, no new milestones confirmed landed since July per project memory |
| Continuations | `setjmp`/`longjmp` escape-only `call/cc` | Unchanged — full multi-shot `call/cc` still blocked on the same prerequisite (§4.4), which is now measurably closer but not complete: `eval()` still has real callers (`prim_eval`'s R7RS `eval`, `tree-eval` itself, `scm_load`, `modules.c`'s `load_scheme_module`, `eval_body` for any remaining tree-walker closures) |
| Caching | `.scc` bytecode files exist but only via explicit `-c`; no transparent cache | **Done** — running `curry script.scm` directly now auto-compiles and transparently caches to `.scc` (content-hash keyed via `src_hash()`, not mtime/size), reused on the next run; `--timings` reports cache `HIT`/`MISS`. Matches Tier 1.4 and Kaappi's own pattern (§4.7) exactly |
| Benchmarks | `tests/bench*.scm` exist; not in CI, no trend tracking | **Done** — `.github/workflows/benchmark.yml` runs on every push/PR with a stored baseline and a regression gate (currently a flat 130%-of-baseline alert threshold, single-sample comparison — noted during this session as noise-prone on at least one PR; per-commit trend *storage* exists, but the gate itself would benefit from averaging/repetition before being fully trusted) |

---

## 2. The headline: both references validate curry's architecture

Kaappi and Chez arrived at the same two-tier shape curry already has:

- **Kaappi**: register bytecode VM (dev/REPL/debugging) + LLVM AOT backend
  (deployment), both fed by one shared IR. LLVM replaces *only the emission
  stage*; the runtime (GC, values, primitives) is identical for both tiers.
- **Chez 10**: native backends + the `pb` (portable bytecode) interpreter used
  for bootstrapping and porting.

Curry's VM + ORC JIT is the same architecture. Nothing below says "rewrite" —
the recommendations are about closing specific gaps inside this shape.

---

## 3. What Chez teaches

### 3.1 Continuations: the segmented stack (the endgame)

Chez's continuations come from Hieb, Dybvig & Bruggeman, *"Representing
Control in the Presence of First-Class Continuations"* (PLDI 1990): the stack
is a chain of heap-allocated **segments**. Capturing a continuation is O(1) —
seal the current segment and keep a pointer; the running frame continues on a
fresh segment. Underflow frames splice segments back. One-shot continuations
(`call/1cc`) avoid even the copy on restore. This is why `call/cc` is nearly
free in Chez and why Racket could move its whole thread system onto it.

**Lesson for curry:** full `call/cc` requires execution state to live in data
structures the runtime owns, not the C stack. Curry's VM already has an
explicit frame stack; the tree-walker does not. The pragmatic sequence is
Kaappi's (copy the VM stack, §4.4) with Chez's segments as the later
optimization *if* capture ever shows up hot. Do not start with segments —
Kaappi evaluated them and rejected the pervasive per-call cost.

### 3.2 BiBOP generational GC (direct input to gc-rewrite P3)

Chez's collector (Dybvig, Eby & Bruggeman, *"Don't Stop the BiBOP"*, 1994)
allocates homogeneous **segments**: each 4–8 KB segment holds objects of one
type and one generation. Consequences worth stealing for the three-zone heap
planned in gc-rewrite P3:

- **Type is a property of the page, not the object** — no per-object header
  needed for GC purposes; curry's 8-byte `Hdr` on every pair is pure overhead
  by comparison (a pair could be 16 bytes, not 24).
- **Write barriers are card/segment-granular** — the remembered set is "dirty
  segments," cheap to maintain and scan.
- **Sweeping is per-segment and type-specialized** — no interleaved-type heap
  walks.
- Guardians and weak pairs fall out of the segment model nearly for free.

### 3.3 cp0: source-level inlining before anything else

Waddell & Dybvig, *"Fast and Effective Procedure Inlining"* (SAS 1997): a
polyvariant inliner with effort and size budgets that runs on the source-level
IR, doing constant folding, copy propagation, and dead-code elimination as it
goes. Chez credits cp0 with much of its performance — it is what makes
`(map (lambda (x) (* x x)) ls)` compile as if hand-written. **This is the
single highest-leverage compiler pass curry lacks**, and it requires an IR to
run on (§4.2).

### 3.4 Flat closures, optimized away (Keep & Dybvig, 2012)

*"Optimizing Closures in O(0) Time"*: most closures don't need to exist.
Chez eliminates closures for non-escaping lambdas (variables passed as extra
arguments), shares closures with identical free-variable sets, and skips
allocation for closures whose free variables are all bound to constants or
well-known procedures. Curry's VM closures are already flat (upvalue arrays),
so the *representation* is right; the missing piece is the *analysis* that
avoids allocating them. A "known non-escaping lambda → direct call, free vars
as locals" pass covers the majority of the win (named-`let` loops, `let`-bound
lambdas).

### 3.5 Open-coded primitives with type recovery

Chez open-codes `car`, `cdr`, `vector-ref`, `+` etc. inline, and its
source-optimizer proves types to delete checks (`(car x)` after `(pair? x)`
in the same branch emits no check). Curry's VM already open-codes these as
opcodes; the missing half is **unchecked variants** (`OP_CAR_NOCHECK`, fixnum
`OP_ADD_FX`) emitted when a simple dominating-test analysis proves safety.
This matters double for the LLVM tier, where a proven-fixnum loop can run
entirely in registers.

### 3.6 Flonum unboxing is a compiler problem, not a representation problem

Chez heap-allocates flonums, same as curry — and still wins flonum benchmarks,
because the compiler *unboxes across expressions*: intermediate results of
proven-flonum arithmetic stay in FP registers, boxing only at escape points.
NaN-boxing (Kaappi's choice) gets unboxed flonums "for free" but would mean
rewriting curry's entire `val_t` scheme and every module. **Recommendation:
keep the 2-bit low-tag scheme; do flonum unboxing as an IR/LLVM-tier
optimization.** Curry's SICM/physics workloads are exactly where this pays.

---

## 4. What Kaappi teaches

Kaappi's dev docs (`docs/dev/` in [kaappi/kaappi](https://github.com/kaappi/kaappi))
are unusually candid — decision records, postmortems, and measured numbers.

### 4.1 Register VM vs curry's stack VM — take the superinstructions, skip the rewrite

Kaappi's VM is register-based (29 opcodes); curry's is stack-based. Registers
avoid push/pop dispatch traffic, but a migration would touch every line of
`compiler.c` and `vm.c`. Kaappi's own data suggests the cheaper path: their
`self_tail_call` opcode — copy args to frame base, reset IP, skipping global
lookup, type check, and arity check — measured **~23% on tak(33,22,11)** by
itself. Their fused `call_global`/`tail_call_global` superinstructions
(lookup+call in one dispatch) are equally applicable to a stack VM.

Their failure is as instructive as the success: enabling `tail_call_global`
for *all* tail calls broke tail calls to globals holding **parameter objects,
continuations, and FFI functions**, because the fused handler only dispatched
closures and natives. **Any fused call opcode in curry must handle every
callee type `OP_CALL` handles, or reject at compile time.**

### 4.2 A small shared IR between expander and emission

Kaappi lowers post-expansion S-expressions to a 33-node tree IR, runs three
analysis passes (tail positions, primitive identification, constant marking)
and five cheap optimizations (constant folding, dead-branch elimination,
boolean simplification, identity elimination, begin flattening), then splits
into bytecode emission or LLVM emission. Curry's `compiler.c` emits bytecode
directly from S-expressions, and `src/llvm/codegen.cpp` does its own thing —
so any optimization must be written twice or not at all. cp0 (§3.3), closure
elision (§3.4), and unchecked-primitive emission (§3.5) all want this layer to
exist first. Kaappi even kept an escape hatch during migration — forms not yet
lowered stay as raw S-expressions and delegate to the old compiler — which is
exactly curry's `tree-eval` passthrough pattern, so the migration can be
incremental.

### 4.3 LLVM tier: naive IR, `-O2`, and verify everything

Three concrete practices from their LLVM backend doc:

1. **Emit deliberately naive IR** — every immediate as an instruction, every
   binding through `alloca` — and let mem2reg/instcombine/simplifycfg clean it
   up at `-O2`. Cleverness in the emitter is bugs waiting to happen.
2. **Run the IR verifier on every emitted module in CI** (`opt
   -passes=verify`). Malformed IR can pass `-O0` and miscompile at `-O2`.
   Curry's ORC JIT should verify each module in Debug builds and in the test
   suite.
3. **A thin C-ABI bridge passing values as plain `u64`** — curry's `val_t` is
   `uintptr_t`, so this discipline is already natural; keep the JIT↔runtime
   surface to a small explicit export list (theirs: init, global
   lookup/define, call, make-string, intern-symbol, box ops, cached eval).

Their per-call-site caches are worth copying directly: `kaappi_eval_cached`
compiles a fallback expression **once per call site** and memoizes the
resulting function in a site-local slot; `kaappi_quote_cached` builds quoted
heap constants once. Curry's `(tree-eval '<form>)` passthrough currently
re-walks the form on every execution — a per-site cached-closure slot is the
same fix at the bytecode tier.

### 4.4 The hybrid continuation strategy (decision record, 2026-06-27)

Kaappi evaluated CPS conversion (rejected: every call pays; two IR flavors)
and segmented stacks (rejected: per-call heap allocation, C-ABI breakage) and
chose: **direct-style native code + VM fallback**. Multi-shot `call/cc` works
by copying the VM's explicit state (registers, frames, handler stack,
dynamic-wind stack) into a heap object — O(depth) capture. A conservative
analysis marks functions that may reach `call/cc`; only those stay on the VM,
everything else compiles native with zero overhead. `call/ec` compiles
natively as `setjmp`/`longjmp` since escapes are single-shot.

This maps onto curry step by step:

1. Full `call/cc` in the VM via frame-stack copying (requires all execution
   through the VM — shrink `tree-eval` passthrough first).
2. Keep `setjmp`-based escapes as the fast path for `call/ec`, `guard`,
   `dynamic-wind` exits.
3. JIT tier: continuation-free functions (the overwhelming majority) compile
   native; `call/cc`-reachable code side-exits to the VM.

### 4.5 GC postmortems — a checklist for gc-rewrite M2+

Once curry leaves Boehm's conservative scanning, kaappi's root-marking bugs
become curry's future bugs. Their distilled rule: *every heap value reachable
by any code path must be traceable from a root; the common gaps are hashmaps,
caches, and temporaries held across allocation points.* Their specific
instances, each of which has a curry analogue to audit:

- Global-value cache not GC-traced → curry's `GlobCacheEntry` slots
- Library/module export tables not rooted → `src/modules.c` registries
- Flonum cache entries collected mid-use
- Exception handler collectible between pop and invoke → `ExnHandler` chain
- `vm_instance` root registered too late in startup

### 4.6 Cache invalidation: full or per-entry, no middle ground

Kaappi's global cache stamped one version on the whole cache; caching any
entry made all stale entries look valid. Curry's design (per-entry version
compared against `root->version`, bumped on every global write) is the safe
"full invalidation" variant — but it invalidates everything on *any* global
`set!`, so a program that mutates one hot global thrashes the cache for all
globals. If profiling ever shows this, the fix is per-binding generation
counters — never a shared stamp with per-entry writes.

### 4.7 Operational transparency: `--timings`, cache visibility, benchmark CI

- Kaappi's `--timings` prints per-stage pipeline times (read / expand / lower /
  optimize / emit / execute) plus an explicit cache **HIT/MISS** line — added
  after an invisible bytecode cache cost them real debugging hours. Curry has
  profiling for user code (`src/profiling.c`) but nothing for its own pipeline.
- Their `.sbc` cache is transparent (content-hash keyed, `~/.kaappi/cache/`),
  not opt-in like curry's `-c`. Free startup wins for scripts, *if* paired
  with the visibility above.
- Per-commit benchmark tracking in CI with a PR-regression gate (they use a
  fork of `github-action-pull-request-benchmark`). This is gc-rewrite P0,
  already packaged and proven on a sibling project.

---

## 5. Recommendations

Ordered by leverage-per-effort, mapped to existing roadmap items.

### Tier 0 — instrumentation first (days) — **both items done**

| # | Item | Source | Notes |
|---|------|--------|-------|
| 0.1 | ~~Benchmark suite in CI with per-commit trends + PR regression gate~~ **Done** | Kaappi §4.7 | `.github/workflows/benchmark.yml`, stored baseline + gate. Caveat: the gate is a single-sample comparison against one baseline (flat 130% threshold), confirmed noise-prone on at least one real PR this session (zero production-code changes still tripped it uniformly ~1.3-1.6x) — worth revisiting with repetition/averaging before treating a gate failure as automatically meaningful |
| 0.2 | ~~`--timings` per-stage pipeline report~~ **Done** | Kaappi §4.7 | Reports read/expand/compile/execute stage times plus cache HIT/MISS; see `--timings` in the CLI reference |

### Tier 1 — VM quick wins (days–weeks each, all independently benchmarkable)

| # | Item | Source | Expected effect | Status |
|---|------|--------|-----------------|--------|
| 1.1 | `OP_SELF_TAIL_CALL` for direct self-recursion and named-`let` loops | Kaappi §4.1 | ~20%+ on call-heavy code (their measured 23% on tak) | **Done** (PR #60) — scoped to named-`let` only (a global self-reference could be reassigned by unrelated code, unlike a loop's private upvalue); ~15% measured on a named-let benchmark |
| 1.2 | Fused `OP_CALL_GLOBAL` (lookup+call, one dispatch) — handling **all** callee types | Kaappi §4.1 | One dispatch instead of two on the most common call shape | **Done** (PR #60) — `OP_CALL_GLOBAL`/`OP_TAIL_CALL_GLOBAL`, reuses the same `call_foreign` trampoline `OP_CALL` uses for every non-closure callee type; ~5-8% measured on `fib(30)` |
| 1.3 | Per-call-site cache for `tree-eval` passthrough (compile once, memoize thunk) | Kaappi §4.3 | Removes re-walking cost from every non-compiled form | **Done** (PR #60) — `OP_TREE_EVAL_CACHED`, a per-chunk side array (`Chunk::tree_eval_cache`) parallel to the constant pool, C11 atomics for cross-actor-thread safety, never touched by the `.scc` serializer (mirrors `glob_cache`'s always-cold-on-load convention) |
| 1.4 | Transparent content-hash `.scc` cache with HIT/MISS visibility | Kaappi §4.7 | Script startup; visibility line is non-negotiable | **Done** — see §1 table above |

### Tier 2 — the IR layer (the enabling investment, ~weeks)

| # | Item | Source |
|---|------|--------|
| 2.1 | Small tree IR between expander and emission; analysis passes (tail positions, primitive ID, constants); migrate incrementally using `tree-eval`-style delegation for unmigrated forms | Kaappi §4.2 |
| 2.2 | Cheap optimization passes on the IR: constant folding, dead-branch elimination, boolean simplification | Kaappi §4.2 |
| 2.3 | cp0-style inliner with effort/size budgets on the IR | Chez §3.3 — **local candidates fully covered** (first landing `tier2-cp0-inline-1`, 2026-08-22; widened to `let`/`let*` by `tier2-let-inline-1`, 2026-08-23): inlines a call to a **local** procedure — internal `(define name (lambda ...))` (including `(define (f params...) body...)` sugar), `letrec`/`letrec*` bindings (these desugar to internal defines inside a zero-arg IIFE, so they reach the exact same registration as internal defines with no further work), and now `let`/`let*` bindings too — whose body compiles/proves closed (zero captured upvalues) and is non-self-recursive, has a proper (non-rest) param list, and is within a size budget, with an effort budget capping total inlines per top-level compile. Splices the candidate's params/body directly into the calling frame (`ir_emit_inline_call`) — no new closure, no `OP_CALL`/`OP_RETURN`, reusing `begin_scope`/`end_scope`'s existing `OP_SLIDE`/`OP_CLOSE_UP` machinery, no new opcode. `let`/`let*` needed a genuinely different mechanism from internal defines: their bindings become IIFE *parameters*, bound via a call argument the wrapper's own callee (its body, including any calls to the binding) has already been fully compiled and torn down *before* — the opposite order from `IR_DEFINE`, where the value compiles first and a later statement in the same pass reads the result — so closedness can't be determined post-hoc (`g_last_lambda_upval_count`) the way it is for internal defines; instead a new, side-effect-free, pre-expansion syntactic walker (`lambda_is_closed`, modeled on the existing `expr_mentions_set_target`) runs at `ir_lower_let`/`ir_lower_let_star` time, before either the wrapper or the candidate exist as real Compilers, threading the decision through a new `known_params` field on `IR_LAMBDA` (`ir.h`) for `ir_emit` to consult once real param slots exist. **Not in scope at all**: inlining calls to *global* procedures, which would need speculative-inline + deopt-guard machinery against the existing `GlobCacheEntry` version cache. |
| 2.4 | Closure elision for known non-escaping lambdas | Chez §3.4 — **compiler-synthesized wrappers fully covered** (branch `tier2-let-wrapper-elide-1`, 2026-08-24): `let`/`let*`/`letrec`/`letrec*`/named-`let` all desugar to a compiler-built `((lambda (params...) body...) args...)` IIFE — provably non-escaping and single-call-site *by construction* (each is the sole producer of its own callee node, immediately consumed by the very next emission), unlike Tier 2.3's named-candidate inliner, so no closedness check, no self-recursion guard, and no size/effort budget are needed. Splices the wrapper's body directly into the caller's frame via the same `ir_emit_inline_call` machinery Tier 2.3 uses, eliminating the closure allocation and `OP_CALL`/`OP_RETURN` pair the wrapper previously paid for on every entry. Named-`let` needed its own hand-rolled splice (`ir_emit`'s `IR_NAMED_LET` case) rather than routing through `ir_emit_inline_call`, since its own loop closure must stay a genuine closure (self-tail-call recursion) with only the *entry* wrapper elided — including an explicit `OP_SLIDE(1)` on its non-tail path to reclaim a stale stack slot the call itself doesn't consume. Two real, independently-found miscompilations fixed en route: an unguarded `MAX_LOCALS`(256) array write in the `let`/`let*`/`letrec`/`letrec*` splice (a single flat `let` with 256+ bindings corrupted the C stack — found by code review, confirmed via a reproducible SIGBUS, fixed with the same bound check Tier 2.3's own call site already had); and a pending-slot bookkeeping gap in `apply`/`values`/`call-with-values` (three classic special forms predating Tier 2.3's `reserve_pending_slot` convention) that silently aliased an earlier sibling argument once any wrapper-elided form appeared later in the same argument list — found by `tests/sicm_tests.scm` failing on real physics code, invisible to the differential classic-vs-IR check since both sides share the same unlowered classic code path. **Not in scope**: closure elision for ordinary, programmer-written non-escaping lambdas (Tier 2.3's local inliner already covers the call-site-inlining case for those; a separate escape-analysis pass would be needed for elision without inlining). |
| 2.5 | Unchecked primitive variants under dominating-test analysis (`OP_CAR_NOCHECK`, `OP_ADD_FX`) | Chez §3.5 — **step 1 (open-coding) shipped for car/cdr/cons/pair?/null?** (branch `tier2-unchecked-primitives-1`, 2026-08-24): found on starting this item that its own prerequisite didn't exist — `car`/`+`/etc. were never open-coded at all before this landing, always compiled as an ordinary `OP_CALL_GLOBAL` to the primitive procedure; the doc's own earlier claim that curry "already open-codes these as opcodes" didn't match reality (`OP_CAR`/`OP_CDR`/`OP_CONS`/`OP_NULLP`/`OP_PAIRP` existed in `opcode.h` with real VM handlers but zero compiler emitters). Repurposes those five previously-dead opcodes: each takes one const-pool operand (the callee symbol), validated at *every* execution against the actual `prim_car`/`prim_cdr`/etc. C function pointer (not a compile-time-captured value — a real bug found and fixed mid-landing, since a value captured after an earlier redefinition would wrongly treat the redefinition as the primitive forever) via the same `glob_cache` infrastructure `OP_LOAD_GLOBAL`/`OP_CALL_GLOBAL` already use, falling back to an ordinary call on a mismatch. A second real bug, found by independent code review: naively open-coding a *tail-position* call broke proper tail-call optimization for a redefined name (the fallback path recurses via `apply_arr`→a nested `vm_run()`, unlike `OP_TAIL_CALL_GLOBAL`'s frame-reuse) — fixed by never open-coding a call already known to be in tail position at all. Benchmarked (Release, low-variance): ~5.7% faster on a car/cdr-heavy traversal — the expected narrow scope of open-coding alone, removing call-frame setup but not the type check itself. **Step 2 (open-coding extended to `+`/`-`/`*`/`=`/`<`/`<=`/`>`/`>=`) also shipped** (same branch, 2026-08-25): repurposes `OP_ADD`/`OP_SUB`/`OP_MUL`/`OP_NUMEQ`/`OP_LT`/`OP_LE`/`OP_GT`/`OP_GE` (all previously dead code, zero emitters, same as step 1's five) the same way — one const-pool operand, guarded against the real `prim_add`/`prim_num_lt`/etc. C function pointer, strictly 2-arg only (R7RS's own variadic `+`/`-`/`*`/comparison chains fall through to the ordinary call path unchanged, since the opcodes themselves are strictly binary). `OP_EQ` (a separate, still-genuinely-dead legacy alias that used to share one switch-case body with `OP_NUMEQ` via fallthrough) was split out and left untouched rather than folded into this. Design correction made *before* writing the fast path, not after: an earlier plan to reuse `OP_ADD`/`OP_LT`/etc.'s own separate, pre-existing inline fixnum-fast-path bodies as the "match" branch was dropped on finding that `prim_num_lt`/`le`/`gt`/`ge`'s real 2-arg case dispatches to `sx_lt`/`sx_le`/`sx_gt`/`sx_ge` for a symbolic (CAS) operand — logic the raw opcodes' existing fixnum-or-`num_lt` bodies never replicated, so reusing them for an open-coded `(< x 5)` on a sym-var `x` would have silently returned the wrong thing instead of a real symbolic comparison node. Fixed by calling the real primitive function directly on a match instead, for all eight operators uniformly (same as car/cdr/cons/pair?/null?'s own design). Caught one self-inflicted test bug while landing this (not a product bug): a new test that redefines `<`/`+` via `run_vm_script` runs *before* an unrelated new symbolic-dispatch test in the same test function, and both defines and later calls share ONE real, process-wide `GLOBAL_ENV` across the whole `curry_test` binary — the redefinition permanently poisoned `<`/`+` for every later check in the same process until the test itself was reordered (symbolic-dispatch checks now run first). 324/324 C unit tests, 107/107 ctest suites, fresh `--clear-cache` run. **Still not done**: the dominating-test analysis and true `_NOCHECK` variants that would drop the type check itself under a proven-safe context (this step, like step 1, only removes call-frame setup). |
| 2.6 | Point `src/llvm/codegen.cpp` at the IR instead of raw S-exprs; add `opt -passes=verify` on every emitted module in Debug/CI | Kaappi §4.3 — **verify-pass half already done** (found already unconditional, not just Debug/CI, at all four `codegen_*` entry points — stronger than this item asked for, nothing further needed). **IR-retargeting half: step 1 only** (branch `tier2-unchecked-primitives-1`, 2026-08-24): exposes `ir_lower()`+`ir_optimize()` as a public entry point (`compiler_ir_lower_for_jit`, compiler.h) — real groundwork, but NOT on its own something `codegen.cpp` (or anything else) can walk standalone yet, since the IR is lazily lowered by design: `IR_VAR_REF` resolution is deferred to `ir_emit` time, and a lambda's body stays raw, un-lowered S-expression until `ir_emit` walks it one statement at a time. `codegen.cpp` itself (~1400 lines) is still a complete, independent second special-form dispatcher matching raw S-expressions directly (`emit_if`/`emit_lambda`/`emit_and`/`emit_or`/`emit_cond`/`emit_do`/`emit_call`, its own `collect_free_vars`), intertwined with GC statepoint insertion the header's own comment calls "prohibitive" to retrofit once a backend is working — retargeting it to walk IR node kinds instead, correctly, without silently breaking GC safety under statepoints, is a genuinely large rewrite deliberately deferred as its own separately-scoped project, not attempted here. |

Sequencing note: 2.1 gates 2.2–2.6. Everything in Tier 2 benefits *both*
tiers once the IR feeds both.

### Tier 3 — GC (already planned; refine with §3.2 and §4.5)

- Adopt BiBOP segment structure for the P3 three-zone heap: homogeneous
  segments, page-granular type info, dirty-segment remembered sets (§3.2).
- Turn §4.5 into an explicit audit checklist for the precise-marking
  milestones (caches, module tables, handler chains, startup ordering).
- Revisit per-object `Hdr` overhead once segments carry type (pairs at 16
  bytes is a heap-size and cache-locality win).
- **Known bug, found while landing Tier 1 (2026-08-19), not yet fixed**: under
  `--gc generational` with an artificially small `--gc-nursery-size`, a script
  using `import` can hit `scc: internal error: constant of type -1 has no
  .scc serialization support` on write. The `-1` is `GC_FORWARDED`
  (`0xFFFFFFFF` misread as a signed type tag) — a stale, unfollowed pointer
  into nursery memory that's since been reused for a fresh allocation.
  Reproduces on plain `main` (predates Tier 1 entirely), only with a tiny
  nursery (not the default), and only via the tree-eval constant path
  (`import`/`define-library`/`library`) — a plain quoted-list constant under
  identical tiny-nursery settings serializes fine. Root cause is presumably a
  GC-root-registration-timing gap: the constant is added to a chunk's pool
  mid-compilation, before that chunk is reachable as a scanned root, so a
  minor GC in that window can move the underlying object without the
  constant-pool slot ever being updated to follow it. `--gc generational`
  isn't exercised by `ctest` at all currently, which is why this stayed
  invisible. Worth an explicit audit item under the §4.5 checklist above
  before `--gc generational` is anything but experimental.

### Tier 4 — continuations and the native tier (aligns with existing plans)

- Full multi-shot `call/cc` via VM frame-stack copying, once passthrough is
  small enough that all execution flows through the VM (§4.4). **Progress
  since July**: `define-library`/R6RS-library bodies (a major real-world
  source of tree-walked code — every `(curry X)`/SRFI library in the tree)
  now compile and run through the VM instead of `eval()`, and the JIT's own
  compile-failure fallback (§4.3) now routes to the VM too. What's left
  before "all execution through the VM" is actually true: `eval()`'s
  remaining real callers — the R7RS `(eval expr env)` primitive itself
  (which will always need *some* general evaluator for an arbitrary
  environment, VM or not), `scm_load` (`-l`/`(load ...)`/`(include ...)`,
  deliberately left on `eval()` — see project memory on why an unconditional
  switch there broke a real case), `modules.c`'s `load_scheme_module`, and
  `eval_body` for any tree-walker closures still in play. Two forms
  (`define-values`, `defined?`) were also found to have **zero** compiler/VM
  codegen at all — tree-walker-only even at plain top level, unrelated to
  any of the above — and block cleanly closing several of these call sites
  until they get real `compile_*` codegen.
- `call/ec` and `guard` stay on the `setjmp` fast path.
- LLVM tier compiles continuation-free functions natively; conservative
  reachability analysis; VM fallback for the rest (§4.4). This slots into the
  existing statepoint scaffolding in `src/llvm/`.
- Flonum unboxing across expressions at the IR/LLVM tier — keep the low-tag
  `val_t`, do it in the compiler like Chez (§3.6). High payoff for the
  SICM/ODE/PDE workloads.
- Chez's segmented stack (§3.1) only if benchmarks ever show capture cost
  mattering; do not front-load it.

### Explicit non-recommendations

- **No NaN-boxing migration** — the compiler-side unboxing path (§3.6) gets
  the flonum win without rewriting `val_t` and every module.
- **No register-VM rewrite** — superinstructions (1.1, 1.2) capture most of
  the dispatch win at a fraction of the cost.
- **No CPS conversion for continuations** — rejected by Kaappi for reasons
  that apply verbatim to curry (§4.4).

---

## 6. References

**Chez Scheme**
- R. K. Dybvig, *The Development of Chez Scheme*, ICFP 2006.
- R. Hieb, R. K. Dybvig, C. Bruggeman, *Representing Control in the Presence
  of First-Class Continuations*, PLDI 1990.
- R. K. Dybvig, D. Eby, C. Bruggeman, *Don't Stop the BiBOP: Flexible and
  Efficient Storage Management for Dynamically-Typed Languages*, IU TR 400, 1994.
- O. Waddell, R. K. Dybvig, *Fast and Effective Procedure Inlining*, SAS 1997.
- A. W. Keep, A. Adams, R. K. Dybvig, *Optimizing Closures in O(0) Time*,
  Scheme Workshop 2012.
- M. Flatt et al., *Rebuilding Racket on Chez Scheme*, ICFP 2019.

**Kaappi**
- Main repo: https://github.com/kaappi/kaappi
- `docs/dev/architecture.md`, `docs/dev/ir.md`, `docs/dev/bytecode.md`,
  `docs/dev/llvm-backend.md`, `docs/dev/timings.md`
- `docs/dev/decisions/continuation-strategy.md`,
  `docs/dev/decisions/self-tail-call-optimization.md`
- `docs/dev/lessons-learned.md` and `docs/dev/postmortems/`
- KEPs and TLA+ channel specs: https://github.com/kaappi/keps
