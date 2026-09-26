# Module: (curry ffi)

*v1.2.2 — 2026-06-07*

General C foreign function interface backed by libffi.  Requires `-DBUILD_FFI=ON`
at build time; links `libffi`.

```bash
# Linux
sudo apt install libffi-dev
cmake -B build -DBUILD_FFI=ON

# macOS (Homebrew keg-only, found automatically)
brew install libffi
cmake -B build -DBUILD_FFI=ON
```

---

## Import

```scheme
(import (curry ffi))
```

All `%ffi-*` primitives are in the global environment when `BUILD_FFI=ON`.
The `(curry ffi)` module adds the high-level macros on top.

---

## Loading libraries

### `(define-foreign-library name path)`

Load a shared library and bind it to `name`.

```scheme
(define-foreign-library libm  "libm.so")       ; Linux
(define-foreign-library libm  "libm.dylib")    ; macOS
(define-foreign-library libc  "libc.so.6")     ; Linux glibc
```

### `(foreign-load-library path)` → *foreign-lib*

Runtime form of the above — returns a handle without defining a variable.

### `(foreign-lib? v)` → *boolean*
### `(foreign-lib-path lib)` → *string*

---

## Declaring foreign functions

### `(define-foreign (name (param type) ...) → ret-type #:from lib)`
### `(define-foreign (name (param type) ...) → ret-type #:from lib #:c-name "sym")`

Defines a Scheme procedure that marshals arguments, calls the C function,
and returns the unmarshalled result.  Without `#:c-name`, the Scheme name is
used directly as the C symbol name.

```scheme
(define-foreign (c-sin  (x double)) → double #:from libm #:c-name "sin")
(define-foreign (c-pow  (x double) (y double)) → double #:from libm #:c-name "pow")
(define-foreign (c-strlen (s string)) → size-t #:from libc #:c-name "strlen")
(define-foreign (c-abs (n int)) → int #:from libc #:c-name "abs")

(c-sin 1.5707963)   ; → 1.0
(c-pow 2.0 10.0)    ; → 1024.0
(c-strlen "hello")  ; → 5
```

---

## Variadic C functions (printf-shaped)

### `(define-foreign (name (param type) ... #:variadic) → ret-type #:from lib)`
### `(define-foreign (name (param type) ... #:variadic) → ret-type #:from lib #:c-name "sym")`

`#:variadic` marks the end of the declared, fixed signature; the resulting
procedure takes those fixed parameters plus any number of trailing
arguments, each wrapped with `(va type value)`. Unlike a fixed-arity
`define-foreign`, C variadic calls have no static signature for the
trailing arguments — this is exactly the information a format string
like `"%d %s"` encodes implicitly for `printf`, made explicit here
because a bare Scheme value doesn't carry a C type on its own (an exact
integer could mean `int32` or `int64`; a string is always `char*`, but
plenty of other types aren't distinguishable from the Scheme value
alone).

```scheme
(define-foreign-library libc "libc.so.6")   ; Linux — see Platform notes

(define-foreign (c-snprintf (buf c-ptr) (size uint) (fmt string) #:variadic)
  → int #:from libc #:c-name "snprintf")

(define buf (make-bytevector 64 0))
(with-pinned-bytevector buf p
  (c-snprintf p 64 "%d-%s-%.2f" (va 'int 7) (va 'string "x") (va 'double 3.5)))
; → 8  (the string "7-x-3.50" is 8 bytes; p now holds it)
```

### `(va type value)`

