;;; (curry matchable) tests

(import (curry matchable))

(define pass 0)
(define fail 0)

(define (check label result expected)
  (if (equal? result expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (display " expected ") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

(define (check-error label thunk)
  (if (guard (e (#t #t)) (thunk) #f)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label) (display " did not raise") (newline)
             (set! fail (+ fail 1)))))

;;; Literals, quote, wildcard, variables

(check "literal number match" (match 5 (5 'yes) (_ 'no)) 'yes)
(check "literal number mismatch" (match 5 (6 'yes) (_ 'no)) 'no)
(check "quote literal" (match 'a ('b 1) ('a 2)) 2)
(check "wildcard" (match 5 (_ 'ok)) 'ok)
(check "variable binds" (match 5 (x x)) 5)

;;; Non-linear (repeated) pattern variables

(check "non-linear repeat matches" (match (list 1 2 1) ((a a b) 'first) ((a b a) 'second)) 'second)
(check "non-linear repeat fails to next clause" (match (list 1 1 2) ((a a b) 'first) ((a b a) 'second)) 'first)

;;; Lists and dotted pairs

(check "list pattern" (match (list 1 2 3) ((a b c) (list c b a))) (list 3 2 1))
(check "dotted pattern" (match (cons 1 2) ((a . b) (list a b))) (list 1 2))
(check "3-var flat list, no collision with internal names" (match (list 10 20 30) ((v w x) (list v w x))) (list 10 20 30))
(check "v as a user variable name doesn't collide with match's own internal v"
  (match 5 (v v)) 5)

;;; and / or / not / ? / =

(check "and as-binding" (match 1 ((and x 1) x)) 1)
(check "and fails" (match 1 ((and x 2) x) (_ 'else)) 'else)
(check "or first branch" (match 1 ((or x 2) x)) 1)
(check "or second branch" (match 2 ((or 1 x) x)) 2)
(check "nested or" (match 5 ((or (or 1 5) 9) 'inner-matched) (_ 'no)) 'inner-matched)
(check "not pattern" (match 1 ((not 2) 'ok) (_ 'no)) 'ok)
(check "predicate pattern" (match 1 ((? odd? x) x)) 1)
(check "predicate pattern fails" (match 2 ((? odd? x) x) (_ 'even)) 'even)
(check "field extraction" (match '(1 . 2) ((= car x) x)) 1)

;;; Ellipsis

(check "ellipsis basic" (match (list 1 2 3) ((a b c ...) c)) (list 3))
(check "ellipsis empty" (match (list 1 2) ((a b c ...) c)) '())
(check "ellipsis nested" (match (list 1 (list 2 3 4)) ((a (b ...)) b)) (list 2 3 4))
(check "ellipsis ___ alias" (match (list 1 2 3) ((a b c ___) c)) (list 3))
(check-error "trailing patterns after ... raise a clear error, not a silent mismatch"
  (lambda () (match (list 1 2 3 4) ((a b ... c d) (list a b c d)))))

;;; Vectors

(check "vector basic" (match (vector 1 2 3) (#(a b c) (list a b c))) (list 1 2 3))
(check "vector wrong length falls through" (match (vector 1 2) (#(a b c) 'yes) (_ 'no)) 'no)

;;; Records

(check "record pattern" (match (cons 10 20) ((record pair? (car a) (cdr b)) (list a b))) (list 10 20))

;;; Sugar: match-lambda, match-lambda*, match-let, match-let*

(check "match-lambda" ((match-lambda ((a b) (+ a b)) (_ 0)) (list 3 4)) 7)
(check "match-lambda*" ((match-lambda* ((a b) (+ a b))) 3 4) 7)
(check "match-let" (match-let (((a b) (list 1 2))) (+ a b)) 3)
(check "match-let*" (match-let* (((a b) (list 1 2)) (c (+ a b))) c) 3)

;;; Deep-nesting regression — this exact case returned (1 2 3 3) instead of
;;; (1 2 3 4) before the fix removing named intermediate bindings from
;;; match-two's (p . q)/(p) clauses (curry's unhygienic macro expansion let
;;; an inner recursive match's own binding shadow an outer one whose
;;; continuation was still pending).

(check "deep nesting (3+ levels) regression"
  (match (list 1 (list 2 3) 4) ((a (b c) d) (list a b c d)))
  (list 1 2 3 4))
(check "deep nesting with ellipsis mixed in"
  (match (list 1 (list 2 3 4) 5 6) ((a (b ...) c d) (list a b c d)))
  (list 1 (list 2 3 4) 5 6))
(check "4-level nesting"
  (match (list 1 (list 2 (list 3 4) 5) 6)
    ((a (b (c d) e) f) (list a b c d e f)))
  (list 1 2 3 4 5 6))

(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(when (> fail 0)
  (error (string-append "matchable_tests: " (number->string fail) " test(s) failed")))
