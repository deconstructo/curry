;;; curry/ffi — General C foreign function interface
;;;
;;; Requires BUILD_FFI=ON at compile time (links libffi).
;;;
;;; Primitives (in global env when BUILD_FFI=ON):
;;;   %ffi-load  %ffi-make-fn  %ffi-call
;;;   %ffi-make-cptr  %ffi-cptr-address
;;;   %ffi-matrix-ptr  %ffi-matrix-unpin
;;;   %ffi-tensor-ptr  %ffi-tensor-unpin
;;;   %ffi-bytevector-ptr  %ffi-bytevector-unpin
;;;   %ffi-peek-bytes
;;;   c-ptr?  foreign-lib?  foreign-fn?  foreign-lib-path

(define-library (curry ffi)
  (import (scheme base))
  (export
    define-foreign-library foreign-load-library
    define-foreign
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

(define-syntax define-foreign
  (syntax-rules (→)
    ;; With explicit C name
    ((_ (fn-name (pname ptype) ...) → ret-type #:from lib #:c-name c-name)
     (define fn-name
       (let ((ff (%ffi-make-fn lib c-name 'ret-type (list 'ptype ...))))
         (lambda (pname ...)
           (%ffi-call ff (list pname ...))))))
    ;; Without explicit C name — use Scheme name converted to string
    ((_ (fn-name (pname ptype) ...) → ret-type #:from lib)
     (define fn-name
       (let ((ff (%ffi-make-fn lib (symbol->string 'fn-name)
                               'ret-type (list 'ptype ...))))
         (lambda (pname ...)
           (%ffi-call ff (list pname ...))))))))

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
