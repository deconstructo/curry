;;; srfi_s54_cat_tests.scm — (srfi s54 cat)

(import (srfi s54 cat))

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

;;; ---- numbers: precision, width, sign, exactness ----

(check "precision, left-align width (negative width)" (cat 129.995 -10 2.) "130.00    ")
(check "precision, right-align width" (cat 129.995 10 2.) "    130.00")
(check "precision rounds using the printed decimal, not the exact binary value"
       (cat 129.985 10 2.) "    129.98")
(check "negative precision suppresses the #e prefix on an exact number" (cat 129 -2.) "129.00")
(check "non-negative precision adds #e prefix on an exact number" (cat 129 2.) "#e129.00")
(check "precision + zero-pad + sign: padding goes after the sign/prefix run"
       (cat 129 10 2. #\0 'sign) "#e+0129.00")
(check "precision + non-zero pad char: padding goes at the string's edge, not after the sign"
       (cat 129 10 2. #\* 'sign) "*#e+129.00")
(check "plain exact rational, no options" (cat 1/3) "1/3")
(check "exact rational with precision gets #e prefix" (cat 1/3 10 2.) "    #e0.33")
(check "exact rational with negative precision: no #e prefix" (cat 1/3 10 -2.) "      0.33")
(check "'exact forces an inexact number's exactness before formatting"
       (cat 129.995 2. 'exact) "#e130.00")
; Regression: precision 0 must still keep the decimal point (with nothing
; after it) -- the spec's own worked example relies on this by taking off
; exactly one trailing character (the ".") via a nested `cat`.
(check "precision 0 keeps a trailing decimal point (exact number)" (cat 129 0.) "#e129.")
(check "precision 0 keeps a trailing decimal point (inexact number)" (cat 129.995 0.) "130.")
(check "spec's own nested-cat example relying on the precision-0 trailing dot"
       (cat (cat 129.995 0.) (list 0 -1)) "130")

;;; ---- separator ----

(check "separator groups both sides of the decimal point, mirrored outward"
       (cat 129.995 10 (list #\, 2)) " 1,29.99,5")
(check "separator with default group size 3, plus forced sign"
       (cat 129995 10 (list #\,) 'sign) "  +129,995")

;;; ---- radix ----

(check "hexadecimal radix on an exact integer, with sign" (cat #x123 'octal 'sign) "#o+443")
(check "non-decimal radix + precision raises (spec's own stated error)"
       (guard (e (#t 'raised)) (cat 3.5 'octal 2.))
       'raised)
(check "non-decimal radix on a non-integer raises"
       (guard (e (#t 'raised)) (cat 3.5 'octal))
       'raised)

;;; ---- malformed optional arguments raise a clear classification error,
;;;      rather than misclassifying and crashing deeper in formatting ----

(check "a malformed take-spec (non-integer second element) raises at classification"
       (guard (e (#t 'raised)) (cat "hello" (list 3 "x")))
       'raised)
(check "a well-formed two-element take-spec still works"
       (cat "string" (list 2 3)) "sting")

;;; ---- strings: width, pipe, take ----

(check "string, left-align width" (cat "string" -10) "string    ")
(check "string with a one-procedure pipe" (cat "string" 10 (list string-upcase)) "    STRING")
(check "string with pipe then negative-n take (drop from the left)"
       (cat "string" 10 (list string-upcase) (list -2)) "      RING")
(define (%titlecase s)
  (if (= (string-length s) 0) s
      (string-append (string (char-upcase (string-ref s 0))) (substring s 1 (string-length s)))))
(check "string with two-sided take (n from the left, m from the right)"
       (cat "string" 10 `(,%titlecase) (list 2 3)) "     Sting")
(define (%string-reverse s) (list->string (reverse (string->list s))))
(check "two-procedure pipe chains left to right"
       (cat "string" `(,%string-reverse ,string-upcase)) "GNIRTS")

;;; ---- default writer: display for self-evaluating constants, write otherwise ----

(check "char uses display (bare character, no #\\ prefix)" (cat #\a 10) "         a")
(check "symbol" (cat 'symbol 10) "    symbol")
(check "list uses write (chars/strings inside get write-escaped)"
       (cat '(#\a "str" s)) "(#\\a \"str\" s)")
(check "vector uses write" (cat #(#\a "str" s)) "#(#\\a \"str\" s)")
(check "explicit writer argument overrides the default" (cat "str" write) "\"str\"")

;;; ---- string* argument: appended in order, possibly more than once ----

(check "a plain string argument is appended after the formatted value"
       (cat 129 "-" (cat "str")) "129-str")
(check "multiple string arguments append in the order given"
       (cat 3 (cat 's) " " (cat "str" write)) "3s \"str\"")

;;; ---- converter ----

(define (example? x) (and (pair? x) (eq? (car x) 'ex)))
(define (record->string x) (string-append (number->string (cadr x)) "-" (caddr x)))

(check "converter: predicate matches, converter proc used, width still applies"
       (cat (list 'ex 123 "string") 20 (cons example? record->string))
       "          123-string")
(check "converter: predicate doesn't match falls back to normal formatting"
       (cat 42 (cons example? record->string))
       "42")

;;; ---- port ----

(check "port #f (default): no side effect, just returns the string"
       (cat 129 2.) "#e129.00")

;;; ---- Summary ----

(newline)
(display "srfi-s54 tests: ")
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0)
    (begin (display "SOME TESTS FAILED") (newline) (exit 1))
    (begin (display "all OK") (newline)))
