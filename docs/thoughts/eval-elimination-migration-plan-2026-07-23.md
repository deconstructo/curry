# Migration plan: eliminate the tree-walker, VM+JIT as sole execution engine

Private working notes. Not committed. Companion to
`architecture-review-2026-07-23.md`. Based on a full-codebase fact-finding pass
(file:line cited throughout) rather than assumption.

## The headline correction to my earlier framing

I previously said "delete eval.c, keep the VM+JIT." That's wrong in one
important way: **`eval.h`/`eval.c` is not just the tree-walker.** It is also
the home of:

- The entire exception/condition machinery: `ExnHandler`, `SCM_PROTECT`,
  `current_handler`, `scm_raise*` — used by `condition.c`, `main.c`, and
  C-extension modules with zero tree-walker involvement (`modules/lsp/lsp.c`,
  `modules/mcp/mcp.c`, `modules/qt6/qt6.cpp`, `modules/sync/sync.c`).
- `dynamic-wind`'s `WindFrame`/`current_wind` stack.
- The JIT call-depth guard (`g_jit_call_depth`, `jit_depth_push/pop/save/restore`)
  — consumed by `src/llvm/jit.cpp`/`codegen.cpp` via C-linkage specifically to
  dodge C++ TLS ABI issues.
- `apply`/`apply_arr` — the **universal call trampoline** used by `map`,
  `sort`, `for-each`, `dynamic-wind` thunks, `call/cc` invocation, the
  condition system's re-signaling, `guard`, STM, actors, FFI callbacks, and
  the VM's own `call_foreign()` (`vm.c:329-331`) for calling anything that
  isn't a VM-native `BcClosure`.
- `eval_body`, `scm_load`, `expand_qq` (the last of which `compiler.c:1163`
  calls **at compile time** to expand quasiquote — a genuine compiler
  dependency on an eval.c helper, not a legacy fallback).

None of that can be deleted. The actual deletable surface is: the `eval()`
function's special-form dispatch itself, `eval_call_cc` (used nowhere but
`eval()`'s own `S_CALLCC` case), and whatever internal-define/`letrec*` body
logic is exclusively `eval()`'s.

**Practical implication**: step one of this migration is a file split, not a
deletion. Rename `eval.c` to something like `runtime.c` (or split into
`runtime.c` + `exceptions.h`) containing everything above, and carve `eval()`
itself plus its private helpers into a shrinking, clearly-doomed unit. This
de-risks everything downstream because it stops "eval.h" from silently
gatekeeping code nobody thinks of as "the tree-walker."

## The real scope: a dozen special forms the VM doesn't compile natively

This is the part that changes the size of the task. The VM does **not**
already handle all special forms as bytecode. `compiler.c:1255-1269` punts a
fixed list straight to the tree-walker **at runtime, unconditionally**, by
compiling them into `(tree-eval '<form>)` — i.e. `OP_LOAD_GLOBAL "tree-eval"`
+ `OP_CALL`, which calls `prim_tree_eval()` (`builtins_curry.c:1015`), which
calls `eval(av[0], GLOBAL_ENV)`. This is not a bootstrap-only dependency —
**any compiled program that uses these forms calls into eval() every time it
runs them**:

```
import, define-syntax, let-syntax, letrec-syntax, define-record-type,
define-library, library, receive, syntax-rules, symbolic, define-rule,
define-ruleset, define-algebra, with-assumptions
```

So "make the VM canonical" already silently depends on eval() staying alive
for this list, independent of the `modules.c` bootstrap-time calls I flagged
earlier. Porting these to compiler-native codegen is the bulk of the real
work, not a footnote.

Two macro-expansion mechanisms exist today, and only one of them is already
VM-native, which narrows what's actually missing:

- **Macro *use-site expansion* is already VM-native.** `compiler.c:1271-1290`
  independently looks up the head symbol in `GLOBAL_ENV`, checks
  `vis_syntax`, and calls `apply(transformer, ...)` directly at compile time
  — no `eval()` involved. Good: nothing to do here.
- **Macro *registration* (`define-syntax` itself) is 100% eval()-dependent**,
  either directly (`eval.c:735-743`, `S_DEFINE_SYNTAX`) or via the
  compiler's `tree-eval` escape hatch for compiled code. This is what
  actually needs a VM-native replacement.
- `sx_rules.c`'s separate `define-rule`/`define-ruleset` mechanism (distinct
  from R7RS `syntax-rules`) is **entirely** an `eval()`/`S_DEFINE_RULE`
  construct (`eval.c:1234-1299`) with no compiler-side equivalent at all —
  a third macro-like system, smaller blast radius (used by CAS rewrite
  rules), but needs the same treatment.

## The open design question: module environment isolation

`modules.c` achieves R7RS/R6RS library isolation purely through **environment-frame
topology**, not through any export-list filtering (which, per the earlier
review, is currently broken anyway — `modules.c:436` discards `exports`
entirely):

- `load_scheme_module()` (`modules.c:151,158`): `env_extend(GLOBAL_ENV)` — a
  *child* frame, so definitions see globals implicitly.
