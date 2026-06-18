# Moving GC Design for Curry Scheme

This document specifies the design for adding a moving (semispace or generational)
GC to Curry.  The previous attempt (v1.5.0–v1.5.1) accumulated seven bug-fix
commits after the initial eight-milestone implementation, each exposing an
assumption that was not captured before coding started.  This document is the
contract to build against.

---

## 1. Problem statement

Boehm GC (the current baseline) is conservative, non-moving, and stop-the-world
per-allocation.  A moving collector would give:

- Bump-pointer allocation (O(1) vs Boehm's O(n) scan)
- Compaction, improving cache locality for linked structures
- Shorter pause variance for interactive workloads

The challenge is that Curry has two evaluators sharing the same heap:

| Evaluator  | File          | Root discipline |
|------------|---------------|-----------------|
| Tree-walker | `src/eval.c`  | C-local `val_t` variables — NO explicit stack |
| Bytecode VM | `src/vm.c`    | Explicit `vm->stack[]`, `vm->sp` — precise root |

A moving GC requires *precise* knowledge of every live reference so it can
update them after copying.  The tree-walker's C-local `val_t` variables are
opaque to our GC; Boehm finds them conservatively but cannot update them.
This asymmetry is the root cause of the previous bug cascade.

---

## 2. Fundamental invariant (the contract)

> **A collection may only occur at a safe point where every live reference to a
> moveable object is reachable through the explicit root set defined in §4.**

Any allocation that might trigger collection must be called only when the caller
has no C-local `val_t` references to moveable objects that are not already in
the root set.

The previous implementation violated this because `eval.c` holds many C-local
`val_t`s, and collection could fire at any `CURRY_NEW` call inside a builtin.

---

## 3. Object classification: move vs. pin

Every heap object type must be assigned to exactly one class.

### 3a. Moveable objects (live in the nursery / semispace)

These are *only* referenced via `val_t` (tagged pointer), never via raw C
pointer from outside the GC-managed root set.

| Type | Why moveable |
|------|-------------|
| `T_PAIR` | Pure data, no external C references |
| `T_FLONUM` | Pure data |
| `T_COMPLEX` | Pure data |
| `T_QUATERNION`, `T_OCTONION` | Pure data |
| `T_STRING` | Only referenced via `val_t` |
| `T_BYTEVECTOR` | Only referenced via `val_t` |
| `T_VECTOR` | Only referenced via `val_t` |
| `T_F64VEC` | Only referenced via `val_t` |
| `T_VALUES` | Transient multi-value wrapper |
| `T_PROMISE` | Only `val_t` fields |
| `T_PARAMETER` | Only `val_t` fields |
| `T_SYMVAR`, `T_SYMEXPR`, `T_SYMFN` | CAS nodes, only `val_t` |
| `T_SURREAL`, `T_QUANTUM` | Only `val_t` arrays |
| `T_UP`, `T_DOWN` | Tuple, only `val_t` |
| `T_MATRIX`, `T_TENSOR`, `T_MULTIVECTOR`, `T_SPINOR` | Numeric data, no external C refs |
| `T_ERROR`, `T_CONDITION`, `T_RESTART` | Only `val_t` fields |
| `T_SYNTAX` | Only `val_t` fields |
| `T_TRACED` | Only `val_t` fields |
| `T_CPTR` | Holds a `void*` but it is opaque user data, not a GC pointer |
| `T_RECORD_TYPE`, `T_RECORD` | Only `val_t` fields |
| `T_SET`, `T_HASHTABLE` | Buckets are `GC_MALLOC` arrays of `val_t` — scan in place |

### 3b. Pinned objects (always in Boehm, never moved)

Pinned means: allocated with `GC_MALLOC`/`GC_MALLOC_ATOMIC`, address never
changes, may be referenced by raw C pointer from outside the GC.

| Type | Reason for pinning |
|------|--------------------|
| `T_SYMBOL` | Interned; pointer equality is identity; `Symbol*` held in `syms[]` arrays |
| `T_BIGNUM` | `mpz_t` holds GMP-managed limb pointer; GMP allocator cannot be intercepted safely |
| `T_RATIONAL` | Same as bignum (`mpq_t` internals) |
| `T_ENV` (`EnvFrame`) | `eval.c` holds raw `EnvFrame*` C-locals that survive across allocation calls |
| `T_CLOSURE` | `eval.c` holds raw `Closure*` C-locals during parameter binding |
| `T_BCCLOSURE` | `vm.c` call frames hold raw `BcClosure*` |
| `T_CHUNK` | `BcClosure.chunk` is a raw C pointer; chunks are long-lived and shared |
| `T_UPVALUE` | `vm->open_upvalues` is a raw linked list; `location` is an interior C pointer |
| `T_MODULE` | Module registry holds raw `Module*` pointers |
| `T_PORT` | OS file descriptor; stdio buffers not GC-managed |
| `T_PRIMITIVE` | Function pointers; no heap data |
| `T_FOREIGN_LIB`, `T_FOREIGN_FN` | dlopen handle / libffi CIF; OS resources |
| `T_ACTOR`, `T_MAILBOX` | Concurrency primitives with POSIX thread references |
| `T_TVAR`, `T_CHANNEL` | Concurrency; mutex/condvar inside |
| `T_CONTINUATION` | `setjmp` buffer; must not move |
| `T_JITCLOSURE` | Native code pointer; JIT page must not move |
| `T_CPTR` (if used as raw handle) | Already in moveable list as data-only; revisit if used as handle |
| `T_INTERVAL` (MPFR) | `mpfr_t` internals; same rationale as bignum |

> **Rule**: if any C file outside the GC holds a raw typed pointer to the object,
> it must be pinned.  When in doubt, pin it.

### 3c. Pinned objects with val_t fields: the cross-heap scan list

Pinned objects that contain `val_t` fields pointing into the moveable heap must
be scanned during every collection so those fields are updated.  This list must
be **complete** — missing an entry is the class of bug that caused the previous
crash.

| Type | val_t fields to scan |
|------|----------------------|
| `T_ENV` | `f->vals[0..f->size-1]` |
| `T_CLOSURE` | `params`, `body`, `name` |
| `T_BCCLOSURE` | `jit_val`; also `upvals[i]->closed` if open upvalues are pinned |
| `T_CHUNK` | `constants[0..const_len-1]`, `src_lambda` |
| `T_MODULE` | `name`, `exports` |
| `T_ACTOR` | `closure`, `name` |
| `T_MAILBOX` | `q.msgs[head..tail-1]` |
| `T_TVAR` | `value` |
| `T_CHANNEL` | `buf[0..cap-1]` |
| `T_CONTINUATION` | `result` |
| `T_FOREIGN_LIB` | `path` |
| `T_FOREIGN_FN` | `arg_tags`, `ret_tag` |
| `T_RECORD_TYPE` | `name`, `field_names[0..nfields-1]` |

> **Implementation note**: `scan_pinned_object()` must have an explicit `case`
> for every type in this table.  The `default:` branch must be a no-op (for types
> with no `val_t` fields), **never** an `abort()` — unknown pinned types are safe
> to skip, not fatal.

---

## 4. Root set definition

The GC root set is the complete set of starting points from which the live object
graph is traced.  Every moveable object reachable from any root must survive
collection; all others are garbage.

### 4a. Explicit val_t roots

Registered via `gc_register_root(val_t *slot)`:

- `GLOBAL_ENV` (val_t holding the top-level EnvFrame — pinned, so this is
  a root to a pinned object; still register so the slot is updated if ever
  changed)
- Any module-level `static val_t` that holds a moveable object
- `vm->accumulator` if the VM has one

### 4b. VM value stack

The bytecode VM's value stack `vm->stack[0..vm->sp-1]` is scanned directly
during collection.  This covers all values currently being computed.

### 4c. Call frame raw pointers

`vm->frames[fi].closure` (raw `BcClosure*`) and `vm->open_upvalues` (raw
`Upvalue*`) must be updated if those objects move.  Since they are pinned under
this design, no update is needed — but they must still be listed here for
completeness and to catch any future reclassification.

### 4d. Pinned cross-heap objects

Every object in the cross-heap scan list (§3c) is scanned during collection as
described there.

### 4e. External module roots

Modules register an `ext_scanner` callback that evacuates any `val_t`s they hold
outside the standard object graph (e.g., the module registry table, Qt6 callback
closures, WorkPool task queues).

### 4f. What is NOT in the root set

- C-local `val_t` variables in `eval.c` or builtins — **this is why §2 exists**.
  The tree-walker must not trigger a collection while holding C-local moveables.
- GMP limb arrays — they are in GMP's heap, not ours.
- `void*` fields in `T_CPTR` — opaque user data, not GC pointers.

---

## 5. Safe point discipline

### 5a. Bytecode VM (vm.c)

The VM dispatch loop has a natural safe point at the top of each opcode: the
only live `val_t` references are in `vm->stack` (covered by §4b) and the
current instruction's operands (loaded from constants, which are in `Chunk`,
which is pinned and covered by §4d).

