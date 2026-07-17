;;; HDF5 wrapper tests — (curry hdf5)
;;;
;;; libhdf5 is a runtime-only dependency (dlopen'd, never linked at build
;;; time — see hdf5.scm), so it may not be installed on the machine running
;;; this suite. If the import fails, skip cleanly (print a note, exit 0)
;;; rather than failing the whole test run — there is no compile-time flag
;;; to gate this test's registration on the way BUILD_MPFR gates the mpfr
;;; suite, since the dependency is only ever known at runtime.

(define hdf5-available
  (guard (e (#t #f))
    (import (curry hdf5))
    #t))

(if (not hdf5-available)
    (begin
      (display "SKIP: libhdf5 not found on this system — install it to run this suite")
      (newline)
      (display "  macOS:         brew install hdf5") (newline)
      (display "  Debian/Ubuntu: apt install libhdf5-dev") (newline)
      (display "  Fedora/RHEL:   dnf install hdf5-devel") (newline)
      (exit 0))
    (begin

(define pass 0)
(define fail 0)

(define (check label got expected)
  (if (equal? got expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " — got ") (write got)
             (display "  expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))

(define (check-num label got expected) (check label (= got expected) #t))

(define test-path "/tmp/curry-hdf5-test.h5")
;; hdf5-open reuses an existing file (RDWR, not truncate) so a stale file
;; from a previous run would make hdf5-write's "create" fail on a name
;; that already exists — start from a clean slate every run.
(guard (e (#t #f)) (delete-file test-path))

;;; ════════════════════════════════════════════════════════════
;;; § 1  2-D dataset write/read roundtrip
;;; ════════════════════════════════════════════════════════════

(let ((f (hdf5-open test-path))
      (t (make-tensor (list 2 3) 0.0)))
  (tensor-set! t 0 0 1.5) (tensor-set! t 0 1 2.5) (tensor-set! t 0 2 3.5)
  (tensor-set! t 1 0 4.5) (tensor-set! t 1 1 5.5) (tensor-set! t 1 2 6.5)
  (hdf5-write f "temperature" t)
  (hdf5-close f))

(let ((f (hdf5-open test-path)))
  (let ((t (hdf5-read f "temperature")))
    (check "2d shape" (tensor-shape t) (list 2 3))
    (check "2d [0,0]" (tensor-ref t 0 0) 1.5)
    (check "2d [0,2]" (tensor-ref t 0 2) 3.5)
    (check "2d [1,1]" (tensor-ref t 1 1) 5.5))
  (check "no attrs" (hdf5-attributes f "temperature") (list))
  (hdf5-close f))

;;; ════════════════════════════════════════════════════════════
;;; § 2  3-D dataset roundtrip
;;; ════════════════════════════════════════════════════════════

(let ((f (hdf5-open test-path))
      (t3 (make-tensor (list 2 2 2) 0.0)))
  (let loop ((i 0))
    (when (< i 8)
      (tensor-set! t3 (quotient i 4) (quotient (remainder i 4) 2) (remainder i 2)
                   (exact->inexact i))
      (loop (+ i 1))))
  (hdf5-write f "cube" t3)
  (hdf5-close f)
  (let* ((f2 (hdf5-open test-path))
         (t3r (hdf5-read f2 "cube")))
    (check "3d shape" (tensor-shape t3r) (list 2 2 2))
    (check-num "3d [1,1,1]" (tensor-ref t3r 1 1 1) 7)
    (check-num "3d [0,1,0]" (tensor-ref t3r 0 1 0) 2)
    (hdf5-close f2)))

;;; ════════════════════════════════════════════════════════════
;;; § 3  Numeric attributes of varying type/width (regression: h5aread's
;;;      mem_type_id must match the stored type's real size, not assume
;;;      every numeric attribute is an 8-byte double)
;;; ════════════════════════════════════════════════════════════
;;;
;;; curry has no attribute *writer*, so this reads a fixture pre-built with
;;; h5py (tests/fixtures/hdf5/numeric_attrs.h5) covering int64/int32/
;;; float32/float64 scalars and int64/float32 arrays — real ground truth,
;;; not hand-derived bytes.

(define (script-dir)
  (let* ((self (car (command-line)))
         (slash (let loop ((i (- (string-length self) 1)))
                  (cond ((< i 0) #f)
                        ((char=? (string-ref self i) #\/) i)
                        (else (loop (- i 1)))))))
    (if slash (substring self 0 (+ slash 1)) "./")))

(let* ((fixture (string-append (script-dir) "fixtures/hdf5/numeric_attrs.h5"))
       (f (hdf5-open fixture))
       (attrs (hdf5-attributes f "data")))
  (check-num "int64 scalar" (cdr (assoc "int_scalar" attrs)) 42)
  (check-num "int32 scalar" (cdr (assoc "int32_scalar" attrs)) -7)
  (check-num "float32 scalar" (cdr (assoc "float32_scalar" attrs)) 3.5)
  (check-num "float64 scalar" (cdr (assoc "float64_scalar" attrs)) 2.5)
  (check "int64 array" (cdr (assoc "int_array" attrs)) (list 1 2 3))
  (check "float32 array" (cdr (assoc "float32_array" attrs)) (list 1.5 2.5 3.5))
  (check "string attr" (cdr (assoc "str_attr" attrs)) "kelvin")
  (hdf5-close f))

;;; ════════════════════════════════════════════════════════════
;;; § 4  Error handling: missing dataset
;;; ════════════════════════════════════════════════════════════

(check "missing dataset raises"
  (guard (e (#t #t))
    (let ((f (hdf5-open test-path)))
      (hdf5-read f "does-not-exist")
      #f))
  #t)

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))

))
