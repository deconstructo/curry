(define-library (surfage s194 random-data-samples)
  (import (scheme base) (curry random))
  (export
    make-random-integer-generator make-random-real-generator
    make-random-boolean-generator make-random-char-generator
    make-uniform-generator make-normal-generator make-exponential-generator
    make-bernoulli-generator make-binomial-generator
    make-geometric-generator make-poisson-generator
    make-categorical-generator)
  (begin

    ; Generators here follow (surfage s158)'s protocol directly (a thunk with
    ; no end-of-sequence — these are infinite streams) rather than importing
    ; that library, to avoid a hard dependency for such a small surface.
    ;
    ; Continuous distributions delegate to (curry random)'s samplers.
    ; Discrete ones (bernoulli/binomial/geometric/poisson/categorical) are
    ; not provided by (curry random) — its own header comment says discrete
    ; distributions are intentionally omitted in favor of T_QUANTUM — so
    ; they're implemented directly here from random-real/random-integer.
    ; make-zipf-generator and the geometry samplers (sphere/ball/rectangle)
    ; from the full SRFI-194 are out of scope for this subset.

    (define (make-random-integer-generator lo hi)
      (lambda () (+ lo (random-integer (- hi lo)))))

    (define (make-random-real-generator lo hi)
      (lambda () (+ lo (* (random-real) (- hi lo)))))

    (define (make-random-boolean-generator . p)
      (let ((prob (if (pair? p) (car p) 0.5)))
        (lambda () (< (random-real) prob))))

    (define (make-random-char-generator str)
      (let ((n (string-length str)))
        (lambda () (string-ref str (random-integer n)))))

    (define (make-uniform-generator lo hi)
      (let ((d (uniform-distribution lo hi)))
        (lambda () (sample d))))

    (define (make-normal-generator . args)
      (let ((d (normal-distribution (if (pair? args) (car args) 0.0)
                                     (if (and (pair? args) (pair? (cdr args))) (cadr args) 1.0))))
        (lambda () (sample d))))

    (define (make-exponential-generator . args)
      (let ((d (exponential-distribution (if (pair? args) (car args) 1.0))))
        (lambda () (sample d))))

    (define (make-bernoulli-generator . p)
      (make-random-boolean-generator (if (pair? p) (car p) 0.5)))

    (define (make-binomial-generator n . p)
      (let ((prob (if (pair? p) (car p) 0.5)))
        (lambda ()
          (let loop ((i 0) (successes 0))
            (if (= i n) successes
                (loop (+ i 1) (if (< (random-real) prob) (+ successes 1) successes)))))))

    (define (make-geometric-generator . p)
      (let ((prob (if (pair? p) (car p) 0.5)))
        (lambda ()
          (let loop ((trials 1))
            (if (< (random-real) prob) trials (loop (+ trials 1)))))))

    (define (make-poisson-generator mu)
      ; Knuth's algorithm: multiply uniforms until the product drops below e^-mu
      (let ((l (exp (- mu))))
        (lambda ()
          (let loop ((k 0) (p 1.0))
            (let ((p2 (* p (random-real))))
              (if (<= p2 l) k (loop (+ k 1) p2)))))))

    ;; weights is a list of (value . weight) pairs with positive weights
    (define (make-categorical-generator weights)
      (let* ((total (fold-left (lambda (acc wv) (+ acc (cdr wv))) 0 weights)))
        (lambda ()
          (let ((target (* (random-real) total)))
            (let loop ((ws weights) (acc 0))
              (if (null? (cdr ws))
                  (caar ws)
                  (let ((acc2 (+ acc (cdar ws))))
                    (if (< target acc2) (caar ws) (loop (cdr ws) acc2)))))))))))
