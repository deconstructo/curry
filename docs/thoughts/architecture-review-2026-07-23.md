# Critical architecture review — curry (2026-07-23)

Private working notes. Not committed, not for publication. Compiled from a four-way
codebase survey (core/numeric/GC, evaluator/VM, builtins/FFI/actors/modules,
bolt-on protocol modules) plus manual synthesis.

## The single biggest issue: two evaluators

`src/eval.c` (1799 lines) is a full tree-walking interpreter with TCO via `goto`,
handling all ~66 special forms. `src/compiler.c` + `src/vm.c` (2500+ lines combined)
independently re-implement the *same* special forms as bytecode. But `main.c` shows
the REPL, script loading, and every top-level entry point go through the
compiler+VM path — `eval()` is now only reached via the Scheme `eval` primitive
and module bootstrapping. A second full interpreter is being maintained for what
is effectively a fallback used by one builtin.

Worse: **`call/cc` is implemented twice, independently, with different semantics.**
`eval_call_cc()` does `setjmp` against the tree-walker's C stack; `prim_call_cc()`
does `setjmp`/`longjmp` against VM frame/stack state. Two divergent continuation
models under one language feature. Calling `eval` from inside VM-compiled code
and then invoking a continuation across that boundary is likely UB. This is the
kind of thing that produces a bug report months from now that takes a day just
to localize.

Recommendation: pick the VM as canonical, reduce `eval()` to a thin
"compile the form, run it on the VM" shim — the same thing `main.c` already does
at the top level. Deletes ~1500+ lines, removes a whole class of divergence bugs.
Cost: `eval` gets slightly slower per call (compile step) — likely a good trade
since `eval` isn't the hot path.

## Numeric tower: real duplication, one likely-dead-code bug

- `num_add`/`num_sub`/`num_mul`/`num_div` in `numeric.c` each hand-roll the same
  ~20-line promotion cascade (tuple → symbolic → quantum → surreal → complex →
  quat → oct → scalar). Should be one dispatch table driven by a rank tag, not
  four hand-synced copies. A comment about ordering ("tuple before symbolic") is
  duplicated verbatim at two call sites — a sign it's already drifting.
- `surreal.c` and `quantum.c` both reimplement "flat val_t[2n] pairs + linear
  scan/merge" independently — same shape, no shared sparse-vector abstraction.
- `sort_terms` in `surreal.c` is O(n²) insertion sort used inside multiplication
  where n can hit 4096 — each comparison can hit GMP for bignum exponents. Swap
  for `qsort`; free performance win, no design risk.
- `add_fix` in `numeric.c:345-358` has a dead first pass (computes into `z`,
  discards it, redoes the computation three lines later) — leftover from an
  edit, harmless but a five-minute cleanup.
- **`src/gc_generational.c` (1230 lines) and `src/gc_semispace.c` (919 lines)
  sit in the tree, fully written, and are not wired into `CMakeLists.txt` at
  all.** Only `gc_gen.c` is built, and only behind a non-default `--gc
  generational` flag. ~2100 lines of dead GC code from the reverted GC rewrite.
  Delete or branch it off — it's exactly the kind of thing that will confuse
  future-me (or an agent) grepping for "the" GC implementation.

## Modules: a real correctness bug, not just style

`modules.c:436` — the `export` clause of `library`/`define-library` is parsed
and then discarded (`(void)exports;`). `mod->exports` is always nil. **Every
binding in a library is importable regardless of what it declares as
exported.** Not an elegance nitpick — module encapsulation silently doesn't
exist. Worth fixing before it surprises me in a package-manager future.

Smaller findings:
- `map`/`reduce`/`for-each/par` in `builtins_curry.c` are three near-identical
  dispatch/threshold/submit/wait blocks — collapsible into one helper
  parameterized by a `WorkKind` + combine callback.
- Actors are 1:1 OS threads with 8MB stacks (`actors.c`), not M:N — fine for
  hundreds of actors, bad for thousands; `actors.h`'s doc comment claims M:N
  and is currently wrong.
- Two independent "register a builtin" helpers exist (`defprim` in builtins.c,
  `ffi_def` in ffi.c) doing the same job.

## Protocol-parsing: duplicated across three bolt-on modules

MCP, Neo4j, and LSP each parse their own framed-message protocol from scratch.
MCP and LSP both hand-roll their own recursive-descent JSON parser — despite a
`(curry json)` module already existing. They're near-identical copies, and
LSP's copy has a stack-depth guard that MCP's doesn't — a bug fixed in one fork
never made it into the sibling. Neo4j adds a third bespoke framing/codec layer
(justified there — PackStream, not JSON — but still a third "read framed
messages off a stream" implementation with zero shared abstraction). Next time
either module is touched, factor out a shared framed-stream reader and reuse
`(curry json)` for MCP/LSP.

Also: the debugger reports errors via `fprintf(stderr, ...)` instead of the
condition system every other module (MCP, Neo4j, RPi) uses — debugger failures
can't be caught by Scheme-level `guard`, unlike everything else.

## Scope and coupling

The Akkadian/cuneiform layer is the most structurally invasive "flavor" piece:
`akk_translate()` is a **linear scan** over a synonym table, called on the
operator of every single form evaluated, in *both* the tree-walker and the
compiler — paid by every program that never uses a word of Akkadian. O(n) over
up to 256 entries, called multiple times per form during compilation. Should be
resolved once at read/intern time (pointer-identity check, O(1)) rather than
rescanned per dispatch per engine. It's wired directly into `reader.c`'s core
tokenizer rather than living as a pluggable layer.

Zooming out: CAS, surreal numbers, Clifford algebra, quantum superposition,
Akkadian, sexagesimal/cuneiform numerals, an MCP server, a Neo4j Bolt client,
LSP, a Qt6 GUI layer, and planned RPi GPIO is a lot of surface area for one
interpreter. Qt6 alone is 2719 lines of C++ in an otherwise pure-C codebase and
needs its own build variant. None of this is inherently wrong for a
maximalist, exotic Scheme — but the numeric-tower duplication above suggests
the breadth is already outpacing the shared infrastructure that would make it
cheap to maintain.

## If I had to prioritize three things

1. **Fix the module export bug** (`modules.c:436`) — real correctness/security
   gap (no actual encapsulation), small fix.
2. **Decide the eval.c-vs-VM question** — delete the tree-walker in favor of a
   VM shim, or explicitly accept dual-maintenance and document why. The two
   independent `call/cc` implementations are the concrete risk.
3. **Delete the dead GC files** (`gc_generational.c`, `gc_semispace.c`) or
   branch them off — pure noise for future work in this repo.

Everything else (numeric dispatch duplication, JSON parser triplication,
akk_translate scan) is real but lower urgency — worth doing opportunistically,
not a dedicated sprint.
