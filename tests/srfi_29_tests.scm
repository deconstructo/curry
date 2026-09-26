;;; srfi_29_tests.scm — (srfi 29) Localization: current-language/
;;; current-country/current-locale-details, declare-bundle!/
;;; store-bundle!/load-bundle!, localized-template, and the bundle
;;; fallback-search algorithm.

(import (scheme base) (srfi 28) (srfi 29))

(define pass 0)
(define fail 0)

(define-syntax check
  (syntax-rules ()
    ((_ label expr expected)
     (let ((got expr))
       (if (equal? got expected)
           (begin (set! pass (+ pass 1)))
           (begin
             (set! fail (+ fail 1))
             (display "FAIL: ") (display label) (newline)
             (display "  expected: ") (write expected) (newline)
             (display "  got:      ") (write got) (newline)))))))

;; Restore locale state to the spec's own defaults before AND after
;; every section, since these are plain global mutables shared across
;; the whole file (matching the SRFI's own reference implementation --
;; see localization.scm's own header comment) and every `check` below
;; that depends on the current locale would otherwise be order-
;; dependent on whatever an earlier section last set it to.
(define (reset-locale!)
  (current-language 'en)
  (current-country 'us)
  (current-locale-details '()))

;;; ---- default locale ----

(reset-locale!)
(check "current-language defaults to en" (current-language) 'en)
(check "current-country defaults to us" (current-country) 'us)
(check "current-locale-details defaults to empty" (current-locale-details) '())

;;; ---- locale setters ----

(check "current-language setter returns unspecified, then getter reflects it"
       (begin (current-language 'fr) (current-language)) 'fr)
(check "current-country setter" (begin (current-country 'ca) (current-country)) 'ca)
(check "current-locale-details setter" (begin (current-locale-details '(utf8)) (current-locale-details)) '(utf8))
(reset-locale!)

;;; ---- declare-bundle! / localized-template basic lookup ----

(declare-bundle! '(t29lib) '((greeting . "Hello, ~a!") (farewell . "Bye, ~a.")))
(check "exact base-bundle lookup finds a declared template"
       (localized-template 't29lib 'greeting) "Hello, ~a!")
(check "format works directly on a localized template"
       (format (localized-template 't29lib 'greeting) "world") "Hello, world!")
(check "lookup for an undeclared template name returns #f"
       (localized-template 't29lib 'nonexistent) #f)
(check "lookup for an undeclared package returns #f"
       (localized-template 't29lib-unknown 'greeting) #f)

;;; ---- fallback search: language/country-specific bundles ----

(declare-bundle! '(t29lib fr) '((greeting . "Bonjour, ~a!")))
(declare-bundle! '(t29lib fr ca) '((greeting . "Allo, ~a!")))

(current-language 'en) (current-country 'us)
(check "en/us locale falls all the way back to the base bundle"
       (localized-template 't29lib 'greeting) "Hello, ~a!")

(current-language 'fr) (current-country 'us)
(check "fr/us locale matches the language-only bundle"
       (localized-template 't29lib 'greeting) "Bonjour, ~a!")

(current-language 'fr) (current-country 'ca)
(check "fr/ca locale matches the most specific bundle"
       (localized-template 't29lib 'greeting) "Allo, ~a!")

(current-language 'de) (current-country 'de)
(check "an unregistered locale still falls back to the base bundle"
       (localized-template 't29lib 'greeting) "Hello, ~a!")

;; fr/ca bundle doesn't override 'farewell -- must fall back past it to
;; the base bundle, not stop and fail just because SOME fr/ca bundle exists.
(current-language 'fr) (current-country 'ca)
(check "fallback skips a more-specific bundle that lacks the requested template"
       (localized-template 't29lib 'farewell) "Bye, ~a.")

(reset-locale!)

;;; ---- declare-bundle! overwrite semantics ----

(declare-bundle! '(t29lib) '((greeting . "Hello, ~a!") (farewell . "Bye, ~a.")))
(declare-bundle! '(t29lib) '((greeting . "Hi there, ~a!")))
(check "re-declaring a bundle with the same specifier replaces it entirely"
       (localized-template 't29lib 'greeting) "Hi there, ~a!")
(check "the old bundle's other entries are gone after being replaced, not merged"
       (localized-template 't29lib 'farewell) #f)

;;; ---- declare-bundle! validation ----

(check "an empty bundle specifier is rejected"
       (guard (e (#t 'caught)) (declare-bundle! '() '()))
       'caught)
(check "a bundle specifier containing a non-symbol is rejected"
       (guard (e (#t 'caught)) (declare-bundle! '(5) '()))
       'caught)
(check "a bare (non-list) bundle specifier is rejected"
       (guard (e (#t 'caught)) (declare-bundle! 't29lib '()))
       'caught)

;;; ---- store-bundle! / load-bundle! (always #f per the SRFI's own text) ----

(check "store-bundle! returns #f" (store-bundle! '(t29lib)) #f)
(check "load-bundle! returns #f" (load-bundle! '(t29lib)) #f)
(check "load-bundle! returning #f leaves the registry unchanged"
       (begin (load-bundle! '(t29lib)) (localized-template 't29lib 'greeting))
       "Hi there, ~a!")

;;; ---- Summary ----

(newline)
(display "srfi-29 tests: ")
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0)
    (begin (display "SOME TESTS FAILED") (newline) (exit 1))
    (begin (display "all OK") (newline)))
