# Garbage Collector Reference

Curry has two GC backends selectable at startup via `--gc`:

| Flag | Backend | Moving? | Default? |
|------|---------|---------|---------|
| `--gc boehm` | Boehm conservative GC | No | Yes |
| `--gc generational` | Generational (nursery + Boehm tenured) | Nursery only | No |

Both implement the same `gc_ops_t` vtable, so C extension modules work unchanged under either backend.

---

## Boehm (default)

Conservative, stop-the-world, non-moving. All allocations go directly through
`GC_MALLOC` / `GC_MALLOC_ATOMIC`. No configuration required.

```
curry script.scm              # Boehm (default)
curry --gc boehm script.scm   # explicit
```

The conservative scan covers the C stack, global data segments, and all
`GC_MALLOC_UNCOLLECTABLE` blocks. No write barriers. No root registration
required for stack-resident pointers.

---

## Generational

A two-generation design:

- **Gen 0 (nursery):** per-thread bump-pointer slab (default 512 KB, `GC_MALLOC_UNCOLLECTABLE`). Allocation is a single pointer increment — no lock, no function call.
- **Gen 1 (tenured):** Boehm GC. Objects promoted from the nursery via `memcpy` into `GC_MALLOC` blocks.

```
curry --gc generational script.scm
curry --gc gen script.scm          # shorthand
```

### Minor GC

A minor collection fires when the nursery is exhausted and the thread is at a
safe-point (between bytecode instructions, outside the tree-walking evaluator).
It runs stop-the-world under a `GC_disable()` / `GC_enable()` bracket:

1. Snapshot nursery extent.
2. Evacuate shadow-stack roots (tree-walker eval frames).
3. Evacuate VM value-stack slots.
4. Evacuate registered `val_t` roots.
5. BFS drain — scan promoted objects for nursery references.
6. Scan pinned objects for cross-heap (tenured → nursery) references.
7. Call registered external scanner callbacks.
8. Second BFS drain.
9. Zero and reset nursery.
10. Clear write-barrier card table.

Boehm manages the tenured generation; a major Boehm collection is triggered
as normal by allocation pressure after `GC_enable()` returns.

### Object classes

**GC:MOVE** — allocated in the nursery, evacuated to Boehm on minor GC:
`Pair`, `Vector`, `String`, `Bytevector`, `Flonum`, `Complex`,
`Quaternion`, `Octonion`, `F64Vec`, `Values`, `Matrix`, `Tensor`,
`Multivector`, `Spinor`, `SymVar`, `SymExpr`, `Surreal`, `Quantum`,
`Tuple (up/down)`, `Record`, `RecordType`, `Set`, `Hashtable`,
`Promise`, `Parameter`, `Traced`, `Syntax`, `ErrorObj`, `Condition`,
`Restart`, `SymFn`, `CPtr`.

**GC:PIN** — allocated directly in Boehm (`GC_MALLOC_UNCOLLECTABLE`), never
moved: `Symbol`, `Bignum`, `Rational`, `Mpfr`, `Port`, `Primitive`,
`BcClosure`, `Chunk`, `Upvalue`, `EnvFrame`, `Closure`, `Module`,
`Actor`, `Mailbox`, `TVar`, `Channel`, `Continuation`,
`ForeignLib`, `ForeignFn`, `JitClosure`.

Pinned objects are registered in an internal `pinned_slots` array so that
their `val_t` fields are updated during each minor GC.

### Safe-point discipline

Minor GC only fires when **both** conditions hold:

- `gc_shadow_stack == NULL` — the tree-walking evaluator is not active.
- `gc_inhibit_count == 0` — no `gc_inhibit_minor()` / `gc_resume_minor()` guard is held.

The bytecode VM dispatch loop is the primary safe-point. Allocations that
occur inside the reader, compiler, or tree-walker fall back to Boehm directly.

### Nursery size

The default 512 KB nursery holds roughly 16 000 pairs. It can be adjusted
with `--gc-nursery-size BYTES`:

```
curry --gc generational --gc-nursery-size 4194304 script.scm  # 4 MB
```

### Known limitations

**Pinned objects are never freed by Boehm.** `GC_MALLOC_UNCOLLECTABLE`
blocks (BcClosure, Upvalue, Chunk, etc.) accumulate for the process lifetime.
Programs that create closures in tight loops will leak memory proportional to
the number of closures created. A `gc_gen_unpin` / `GC_FREE` mechanism is
planned for a future release.

