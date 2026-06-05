;;; bench_jit.scm — JIT vs bytecode performance benchmark
;;;
;;; Run with JIT build:  ./build-llvm/curry examples/bench_jit.scm
;;; Run with base build: ./build/curry      examples/bench_jit.scm
;;;
;;; Compare the two outputs side-by-side to measure actual JIT speedup.
;;; In the JIT build, jit-compile! is called before any timing loop so all
;;; iterations run compiled native code (no warmup overhead in the numbers).

(define jit? (guard (e (#t #f)) (curry-llvm-available?)))

(define (bench-ns/call thunk n)
  (let ((t0 (current-jiffy)))
    (let loop ((i n))
      (when (> i 0) (thunk) (loop (- i 1))))
    (/ (- (current-jiffy) t0) n)))

(define (maybe-compile! proc)
  (when jit? (jit-compile! proc)))

(define (show label ns n)
  (let* ((us (/ ns 1000.0))
         (tag (if jit? " [jit]" " [bc] ")))
    (display tag) (display " ")
    (display label) (display ": ")
    (display (round us)) (display " µs/call")
    (display "  (") (display n) (display " calls)")
    (newline)))

(define N 200)  ; iterations per benchmark

;;; ── 1. fib(25) — recursive, depth ≤ 25 ─────────────────────────────────
(define (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))
(maybe-compile! fib)
(fib 5) (fib 5)  ; warm icache
(show "fib(25)              " (bench-ns/call (lambda () (fib 25)) N) N)

;;; ── 2. fixnum tail loop: sum 1..50000 ────────────────────────────────────
(define (fixsum n acc)
  (if (= n 0) acc (fixsum (- n 1) (+ acc n))))
(maybe-compile! fixsum)
(fixsum 100 0)
(show "fixsum(50000)        " (bench-ns/call (lambda () (fixsum 50000 0)) N) N)

;;; ── 3. flonum tight loop ─────────────────────────────────────────────────
(define (floloop n acc)
  (if (= n 0) acc (floloop (- n 1) (+ (* acc 1.0000001) 0.000001))))
(maybe-compile! floloop)
(floloop 100 1.0)
(show "floloop(10000)       " (bench-ns/call (lambda () (floloop 10000 1.0)) N) N)

;;; ── 4. Closure: mutable upvalue counter ──────────────────────────────────
(define ctr (let ((n 0)) (lambda () (set! n (+ n 1)) n)))
(maybe-compile! ctr)
(ctr) (ctr)
(show "closure-counter(2000)" (bench-ns/call ctr (* N 10)) (* N 10))

;;; ── 5. map x*x over 500-element list ────────────────────────────────────
(define sample-list (let loop ((i 500) (a '()))
                      (if (= i 0) a (loop (- i 1) (cons i a)))))
(define (sq x) (* x x))
(maybe-compile! sq)
(sq 3)
(show "map sq 500 elems     " (bench-ns/call (lambda () (map sq sample-list)) N) N)

;;; ── 6. named-let loop ────────────────────────────────────────────────────
(define (namedlet-sum limit)
  (let loop ((i 0) (acc 0))
    (if (= i limit) acc (loop (+ i 1) (+ acc i)))))
(maybe-compile! namedlet-sum)
(namedlet-sum 100)
(show "named-let sum(10000) " (bench-ns/call (lambda () (namedlet-sum 10000)) N) N)

(newline)
(display (if jit?
             "Build: JIT active  — LLVM ORC v2 (run ./build/curry for baseline)"
             "Build: bytecode    — no JIT      (run ./build-llvm/curry for JIT)"))
(newline)
