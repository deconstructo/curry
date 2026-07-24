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

;;; ---- SRFI 149: template extensions ----

;;; (... template) escapes ellipsis-as-operator so a macro can generate
;;; code that itself uses "...", e.g. a macro that expands into another
;;; define-syntax whose own patterns/templates need a real ellipsis.
(define-syntax gen-list-macro
  (syntax-rules ()
    ((_ name)
     (define-syntax name
       (syntax-rules ()
         ((_ a (... ...)) (list a (... ...))))))))

(gen-list-macro generated-list)
(check "ellipsis escape: generated macro works" (generated-list 1 2 3) '(1 2 3))

;;; Escaping without any pattern variables inside — pure literal passthrough.
(define-syntax literal-dots
  (syntax-rules ()
    ((_) '(... (a b c)))))

(check "ellipsis escape: literal passthrough" (literal-dots) '(a b c))

;;; Pattern variables still substitute normally inside an escaped template.
(define-syntax echo-then-dots
  (syntax-rules ()
    ((_ x) (list x '(... (p ...))))))

(check "ellipsis escape: pvar substitution still applies" (echo-then-dots 'hi)
       '(hi (p ...)))

;;; ---- SRFI 149: custom ellipsis identifier ----

;;; (syntax-rules ellipsis (literals ...) rule ...) — a non-"..." token
;;; used as the repetition operator instead.
(define-syntax my-list-custom-ell
  (syntax-rules ::: ()
    ((_ a :::) (list a :::))))

(check "custom ellipsis: basic repetition" (my-list-custom-ell 1 2 3) '(1 2 3))
(check "custom ellipsis: zero repetitions" (my-list-custom-ell) '())

;;; With a custom ellipsis identifier, "..." itself is no longer special
;;; and can appear as ordinary literal data without escaping.
(define-syntax dots-as-data
  (syntax-rules ::: ()
    ((_ x) (list x '...))))

(check "custom ellipsis: literal ... as data" (dots-as-data 5) '(5 ...))

;;; Nested/recursive macro using a custom ellipsis, mirroring my-or above.
(define-syntax my-or-custom-ell
  (syntax-rules @@ ()
    ((_) #f)
    ((_ e) e)
    ((_ e1 e2 @@)
     (let ((t e1))
       (if t t (my-or-custom-ell e2 @@))))))

(check "custom ellipsis: recursive macro" (my-or-custom-ell #f 7 #f) 7)

;;; ---- SRFI 149: vector patterns and templates ----

(define-syntax vec-swap
  (syntax-rules ()
    ((_ #(a b)) #(b a))))

(check "vector pattern/template: fixed arity" (vec-swap #(1 2)) #(2 1))

(define-syntax vec-collect
  (syntax-rules ()
    ((_ a ...) #(a ...))))

(check "vector template: ellipsis"       (vec-collect)        #())
(check "vector template: ellipsis one"   (vec-collect 1)      #(1))
(check "vector template: ellipsis many"  (vec-collect 1 2 3)  #(1 2 3))

(define-syntax vec-pat-ellipsis
  (syntax-rules ()
    ((_ #(a ...)) (list a ...))))

(check "vector pattern: ellipsis" (vec-pat-ellipsis #(1 2 3)) '(1 2 3))

;;; Mixed: a vector pattern containing a fixed head and an ellipsis tail.
(define-syntax vec-head-tail
  (syntax-rules ()
    ((_ #(first rest ...)) (list first (list rest ...)))))

(check "vector pattern: head + ellipsis tail"
       (vec-head-tail #(1 2 3 4)) '(1 (2 3 4)))

;;; ---- define-syntax compiled natively (not tree-eval punted) ----
;;; Regression coverage for two behaviors that were broken before native
;;; compiler support: a macro defined and used within the SAME compiled
;;; unit (previously only visible to a later, separately-compiled
;;; top-level form), and a macro defined inside a lambda body staying
;;; local instead of leaking into the global environment.

(check "define-syntax used in the same begin block"
  (begin
    (define-syntax same-unit-if
      (syntax-rules ()
        ((_ c t e) (cond (c t) (else e)))))
    (same-unit-if #t 'yes 'no))
  'yes)

(define (use-local-macro-internally)
  (define-syntax local-double
    (syntax-rules ()
      ((_ x) (* 2 x))))
  (local-double 21))
(check "internal define-syntax works locally" (use-local-macro-internally) 42)
(check "internal define-syntax does not leak to global"
  (guard (e (#t 'unbound)) (local-double 1))
  'unbound)

(define (shadow-outer-macro)
  (define-syntax shadowed (syntax-rules () ((_ x) (list 'outer x))))
  (define (inner)
    (define-syntax shadowed (syntax-rules () ((_ x) (list 'inner x))))
    (shadowed 1))
  (list (shadowed 2) (inner)))
(check "internal define-syntax shadows an outer local macro correctly"
  (shadow-outer-macro) '((outer 2) (inner 1)))

;;; ---- let-syntax / letrec-syntax (native codegen) ----

(check "let-syntax basic"
  (let-syntax ((sq (syntax-rules () ((_ x) (* x x)))))
    (sq 5))
  25)

(check "letrec-syntax basic"
  (letrec-syntax ((sq (syntax-rules () ((_ x) (* x x)))))
    (sq 6))
  36)

(check "let-syntax macro invisible outside its body"
  (guard (e (#t 'unbound))
    (begin (let-syntax ((only-here (syntax-rules () ((_ x) x)))) (only-here 1))
           (only-here 2)))
  'unbound)

(check "let-syntax visible to a nested lambda"
  ((let-syntax ((twice (syntax-rules () ((_ x) (* 2 x)))))
     (lambda (n) (twice n)))
   7)
  14)

;;; ---- top-level define-syntax's transformer-expr runs exactly once ----
;;; Regression test: the compiler evaluates a top-level macro's transformer
;;; expression once at compile time (for same-compiled-unit visibility) and
;;; must also re-register the macro at runtime (for .scc cache-hit
;;; persistence) without re-running transformer-expr itself when it produced
;;; an ordinary syntax-rules transformer — otherwise side-effecting code
;;; wrapped around a (syntax-rules ...) form would run twice.
(define double-eval-counter 0)
(define-syntax count-and-build
  (begin
    (set! double-eval-counter (+ double-eval-counter 1))
    (syntax-rules ()
      ((_ x) (* 3 x)))))
(check "define-syntax transformer-expr evaluated exactly once"
  double-eval-counter 1)
(check "macro rebuilt from extracted syntax-rules data still works"
  (count-and-build 5) 15)

;;; ---- %rebuild-syntax-rules direct misuse must not crash ----
;;; Regression test: %rebuild-syntax-rules (the internal primitive
;;; compile_define_syntax's codegen uses to reconstruct a macro without
;;; re-running its transformer-expr) is an ordinary, discoverable global —
;;; nothing stops user code from calling it directly as a define-syntax
;;; transformer-expr instead of only through generated bytecode. An earlier
;;; version returned an already-Syntax-wrapped value, so doing this built a
;;; Syntax-wrapping-a-Syntax macro binding that corrupted VM state when
;;; used. It must now behave like any other transformer-expr: produce a
;;; correctly-wrapped, working macro when the data is well-formed, and a
;;; normal Scheme error (never a crash) when it isn't.
(define-syntax direct-rebuild
  (%rebuild-syntax-rules '() '(((_ x) * 4 x)) '...))
(check "direct %rebuild-syntax-rules use produces a working macro"
  (direct-rebuild 5) 20)

(check "%rebuild-syntax-rules with malformed literals raises cleanly, no crash"
  (guard (e (#t 'caught)) (%rebuild-syntax-rules 5 '() '...))
  'caught)
(check "%rebuild-syntax-rules with malformed rules raises cleanly, no crash"
  (guard (e (#t 'caught)) (%rebuild-syntax-rules '() 5 '...))
  'caught)
(check "%rebuild-syntax-rules with non-symbol ellipsis raises cleanly, no crash"
  (guard (e (#t 'caught)) (%rebuild-syntax-rules '() '() 5))
  'caught)

;;; ---- calling a syntax-rules transformer directly as a procedure ----
;;; Regression test for a pre-existing (not introduced by the above fixes,
;;; but made more directly reachable by %rebuild-syntax-rules exposing a
;;; raw transformer as an ordinary first-class value) crash: a syntax-rules
;;; transformer Primitive assumes its argument is always the whole
;;; unevaluated use-site form (a pair). Binding it to a plain variable and
;;; calling it with a non-pair argument used to segfault (vcdr on a fixnum);
;;; must now raise a clean Scheme error.
(define bare-transformer
  (%rebuild-syntax-rules '() '(((_ x) x)) '...))
(check "calling a bare syntax-rules transformer with a non-pair arg raises cleanly"
  (guard (e (#t 'caught)) (bare-transformer 5))
  'caught)

;;; Summary
(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
