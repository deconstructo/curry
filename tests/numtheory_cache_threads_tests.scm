;;; (bernoulli n) and (euler-number n) from several threads at once (#454).
;;;
;;; Both keep a cache filled on demand. Computing an entry initialised GMP
;;; numbers in place and set a flag afterwards, with no lock, so a parallel map
;;; over them gave wrong values (B10 = 17993126/33), a division by zero (the
;;; process died on SIGFPE), "error in parallel worker" and hangs. The first
;;; thing this file does is the parallel call, so the caches start cold.

(define pass 0)
(define fail 0)
(define (check label got expected)
  (if (equal? got expected)
      (set! pass (+ pass 1))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write got)
             (display " expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))

(define (range n) (let loop ((i (- n 1)) (a '())) (if (< i 0) a (loop (- i 1) (cons i a)))))
(define (binom n k) (let loop ((i 0) (r 1)) (if (= i k) r (loop (+ i 1) (/ (* r (- n i)) (+ i 1))))))

;; Overlapping indices, the high ones first, so threads need each other's entries.
(define idx (append (map (lambda (i) (- 249 i)) (range 250)) (range 250) (range 250)))
(define bvals (map (lambda (i) (cons i (bernoulli i))) idx))
(define (b i) (cdr (assv i bvals)))

(check "B0" (b 0) 1)
(check "B1" (b 1) -1/2)
(check "B10" (b 10) 5/66)
(check "B12" (b 12) -691/2730)
(check "B20" (b 20) -174611/330)
(check "B24" (b 24) -236364091/2730)
(check "odd Bernoulli numbers past 1 are zero" (b 101) 0)
;; The defining recurrence: sum over k of C(m+1,k) B_k is 0 for every m >= 1.
(check "recurrence holds for m = 1 .. 119"
       (let loop ((m 1) (bad 0))
         (if (= m 120) bad
             (loop (+ m 1)
                   (if (= 0 (let sum ((k 0) (s 0))
                              (if (> k m) s (sum (+ k 1) (+ s (* (binom (+ m 1) k) (b k)))))))
                       bad (+ bad 1)))))
       0)
(check "every thread saw the same value for an index"
       (let loop ((l bvals) (bad 0))
         (if (null? l) bad (loop (cdr l) (if (equal? (cdar l) (b (caar l))) bad (+ bad 1)))))
       0)

(define evals (map (lambda (i) (cons i (euler-number i)))
                   (append (map (lambda (i) (- 248 (* 2 i))) (range 120)) (range 249) (range 249))))
(define (e i) (cdr (assv i evals)))
(check "E0" (e 0) 1)
(check "E2" (e 2) -1)
(check "E4" (e 4) 5)
(check "E10" (e 10) -50521)
(check "E12" (e 12) 2702765)
(check "E14" (e 14) -199360981)
(check "E20" (e 20) 370371188237525)
(check "odd Euler numbers are zero" (e 101) 0)
;; sum over k of C(2n,2k) E_2k = 0 for n >= 1.
(check "recurrence holds for n = 1 .. 100"
       (let loop ((n 1) (bad 0))
         (if (= n 101) bad
             (loop (+ n 1)
                   (if (= 0 (let sum ((k 0) (s 0))
                              (if (> k n) s (sum (+ k 1) (+ s (* (binom (* 2 n) (* 2 k)) (e (* 2 k))))))))
                       bad (+ bad 1)))))
       0)

;; Agrees with the sequential map on a warm cache.
(check "map/seq agrees on B"
       (map/seq (lambda (i) (bernoulli i)) (range 100)) (map b (range 100)))
(check "map/seq agrees on E"
       (map/seq (lambda (i) (euler-number i)) (range 100)) (map e (range 100)))

;; Errors still raise, and leave the lock free.
(check "negative index" (guard (x (#t 'raised)) (bernoulli -1)) 'raised)
(check "index too large" (guard (x (#t 'raised)) (bernoulli 100000)) 'raised)
(check "not a number" (guard (x (#t 'raised)) (euler-number 'a)) 'raised)
(check "still works after the errors" (bernoulli 6) 1/42)

(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
