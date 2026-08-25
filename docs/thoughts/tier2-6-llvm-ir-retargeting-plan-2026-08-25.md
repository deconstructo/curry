# Tier 2.6: retargeting `codegen.cpp` at the IR — plan

Status: **planning, not yet started implementing**. Written 2026-08-25 to
track this across multiple sessions — see
`docs/thoughts/performance-chez-kaappi.md` item 2.6 for the one-paragraph
summary and this file for the actual working plan.

## The goal (recap)

`src/llvm/codegen.cpp` (~1426 lines) is a complete, independent second
special-form dispatcher: it matches raw Scheme S-expressions directly
(`emit_if`/`emit_lambda`/`emit_and`/`emit_or`/`emit_cond`/`emit_do`/
`emit_call`, its own `collect_free_vars`) to emit LLVM IR, duplicating
logic that `compiler.c`'s `ir_lower`/`ir_emit` already implements once for
the bytecode VM backend. The goal is to have `codegen.cpp` walk the same
`IRNode` tree (`ir.h`) that `ir_emit` walks, instead of re-parsing
S-expressions itself — one special-form classification, shared by both
backends, less duplicated logic to keep in sync.

## The original framing was wrong — reframed here

The performance doc's original notes on this item (see the git history of
`performance-chez-kaappi.md`'s 2.6 entry before this plan) assumed the
blocker was that curry's IR is *lazily lowered by design*: `IR_VAR_REF`
resolution is deferred to `ir_emit` time, and `IR_LAMBDA`/`IR_SEQ` bodies
stay raw, unlowered S-expression until walked one form at a time. The
assumed fix was "make the IR eager" — pre-resolve every var-ref and
pre-lower every body into a real tree before any consumer walks it. That
would have been a deep rearchitecture of a pipeline that's had 7+ careful
landings (Tier 2.1–2.5) building on the *lazy* contract, each landing's
own header comments in `ir.h` documenting a real bug caught by making
resolution eager too early (see `IRNode::as.var_ref`'s own comment:
resolving eagerly at lower time let a natively-lowered sibling's
upvalue/const-pool slot registration jump ahead of an earlier
`IR_FALLBACK` sibling's own registration, silently reordering the
constant pool relative to the original interleaved `compile()` order).

**Re-reading `ir.h`'s own comments closely, plus checking `codegen.cpp`'s
actual structure, finds that eagerness was never actually the blocker for
what Tier 2.6 needs.** Two concrete findings:

1. `codegen.cpp` already has its own, fully independent scope-tracking —
   `struct CompileCtx` with `std::vector<std::unordered_map<std::string,
   Binding>> scopes` (`codegen.cpp:107,114`) — entirely separate from the
   VM `Compiler`'s `locals[]`/`resolve_local`/`resolve_upvalue` machinery.
   It was never going to reuse VM-specific resolution anyway. `IR_VAR_REF`
   holding a raw, unresolved symbol is therefore *already* the right shape
   for an LLVM consumer: LLVM's own `IR_VAR_REF` case just does
   `cc.lookup(name)` against its own `scopes` stack, exactly mirroring how
   `ir_emit`'s own `IR_VAR_REF` case does `resolve_local`/`resolve_upvalue`
   against the VM `Compiler`'s state. No pre-resolution needed at all.

2. `IR_LAMBDA`/`IR_SEQ` keeping their bodies *raw* (not pre-lowered) isn't
   a problem to route around either — `ir_emit`'s own existing pattern for
   both (`ir_lower(&child, form, ...)` immediately followed by
   `ir_emit(&child, that_node)`, one form at a time, interleaved) is
   exactly the shape an LLVM consumer can copy verbatim, substituting its
   own per-node LLVM-emission function for `ir_emit`'s bytecode emission.
   `compiler_ir_lower_for_jit` (`compiler.h`/`compiler.c`, Tier 2.6 step 1,
   already landed, **currently has zero call sites** — pure groundwork)
   already proves the shape works from a C++ call site: it creates a real
   `Compiler c` via `init_compiler(&c, NULL, "<jit>")` purely for
   `ir_lower`'s own macro/scope bookkeeping, wraps the call in
   `SCM_PROTECT` so a raised condition never crosses the C/C++ boundary as
   a raw `longjmp`, and returns a `NULL` on failure instead. That's the
   right template — it just needs to support being called *repeatedly*,
   once per form in a body, sharing one `Compiler`/arena across the whole
   function, rather than the current one-shot "lower this single
   top-level expr" contract.