**Rule**: No opcode handler may hold a C-local `val_t` pointing to a moveable
object across any allocation call.  Push first, allocate, pop.

### 5b. Tree-walking evaluator (eval.c)

The tree-walker holds many C-local `val_t`s.  The safe options are:

1. **Never trigger a moving collection from the tree-walker path.**  The
   tree-walker always uses Boehm allocation (`GC_MALLOC`).  Moving GC is only
   triggered from the bytecode VM.  This is the recommended approach.

2. **Shadow-stack protocol.**  Every C-local `val_t` in `eval.c` that crosses
   an allocation is pushed to a thread-local root stack before the call.
   Complex and error-prone; not recommended.

The recommended approach (option 1) means:
- `CURRY_NEW` in builtins called from the tree-walker allocates via Boehm.
- `CURRY_NEW` in builtins called from the bytecode VM allocates from the nursery.
- Distinguish call site via a `vm_context` flag or two separate allocator APIs.

### 5c. Builtins (builtins.c, builtins_curry.c, etc.)

Builtins are called from both evaluators.  Under option 1 above:
- Builtins allocate via Boehm when called from the tree-walker.
- Builtins allocate from the nursery when called from the VM.
- The GC context is stored in a thread-local or passed explicitly.

Alternatively, pin all builtin return values (they go to Boehm), and only
cons cells / closures created by Scheme code in the VM go to the nursery.
This is conservative but correct and easy to verify.

