;;; syntax_rules_nested_ellipsis_tests.scm — regression coverage for issue
;;; #101: a pattern where an outer `...` wraps a sub-pattern that itself
;;; contains a variable followed by its own `...` (e.g. "(a b ...) ...")
;;; failed to bind the inner variable correctly.
;;;
;;; Found while implementing (scheme case-lambda), whose standard R7RS
;;; reference implementation naturally uses exactly this shape. Root cause
;;; (src/syntax_rules.c): sr_match_list's per-group accumulation loop only
;;; ever searched `sb` (the inner match's scalar bindings) for each
;;; pattern-variable name, silently defaulting to '() when a name's match
;;; actually landed in `se` (the inner match's own ellipsis bindings,
;;; which is where a variable with its own further ellipsis inside the
;;; outer sub-pattern lands) instead. sr_expand_list had the symmetric gap
;;; on the way back out: it never re-scoped ell_bindings per outer
;;; iteration, so a nested "name ..." inside the repeated sub-template
;;; would iterate over the WRONG (whole, cross-iteration) list. Fixed by
;;; falling back to `se` when a name isn't in `sb` (match side) and by
;;; building a per-iteration, shadowed ell_bindings table (expand side) --
;;; both fixes compose correctly to arbitrary nesting depth by induction,
;;; since each level applies the exact same fallback.

(define pass 0)
(define fail 0)

(define (check label got expected)
  (if (equal? got expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " — got ") (write got)
             (display "  expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))

;; The exact minimal repro from issue #101.
(define-syntax nest-basic
  (syntax-rules ()
    ((_ (a b ...) ...) '((a b ...) ...))))
(check "nested ellipsis: issue #101's exact minimal repro"
       (nest-basic (1 10 20) (2 30))
       '((1 10 20) (2 30)))

;; Asymmetric group sizes, including a zero-inner-elements group.
(check "nested ellipsis: asymmetric group sizes"
       (nest-basic (1) (2 20 21) (3 30))
       '((1) (2 20 21) (3 30)))

;; A single outer group (degenerate but valid case).
(check "nested ellipsis: single outer group"
       (nest-basic (1 10 20 30))
       '((1 10 20 30)))

;; Zero outer groups (degenerate but valid case).
(check "nested ellipsis: zero outer groups" (nest-basic) '())

;; The inner and outer ellipsis variables used SEPARATELY in the template,
;; not just spliced back into the same shape -- proves both a's (depth 1)
;; and b's (depth 2) bindings are independently correct, not just
;; "round-tripping" the same input structure back out unchanged.
(define-syntax nest-separate
  (syntax-rules ()
    ((_ (a b ...) ...)
     (list (list 'outer-vals a ...) (list 'inner-groups (list b ...) ...)))))
(check "nested ellipsis: outer and inner vars used independently"
       (nest-separate (1 10 20 30) (2 40) (3 50 60))
       '((outer-vals 1 2 3) (inner-groups (10 20 30) (40) (50 60))))

;; Three levels of nesting: outer wraps (a (b c ...) ...).
(define-syntax nest-triple
  (syntax-rules ()
    ((_ (a (b c ...) ...) ...) '((a (b c ...) ...) ...))))
(check "nested ellipsis: three levels deep"
       (nest-triple (1 (10 100 101) (20 200)) (2 (30 300)))
       '((1 (10 100 101) (20 200)) (2 (30 300))))

;; A dotted tail inside the doubly-nested shape.
(define-syntax nest-dotted
  (syntax-rules ()
    ((_ (a b ... . tail) ...) '((a (b ...) tail) ...))))
(check "nested ellipsis: dotted tail inside a nested-ellipsis clause"
       (nest-dotted (1 10 20 . extra1) (2 . extra2))
       '((1 (10 20) extra1) (2 () extra2)))

;; Real-world shape: the (scheme case-lambda) reference implementation's
;; own outer macro, using the DIRECT (a b ...) ... shape rather than the
;; opaque-clause workaround the shipped .sld file uses -- proves the
;; engine fix itself is sufficient to make the textbook R7RS reference
;; implementation work as originally written, not just that the workaround
;; still works.
(define-syntax direct-case-lambda-help
  (syntax-rules ()
    ((_ args len) (error "no matching clause"))
    ((_ args len ((p ...) body0 ...) . rest)
     (if (= len (length '(p ...)))
         (apply (lambda (p ...) body0 ...) args)
         (direct-case-lambda-help args len . rest)))
    ((_ args len (tail body0 ...) . rest)
     (apply (lambda tail body0 ...) args))))
(define-syntax direct-case-lambda
  (syntax-rules ()
    ((_ (params body0 ...) ...)
     (lambda args
       (let ((len (length args)))
         (direct-case-lambda-help args len (params body0 ...) ...))))))
(define dcl-f
  (direct-case-lambda
    (() 'zero)
    ((a) (list 'one a))
    ((a b) (list 'two a b))))
(check "nested ellipsis: direct (non-workaround) case-lambda reference shape, 0 args"
       (dcl-f) 'zero)
(check "nested ellipsis: direct (non-workaround) case-lambda reference shape, 1 arg"
       (dcl-f 1) '(one 1))
(check "nested ellipsis: direct (non-workaround) case-lambda reference shape, 2 args"
       (dcl-f 1 2) '(two 1 2))

;;; Regression: malformed macro uses that mix ellipsis depths within the
;;; same repeated sub-template (invalid per R7RS, but nothing in this file
;;; rejects it at match time) used to SIGSEGV -- uncatchable by guard,
;;; since macro expansion happens at compile time, not run time. Found by
;;; independent review: the expand-side fix's per-iteration
;;; iter_ell_bindings shadowing broke the invariant that every
;;; ell_bindings value is a proper list, so an unguarded scm_list_ref
;;; dereferenced a scalar as a pair. Fixed by treating a not-actually-a-
;;; list value as "no more repetitions" instead of crashing. These tests
;;; exist to prove the process survives at all -- the exact output for
;;; genuinely invalid macro input is "wrong but survivable" by design, not
;;; a meaningful contract, so only checking "didn't crash, execution
;;; continued" here, not specific result shapes.
(define-syntax mixed-depth-1
  (syntax-rules () ((_ (a b ...) ...) '(((a b) ...) ...))))
(mixed-depth-1 (1 10))
(check "nested ellipsis: mixed-depth macro use does not crash the process"
       'still-alive 'still-alive)

(define-syntax mixed-depth-2
  (syntax-rules () ((_ (a b ...) ...) '(((a . b) ...) ...))))
(mixed-depth-2 (1 10 20))
(check "nested ellipsis: mixed-depth dotted-tail use does not crash the process"
       'still-alive 'still-alive)

;; Pre-existing (not introduced by this fix, same call site, same guard):
;; two same-depth ellipsis variables of UNEQUAL length used together also
;; used to crash the same way.
(define-syntax unequal-length
  (syntax-rules () ((_ (a ...) (b ...)) '((b a) ...))))
(unequal-length (1 2 3 4 5) (10 20))
(check "nested ellipsis: unequal-length same-depth vars do not crash the process"
       'still-alive 'still-alive)

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