**Conclusion: no IR eagerness rearchitecture is needed.** The real
prerequisite is much smaller: give `codegen.cpp` an interleaved
lower-then-consume adapter, matching `ir_emit`'s own pattern, with LLVM
emission substituted for bytecode emission. This is lower-risk (zero
changes to `ir_lower`/`ir_emit`/`ir.h`'s existing, well-tested contract)
and lower-effort than the original framing implied.

## What `IR_FALLBACK` means for this plan

`codegen.cpp`'s own header comment lists the forms it currently handles
directly: `if`, `cond`, `case`, `and`/`or`, `when`/`unless`, `begin`,
`lambda`, `define`, `let`/`let*`/`letrec`/`letrec*`, `set!`, `do`, and
ordinary calls. Cross-referencing against `ir.h`'s `IRKind` enum, only
some of these have real native IR node kinds today (`IR_IF`, `IR_SEQ`,
`IR_SET`, `IR_AND`, `IR_OR`, `IR_DEFINE` non-lambda-sugar, `IR_CALL`,
`IR_LAMBDA`, `IR_NAMED_LET`, plus `let`/`let*`/`letrec`/`letrec*`
desugaring to `IR_CALL{callee=IR_LAMBDA}`). `cond`, `case`, `when`,
`unless`, and `do` have **no native IR lowering** — `ir_lower` falls them
back whole to `IR_FALLBACK{expr}`, and `ir_emit`'s own `IR_FALLBACK` case
just calls classic `compile()` (bytecode-specific) on the raw expression.

For an LLVM consumer, `IR_FALLBACK` doesn't need `compile()` at all — its
own `IR_FALLBACK` case can call `fallback.expr` into its own *existing*
raw-S-expression handling for that form (the same `emit_cond`/`emit_do`/
etc. functions `codegen.cpp` already has), completely unchanged. This
means Phase A below (see plan) doesn't require extending native IR
coverage at all to land something real — the natively-lowered kinds are
where the actual duplication is today, and those are exactly what `ir.h`
already covers. Extending IR coverage to `cond`/`case`/`when`/`unless`/
`do` is real, valuable follow-up work (Phase B) but isn't a hard blocker.

## Phased plan

### Phase A — LLVM consumer for the already-natively-lowered IR kinds

Give `codegen.cpp` its own `ir_emit`-shaped dispatcher (working name:
`llvm_emit_ir(CompileCtx &cc, IRNode *n)`) covering exactly the kinds
`ir.h` already lowers natively, each replacing the matching hand-rolled
S-expression case in `codegen.cpp` today:

| IR kind | Replaces | Notes |
|---|---|---|
| `IR_CONST` | literal/quote handling | Direct value emission, no scope interaction |
| `IR_VAR_REF` | symbol lookup | `cc.lookup(name)` instead of VM's `resolve_local`/`resolve_upvalue` |
| `IR_IF` | `emit_if` | Structurally identical to what's there now |
| `IR_SEQ` | `begin` handling | Interleaved lower+consume per item, matching `ir_emit`'s own `IR_SEQ` case exactly (needed for internal-`define-syntax`-mid-body registration ordering — see `ir.h`'s own comment on why this can't be a pre-lowered array) |
| `IR_SET` | `set!` handling | |
| `IR_AND`, `IR_OR` | `emit_and`, `emit_or` | |
| `IR_DEFINE` | `define` handling (non-lambda-sugar case only) | Lambda-sugar `(define (f ...) ...)` still falls back to `IR_FALLBACK` today — `codegen.cpp`'s own `emit_define` keeps handling it via the fallback path until `ir_lower_define_lambda_sugar` produces `IR_DEFINE{value=IR_LAMBDA}` instead of falling back whole (check whether that landed already before assuming fallback here — `ir_lower_define_lambda_sugar` exists per compiler.c, verify it's not itself still gated) |
| `IR_CALL` | `emit_call`, `gc.statepoint` sequence | **Highest-risk migration** — every call site's `gc.statepoint` wrapping must be re-verified present after the port, one node kind at a time |
| `IR_LAMBDA` | `emit_lambda` | Interleaved lower+consume of the body, one form at a time, exactly mirroring `ir_emit`'s own `IR_LAMBDA` case — needs its own child `CompileCtx` scope AND a child VM `Compiler` (for `ir_lower`'s macro bookkeeping) created together |
| `IR_NAMED_LET`, `let`/`let*`/`letrec`/`letrec*` desugaring | `emit_let` and friends | Reuses `IR_CALL{callee=IR_LAMBDA}` machinery already built above |
| `IR_FALLBACK` | (nothing new) | Routes to `codegen.cpp`'s own existing raw-S-expression handling for that form, unchanged |

Needs a new C++-callable entry point extending `compiler_ir_lower_for_jit`'s
existing shape (`compiler.h`) to support the *interleaved, repeated*
lowering `IR_SEQ`/`IR_LAMBDA` need — one call per form, sharing one
`Compiler`/arena across an entire function body — rather than the current
one-shot "lower this single top-level expr, return, done" contract. This
is new API surface, not a change to `ir_lower`/`ir_emit` themselves.