### 5d. Qt6 callbacks (modules/qt6/qt6.cpp)

Qt6 callbacks fire on the main thread during `QApplication::exec()`.  The VM
may be mid-execution.  Rules:
- A callback that calls back into Scheme must acquire the GC lock.
- A minor/major collection must not start while a callback is executing.
- Implement via a `gc_inhibit_count` counter: callbacks increment on entry,
  decrement on exit; collection checks the counter.

### 5e. Actor threads (src/actors.c)

Each actor runs in its own POSIX thread.  Rules:
- Each actor thread has its own nursery (or uses Boehm allocation for safety).
- Cross-actor `send!` involves pinned `Mailbox` (already covered in §3c).
- The global nursery (if shared) requires a mutex around the bump pointer, or
  per-thread nurseries with a shared tenured space.

---

## 6. Write barrier (generational GC only)

A generational collector requires a write barrier: when a tenured object
stores a reference to a nursery object, the tenured object's card must be
marked dirty so the nursery collection remembers to scan it.

**Placement rule**: every `val_t` store that might write a nursery pointer into
a tenured object needs a write barrier.  The exhaustive list of mutation sites:

| Operation | VM opcode / C call |
|-----------|--------------------|
| `set!` | `OP_SET_GLOB`, `OP_STORE_UP` |
| `vector-set!` | builtin `vector-set!` |
| `string-set!` | builtin `string-set!` |
| `set-car!`, `set-cdr!` | builtins |
| `EnvFrame` val update | `frame_define`, `frame_set` |
| Upvalue close-over | `close_upvalue()` in vm.c |
| Record field set | `record-set!` builtin |
| Hash table set | `hash-table-set!` builtin |
| Mailbox enqueue | `mailbox_send` in actors.c |
| TVar set | `tvar-set!` builtin |
| Channel write | channel write in actors.c |

**Omit the barrier** when:
- The store target is known to be in the nursery (nursery→nursery stores are
  fine; they will be scanned during the next nursery collection anyway).
- The stored value is a fixnum, character, boolean, or other immediate (not a
  heap pointer).

---

## 7. Implementation milestones

Each milestone must leave the test suite at 100% before the next begins.

