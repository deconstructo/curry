;;; curry/ffi — General C foreign function interface
;;;
;;; Requires BUILD_FFI=ON at compile time (links libffi).
;;;
;;; Primitives (in global env when BUILD_FFI=ON):
;;;   %ffi-load  %ffi-make-fn  %ffi-call
;;;   %ffi-make-fn-variadic  %ffi-call-variadic
;;;   %ffi-make-callback  %ffi-callback-ptr  %ffi-callback-free!
;;;   %ffi-make-struct-type  %ffi-struct-size  %ffi-struct-make
;;;   %ffi-struct-ref  %ffi-struct-set!
;;;   %ffi-make-cptr  %ffi-cptr-address
;;;   %ffi-matrix-ptr  %ffi-matrix-unpin
;;;   %ffi-tensor-ptr  %ffi-tensor-unpin
;;;   %ffi-bytevector-ptr  %ffi-bytevector-unpin
;;;   %ffi-peek-bytes
;;;   c-ptr?  foreign-lib?  foreign-fn?  foreign-lib-path  foreign-callback?
;;;   ffi-struct-type?

(define-library (curry ffi)
  (import (scheme base))
  (export
    define-foreign-library foreign-load-library
    define-foreign
    va
    define-foreign-callback foreign-callback-ptr foreign-callback-free!
    define-c-struct
    ffi-struct-size ffi-struct-make ffi-struct-ref ffi-struct-set!
    with-pinned-matrix with-pinned-tensor with-pinned-bytevector
    peek-bytes
    make-cptr cptr-address cptr-null? cptr-null)
  (begin

;;; ── Loading libraries ────────────────────────────────────────────────────────

;;; (define-foreign-library name "path/to/lib.so")
;;; Loads the shared library at compile time (when the form is evaluated)
;;; and binds it to name.

(define-syntax define-foreign-library
  (syntax-rules ()
    ((_ name path)
     (define name (%ffi-load path)))))

;;; (foreign-load-library "path") → foreign-lib   [runtime form]
(define (foreign-load-library path)
  (%ffi-load path))

;;; ── Declaring foreign functions ──────────────────────────────────────────────

;;; (define-foreign (fn-name (p type) ...) → ret-type #:from lib)
;;; (define-foreign (fn-name (p type) ...) → ret-type #:from lib #:c-name "sym")
;;;
;;; Creates a Scheme procedure that marshals arguments, calls the C function
;;; via libffi, and returns the unmarshalled result.
;;;
;;; Supported types:
;;;   void  int  uint  long  ulong  int32  uint32  int64  uint64
;;;   int32_t  uint32_t  int64_t  uint64_t  size_t  ssize_t  intptr  uintptr
;;;   float  double  bool  c-ptr  pointer  string  c-string
;;;
;;; ── Variadic C functions (printf-shaped) ────────────────────────────────────
;;;
;;; (define-foreign (fn-name (p type) ... #:variadic) → ret-type #:from lib)
;;; (define-foreign (fn-name (p type) ... #:variadic) → ret-type #:from lib #:c-name "sym")
;;;
;;; #:variadic marks the LAST fixed parameter as the end of the declared
;;; signature; the resulting procedure takes those fixed args plus any
;;; number of extra trailing arguments, each wrapped with (va type value).
;;; C variadic calls have no static signature for the trailing arguments —
;;; this is the same information printf's own format string encodes
;;; implicitly, made explicit here because Scheme values don't carry a C
;;; type on their own (an exact integer could mean int32 or int64; a
;;; string could mean char* or something else entirely).
;;;
;;;   (define-foreign (c-printf (fmt string) #:variadic) → int #:from libc)
;;;   (c-printf "%d and %s\n" (va 'int 42) (va 'string "hi"))
;;;
;;; A 'float variadic argument is silently promoted to double, matching
;;; C's own default argument promotion rule for variadic calls — you don't
;;; need to (and shouldn't) do this promotion yourself.

(define-syntax define-foreign
  (syntax-rules (→ #:variadic)
    ;; Variadic, with explicit C name
    ((_ (fn-name (pname ptype) ... #:variadic) → ret-type #:from lib #:c-name c-name)
     (define fn-name
       (let ((ff (%ffi-make-fn-variadic lib c-name 'ret-type (list 'ptype ...))))
         (lambda (pname ... . rest)
           (%ffi-call-variadic ff (list pname ...) rest)))))
    ;; Variadic, without explicit C name
    ((_ (fn-name (pname ptype) ... #:variadic) → ret-type #:from lib)
     (define fn-name
       (let ((ff (%ffi-make-fn-variadic lib (symbol->string 'fn-name)
                                        'ret-type (list 'ptype ...))))
         (lambda (pname ... . rest)
           (%ffi-call-variadic ff (list pname ...) rest)))))
    ;; Fixed-arity, with explicit C name
    ((_ (fn-name (pname ptype) ...) → ret-type #:from lib #:c-name c-name)
     (define fn-name
       (let ((ff (%ffi-make-fn lib c-name 'ret-type (list 'ptype ...))))
         (lambda (pname ...)
           (%ffi-call ff (list pname ...))))))
    ;; Fixed-arity, without explicit C name — use Scheme name converted to string
    ((_ (fn-name (pname ptype) ...) → ret-type #:from lib)
     (define fn-name
       (let ((ff (%ffi-make-fn lib (symbol->string 'fn-name)
                               'ret-type (list 'ptype ...))))
         (lambda (pname ...)
           (%ffi-call ff (list pname ...))))))))

;;; (va type value) — wrap one trailing argument for a #:variadic
;;; define-foreign procedure, tagging it with the C type it should be
;;; passed as (there's no way to infer this from the Scheme value alone —
;;; see the #:variadic section above).
(define (va type value) (cons type value))

;;; ── Foreign callbacks (a Scheme procedure exposed to C as a real,
;;; callable function pointer) ────────────────────────────────────────────────
;;;
;;; (define-foreign-callback (name (p type) ...) → ret-type body ...)
;;;
;;; Defines name as a foreign-callback object wrapping a fresh procedure
;;; (lambda (p ...) body ...). Pass (foreign-callback-ptr name) wherever a
;;; C function parameter expects a callback function pointer.
;;;
;;;   (define-foreign-callback (compar (a c-ptr) (b c-ptr)) → int
;;;     (- (peek-s32 a) (peek-s32 b)))
;;;   (c-qsort base n 4 (foreign-callback-ptr compar))
;;;
;;; Fixed-arity only — no variadic callbacks. 'string/'c-string is not a
;;; supported ret-type: the C caller may hold onto a returned pointer
;;; indefinitely after this call returns, and nothing keeps a curry
;;; string reachable from Boehm GC's perspective once it's reachable only
;;; via a raw pointer buried inside C library state Boehm can't scan —
;;; write into a caller-supplied buffer argument instead, the way most
;;; real C callback APIs already expect.
;;;
;;; A C library can invoke the callback from ANY thread it chooses,
;;; including one curry never spawned or registered itself — this is
;;; handled defensively at the C level (see closure_trampoline in
;;; src/ffi.c), not something a caller needs to think about here.
;;;
;;; Lifetime is NOT garbage-collected: a still-referenced callback whose
;;; C function pointer a library might still call at any time can't
;;; safely be freed just because the Scheme object became unreachable.
;;; Call (foreign-callback-free! cb) explicitly once certain the C side
;;; will never invoke it again — freeing one still in use, or freeing the
;;; same one twice, is a caller error the same way it would be in C.
(define-syntax define-foreign-callback
  (syntax-rules (→)
    ((_ (name (pname ptype) ...) → ret-type body ...)
     (define name
       (%ffi-make-callback (lambda (pname ...) body ...)
                           'ret-type (list 'ptype ...))))))

;;; (foreign-callback-ptr cb) → c-ptr — the callback's own function
;;; pointer, to pass to a C function parameter expecting a callback.
(define (foreign-callback-ptr cb) (%ffi-callback-ptr cb))

;;; (foreign-callback-free! cb) — releases the callback's underlying
;;; libffi closure. See the define-foreign-callback docstring above for
;;; why this has to be explicit rather than GC-driven.
(define (foreign-callback-free! cb) (%ffi-callback-free! cb))

;;; ── Struct-by-value support ──────────────────────────────────────────────────
;;;
;;; (define-c-struct name (field type) ...)
;;;
;;; Defines name as a struct-by-value type descriptor, usable as an
;;; arg-tag or ret-tag with define-foreign's #:from lib forms via
;;; %ffi-make-fn/%ffi-call directly (define-foreign's own (p type) ...
;;; sugar is scalar-type-only -- see below for why) to pass or receive a
;;; struct BY VALUE:
;;;
;;;   (define-c-struct point (x double) (y double))
;;;   (define c-point-dot (%ffi-make-fn libm "point_dot" 'double (list point point)))
;;;   (define p (ffi-struct-make point))
;;;   (ffi-struct-set! point p 'x 3.0)
;;;   (ffi-struct-set! point p 'y 4.0)
;;;   (%ffi-call c-point-dot (list p p))  ; => 25.0
;;;
;;; A struct instance is always a plain bytevector, exactly
;;; (ffi-struct-size name) bytes -- there is no separate "struct
;;; instance" type. Field layout (size/alignment/per-field byte offset,
;;; including real platform struct padding) is computed by libffi itself
;;; from each field's type, not hand-rolled.
;;;
;;; No field may itself be a struct type (nested structs aren't
;;; supported), and a struct-type value cannot be used as an arg-tag/
;;; ret-tag with define-foreign's #:variadic forms or with
;;; define-foreign-callback -- both reject one with a clear error rather
;;; than silently mishandling it (v1 scope limits, not oversights).
;;;
;;; Why define-foreign's own (p type) ... sugar doesn't accept a struct
;;; type directly: every type there is a bare symbol, quoted by the
;;; macro ('double, 'int, ...) -- a struct type is a real runtime VALUE
;;; (the result of define-c-struct), which would need to be evaluated,
;;; not quoted, and syntax-rules has no clean way to tell "this token is
;;; one of the known scalar names" from "this token is a variable naming
;;; a struct type" at each parameter position. Declaring a struct-typed
;;; function's signature via %ffi-make-fn/%ffi-call directly (both
;;; already take arg-tags/ret-tag as ordinary runtime values, symbol or
;;; struct-type alike) sidesteps that rather than fighting it.
(define-syntax define-c-struct
  (syntax-rules ()
    ((_ name (field type) ...)
     (define name (%ffi-make-struct-type (list 'type ...) '(field ...))))))

;;; (ffi-struct-size struct-type) → fixnum -- the instance byte size.
(define (ffi-struct-size struct-type) (%ffi-struct-size struct-type))

;;; (ffi-struct-make struct-type) → bytevector -- a fresh, zeroed instance.
(define (ffi-struct-make struct-type) (%ffi-struct-make struct-type))

;;; (ffi-struct-ref struct-type instance field) → Scheme value
;;; field is either a 0-based exact integer index, or (for a struct-type
;;; defined via define-c-struct) a symbol naming the field.
(define (ffi-struct-ref struct-type instance field)
  (%ffi-struct-ref struct-type instance field))

;;; (ffi-struct-set! struct-type instance field value) → void
(define (ffi-struct-set! struct-type instance field value)
  (%ffi-struct-set! struct-type instance field value))

;;; ── Zero-copy matrix / tensor passthrough ────────────────────────────────────

;;; (with-pinned-matrix m var body ...)
;;;   Binds var to a c-ptr pointing to m's raw double[] data.
;;;   Under Boehm GC pinning is a no-op; the protocol is in place for
;;;   future moving collectors.  var must not be used after body exits.

(define-syntax with-pinned-matrix
  (syntax-rules ()
    ((_ m var body ...)
     (let* ((m* m)
            (var (%ffi-matrix-ptr m*)))
       (let ((result (begin body ...)))
         (%ffi-matrix-unpin m*)
         result)))))

;;; (with-pinned-tensor t var body ...)
(define-syntax with-pinned-tensor
  (syntax-rules ()
    ((_ t var body ...)
     (let* ((t* t)
            (var (%ffi-tensor-ptr t*)))
       (let ((result (begin body ...)))
         (%ffi-tensor-unpin t*)
         result)))))

;;; (with-pinned-bytevector bv var body ...)
;;;   Binds var to a c-ptr pointing to bv's raw bytes — a general-purpose
;;;   buffer for foreign calls matrix/tensor pinning doesn't cover: input
;;;   arrays of a non-double element type, and out-parameters a C function
;;;   writes results into. Read written bytes back with bytevector-u8-ref
;;;   or the endian-decode helpers in (curry private binary-io).
(define-syntax with-pinned-bytevector
  (syntax-rules ()
    ((_ bv var body ...)
     (let* ((bv* bv)
            (var (%ffi-bytevector-ptr bv*)))
       (let ((result (begin body ...)))
         (%ffi-bytevector-unpin bv*)
         result)))))

;;; (peek-bytes ptr n) — copy n bytes starting at ptr (a c-ptr or raw fixnum
;;;   address) into a fresh bytevector. For dereferencing a pointer a C
;;;   function has handed back to its own heap memory (e.g. HDF5's
;;;   variable-length string attributes) — not for out-parameters a
;;;   function writes into a caller-supplied buffer, which
;;;   with-pinned-bytevector already covers.
(define (peek-bytes ptr n) (%ffi-peek-bytes ptr n))

;;; ── Raw pointer utilities ────────────────────────────────────────────────────

;;; (make-cptr address)   — wrap a fixnum address as a c-ptr
(define (make-cptr addr) (%ffi-make-cptr addr))

;;; (cptr-address p)      — extract address as a fixnum
(define (cptr-address p)  (%ffi-cptr-address p))

;;; (cptr-null? p)        — true if the pointer is NULL
(define (cptr-null? p)    (= (cptr-address p) 0))

;;; (cptr-null)           — the NULL pointer
(define (cptr-null)       (make-cptr 0))

  )) ;; end begin, define-library
