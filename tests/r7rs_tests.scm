;;; Basic R7RS conformance tests for Curry Scheme

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

;;; Booleans
(check "not #f" (not #f) #t)
(check "not #t" (not #t) #f)
(check "boolean=?" (boolean=? #t #t) #t)

;;; Numbers
(check "exact->inexact" (inexact 1/3) (/ 1.0 3.0))
(check "floor" (floor 3.7) 3.0)
(check "ceiling" (ceiling 3.2) 4.0)
(check "round half-even" (round 2.5) 2.0)
(check "round 3.5" (round 3.5) 4.0)
(check "gcd" (gcd 32 -36) 4)
(check "lcm" (lcm 32 -36) 288)
(check "quotient" (quotient 13 4) 3)
(check "remainder" (remainder -13 4) -1)
(check "modulo" (modulo -13 4) 3)
(check "expt" (expt 2 10) 1024)
(check "sqrt exact" (sqrt 4) 2)
(check "number->string" (number->string 255 16) "ff")
(check "string->number" (string->number "ff" 16) 255)

;;; Arithmetic
(check "+" (+ 1 2 3) 6)
(check "*" (* 2 3 4) 24)
(check "-" (- 10 3 2) 5)
(check "/" (/ 10 2) 5)
(check "mixed exact/inexact" (exact? (* 2 3)) #t)

;;; Characters
(check "char->integer" (char->integer #\A) 65)
(check "integer->char" (integer->char 65) #\A)
(check "char-upcase" (char-upcase #\a) #\A)
(check "char-alphabetic?" (char-alphabetic? #\a) #t)

;;; Strings
(check "string-length" (string-length "hello") 5)
(check "string-ref" (string-ref "hello" 1) #\e)
(check "substring" (substring "hello" 1 3) "el")
(check "string->list" (string->list "abc") '(#\a #\b #\c))
(check "list->string" (list->string '(#\h #\i)) "hi")
(check "string->symbol" (string->symbol "foo") 'foo)
(check "symbol->string" (symbol->string 'bar) "bar")

;;; Pairs
(check "car" (car '(1 2 3)) 1)
(check "cdr" (cdr '(1 2 3)) '(2 3))
(check "cadr" (cadr '(1 2 3)) 2)
(check "list-tail" (list-tail '(a b c d) 2) '(c d))
(check "list-ref" (list-ref '(a b c) 1) 'b)
(check "assoc" (assoc 2 '((1 a) (2 b) (3 c))) '(2 b))
(check "member" (member 2 '(1 2 3)) '(2 3))

;;; Vectors
(define v (make-vector 3 0))
(vector-set! v 1 42)
(check "vector-ref" (vector-ref v 1) 42)
(check "vector-length" (vector-length v) 3)
(check "vector->list" (vector->list '#(1 2 3)) '(1 2 3))

;;; Control
(check "values" (call-with-values (lambda () (values 1 2)) +) 3)

;;; Let forms
(check "let" (let ((x 1) (y 2)) (+ x y)) 3)
(check "let*" (let* ((x 1) (y (+ x 1))) y) 2)
(check "letrec" (letrec ((even? (lambda (n) (if (= n 0) #t (odd? (- n 1)))))
                          (odd?  (lambda (n) (if (= n 0) #f (even? (- n 1))))))
                   (even? 10)) #t)
(check "named let" (let loop ((n 5) (acc 1))
                     (if (= n 0) acc (loop (- n 1) (* acc n)))) 120)

;;; Tail calls
(define (count-down n)
  (if (= n 0) 'done (count-down (- n 1))))
(check "tail recursion" (count-down 1000000) 'done)

;;; Quasiquote
(let ((x 42))
  (check "quasiquote" `(a ,x c) '(a 42 c))
  (check "quasiquote splicing" `(a ,@(list 1 2) c) '(a 1 2 c)))

;;; do loop
(check "do" (let ((result '()))
              (do ((i 0 (+ i 1)))
                  ((= i 5) (reverse result))
                (set! result (cons i result))))
            '(0 1 2 3 4))

;;; Dynamic binding
(define p (make-parameter 10))
(check "parameter" (p) 10)
(parameterize ((p 99))
  (check "parameterize" (p) 99))
(check "parameterize restored" (p) 10)

;;; Exception handling
(check "guard" (guard (e (#t 'caught)) (error "oops")) 'caught)
(check "with-exception-handler"
       (with-exception-handler
         (lambda (e) 'handled)
         (lambda () (raise 'boom)))
       'handled)

;;; Sets
(define s (make-set))
(set-add! s 1)
(set-add! s 2)
(set-add! s 3)
(check "set-member?" (set-member? s 2) #t)
(check "set-size" (set-size s) 3)
(define s2 (list->set '(3 4 5)))
(check "set-intersection" (set-size (set-intersection s s2)) 1)

;;; Records
(define-record-type <person>
  (make-person name age)
  person?
  (name person-name)
  (age  person-age set-person-age!))

(define alice (make-person "Alice" 30))
(check "record?" (person? alice) #t)
(check "record-accessor" (person-name alice) "Alice")
(set-person-age! alice 31)
(check "record-mutator" (person-age alice) 31)

;;; Summary
(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
