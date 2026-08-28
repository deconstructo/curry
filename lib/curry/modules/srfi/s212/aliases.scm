;;; SRFI-212: Aliases.
;;;
;;; `(define-alias new old)` makes `new` name the same binding as `old`.
;;; This works generically -- as a bare reference or applied -- for any
;;; ordinary value (a procedure, a number, anything `define` can bind),
;;; which covers the overwhelming majority of real aliasing use (e.g.
;;; giving a foreign-language synonym to an existing procedure, the
;;; pattern curry's own Akkadian names already use throughout the
;;; codebase).
;;;
;;; It can NOT alias a syntax-rules macro or one of curry's hardcoded
;;; special forms (`cond`, `lambda`, `define-record-type`, ...): curry's
;;; macro system is syntax-rules only (no syntax-case/er-macro-transformer
;;; reflection), so there is no portable way for this macro to detect
;;; "is `old` bound to a macro rather than a value" and branch -- the
;;; same wall SRFI-61 hit trying to override `cond` itself (see issue
;;; #81). Aliasing a macro `old` needs a hand-written forwarding
;;; `(define-syntax new (syntax-rules () ((_ . args) (old . args))))`
;;; instead, and aliasing a hardcoded special form isn't possible at all
;;; via a library.
(define-library (srfi s212 aliases)
  (import (scheme base))
  (export define-alias)
  (begin
    (define-syntax define-alias
      (syntax-rules ()
        ((_ new old) (define new old))))))
