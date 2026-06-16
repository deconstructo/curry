# Garbage Collector Reference

Curry ships three GC backends, selected at startup with `--gc`:

| Backend | Flag | Character |
|---|---|---|
| **Boehm** (default) | `--gc boehm` | Conservative, non-moving, thread-safe |
| **Semispace** | `--gc semispace` | Precise, moving, Cheney copy |
| **Generational** *(experimental)* | `--gc generational` | Two-generation, Cheney minor + major |

All three implement the same `gc_ops_t` vtable; C extension modules are
unaffected by the choice.

---

## Backends

### Boehm (default)

Conservative, stop-the-world, non-moving. All allocations go through
`GC_MALLOC` / `GC_MALLOC_ATOMIC`. No configuration needed; no C-extension
source changes required.

```
curry script.scm              # Boehm (default)
curry --gc boehm script.scm   # explicit
```

### Semispace (Cheney)

Precise, stop-the-world, moving. Two equal semispaces (32 MB each by default).
Allocation uses a bump pointer in from-space. On exhaustion, or when
`(gc-collect!)` is called, all live objects are evacuated to to-space via
Cheney's algorithm, then the spaces are swapped.

```
curry --gc semispace script.scm
```

**Semispace size** is controlled by the `GC_SS_SPACE_BYTES` compile-time
constant (default `32 * 1024 * 1024`). Redefine it in `gc_semispace.h` and
rebuild to change.

**Pinned types** — allocated in Boehm and never moved: `Symbol`, `Bignum`,
`Rational`, `Mpfr`, `Port`, `Actor`, `Mailbox`, `TVar`, `Channel`,
`Continuation`, `Primitive`.

**C extension compatibility** — when the semispace backend is active, any C
code that stores a Scheme pointer in a struct field or global that lives across
a GC point must use `gc_pin` / `gc_unpin`, or register the slot with
`gc_register_root`. The FFI `with-pinned-matrix` and `with-pinned-tensor`
macros handle this automatically for BLAS/LAPACK calls.

### Generational (two-generation Cheney) *(experimental)*

