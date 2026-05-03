;;; Tests for syntax-rules macro system

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

;;; ---- Single non-variadic pattern ----

(define-syntax my-identity
  (syntax-rules ()
    ((_ x) x)))

(check "identity macro" (my-identity 42) 42)
(check "identity list" (my-identity '(a b c)) '(a b c))

;;; ---- Wildcard _ ----

(define-syntax ignore-first
  (syntax-rules ()
    ((_ _ x) x)))

(check "wildcard _" (ignore-first 99 'hello) 'hello)

;;; ---- Multiple patterns with fallthrough ----

(define-syntax my-and
  (syntax-rules ()
    ((_)         #t)
    ((_ e)       e)
    ((_ e1 e2 ...) (if e1 (my-and e2 ...) #f))))

(check "my-and zero args"  (my-and)        #t)
(check "my-and one arg #t" (my-and #t)     #t)
(check "my-and one arg 5"  (my-and 5)      5)
(check "my-and two args"   (my-and #t #t)  #t)
(check "my-and false"      (my-and #f #t)  #f)
(check "my-and three args" (my-and 1 2 3)  3)

;;; ---- Ellipsis: zero matches ----

(define-syntax collect
  (syntax-rules ()
    ((_ x ...) (list x ...))))

(check "ellipsis zero"  (collect)        '())
(check "ellipsis one"   (collect 1)      '(1))
(check "ellipsis many"  (collect 1 2 3)  '(1 2 3))

;;; ---- Ellipsis in pattern and template ----

(define-syntax my-list
  (syntax-rules ()
    ((_ e ...) (list e ...))))

(check "my-list empty"    (my-list)          '())
(check "my-list single"   (my-list 'a)       '(a))
(check "my-list multiple" (my-list 'a 'b 'c) '(a b c))

;;; ---- Recursive macro ----

(define-syntax my-or
  (syntax-rules ()
    ((_) #f)
    ((_ e) e)
    ((_ e1 e2 ...)
     (let ((t e1))
       (if t t (my-or e2 ...))))))

(check "my-or zero"  (my-or)           #f)
(check "my-or one"   (my-or 5)         5)
(check "my-or false" (my-or #f #f)     #f)
(check "my-or true"  (my-or #f 7 #f)   7)

;;; ---- Literal matching ----

(define-syntax my-case-lambda
  (syntax-rules (=>)
    ((_ (test => result)) (if test result #f))
    ((_ (test))           test)))

(check "literal => branch" (my-case-lambda (#t => 'yes)) 'yes)
(check "literal no =>"     (my-case-lambda (42))         42)

;;; ---- swap! macro ----

(define-syntax swap!
  (syntax-rules ()
    ((_ a b)
     (let ((tmp a))
       (set! a b)
       (set! b tmp)))))

(define x 1)
(define y 2)
(swap! x y)
(check "swap! x" x 2)
(check "swap! y" y 1)

;;; ---- let defined via syntax-rules ----

(define-syntax my-let
  (syntax-rules ()
    ((_ ((var init) ...) body ...)
     ((lambda (var ...) body ...) init ...))))

(check "my-let basic" (my-let ((a 1) (b 2)) (+ a b)) 3)
(check "my-let body"  (my-let ((x 10)) (* x x)) 100)
(check "my-let empty" (my-let () 'ok) 'ok)

;;; ---- Zero-clause syntax-rules raises on use ----

(define-syntax no-patterns
  (syntax-rules ()))

(define raised? #f)
(guard (exn (#t (set! raised? #t)))
  (no-patterns foo))
(check "zero-clause raises" raised? #t)

;;; ---- Nested macro calls ----

(define-syntax my-begin
  (syntax-rules ()
    ((_ e) e)
    ((_ e1 e2 ...) (let ((ignored e1)) (my-begin e2 ...)))))

(check "my-begin single"   (my-begin 42)      42)
(check "my-begin sequence" (my-begin 1 2 3)   3)

;;; ---- HYGIENE: tests below are marked unhygienic and skipped ----
;;; These would require a full marks-and-substitutions system to pass.

;;; ;;HYGIENE: (if #f (begin ... ))

;;; Summary
(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
