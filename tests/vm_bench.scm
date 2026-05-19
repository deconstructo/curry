;;; tests/vm_bench.scm — bytecode VM vs tree-walking interpreter benchmark
;;;
;;; Usage: ./build/curry tests/vm_bench.scm
;;;
;;; Each benchmark is defined twice:
;;;   - VM version: normal (define ...) → compiled to BcClosure by the compiler
;;;   - TW version: (tree-eval '(define ...)) → interpreted T_PROC closure
;;;
;;; Recursive calls within TW functions stay in the tree-walker because the
;;; env binding is a T_PROC, which dispatches through eval() at every call.

;; ── Timing ──────────────────────────────────────────────────────────────────

;; Returns milliseconds per call for (thunk) repeated n times.
(define (bench-ms thunk n)
  (define t0 (current-jiffy))
  (define i 0)
  (let run ()
    (when (< i n)
      (thunk)
      (set! i (+ i 1))
      (run)))
  (/ (* 1000.0 (- (current-jiffy) t0))
     (* (jiffies-per-second) n)))

;; ── VM versions (compiled to bytecode) ──────────────────────────────────────

(define (fib n)
  (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))

(define (tak x y z)
  (if (not (< y x)) z
      (tak (tak (- x 1) y z)
           (tak (- y 1) z x)
           (tak (- z 1) x y))))

(define (count-down n)
  (let lp ((i n)) (if (= i 0) i (lp (- i 1)))))

(define (list-fold lst)
  (let lp ((l lst) (acc 0))
    (if (null? l) acc (lp (cdr l) (+ acc (car l))))))

(define (map-square lst)
  (map (lambda (x) (* x x)) lst))

;; ── Tree-walker versions (T_PROC closures, stay in eval() at runtime) ───────

(tree-eval '(define (fib-tw n)
               (if (< n 2) n
                   (+ (fib-tw (- n 1)) (fib-tw (- n 2))))))

(tree-eval '(define (tak-tw x y z)
               (if (not (< y x)) z
                   (tak-tw (tak-tw (- x 1) y z)
                           (tak-tw (- y 1) z x)
                           (tak-tw (- z 1) x y)))))

(tree-eval '(define (count-down-tw n)
               (let lp ((i n)) (if (= i 0) i (lp (- i 1))))))

(tree-eval '(define (list-fold-tw lst)
               (let lp ((l lst) (acc 0))
                 (if (null? l) acc (lp (cdr l) (+ acc (car l)))))))

(tree-eval '(define (map-square-tw lst)
               (map (lambda (x) (* x x)) lst)))

;; ── Shared input data ────────────────────────────────────────────────────────

(define bench-list
  (let lp ((i 999) (acc '()))
    (if (< i 0) acc (lp (- i 1) (cons i acc)))))

;; ── Reporter ─────────────────────────────────────────────────────────────────

;; Pad string s to at least width chars by appending spaces.
(define (pad-right s width)
  (let lp ((out s))
    (if (>= (string-length out) width) out
        (lp (string-append out " ")))))

(define (fmt-ms ms)
  (number->string (/ (round (* (exact->inexact ms) 100)) 100.0)))

;; Run one benchmark, print a result row, return speedup ratio.
(define (run-bench label n vm-thunk tw-thunk)
  (define vm-ms (bench-ms vm-thunk n))
  (define tw-ms (bench-ms tw-thunk n))
  (define ratio (/ (round (* (/ tw-ms vm-ms) 10)) 10.0))
  (display (pad-right label 22))
  (display "  VM ") (display (fmt-ms vm-ms)) (display " ms")
  (display "    TW ") (display (fmt-ms tw-ms)) (display " ms")
  (display "    ") (display (exact->inexact ratio)) (display "x")
  (newline)
  ratio)

;; ── Run ──────────────────────────────────────────────────────────────────────

(display "benchmark               VM (ms/iter)    TW (ms/iter)    speedup") (newline)
(display "────────────────────────────────────────────────────────────────") (newline)

(define r1  (run-bench "fib(28)"          5   (lambda () (fib 28))               (lambda () (fib-tw 28))))
(define r2  (run-bench "tak(18,12,6)"     5   (lambda () (tak 18 12 6))          (lambda () (tak-tw 18 12 6))))
(define r3  (run-bench "count-down(1M)"   5   (lambda () (count-down 1000000))   (lambda () (count-down-tw 1000000))))
(define r4  (run-bench "list-fold(1000)" 50   (lambda () (list-fold bench-list)) (lambda () (list-fold-tw bench-list))))
(define r5  (run-bench "map-square(1000)" 50  (lambda () (map-square bench-list))(lambda () (map-square-tw bench-list))))

(display "────────────────────────────────────────────────────────────────") (newline)
(display "geometric mean speedup: ")
(define ratios (list r1 r2 r3 r4 r5))
(define gmean
  (exp (/ (apply + (map log ratios)) (length ratios))))
(display (exact->inexact (/ (round (* gmean 10)) 10)))
(display "x")
(newline)
