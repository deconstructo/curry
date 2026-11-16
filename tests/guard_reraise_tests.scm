;;; A guard with no matching clause raises the object again with
;;; raise-continuable when the original raise was continuable (#406), and
;;; guard accepts (test => receiver) clauses (#422).
;;;
;;; R7RS 4.2.7. The re-raise used plain raise: an outer handler's value became
;;; the value of the whole with-exception-handler call, and the code after the
;;; raise-continuable was skipped (the first check below gave 42, not 43).
;;;
;;; Each case runs compiled (this file), tree-walked (tree-eval, eval, a
;;; loaded file) and inside a define-library body.

(define pass 0)
(define fail 0)
(define (check label got expected)
  (if (equal? got expected)
      (set! pass (+ pass 1))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write got)
             (display " expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))

;;; ---- compiled ----
(check "the issue's example: the outer handler's value returns to the raise point"
       (with-exception-handler (lambda (e) 42)
         (lambda () (+ 1 (guard (x ((string? x) 'str)) (raise-continuable 'oops)))))
       43)
(check "the code after the raise runs"
       (let ((log '()))
         (with-exception-handler (lambda (e) 'v)
           (lambda ()
             (guard (x ((string? x) 'str))
               (set! log (cons (raise-continuable 'one) log))
               (set! log (cons 'after log)))))
         log)
       '(after v))
(check "two guards with no matching clause"
       (with-exception-handler (lambda (e) 5)
         (lambda ()
           (* 2 (guard (a ((string? a) 0))
                  (+ 1 (guard (b ((number? b) 0)) (raise-continuable 'x)))))))
       12)
(check "the outer handler sees the same object"
       (with-exception-handler (lambda (e) (list 'outer e))
         (lambda () (guard (x ((string? x) 'str)) (raise-continuable 'oops))))
       '(outer oops))
(check "a clause that applies still ends the guard"
       (with-exception-handler (lambda (e) 42)
         (lambda () (+ 1 (guard (x ((symbol? x) 10)) (raise-continuable 'oops)))))
       11)
(check "an inner guard passes it on, an outer guard's clause applies"
       (guard (o ((symbol? o) (list 'outer o)))
         (+ 1 (guard (x ((string? x) 'str)) (raise-continuable 'oops))))
       '(outer oops))
(check "an else clause"
       (guard (x (else 'e)) (+ 1 (raise-continuable 1))) 'e)
(check "through dynamic-wind: the after thunk runs once, after the body"
       (let ((log '()))
         (list (with-exception-handler (lambda (e) 7)
                 (lambda ()
                   (dynamic-wind
                     (lambda () (set! log (cons 'in log)))
                     (lambda () (+ 1 (guard (x ((string? x) 0)) (raise-continuable 'q))))
                     (lambda () (set! log (cons 'out log))))))
               log))
       '(8 (out in)))
(check "the guard is usable again afterwards"
       (let ((f (lambda (v)
                  (with-exception-handler (lambda (e) (* e 2))
                    (lambda () (+ 1 (guard (x ((string? x) 0)) (raise-continuable v))))))))
         (list (f 1) (f 2) (f 3)))
       '(3 5 7))
(check "in a loop: no handler is left behind"
       (let loop ((i 0) (sum 0))
         (if (= i 1000)
             sum
             (loop (+ i 1)
                   (+ sum (with-exception-handler (lambda (e) 1)
                            (lambda () (guard (x ((string? x) 0)) (raise-continuable 'x))))))))
       1000)

;;; ---- a plain raise is not made continuable ----
(check "raise, no matching clause, outer guard"
       (guard (o (#t (list 'outer o))) (guard (x ((string? x) 'str)) (raise 'boom)))
       '(outer boom))
(check "raise, matching clause"
       (guard (x ((symbol? x) (list 'got x))) (raise 'boom)) '(got boom))
(check "an error, no matching clause, outer guard"
       (guard (o ((error-object? o) (error-object-message o)))
         (guard (x ((string? x) 'str)) (error "bad" 1)))
       "bad")
(check "a primitive's error, no matching clause"
       (guard (o ((error-object? o) 'outer)) (guard (x ((string? x) 'str)) (car 5)))
       'outer)
(check "raise-continuable with no handler outside reaches the top guard"
       (guard (top (#t (list 'top top))) (guard (x ((string? x) 's)) (raise-continuable 'zz)))
       '(top zz))

;;; ---- (test => receiver) clauses (#422) ----
(check "=> clause" (guard (x ((assq 'a x) => cdr)) (raise (list (cons 'a 42)))) 42)
(check "=> clause that does not apply, then another"
       (guard (x ((assq 'b x) => cdr) ((pair? x) 'pair)) (raise (list (cons 'a 42)))) 'pair)
(check "=> clause with raise-continuable"
       (guard (x ((assq 'a x) => cdr)) (+ 1 (raise-continuable (list (cons 'a 42))))) 42)
(check "=> with a lambda receiver"
       (guard (x ((and (number? x) (* x 2)) => (lambda (v) (+ v 1)))) (raise 5)) 11)
(check "a clause with only a test returns the test's value"
       (guard (x ((assq 'a x))) (raise (list (cons 'a 42)))) '(a . 42))

;;; ---- tree-walked ----
(define issue-example
  '(with-exception-handler (lambda (e) 42)
     (lambda () (+ 1 (guard (x ((string? x) 'str)) (raise-continuable 'oops))))))
(check "tree-eval: the issue's example" (tree-eval issue-example) 43)
(check "eval: the issue's example" (eval issue-example (interaction-environment)) 43)
(check "tree-eval: a clause that applies"
       (tree-eval '(with-exception-handler (lambda (e) 42)
                     (lambda () (+ 1 (guard (x ((symbol? x) 10)) (raise-continuable 'oops))))))
       11)
(check "tree-eval: two guards"
       (tree-eval '(with-exception-handler (lambda (e) 5)
                     (lambda ()
                       (* 2 (guard (a ((string? a) 0))
                              (+ 1 (guard (b ((number? b) 0)) (raise-continuable 'x))))))))
       12)
(check "tree-eval: a plain raise goes to the outer guard"
       (tree-eval '(guard (o (#t (list 'outer o))) (guard (x ((string? x) 'str)) (raise 'boom))))
       '(outer boom))
(check "tree-eval: through dynamic-wind"
       (tree-eval '(let ((log '()))
                     (list (guard (x ((symbol? x) 'caught))
                             (dynamic-wind
                               (lambda () (set! log (cons 'in log)))
                               (lambda () (raise-continuable 'q))
                               (lambda () (set! log (cons 'out log)))))
                           log)))
       '(caught (out in)))
(check "tree-eval: => clause"
       (tree-eval '(guard (x ((assq 'a x) => cdr)) (raise (list (cons 'a 42))))) 42)
(check "tree-eval: in a loop"
       (tree-eval '(let loop ((i 0) (sum 0))
                     (if (= i 1000)
                         sum
                         (loop (+ i 1)
                               (+ sum (with-exception-handler (lambda (e) 1)
                                        (lambda () (guard (x ((string? x) 0)) (raise-continuable 'x)))))))))
       1000)
(check "a tree-walked guard inside a compiled handler frame"
       (with-exception-handler (lambda (e) 42)
         (lambda () (+ 1 (tree-eval '(guard (x ((string? x) 'str)) (raise-continuable 'oops))))))
       43)
(check "a compiled guard inside a tree-walked handler frame"
       (let ((thunk (lambda () (+ 1 (guard (x ((string? x) 'str)) (raise-continuable 'oops))))))
         (tree-eval (list 'with-exception-handler '(lambda (e) 42) (list 'quote thunk))))
       43)

;;; ---- a loaded file ----
(load "fixtures/guard_reraise/loaded.scm")
(check "loaded file: at top level" loaded-top-level 43)
(check "loaded file: in a procedure" (loaded-procedure) 43)
(check "loaded file: => clause" (loaded-arrow) 42)

;;; ---- inside a library that imports none of call/cc, raise,
;;; raise-continuable or with-exception-handler: guard is a special form and
;;; needs none of them bound (#423: "unbound variable: call/cc") ----
(define-library (test guard-reraise)
  (import (only (scheme base) define lambda + string? quote))
  (export pass-on)
  (begin
    (define (pass-on thunk) (+ 1 (guard (x ((string? x) 'str)) (thunk))))))
(import (test guard-reraise))
(check "library: continuable, passed on"
       (with-exception-handler (lambda (e) 42) (lambda () (pass-on (lambda () (raise-continuable 'oops)))))
       43)
(check "library: plain raise, passed on"
       (guard (o (#t (list 'outer o))) (pass-on (lambda () (raise 'boom))))
       '(outer boom))

(check "library: no exception at all" (pass-on (lambda () 5)) 6)
(check "library: a clause applies (its value, a symbol, then fails in +)"
       (guard (e ((error-object? e) 'type-error)) (pass-on (lambda () (raise "a string"))))
       'type-error)
(check "guard where call/cc, raise and friends are shadowed"
       (let ((call/cc (lambda (x) 'shadowed)) (raise-continuable 1)
             (with-exception-handler 2) (call-with-current-continuation 3))
         (guard (e (#t (list 'caught e))) (car 5) 'not-reached)
         (guard (e ((symbol? e) (list 'caught e))) (raise 'x)))
       '(caught x))
(check "guard in a deep recursion"
       (letrec ((deep (lambda (n) (if (= n 0) (raise 'bottom) (+ 1 (deep (- n 1)))))))
         (guard (e (#t e)) (deep 1000)))
       'bottom)
(check "a hundred thousand guards in a loop"
       (let loop ((i 0) (acc 0))
         (if (= i 100000)
             acc
             (loop (+ i 1) (+ acc (guard (e (#t 1)) (if (odd? i) (raise 'x) 2))))))
       150000)

(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
