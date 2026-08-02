;;; srfi_s210_multiple_values_tests.scm — (srfi s210 multiple-values)

(import (srfi s210 multiple-values))
(import (srfi s195 multiple-value-boxes))

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

(define (mv->list producer-thunk) (call-with-values producer-thunk list))

;;; ---- apply/mv ----

(check "apply/mv: spec example" (apply/mv string #\a (values #\b #\c)) "abc")
(check "apply/mv: zero operands" (apply/mv + (values 1 2 3)) 6)
(check "apply/mv: producer yields single value" (apply/mv + 1 2 (values 3)) 6)

;;; ---- call/mv ----

(check "call/mv: spec example" (call/mv string (values #\a #\b) (values #\c #\d)) "abcd")
(check "call/mv: single producer" (call/mv + (values 1 2 3)) 6)
(check "call/mv: zero producers" (call/mv list) '())
(check "call/mv: three producers" (call/mv list (values 1) (values 2 3) (values 4)) '(1 2 3 4))

;;; ---- list/mv / vector/mv / box/mv ----

(check "list/mv: spec example" (list/mv 'a (values 'b 'c)) '(a b c))
(check "list/mv: zero leading elements" (list/mv (values 'a 'b)) '(a b))
(check "list/mv: three leading elements" (list/mv 'a 'b 'c (values 'd 'e 'f)) '(a b c d e f))
(check "apply/mv: three leading operands" (apply/mv list 1 2 3 (values 4 5 6)) '(1 2 3 4 5 6))
(check "vector/mv: spec example" (vector/mv 'a (values 'b 'c)) #(a b c))
(check "box/mv: spec example" (mv->list (lambda () (unbox (box/mv 'a (values 'b 'c))))) '(a b c))

;;; ---- value/mv ----

(check "value/mv: spec example" (value/mv 1 'a (values 'b 'c)) 'b)
(check "value/mv: index into producer's own values" (value/mv 2 'a (values 'b 'c)) 'c)
(check "value/mv: out-of-range raises" (guard (e (#t 'raised)) (value/mv 5 'a (values 'b 'c))) 'raised)
(check "value/mv: non-integer index raises" (guard (e (#t 'raised)) (value/mv 'x 'a (values 'b))) 'raised)
(check "value/mv: negative index raises" (guard (e (#t 'raised)) (value/mv -1 'a (values 'b))) 'raised)
(check "value: negative index raises" (guard (e (#t 'raised)) (value -1 'a 'b)) 'raised)

;;; ---- coarity ----

(check "coarity: spec example" (coarity (values 'a 'b 'c)) 3)
(check "coarity: single value" (coarity 42) 1)
(check "coarity: zero values" (coarity (values)) 0)

;;; ---- set!-values ----

(check "set!-values: spec example (dotted formals)"
       (let ((x #f) (y #f))
         (set!-values (x . y) (values 'a 'b))
         (list x y))
       '(a (b)))
(check "set!-values: proper formals"
       (let ((x #f) (y #f))
         (set!-values (x y) (values 1 2))
         (list x y))
       '(1 2))
(check "set!-values: bare rest formal"
       (let ((all #f))
         (set!-values all (values 1 2 3))
         all)
       '(1 2 3))
(check "set!-values: mutates the OUTER binding, not a shadowed copy"
       (let ((x 'before))
         (define result #f)
         (let () (set!-values (x) (values 'after)))
         x)
       'after)

;;; ---- with-values ----

(check "with-values: spec example" (with-values (values 4 5) (lambda (a b) b)) 5)

;;; ---- case-receive ----

(check "case-receive: spec example"
       (case-receive (values 'a 'b)
         ((x) #f)
         ((x . y) (list x y)))
       '(a (b)))
(check "case-receive: first matching clause wins"
       (case-receive (values 1 2)
         ((x) 'one)
         ((x y) 'two)
         (rest 'many))
       'two)
(check "case-receive: rest-formal clause matches any arity"
       (case-receive (values 1 2 3 4)
         ((x) 'one)
         (rest 'many))
       'many)
(check "case-receive: matching clause buried in the middle of five"
       (case-receive (values 1 2 3)
         ((a) 'one)
         ((a b) 'two)
         ((a b c) (list 'three a b c))
         ((a b c d) 'four)
         (rest 'many))
       '(three 1 2 3))
(check "case-receive: no matching clause raises"
       (guard (e (#t 'raised)) (case-receive (values 1 2) ((x) x)))
       'raised)

;;; ---- bind/mv ----

(check "bind/mv: spec example"
       (mv->list (lambda () (bind/mv (values 1 2 3)
                                      (map-values (lambda (x) (* 2 x)))
                                      (map-values (lambda (x) (+ 1 x))))))
       '(3 5 7))
(check "bind/mv: zero transducers yields producer unchanged"
       (mv->list (lambda () (bind/mv (values 1 2 3))))
       '(1 2 3))
(check "bind/mv: three transducers chain correctly (each stage sees the PREVIOUS stage's output, not the last stage's)"
       (mv->list (lambda () (bind/mv (values 2 3)
                                      (map-values (lambda (x) (+ x 10)))
                                      (map-values (lambda (x) (* x 2)))
                                      (map-values (lambda (x) (- x 1))))))
       '(23 25))

;;; ---- list-values / vector-values / box-values ----

(check "list-values: spec example" (mv->list (lambda () (list-values '(a b c)))) '(a b c))
(check "list-values: non-list raises" (guard (e (#t 'raised)) (list-values 5)) 'raised)
(check "vector-values: spec example" (mv->list (lambda () (vector-values #(a b c)))) '(a b c))
(check "vector-values: non-vector raises" (guard (e (#t 'raised)) (vector-values 5)) 'raised)
(check "box-values: spec example" (mv->list (lambda () (box-values (box 'a 'b 'c)))) '(a b c))
(check "box-values: non-box raises" (guard (e (#t 'raised)) (box-values 5)) 'raised)

;;; ---- value / identity ----

(check "value: spec example" (value 1 'a 'b 'c) 'b)
(check "value: out-of-range raises" (guard (e (#t 'raised)) (value 5 'a 'b)) 'raised)
(check "identity: spec example" (mv->list (lambda () (identity 1 2 3))) '(1 2 3))

;;; ---- compose-left / compose-right ----

(check "compose-left: spec example"
       (let ((f (map-values (lambda (x) (* 2 x))))
             (g (map-values (lambda (x) (+ x 1)))))
         (mv->list (lambda () ((compose-left f g) 1 2 3))))
       '(3 5 7))
(check "compose-right: spec example"
       (let ((f (map-values (lambda (x) (* 2 x))))
             (g (map-values (lambda (x) (+ x 1)))))
         (mv->list (lambda () ((compose-right f g) 1 2 3))))
       '(4 6 8))
(check "compose-left: non-procedure raises" (guard (e (#t 'raised)) (compose-left 5)) 'raised)

;;; ---- map-values ----

(check "map-values: spec example" (mv->list (lambda () ((map-values odd?) 1 2 3))) '(#t #f #t))
(check "map-values: non-procedure raises" (guard (e (#t 'raised)) (map-values 5)) 'raised)

;;; ---- bind/list / bind/box / bind ----

(check "bind/list: spec example" (mv->list (lambda () (bind/list '(1 2 3) (map-values (lambda (x) (* 3 x)))))) '(3 6 9))
(check "bind/list: non-list raises" (guard (e (#t 'raised)) (bind/list 5 (map-values (lambda (x) x)))) 'raised)
(check "bind/box: spec example" (mv->list (lambda () (bind/box (box 1 2 3) (map-values (lambda (x) (* 3 x)))))) '(3 6 9))
(check "bind/box: multi-transducer chain"
       (mv->list (lambda () (bind/box (box 1 2 3)
                                       (map-values (lambda (x) (+ x 1)))
                                       (map-values (lambda (x) (* x 10))))))
       '(20 30 40))
(check "bind/box: non-box raises" (guard (e (#t 'raised)) (bind/box 5 (map-values (lambda (x) x)))) 'raised)
(check "bind: spec example" (mv->list (lambda () (bind 1 (lambda (x) (values (* 3 x) (+ 1 x)))))) '(3 2))
(check "bind: zero transducers yields the single value" (mv->list (lambda () (bind 42))) '(42))

;;; ---- Summary ----

(newline)
(display "srfi-s210 tests: ")
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0)
    (begin (display "SOME TESTS FAILED") (newline) (exit 1))
    (begin (display "all OK") (newline)))
