;;; Collatz Conjecture — step counter and range tester with millisecond timing.

;; ── Core arithmetic ──────────────────────────────────────────────────────────

(define (collatz-step n)
  (if (even? n) (/ n 2) (+ (* 3 n) 1)))

;; Full sequence to 1 (builds a list — use sparingly for large n)
(define (collatz-seq n)
  (let loop ((n n) (acc (list n)))
    (if (= n 1)
        (reverse acc)
        (loop (collatz-step n) (cons (collatz-step n) acc)))))

;; Stopping time only — no list allocation
(define (collatz-steps n)
  (let loop ((n n) (count 0))
    (if (= n 1) count (loop (collatz-step n) (+ count 1)))))

;; ── Timestamp helpers ─────────────────────────────────────────────────────────

(define (pad2 n)
  (if (< n 10) (string-append "0" (number->string n)) (number->string n)))

(define (pad3 n)
  (cond ((< n  10) (string-append "00" (number->string n)))
        ((< n 100) (string-append "0"  (number->string n)))
        (else       (number->string n))))

;; Format a Unix timestamp (flonum seconds) as HH:mm:ss.mmm UTC
(define (format-timestamp ts)
  (let* ((total-ms (exact (round (* ts 1000))))
         (ms       (modulo total-ms 1000))
         (total-s  (quotient total-ms 1000))
         (secs     (modulo total-s 60))
         (total-m  (quotient total-s 60))
         (mins     (modulo total-m 60))
         (hours    (modulo (quotient total-m 60) 24)))
    (string-append (pad2 hours) ":" (pad2 mins) ":" (pad2 secs) "." (pad3 ms))))

;; Format an elapsed duration in seconds as "[Xm ]Y.ZZZs"
(define (format-elapsed elapsed)
  (let* ((el-ms   (exact (round (* elapsed 1000))))
         (el-s    (quotient el-ms 1000))
         (el-min  (quotient el-s 60))
         (el-sec  (modulo el-s 60))
         (el-frac (modulo el-ms 1000)))
    (string-append
      (if (> el-min 0) (string-append (number->string el-min) "m ") "")
      (number->string el-sec) "." (pad3 el-frac) "s")))

;; ── Range tester ─────────────────────────────────────────────────────────────

;; Test all n in [start, limit].  Prints a timestamped line at start, every
;; 100 000 numbers checked, and at the end with the total elapsed time.
(define (test-collatz start limit)
  (when (> start (expt 2 60))
    (display "Warning: start > 2^60; all arithmetic is bignum — expect slow results")
    (newline))
  (let ((t0 (current-second)))
    (display "Start:  ")
    (display (format-timestamp t0))
    (newline)
    (let loop ((n start) (worst-n start) (worst-steps 0))
      (if (> n limit)
          (let* ((t1      (current-second))
                 (elapsed (- t1 t0)))
            (display "End:    ")
            (display (format-timestamp t1))
            (newline)
            (display "Total:  ")
            (display (format-elapsed elapsed))
            (newline)
            (display "Worst in [")
            (display start)
            (display ", ")
            (display limit)
            (display "]: n=")
            (display worst-n)
            (display " at ")
            (display worst-steps)
            (display " steps")
            (newline))
          (let ((steps (collatz-steps n)))
            (when (= (modulo (- n start) 100000) 0)
              (let* ((now     (current-second))
                     (elapsed (- now t0)))
                (display (format-timestamp now))
                (display " | checked up to ")
                (display n)
                (display " | elapsed ")
                (display (format-elapsed elapsed))
                (newline)))
            (if (> steps worst-steps)
                (loop (+ n 1) n steps)
                (loop (+ n 1) worst-n worst-steps)))))))

(test-collatz 1 1000000)
