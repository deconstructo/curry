;;; test_numeric.scm — targeted tests for the numeric/vector crash paths
;;;
;;; Run: ./build/curry tests/test_numeric.scm
;;;
;;; Each test prints PASS or FAIL.  Any uncaught error is itself a failure.

(import (scheme base))
(import (scheme inexact))
(import (scheme write))

(define pass-count 0)
(define fail-count 0)

(define (check label got expected)
  (cond
    ((equal? got expected)
     (set! pass-count (+ pass-count 1))
     (display "PASS ") (display label) (newline))
    (else
     (set! fail-count (+ fail-count 1))
     (display "FAIL ") (display label)
     (display " — got: ") (display got)
     (display " expected: ") (display expected)
     (newline))))

(define (check-true label val)
  (check label val #t))

;;; ---- 1. make-vector with exact integer size + flonum fill ----

(let ((v (make-vector 3 0.0)))
  (check-true "make-vector length"  (= (vector-length v) 3))
  (check-true "element 0 is flonum" (inexact? (vector-ref v 0)))
  (check-true "element 1 is flonum" (inexact? (vector-ref v 1)))
  (check-true "element 2 is flonum" (inexact? (vector-ref v 2)))
  (check-true "element 0 = 0.0"     (= (vector-ref v 0) 0.0)))

;;; ---- 2. make-vector with flonum size (coercion) ----

(let ((v (make-vector 3.0 0.0)))   ; 3.0 should be coerced to 3
  (check-true "flonum-size length"  (= (vector-length v) 3))
  (check-true "flonum-size elem"    (= (vector-ref v 0) 0.0)))

;;; ---- 3. (* flonum flonum) does not hit bignum path ----

(let ((r (* (vector-ref (make-vector 1 0.0) 0) 0.005)))
  (check-true "* flonum flonum is inexact" (inexact? r))
  (check-true "* 0.0 0.005 = 0.0"         (= r 0.0)))

;;; ---- 4. vec-add! mimic: update a vector element ----

(define (vec-add! dst src scale)
  (let loop ((i 0))
    (when (< i (vector-length dst))
      (vector-set! dst i (+ (vector-ref dst i)
                             (* (vector-ref src i) scale)))
      (loop (+ i 1)))))

(let ((dst (make-vector 3 1.0))
      (src (make-vector 3 2.0)))
  (vec-add! dst src 0.5)
  (check-true "vec-add! elem 0" (= (vector-ref dst 0) 2.0))
  (check-true "vec-add! elem 1" (= (vector-ref dst 1) 2.0))
  (check-true "vec-add! elem 2" (= (vector-ref dst 2) 2.0)))

;;; ---- 5. multi-list for-each ----

(let ((sums '()))
  (for-each
    (lambda (a b)
      (set! sums (cons (+ a b) sums)))
    '(1 2 3)
    '(10 20 30))
  (check "multi for-each result" (reverse sums) '(11 22 33)))

;;; ---- 6. multi-list map ----

(let ((products (map * '(2 3 4) '(10 20 30))))
  (check "multi map result" products '(20 60 120)))

;;; ---- 7. map returns correct length ----

(let ((r (map (lambda (x) (* x x)) '(1 2 3 4 5))))
  (check "map length"  (length r) 5)
  (check "map squares" r '(1 4 9 16 25)))

;;; ---- 8. gravity-accel-k mimic ----

(define (vec-dist2 a b)
  (let loop ((i 0) (s 0.0))
    (if (= i (vector-length a)) s
        (let ((d (- (vector-ref a i) (vector-ref b i))))
          (loop (+ i 1) (+ s (* d d)))))))

(define (vec-dist a b) (sqrt (vec-dist2 a b)))

(define (gravity-accel-k D G mj ri rj k)
  (let* ((r      (vec-dist ri rj))
         (r-soft (max r 0.1))
         (delta  (- (vector-ref rj k) (vector-ref ri k))))
    (/ (* G mj delta)
       (expt r-soft D))))

(let* ((D   3.0)
       (G   1.0)
       (ri  (vector 0.0 0.0 0.0))
       (rj  (vector 100.0 0.0 0.0))
       (mj  50.0)
       (ak  (gravity-accel-k D G mj ri rj 0)))
  (check-true "gravity-accel-k is inexact" (inexact? ak))
  (check-true "gravity-accel-k positive"   (> ak 0.0)))

;;; ---- 9. compute-accel mimic (the D-inexact issue) ----

(define (compute-accel bodies D G)
  (let ((Dint (inexact->exact (round D))))
    (map (lambda (bi)
           (let ((a (make-vector Dint 0.0)))
             (for-each
               (lambda (bj)
                 (when (not (eq? bi bj))
                   (let loop ((k 0))
                     (when (< k D)
                       (vector-set! a k
                         (+ (vector-ref a k)
                            (gravity-accel-k D G
                              (vector-ref bj 0)  ; mj = mass of body j
                              (vector-ref bi 1)  ; ri = position of body i
                              (vector-ref bj 1)  ; rj = position of body j
                              k)))
                       (loop (+ k 1))))))
               bodies)
             a))
         bodies)))

(let* ((b1 (vector 1.0 (vector 0.0 0.0 0.0) (vector 0.0 0.0 0.0)))
       (b2 (vector 1.0 (vector 100.0 0.0 0.0) (vector 0.0 0.0 0.0)))
       (bodies (list b1 b2))
       (D 3.0)
       (G 1.0)
       (accels (compute-accel bodies D G)))
  (check-true "accels is a list"          (list? accels))
  (check-true "accels length = 2"         (= (length accels) 2))
  (check-true "accel vector length = 3"   (= (vector-length (car accels)) 3))
  (check-true "accel[0][0] is inexact"    (inexact? (vector-ref (car accels) 0)))
  (check-true "accel[0][0] > 0 (pulled toward b2)"
              (> (vector-ref (car accels) 0) 0.0)))

;;; ---- 10. multi-list for-each advancing both lists ----

(let ((log '()))
  (for-each
    (lambda (body accel)
      (set! log (cons (list (car body) (car accel)) log)))
    '((a 1) (b 2) (c 3))
    '((x 10) (y 20) (z 30)))
  (check "for-each both lists advanced"
         (reverse log)
         '((a x) (b y) (c z))))

;;; ---- Summary ----

(newline)
(display "Results: ")
(display pass-count) (display " passed, ")
(display fail-count) (display " failed")
(newline)
(when (> fail-count 0)
  (error "some tests failed"))
