;;; NetCDF classic reader tests — (curry netcdf)
;;;
;;; curry has no NetCDF writer, so these tests read pre-built, byte-exact
;;; fixture files (tests/fixtures/netcdf/*.nc) generated with scipy.io's
;;; netcdf_file writer and cross-checked against scipy's own reader before
;;; being checked in — real ground truth, not hand-derived bytes.
;;;
;;;   simple.nc              — 2x3 fixed double variable + global/var attrs
;;;   record_single.nc       — one record (unlimited-dimension) variable
;;;   record_interleaved.nc  — two record variables, tests recsize striding
;;;   cdf2_offset.nc         — CDF-2 (64-bit offset) format variant

(import (curry netcdf))

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

;; Fixtures live next to this script; derive the directory from our own
;; invocation path (the script path is always the first command-line arg)
;; rather than assuming a fixed working directory.
(define (script-dir)
  (let* ((self (car (command-line)))
         (slash (let loop ((i (- (string-length self) 1)))
                  (cond ((< i 0) #f)
                        ((char=? (string-ref self i) #\/) i)
                        (else (loop (- i 1)))))))
    (if slash (substring self 0 (+ slash 1)) "./")))

(define (fixture name) (string-append (script-dir) "fixtures/netcdf/" name))

;;; ════════════════════════════════════════════════════════════
;;; § 1  Fixed-size variable, global + variable attributes
;;; ════════════════════════════════════════════════════════════

(let ((nc (netcdf-open (fixture "simple.nc"))))
  (check "dims" (netcdf-dimensions nc) (list (cons "x" 2) (cons "y" 3)))
  (check "global attrs" (netcdf-attributes nc)
         (list (cons "history" "created for curry testing")))
  (call-with-values
    (lambda () (netcdf-variable nc "temperature"))
    (lambda (t names)
      (check "var dim names" names (list "x" "y"))
      (check "var shape" (tensor-shape t) (list 2 3))
      (check "var [0,0]" (tensor-ref t 0 0) 1.5)
      (check "var [0,2]" (tensor-ref t 0 2) 3.5)
      (check "var [1,0]" (tensor-ref t 1 0) 4.5)))
  (check "var attrs" (netcdf-attributes nc "temperature")
         (list (cons "units" "kelvin")))
  (netcdf-close nc))

;;; ════════════════════════════════════════════════════════════
;;; § 2  Single record (unlimited-dimension) variable
;;; ════════════════════════════════════════════════════════════

(let ((nc (netcdf-open (fixture "record_single.nc"))))
  (check "unlimited dim" (netcdf-dimensions nc)
         (list (cons "time" #f) (cons "x" 2)))
  (call-with-values
    (lambda () (netcdf-variable nc "temp"))
    (lambda (t names)
      (check "record shape" (tensor-shape t) (list 3 2))
      (check "record [0,0]" (tensor-ref t 0 0) 1.0)
      (check "record [1,0]" (tensor-ref t 1 0) 3.0)
      (check "record [2,1]" (tensor-ref t 2 1) 6.0)))
  (netcdf-close nc))

;;; ════════════════════════════════════════════════════════════
;;; § 3  Two interleaved record variables (recsize striding)
;;; ════════════════════════════════════════════════════════════

(let ((nc (netcdf-open (fixture "record_interleaved.nc"))))
  (call-with-values
    (lambda () (netcdf-variable nc "temp"))
    (lambda (t names) (check "interleaved temp" (tensor->list t)
                             (list (list 1.0 2.0) (list 3.0 4.0) (list 5.0 6.0)))))
  (call-with-values
    (lambda () (netcdf-variable nc "pres"))
    (lambda (t names) (check "interleaved pres" (tensor->list t)
                             (list (list 10.0 20.0) (list 30.0 40.0) (list 50.0 60.0)))))
  (netcdf-close nc))

;;; ════════════════════════════════════════════════════════════
;;; § 4  CDF-2 (64-bit offset) format
;;; ════════════════════════════════════════════════════════════

(let ((nc (netcdf-open (fixture "cdf2_offset.nc"))))
  (call-with-values
    (lambda () (netcdf-variable nc "t"))
    (lambda (t names) (check "cdf2 data" (tensor->list t) (list 1.0 2.0))))
  (netcdf-close nc))

;;; ════════════════════════════════════════════════════════════
;;; § 5  Error handling
;;; ════════════════════════════════════════════════════════════

(check "missing variable raises"
  (guard (e (#t #t)) (netcdf-open (fixture "simple.nc"))
    (netcdf-variable (netcdf-open (fixture "simple.nc")) "nonexistent") #f)
  #t)

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