Wraps one trailing argument with the C type it should be passed as.
`type` is a symbol from the same [type mapping](#type-mapping) table
fixed-arity `define-foreign` uses. A `'float` argument is silently
promoted to `double` — this is C's own mandatory default argument
promotion for variadic calls (libffi requires it explicitly; passing an
unpromoted `float` in a variadic slot is undefined behavior), not
something you need to do yourself.

A function not declared with `#:variadic` cannot be called with `va`
arguments, and a `#:variadic`-declared function's procedure always takes
`va`-wrapped values after its fixed parameters — mixing the two forms on
one `define-foreign` isn't meaningful, since libffi needs to know at
`define-foreign` time whether the function's calling convention is fixed
or variadic at all.

---

## Foreign callbacks (exposing a Scheme procedure to C)

### `(define-foreign-callback (name (param type) ...) → ret-type body ...)`

Defines `name` as a real, callable C function pointer backed by `(lambda (param ...) body ...)`, for handing to a C function that itself expects a callback (comparators, event handlers, visitor functions):

```scheme
(import (curry ffi))
(define-foreign-library libc "libc.so.6")   ; Linux — see Platform notes

(define-foreign (c-qsort (base c-ptr) (nmemb uint) (width uint) (compar c-ptr)) → void
  #:from libc #:c-name "qsort")

;; qsort's comparator receives two `const void*` pointing at 4-byte
;; little-endian int32 elements — decode each one from the raw bytes.
(define (peek-s32-le ptr)
  (let* ((bv (peek-bytes ptr 4))
         (u (+ (bytevector-u8-ref bv 0)
               (* 256 (bytevector-u8-ref bv 1))
               (* 65536 (bytevector-u8-ref bv 2))
               (* 16777216 (bytevector-u8-ref bv 3)))))
    (if (>= u 2147483648) (- u 4294967296) u)))

(define-foreign-callback (int-compar (a c-ptr) (b c-ptr)) → int
  (- (peek-s32-le a) (peek-s32-le b)))

(with-pinned-bytevector my-int-array base
  (c-qsort base n 4 (foreign-callback-ptr int-compar)))
```

See `tests/ffi_tests.scm` for a complete, runnable version of this example (encode helper included), verified against a real `qsort` call.

Fixed-arity only — there is no support for a callback with a variadic *incoming* signature. `ret-type` cannot be `string`/`c-string`: the C caller may hold onto a returned pointer indefinitely after the callback returns, and nothing keeps a curry-heap string reachable from Boehm GC's perspective once it's reachable only via a raw pointer buried inside C library state GC can't scan. Write into a caller-supplied buffer argument instead — the convention most real C callback APIs already use.

A C library can invoke the callback from **any thread it chooses**, including one curry never spawned or registered at all (an async callback fired from a library's own worker thread). This is handled defensively at the C level — nothing a caller needs to think about.

A Scheme exception raised inside a callback's body cannot be allowed to propagate back into the C library that invoked it (C has no concept of curry's `setjmp`-based unwinding — the result would be undefined behavior). It's caught, reported to stderr, and a default (zeroed) value is returned to the C caller instead; the VM's own state is unaffected by this — ordinary evaluation continues working normally afterward.

### `(foreign-callback-ptr cb)` → *c-ptr*

The callback's function pointer, to pass as a `c-ptr`-typed argument to a `define-foreign` procedure expecting a callback parameter.

### `(foreign-callback-free! cb)`

Releases the callback's underlying libffi closure. **Not garbage-collected** — a `<foreign-callback>` object is pinned for its whole life and never freed implicitly, because a C library that still holds its function pointer could invoke it at any time until told otherwise, and nothing can know from the Scheme side whether that's still true. Call this explicitly only once certain the C side will never invoke the callback again. Freeing a callback the C library might still call, or freeing the same one twice, is a caller error — same as it would be in C.

### `(foreign-callback? v)` → *boolean*

---

## Struct-by-value support

### `(define-c-struct name (field type) ...)`

Defines `name` as a struct-by-value type descriptor, usable as an arg-tag or ret-tag with `%ffi-make-fn`/`%ffi-call` directly (**not** with `define-foreign`'s own `(param type)` sugar — see below for why) to pass or receive a struct BY VALUE:

```scheme
(import (curry ffi))
(define-foreign-library libm "libm.so")   ; Linux — see Platform notes

(define-c-struct point (x double) (y double))
(define c-point-dot (%ffi-make-fn libm "point_dot" 'double (list point point)))

(define p (ffi-struct-make point))
(ffi-struct-set! point p 'x 3.0)
(ffi-struct-set! point p 'y 4.0)
(define q (ffi-struct-make point))
(ffi-struct-set! point q 'x 1.0)
(ffi-struct-set! point q 'y 2.0)
(%ffi-call c-point-dot (list p q))
; → 11.0
```

A struct **instance** is always a plain bytevector, exactly `(ffi-struct-size name)` bytes — there is no separate "struct instance" type. Field layout (size, alignment, and each field's byte offset, including real platform struct padding) is computed by libffi itself from each field's type — not hand-rolled, and verified against `{int32, double, int32}`'s real ABI size (24 bytes, not the naive 4+8+4=16) in `tests/ffi_tests.scm`.

**Scope limits (v1, deliberate, not oversights):**
- No field may itself be a struct type — nested structs aren't supported. Rejected with a clear error at `define-c-struct`/`%ffi-make-struct-type` time.
- A struct-type value cannot be used as an arg-tag or ret-tag with `define-foreign`'s `#:variadic` forms, or with `define-foreign-callback`. Both reject one with a clear error rather than silently mishandling it — `ffi_call_fn_variadic`'s per-call marshaling and `closure_trampoline`'s return-slot ABI handling were never made struct-size-aware, unlike the plain (non-variadic) call path.

**Why `define-foreign`'s own sugar doesn't accept a struct type directly:** every type there is a bare symbol, quoted by the macro (`'double`, `'int`, ...). A struct type is a real runtime *value* (the result of `define-c-struct`), which would need to be evaluated, not quoted — and `syntax-rules` has no clean way to tell "this token is one of the known scalar names" from "this token is a variable naming a struct type" at each parameter position. Declaring a struct-typed function's signature via `%ffi-make-fn`/`%ffi-call` directly (both already take arg-tags/ret-tag as ordinary runtime values, symbol or struct-type alike) sidesteps that rather than fighting it.

### `(ffi-struct-size struct-type)` → *fixnum*

The instance byte size.

### `(ffi-struct-make struct-type)` → *bytevector*

A fresh, zeroed instance.

### `(ffi-struct-ref struct-type instance field)` → Scheme value
### `(ffi-struct-set! struct-type instance field value)`

Read/write one field of a struct instance in place. `field` is either a 0-based exact integer index, or (for a struct type defined via `define-c-struct`, which names its fields) a symbol naming the field. `%ffi-make-struct-type` can also build an unnamed struct type directly (pass `'()` for its field-names argument) if index-only access is fine — index and name access both work on any struct type that has names.

### `(ffi-struct-type? v)` → *boolean*

---

## Type mapping

Both C-style (`size_t`) and Scheme-style (`size-t`) names are accepted.

| Scheme type | C type | libffi type |
|---|---|---|
| `void` | `void` | `ffi_type_void` |
| `int` `int32` `int32_t` `bool` | `int32_t` | `ffi_type_sint32` |
| `uint` `uint32` `uint32_t` | `uint32_t` | `ffi_type_uint32` |
| `long` `int64` `int64_t` `int64-t` `ssize_t` `ssize-t` | `int64_t` | `ffi_type_sint64` |
| `ulong` `uint64` `uint64_t` `size_t` `size-t` `uintptr` | `uint64_t` | `ffi_type_uint64` |
| `float` | `float` | `ffi_type_float` |
| `double` | `double` | `ffi_type_double` |
| `c-ptr` `pointer` | `void*` | `ffi_type_pointer` |
| `string` `c-string` | `char*` | `ffi_type_pointer` |

**Marshaling:** fixnum → int/long, flonum → double/float, string → `char*` (data pointer,
not copied), `c-ptr` → `void*`, `#f` → NULL pointer.

**Unmarshaling:** int/long → fixnum, double/float → flonum, `void*` → `T_CPTR`,
`char*` → Scheme string (copied from C), `void` → `#<void>`.

---

## Zero-copy matrix and tensor passthrough

Dense matrix and tensor data is stored as a flat `double[]` array in the heap object.
`with-pinned-matrix` and `with-pinned-tensor` extract the raw pointer, pin the object
for the duration of the body (no-op under Boehm; correct protocol for future moving GC),
and unpin on exit — even if the body raises an exception.

### `(with-pinned-matrix m var body ...)`

Binds `var` to a `c-ptr` pointing to `m`'s `double[]` data.

```scheme
; BLAS dgemm: C = alpha*A*B + beta*C
(with-pinned-matrix A pa
  (with-pinned-matrix B pb
    (with-pinned-matrix C pc
      (cblas-dgemm 101 111 111        ; RowMajor, NoTrans, NoTrans
                   (matrix-rows A) (matrix-cols B) (matrix-cols A)
                   1.0 pa (matrix-cols A)
                   pb  (matrix-cols B)
                   0.0 pc (matrix-cols B)))))
```

### `(with-pinned-tensor t var body ...)`

Same for `T_TENSOR` objects.

### `(with-pinned-bytevector bv var body ...)`

Binds `var` to a `c-ptr` pointing to `bv`'s raw bytes — a general-purpose
buffer for foreign calls the double-only matrix/tensor pinning above
doesn't cover: input arrays of a non-double element type, and
out-parameters a C function writes results into (an `int*`/`size_t*` the
callee fills in, a fixed-size struct to populate, etc.). Read the written
bytes back with `bytevector-u8-ref`, or decode multi-byte values by hand
(there is no built-in endian-aware accessor — see `(curry private
binary-io)` inside the `fits`/`netcdf` modules for a worked example of
writing one).

```scheme
; an out-parameter: an HDF5 call that writes an int32 rank into our buffer
(define rank-bv (make-bytevector 4 0))
(with-pinned-bytevector rank-bv rank-ptr
  (h5lt-get-dataset-ndims file-id "temperature" rank-ptr))
```

### `(peek-bytes ptr n)` → *bytevector*

Copy `n` bytes starting at `ptr` (a `c-ptr` or raw fixnum address) into a
fresh bytevector. For dereferencing a pointer a C function has handed
*back* to its own heap memory — e.g. a library that returns
variable-length string data as a `char*` rather than writing into a
caller-supplied buffer. Not for out-parameters; `with-pinned-bytevector`
covers those.

---

## Raw pointer utilities

### `(make-cptr address)` → *c-ptr*

Wrap a fixnum address as a `c-ptr`.

### `(cptr-address p)` → *fixnum*

Extract the raw address from a `c-ptr`.

### `(cptr-null)` → *c-ptr*

The NULL pointer (`address = 0`).

### `(cptr-null? p)` → *boolean*

`#t` if `p` is the NULL pointer.

### `(c-ptr? v)` → *boolean*
### `(foreign-fn? v)` → *boolean*

---

## Low-level primitives

These are available in the global environment when `BUILD_FFI=ON`.  The
high-level macros above are built from them.

| Primitive | Description |
|---|---|
| `(%ffi-load path)` | Load library, return `T_FOREIGN_LIB` |
| `(%ffi-make-fn lib c-name ret-tag arg-tag-list)` | Build `T_FOREIGN_FN` descriptor |
| `(%ffi-call fn args)` | Call a `T_FOREIGN_FN` with a list of arguments |
| `(%ffi-make-fn-variadic lib c-name ret-tag fixed-arg-tag-list)` | Build a variadic `T_FOREIGN_FN` — `fixed-arg-tag-list` covers only the non-variadic prefix |
| `(%ffi-call-variadic fn fixed-args variadic-typed-args)` | Call a variadic `T_FOREIGN_FN`; `variadic-typed-args` is a list of `(type-symbol . value)` pairs |
| `(%ffi-make-callback proc ret-tag arg-tag-list)` | Build a `T_FOREIGN_CALLBACK` — a libffi closure exposing `proc` as a C function pointer |
| `(%ffi-callback-ptr cb)` | The callback's function pointer, as a `c-ptr` |
| `(%ffi-callback-free! cb)` | Release the callback's closure — not GC-driven, see `foreign-callback-free!` above |
| `(foreign-callback? v)` | `#t` for a `T_FOREIGN_CALLBACK` |
| `(%ffi-make-struct-type field-tag-list field-name-list)` | Build a `T_FFI_STRUCT_TYPE` — `field-name-list` is `'()` for an unnamed (index-only) struct type |
| `(%ffi-struct-size struct-type)` | Instance byte size, as a fixnum |
| `(%ffi-struct-make struct-type)` | A fresh, zeroed instance (a bytevector) |
| `(%ffi-struct-ref struct-type instance field)` | Read one field (index or, if named, symbol) |
| `(%ffi-struct-set! struct-type instance field value)` | Write one field in place |
| `(ffi-struct-type? v)` | `#t` for a `T_FFI_STRUCT_TYPE` |
| `(%ffi-make-cptr n)` | Wrap fixnum as `T_CPTR` |
| `(%ffi-cptr-address p)` | Extract address from `T_CPTR` |
| `(%ffi-matrix-ptr m)` | `T_CPTR` to `m->data`; pins `m` |
| `(%ffi-matrix-unpin m)` | Unpin after `%ffi-matrix-ptr` |
| `(%ffi-tensor-ptr t)` | `T_CPTR` to tensor data; pins `t` |
| `(%ffi-tensor-unpin t)` | Unpin after `%ffi-tensor-ptr` |
| `(%ffi-bytevector-ptr bv)` | `T_CPTR` to bytevector's raw bytes; pins `bv` |
| `(%ffi-bytevector-unpin bv)` | Unpin after `%ffi-bytevector-ptr` |
| `(%ffi-peek-bytes ptr n)` | Copy `n` bytes at `ptr`/address into a fresh bytevector |

---

## Platform notes

**macOS:** libffi is keg-only in Homebrew — not on the default path.  The CMake
build finds it automatically via `brew --prefix libffi`.

**Linux:** install `libffi-dev` (Debian/Ubuntu) or `libffi-devel` (Fedora/RHEL).
Library names use `.so` instead of `.dylib`:
```scheme
(define-foreign-library libm "libm.so")
```

**Windows:** not yet tested; should work with `libffi.dll` and `ffi.h` on the
include path, but no CI coverage yet.

---

## When to use the FFI vs writing a C module

Both the FFI and native C extension modules let you call C code from Scheme,
but they occupy different niches.  Choosing the wrong tool creates bindings that
are either fragile (FFI used for complex APIs) or over-engineered (a full C
module written for three function calls).

### Use the FFI when

**The API is flat and primitive-typed.** The ideal FFI target is a library whose
functions take ints, doubles, strings, and opaque pointers, and return the same.
No struct traversal, no callback trampolines, no complex ownership chains.

```scheme
; Perfect FFI targets — flat, stateless, primitive types in and out
(define-foreign (fft-execute (plan c-ptr)) → void #:from libfftw)
(define-foreign (gsl-sf-bessel-j0 (x double)) → double #:from libgsl)
(define-foreign (zstd-compress-bound (src-size size-t)) → size-t #:from libzstd)
```

**You need bindings without a C compile step.** FFI bindings are pure Scheme —
no CMake target, no `-DBUILD_MODULE_X=ON` flag, no C compiler required at
binding-write time.  This makes the FFI the right tool for:
- Quick one-off bindings to a system library
- User-supplied library bindings in application code
- Thin wrappers around platform APIs

**Memory ownership is unambiguous.** The C function either (a) returns a pointer
the caller must free, (b) returns a pointer owned by the library, or (c) takes a
pointer it doesn't keep.  If you can express ownership in a single
`dynamic-wind` or a Scheme finaliser without `GC_register_finalizer`, the FFI
is sufficient.

**The library is purely computational.** No persistent sessions, no handles that
span multiple calls with teardown requirements, no error-handler registration.
Good examples: libm extensions, FFTW, libzstd, libbrotli, GSL scalar functions.

### Write a C module when

**The API is a session state machine with opaque handle chains.**  Libraries like
libldap, libgit2, libsqlite3, and GTK expose opaque handles (`LDAP *`,
`git_repository *`, `sqlite3 *`, `GtkWidget *`) that chain through sequences of
calls with complex teardown requirements.  A C module can enforce ownership and
call teardown reliably in GC finalisers; an FFI binding cannot.

```c
/* C module: finaliser ensures ldap_unbind_ext is always called */
static void ldap_conn_finalize(void *obj, void *cd) {
    ldap_unbind_ext(((LDAPConn *)obj)->ld, NULL, NULL);
}
```

**The API uses a single, straightforward callback shape.** `define-foreign-callback` (see above) covers the common case well — a comparator, a simple event/visitor callback, one function pointer handed to one registration call — including the thread-safety story (a C library can invoke it from any thread) and exception-safety (a raised error inside the callback can't crash the process). It does not solve every callback shape, though: **prefer a C module when** the API needs many interrelated callbacks with shared mutable state across calls (GTK signal handlers wiring up a whole widget tree), SASL-style multi-step callback negotiation, or a callback whose lifetime is entangled with a session handle's own teardown (SQLite aggregate functions tied to a statement's lifecycle) — a C module can express that state machine directly in C, where curry's own `(curry ffi)`-based callback is deliberately just "one Scheme procedure, one function pointer, explicit free."

**You need deep struct traversal.**  When the return value is a pointer to a
linked list of structs that must be walked and freed with a different function
for each level (`ldap_first_entry`, `ldap_next_entry`, `ldap_get_values_len`,
`ldap_value_free_len`), expressing that in Scheme via raw `c-ptr` values is
fragile.  A C module wraps each level in a typed Scheme object with its own
finaliser.

**Performance matters for tight inner loops.**  Each FFI call dispatches through
libffi's generic ABI bridge.  For a function called millions of times in a loop
(e.g., a custom allocator, a hot matrix kernel) the per-call overhead adds up.
C module primitives invoke via a direct function pointer with no libffi
indirection.

### The borderline: wrapper libraries

Some C libraries sit in the middle:

| Library | Recommendation | Reason |
|---------|---------------|--------|
| libm extensions | FFI | Flat, stateless, all primitive types |
| FFTW | FFI | Plan is a c-ptr; execute takes it; no callbacks needed |
| GSL scalar functions | FFI | Flat; complex GSL ODE/Monte-Carlo machinery → C module |
| libzstd / libbrotli | FFI | Stateless compression; streaming API → C module |
| libsqlite3 | C module | Statement objects, column iteration, aggregate callbacks |
| libldap | C module | Session state machine, struct chains, SASL callbacks |
| libgit2 | C module | Repository handles, deeply nested object graphs |
| libcurl (simple API) | FFI | `curl_easy_*` is flat; multi/async API → C module |

### Rule of thumb

If you can write the binding without storing a `c-ptr` in a data structure that
outlives the call, the FFI is probably the right choice.  The moment you find
yourself writing `(define *connection* #f)` and worrying about whether
`ldap_unbind` will be called on GC, you want a C module.

---

## Full BLAS example

```scheme
(import (curry ffi))

(define-foreign-library libblas "libcblas.so")   ; Linux — adjust for platform

; Matrix multiply: C = alpha*A*B + beta*C
(define-foreign (cblas-dgemm
  (order  int) (transA int) (transB int)
  (M      int) (N      int) (K      int)
  (alpha  double) (A c-ptr) (lda int)
               (B c-ptr) (ldb int)
  (beta   double) (C c-ptr) (ldc int)) → void
  #:from libblas #:c-name "cblas_dgemm")

(define (blas-mat* A B)
  (define C (make-matrix (matrix-rows A) (matrix-cols B)))
  (with-pinned-matrix A pa
    (with-pinned-matrix B pb
      (with-pinned-matrix C pc
        (cblas-dgemm 101 111 111
                     (matrix-rows A) (matrix-cols B) (matrix-cols A)
                     1.0 pa (matrix-cols A)
                     pb  (matrix-cols B)
                     0.0 pc (matrix-cols B)))))
  C)

(define A (matrix 2 3 '(1 2 3 4 5 6)))
(define B (matrix 3 2 '(7 8 9 10 11 12)))
(display (mat->list (blas-mat* A B)))
; → ((58 64) (139 154))
```
