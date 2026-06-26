;;; tests/bench_torture.scm — long-duration torture suite with live Grafana telemetry
;;;
;;; Each phase runs for a target wall-clock duration (default 60s) and publishes
;;; GC snapshots every 5 s so Grafana shows live heap/collection evolution,
;;; not just a final result.
;;;
;;; Usage:
;;;   ./build/curry tests/bench_torture.scm
;;;   ./build/curry tests/bench_torture.scm --duration 120   # 2 min per phase
;;;   ./build/curry tests/bench_torture.scm --phase heap     # single phase

(import (curry mqtt))
(import (curry json))
(import (curry sync))
(import (curry profiling))

;; ── CLI ──────────────────────────────────────────────────────────────────────

(define argv
  (with-exception-handler (lambda (e) '()) (lambda () command-line-args)))

(define (flag-value flag args)
  (let lp ((a args))
    (cond ((null? a)       #f)
          ((null? (cdr a)) #f)
          ((equal? (car a) flag) (cadr a))
          (else (lp (cdr a))))))

(define target-duration
  (let ((v (flag-value "--duration" argv)))
    (if v (string->number v) 60)))

(define only-phase (flag-value "--phase" argv))

(define bench-gc-tag
  (let ((e (assoc 'nursery-used (gc-stats))))
    (if (and e (> (cdr e) 0)) "generational" "boehm")))

;; ── MQTT ─────────────────────────────────────────────────────────────────────

(define mqtt-client #f)

(define (mqtt-init!)
  (with-exception-handler
    (lambda (e)
      (display "bench_torture: MQTT unavailable, stderr fallback\n"
               (current-error-port)))
    (lambda ()
      (set! mqtt-client (mqtt-connect "localhost" 1883 "curry-torture")))))

(define (publish payload-alist)
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

;; ── Profiling helpers ────────────────────────────────────────────────────────

(define (publish-profile-report label top-n)
  (let ((report (profiler-report/top top-n)))
    (for-each
      (lambda (entry)
        (let* ((name   (symbol->string (car entry)))
               (calls  (cadr entry))
               (ns     (cddr entry))
               (ms     (/ ns 1000000.0)))
          (publish
            (list (cons "event"         "profile")
                  (cons "benchmark"     label)
                  (cons "gc"            bench-gc-tag)
                  (cons "mode"          "vm")
                  (cons "fn_name"       name)
                  (cons "calls"         calls)
                  (cons "total_ns"      ns)
                  (cons "mean_us"       (if (> calls 0) (/ ns calls 1000.0) 0.0))
                  (cons "timestamp_ms"  (unix-ms))
                  ; reuse mean field so existing bargauge panels pick it up
                  (cons "mean"          ms)))))
      report)))

(define (publish-actor-stats actor label)
  (let ((s (actor-stats actor)))
    (when (pair? s)
      (define (ref k) (let ((p (assoc k s))) (if p (cdr p) 0)))
      (publish
        (list (cons "event"          "actor-stats")
              (cons "benchmark"      label)
              (cons "gc"             bench-gc-tag)
              (cons "mode"           "vm")
              (cons "actor_id"       (ref "id"))
              (cons "msgs_received"  (ref "msgs-received"))
              (cons "msgs_sent"      (ref "msgs-sent"))
              (cons "ns_in_body"     (ref "ns-in-body"))
              (cons "mailbox_depth"  (ref "mailbox-depth"))
              (cons "age_ns"         (ref "age-ns"))
              (cons "mean"           (let ((rx (ref "msgs-received"))
                                          (ns (ref "ns-in-body")))
                                       (if (> rx 0) (/ ns rx 1000.0) 0.0)))
              (cons "timestamp_ms"   (unix-ms)))))))

;; ── Timing ───────────────────────────────────────────────────────────────────

(define jps (jiffies-per-second))
(define (jiffy->ms j) (/ (* j 1000.0) jps))
(define (jiffy->s  j) (/ (* j 1.0) jps))
(define (unix-ms) (inexact->exact (round (* (current-second) 1000))))
(define (now-s)   (current-second))

;; ── GC helpers ───────────────────────────────────────────────────────────────

(define (alist-ref key alist default)
  (let ((p (assoc key alist))) (if p (cdr p) default)))

; Sort a vector in place using insertion sort (small vectors only).
(define (vector-sort! v <?)
  (let ((n (vector-length v)))
    (let outer ((i 1))
      (when (< i n)
        (let ((key (vector-ref v i)))
          (let inner ((j (- i 1)))
            (if (and (>= j 0) (<? key (vector-ref v j)))
                (begin (vector-set! v (+ j 1) (vector-ref v j))
                       (inner (- j 1)))
                (vector-set! v (+ j 1) key))))
        (outer (+ i 1))))))

; Compute a percentile (0.0–1.0) from a sorted vector.
(define (vector-percentile sorted-v p)
  (let ((n (vector-length sorted-v)))
    (if (= n 0) 0
        (let ((idx (inexact->exact (floor (* p (- n 1))))))
          (vector-ref sorted-v idx)))))

; Snapshot GC stats + pause-ring percentiles in one call.
(define (gc-snap)
  (let* ((s      (gc-stats))
         (ring   (alist-ref 'pause-ring-us s (make-vector 0)))
         (sorted (vector-copy ring)))
    (vector-sort! sorted <)
    (list (cons "gc_minor_count"    (alist-ref 'minor-count    s 0))
          (cons "gc_major_count"    (alist-ref 'major-count    s 0))
          (cons "gc_heap_bytes"     (alist-ref 'heap-size      s 0))
          (cons "gc_nursery_used"   (alist-ref 'nursery-used   s 0))
          (cons "gc_minor_max_us"   (alist-ref 'minor-max-us   s 0))
          (cons "gc_pause_p50_us"   (vector-percentile sorted 0.50))
          (cons "gc_pause_p95_us"   (vector-percentile sorted 0.95))
          (cons "gc_pause_p99_us"   (vector-percentile sorted 0.99)))))

;; ── Phase runner — publishes heartbeats every 5 s ────────────────────────────
;;
;; thunk is called repeatedly; it should do one "unit of work" and return.
;; publish-interval controls heartbeat frequency (seconds).
;; Returns total iterations completed.

(define publish-interval 5.0)   ; seconds between Grafana updates

(define (run-phase name thunk)
  (let* ((label (string-append name " [torture]"))
         (t-start (now-s))
         (t-end   (+ t-start target-duration))
         (t-next-publish (+ t-start publish-interval)))
    (display (string-append "\n▶ " name "  (target=" (number->string target-duration) "s)\n"))
    (gc-stats-reset!)
    (profiler-reset)
    (profiler-start 2)
    (publish
      (list (cons "event"        "start")
            (cons "benchmark"    label)
            (cons "gc"           bench-gc-tag)
            (cons "mode"         "vm")
            (cons "iterations"   0)
            (cons "timestamp_ms" (unix-ms))))
    (let loop ((iters 0))
      (thunk)
      (let ((now (now-s)))
        (when (>= now t-next-publish)
          (let* ((elapsed (- now t-start))
                 (snap    (gc-snap))
                 (rate    (if (> elapsed 0)
                              (inexact->exact (round (/ iters elapsed)))
                              0)))
            (display
              (string-append "  " (number->string (inexact->exact (round elapsed))) "s"
                             "  iters=" (number->string iters)
                             "  major=" (number->string (cdr (assoc "gc_major_count" snap)))
                             "  heap=" (number->string
                                        (inexact->exact
                                          (round (/ (cdr (assoc "gc_heap_bytes" snap))
                                                    1048576.0))))
                             "MiB"
                             "  p99=" (number->string (cdr (assoc "gc_pause_p99_us" snap)))
                             "µs\n"))
            (publish
              (append
                (list (cons "event"        "heartbeat")
                      (cons "benchmark"    label)
                      (cons "gc"           bench-gc-tag)
                      (cons "mode"         "vm")
                      (cons "iterations"   iters)
                      (cons "elapsed_s"    elapsed)
                      (cons "iter_per_s"   rate)
                      (cons "timestamp_ms" (unix-ms))
                      (cons "mean"         (if (> iters 0)
                                               (* elapsed 1000.0 (/ 1.0 iters))
                                               0.0)))
                snap)))
          (set! t-next-publish (+ now publish-interval)))
        (if (< now t-end)
            (loop (+ iters 1))
            (let* ((total-s (- now t-start))
                   (snap    (gc-snap)))
              (publish
                (append
                  (list (cons "event"        "result")
                        (cons "benchmark"    label)
                        (cons "gc"           bench-gc-tag)
                        (cons "mode"         "vm")
                        (cons "iterations"   iters)
                        (cons "elapsed_s"    total-s)
                        (cons "iter_per_s"   (if (> total-s 0)
                                                 (inexact->exact (round (/ iters total-s)))
                                                 0))
                        (cons "mean"         (if (> iters 0)
                                                 (* total-s 1000.0 (/ 1.0 iters))
                                                 0.0))
                        (cons "p99"          0.0)
                        (cons "stddev"       0.0)
                        (cons "timestamp_ms" (unix-ms)))
                  snap))
              (profiler-stop)
              (display (string-append "  done: " (number->string iters)
                                      " iters in " (number->string total-s) "s\n"))
              (display "  publishing function profile (top 20)...\n")
              (publish-profile-report label 20)
              iters))))))

;; ── Phase 1: Heap flood ───────────────────────────────────────────────────────
;; Continuously allocates large lists and drops them, forcing the GC to reclaim
;; tens of MB per cycle. Heap should oscillate visibly in Grafana.

(define (phase-heap-flood)
  (let lp ((i 0) (sink #f))
    (when (< i 100)
      ; Build a 100K-element list (~4 MB of cons cells), hold briefly, drop
      (let build ((j 100000) (acc '()))
        (if (= j 0)
            acc
            (build (- j 1) (cons (list j (* j 3)) acc)))))))

;; ── Phase 2: Live-set pressure ────────────────────────────────────────────────
;; Maintains a large "working set" of live data (~50 MB) while also allocating
;; short-lived garbage. Forces the GC to trace a big live graph every collection.

(define live-set #f)

(define (build-live-set size)
  ; A vector of 10K entries each pointing to a 500-element list
  (let ((v (make-vector size)))
    (let fill ((i 0))
      (when (< i size)
        (let build ((j 500) (acc '()))
          (if (= j 0)
              (vector-set! v i acc)
              (build (- j 1) (cons (* i j) acc))))
        (fill (+ i 1))))
    v))

(define (phase-live-set)
  ; Ensure live set is built (first call)
  (when (eq? live-set #f)
    (display "  building 50 MiB live set...\n")
    (set! live-set (build-live-set 10000)))
  ; Mutate a random slice + allocate short-lived trash
  (let ((n (vector-length live-set)))
    (let lp ((i 0))
      (when (< i 200)
        ; Replace one entry (forces GC to re-trace that slot)
        (let ((slot (remainder (+ i 37) n)))
          (let build ((j 500) (acc '()))
            (if (= j 0)
                (vector-set! live-set slot acc)
                (build (- j 1) (cons (* slot j) acc)))))
        ; Short-lived trash
        (let trash ((k 0))
          (when (< k 50)
            (cons k (cons k k))
            (trash (+ k 1))))
        (lp (+ i 1))))))

;; ── Phase 3: Deep recursion / stack pressure ──────────────────────────────────
;; Non-tail-recursive fib builds up a deep call stack + intermediate values.
;; At fib(35) the call tree has 29M nodes — stresses both the evaluator and GC.

(define (fib35 n)
  (if (< n 2) n (+ (fib35 (- n 1)) (fib35 (- n 2)))))

(define (phase-deep-recursion)
  (fib35 35))

;; ── Phase 4: Write-barrier storm ──────────────────────────────────────────────
;; A large vector in tenured space gets 1M pointer writes per iteration.
;; Under generational GC this hits the write barrier on every store.
;; Under Boehm it stresses cache and dirty-page scanning.

(define wb-vec #f)
(define wb-lists #f)

(define (phase-write-barrier)
  (when (eq? wb-vec #f)
    (display "  promoting write-barrier targets...\n")
    (set! wb-vec (make-vector 200000 0))
    ; Build 200K small lists to store as pointer values
    (let ((v (make-vector 200000)))
      (let fill ((i 0))
        (when (< i 200000)
          (vector-set! v i (list i (* i 2)))
          (fill (+ i 1))))
      (set! wb-lists v))
    ; Force promotion to old generation
    (gc) (gc))
  (let ((n 200000))
    (let lp ((i 0))
      (when (< i 1000000)
        (vector-set! wb-vec (remainder i n)
                     (vector-ref wb-lists (remainder (* i 7) n)))
        (lp (+ i 1))))))

;; ── Phase 5: Allocation rate benchmark ───────────────────────────────────────
;; Pure allocation speed: cons cells as fast as possible.
;; Shows raw nursery throughput and how quickly major GCs accumulate.

(define (phase-alloc-rate)
  (let lp ((i 0))
    (when (< i 2000000)
      (cons i (cons i (cons i i)))
      (lp (+ i 1)))))

;; ── Phase 6: Actor mailbox storm ─────────────────────────────────────────────
;; 64 actors in a ring, each forwarding a growing list payload.
;; Stresses cross-thread allocation, write barriers, and mailbox GC roots.

(define (phase-actors)
  (let* ((n-actors 64)
         (rounds   200)
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
                 (begin
                   ; Grow the payload by consing onto it — allocates cross-thread
                   (send! next (list 'msg (cons count (cadr msg))))
                   (lp next (- count 1)))))))))
    (let* ((actors (let lp ((i 0) (acc '()))
                     (if (= i n-actors) acc
                         (lp (+ i 1) (cons (spawn actor-body) acc)))))
           (vec    (list->vector actors)))
      (set! finished #f)
      (let setup ((i 0))
        (when (< i n-actors)
          (send! (vector-ref vec i)
                 (list 'next (vector-ref vec (remainder (+ i 1) n-actors))))
          (setup (+ i 1))))
      (send! (vector-ref vec 0) (list 'msg (list 0 1 2 3 4)))
      (mutex-lock! done-mtx)
      (let wait ()
        (unless finished
          (cond-wait! done-cv done-mtx)
          (wait)))
      (mutex-unlock! done-mtx)
      ; Publish per-actor stats after the ring completes
      (display "  publishing actor stats...\n")
      (let pub ((i 0))
        (when (< i n-actors)
          (publish-actor-stats (vector-ref vec i) "actors [torture]")
          (pub (+ i 1))))))

;; ── Phase 7: Retained graph + pointer chasing ────────────────────────────────
;; Build a 500K-node doubly-linked list in tenured space, then walk it
;; repeatedly. The GC must trace all 500K nodes every major collection.

(define retained-graph #f)

(define (phase-retained-graph)
  (when (eq? retained-graph #f)
    (display "  building 500K-node retained graph...\n")
    ; Build as a vector of vectors (adjacency list style)
    (let* ((n   500000)
           (v   (make-vector n #f)))
      (let fill ((i 0))
        (when (< i n)
          (vector-set! v i (vector (remainder (+ i 1) n)
                                   (remainder (+ i n -1) n)
                                   (* i 3)))
          (fill (+ i 1))))
      (gc)
      (set! retained-graph v)))
  ; Walk 1M random steps through the graph
  (let ((n (vector-length retained-graph)))
    (let lp ((pos 0) (steps 0) (checksum 0))
      (when (< steps 1000000)
        (let* ((node  (vector-ref retained-graph pos))
               (next  (vector-ref node 0))
               (val   (vector-ref node 2)))
          (lp next (+ steps 1) (+ checksum val)))))))

;; ── Main ─────────────────────────────────────────────────────────────────────

(define phases
  (list
    (list "heap-flood"      phase-heap-flood)
    (list "live-set"        phase-live-set)
    (list "deep-recursion"  phase-deep-recursion)
    (list "write-barrier"   phase-write-barrier)
    (list "alloc-rate"      phase-alloc-rate)
    (list "actors"          phase-actors)
    (list "retained-graph"  phase-retained-graph)))

(mqtt-init!)

(display
  (string-append
    "\n╔══════════════════════════════════════════════════════════════════╗\n"
    "║  Curry GC TORTURE  gc=" bench-gc-tag
    "  duration=" (number->string target-duration) "s/phase"
    "                  ║\n"
    "╚══════════════════════════════════════════════════════════════════╝\n"))

(for-each
  (lambda (phase-spec)
    (let ((name   (car  phase-spec))
          (thunk  (cadr phase-spec)))
      (when (or (not only-phase) (equal? only-phase name))
        ; Reset live-set and wb state between phases so each starts fresh
        (set! live-set #f)
        (set! wb-vec   #f)
        (set! wb-lists #f)
        (set! retained-graph #f)
        (run-phase name thunk))))
  phases)

(display "\n✓ torture complete\n")
