;;; tests/bench_heavy.scm — punishment suite
;;;
;;; Designed to make the GC work hard and expose pressure points.
;;; Run directly:   ./build/curry tests/bench_heavy.scm
;;; Or via punish:  tools/punish.sh
;;;
;;; Each benchmark is calibrated to run 2–15 seconds, produce serious
;;; GC pressure, and exercise different allocation patterns.

(import (curry mqtt))
(import (curry json))
(import (curry sync))

;; ── CLI ─────────────────────────────────────────────────────────────────────

(define argv
  (with-exception-handler (lambda (e) '()) (lambda () command-line-args)))

(define (flag-value flag args)
  (let lp ((a args))
    (cond ((null? a)       #f)
          ((null? (cdr a)) #f)
          ((equal? (car a) flag) (cadr a))
          (else (lp (cdr a))))))

(define bench-suite (or (flag-value "--suite" argv) "all"))
(define bench-label (or (flag-value "--label" argv) ""))

(define bench-gc-tag
  (let ((e (assoc 'nursery-used (gc-stats))))
    (if (and e (> (cdr e) 0)) "generational" "boehm")))

;; ── MQTT ────────────────────────────────────────────────────────────────────

(define mqtt-client #f)

(define (mqtt-init!)
  (with-exception-handler
    (lambda (e)
      (display "bench_heavy: MQTT unavailable, stderr fallback\n"
               (current-error-port)))
    (lambda ()
      (set! mqtt-client (mqtt-connect "localhost" 1883 "curry-heavy")))))

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

;; ── Timing / stats ───────────────────────────────────────────────────────────

(define jps (jiffies-per-second))
(define (jiffy->ms j) (/ (* j 1000.0) jps))
(define (now-ms) (jiffy->ms (current-jiffy)))
(define (unix-ms) (inexact->exact (round (* (current-second) 1000))))

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

(define (percentile sv p)
  (let* ((n (vector-length sv))
         (idx (inexact->exact (floor (* p (- n 1))))))
    (vector-ref sv (max 0 (min (- n 1) idx)))))

(define (sample-stats samples)
  (let* ((n (vector-length samples))
         (sv (vector-copy samples))
         (_ (vector-sort! < sv))
         (mean (/ (vec-sum sv) n))
         (var (/ (let lp ((i 0) (s 0.0))
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

(define (alist-ref key alist default)
  (let ((p (assoc key alist))) (if p (cdr p) default)))

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

;; ── Runner ───────────────────────────────────────────────────────────────────

(define (run-bench name thunk n mode)
  (let ((tag (if (string=? bench-label "") name
                 (string-append name " [" bench-label "]"))))
    (bench-publish
      (list (cons "event"        "start")
            (cons "benchmark"    tag)
            (cons "gc"           bench-gc-tag)
            (cons "mode"         mode)
            (cons "iterations"   n)
            (cons "timestamp_ms" (unix-ms))))
    (gc-stats-reset!)
    (let ((samples (make-vector n 0.0)))
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
                       (cons "benchmark"    tag)
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
            (let pad ((s tag)) (if (>= (string-length s) 30) s (pad (string-append s " "))))
            "  mean=" (number->string (inexact->exact (round (cdr (assoc "mean" st))))) "ms"
            "  p99="  (number->string (inexact->exact (round (cdr (assoc "p99"  st))))) "ms"
            "  major=" (number->string (cdr (assoc "gc_major_count" gst)))
            "\n"))
        payload))))

;; ── Heavy throughput ─────────────────────────────────────────────────────────
;;
;; fib(33) in VM is ~4–8s on modern hardware.
;; fib(30) in tree-walker is ~3–6s (3.6x slower per eval step).
;; tak(24,16,8) is ~500ms VM / ~2s TW.

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

(tree-eval '(define (count-down-tw n)
  (let lp ((i n)) (if (= i 0) i (lp (- i 1))))))

(define (run-throughput-suite)
  (display "── Heavy throughput ─────────────────────────────────────────────\n")
  (run-bench "fib(33)/vm"           (lambda () (fib 33))                3 "vm")
  (run-bench "fib(30)/tw"           (lambda () (fib-tw 30))             3 "tw")
  (run-bench "tak(24,16,8)/vm"      (lambda () (tak 24 16 8))           5 "vm")
  (run-bench "tak(24,16,8)/tw"      (lambda () (tak-tw 24 16 8))        5 "tw")
  (run-bench "count-down(10M)/vm"   (lambda () (count-down 10000000))   5 "vm")
  (run-bench "count-down(10M)/tw"   (lambda () (count-down-tw 10000000)) 5 "tw"))

;; ── Heavy GC suite ───────────────────────────────────────────────────────────
;;
;; These are calibrated to produce hundreds of major GC collections per run.

;; 5M short-lived pairs — nursery thrash, many minor GC cycles
(define (bench-alloc-short-heavy)
  (let lp ((i 5000000))
    (when (> i 0)
      (cons i (cons i i))         ; 3 allocations per iteration
      (lp (- i 1)))))

;; 200K-element list, force GC while holding it, then drop
(define (bench-alloc-medium-heavy)
  (let build ((i 200000) (acc '()))
    (if (= i 0)
        (begin (gc) (gc) (length acc))  ; 2 full GCs while holding the list
        (build (- i 1) (cons (list i (* i 2) (* i 3)) acc)))))

;; 10K vectors of 2K each = 20M words allocated in large-object space
(define (bench-alloc-large-heavy)
  (let lp ((i 0))
    (when (< i 50)
      ;; Allocate a batch and immediately drop them to force reclamation
      (let inner ((j 0) (last #f))
        (when (< j 200)
          (inner (+ j 1) (make-vector 2048 j))))
      (gc)
      (lp (+ i 1)))))

;; 500K-element vector, 10M mutations — write-barrier stress test
(define (bench-mutation-heavy)
  (let* ((n   500000)
         (vec (make-vector n 0)))
    (gc)  ; promote vec to tenured
    (let lp ((i 0))
      (when (< i 10000000)
        (vector-set! vec (remainder i n) i)
        (lp (+ i 1))))))

;; Self-hosting mini-interpreter: evaluate 50K expressions
;; Each step creates a new env frame (linked list cons), stressing both
;; allocation and survival rates.
(define (bench-mixed-heavy)
  (define (eval-expr e env)
    (cond ((number? e) e)
          ((symbol? e) (let ((b (assoc e env))) (if b (cdr b) 0)))
          ((pair? e)
           (case (car e)
             ((+) (+ (eval-expr (cadr e) env) (eval-expr (caddr e) env)))
             ((-) (- (eval-expr (cadr e) env) (eval-expr (caddr e) env)))
             ((*) (* (eval-expr (cadr e) env) (eval-expr (caddr e) env)))
             ((let) (eval-expr (caddr e)
                               (cons (cons (caadr e)
                                           (eval-expr (cadadr e) env))
                                     env)))
             (else 0)))
          (else 0)))
  (let lp ((i 0) (env '((x . 3) (y . 7) (z . 11))))
    (when (< i 50000)
      (eval-expr '(let ((w (+ x y))) (* w (- z w))) env)
      (lp (+ i 1) (cons (cons 'w i) env)))))

;; Retained-set pressure: build a hash table of 100K entries in long-lived
;; space, then do 5M lookups — stresses the mark phase.
(define (bench-retained-heavy)
  (let* ((size 100000)
         (table (make-vector size '())))
    ;; populate
    (let fill ((i 0))
      (when (< i size)
        (let ((slot (remainder i size)))
          (vector-set! table slot (cons i (vector-ref table slot))))
        (fill (+ i 1))))
    ;; promote to old gen
    (gc)
    ;; lookup storm
    (let lp ((i 0) (hits 0))
      (if (= i 5000000)
          hits
          (let* ((slot (remainder i size))
                 (chain (vector-ref table slot))
                 (found (if (pair? chain) (car chain) #f)))
            (lp (+ i 1) (if found (+ hits 1) hits)))))))

(define (run-gc-suite)
  (display "── Heavy GC ─────────────────────────────────────────────────────\n")
  (run-bench "alloc-short/5M"    bench-alloc-short-heavy   5 "vm")
  (run-bench "alloc-medium/200K" bench-alloc-medium-heavy  5 "vm")
  (run-bench "alloc-large/10K"   bench-alloc-large-heavy   3 "vm")
  (run-bench "mutation/10M"      bench-mutation-heavy       3 "vm")
  (run-bench "mixed/50K"         bench-mixed-heavy          5 "vm")
  (run-bench "retained/100K+5M"  bench-retained-heavy       3 "vm"))

;; ── Heavy actor suite ────────────────────────────────────────────────────────
;;
;; 32 actors, 500 rounds each, payload is a 500-element list.
;; This means 32 × 500 = 16K messages in flight, each carrying a live 500-cons
;; list — forces cross-thread GC interaction on every send.

(define (ring-of n)
  (let lp ((i 0) (acc '()))
    (if (= i n) acc (lp (+ i 1) (cons i acc)))))

(define (run-actors-suite)
  (display "── Heavy actors ─────────────────────────────────────────────────\n")
  (run-bench "actor-ring/32x500"
    (lambda ()
      (let* ((n-actors 32)
             (rounds   500)
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
          (send! (vector-ref vec 0) (list 'msg (ring-of 500)))
          (mutex-lock! done-mtx)
          (let wait ()
            (unless finished
              (cond-wait! done-cv done-mtx)
              (wait)))
          (mutex-unlock! done-mtx))))
    5 "vm"))

;; ── Main ─────────────────────────────────────────────────────────────────────

(mqtt-init!)

(display
  (string-append "\ncurry HEAVY benchmark  gc=" bench-gc-tag
                 "  suite=" bench-suite
                 (if (string=? bench-label "") "" (string-append "  label=" bench-label))
                 "\n"))
(display "════════════════════════════════════════════════════════════════\n")

(cond
  ((equal? bench-suite "throughput") (run-throughput-suite))
  ((equal? bench-suite "gc")         (run-gc-suite))
  ((equal? bench-suite "actors")     (run-actors-suite))
  (else
   (run-throughput-suite)
   (run-gc-suite)
   (run-actors-suite)))

(display "════════════════════════════════════════════════════════════════\n")
(display "done.\n")