- `modules_define_library()` / `modules_define_r6rs_library()`
  (`modules.c:426,446,472,478,496`): `env_new_root()` — a *root* frame with
  **no** implicit visibility into `GLOBAL_ENV`; the body must `import`
  everything explicitly. This is the actual mechanism giving R7RS libraries
  their isolation today.

The VM has no equivalent concept. `vm_eval()`'s own comment says it plainly
(`vm.c:1163-1165`): the `env` argument is accepted "for API compatibility
with the tree-walker but is ignored; all global operations use `GLOBAL_ENV`."
Compiled bytecode's `OP_LOAD_GLOBAL`/`OP_STORE_GLOBAL`/`OP_DEF_GLOBAL` are
hard-wired to the one real global environment (`vm.c:544,551,563,569,582,586,587`).

So swapping `modules.c`'s `eval()` calls for `compiler_compile()`+`vm_run()`
is **not a mechanical swap** — it changes library isolation semantics unless
something replaces what `env_new_root()` currently buys you.

Two ways to close this gap:

**(a) Teach the compiler/VM to compile against an arbitrary "globals" env**,
parameterizing what `OP_*_GLOBAL` targets per compiled chunk. Preserves
current semantics exactly. More invasive — touches the compiler's global-op
codegen and the VM's global-op handlers, and needs per-chunk env plumbing
through `CallFrame`/`Chunk`.

**(b) Always compile against the one real `GLOBAL_ENV`, and make the
already-broken export/import list the actual isolation mechanism** — i.e.
fix the `modules.c:436` bug (exports currently discarded) as part of this
work, and have `modules_import()` be the sole gatekeeper of what a library
exposes, rather than relying on frame-chain topology to hide bindings.

I'd take (b). It's simpler, it doesn't require plumbing a new "which globals"
concept through the compiler/VM, and it turns a bug you already need to fix
(no real encapsulation) into the mechanism that makes this migration
semantics-preserving instead of semantics-changing. The risk to manage: audit
every place that currently relies on `env_new_root()`'s *implicit* opacity
(no accidental global leakage into a library body) and make sure explicit
import-list filtering actually replaces it — don't assume "fix the export bug
and it's equivalent," verify it against `r6rs_tests.scm`/`r7rs_tests.scm`'s
library tests specifically.

## call/cc: low risk, already almost done

