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
