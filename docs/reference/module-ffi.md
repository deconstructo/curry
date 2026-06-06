# Module: (curry ffi)

*v1.2.1 — 2026-06-06*

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
| `(%ffi-make-cptr n)` | Wrap fixnum as `T_CPTR` |
| `(%ffi-cptr-address p)` | Extract address from `T_CPTR` |
| `(%ffi-matrix-ptr m)` | `T_CPTR` to `m->data`; pins `m` |
| `(%ffi-matrix-unpin m)` | Unpin after `%ffi-matrix-ptr` |
| `(%ffi-tensor-ptr t)` | `T_CPTR` to tensor data; pins `t` |
| `(%ffi-tensor-unpin t)` | Unpin after `%ffi-tensor-ptr` |

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

**The API uses callbacks.** libffi can construct callback trampolines but they
require careful lifetime management and cannot capture Scheme closures directly.
If the library calls back into user code (GTK signal handlers, LDAP SASL
callbacks, SQLite aggregate functions), a C module that manages the trampoline
lifetime is far safer.

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
