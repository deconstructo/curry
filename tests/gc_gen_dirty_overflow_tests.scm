;;; Regression test for issue #285 -- run with --gc generational.
;;;
;;; More than 4096 write-barrier records between two minor GCs used to
;;; overflow the fixed-size dirty-slot buffer. The collector then skipped
;;; every recorded slot, so slots in objects tenured before the previous
;;; minor GC (here GLOBAL_ENV's slot for `v`) kept pointing into the reset
;;; nursery, and `v` stopped being a vector.

(define failures 0)
(define (check name ok)
  (if ok
      (begin (display "PASS: ") (display name) (newline))
      (begin (display "FAIL: ") (display name) (newline)
             (set! failures (+ failures 1)))))

;; Enough allocation for at least one minor GC before `v` is defined, so
;; GLOBAL_ENV is already tenured and no longer on the pinned list.
(define (build n acc) (if (= n 0) acc (build (- n 1) (cons n acc))))
(define keep (build 50000 '()))
(define v (make-vector 1000 '()))

(define (mutate i)
  (cond ((>= i 200000) #t)
        ((not (vector? v)) #f)
        (else (vector-set! v (modulo i 1000) (cons i i))
              (mutate (+ i 1)))))

(check "global vector survives many nursery stores" (mutate 0))
(check "vector contents are correct" (equal? (vector-ref v 999) (cons 199999 199999)))
(check "earlier list is intact" (and (= (length keep) 50000) (= (car keep) 1)))

;; Store-only loop: no allocation, so only the soft limit can trigger a
;; minor GC. The buffer must not grow for all 2M stores.
(define (store y i) (if (< i 2000000) (begin (vector-set! v 0 y) (store y (+ i 1)))))
(define before (cdr (assq 'minor-count (gc-stats))))
(store (cons 'a 'b) 0)
(check "soft limit requests a minor GC" (> (cdr (assq 'minor-count (gc-stats))) before))
(check "stored value is correct" (equal? (vector-ref v 0) '(a . b)))

(if (> failures 0) (exit 1))
