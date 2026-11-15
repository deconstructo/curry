;;; apply, reverse, list->string and list-head check their list argument (#389).
;;;
;;; (apply + 5) was 0, (reverse 5) was (), (reverse '(1 2 . 3)) dropped the
;;; tail, (list->string 5) was "" and (list-head '(1 2) 5) was (1 2): a wrong
;;; argument gave a value and no error.

(define pass 0)
(define fail 0)
(define (check label got expected)
  (if (equal? got expected)
      (set! pass (+ pass 1))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write got)
             (display " expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))
(define (raises? thunk) (guard (e (#t #t)) (thunk) #f))
(define (circular) (let ((c (list 1 2 3))) (set-cdr! (cddr c) c) c))

;;; apply ────────────────────────────────────────────────────────────────────
(check "apply: a number as the last argument raises" (raises? (lambda () (apply + 5))) #t)
(check "apply: a number after some arguments raises" (raises? (lambda () (apply + 1 5))) #t)
(check "apply: an improper list raises" (raises? (lambda () (apply + '(1 2 . 3)))) #t)
(check "apply: a circular list raises" (raises? (lambda () (apply + (circular)))) #t)
(check "apply: a symbol raises" (raises? (lambda () (apply + 'x))) #t)
(check "apply: a list" (apply + '(1 2 3)) 6)
(check "apply: arguments then a list" (apply + 1 2 '(3 4)) 10)
(check "apply: the empty list" (apply + '()) 0)
(check "apply: a list of lists" (apply append '((1) (2) (3))) '(1 2 3))
;; The primitive reached as a value, in tail position, and through other procedures.
(define my-apply apply)
(check "apply as a value: a number raises" (raises? (lambda () (my-apply + 5))) #t)
(check "apply as a value: a list" (my-apply + '(1 2)) 3)
(define (tail-apply x) (apply + x))
(check "apply in tail position: a number raises" (raises? (lambda () (tail-apply 5))) #t)
(check "apply in tail position: a list" (tail-apply '(4 5)) 9)
(check "apply through map: a number raises"
       (raises? (lambda () (map apply (list +) (list 5)))) #t)
(check "apply through apply" (apply apply (list + '(1 2))) 3)
(define (hot n acc) (if (= n 0) acc (hot (- n 1) (+ acc (apply + (list n 1))))))
(check "apply in a loop run 300 times (compiled)" (hot 300 0) 45450)
(check "and it still raises afterwards" (raises? (lambda () (tail-apply 5))) #t)

;;; reverse ──────────────────────────────────────────────────────────────────
(check "reverse: a number raises" (raises? (lambda () (reverse 5))) #t)
(check "reverse: an improper list raises" (raises? (lambda () (reverse '(1 2 . 3)))) #t)
(check "reverse: a circular list raises" (raises? (lambda () (reverse (circular)))) #t)
(check "reverse: a symbol raises" (raises? (lambda () (reverse 'x))) #t)
(check "reverse: a list" (reverse '(1 2 3)) '(3 2 1))
(check "reverse: the empty list" (reverse '()) '())
(check "reverse: a nested list is reversed one level" (reverse '((1 2) 3)) '(3 (1 2)))

;;; list->string ─────────────────────────────────────────────────────────────
(check "list->string: a number raises" (raises? (lambda () (list->string 5))) #t)
(check "list->string: an improper list raises" (raises? (lambda () (list->string '(#\a . #\b)))) #t)
(check "list->string: a non-character element raises" (raises? (lambda () (list->string '(1 2)))) #t)
(check "list->string: a string element raises" (raises? (lambda () (list->string '("a")))) #t)
(check "list->string: a circular list raises" (raises? (lambda () (list->string (circular)))) #t)
(check "list->string: characters" (list->string '(#\a #\b #\c)) "abc")
(check "list->string: the empty list" (list->string '()) "")
(check "list->string: non-ASCII" (list->string '(#\é #\x3bb)) "éλ")

;;; list-head ────────────────────────────────────────────────────────────────
(check "list-head: more than the length raises" (raises? (lambda () (list-head '(1 2) 5))) #t)
(check "list-head: a negative count raises" (raises? (lambda () (list-head '(1 2) -1))) #t)
(check "list-head: a non-integer count raises" (raises? (lambda () (list-head '(1 2) 1.5))) #t)
(check "list-head: a non-list raises" (raises? (lambda () (list-head 5 1))) #t)
(check "list-head: some elements" (list-head '(1 2 3) 2) '(1 2))
(check "list-head: all of them" (list-head '(1 2 3) 3) '(1 2 3))
(check "list-head: none" (list-head '(1 2 3) 0) '())
(check "list-head: none of the empty list" (list-head '() 0) '())

(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
