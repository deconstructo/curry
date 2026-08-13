;;; SRFI-26: Notation for Specializing Parameters without Currying.
;;; https://srfi.schemers.org/srfi-26/
;;;
;;; (cut proc slot-or-expr ...) builds a lambda that partially applies
;;; proc: <> marks a slot (a formal parameter of the resulting lambda,
;;; in order), <...> as the final slot-or-expr means the resulting
;;; lambda also accepts and forwards any further trailing arguments.
;;; Every non-slot slot-or-expr is an ordinary expression captured into
;;; the closure. cut re-evaluates non-slot expressions on every call of
;;; the resulting procedure; cute evaluates them once, when the cut/cute
;;; form itself is evaluated, and reuses that value on every call.
;;;
;;;   (cut list 1 <> 3)      => (lambda (x) (list 1 x 3))
;;;   (cut list 1 <> <...>)  => (lambda (x . rest) (apply list 1 x rest))
;;;   (cut list)             => (lambda () (list))
;;;
;;; This is the reference implementation verbatim (Sebastian Egner,
;;; adapted from Al Petrofsky's posting, public domain) -- SRFI-26 §
;;; "Reference Implementation" / cut.scm. No curry-specific changes:
;;; it's plain portable syntax-rules, nothing here depends on any
;;; primitive curry is missing.

(define-library (srfi s26 cut)
  (export
    cut cute
    ;; srfi-26-internal-cut/-cute are reached by cut/cute's own
    ;; expansion (the recursive slot-scanning step), not just internal
    ;; plumbing -- curry's syntax-rules is not hygienic across
    ;; define-library boundaries (see docs/reference/writing-a-module.md),
    ;; so these must be exported too or importers get an unbound-variable
    ;; error the first time they actually use cut/cute, not at import time.
    srfi-26-internal-cut srfi-26-internal-cute)
  (import (scheme base))
  (begin

    (define-syntax srfi-26-internal-cut
      (syntax-rules (<> <...>)
        ((srfi-26-internal-cut (slot-name ...) (proc arg ...))
         (lambda (slot-name ...) ((begin proc) arg ...)))
        ((srfi-26-internal-cut (slot-name ...) (proc arg ...) <...>)
         (lambda (slot-name ... . rest-slot) (apply proc arg ... rest-slot)))
        ((srfi-26-internal-cut (slot-name ...)   (position ...)      <>  . se)
         (srfi-26-internal-cut (slot-name ... x) (position ... x)        . se))
        ((srfi-26-internal-cut (slot-name ...)   (position ...)      nse . se)
         (srfi-26-internal-cut (slot-name ...)   (position ... nse)      . se))))

    (define-syntax srfi-26-internal-cute
      (syntax-rules (<> <...>)
        ((srfi-26-internal-cute
          (slot-name ...) nse-bindings (proc arg ...))
         (let nse-bindings (lambda (slot-name ...) (proc arg ...))))
        ((srfi-26-internal-cute
          (slot-name ...) nse-bindings (proc arg ...) <...>)
         (let nse-bindings (lambda (slot-name ... . x) (apply proc arg ... x))))
        ((srfi-26-internal-cute
          (slot-name ...)         nse-bindings  (position ...)   <>  . se)
         (srfi-26-internal-cute
          (slot-name ... x)       nse-bindings  (position ... x)     . se))
        ((srfi-26-internal-cute
          slot-names              nse-bindings  (position ...)   nse . se)
         (srfi-26-internal-cute
          slot-names ((x nse) . nse-bindings) (position ... x)       . se))))

    (define-syntax cut
      (syntax-rules ()
        ((cut . slots-or-exprs)
         (srfi-26-internal-cut () () . slots-or-exprs))))

    (define-syntax cute
      (syntax-rules ()
        ((cute . slots-or-exprs)
         (srfi-26-internal-cute () () () . slots-or-exprs))))

  )) ;; end begin, define-library
