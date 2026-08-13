;;; Tests for (features) and cond-expand (R7RS 4.2.9 / 6.13.3) --
;;; expression position (eval.c tree-walker + compiler.c/VM), and
;;; define-library declaration position (modules.c), including the
;;; and/or/not/library requirement grammar and the no-match error path.

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

;;; ════════════════════════════════════════════════════════════
;;; § 1  (features)
;;; ════════════════════════════════════════════════════════════

(check "features includes 'curry" (if (memq 'curry (features)) #t #f) #t)
(check "features includes 'r7rs" (if (memq 'r7rs (features)) #t #f) #t)
(check "features is a proper list" (list? (features)) #t)

;;; ════════════════════════════════════════════════════════════
;;; § 2  cond-expand — expression position, feature identifiers
;;; ════════════════════════════════════════════════════════════

(check "cond-expand picks a matching feature-identifier clause"
  (cond-expand (curry 'matched-curry) (else 'matched-else))
  'matched-curry)

(check "cond-expand falls through to else"
  (cond-expand (definitely-not-a-real-feature 'wrong) (else 'matched-else))
  'matched-else)

(check "cond-expand tries clauses in order, first match wins"
  (cond-expand (curry 'first) (curry 'second))
  'first)

;;; ════════════════════════════════════════════════════════════
;;; § 3  cond-expand — and / or / not / library requirement grammar
;;; ════════════════════════════════════════════════════════════

(check "cond-expand (and ...) — all satisfied"
  (cond-expand ((and curry r7rs) 'yes) (else 'no))
  'yes)

(check "cond-expand (and ...) — one unsatisfied fails the clause"
  (cond-expand ((and curry definitely-not-a-real-feature) 'wrong) (else 'right))
  'right)

(check "cond-expand (or ...) — one satisfied is enough"
  (cond-expand ((or definitely-not-a-real-feature curry) 'yes) (else 'no))
  'yes)

(check "cond-expand (or ...) — none satisfied fails the clause"
  (cond-expand ((or definitely-not-a-real-feature also-not-real) 'wrong) (else 'right))
  'right)

(check "cond-expand (not ...) — negates a satisfied requirement"
  (cond-expand ((not curry) 'wrong) (else 'right))
  'right)

(check "cond-expand (not ...) — negates an unsatisfied requirement"
  (cond-expand ((not definitely-not-a-real-feature) 'right) (else 'wrong))
  'right)

(check "cond-expand (library ...) — an importable library"
  (cond-expand ((library (scheme base)) 'available) (else 'missing))
  'available)

(check "cond-expand (library ...) — a library that doesn't exist"
  (cond-expand ((library (definitely not a real library)) 'wrong) (else 'right))
  'right)

;;; ════════════════════════════════════════════════════════════
;;; § 4  cond-expand — no matching clause and no else is an error
;;; ════════════════════════════════════════════════════════════
;;;
;;; cond-expand is resolved at compile time (R7RS "expansion time"), so
;;; a no-match error fires while the enclosing form is being compiled —
;;; wrapping it in a plain (lambda () ...) thunk wouldn't work here, since
;;; that lambda is compiled (and would already raise) before check-error's
;;; own guard's dynamic extent begins. Going through eval forces
;;; compilation to happen inside the thunk call, where guard can see it.

(check-error "cond-expand raises when nothing matches and there's no else"
  (lambda ()
    (eval '(cond-expand (definitely-not-a-real-feature 'wrong))
          (interaction-environment))))

;;; A clause that isn't a (requirement expr...) list must raise cleanly,
;;; not crash -- cond_expand_choose (features.c) checks vis_pair on each
;;; clause before touching it.
(check-error "cond-expand raises (not crashes) on a non-pair clause"
  (lambda () (eval '(cond-expand 5) (interaction-environment))))

(check-error "cond-expand raises (not crashes) on an empty-list clause"
  (lambda () (eval '(cond-expand ()) (interaction-environment))))

;;; A malformed top-level define-library declaration (not a (kind ...)
;;; list) must raise cleanly too -- define_library_clause (modules.c)
;;; checks vis_pair before touching it, same discipline as
;;; cond_expand_choose above.
(check-error "define-library raises (not crashes) on a non-list declaration"
  (lambda () (eval '(define-library (test malformed-declaration) 42)
                    (interaction-environment))))

;;; ════════════════════════════════════════════════════════════
;;; § 5  cond-expand — define-library declaration position
;;; ════════════════════════════════════════════════════════════
;;;
;;; This is the actual motivating use case: a library picking its
;;; (import ...)/(begin ...) declarations per implementation, the same
;;; shape SRFI 279's own 279.sld uses to dispatch chibi/kawa/guile/else.

(define-library (test cond-expand-declaration)
  (export tag)
  (import (scheme base))
  (cond-expand
    (curry (begin (define tag 'curry-branch)))
    (else  (begin (define tag 'else-branch)))))

(import (test cond-expand-declaration))
(check "cond-expand as a define-library declaration picks the curry branch"
  tag 'curry-branch)

;;; Nested cond-expand inside a matched declaration-position clause.
(define-library (test cond-expand-nested)
  (export nested-tag)
  (import (scheme base))
  (cond-expand
    (curry
     (cond-expand
       ((and curry r7rs) (begin (define nested-tag 'nested-match)))
       (else (begin (define nested-tag 'nested-wrong)))))
    (else (begin (define nested-tag 'outer-wrong)))))

(import (test cond-expand-nested))
(check "cond-expand nests correctly at declaration position"
  nested-tag 'nested-match)

(display (string-append (number->string pass) " passed, " (number->string fail) " failed")) (newline)
(if (> fail 0) (exit 1) (exit 0))
