;;; spacetime_tests.scm — (curry glsl) and (curry spacetime), no GPU needed
;;;
;;; sym->glsl output, common-subexpression elimination, the CAS's metric
;;; derivatives against central finite differences, and the Kerr–Schild
;;; metrics' basic properties.  The GPU geodesic checks are in
;;; spacetime_gpu_tests.scm.

(import (scheme base) (scheme inexact) (scheme write)
        (curry glsl) (curry spacetime))

(define pass 0)
(define fail 0)

(define-syntax check
  (syntax-rules ()
    ((_ label expr expected)
     (let ((got (guard (e (#t (list 'raised (if (error-object? e) (error-object-message e) e)))) expr)))
       (if (equal? got expected)
           (set! pass (+ pass 1))
           (begin
             (set! fail (+ fail 1))
             (display "FAIL: ") (display label) (newline)
             (display "  expected: ") (write expected) (newline)
             (display "  got:      ") (write got) (newline)))))))

(define (raises? thunk) (guard (e (#t #t)) (thunk) #f))

(define (string-contains? s sub)
  (let ((n (string-length s)) (m (string-length sub)))
    (let loop ((i 0))
      (cond ((> (+ i m) n) #f)
            ((string=? (substring s i (+ i m)) sub) #t)
            (else (loop (+ i 1)))))))

;;; ---- Part 1: (curry glsl) ---------------------------------------------------

(define r (sym-var 'r 'positive))
(define M (sym-var 'M 'positive))
(define vm '((r . "r") (M . "mass")))

(check "number literal gets a decimal point" (glsl-float 2) "2.0")
(check "rational literal" (glsl-float 1/4) "0.25")
(check "simple expression" (sym->glsl (/ (* 2 M) r) vm) "((2.0 * mass) / r)")
(check "integer power expands" (sym->glsl (expt r 3) vm) "(r * r * r)")
(check "negative power" (sym->glsl (expt r -2) vm) "(1.0 / (r * r))")
(check "square root" (sym->glsl (expt r 1/2) vm) "sqrt(r)")
(check "inverse square root" (sym->glsl (expt r -1/2) vm) "inversesqrt(r)")
(check "functions" (sym->glsl (sin (* 2 r)) vm) "sin((2.0 * r))")
(check "unmapped variable raises" (raises? (lambda () (sym->glsl (* r (sym-var 'q)) vm))) #t)
(check "complex number raises" (raises? (lambda () (sym->glsl (* r (make-rectangular 1 2)) vm))) #t)
(check "CSE computes a shared term once"
       (let ((code (glsl-assignments
                    (list (cons "a" (* r (sqrt (/ (* 2 M) r))))
                          (cons "b" (+ 1 (sqrt (/ (* 2 M) r)))))
                    vm)))
         (and (string-contains? code "sqrt(")
              (string-contains? code "a = ")
              (string-contains? code "b = ")
              ;; sqrt appears once: in its temporary
              (let loop ((i 0) (n 0))
                (cond ((> (+ i 5) (string-length code)) (= n 1))
                      ((string=? (substring code i (+ i 5)) "sqrt(") (loop (+ i 1) (+ n 1)))
                      (else (loop (+ i 1) n))))))
       #t)

;; review of #347: var-map expressions keep their meaning; power bases
;; are computed once in glsl-assignments
(check "var-map expression is parenthesized" (sym->glsl (* 2 r) '((r . "a + b"))) "(2.0 * (a + b))")
(check "var-map member access is left alone" (sym->glsl (* 2 r) '((r . "u.r"))) "(2.0 * u.r)")
(check "power base becomes a temporary"
       (let ((code (glsl-assignments (list (cons "y" (expt (+ r M) 4))) vm)))
         (and (string-contains? code "float cse0 = (r + mass);")
              (string-contains? code "y = (cse0 * cse0 * cse0 * cse0);")))
       #t)

;;; ---- Part 1: (curry spacetime) -------------------------------------------------

(define coords (spacetime-coordinates))

(define (eval-at expr point params)
  ;; point: (t x y z); params: alist of (sym-var . value)
  (let loop ((e expr) (vs (append coords (map car params))) (xs (append point (map cdr params))))
    (if (or (null? vs) (number? e)) e (loop (substitute e (car vs) (car xs)) (cdr vs) (cdr xs)))))

(define kerr (kerr-metric))
(define kerr-params (map (lambda (p) (cons p (if (eq? (sym-var-name p) 'M) 1.0 0.9))) (metric-parameters kerr)))

(check "Kerr has parameters M and a" (map sym-var-name (metric-parameters kerr)) '(M a))
(check "Minkowski g_tt" (metric-component (minkowski-metric) 0 0) -1)

;; Far away, Kerr approaches flat space.
(check "Kerr is nearly flat far away"
       (let ((gtt (eval-at (metric-component kerr 0 0) '(0.0 1e6 0.0 0.0) kerr-params)))
         (< (abs (+ gtt 1.0)) 1e-5))
       #t)

;; CAS derivatives against central differences at an off-axis point.
(define (finite-difference i j k)
  (let* ((h 1e-5) (p '(0.0 3.1 -2.3 1.7))
         (shift (lambda (s) (let loop ((q p) (n 0) (acc '()))
                              (if (null? q) (reverse acc)
                                  (loop (cdr q) (+ n 1) (cons (if (= n k) (+ (car q) s) (car q)) acc)))))))
    (/ (- (eval-at (metric-component kerr i j) (shift h) kerr-params)
          (eval-at (metric-component kerr i j) (shift (- h)) kerr-params))
       (* 2 h))))
(define kerr-derivs (metric-derivatives kerr))
(define (cas-derivative i j k)
  (eval-at (vector-ref (vector-ref (vector-ref kerr-derivs k) i) j) '(0.0 3.1 -2.3 1.7) kerr-params))
(check "CAS ∂g agrees with finite differences for every Kerr component"
       (let loop ((i 0) (j 0) (k 1) (worst 0.0))
         (cond ((= k 4) (< worst 1e-6))
               ((= i 4) (loop 0 0 (+ k 1) worst))
               ((= j 4) (loop (+ i 1) (+ i 1) k worst))
               (else (loop i (+ j 1) k (max worst (abs (- (cas-derivative i j k) (finite-difference i j k))))))))
       #t)
(check "stationary metric: ∂_t g = 0"
       (let ((d0 (vector-ref kerr-derivs 0)))
         (let loop ((i 0) (j 0))
           (cond ((= i 4) #t)
                 ((= j 4) (loop (+ i 1) 0))
                 ((not (eqv? 0 (eval-at (vector-ref (vector-ref d0 i) j) '(0.0 3.1 -2.3 1.7) kerr-params)))
                  (let ((v (eval-at (vector-ref (vector-ref d0 i) j) '(0.0 3.1 -2.3 1.7) kerr-params)))
                    (and (< (abs v) 1e-12) (loop i (+ j 1)))))
                 (else (loop i (+ j 1))))))
       #t)
(check "make-metric rejects an asymmetric metric"
       (raises? (lambda ()
         (make-metric (list (list -1 (car (cdr coords)) 0 0) '(0 1 0 0) '(0 0 1 0) '(0 0 0 1)) '())))
       #t)
(check "metric->glsl needs every parameter mapped"
       (raises? (lambda () (metric->glsl kerr '((M . "mass"))))) #t)
(check "metric->glsl emits metric_eval"
       (string-contains? (metric->glsl (schwarzschild-metric) '((M . "mass"))) "void metric_eval(") #t)

;;; ---- Summary ----

(newline)
(display "spacetime tests: ")
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0)
    (begin (display "SOME TESTS FAILED") (newline) (exit 1))
    (begin (display "all OK") (newline)))