### Milestone 0 — Object classification audit
- Read every `T_*` type in `object.h` against §3 and mark move/pin.
- Add `static_assert` or a comment in each allocation site stating the class.
- No code changes to the GC.  Pure audit.  Test: `ctest` still 100%.

### Milestone 1 — Nursery allocator (semispace, Boehm fallback)
- Implement `gc_semispace_init`, `ss_alloc`, `ss_alloc_pinned`,
  `ss_alloc_raw_pinned`.
- Only moveable types use `ss_alloc`; pinned types use Boehm paths.
- No collection yet — just bump-pointer allocation with Boehm as overflow.
- Test: `ctest` 100%.  Add a `--gc semispace` flag that enables the nursery.

### Milestone 2 — obj_size and evacuation (no roots yet)
- Implement `obj_size()` for all moveable types.
- Implement `evacuate()` and `scan_object()` for all moveable types.
- Implement `scan_pinned_object()` for all types in §3c.
- `scan_pinned_object` default branch: `break` (not `abort`).
- No collection triggered yet.  Test: `ctest` 100%.

### Milestone 3 — Collection with VM-stack root
- Wire `ss_collect()` into `ss_alloc` overflow path.
- Root set: VM value stack only (`vm->stack[0..vm->sp-1]`).
- Test with a bytecode-VM-only test that forces collection.
- `ctest` 100%.

### Milestone 4 — Full root set
- Add `GLOBAL_ENV`, registered roots, cross-heap pinned scan (§3c), ext scanners.
- Verify with Collatz under `--gc semispace` running to completion.
- `ctest` 100%.

### Milestone 5 — Tree-walker isolation
- Enforce that `eval.c` always allocates via Boehm (never from nursery).
- Add assertion: collection count must not increase during tree-walker dispatch.
- `ctest` 100%.

### Milestone 6 — Qt6 and actor inhibition
- Add `gc_inhibit_count`; Qt6 callbacks bracket with increment/decrement.
- Actor threads use Boehm allocation or per-thread nursery (TBD).
- Test: Qt6 test suite still 100%, actor tests still 100%.

### Milestone 7 — Generational promotion (optional, separate branch)
- Only after milestones 0–6 are stable for at least one release.
- Add card table, write barriers at all sites listed in §6.
- Write barrier test: perturbation mode that forces a minor collection after
  every store and verifies no stale pointers exist.

---

## 8. Testing strategy

### Unit tests for the GC itself
- `tests/gc_semispace_tests.c` (C binary, added to ctest as `gc_semispace`):
  - Force collection with a known object graph; verify all pointers updated.
  - Fill-to-overflow test; verify Boehm fallback keeps objects alive.
  - Bignum-across-collection test: bignum in an EnvFrame survives a collection.
  - Flonum-in-env-frame test: the exact scenario that triggered the original crash.

### Regression guard
- The Collatz test (`examples/collatz.scm`) running `(test-collatz (expt 2 71) (+ (expt 2 71) 1000000))` to completion under `--gc semispace` is the acceptance criterion for milestone 4.

### Invariant checker (debug mode)
- `gc_verify()`: after every collection in debug builds, scan the entire to-space
  and confirm no pointer into from-space remains.  Abort with diagnostic if found.
  This would have caught the `T_ENV` bug immediately.

---

## 9. Files affected

| File | Change |
|------|--------|
| `src/gc.h` | Add `gc_ops_t` entries; document safe-point contract |
| `src/gc_semispace.c` / `.h` | New implementation per above |
| `src/gc.c` | Wire `--gc semispace` flag |
| `src/vm.c` | No raw C-local `val_t` across allocation (audit in M0) |
| `src/eval.c` | Enforce Boehm-only allocation (M5) |
| `src/env.c` | Allocation stays pinned; no change expected |
| `src/numeric.c` | Bignums stay pinned; no change expected |
| `modules/qt6/qt6.cpp` | `gc_inhibit` bracketing (M6) |
| `src/actors.c` | Thread registration; inhibit or per-thread nursery (M6) |
| `tests/gc_semispace_tests.c` | New test binary |
| `CMakeLists.txt` | Add new test binary; `--gc semispace` option |

---

## 10. Non-goals

- Full first-class continuations (requires CPS transform, separate project).
- Concurrent / incremental collection (tri-color marking, too much complexity).
- MPFR / `T_INTERVAL` moving (GMP constraint applies equally to MPFR).
