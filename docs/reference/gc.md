# Garbage Collector Reference

Curry uses the **Boehm conservative GC** by default and ships a second backend,
the **Cheney semispace GC**, selectable at startup with `--gc semispace`.
Both implement the same `gc_ops_t` vtable so C extension modules are unaffected.

## Backends

### Boehm (default)

Conservative, stop-the-world, non-moving.  All allocations go through
`GC_MALLOC` / `GC_MALLOC_ATOMIC`.  No configuration needed; no C-extension
source changes required.

```
curry script.scm              # Boehm (default)
curry --gc boehm script.scm   # explicit
```

### Semispace (Cheney)

Precise, stop-the-world, moving.  Two equal semispaces (32 MB each by default).
Allocation uses a per-thread bump pointer in from-space.  On exhaustion, or
when `(gc-collect!)` is called, all live objects are evacuated to to-space via
Cheney's algorithm, then the spaces are swapped.

```
curry --gc semispace script.scm
```

**Semispace size** is controlled by the `GC_SS_SPACE_BYTES` compile-time
constant (default `32 * 1024 * 1024`).  Redefine it in `gc_semispace.h` and
rebuild to change.

**Pinned types** — these are allocated in Boehm and never moved by the
semispace collector: `Symbol`, `Bignum`, `Rational`, `Mpfr`, `Port`,
`Actor`, `Mailbox`, `TVar`, `Channel`, `Continuation`, `Primitive`.

**C extension compatibility** — when the semispace backend is active, any C
code that stores a Scheme pointer in a struct field or global that lives across
a GC point must use `gc_pin` / `gc_unpin`, or register the slot with
`gc_register_root`.  The FFI `with-pinned-matrix` and `with-pinned-tensor`
macros handle this automatically for BLAS/LAPACK calls.

## Scheme API

All procedures in this section are available without any import form.

---

### `(gc-collect!)`

Trigger an immediate collection cycle.  Under Boehm this calls `GC_gcollect()`;
under semispace it runs a full Cheney evacuation.  Returns `#<void>`.

```scheme
(gc-collect!)
```

---

### `(gc-stats)`

Return a snapshot of GC statistics as an association list.

```scheme
(gc-stats)
; Under semispace:
; ((collections . 3) (bytes-allocated . 1048576) (bytes-survived . 262144)
;  (from-used . 262144) (space-size . 33554432) (pinned-count . 611))
;
; Under Boehm:
; ((heap-size . 4194304) (free-bytes . 2097152) (backend . 0))
```

**Semispace fields:**

| Key | Description |
|---|---|
| `collections` | Total number of Cheney collections since startup |
| `bytes-allocated` | Cumulative bytes allocated via `gc_alloc` |
| `bytes-survived` | Bytes surviving the most recent collection |
| `from-used` | Bytes currently in use in from-space |
| `space-size` | Size of each semispace in bytes |
| `pinned-count` | Number of live objects in the pinned (Boehm) list |

---

### `(gc-on-collection proc)`

Register `proc` (a zero-argument procedure) as a post-collection hook.  `proc`
is called after every collection cycle.  Replaces any previously registered
hook; pass `#f` to clear.  No-op under Boehm.

```scheme
(gc-on-collection
  (lambda ()
    (display "GC fired: ")
    (display (cdr (assq 'collections (gc-stats))))
    (newline)))
```

> **Note:** The hook runs synchronously inside the collector, before the
> mutator resumes.  Keep it short; avoid allocations in the hook body.

---

## C API

### `gc_alloc` / `gc_alloc_atomic`

Standard allocation — objects with GC-traced pointer fields use `gc_alloc`;
atomic objects (no interior pointers) use `gc_alloc_atomic`.  Under semispace
these go into the bump-pointer nursery.

### `gc_alloc_pinned` / `gc_alloc_pinned_atomic`

Allocate a typed Scheme object (with `Hdr`) that must never be moved.  Under
Boehm these are identical to `gc_alloc`; under semispace they allocate via
Boehm and the object is registered in the pinned list so its `val_t` fields
are updated after each collection.

Use `CURRY_NEW_PINNED(T)`, `CURRY_NEW_PINNED_ATOM(T)`,
`CURRY_NEW_FLEX_PINNED(T, n)` macros.

### `gc_alloc_raw_pinned` / `gc_alloc_raw_pinned_atomic`

Allocate a raw C array (no `Hdr`, no `ObjType`) in non-moving space.  Never
added to the pinned-list — the owning typed object's scanner is responsible
for iterating the array's `val_t` entries.

Use for `val_t[]`, `uint32_t[]`, `uint8_t[]` helper arrays owned by typed heap
objects.

### `gc_register_root(val_t *slot)`

Register a `val_t` slot as a GC root.  Under Boehm this is a no-op (conservative
scan covers it); under semispace the slot is added to the root list and updated
after each collection.  Must call `gc_unregister_root` when the slot is
no longer valid.

### `gc_ss_register_ext_scanner(void (*cb)(void))`

Register an external root scanner callback.  Called after the main root
evacuation phase and after the pinned-list scan during each semispace
collection.  The callback must call `gc_ss_evac(v)` on every `val_t` it holds,
assigning the result back, and `gc_ss_fwd(p)` for raw C pointers to semispace
objects.

```c
static void my_scanner(void) {
    for (MyEntry *e = my_registry; e; e = e->next) {
        e->val = (val_t)gc_ss_evac((uintptr_t)e->val);
        e->obj = gc_ss_fwd(e->obj);
    }
}

/* Call once at module init: */
gc_ss_register_ext_scanner(my_scanner);
```

Under Boehm `gc_ss_register_ext_scanner` is a no-op.

---

## Performance notes

The semispace backend eliminates per-object allocation overhead (a single
pointer increment vs. a Boehm `GC_MALLOC` call) and improves cache locality
for short-lived objects.  It is most beneficial for allocation-heavy functional
code (list processing, CAS rewrites, numeric computations).

Collection pause time is proportional to the number of **live** objects, not
total heap size — programs with a small live set collect cheaply regardless of
how many objects have been allocated.

The 32 MB default semispace is generous for interactive use.  For long-running
simulations that accumulate large live sets (GR geodesic integrators, rigid-body
ODE solvers), consider increasing `GC_SS_SPACE_BYTES` or switching to Boehm for
those workloads until the generational GC (Phase 6) ships.
