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

;;; ---- from the review ----
(define (raises? thunk) (guard (e (#t #t)) (thunk) #f))

;;; A handler slot was leaked each time a tree-walked guard caught what a
;;; compiled guard passed on in place; after 64, "handler stack overflow" or a
;;; segmentation fault (#426).
(define (compiled-pass-on v) (guard (e ((symbol? e) (list 'sym e))) (raise-continuable v)))
(check "a tree-walked guard catching past a compiled guard, 1000 times"
       (let loop ((i 0) (acc 0))
         (if (= i 1000)
             acc
             (loop (+ i 1) (+ acc (tree-eval '(guard (e ((string? e) 1)) (compiled-pass-on "s")))))))
       1000)
(check "the same inside a compiled guard"
       (let loop ((i 0) (acc 0))
         (if (= i 1000)
             acc
             (loop (+ i 1)
                   (+ acc (guard (o ((symbol? o) 1))
                            (tree-eval '(guard (e ((string? e) 2)) (compiled-pass-on "s"))))))))
       2000)
(check "a compiled handler that raises on, caught by the with-exception-handler procedure"
       (let ((weh with-exception-handler))
         (let loop ((i 0) (acc 0))
           (if (= i 1000)
               acc
               (loop (+ i 1)
                     (+ acc (call/cc (lambda (k)
                              (weh (lambda (e) (k 1))
                                   (lambda ()
                                     (with-exception-handler (lambda (e) (raise e))
                                       (lambda () (raise-continuable 'x))))))))))))
       1000)

;;; Where a clause runs. Its test is tried where the guard sees the raise (at
;;; the raise point for a raise-continuable, so the re-raise can return there
;;; when nothing matches); its body runs after the dynamic-wind frames between
;;; the raise and the guard are left, after their after thunks, as R7RS 6.11
;;; has it (#431).
(define log '())
(define (L x) (set! log (cons x log)))
(define (taken) (let ((l (reverse log))) (set! log '()) l))
(check "compiled: clause after the after thunk, for raise-continuable"
       (list (guard (e (#t (L 'clause) 'v))
               (dynamic-wind (lambda () (L 'in)) (lambda () (raise-continuable 'x)) (lambda () (L 'out))))
             (taken))
       '(v (in out clause)))
(check "compiled: clause after the after thunk, for raise"
       (list (guard (e (#t (L 'clause) 'v))
               (dynamic-wind (lambda () (L 'in)) (lambda () (raise 'x)) (lambda () (L 'out))))
             (taken))
       '(v (in out clause)))
(check "tree-walked: the same order for raise-continuable"
       (list (tree-eval '(guard (e (#t (L 'clause) 'v))
                           (dynamic-wind (lambda () (L 'in)) (lambda () (raise-continuable 'x)) (lambda () (L 'out)))))
             (taken))
       '(v (in out clause)))
(check "tree-walked: the same order for raise"
       (list (tree-eval '(guard (e (#t (L 'clause) 'v))
                           (dynamic-wind (lambda () (L 'in)) (lambda () (raise 'x)) (lambda () (L 'out)))))
             (taken))
       '(v (in out clause)))
;; The after thunk runs with the handlers outside its dynamic-wind (#425), so
;; the inner guard, which is outside it, handles the thunk's raise as well; the
;; clause runs once, for that exception (R7RS), after the after thunk.
(check "tree-walked: an after thunk that raises runs once"
       (list (tree-eval '(guard (o (#t (list 'outer o)))
                           (guard (e (#t (L 'clause) 'v))
                             (dynamic-wind (lambda () (L 'in))
                                           (lambda () (raise-continuable 'x))
                                           (lambda () (L 'out) (raise 'from-after))))))
             (taken))
       '(v (in out clause)))

;; The same, compiled, and in the other clause shapes (#431).
(check "compiled: an after thunk that raises, the clause runs once"
       (list (guard (o (#t (list 'outer o)))
               (guard (e (#t (L 'clause) 'v))
                 (dynamic-wind (lambda () (L 'in))
                               (lambda () (raise-continuable 'x))
                               (lambda () (L 'out) (raise 'from-after)))))
             (taken))
       '(v (in out clause)))
(check "compiled: the clause sees the exception that picked it (the thunk's)"
       (guard (o (#t (list 'outer o)))
         (guard (e (#t e))
           (dynamic-wind (lambda () #f) (lambda () (raise-continuable 'x))
                         (lambda () (raise 'from-after)))))
       'from-after)
(check "compiled: => receiver is called after the after thunk"
       (list (guard (e ((assq 'a e) => (lambda (p) (L 'receiver) (cdr p))))
               (dynamic-wind (lambda () (L 'in))
                             (lambda () (raise-continuable (list (cons 'a 42))))
                             (lambda () (L 'out))))
             (taken))
       '(42 (in out receiver)))
(check "tree-walked: => receiver is called after the after thunk"
       (list (tree-eval '(guard (e ((assq 'a e) => (lambda (p) (L 'receiver) (cdr p))))
                           (dynamic-wind (lambda () (L 'in))
                                         (lambda () (raise-continuable (list (cons 'a 42))))
                                         (lambda () (L 'out)))))
             (taken))
       '(42 (in out receiver)))
(check "compiled: else clause after the after thunk"
       (list (guard (e (else (L 'else) 'v))
               (dynamic-wind (lambda () (L 'in)) (lambda () (raise-continuable 'x)) (lambda () (L 'out))))
             (taken))
       '(v (in out else)))
(check "compiled: a clause with only a test"
       (list (guard (e ((memq e '(a b c))))
               (dynamic-wind (lambda () (L 'in)) (lambda () (raise-continuable 'b)) (lambda () (L 'out))))
             (taken))
       '((b c) (in out)))
(check "compiled: the clause test still runs at the raise point"
       (list (guard (e ((begin (L 'test) #t) (L 'body) 'v))
               (dynamic-wind (lambda () (L 'in)) (lambda () (raise-continuable 'x)) (lambda () (L 'out))))
             (taken))
       '(v (in test out body)))
(check "compiled: no clause matches, the outer handler's value returns to the raise point"
       (list (with-exception-handler (lambda (e) 10)
               (lambda ()
                 (guard (e2 ((string? e2) 'no))
                   (dynamic-wind (lambda () (L 'in))
                                 (lambda () (+ 1 (raise-continuable 'x)))
                                 (lambda () (L 'out))))))
             (taken))
       '(11 (in out)))
(check "compiled: a clause body with several expressions, after the escape"
       (list (guard (e (#t (L 'one) (L 'two) 'last))
               (dynamic-wind (lambda () (L 'in)) (lambda () (raise-continuable 'x)) (lambda () (L 'out))))
             (taken))
       '(last (in out one two)))
(check "compiled: the guard's value can be several values"
       (call-with-values (lambda () (guard (e (#t #f)) (values 1 2 3))) list)
       '(1 2 3))
(check "compiled: the exception variable keeps its name inside the body"
       (guard (my-var (#t (list 'got my-var))) (raise-continuable 'x))
       '(got x))
(check "compiled: a body that raises goes outward after the after thunk"
       (list (guard (o (#t (list 'outer o)))
               (guard (e (#t (L 'body) (raise 'from-body)))
                 (dynamic-wind (lambda () (L 'in)) (lambda () (raise-continuable 'x)) (lambda () (L 'out)))))
             (taken))
       '((outer from-body) (in out body)))
(check "nested guards: the inner clause body and the outer one in order"
       (list (guard (o (#t (L 'outer) (list 'outer o)))
               (guard (e ((symbol? e) (L 'inner) (raise 'again)))
                 (dynamic-wind (lambda () (L 'in)) (lambda () (raise-continuable 'x)) (lambda () (L 'out)))))
             (taken))
       '((outer again) (in out inner outer)))
(check "a guard in a loop, repeated"
       (let loop ((i 0) (acc 0))
         (if (= i 300) acc
             (loop (+ i 1) (+ acc (guard (e (#t (+ e 1))) (raise-continuable i))))))
       45150)
(check "a guard whose body returns normally still returns the value"
       (guard (e (#t 'caught)) (+ 1 2)) 3)
(check "the value is not mistaken for a clause pick"
       (guard (e (#t 'caught)) #f) #f)

;;; An exception from a clause goes outward, not back into the same guard.
(check "a clause test that raises (in place)"
       (guard (o (#t (list 'outer o))) (guard (e ((raise 'from-test) 1)) (raise-continuable 'x)))
       '(outer from-test))
(check "a clause body that raises (after unwinding)"
       (guard (o (#t (list 'outer o))) (guard (e (#t (raise 'from-body))) (raise 'x)))
       '(outer from-body))
(check "a clause test that itself raises continuably"
       (with-exception-handler (lambda (e) #t)
         (lambda () (guard (e ((raise-continuable (list 'test e)) (list 'matched e))) (+ 1 (raise-continuable 10)))))
       '(matched 10))
(check "tree-walked: a clause test that raises"
       (tree-eval '(guard (o (#t (list 'outer o))) (guard (e ((raise 'from-test) 1)) (raise-continuable 'x))))
       '(outer from-test))

;;; Clause shapes.
(check "else with raise-continuable" (guard (e (else (list 'else e))) (+ 1 (raise-continuable 'x))) '(else x))
(check "no clauses, no exception" (guard (e) 5) 5)
(check "no clauses, raise" (guard (o (#t (list 'o o))) (guard (e) (raise 5))) '(o 5))
(check "no clauses, raise-continuable"
       (with-exception-handler (lambda (e) 9) (lambda () (+ 1 (guard (e) (raise-continuable 5))))) 10)
(check "a clause whose test is #f" (guard (o (#t (list 'o o))) (guard (e (#f 1)) (raise 5))) '(o 5))
(check "=> with no receiver is an error"
       (raises? (lambda () (eval '(guard (e ((car e) =>)) (raise '(1))) (interaction-environment)))) #t)
(check "=> with two receivers is an error"
       (raises? (lambda () (eval '(guard (e ((car e) => list list)) (raise '(1))) (interaction-environment)))) #t)

;;; The names the expansion uses are not the program's.
(check "a program variable named %guard-unwound"
       (let ((%guard-unwound 'mine)) (guard (e (#t %guard-unwound)) (raise 1))) 'mine)
(check "a guard variable named %guard-unwound"
       (guard (%guard-unwound ((symbol? %guard-unwound) (list 'got %guard-unwound))) (raise 'a)) '(got a))
(check "a program variable named %guard-k"
       (let ((%guard-k 'mine)) (guard (e (#t %guard-k)) (raise 1))) 'mine)
(check "a receiver named %guard-v"
       (let ((%guard-v (lambda (x) (list 'recv x)))) (guard (e ((car e) => %guard-v)) (raise '(7)))) '(recv 7))

;;; Several threads at once.
(import (curry sync))
(define actor-total 0)
(define actor-mutex (make-mutex))
(define actor-done (make-semaphore 0))
(define (actor-body)
  (let loop ((i 0) (acc 0))
    (if (= i 1000)
        (begin (mutex-lock! actor-mutex)
               (set! actor-total (+ actor-total acc))
               (mutex-unlock! actor-mutex)
               (sem-post! actor-done))
        (loop (+ i 1)
              (+ acc (with-exception-handler (lambda (e) 1)
                       (lambda () (guard (x ((string? x) 0)) (raise-continuable 'x)))))))))
(do ((a 0 (+ a 1))) ((= a 8)) (spawn actor-body))
(do ((a 0 (+ a 1))) ((= a 8)) (sem-wait! actor-done))
(check "eight actors, each a thousand guards with raise-continuable" actor-total 8000)

(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
