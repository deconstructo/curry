;; (scheme case-lambda), R7RS: a case-lambda dispatches on argument count to
;; one of several (params body ...) clauses, exactly like several ordinary
;; `lambda`s glued together with arity-based selection.
;;
;; This was previously listed in modules.c's "alias GLOBAL_ENV" table
;; alongside the real (scheme base)/(scheme write)/etc. libraries, which
;; meant (import (scheme case-lambda)) silently succeeded while providing
;; nothing at all -- GLOBAL_ENV never actually had a case-lambda binding.
;; Real implementations elsewhere in this codebase (SRFI-41's
;; stream->list, in particular) had to work around the absence with manual
;; rest-arg + length dispatch instead of using the standard form. This is
;; the portable syntax-rules implementation R7RS itself documents (widely
;; attributed to Dybvig et al.) -- no VM/compiler changes needed, since
;; it's pure sugar over ordinary lambda + apply.
;;
;; Known limitation: curry's `apply` is not tail-called (see the open
;; issue on the VM/apply itself), so a self-recursive case-lambda using
;; the canonical accumulator idiom -- e.g.
;;   (define count (case-lambda ((n) (count n 0))
;;                               ((n acc) (if (= n 0) acc (count (- n 1) (+ acc 1))))))
;; -- will stack-overflow around a few hundred iterations instead of
;; looping forever, exactly like calling any other procedure through
;; `apply` in a loop would. Not fixable from within this file (portable
;; syntax-rules has no way to avoid the underlying `apply` call each
;; clause dispatch needs); flagged here since it's the single most
;; natural case-lambda usage pattern to hit it.
(define-library (scheme case-lambda)
  (import (scheme base))
  (export case-lambda %case-lambda-help)
  (begin
    ;; %case-lambda-help must be exported too (not just case-lambda),
    ;; even though it's an internal helper never meant to be called
    ;; directly -- curry's syntax-rules is not hygienic across
    ;; define-library boundaries (see docs/reference/writing-a-module.md),
    ;; so an importer using case-lambda would otherwise hit an
    ;; unbound-variable error the first time they actually USE it, not at
    ;; import time, since the macro's own expansion references this
    ;; helper by name.
    (define-syntax %case-lambda-help
      (syntax-rules ()
        ((_ args len)
         (error "case-lambda: no clause matches the given number of arguments" len))
        ((_ args len ((p ...) body0 ...) . rest)
         (if (= len (length '(p ...)))
             (apply (lambda (p ...) body0 ...) args)
             (%case-lambda-help args len . rest)))
        ((_ args len ((p ... . tail) body0 ...) . rest)
         (if (>= len (length '(p ...)))
             (apply (lambda (p ... . tail) body0 ...) args)
             (%case-lambda-help args len . rest)))
        ((_ args len (tail body0 ...) . rest)
         (apply (lambda tail body0 ...) args))))

    ;; NOTE: deliberately does NOT decompose each clause in this macro's own
    ;; pattern (i.e. NOT `((_ (params body0 ...) ...) ...)`)  --  curry's
    ;; syntax-rules has a confirmed bug where a pattern variable that is
    ;; itself followed by an ellipsis, nested inside ANOTHER pattern that is
    ;; also wrapped in an outer ellipsis (an outer `...` wrapping a
    ;; sub-pattern that contains its own `...`), fails to bind correctly:
    ;; minimal repro is
    ;;   (define-syntax my-test
    ;;     (syntax-rules () ((_ (a b ...) ...) '((a b ...) ...))))
    ;;   (my-test (1 10 20) (2 30))   ; => ((1 () ()) (2 () ())), should be
    ;;                                ;    ((1 10 20) (2 30))
    ;; Filed as its own issue -- not fixed here, since it's a core
    ;; syntax-rules engine bug (src/syntax_rules.c), well beyond the scope
    ;; of adding case-lambda. Worked around instead: this macro captures
    ;; each clause as one OPAQUE `clause` pattern variable (ellipsis depth
    ;; 1, no internal structure in this macro's own pattern), and
    ;; %case-lambda-help above does the actual (params body0 ...)
    ;; decomposition one clause at a time via ordinary recursion (`. rest`)
    ;; rather than a second, nested `...` -- exactly the shape confirmed
    ;; NOT to trigger the bug.
    ;;
    ;; %cl-args/%cl-len (not the more natural `args`/`len`) are deliberately
    ;; unusual names: curry's syntax-rules does NOT rename template-
    ;; introduced binders when they collide with an identifier the user's
    ;; own code happens to use (a real hygiene gap independent of the
    ;; nested-ellipsis bug above, found by independent review) -- a clause
    ;; body that itself referenced a variable literally named `args` or
    ;; `len` would otherwise silently see this macro's own internal
    ;; binding instead of the user's, with no error. `%cl-` prefixed names
    ;; make an accidental real-world collision effectively impossible.
    (define-syntax case-lambda
      (syntax-rules ()
        ((_ clause ...)
         (lambda %cl-args
           (let ((%cl-len (length %cl-args)))
             (%case-lambda-help %cl-args %cl-len clause ...))))))))
