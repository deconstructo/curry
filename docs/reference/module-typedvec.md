# Module: (curry typedvec)

SRFI-4 core homogeneous numeric vectors for 8 integer kinds: `u8`, `s8`, `u16`, `s16`, `u32`, `s32`, `u64`, `s64`. One C module, one GC heap type (`T_TYPEDVEC`), one generic set of C functions parameterized over element kind — not 8 hand-duplicated implementations.

`f64vector` is deliberately **not** part of this module — curry already ships a separate, more capable `(curry f64vector)` module with its own numeric-computation surface (`f64vector-dot`, `-map`, element-wise trig/exp/log, etc). [`(srfi s4 uniform-vectors)`](srfi/s4.md) (and its `(srfi 4)`/`(srfi srfi-4)` shims) combine both modules' basic SRFI-4 operations into one import; use `(curry f64vector)` directly for its extended numeric surface. There is no `f32vector`: curry's numeric tower has no native single-precision float representation to back it with.

Build flag: `-DBUILD_MODULE_TYPEDVEC=ON` (default `ON`).

## Import

```scheme
(import (curry typedvec))
```

Or, for the combined SRFI-4 surface including `f64vector`:

```scheme
(import (srfi 4))
```

## Per-kind operations

Every kind `TAG` in `{u8, s8, u16, s16, u32, s32, u64, s64}` gets the same 12 procedures:

| Procedure | Arity | Description |
|---|---|---|
| `(make-TAGvector n [fill])` | 1–2 | New vector of `n` elements, `fill` (default 0) |
| `(TAGvector x ...)` | 0+ | New vector from the given elements |
| `(TAGvector? obj)` | 1 | `#t` iff `obj` is a `TAGvector` (kind-specific — a `u8vector` is not `s8vector?`) |
| `(TAGvector-length v)` | 1 | Element count |
| `(TAGvector-ref v i)` | 2 | Element at index `i`; raises on out-of-bounds |
| `(TAGvector-set! v i x)` | 3 | Set element `i` to `x`; raises if `x` is out of the kind's representable range or not an exact integer |
| `(TAGvector->list v [start [end]])` | 1–3 | Elements as a list |
| `(list->TAGvector lst)` | 1 | New vector from a list |
| `(TAGvector-copy v [start [end]])` | 1–3 | Copy of a sub-range (default: the whole vector) |
| `(TAGvector-copy! to at from [start [end]])` | 3–5 | Copy a sub-range of `from` into `to` starting at index `at` |
| `(TAGvector-append v ...)` | 0+ | Concatenation of all given vectors of the same kind |
| `(TAGvector-fill! v x [start [end]])` | 2–4 | Fill a sub-range with `x` |

## Range checking and the numeric tower

`TAGvector-set!` validates both type (must be an exact integer) and range against the kind's representable bounds — e.g. `(u8vector-set! v 0 300)` and `(u8vector-set! v 0 -1)` both raise, since `u8` is `[0, 255]`.

`u64`/`s64` elements are read back through curry's numeric tower, not truncated to fixnum/`long`: values exceeding `FIXNUM_MAX`/`long` range (including all of `u64`'s upper half, past `INT64_MAX`) come back as exact bignums.

```scheme
(define v (u64vector 18446744073709551615))  ; UINT64_MAX
(u64vector-ref v 0)   ; => 18446744073709551615, an exact bignum
(integer? (u64vector-ref v 0))  ; => #t
```

## External representation

`write`/`display` produce a `#TAG(...)` form, one per kind, mirroring `(curry f64vector)`'s existing `#f64(...)`:

```scheme
(display (u8vector 1 2 3))                          ; #u8(1 2 3)
(display (s64vector -9223372036854775808 5))         ; #s64(-9223372036854775808 5)
```

This is a printed *representation*, not a reader syntax — there is no `#u8(...)` literal in the reader (R7RS's `#u8(...)` bytevector literal is unrelated and produces a `bytevector`, a different type, not a `u8vector`).

## GC

Typed vectors are allocated atomically (`gc_alloc_atomic`) — the element bytes are never scanned for pointers, matching every other flat numeric buffer type in curry (`F64Vec`, `Bytevector`, `String`). Supported under both the default Boehm backend and the experimental `--gc generational` backend (`src/gc_gen.c` has explicit `T_TYPEDVEC` cases for size computation and marking it pointer-free).

## See also

- [`srfi/s4.md`](srfi/s4.md) — the combined SRFI-4 library (this module + `(curry f64vector)`)
- [`srfi/index.md`](srfi/index.md) — full SRFI availability table
