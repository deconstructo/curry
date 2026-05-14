;;; Profiling module tests

(import (curry profiling))

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

(define (check-pred label result pred)
  (if (pred result)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (newline)
             (set! fail (+ fail 1)))))

;;; ---- Level 0: off by default ----

(check "initial level is 0" (profiler-level) 0)
(check "report empty when off" (profiler-report) '())

;;; ---- Level 1: call counts ----

(profiler-start 1)
(check "level set to 1" (profiler-level) 1)
(check "**eval-profiler** mirrors level" **eval-profiler** 1)

(define (fib n)
  (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))

(fib 10)

(let ((report (profiler-report)))
  (check-pred "report is non-empty after fib" report pair?)
  (let ((entry (assq 'fib report)))
    (check-pred "fib appears in report" entry pair?)
    (when entry
      ;; fib(10) call tree has 177 nodes
      (check "fib(10) call count" (cadr entry) 177)
      (check "ns is 0 on TCO path" (cddr entry) 0))))

;;; ---- Reset clears data ----

(profiler-reset)
(check "report empty after reset" (profiler-report) '())

;;; ---- Level 2: timing via apply() path (map/for-each) ----

(profiler-start 2)
(check "level set to 2" (profiler-level) 2)

(define (busy n)
  (let loop ((i 0) (acc 0))
    (if (= i n) acc (loop (+ i 1) (+ acc i)))))

;; for-each calls through apply() so timing is captured
(for-each busy (list 10000 20000 30000))

(let ((report (profiler-report)))
  (let ((entry (assq 'busy report)))
    (check-pred "busy appears in report at level 2" entry pair?)
    (when entry
      (check "busy called 3 times" (cadr entry) 3)
      (check-pred "busy has non-zero timing" (cddr entry) positive?))))

;;; ---- Level 3: primitives are counted ----

(profiler-reset)
(profiler-start 3)
(check "level set to 3" (profiler-level) 3)

(define (work x) (* x x))
(for-each work (list 1 2 3 4 5))

(let ((report (profiler-report)))
  (check-pred "* appears in report at level 3" (assq '* report) pair?)
  (check-pred "work appears in report at level 3" (assq 'work report) pair?))

;;; ---- Stop disables profiling ----

(profiler-stop)
(check "level is 0 after stop" (profiler-level) 0)
(check "**eval-profiler** is 0 after stop" **eval-profiler** 0)

(profiler-reset)
(define (after-stop x) (+ x 1))
(after-stop 42)
(check "no data collected after stop" (profiler-report) '())

;;; ---- Multiple named functions ----

(profiler-start 1)
(profiler-reset)

(define (double x) (* 2 x))
(define (triple x) (* 3 x))
(for-each double (list 1 2 3))
(for-each triple (list 1 2))

(let ((report (profiler-report)))
  (let ((d (assq 'double report))
        (t (assq 'triple report)))
    (check-pred "double in report" d pair?)
    (check-pred "triple in report" t pair?)
    (when (and d t)
      (check "double called 3 times" (cadr d) 3)
      (check "triple called 2 times" (cadr t) 2)
      ;; double has more calls so should appear first (report is sorted)
      (check "sorted by calls desc"
             (symbol->string (car (car report)))
             "double"))))

(profiler-stop)
(profiler-reset)

;;; ---- Summary ----

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
