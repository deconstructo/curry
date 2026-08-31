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
    ;; pattern (i.e. NOT `((_ (params body0 ...) ...) ...)`) -- NOT because
    ;; that shape is broken anymore (issue #101, the syntax-rules engine
    ;; bug this comment used to describe, is fixed -- src/syntax_rules.c
    ;; now correctly binds a pattern variable that has its own further
    ;; ellipsis nested inside an outer ellipsis-repeated sub-pattern), but
    ;; because this file was written and shipped before that fix landed,
    ;; and the opaque-clause-plus-recursion shape below works fine and
    ;; costs nothing to keep -- no need to churn already-correct, already-
    ;; tested code just to use the more direct shape now that it works too.
    ;; %case-lambda-help does the actual (params body0 ...) decomposition
    ;; one clause at a time via ordinary recursion (`. rest`).
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