**Bignum-heavy workloads are slower than Boehm.** The generational GC
assumes most objects are short-lived (the "generational hypothesis"). Bignum
(`T_BIGNUM`) arithmetic — such as Collatz sequences starting from large
numbers — allocates many objects that survive multiple minor GC cycles, making
evacuation expensive. Measured overhead in this regime is approximately 60%
vs. Boehm. For bignum-intensive programs, use the default Boehm backend.

**The generational hypothesis is workload-dependent.** Programs dominated
by list processing and short-lived functional values benefit from the nursery.
Programs with large live sets or long-lived allocations (numerical
simulations accumulating results, programs with many persistent actors) may
see no benefit or a small regression.

### When to use `--gc generational`

Use it for programs that:
- Build and discard many short-lived list structures.
- Do functional-style tree transformations (CAS rewrites, symbolic computation).
- Are allocation-heavy but with low survivor rates (most allocated objects
  become unreachable before the next minor GC).

Stick with Boehm (default) for:
- Bignum / arbitrary-precision arithmetic.
- Programs that accumulate large live sets.
- Programs where allocation is infrequent (most compute is on fixnums).

---

## Scheme API

All procedures below are available without any import form.

---

### `(gc)`

Force an immediate collection. Under Boehm calls `GC_gcollect()`; under
generational triggers a minor GC followed by a Boehm major collection.
Returns `#<void>`.

```scheme
(gc)
```

---

### `(gc-mode)`

Return a symbol identifying the active GC backend: `boehm` or `generational`.

```scheme
(gc-mode)   ; ⇒ boehm
```

---

### `(gc-heap-size)`

Return the current Boehm heap size in bytes (fixnum).

---

### `(gc-free-bytes)`

Return the estimated free bytes in the Boehm heap (fixnum).

---

### `(gc-total-bytes)`

Return total bytes allocated since process start (fixnum).

---

### `(gc-enable-incremental!)`

Switch Boehm to incremental (step-by-step) collection mode. Reduces
stop-the-world pause length at the cost of slightly higher overall overhead.
No-op if already incremental.

---

### `(gc-set-free-space-divisor! n)`

Set Boehm's free-space divisor (default 3). Higher values trigger GC more
aggressively, reducing heap size at the cost of more frequent pauses.

---

### `(gc-set-max-heap! bytes)`

Cap the Boehm heap at `bytes` bytes. Pass `0` for no limit (default).

---

## C API

### Allocation

```c
void *gc_alloc(size_t n);              /* GC:MOVE — goes to nursery */
void *gc_alloc_atomic(size_t n);       /* GC:MOVE, no interior pointers */
void *gc_alloc_pinned(size_t n);       /* GC:PIN — Boehm uncollectable */
void *gc_alloc_pinned_atomic(size_t n);
void *gc_alloc_raw_pinned(size_t n);   /* raw array, no Hdr, no pinned-list */
void *gc_alloc_raw_pinned_atomic(size_t n);
```

Convenience macros: `CURRY_NEW(T)`, `CURRY_NEW_PINNED(T)`,
`CURRY_NEW_FLEX(T, n)`, `CURRY_NEW_FLEX_PINNED(T, n)`.

### Root registration

```c
void gc_register_root(val_t *slot);    /* register a val_t GC root */
void gc_unregister_root(val_t *slot);
void gc_register_stack(void *base, void **sp_ptr);  /* register a val_t stack */
void gc_unregister_stack(void *base);
void gc_register_ext_scanner(void (*cb)(void));     /* external scanner */
```

Under Boehm, root registration is supplementary — the conservative scan
already covers stack and global data. Under the generational backend,
registered roots are evacuated explicitly during minor GC.

### Safe-point guards

```c
gc_inhibit_minor();   /* prevent minor GC until matching gc_resume_minor() */
gc_resume_minor();
```

Use these when C code holds raw `val_t` pointers that are not on the Scheme
stack and not covered by the shadow stack or root registry. The reader,
compiler, and several builtins use these guards internally.

---

## Roadmap

Planned improvements to the generational GC:

- **`gc_gen_unpin` / `GC_FREE`** — explicit lifetime management for pinned
  objects so that ephemeral closures and upvalues are eventually freed.
- **Pin `T_BIGNUM` / `T_RATIONAL`** — making numeric tower bignums GC:PIN
  would eliminate evacuation overhead for arithmetic-heavy programs and close
  the performance gap with Boehm.
- **Remembered set / card table scanning** — the card table infrastructure
  exists (`gc_card_table`, `gc_wb_slot`) but is compiled out unless
  `CURRY_GC_PRECISE` is defined. Enabling it would allow minor GC to scan
  only dirty cards instead of all pinned objects.
- **Green threads / concurrent GC** — long-term; requires safepoint
  coordination across multiple OS threads.
