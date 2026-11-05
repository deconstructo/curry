;;; Tests for syntax-case: syntax, quasisyntax, with-syntax,
;;; generate-temporaries, datum->syntax, syntax->datum and the identifier
;;; procedures, plus the reader abbreviations #' #` #, #,@

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

;; Evaluates form and returns the message of the error it raises, or
;; 'no-error.
(define (error-of thunk)
  (guard (e (#t (if (error-object? e) (error-object-message e) 'non-error)))
    (thunk)
    'no-error))

(define (message-contains? msg text)
  (and (string? msg)
       (let loop ((i 0))
         (cond ((> (+ i (string-length text)) (string-length msg)) #f)
               ((string=? (substring msg i (+ i (string-length text))) text) #t)
               (else (loop (+ i 1)))))))

;;; ---- Reader abbreviations ----

(check "#' reads as (syntax x)" (syntax->datum '#'(a b)) '(syntax (a b)))
(check "#` reads as quasisyntax" (car '#`x) 'quasisyntax)
(check "#, reads as unsyntax" (car '#,x) 'unsyntax)
(check "#,@ reads as unsyntax-splicing" (car '#,@x) 'unsyntax-splicing)

;;; ---- Basic matching and templates ----

(define-syntax first-of
  (lambda (x)
    (syntax-case x ()
      ((_ a b) #'a))))
(check "pattern variable substitution" (first-of 1 2) 1)

(define-syntax lit-case
  (lambda (x)
    (syntax-case x (=>)
      ((_ a => b) #'(cons a b))
      ((_ a b c)  #'(list a b c)))))
(check "literal matches" (lit-case 1 => 2) '(1 . 2))
(check "literal mismatch falls through" (lit-case 1 2 3) '(1 2 3))

(define-syntax wild
  (lambda (x)
    (syntax-case x ()
      ((_ _ b) #'b))))
(check "_ matches anything" (wild 99 'kept) 'kept)

(define-syntax tail-pat
  (lambda (x)
    (syntax-case x ()
      ((_ a ... z) #'(quote (z a ...))))))
(check "ellipsis then a fixed tail" (tail-pat 1 2 3 4) '(4 1 2 3))

(define-syntax dotted
  (lambda (x)
    (syntax-case x ()
      ((_ a . rest) #'(quote (a rest))))))
(check "dotted pattern" (dotted 1 2 3) '(1 (2 3)))

(define-syntax vec-pat
  (lambda (x)
    (syntax-case x ()
      ((_ #(a b ...)) #'(quote (b ... a))))))
(check "vector pattern" (vec-pat #(1 2 3)) '(2 3 1))

(define-syntax const-pat
  (lambda (x)
    (syntax-case x ()
      ((_ 1) #'(quote one))
      ((_ "s") #'(quote str))
      ((_ other) #'(quote more)))))
(check "constant patterns" (list (const-pat 1) (const-pat "s") (const-pat z))
       '(one str more))

;;; ---- Ellipsis depth ----

(define-syntax lets
  (lambda (x)
    (syntax-case x ()
      ((_ ((n v) ...) b ...) #'((lambda (n ...) b ...) v ...)))))
(check "ellipsis over pairs" (lets ((a 1) (b 2)) (+ a b)) 3)

(define-syntax nest
  (lambda (x)
    (syntax-case x ()
      ((_ (a b ...) ...) #'(quote ((b ... a) ...))))))
(check "nested ellipsis" (nest (1 2 3) (4 5)) '((2 3 1) (5 4)))
(check "nested ellipsis, an empty group" (nest (1) (2 3)) '((1) (3 2)))
(check "zero repetitions" (nest) '())

;;; ---- Fenders ----

(define-syntax kind
  (lambda (x)
    (syntax-case x ()
      ((_ n) (number? (syntax->datum #'n)) #'(quote number))
      ((_ n) (identifier? #'n)             #'(quote identifier))
      ((_ n)                               #'(quote other)))))
(check "fender selects a clause"
       (list (kind 3) (kind foo) (kind "s"))
       '(number identifier other))

;;; ---- Hygiene (partial: introduced binders do not capture) ----

(define-syntax swap!
  (lambda (x)
    (syntax-case x ()
      ((_ a b) #'(let ((tmp a)) (set! a b) (set! b tmp))))))
(check "swap! does not capture the user's tmp"
       (let ((tmp 1) (y 2)) (swap! tmp y) (list tmp y))
       '(2 1))

(define-syntax my-or
  (lambda (x)
    (syntax-case x ()
      ((_) #'#f)
      ((_ e) #'e)
      ((_ e r ...) #'(let ((t e)) (if t t (my-or r ...)))))))
(check "my-or does not capture the user's t"
       (let ((t 5)) (my-or #f t))
       5)
(check "recursive macro, three arguments" (my-or #f #f 7) 7)

;; Two expansions of the same macro must not share an introduced binder.
(define-syntax two-lets
  (lambda (x)
    (syntax-case x ()
      ((_ a b) #'(list (let ((v a)) v) (let ((v b)) v))))))
(check "separate expansions" (two-lets 1 2) '(1 2))

;;; ---- datum->syntax: deliberately exposing a name ----

(define-syntax aif
  (lambda (x)
    (syntax-case x ()
      ((k c then else)
       (with-syntax ((it (datum->syntax #'k 'it)))
         #'(let ((it c)) (if it then else)))))))
(check "anaphoric it" (aif (+ 1 2) (* it 10) 0) 30)
(check "anaphoric it, else branch" (aif #f 1 'none) 'none)

;;; ---- with-syntax and generate-temporaries ----

(define-syntax squares
  (lambda (x)
    (syntax-case x ()
      ((_ n ...)
       (with-syntax (((t ...) (generate-temporaries #'(n ...))))
         #'(list (let ((t n)) (* t t)) ...))))))
(check "generate-temporaries" (squares 1 2 3) '(1 4 9))

(define-syntax with-two
  (lambda (x)
    (syntax-case x ()
      ((_ a b)
       (with-syntax ((x #'a) (y #'b))
         #'(list x y))))))
(check "with-syntax, two bindings" (with-two 1 2) '(1 2))

(check "with-syntax scope: body sees the new variables"
       (let-syntax ((m (lambda (x)
                         (syntax-case x ()
                           ((_ e) (with-syntax ((v #'e)) #'(+ v v)))))))
         (m 21))
       42)

;;; ---- Nested syntax-case ----

(define-syntax nested
  (lambda (x)
    (syntax-case x ()
      ((_ a b)
       (syntax-case #'b ()
         ((c d) #'(quote (a c d))))))))
(check "inner template sees outer and inner variables"
       (nested 1 (2 3))
       '(1 2 3))

(define-syntax shadow
  (lambda (x)
    (syntax-case x ()
      ((_ a)
       (syntax-case #'(inner) ()
         ((a) #'(quote a)))))))
(check "inner pattern variable shadows the outer one" (shadow outer) 'inner)

;;; ---- quasisyntax ----

(define-syntax qs
  (lambda (x)
    (syntax-case x ()
      ((_ a b) #`(list #,(+ 1 2) a #,@(list #'b #'b))))))
(check "unsyntax and unsyntax-splicing" (qs 10 20) '(3 10 20 20))

(define-syntax qs-tail
  (lambda (x)
    (syntax-case x ()
      ((_ a) #`(quote (a . #,(+ 1 2)))))))
(check "unsyntax in a dotted tail" (qs-tail z) '(z . 3))

(define-syntax qs-vec
  (lambda (x)
    (syntax-case x ()
      ((_ a) #`(quote #(a #,(* 2 3)))))))
(check "unsyntax inside a vector" (qs-vec z) '#(z 6))

(define-syntax qs-empty-splice
  (lambda (x)
    (syntax-case x ()
      ((_) #`(list 1 #,@'() 2)))))
(check "empty splice" (qs-empty-splice) '(1 2))

;;; ---- Identifiers and syntax objects ----

(check "identifier?" (list (identifier? #'a) (identifier? 3) (identifier? #'(a)))
       '(#t #f #f))
(check "bound-identifier=?"
       (list (bound-identifier=? #'a #'a) (bound-identifier=? #'a #'b))
       '(#t #f))
(check "free-identifier=?"
       (list (free-identifier=? #'a #'a) (free-identifier=? #'a #'b))
       '(#t #f))
(check "syntax->datum, deep" (syntax->datum #'(a #(b c) . d)) '(a #(b c) . d))
(check "syntax->datum leaves data alone" (syntax->datum '(1 "x" #\y)) '(1 "x" #\y))
(check "a syntax object prints readably" (let ((p (open-output-string)))
                                          (write #'foo p)
                                          (get-output-string p))
       "#<syntax foo>")
(check "type-of" (type-of #'foo) 'syntax-object)
(check "equal? on syntax objects" (equal? #'foo #'foo) #t)
(check "eq? on distinct syntax objects" (eq? #'foo #'foo) #f)

;;; ---- Errors ----

(define-syntax needs-two
  (lambda (x) (syntax-case x () ((_ a b) #'a))))

(check "no matching clause"
       (message-contains? (error-of (lambda () (eval '(needs-two 1) (interaction-environment))))
                          "no matching clause")
       #t)
(check "malformed syntax-case"
       (message-contains? (error-of (lambda () (eval '(syntax-case) (interaction-environment))))
                          "expected (syntax-case")
       #t)
(check "a literal must be an identifier"
       (message-contains?
        (error-of (lambda () (eval '(syntax-case 1 (2) (_ 3)) (interaction-environment))))
        "identifier")
       #t)
(check "malformed clause"
       (message-contains?
        (error-of (lambda () (eval '(syntax-case 1 () (a b c d)) (interaction-environment))))
        "clause")
       #t)
(check "malformed with-syntax"
       (message-contains?
        (error-of (lambda () (eval '(with-syntax) (interaction-environment))))
        "with-syntax")
       #t)
(check "syntax-violation"
       (message-contains?
        (error-of (lambda () (syntax-violation 'my-macro "bad use" '(my-macro))))
        "my-macro: bad use")
       #t)
(check "bound-identifier=? rejects a non-identifier"
       (message-contains?
        (error-of (lambda () (bound-identifier=? 1 #'a)))
        "identifiers")
       #t)

(define-syntax raises-at-expansion
  (lambda (x)
    (syntax-case x ()
      ((_ a) (syntax-violation 'raises-at-expansion "rejected" x)))))
(check "a syntax-violation inside a transformer is catchable"
       (message-contains?
        (error-of (lambda () (eval '(raises-at-expansion 1) (interaction-environment))))
        "rejected")
       #t)

;;; ---- The expansion boundary leaves ordinary data alone ----

;; A transformer that never calls syntax: its result is used as it is.
(define-syntax plain-proc
  (lambda (x) (list 'quote (cdr x))))
(check "a transformer that does not use syntax" (plain-proc 1 2) '(1 2))

;; Quoted symbols a template introduces stay literal.
(define-syntax quoted-tag
  (lambda (x)
    (syntax-case x ()
      ((_ a) #'(list 'tag a)))))
(check "a quoted introduced symbol is not renamed" (quoted-tag 1) '(tag 1))

;; A vector literal in a template is data: its symbols keep their names.
(define-syntax vec-literal
  (lambda (x)
    (syntax-case x ()
      ((_ a) #'(vector-ref #(a q) 1)))))
(check "a vector literal keeps its symbols" (vec-literal 1) 'q)

;; case data lists are data too.
(define-syntax case-tag
  (lambda (x)
    (syntax-case x ()
      ((_ e) #'(case e ((foo) 'is-foo) (else 'other))))))
(check "case data keeps its symbols" (list (case-tag 'foo) (case-tag 'bar))
       '(is-foo other))

;; A long introduced name is not truncated into a clash.
(define-syntax long-names
  (lambda (x)
    (syntax-case x ()
      ((_) #'(let ((aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1 1)
                    (aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa2 2))
                (list aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa2))))))
(check "long introduced names stay distinct" (long-names) '(1 2))

(check "datum->syntax needs an identifier context"
       (message-contains?
        (error-of (lambda () (datum->syntax 42 'foo)))
        "identifier")
       #t)

;; Quasiquoted data (atoms around an unquote) next to a renamed binder.
(define-syntax qq-atoms
  (lambda (x)
    (syntax-case x ()
      ((_) #'(let ((tmp 1)) `(a ,tmp 2 "s" #(b) . end))))))
(check "quasiquote data with atoms and a renamed binder" (qq-atoms)
       '(a 1 2 "s" #(b) . end))

(define-syntax qq-atom-only
  (lambda (x)
    (syntax-case x ()
      ((_) #'(let ((tmp 7)) (list `5 `sym `,tmp))))))
(check "quasiquote of a bare atom" (qq-atom-only) '(5 sym 7))

;;; ---- Existing syntax-rules macros are unaffected ----

(define-syntax rules-swap!
  (syntax-rules ()
    ((_ a b) (let ((tmp a)) (set! a b) (set! b tmp)))))
(check "syntax-rules still works"
       (let ((tmp 1) (y 2)) (rules-swap! tmp y) (list tmp y))
       '(2 1))

;;; ---- A macro that computes at expansion time (the shape that needs
;;; syntax-case: Schevana's define-foreign) ----

(define (c-double x) (* 2 x))
(define (c-negate b) (not b))

(define-syntax define-wrapped
  (lambda (x)
    (define (is? t name) (eq? (syntax->datum t) name))
    (syntax-case x ()
      ((_ name cname (type ...) ret)
       (with-syntax (((arg ...) (generate-temporaries #'(type ...))))
         (with-syntax (((conv ...)
                        (map (lambda (t a)
                               (cond ((is? t 'double) #`(exact->inexact #,a))
                                     ((is? t 'bool)   #`(if #,a 1 0))
                                     (else a)))
                             #'(type ...) #'(arg ...))))
           #`(define name
               (let ((f cname))
                 (lambda (arg ...)
                   #,(if (is? #'ret 'bool)
                         #'(not (= 0 (f conv ...)))
                         #'(f conv ...)))))))))))

(define-wrapped wrapped-add (lambda (a b) (+ a b)) (double int) double)
(define-wrapped wrapped-flag (lambda (b) b) (bool) bool)
(check "per-type conversion chosen at expansion time"
       (list (wrapped-add 1 2) (wrapped-flag #t) (wrapped-flag #f))
       '(3.0 #t #f))

;;; ---- Evaluated through the tree-walker too ----

(check "tree-walked use"
       (eval '(let ((tmp 1) (y 2)) (swap! tmp y) (list tmp y)) (interaction-environment))
       '(2 1))

;;; Summary
(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
