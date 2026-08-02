;;; srfi_s252_property_testing_tests.scm — (srfi s252 property-testing)

(import (srfi s252 property-testing))
(import (srfi s158 generators-and-accumulators))
(import (srfi s64 testing))
(import (srfi s1 lists))

(define pass 0)
(define fail 0)

(define-syntax check
  (syntax-rules ()
    ((_ label expr expected)
     (let ((got expr))
       (if (equal? got expected)
           (begin (set! pass (+ pass 1)))
           (begin
             (set! fail (+ fail 1))
             (display "FAIL: ") (display label) (newline)
             (display "  expected: ") (write expected) (newline)
             (display "  got:      ") (write got) (newline)))))))

;;; ---- run a self-contained property-test group on a null runner and
;;;      inspect the resulting counts, without printing anything ----

(define (run-group thunk)
  (let ((r (test-runner-null)))
    (test-with-runner r
      (test-begin "group")
      (thunk)
      (test-end "group"))
    (list (test-runner-pass-count r) (test-runner-fail-count r)
          (test-runner-xpass-count r) (test-runner-xfail-count r)
          (test-runner-skip-count r))))

;;; ---- test-property ----

(check "test-property: always-true property passes"
       (run-group (lambda () (test-property (lambda (x) #t) (list (integer-generator)) 20)))
       (list 1 0 0 0 0))

(check "test-property: always-false property fails"
       (run-group (lambda () (test-property (lambda (x) #f) (list (integer-generator)) 5)))
       (list 0 1 0 0 0))

(check "test-property: raising property counts as a failure"
       (run-group (lambda () (test-property (lambda (x) (car x)) (list (integer-generator)) 5)))
       (list 0 1 0 0 0))

(check "test-property: identity property genuinely holds for many integers"
       (run-group (lambda () (test-property (lambda (x) (= x x)) (list (integer-generator)) 200)))
       (list 1 0 0 0 0))

(check "test-property: multiple generators, arity matches property"
       (run-group
         (lambda ()
           (test-property (lambda (x y) (= (+ x y) (+ y x)))
                           (list (integer-generator) (integer-generator))
                           50)))
       (list 1 0 0 0 0))

;;; ---- test-property-expect-fail ----

(check "test-property-expect-fail: a genuinely-false property is xfail"
       (run-group (lambda () (test-property-expect-fail (lambda (x) #f) (list (integer-generator)) 5)))
       (list 0 0 0 1 0))

(check "test-property-expect-fail: a property that actually holds is xpass"
       (run-group (lambda () (test-property-expect-fail (lambda (x) #t) (list (integer-generator)) 5)))
       (list 0 0 1 0 0))

;;; ---- test-property-skip ----

(check "test-property-skip: never runs the property, just skip-counts"
       (run-group
         (lambda ()
           (test-property-skip (lambda (x) (error "should never be called")) (list (integer-generator)))))
       (list 0 0 0 0 1))

;;; ---- test-property-error / test-property-error-type ----

(check "test-property-error: property that always raises passes"
       (run-group (lambda () (test-property-error (lambda (x) (car x)) (list (integer-generator)) 5)))
       (list 1 0 0 0 0))

(check "test-property-error: property that never raises fails"
       (run-group (lambda () (test-property-error (lambda (x) x) (list (integer-generator)) 5)))
       (list 0 1 0 0 0))

(check "test-property-error-type: matching error type passes"
       (run-group
         (lambda ()
           (test-property-error-type error-object? (lambda (x) (error "boom" x))
                                      (list (integer-generator)) 5)))
       (list 1 0 0 0 0))

(check "test-property-error-type: non-matching error type fails"
       (run-group
         (lambda ()
           (test-property-error-type (lambda (e) #f) (lambda (x) (error "boom" x))
                                      (list (integer-generator)) 5)))
       (list 0 1 0 0 0))

;;; ---- generator exhaustion ----

(check "test-property: exhausted s158 generator is a failure, not a crash"
       (run-group
         (lambda ()
           (test-property (lambda (x) #t) (list (list->generator (list 1 2))) 5)))
       (list 0 1 0 0 0))

;;; ---- generator sanity: basic types ----

(define (sample gen n)
  (let loop ((i 0) (acc '()))
    (if (>= i n) (reverse acc) (loop (+ i 1) (cons (gen) acc)))))

(let ((g (boolean-generator)))
  (check "boolean-generator: prefix is #t then #f" (list (g) (g)) (list #t #f))
  (check "boolean-generator: random tail is boolean" (boolean? (g)) #t))

(let ((g (char-generator)))
  (check "char-generator: prefix is #\\null" (g) #\null)
  (check "char-generator: random tail is a char" (char? (g)) #t))

(let ((g (string-generator)))
  (check "string-generator: prefix is empty string" (g) "")
  (check "string-generator: random tail is a string" (string? (g)) #t))

(let ((g (symbol-generator)))
  (check "symbol-generator: prefix is a symbol" (symbol? (g)) #t)
  (check "symbol-generator: random tail is a symbol" (symbol? (g)) #t))

(let ((g (bytevector-generator)))
  (check "bytevector-generator: prefix is empty bytevector" (bytevector-length (g)) 0)
  (check "bytevector-generator: random tail is a bytevector" (bytevector? (g)) #t))

;;; ---- generator sanity: numbers ----

(check "exact-integer-generator: prefix is 0 1 -1"
       (sample (exact-integer-generator) 3) (list 0 1 -1))
(check "exact-integer-generator: random tail is exact and an integer"
       (let ((g (exact-integer-generator))) (g) (g) (g) (let ((v (g))) (list (exact? v) (integer? v))))
       (list #t #t))

(check "exact-rational-generator: random tail is exact and rational"
       (let ((g (exact-rational-generator)))
         (let loop ((i 0)) (if (< i 5) (begin (g) (loop (+ i 1))) #f))
         (let ((v (g))) (list (exact? v) (rational? v))))
       (list #t #t))

(check "inexact-integer-generator: random tail is inexact and an integer"
       (let ((g (inexact-integer-generator)))
         (let loop ((i 0)) (if (< i 4) (begin (g) (loop (+ i 1))) #f))
         (let ((v (g))) (list (inexact? v) (integer? v))))
       (list #t #t))

(check "inexact-real-generator: random tail is inexact and real"
       (let ((g (inexact-real-generator)))
         (let loop ((i 0)) (if (< i 9) (begin (g) (loop (+ i 1))) #f))
         (let ((v (g))) (list (inexact? v) (real? v))))
       (list #t #t))

(check "inexact-complex-generator: random tail is complex"
       (let ((g (inexact-complex-generator)))
         (let loop ((i 0)) (if (< i 7) (begin (g) (loop (+ i 1))) #f))
         (complex? (g)))
       #t)

(check "integer-generator: values are always integers"
       (let ((g (integer-generator))) (every integer? (sample g 30)))
       #t)

(check "number-generator: values are always numbers"
       (let ((g (number-generator))) (every number? (sample g 30)))
       #t)

; curry's exact?/inexact? both return #f for complex numbers (a numeric-tower
; quirk independent of this SRFI), so only complex? is checked here.
(check "complex-generator: aliases inexact-complex-generator (curry has no exact complex)"
       (let ((g (complex-generator))) (every complex? (sample g 10)))
       #t)

(check "exact-complex-generator: raises (curry has no exact complex numbers)"
       (guard (e (#t 'raised)) (exact-complex-generator) ((exact-complex-generator)))
       'raised)

;;; ---- composite generators ----

(check "list-generator-of: prefix is the empty list"
       ((list-generator-of (integer-generator)))
       '())

(check "list-generator-of: random tail is a nonempty list of the subgenerator's type"
       (let ((g (list-generator-of (integer-generator))))
         (g)
         (let ((v (g))) (list (pair? v) (every integer? v))))
       (list #t #t))

(check "vector-generator-of: prefix is the empty vector"
       ((vector-generator-of (boolean-generator)))
       #())

(check "vector-generator-of: random tail is a nonempty vector of the subgenerator's type"
       (let ((g (vector-generator-of (boolean-generator))))
         (g)
         (let ((v (g))) (list (> (vector-length v) 0) (every boolean? (vector->list v)))))
       (list #t #t))

(check "pair-generator-of: one subgenerator populates both car and cdr"
       (let ((g (pair-generator-of (lambda () 42))))
         (let ((p (g))) (list (car p) (cdr p))))
       (list 42 42))

(check "pair-generator-of: two subgenerators populate car and cdr separately"
       (let ((g (pair-generator-of (lambda () 'a) (lambda () 'b))))
         (g))
       (cons 'a 'b))

(check "procedure-generator-of: each generated procedure ignores its arguments"
       (let ((g (procedure-generator-of (lambda () 99))))
         (let ((p (g))) (list (p) (p 1) (p 1 2 3))))
       (list 99 99 99))

;;; ---- property-test-runner ----

(check "property-test-runner produces a usable test-runner"
       (test-runner? (property-test-runner))
       #t)

; Regression: property-args/actual-error must be set on the runner's
; result-alist BEFORE on-test-end fires, not after -- property-test-runner's
; own on-test-end reads them, and a naive "set them after %run-assert
; returns" implementation is too late, since on-test-end already fired
; synchronously inside %run-assert by the time it returns.
(check "property-test-runner reports the failing inputs on a real failure"
       (let ((out (with-output-to-string
                     (lambda ()
                       (test-with-runner (property-test-runner)
                         (test-begin "regression")
                         (test-property (lambda (x) #f) (list (lambda () 42)) 3)
                         (test-end "regression"))))))
         (list (and (string-contains out "failing inputs") #t) (and (string-contains out "42") #t)))
       (list #t #t))

(check "property-test-runner reports the underlying error on test-property-error-type failure"
       (let ((out (with-output-to-string
                     (lambda ()
                       (test-with-runner (property-test-runner)
                         (test-begin "regression2")
                         (test-property-error-type (lambda (e) #f)
                                                    (lambda (x) (error "boom"))
                                                    (list (lambda () 7)) 3)
                         (test-end "regression2"))))))
         (and (string-contains out "boom") #t))
       #t)

; Regression: recursing from inside `guard` (rather than after it returns)
; grew an unreclaimed stack frame per trial and segfaulted around ~100k+
; runs. This run count is chosen well past where the old implementation
; crashed, on a property cheap enough that the loop itself is the only cost.
(check "test-property survives a very large run count without crashing"
       (run-group (lambda () (test-property (lambda (x) #t) (list (integer-generator)) 150000)))
       (list 1 0 0 0 0))

;;; ---- Summary ----

(newline)
(display "srfi-s252 tests: ")
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0)
    (begin (display "SOME TESTS FAILED") (newline) (exit 1))
    (begin (display "all OK") (newline)))
