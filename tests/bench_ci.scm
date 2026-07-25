;;; tests/bench_ci.scm — deterministic benchmark suite for CI regression tracking
;;;
;;; Usage:
;;;   ./build/curry tests/bench_ci.scm                # print JSON to stdout
;;;   ./build/curry tests/bench_ci.scm --out FILE     # write JSON to FILE
;;;
;;; Output is a JSON array in the "customSmallerIsBetter" format consumed by
;;; benchmark-action/github-action-benchmark:
;;;   [{"name": "fib(25)/vm", "unit": "ms", "value": 12.3}, ...]
;;;
;;; Each benchmark reports the median of RUNS timed runs (median resists the
;;; scheduling noise of shared CI runners better than mean). Workloads are
;;; fixed-size and deterministic; no MQTT, no gc-stats — this file must run
;;; on any build configuration.

;; ── Command line ────────────────────────────────────────────────────────────

(define argv
  (with-exception-handler
    (lambda (e) '())
    (lambda () command-line-args)))

(define (flag-value flag args)
  (let lp ((a args))
    (cond ((null? a)       #f)
          ((null? (cdr a)) #f)
          ((equal? (car a) flag) (cadr a))
          (else (lp (cdr a))))))

(define out-path (flag-value "--out" argv))

;; ── Timing ──────────────────────────────────────────────────────────────────

(define RUNS 7)

(define jps (jiffies-per-second))

(define (time-once thunk)
  (let ((t0 (current-jiffy)))
    (thunk)
    (/ (* (- (current-jiffy) t0) 1000.0) jps)))

(define (median-ms thunk)
  ;; one untimed warmup, then RUNS timed runs, median
  (thunk)
  (let ((samples (make-vector RUNS 0.0)))
    (let lp ((i 0))
      (when (< i RUNS)
        (vector-set! samples i (time-once thunk))
        (lp (+ i 1))))
    (let ((n (vector-length samples)))
      (let outer ((i 1))
        (when (< i n)
          (let ((key (vector-ref samples i)))
            (let inner ((j (- i 1)))
              (if (and (>= j 0) (< key (vector-ref samples j)))
                  (begin (vector-set! samples (+ j 1) (vector-ref samples j))
                         (inner (- j 1)))
                  (vector-set! samples (+ j 1) key))))
          (outer (+ i 1))))
      (vector-ref samples (quotient n 2)))))

;; ── Workloads ───────────────────────────────────────────────────────────────

(define (fib n)
  (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))

(define (tak x y z)
  (if (not (< y x)) z
      (tak (tak (- x 1) y z)
           (tak (- y 1) z x)
           (tak (- z 1) x y))))

(define (count-down n)
  (let lp ((i n)) (if (= i 0) i (lp (- i 1)))))

(tree-eval '(define (fib-tw n)
  (if (< n 2) n (+ (fib-tw (- n 1)) (fib-tw (- n 2))))))

(tree-eval '(define (tak-tw x y z)
  (if (not (< y x)) z
      (tak-tw (tak-tw (- x 1) y z)
              (tak-tw (- y 1) z x)
              (tak-tw (- z 1) x y)))))

;; flonum loop: every iteration produces heap-allocated flonums; measures
;; both FP arithmetic and allocation pressure (performance doc §3.6)
(define (flonum-loop n)
  (let lp ((i 0) (acc 0.0))
    (if (= i n) acc
        (lp (+ i 1) (+ acc (* 1.0000001 (+ 0.5 (* 0.25 acc))))))))

;; cont-capture: escape-only call/cc in a tight loop; baseline for the
;; continuation work in performance doc §4.4 / Tier 4
(define (cont-capture n)
  (let lp ((i 0) (acc 0))
    (if (= i n) acc
        (lp (+ i 1)
            (+ acc (call/cc (lambda (k) (k 1))))))))

;; alloc churn: short-lived pairs, the nursery workload for gc-rewrite
(define (alloc-churn n)
  (let lp ((i n))
    (when (> i 0)
      (cons i i)
      (lp (- i 1)))))

;; list building + walking: allocation that survives, then traversal
(define (list-build-walk n)
  (let ((ls (let build ((i n) (acc '()))
              (if (= i 0) acc (build (- i 1) (cons i acc))))))
    (let lp ((l ls) (s 0))
      (if (null? l) s (lp (cdr l) (+ s (car l)))))))

;; ── Suite ───────────────────────────────────────────────────────────────────

(define benchmarks
  (list
    (list "fib(25)/vm"        (lambda () (fib 25)))
    (list "fib(22)/tw"        (lambda () (fib-tw 22)))
    (list "tak(18,12,6)/vm"   (lambda () (tak 18 12 6)))
    (list "tak(16,10,4)/tw"   (lambda () (tak-tw 16 10 4)))
    (list "count-down(3M)/vm" (lambda () (count-down 3000000)))
    (list "flonum-loop(1M)"   (lambda () (flonum-loop 1000000)))
    (list "cont-capture(200k)" (lambda () (cont-capture 200000)))
    (list "alloc-churn(1M)"   (lambda () (alloc-churn 1000000)))
    (list "list-build-walk(500k)" (lambda () (list-build-walk 500000)))))

;; ── JSON output ─────────────────────────────────────────────────────────────

(define (ms->json-number ms)
  ;; fixed 3-decimal rendering, avoids exponent notation in output
  (let* ((thousandths (inexact->exact (round (* ms 1000.0))))
         (whole (quotient thousandths 1000))
         (frac  (remainder thousandths 1000))
         (frac-str (number->string frac)))
    (string-append
      (number->string whole) "."
      (make-string (- 3 (string-length frac-str)) #\0)
      frac-str)))

(define (emit port)
  (display "[" port)
  (let lp ((bs benchmarks) (first #t))
    (unless (null? bs)
      (let* ((b    (car bs))
             (name (car b))
             (ms   (median-ms (cadr b))))
        (display (string-append name ": " (ms->json-number ms) " ms\n")
                 (current-error-port))
        (unless first (display "," port))
        (display (string-append
                   "\n  {\"name\": \"" name
                   "\", \"unit\": \"ms\", \"value\": " (ms->json-number ms) "}")
                 port))
      (lp (cdr bs) #f)))
  (display "\n]\n" port))

(if out-path
    (call-with-output-file out-path emit)
    (emit (current-output-port)))