> **Experimental.** The generational backend is under active development and has
> known correctness limitations (see [Known limitation](#known-limitation) below).
> Do not use in production. The default Boehm backend is stable.

Precise, stop-the-world, moving. Designed for allocation-heavy workloads (CAS
rewrites, ODE integrators, parallel map/reduce) where most objects die young.

```
curry --gc generational script.scm
curry --gc generational --gc-nursery-size 8M script.scm
curry --gc generational --gc-tenured-size 256M script.scm
```

#### Architecture

**Gen0 (nursery)** — a single shared `mmap` region (2 MB default). All
non-pinned objects are born here via a mutex-protected bump pointer. When the
nursery fills, a **minor collection** is triggered.

**Gen1 (tenured)** — a large `mmap` region (128 MB default). Live nursery
objects are promoted here by Cheney copy during minor collection. When tenured
space exceeds 85% fill, a **major collection** evacuates both generations into
a fresh tenured region.

**Write barrier** — `GC_WRITE_BARRIER(obj, field, val)` must wrap every
mutation of a `val_t` field in a heap object. It marks the 512-byte card
containing `obj` dirty when `obj` is in tenured space. Minor collection scans
dirty cards to find old→young pointers (the remembered set). Under Boehm and
semispace the macro compiles to a single store with a predicted-not-taken
branch.

**Safepoints** — polling (`gc_stop_world` flag). Threads yield at:
- nursery exhaustion (every allocation failure)
- actor `receive`/`send!` yield points
- between work items in the parallel map/reduce pool

#### Configuration flags

| Flag | Default | Effect |
|---|---|---|
| `--gc-nursery-size N` | `2M` | Nursery size; supports `K`/`M`/`G` suffixes |
| `--gc-tenured-size N` | `128M` | Tenured region capacity |

Both flags are silently ignored when `--gc` is not `generational`.

#### Pinned objects

The following types are always allocated in Boehm (non-moving) even under
the generational backend: `Symbol`, `Bignum`, `Rational`, `Mpfr`, `Port`,
`Actor`, `Mailbox`, `TVar`, `Channel`, `Continuation`, `Primitive`,
`SymVar`, `SymFn`, `SymExpr`, `WorkPool`, `WorkItem`.

#### Known limitation

The tree-walking `eval`/`apply` interpreter keeps intermediate `val_t` values
as C locals that are not tracked by any GC root (the conservative C-stack scan
is disabled pending a safe write-back strategy). Under the default 2 MB nursery,
a minor GC firing during a deep numeric computation (ODE/PDE/SICM) will corrupt
those locals, causing a crash.

**Workaround:** use a nursery large enough that no minor GC fires during the
longest Scheme→C call chain:

```
curry --gc generational --gc-nursery-size 16M tests/sicm_tests.scm
```

A future release will replace the tree-walking evaluator with a continuation-
passing or stack-mapping approach that eliminates this limitation.

---

## Scheme API

All procedures below are available without any import form.

---

### `(gc-collect!)`

Trigger an immediate collection cycle. Under Boehm calls `GC_gcollect()`; under
semispace runs a full Cheney evacuation; under generational runs a minor
collection (and a major collection if tenured space exceeds 85%). Returns
`#<void>`.

```scheme
(gc-collect!)
```

---

### `(gc-stats)`

Return a snapshot of GC statistics as an association list with symbol keys.

```scheme
(gc-stats)
; Under generational:
; ((minor-collections . 14) (major-collections . 1)
;  (nursery-bytes . 2097152) (nursery-used . 49152)
;  (tenured-used . 8388608) (tenured-capacity . 134217728)
;  (pinned-count . 651))
;
; Under semispace:
; ((collections . 3) (bytes-allocated . 1048576) (bytes-survived . 262144)
;  (from-used . 262144) (space-size . 33554432) (pinned-count . 611))
;
; Under Boehm:
; ((heap-size . 4194304) (free-bytes . 2097152) (backend . 0))
```

**Generational fields:**

| Key | Description |
|---|---|
| `minor-collections` | Minor GC cycles since startup |
| `major-collections` | Major GC cycles since startup |
| `nursery-bytes` | Configured nursery size |
| `nursery-used` | Bytes in use in the nursery |
| `tenured-used` | Bytes live in tenured space |
| `tenured-capacity` | Total tenured capacity |
| `pinned-count` | Pinned (non-moving, Boehm-managed) objects |

**Semispace fields:**

| Key | Description |
|---|---|
| `collections` | Total Cheney collections since startup |
| `bytes-allocated` | Cumulative bytes allocated via `gc_alloc` |
| `bytes-survived` | Bytes surviving the most recent collection |
| `from-used` | Bytes currently in use in from-space |
| `space-size` | Size of each semispace |
| `pinned-count` | Live objects in the pinned (Boehm) list |

---

### `(gc-on-collection proc)`

Register `proc` (a zero-argument procedure) as a post-collection hook. Called
after every collection cycle. Replaces any previously registered hook; pass
`#f` to clear. No-op under Boehm.

```scheme
(gc-on-collection
  (lambda ()
    (display "GC fired: ")
    (display (cdr (assq 'minor-collections (gc-stats))))
    (newline)))
```

> **Note:** The hook runs synchronously inside the collector, before the
> mutator resumes. Keep it short; avoid allocations in the hook body.

---

## C API

### `gc_alloc` / `gc_alloc_atomic`

Standard allocation — objects with GC-traced pointer fields use `gc_alloc`;
atomic objects (no interior pointers) use `gc_alloc_atomic`. Under semispace
and generational these go into the bump-pointer nursery.

### `gc_alloc_pinned` / `gc_alloc_pinned_atomic`

Allocate a typed Scheme object (with `Hdr`) that must never be moved. Under
Boehm identical to `gc_alloc`; under semispace/generational allocates via Boehm
and registers the object in the pinned list so its `val_t` fields are updated
after each collection.

Use `CURRY_NEW_PINNED(T)`, `CURRY_NEW_PINNED_ATOM(T)`,
`CURRY_NEW_FLEX_PINNED(T, n)` macros.

### `gc_alloc_raw_pinned`

Allocate a raw C array (no `Hdr`, no `ObjType`) in non-moving space. Not added
to the pinned list — the owning typed object's scanner is responsible for
iterating the array's `val_t` entries.

Use for `val_t[]`, `uint32_t[]`, `uint8_t[]` helper arrays owned by typed heap
objects.

### `GC_WRITE_BARRIER(obj, field_ptr, new_val)`

Must wrap every store of a `val_t` into a mutable heap object field:

```c
GC_WRITE_BARRIER(pair, &pair->car, new_val);
GC_WRITE_BARRIER(vec,  &vec->data[i], new_val);
```

Under Boehm and semispace this compiles to a single store. Under the
generational backend it additionally marks the 512-byte card containing `obj`
dirty when `obj` is in tenured space.

Mutation sites already instrumented: `set-car!`, `set-cdr!`, `vector-set!`,
`force` (promise), `parameterize`, parameter-object calls, and upvalue closing
in the bytecode VM.

### `gc_register_root(val_t *slot)` / `gc_unregister_root(val_t *slot)`

Register a `val_t` slot as a GC root. Under Boehm a no-op (conservative scan
covers it); under semispace and generational the slot is added to the root list
and updated after each collection. Call `gc_unregister_root` when the slot is
no longer valid.

### `gc_gen_register_ext_scanner(void (*cb)(void))`

Register an external root scanner callback for the generational backend.
Called during minor and major collection after the primary root evacuation
phase. The callback must call `gc_gen_evac(v)` on every `val_t` it holds,
assigning the result back:

```c
static val_t my_val = V_FALSE;

static void my_scanner(void) {
    my_val = (val_t)gc_gen_evac((uintptr_t)my_val);
}

/* Call once at module init when gc_gen_active: */
if (gc_gen_active) gc_gen_register_ext_scanner(my_scanner);
```

The semispace equivalent is `gc_ss_register_ext_scanner` / `gc_ss_evac`.

---

## Performance notes

**Boehm** — lowest friction; conservative scanning means no source annotations
needed. Pause times scale with heap size.

**Semispace** — eliminates per-object allocation overhead (bump pointer vs
`GC_MALLOC`). Pause time proportional to live objects, not heap size. Best for
allocation-heavy functional code with a small live set.

**Generational** *(experimental)* — lowest average pause for workloads with
high allocation rates and young-object mortality (CAS rewrites, list processing,
parallel map). Minor collections pause for milliseconds; major collections are
rarer and proportional to total live set. Current limitation: tree-walking
eval/apply C stack is not scanned; use `--gc-nursery-size 16M` or larger for
computation-heavy scripts until this is resolved.
