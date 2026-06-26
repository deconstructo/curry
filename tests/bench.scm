;;; tests/bench.scm — GC-aware benchmark suite with real-time MQTT telemetry
;;;
;;; Usage:
;;;   ./build/curry tests/bench.scm                         # all suites, Boehm GC
;;;   ./build/curry tests/bench.scm --gc generational       # gen GC backend
;;;   ./build/curry tests/bench.scm --suite gc              # GC-specific suite only
;;;   ./build/curry tests/bench.scm --suite actors          # actor ring suite
;;;   ./build/curry tests/bench.scm --suite throughput      # CPU throughput suite
;;;
;;; If mosquitto is reachable at localhost:1883 each result event is published
;;; to curry/bench/events as a JSON object.  Falls back to stderr JSON when
;;; the broker is unavailable.
;;;
;;; The --gc flag is cosmetic from bench.scm's point of view: the GC backend
;;; is selected by the C runtime flag in the environment.  bench.scm reads the
;;; (gc-stats) alist to detect which backend is active and tags results
;;; accordingly.

(import (curry mqtt))
(import (curry json))
(import (curry sync))

;; ── Command-line argument parsing ───────────────────────────────────────────

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

(define bench-suite
  (or (flag-value "--suite" argv) "all"))

(define bench-gc-tag
  (let ((e (assoc 'nursery-used (gc-stats))))
    (if (and e (> (cdr e) 0)) "generational" "boehm")))

;; ── MQTT setup ──────────────────────────────────────────────────────────────

(define mqtt-client #f)

(define (mqtt-init!)
  (with-exception-handler
    (lambda (e)
      (display "bench: MQTT unavailable, falling back to stderr\n"
               (current-error-port)))
    (lambda ()
      (set! mqtt-client (mqtt-connect "localhost" 1883 "curry-bench")))))

(define (bench-publish payload-alist)
  (let ((json-str (json-stringify payload-alist)))
    (if mqtt-client
        (with-exception-handler
          (lambda (e)
            (display json-str (current-error-port))
            (newline (current-error-port)))
          (lambda ()
            (mqtt-publish mqtt-client "curry/bench/events" json-str 1)))
        (begin (display json-str (current-error-port))
               (newline (current-error-port))))))

;; ── Timing primitives ───────────────────────────────────────────────────────

(define jps (jiffies-per-second))

(define (jiffy->ms j) (/ (* j 1000.0) jps))

(define (now-ms) (jiffy->ms (current-jiffy)))

(define (unix-ms)
  (let ((ts (current-second)))
    (inexact->exact (round (* ts 1000)))))

;; ── Statistics helpers ───────────────────────────────────────────────────────

(define (vector-sort! < v)
  (let ((n (vector-length v)))
    (let outer ((i 1))
      (when (< i n)
        (let ((key (vector-ref v i)))
          (let inner ((j (- i 1)))
            (if (and (>= j 0) (< key (vector-ref v j)))
                (begin (vector-set! v (+ j 1) (vector-ref v j))
                       (inner (- j 1)))
                (vector-set! v (+ j 1) key))))
        (outer (+ i 1))))))

(define (vec-sum v)
  (let lp ((i 0) (s 0.0))
    (if (= i (vector-length v)) s
        (lp (+ i 1) (+ s (vector-ref v i))))))

(define (percentile sorted-v p)
  (let* ((n (vector-length sorted-v))
         (idx (inexact->exact (floor (* p (- n 1))))))
    (vector-ref sorted-v (max 0 (min (- n 1) idx)))))

(define (sample-stats samples)
  (let* ((n   (vector-length samples))
         (sv  (vector-copy samples))
         (_ (vector-sort! < sv))
         (mean  (/ (vec-sum sv) n))
         (var   (/ (let lp ((i 0) (s 0.0))
                     (if (= i n) s
                         (let ((d (- (vector-ref sv i) mean)))
                           (lp (+ i 1) (+ s (* d d))))))
                   (max 1 (- n 1))))
         (stddev (sqrt var)))
    (list (cons "mean"   mean)
          (cons "stddev" stddev)
          (cons "p50"    (percentile sv 0.50))
          (cons "p95"    (percentile sv 0.95))
          (cons "p99"    (percentile sv 0.99))
          (cons "min"    (vector-ref sv 0))
          (cons "max"    (vector-ref sv (- n 1))))))

;; ── GC stats helpers ────────────────────────────────────────────────────────

(define (alist-ref key alist default)
  (let ((p (assoc key alist)))
    (if p (cdr p) default)))

(define (gc-stats-snapshot)
  (let ((s (gc-stats)))
    (list (cons "gc_minor_count"    (alist-ref 'minor-count    s 0))
          (cons "gc_major_count"    (alist-ref 'major-count    s 0))
          (cons "gc_minor_total_us" (alist-ref 'minor-total-us s 0))
          (cons "gc_minor_max_us"   (alist-ref 'minor-max-us   s 0))
          (cons "gc_heap_bytes"     (alist-ref 'heap-size      s 0))
          (cons "gc_nursery_used"   (alist-ref 'nursery-used   s 0)))))

(define (gc-p99-us)
  (let* ((s   (gc-stats))
         (ring (alist-ref 'pause-ring-us s (vector)))
         (n   (vector-length ring)))
    (if (= n 0) 0
        (let ((sv (vector-copy ring)))
          (vector-sort! < sv)
          (percentile sv 0.99)))))

;; ── Benchmark runner ────────────────────────────────────────────────────────

(define (run-bench name thunk n mode)
  (bench-publish
    (list (cons "event"        "start")
          (cons "benchmark"    name)
          (cons "gc"           bench-gc-tag)
          (cons "mode"         mode)
          (cons "iterations"   n)
          (cons "timestamp_ms" (unix-ms))))
  (gc-stats-reset!)
  (let* ((samples (make-vector n 0.0)))
    (let lp ((i 0))
      (when (< i n)
        (let* ((t0 (current-jiffy))
               (_  (thunk))
               (dt (jiffy->ms (- (current-jiffy) t0))))
          (vector-set! samples i dt)
          (lp (+ i 1)))))
    (let* ((st  (sample-stats samples))
           (gst (gc-stats-snapshot))
           (g99 (gc-p99-us))
           (payload
             (append
               (list (cons "event"        "result")
                     (cons "benchmark"    name)
                     (cons "gc"           bench-gc-tag)
                     (cons "mode"         mode)
                     (cons "iterations"   n)
                     (cons "timestamp_ms" (unix-ms))
                     (cons "gc_p99_us"    g99))
               st
               gst)))
      (bench-publish payload)
      (display
        (string-append
          (let pad ((s name)) (if (>= (string-length s) 28) s (pad (string-append s " "))))
          "  mean=" (number->string (inexact->exact (round (cdr (assoc "mean" st))))) "ms"
          "  p99="  (number->string (inexact->exact (round (cdr (assoc "p99"  st))))) "ms"
          "  gc_minor=" (number->string (cdr (assoc "gc_minor_count" gst)))
          "\n"))
      payload)))

;; ── Throughput benchmarks (same workloads as vm_bench.scm) ──────────────────

(define (fib n)
  (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))

(define (tak x y z)
  (if (not (< y x)) z
      (tak (tak (- x 1) y z)
           (tak (- y 1) z x)
           (tak (- z 1) x y))))

(define (count-down n)
  (let lp ((i n)) (if (= i 0) i (lp (- i 1)))))

(define bench-list-1k
  (let lp ((i 999) (acc '()))
    (if (< i 0) acc (lp (- i 1) (cons i acc)))))

(tree-eval '(define (fib-tw n)
  (if (< n 2) n (+ (fib-tw (- n 1)) (fib-tw (- n 2))))))

(tree-eval '(define (tak-tw x y z)
  (if (not (< y x)) z
      (tak-tw (tak-tw (- x 1) y z)
              (tak-tw (- y 1) z x)
              (tak-tw (- z 1) x y)))))

(tree-eval '(define (count-down-tw n)
  (let lp ((i n)) (if (= i 0) i (lp (- i 1))))))

(define (run-throughput-suite)
  (display "── Throughput ───────────────────────────────────────────────────\n")
  (run-bench "fib(28)/vm"          (lambda () (fib 28))               5  "vm")
  (run-bench "fib(28)/tw"          (lambda () (fib-tw 28))            5  "tw")
  (run-bench "tak(18,12,6)/vm"     (lambda () (tak 18 12 6))          5  "vm")
  (run-bench "tak(18,12,6)/tw"     (lambda () (tak-tw 18 12 6))       5  "tw")
  (run-bench "count-down(1M)/vm"   (lambda () (count-down 1000000))   5  "vm")
  (run-bench "count-down(1M)/tw"   (lambda () (count-down-tw 1000000)) 5 "tw"))

;; ── GC-specific benchmarks ──────────────────────────────────────────────────

(define (bench-alloc-short)
  (let lp ((i 500000))
    (when (> i 0)
      (cons i i)
      (lp (- i 1)))))

(define (bench-alloc-medium)
  (let build ((i 10000) (acc '()))
    (if (= i 0)
        (begin (gc) acc)
        (build (- i 1) (cons (list i i i) acc)))))

(define (bench-alloc-large)
  (let lp ((i 0) (sink #f))
    (when (< i 100)
      (lp (+ i 1) (make-vector 1024 i)))))

(define (bench-mutation)
  (let* ((n   10000)
         (vec (make-vector n 0)))
    (gc)
    (let lp ((i 0))
      (when (< i 200000)
        (vector-set! vec (remainder i n) i)
        (lp (+ i 1))))))

(define (bench-mixed)
  (define (eval-expr e env)
    (cond ((number? e) e)
          ((symbol? e) (let ((b (assoc e env))) (if b (cdr b) 0)))
          ((pair? e)
           (case (car e)
             ((+) (+ (eval-expr (cadr e) env) (eval-expr (caddr e) env)))
             ((-) (- (eval-expr (cadr e) env) (eval-expr (caddr e) env)))
             ((*) (* (eval-expr (cadr e) env) (eval-expr (caddr e) env)))
             (else 0)))
          (else 0)))
  (let lp ((i 0) (env (list (cons 'x 3) (cons 'y 7))))
    (when (< i 5000)
      (eval-expr '(+ (* x y) (- x y)) env)
      (lp (+ i 1) (cons (cons 'z i) env)))))

(define (run-gc-suite)
  (display "── GC behaviour ────────────────────────────────────────────────\n")
  (run-bench "alloc-short"   bench-alloc-short   10 "vm")
  (run-bench "alloc-medium"  bench-alloc-medium  10 "vm")
  (run-bench "alloc-large"   bench-alloc-large   20 "vm")
  (run-bench "mutation"      bench-mutation       10 "vm")
  (run-bench "mixed"         bench-mixed          10 "vm"))

;; ── Actor benchmarks ────────────────────────────────────────────────────────

(define (ring-of n)
  (let lp ((i 0) (acc '()))
    (if (= i n) acc
        (lp (+ i 1) (cons i acc)))))

(define (run-actors-suite)
  (display "── Actors ──────────────────────────────────────────────────────\n")
  (run-bench "actor-ring/8x100"
    (lambda ()
      (let* ((n-actors 8)
             (rounds   100)
             (done-mtx (make-mutex))
             (done-cv  (make-condvar))
             (finished #f))
        (define (actor-body)
          (let lp ((next #f) (count rounds))
            (let ((msg (receive)))
              (cond
                ((and (pair? msg) (eq? (car msg) 'next))
                 (lp (cadr msg) count))
                ((and (pair? msg) (eq? (car msg) 'msg))
                 (if (= count 0)
                     (begin (mutex-lock! done-mtx)
                            (set! finished #t)
                            (cond-signal! done-cv)
                            (mutex-unlock! done-mtx))
                     (begin (send! next (list 'msg (cadr msg)))
                            (lp next (- count 1)))))))))
        (let* ((actors (map (lambda (_) (spawn actor-body)) (ring-of n-actors)))
               (vec    (list->vector actors)))
          (let setup ((i 0))
            (when (< i n-actors)
              (send! (vector-ref vec i)
                     (list 'next (vector-ref vec (remainder (+ i 1) n-actors))))
              (setup (+ i 1))))
          (send! (vector-ref vec 0) (list 'msg (ring-of 20)))
          (mutex-lock! done-mtx)
          (let wait ()
            (unless finished
              (cond-wait! done-cv done-mtx)
              (wait)))
          (mutex-unlock! done-mtx))))
    5 "vm"))

;; ── Main ────────────────────────────────────────────────────────────────────

(mqtt-init!)

(display
  (string-append "curry benchmark suite  gc=" bench-gc-tag
                 "  suite=" bench-suite "\n"))
(display "────────────────────────────────────────────────────────────────\n")

(cond
  ((equal? bench-suite "throughput") (run-throughput-suite))
  ((equal? bench-suite "gc")         (run-gc-suite))
  ((equal? bench-suite "actors")     (run-actors-suite))
  (else
   (run-throughput-suite)
   (run-gc-suite)
   (run-actors-suite)))

(display "────────────────────────────────────────────────────────────────\n")
(display "done.\n")