`eval_call_cc()` (`eval.c:267-292`) and `prim_call_cc()` (`builtins.c:1709-1732`)
capture/restore the *same* three VM fields (`frame_count`, `sp`,
`open_upvalues`) via the same `setjmp`/pinned-`jmp_buf` mechanism — the only
real difference is `eval_call_cc` tolerates `vm == NULL` (pure tree-walker
mode, used by `test_core.c` which skips `vm_init`) and uses list-based
`apply()` instead of array-based `apply_arr()`. `eval_call_cc` has exactly one
caller (`eval()`'s own `S_CALLCC` case) and zero external dependents. Once
`eval()` is gone, delete `eval_call_cc` outright; `prim_call_cc` is already
the correct, sole implementation. This is the cheapest part of the whole plan.

Continuation *invocation* (the `longjmp` trigger on `vis_cont(proc)`) already
lives in `apply()`/`apply_arr()`, which are being kept (see above), so nothing
changes there.

## apply_arr and the JIT fast path: relocate, don't duplicate further

`apply_arr`'s JIT fast path (`eval.c:1674-1695`) and the VM's own inline JIT
dispatch in `OP_CALL`/`OP_TAIL_CALL` (`vm.c:813-887`, specifically
`vis_jitclosure` checks at lines 820/874) are **already a duplicated pair**,
independent of this migration — `apply_arr` needs its own copy because
primitives that call back into JIT-compiled closures (`map`, `sort`
comparators, etc.) go through `call_foreign()` → `apply_arr()`, not through
the VM's bytecode dispatch loop directly. This duplication has to survive the
migration (it's inherent — two different call paths reach JIT-compiled code),
but it should not gain a *third* copy. When `apply_arr` is relocated out of
eval.c, do not resist the urge to "simplify" it into the VM's copy — they
serve genuinely different call sites and unifying them is a separate,
optional cleanup, not a requirement of this migration. Flag it as a followup,
don't block on it.

## Other eval() callers that need direct replacement

- **`main.c:537`, `-l` flag** and **`builtins.c:2052`, `(load)` primitive**:
  both go through `scm_load()`'s per-form `eval()` loop (`eval.c:1785-1797`).
  Normal script/REPL execution (`main.c:343-344,516-518,570-578,655-684`)
  *already* uses `compiler_compile()`+`vm_run()` — only preload (`-l`) and
  explicit `(load ...)` don't. Fix: rewrite `scm_load()` to compile+run each
  top-level form instead of `eval()`-ing it. Low risk, small surface,
  directly testable via `test_cli.sh`'s `-l` coverage.
- **`modules.c:450`, `(include ...)` clause**: same fix as `scm_load()`
  above, in fact probably becomes the same code path once `scm_load` is
  ported.
- **`src/llvm/curry_llvm.cpp:88,113`**: JIT-compile-failure fallback
  currently calls `eval()`. Should fall back to `vm_run()`/`vm_eval()`
  instead (compile-to-bytecode-and-interpret, rather than tree-walk) — this
  is a straightforward swap since a JIT-compile failure by definition has a
  successfully-compiled bytecode form already in hand (bytecode is the input
  to JIT compilation), so there's no need to *re-derive* anything, just call
  the bytecode interpreter instead of eval().
- **`builtins.c:2729`, MPFR `with-precision` bootstrap macro**: one hardcoded
  `eval()` call at C-level init time to install a single macro. Once
  `define-syntax` registration is VM-native (see special-forms section
  above), this becomes a call into whatever that new mechanism is, or can be
  hand-registered in C directly (it's installing one fixed macro — probably
  cheaper to just construct the `Syntax`/transformer object directly in C
  than to route it through any Scheme-level mechanism at all).

## Suggested phase order

1. **File split**: rename/reorganize `eval.c`/`eval.h` into a surviving
   runtime unit (exceptions, dynamic-wind, JIT-depth, `apply`/`apply_arr`,
   `eval_body`, `expand_qq`) and an isolated, shrinking tree-walker unit
   containing only `eval()`'s dispatch and its exclusively-internal helpers
   (`eval_call_cc`, internal-define handling). No behavior change — pure
   reorganization, run the full `ctest` suite to confirm zero regressions.
   This is the safest possible first commit and immediately clarifies the
   real deletable surface for everyone (including future-me) reading the code.

2. **Fix the module export bug** (`modules.c:436`) as a prerequisite for
   option (b) above — needed regardless of this migration, and turns the
   isolation-semantics question from "design decision + bug fix" into just
   "design decision."

3. **Port the tree-eval-punted special forms to compiler-native codegen**,
   one at a time, gated by existing suites:
   - `receive` — simplest, pure syntactic sugar over `call-with-values`.
   - `define-record-type` — self-contained, well-tested
     (`r7rs_tests.scm`/`r6rs_tests.scm`).
   - `define-syntax`/`let-syntax`/`letrec-syntax`/`syntax-rules` — the core
     macro-registration path; gate on `syntax_rules_tests.scm`.
   - `define-rule`/`define-ruleset`/`define-algebra`/`symbolic`/
     `with-assumptions` — CAS-adjacent, gate on `numeric_ext_tests.scm`.
   - `import`/`define-library`/`library` — hardest, tied directly to the
     module-isolation design question above; do this last, once (2) is done
     and the isolation approach is proven on the simpler forms first.

4. **Rewrite `scm_load()`** to compile+run per form instead of `eval()`-ing;
   fixes `-l`, `(load ...)`, and `modules.c:450`'s `(include ...)` in one
   pass. Gate on `test_cli.sh`.

5. **Rewrite `modules.c`'s four `eval()` call sites** (158, 446, 478, 496)
   to compile+run against `GLOBAL_ENV`, relying on the now-fixed import/export
   filtering for isolation instead of `env_new_root()`/`env_extend()`
   topology. Gate hard on `r7rs_tests.scm` and `r6rs_tests.scm`'s
   library-specific assertions — this is the step most likely to introduce a
   subtle semantic regression (accidental global visibility inside a library
   body that used to be isolated).

6. **Swap `curry_llvm.cpp`'s fallback** from `eval()` to `vm_run()`.

7. **Delete `eval()`'s dispatch and `eval_call_cc`** once no caller remains
   (verify via grep, not memory — re-run the same call-site search from this
   research pass). `prim_call_cc` remains the sole call/cc. `apply`/`apply_arr`
   stay, relocated to the surviving runtime file from step 1.

8. **Optional followup, not required**: reconcile `apply_arr`'s JIT fast path
   with the VM's inline `OP_CALL`/`OP_TAIL_CALL` copy — genuinely two
   different call sites reaching JIT code, worth a shared helper but not a
   blocker for calling this migration done.

## Test gates per phase

Run the full `ctest --test-dir build` suite after every phase, not just at
the end — particularly `syntax_rules`, `scheme_r7rs`, `scheme_r6rs`,
`numeric_ext`, `cli`, and `dynamic_wind` (the last because `dynamic-wind`
interacts with both `call/cc` and the exception machinery being relocated in
phase 1). `jit_tests.scm` (referenced in `tests/`) should also gate phases 1
and 8 specifically since those touch the JIT call-depth guard and fast path.

## What NOT to do

- Don't try to unify `eval_call_cc`'s VM-optional (`vm == NULL`) tolerance
  into `prim_call_cc` — nothing needs a VM-less call/cc after `eval()` is
  gone; `test_core.c`'s `vm_init`-skipping mode, if it still needs to run
  without a VM at all, needs a different accommodation than resurrecting
  tree-walker semantics.
- Don't fold `apply_arr`'s JIT dispatch and the VM's inline copy together as
  part of this migration — real but separate work (see phase 8).
- Don't treat `modules.c`'s four `eval()` sites as "the scope" of this
  migration, as I initially implied — the dozen tree-eval-punted special
  forms in `compiler.c:1255-1269` are the larger and more load-bearing part.
