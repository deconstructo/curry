;;; (curry ffi) tests — general C FFI (define-foreign / %ffi-call and the
;;; variadic extension: define-foreign's #:variadic marker, va,
;;; %ffi-make-fn-variadic, %ffi-call-variadic).
;;; Only run when curry is compiled with -DBUILD_FFI=ON.
;;;
;;; Exercises real libc functions (strlen, printf) rather than a fake
;;; library, since libc is guaranteed present on every platform this runs
;;; on and its ABI for these functions is completely stable — the point
;;; is testing curry's own marshaling/cif-building, not libc itself.

(import (curry ffi))
(import (scheme base))
(import (scheme write))

(define pass 0)
(define fail 0)

(define (check label result expected)
  (if (equal? result expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (display " expected ") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

(define (check-pred label result)
  (if result
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label) (newline)
             (set! fail (+ fail 1)))))

;;; ── Locating libc ────────────────────────────────────────────────────────────
;;; Same candidate-list-and-guard pattern (curry hdf5) uses — libc's own
;;; path isn't uniform across platforms.

(define %libc-candidates
  (list
    "/usr/lib/libSystem.B.dylib"          ; macOS
    "libc.so.6"                            ; Linux, on loader path
    "/lib/x86_64-linux-gnu/libc.so.6"      ; Debian/Ubuntu, x86_64
    "/lib/aarch64-linux-gnu/libc.so.6"     ; Debian/Ubuntu, arm64
    "/lib64/libc.so.6"))                   ; Fedora/RHEL

(define (%try-load candidates)
  (let loop ((c candidates))
    (if (null? c)
        #f
        (guard (exn (#t (loop (cdr c))))
          (foreign-load-library (car c))))))

(define libc
  (or (%try-load %libc-candidates)
      (error "ffi_tests: could not locate libc on this platform")))

;;; ----------------------------------------------------------------------
;;; Fixed-arity define-foreign (pre-existing, not the new work, but
;;; nothing in (curry ffi) had test coverage before this file at all)
;;; ----------------------------------------------------------------------

(define-foreign (c-strlen (s string)) → uint #:from libc #:c-name "strlen")
(check "strlen empty"  (c-strlen "")      0)
(check "strlen hello"  (c-strlen "hello") 5)

(define-foreign (c-abs (n int)) → int #:from libc #:c-name "abs")
(check "abs -5" (c-abs -5) 5)
(check "abs 0"  (c-abs 0)  0)

;;; ----------------------------------------------------------------------
;;; Variadic define-foreign — #:variadic, va, %ffi-make-fn-variadic,
;;; %ffi-call-variadic
;;; ----------------------------------------------------------------------

;;; snprintf(buf, size, fmt, ...) → int (bytes that would have been
;;; written) -- return value alone is enough to prove the variadic call
;;; actually reached the right C formatting logic without needing to peek
;;; the written buffer.
(define-foreign (c-snprintf (buf c-ptr) (size uint) (fmt string) #:variadic)
  → int #:from libc #:c-name "snprintf")

(define scratch (make-bytevector 64 0))

(check "snprintf int arg"
       (with-pinned-bytevector scratch buf
         (c-snprintf buf 64 "%d" (va 'int 12345)))
       5)  ; "12345" is 5 chars

(check "snprintf string arg"
       (with-pinned-bytevector scratch buf
         (c-snprintf buf 64 "%s" (va 'string "hello")))
       5)

(check "snprintf mixed int/string/double args"
       (with-pinned-bytevector scratch buf
         (c-snprintf buf 64 "%d-%s-%.2f"
                     (va 'int 7) (va 'string "x") (va 'double 3.5)))
       8)  ; "7-x-3.50" is 8 chars

(check "snprintf zero variadic args (nfixed == ntotal path)"
       (with-pinned-bytevector scratch buf
         (c-snprintf buf 64 "no format specifiers"))
       20)

(check "snprintf float promoted to double"
       (with-pinned-bytevector scratch buf
         (c-snprintf buf 64 "%.1f" (va 'float 2.5)))
       3)  ; "2.5" is 3 chars

;;; ── Error paths ──────────────────────────────────────────────────────────────

;;; error-object-message includes curry's Akkadian preamble ahead of the
;;; actual FFI message, so check substring containment rather than exact
;;; equality.
(check-pred "variadic fn called via %ffi-call-variadic with wrong fixed-arg count"
       (string-contains
         (guard (e (#t (error-object-message e)))
           (let ((ff (%ffi-make-fn-variadic libc "snprintf" 'int
                                            (list 'c_ptr 'uint 'string))))
             (%ffi-call-variadic ff '() '())))  ; expects 3 fixed args, got 0
         "ffi-call-variadic: snprintf expects 3 fixed args, got 0"))

(check-pred "non-variadic fn rejected by %ffi-call-variadic"
       (string-contains
         (guard (e (#t (error-object-message e)))
           (%ffi-call-variadic (%ffi-make-fn libc "strlen" 'uint (list 'string))
                               (list "hi") '()))
         "ffi-call-variadic: strlen was not declared variadic (define-foreign it with #:variadic)"))

(check "variadic fn rejected by plain %ffi-call when arg count doesn't match nfixed"
       (guard (e (#t 'raised))
         (%ffi-call (%ffi-make-fn-variadic libc "snprintf" 'int (list 'c_ptr 'uint 'string))
                    (list (make-cptr 0))))  ; only 1 of 3 fixed args
       'raised)

;;; Regression test for a pre-existing bug found during review of the
;;; variadic work (GitHub issue #249): %ffi-make-fn/%ffi-make-fn-variadic
;;; had no check that the declared arg count fits the fixed-size
;;; FFI_MAX_ARGS(64)-element stack buffers ffi_call_fn/ffi_call_fn_variadic
;;; marshal into at call time -- a define-foreign form with more than 64
;;; parameters was a real stack buffer overflow reachable from ordinary
;;; Scheme source. Both constructors now reject an over-wide signature at
;;; definition time instead.
(check-pred "%ffi-make-fn rejects 65 args (FFI_MAX_ARGS boundary)"
       (string-contains
         (guard (e (#t (error-object-message e)))
           (%ffi-make-fn libc "abs" 'int (make-list 65 'int))
           "no error raised")
         "65 args exceeds max 64"))

(check-pred "%ffi-make-fn accepts exactly 64 args (no off-by-one)"
       (guard (e (#t #f))
         (%ffi-make-fn libc "abs" 'int (make-list 64 'int))
         #t))

(check-pred "%ffi-make-fn-variadic rejects 65 fixed args (FFI_MAX_ARGS boundary)"
       (string-contains
         (guard (e (#t (error-object-message e)))
           (%ffi-make-fn-variadic libc "snprintf" 'int (make-list 65 'int))
           "no error raised")
         "65 fixed args exceeds max 64"))

;;; ----------------------------------------------------------------------
;;; Summary
;;; ----------------------------------------------------------------------
(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
