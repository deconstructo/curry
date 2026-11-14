# JIT: translate bytecode to LLVM IR — plan

Status: **plan only, nothing implemented**. Written 2026-11-15.

This replaces the Tier 2.6 plan
(`tier2-6-llvm-ir-retargeting-plan-2026-08-25.md`). That plan kept the JIT
compiling from Scheme source and moved it onto the compiler's IR. This plan
makes the JIT compile from the bytecode the VM already runs.

## Why change direction

`src/llvm/codegen.cpp` is a second compiler front end. It reads the raw
`(lambda ...)` source saved in `Chunk::src_lambda` and decides by itself what
every form means. The bytecode compiler decides the same things separately.
Each time the two disagree, the result is a JIT-only bug. Issues #111, #114,
#115, #117, #118, #291, #294, #309 and #370 are all of this kind.

The same design also keeps most code out of the JIT. `maybe_jit_bcc`
(`src/runtime.c`) refuses:

- every closure with upvalues (#291);
- every chunk compiled inside a `define-library` (`target_env` set), which is
  every SRFI and every `(curry X)` module;
- every function that uses a macro, `quasiquote`, `let-values`, `guard`,
  `parameterize`, `case-lambda`, or about 20 other forms.

Bytecode has none of these problems. Macros are already expanded. Locals,
upvalues, globals and `target_env` are already resolved. Tail calls are
already marked (`OP_TAIL_CALL`, `OP_SELF_TAIL_CALL`). A translator that
handles every opcode handles every form.

## Measurements (2026-11-15, Release build, Apple Silicon)

| Case | Bytecode | Current JIT |
|---|---|---|
| named-let `if` loop, 3e6 iterations | 155 ms | 32 ms (about 10 ns per iteration) |
| the same loop written with `when` | 176 ms | 178 ms (not compiled, #370) |
| flonum loop, 3e6 iterations | 388 ms | 129 ms |
| `vector-ref` sum, 1000 elements × 3000 | 103 ms | 96 ms |

These are the numbers each phase below must be compared against.

## Design

### One native function runs one VM frame

The translated function does what `vm_run` does for one `CallFrame`, with
the dispatch loop and operand decoding removed.

- Signature: `val_t fn(BcClosure *self, val_t *slots)`. The arguments are
  already on the VM stack at `slots[0..argc)`, exactly as the interpreter
  leaves them. Arity checking and rest-list building reuse `vm_bind_args`.
- The function pushes a real `CallFrame` (closure, slots). Backtraces,
  handler unwinding (`frame_count` restore) and `vm_close_upvalues` then work
  without special cases. JIT frames do not appear in backtraces today.
- Upvalues are the VM's own `Upvalue` objects: `OP_LOAD_UP` is
  `*self->upvals[i]->location`. `OP_CLOSURE` calls the same code as the
  interpreter and produces an ordinary `BcClosure`. Nothing is copied, so
  the #291 problem cannot occur, and `T_JITCLOSURE` and its capture arrays
  are no longer needed.
- Globals use the chunk's `glob_cache` and `target_env`, the same as
  `load_global_cached`. Library code works with no extra handling.

### The VM stack, step 1: keep it in memory

In the bytecode, locals and temporaries share one stack window. The stack
height at every instruction is known when translating (the translator
computes it; if two paths reach one instruction with different heights, the
chunk is refused). So every stack position is the fixed address
`slots + k`.

Step 1 keeps every value at that address. `vm->sp` is written only before
something that can allocate, raise, or call. This is slower than registers
but it is simple, and everything that looks at the VM stack (GC root scan,
open upvalues, exception restore) stays correct without new rules.

### The VM stack, step 2: registers for values nobody else can see

Boehm scans the native stack and registers conservatively, so a value held
only in a register is still safe from collection. A stack position can
therefore become an LLVM SSA value when:

- no `OP_CLOSURE` in the chunk captures it (`is_local` with that index), and
- it is not an argument being passed to a call (the callee reads those from
  the VM stack).

This step is where loop variables become registers. It is separate from
step 1 so that step 1 can be tested against the interpreter first.

### Calls and tail calls

- Non-tail call: store the arguments at the top of the VM stack, then call a
  helper that does what `OP_CALL` does (native function if the callee has
  one, `vm_run` otherwise, `apply_arr` for non-closures).
- `OP_SELF_TAIL_CALL`: move the arguments, close upvalues if the chunk
  captures any slot, jump to the top of the function. No call.
- Other tail calls: the function moves the callee and arguments into its own
  frame (as the interpreter does) and returns a "tail call pending" marker.
  The helper that called it runs the next callee in a loop. Tail calls then
  use constant native stack, which the current JIT does not guarantee
  (`JIT_CALL_DEPTH_LIMIT` falls back to bytecode after 512 nested calls).
- Deep non-tail recursion still uses the native stack. Keep a depth or
  stack-pointer check that raises the same `stack-overflow` condition.

### Inline fast paths

`OP_ADD`, `OP_SUB`, `OP_LT`, `OP_NUMEQ` and the others carry the symbol of
the primitive they were compiled for, and the interpreter checks on every
execution that the global still holds the real primitive. The translator
emits the same check (compare the cached slot's value with the primitive
object seen at translation time, plus the root frame version), then:

- both operands fixnum: inline add/sub with overflow check, inline compare;
- anything else, or the check fails: call the same C path the interpreter
  uses (`open_coded_binary2`).

`OP_CAR`, `OP_CDR`, `OP_NULLP`, `OP_PAIRP`, `OP_NOT`, `OP_VECREF`,
`OP_VECLEN` get the same treatment. Because the check is per site and reads
the real binding, the global `jit_arith_tainted` flag is no longer needed
for these functions.

### Shared opcode bodies

Every opcode that is not inlined calls a C helper. The helper and the
interpreter's `CASE` must run the same code. Move those bodies into
`static inline` functions in a new internal header that `vm.c` and the JIT
runtime both include. This is the rule that stops the two tiers drifting
apart again.

### Not supported at first

A chunk containing any of these is refused and stays on bytecode. The
refusal reason is recorded (see phase 0).

- `OP_PUSH_HANDLER` / `OP_POP_HANDLER`: the interpreter uses `setjmp` inside
  `vm_run`. Doing that from LLVM IR is possible but needs care. Later phase.
- Debugger active, or profiling level above 0: call sites already skip
  native code in these cases. Keep that.

Everything else (`OP_APPLY`, `OP_VALUES*`, `OP_CALL_WITH_VALUES`,
`OP_TREE_EVAL_CACHED`, `OP_DISPLAY`, ...) is a helper call.

## Decisions that need an answer from Scáth

1. **The old `codegen.cpp`.**
   - (a) Delete it when the translator passes the test suite. Also removes
     `curry-jit-eval`, `curry-jit-call`, `curry_emit_llvm` (ahead-of-time
     `.ll`/`.bc` output), `T_JITCLOSURE`, `Chunk::src_lambda` use by the JIT.
   - (b) Keep it only for `curry_emit_llvm`.
   - Recommended: (a). One front end is the whole point. Ahead-of-time
     output can be rebuilt on the translator later if it is wanted.
   - This removes builtins, so it is a breaking change.
2. **Fix #370 in the old code now, or not.** The wrong result is live in
   v1.30.1. A small fix is possible without touching the rest: before
   compiling a named let, check that every call to the loop name is in tail
   position, and refuse the function otherwise. About 60 lines.
   Recommended: yes, as its own small PR, because the translator is weeks
   away.
3. **Statepoints.** Every call is wrapped in `gc.statepoint` with no GC
   arguments. With Boehm only, and a non-moving collector as the goal, they
   do nothing. Recommended: the translator emits plain calls. If a precise
   collector is planned after all, say so before phase 1.
4. **Compile on the calling thread or in the background.** Recommended:
   calling thread at first (as now), background thread as a later phase.

## Phases

Each phase is its own PR, with a differential test: run the same program
with the JIT forced on and forced off and compare output.

- **Phase 0 — visibility and the test harness.**
  `jit-compiled?` (#371), a per-chunk refusal reason, `CURRY_JIT=off|on|force`
  (force = compile on first call), `CURRY_JIT_LOG`. A ctest mode that runs
  the existing Scheme suites under `CURRY_JIT=force`. Decision 2's fix.
- **Phase 1 — translator, memory stack, helper calls only.**
  Every supported opcode is a helper call or a load/store. Shared opcode
  bodies header. Upvalues, `target_env`, tail-call loop. Old JIT still the
  default; new one behind `CURRY_JIT_BACKEND=bytecode`. Expected speed:
  somewhat better than bytecode, worse than the current JIT on simple loops.
  The goal of this phase is correctness and coverage, measured as "percent
  of hot chunks compiled" on the test suites.
- **Phase 2 — inline fast paths.** Fixnum arithmetic and comparison, pair
  and vector access, direct global slot loads.
- **Phase 3 — register promotion** (stack step 2 above). After this the
  simple-loop numbers should be better than the current JIT.
- **Phase 4 — switch the default, remove the old code** (decision 1).
- **Phase 5 — later, each optional:** `OP_PUSH_HANDLER`; counting loop
  iterations so that a long loop in a function called once is compiled;
  background compilation; direct native-to-native calls for known global
  callees; flonum unboxing inside one expression.

## Size estimate

Not measured, estimated from reading `vm.c` (2175 lines, 81 opcode handlers):
translator about 1500 lines of C++, shared-bodies refactor of `vm.c` about
400 lines moved, runtime helpers about 400 lines. Phases 0–3 are several
sessions each. Phase 1 is the largest.

## Risks

- **Stack heights.** The plan assumes the compiler always emits code where
  the stack height at each instruction is the same on every path. If some
  form breaks this, those chunks are refused; the translator must check, not
  assume.
- **`vm->sp` discipline.** A helper that allocates while `vm->sp` is below
  a live value loses that value to the GC. Step 1 makes this easy to get
  right (always write `sp` before any helper). Step 2 makes it harder.
- **Exceptions.** A raise `longjmp`s through native frames. The handler
  restores `vm->sp`, `frame_count` and upvalues from its snapshot, which is
  already how raises through today's JIT frames work.
- **Actors.** Chunks are shared between threads. Compiled code must hold no
  per-thread state; `vm` is thread-local and must be read through a helper
  or TLS access in each function.