Each node kind should land as its own small, independently testable step
(mirroring how Tier 2.1–2.5 each landed as separate, reviewed steps) —
build the differential test *first* (LLVM-JIT-compiled output/behavior for
a given form, before vs. after that kind's migration) so a regression is
caught at the exact commit that introduces it, not discovered later by
`ctest`.

### Phase A safety checklist (apply to every migrated node kind, especially `IR_CALL`)

- [ ] Every allocating call site in the new dispatch still goes through
      `emit_statepoint_call` — no bare LLVM call bypassing the statepoint
      sequence. This is the single highest-consequence thing to get
      wrong: a missed statepoint is a silent GC-safety bug (stale/moved
      pointer read after a collection), not a compile error or a crash
      that's easy to attribute to its actual cause.
- [ ] `gc_inhibit_minor()`/`gc_resume_minor()` bracketing around each
      statepoint sequence still balanced (see the `fix-callcc-shadow-stack`
      PR #71 for a fresh example of exactly this class of bug — a missing
      restore on one path, invisible until something actually depends on
      the counter being balanced).
- [ ] `opt -passes=verify` (already unconditional at all four `codegen_*`
      entry points per this doc's own earlier note) still passes on every
      migrated form's output.
- [ ] Free-variable / closure-capture analysis (`collect_free_vars`)
      still produces the same capture set for a migrated `IR_LAMBDA` as
      the old hand-rolled walk did — needs a differential check specific
      to closures that capture from 2+ enclosing scopes deep, the classic
      place an off-by-one in capture-chain walking hides.

### Phase B — extend native IR coverage (follow-up, not blocking)

Once Phase A lands and codegen.cpp is genuinely IR-driven for the common
case, `cond`/`case`/`when`/`unless`/`do` (and anything else still routed
through `IR_FALLBACK`) can get real `IRKind` entries one at a time,
following the exact same "own landing, own differential test" discipline
Tier 2.1–2.5 used. Each one removes a second hand-written S-expression
matcher (`codegen.cpp`'s) *and* — bonus — a moment where `ir_optimize`
still can't see inside that form (dead-branch elimination on a `cond`, for
instance, currently only works if that `cond` first gets rewritten to
`IR_IF` — it doesn't today). Deliberately scoped as its own follow-up
project, not required for the item's own original ask (retarget
`codegen.cpp`).

## Open questions to resolve before/while implementing Phase A

1. Does `IR_LAMBDA`'s interleaved-body walk need codegen.cpp to construct
   a *real* VM `Compiler` (as `compiler_ir_lower_for_jit` does today) for
   every nested lambda, or can a lighter-weight "lowering-only" context be
   split out? A full `Compiler` carries VM-specific fields (`chunk`,
   `known[]` inliner state, `local_count`) that are meaningless to LLVM
   codegen but harmless if simply never touched by it — the pragmatic
   answer is probably "reuse the whole struct, ignore the VM-only fields,"
   at least for Phase A, with splitting deferred as its own hygiene
   project if it turns out to matter.
2. Exception-safety: `compiler_ir_lower_for_jit`'s `SCM_PROTECT` wrapping
   converts a raise into a `NULL` return for a *single* lowering call. An
   interleaved multi-call contract needs the same guarantee to hold
   across a whole function body's worth of forms — needs its own explicit
   design (probably: the new entry point wraps its *own* `SCM_PROTECT`
   per form, same as `ir_emit`'s `IR_SEQ` case implicitly gets for free
   today by living entirely inside one `SCM_PROTECT`-guarded VM compile
   call).
3. Tier 2.3's cp0-style local inliner and Tier 2.4's wrapper elision are
   VM-bytecode-specific optimizations (`ir_emit_inline_call`, splicing
   directly into a `Chunk`) — should LLVM codegen's own `IR_CALL`/
   `IR_LAMBDA` consumer skip them entirely and let LLVM's own inliner
   (`opt` passes, post-emission) handle inlining instead? Recommend: yes,
   skip curry's own inliner for the LLVM path — duplicating Tier 2.3/2.4's
   splicing logic for a second, LLVM-SSA-shaped target is real
   unnecessary work when LLVM's mature inliner already exists downstream.
   `IR_CALL`'s "was this candidate proven closed enough for inlining"
   annotations can just be ignored by the LLVM consumer.

## Session log

- **2026-08-25**: plan written. Reframed the item away from "make the IR
  eager" (the original doc's assumption) toward "give codegen.cpp an
  interleaved lower-then-consume adapter matching ir_emit's own pattern"
  after confirming (a) codegen.cpp already has fully independent
  scope-tracking that never needed VM-specific eager resolution, and (b)
  `compiler_ir_lower_for_jit`'s existing one-shot contract is the right
  template for a repeated-call version, not something needing replacement.
  Not yet started: Phase A implementation.
