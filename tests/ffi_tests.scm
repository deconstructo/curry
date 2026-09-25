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
;;; Foreign callbacks — define-foreign-callback, foreign-callback-ptr,
;;; foreign-callback-free!, %ffi-make-callback/%ffi-callback-ptr/
;;; %ffi-callback-free!. Exercised against real libc qsort -- a Scheme
;;; comparator gets exposed as a real C function pointer and libc's own C
;;; code calls back into it repeatedly to sort an array in place. This is
;;; the closest thing to an authentic end-to-end proof this mechanism
;;; actually works: it isn't just "a value round-tripped correctly", it's
;;; "a real C library, given only a function pointer, drove the sort
;;; using logic that runs inside curry's own evaluator".
;;; ----------------------------------------------------------------------

(define-foreign (c-qsort (base c-ptr) (nmemb uint) (width uint) (compar c-ptr)) → void
  #:from libc #:c-name "qsort")

;; little-endian int32 encode/decode into a bytevector -- local test-only
;; helpers, not part of the module's own API.
(define (s32->u32 n) (if (< n 0) (+ n 4294967296) n))
(define (encode-s32-le! bv off n)
  (let ((u (s32->u32 n)))
    (bytevector-u8-set! bv off       (bitwise-and u 255))
    (bytevector-u8-set! bv (+ off 1) (bitwise-and (arithmetic-shift u -8) 255))
    (bytevector-u8-set! bv (+ off 2) (bitwise-and (arithmetic-shift u -16) 255))
    (bytevector-u8-set! bv (+ off 3) (bitwise-and (arithmetic-shift u -24) 255))))
(define (decode-s32-le bv off)
  (let ((n (+ (bytevector-u8-ref bv off)
              (* 256 (bytevector-u8-ref bv (+ off 1)))
              (* 65536 (bytevector-u8-ref bv (+ off 2)))
              (* 16777216 (bytevector-u8-ref bv (+ off 3))))))
    (if (>= n 2147483648) (- n 4294967296) n)))

(define-foreign-callback (int-compar (a c-ptr) (b c-ptr)) → int
  (- (decode-s32-le (%ffi-peek-bytes a 4) 0)
     (decode-s32-le (%ffi-peek-bytes b 4) 0)))

(check "foreign-callback?: a define-foreign-callback result is one" (foreign-callback? int-compar) #t)
(check "foreign-callback?: an ordinary value is not" (foreign-callback? 42) #f)

(define (sort-via-qsort-with-ptr! vals compar-ptr)
  (let* ((n (length vals))
         (buf (make-bytevector (* 4 n) 0)))
    (let loop ((i 0) (vs vals))
      (unless (null? vs)
        (encode-s32-le! buf (* i 4) (car vs))
        (loop (+ i 1) (cdr vs))))
    (with-pinned-bytevector buf base
      (c-qsort base n 4 compar-ptr))
    (let loop ((i 0) (acc '()))
      (if (= i n) (reverse acc) (loop (+ i 1) (cons (decode-s32-le buf (* i 4)) acc))))))

(define (sort-via-qsort! vals)
  (sort-via-qsort-with-ptr! vals (foreign-callback-ptr int-compar)))

(check "qsort via a Scheme comparator callback: unsorted -> sorted"
       (sort-via-qsort! '(42 -7 100 0 17))
       '(-7 0 17 42 100))

(check "qsort via callback: already sorted stays sorted"
       (sort-via-qsort! '(1 2 3 4 5))
       '(1 2 3 4 5))

(check "qsort via callback: duplicates and negatives"
       (sort-via-qsort! '(3 1 4 1 5 -9 2 6 -5 3 5))
       '(-9 -5 1 1 2 3 3 4 5 5 6))

;; the same callback object works across multiple separate qsort calls --
;; confirms the underlying closure isn't somehow single-use.
(check "the same callback object is reusable across calls"
       (list (sort-via-qsort! '(5 3 1)) (sort-via-qsort! '(9 8 7)))
       '((1 3 5) (7 8 9)))

;;; ── Callback error paths ─────────────────────────────────────────────────────

(check-pred "%ffi-make-callback rejects 'string as a return type"
  (string-contains
    (guard (e (#t (error-object-message e)))
      (%ffi-make-callback (lambda (x) "hi") 'string (list 'int))
      "no error raised")
    "'string is not a supported callback return type"))

(check-pred "%ffi-make-callback rejects 65 args (FFI_MAX_ARGS boundary)"
  (string-contains
    (guard (e (#t (error-object-message e)))
      (%ffi-make-callback (lambda args 0) 'int (make-list 65 'int))
      "no error raised")
    "65 args exceeds max 64"))

(check-pred "%ffi-make-callback accepts exactly 64 args (no off-by-one)"
  (guard (e (#t #f))
    (%ffi-make-callback (lambda args 0) 'int (make-list 64 'int))
    #t))

;; A callback whose Scheme body raises must not crash the process or
;; corrupt VM state -- it should report to stderr and return a default
;; value, and the VM must keep working normally afterward.
(define-foreign-callback (raising-compar (a c-ptr) (b c-ptr)) → int
  (error "deliberate failure inside a foreign-callback, for test purposes"))

(check "a raising callback doesn't crash the process (qsort completes)"
       (let ((buf (make-bytevector 12 0)))
         (encode-s32-le! buf 0 3) (encode-s32-le! buf 4 1) (encode-s32-le! buf 8 2)
         (with-pinned-bytevector buf base
           (c-qsort base 3 4 (foreign-callback-ptr raising-compar)))
         'survived)
       'survived)

(check "VM state is intact after a raising callback (ordinary eval still works)"
       (+ 1 2)
       3)

;;; ── GC lifetime: a callback must survive collection even when nothing on
;;; the Scheme side references it, as long as C might still hold its raw
;;; function pointer ────────────────────────────────────────────────────────
;;;
;;; CURRY_NEW_PINNED is an ORDINARY COLLECTIBLE allocation under the
;;; current Boehm backend (PIN only means non-moving for a future
;;; precise/generational backend) -- without an explicit GC root, a
;;; <foreign-callback> reachable only via the bare C function pointer
;;; handed to a C library (nothing on the Scheme side references the
;;; object itself) is a real use-after-free waiting to happen the next
;;; time GC runs. This regression test was confirmed to actually catch
;;; that: temporarily disabling ffi_make_callback's gc_register_root_val
;;; call made this exact test fail (the array came back unsorted and the
;;; callback raised spurious "not a foreign-callback"-shaped exceptions
;;; after a few forced collections) before the fix was restored.
;;;
;;; Uses %ffi-make-callback directly rather than define-foreign-callback:
;;; the latter is a macro, and curry has a separate, unrelated pre-existing
;;; bug (GitHub issue #253) where a macro-expanded internal define doesn't
;;; bind correctly -- irrelevant to what this test is actually checking,
;;; so routed around rather than tripped over.
(define (make-detached-comparator-ptr)
  (define cb (%ffi-make-callback
               (lambda (a b)
                 (- (decode-s32-le (%ffi-peek-bytes a 4) 0)
                    (decode-s32-le (%ffi-peek-bytes b 4) 0)))
               'int (list 'c_ptr 'c_ptr)))
  (%ffi-callback-ptr cb))

;; The <foreign-callback> object itself never escapes the helper above --
;; only its raw function pointer does. Nothing on the Scheme side
;; references the object from here on.
(define detached-cb-ptr (make-detached-comparator-ptr))

;; Allocate heavily and force real collections, specifically to disturb
;; stack/register content that could otherwise accidentally (and only by
;; luck, under Boehm's conservative scanning) keep the callback alive even
;; without a real root -- the point is to make an absent root actually
;; observable, not just theoretically wrong.
(define (gc-churn! n)
  (when (> n 0)
    (let loop ((i 0) (acc '()))
      (if (< i 2000)
          (loop (+ i 1) (cons (list i (* i i) (vector i i i)) acc))
          (begin (gc) (gc-churn! (- n 1)))))))
(gc-churn! 20)

(check "a callback reachable only via its raw C pointer survives 20 forced GCs"
       (sort-via-qsort-with-ptr! '(42 -7 100 0 17) detached-cb-ptr)
       '(-7 0 17 42 100))

;;; ── Explicit free / double-free guard ────────────────────────────────────────

(define-foreign-callback (freeable (x int)) → int x)
(check "foreign-callback-free! succeeds the first time"
       (begin (foreign-callback-free! freeable) 'freed)
       'freed)
(check-pred "foreign-callback-free! rejects a double free"
  (string-contains
    (guard (e (#t (error-object-message e)))
      (foreign-callback-free! freeable)
      "no error raised")
    "already freed (double free)"))

;;; ----------------------------------------------------------------------
;;; Summary
;;; ----------------------------------------------------------------------
(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
