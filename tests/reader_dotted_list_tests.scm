;;; reader_dotted_list_tests.scm — regression coverage for issue #103: the
;;; reader accepted "(. r)" (a dot with nothing before it) as a valid
;;; 2-element list containing the literal symbol "." followed by "r",
;;; instead of rejecting it. Per R7RS's own grammar ("(datum*)" or
;;; "(datum+ . datum)"), a dot with zero data preceding it matches neither
;;; production and is invalid syntax -- root cause was read_list reading
;;; its own head element via a plain read_datum call with no dot-awareness
;;; at all; only read_list_tail (consulted for every element AFTER the
;;; first) ever checked for a dot marker.

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

;; A bare "." as the very first token of a list must be rejected, not
;; silently read as a 2-element list containing the symbol ".".
;;
;; This must be tested via eval+string-port, not a literal "(. r)" datum
;; in this test file's own source: the read error happens at READ time,
;; before this file's own top-level forms are even evaluated one at a
;; time, so a literal invalid datum here would abort loading this entire
;; test file rather than being something a single check can catch.
(define (read-from-string s)
  (read (open-input-string s)))

(check "reader: bare dot at start of list is rejected, not misread"
       (guard (e (#t 'raised)) (read-from-string "(. r)"))
       'raised)
(check "reader: bare dot at start of a nested list is rejected"
       (guard (e (#t 'raised)) (read-from-string "(a (. r))"))
       'raised)

;; Legitimate dotted-pair syntax is completely unaffected -- the dot
;; still works correctly everywhere it's actually valid (after at least
;; one datum).
(check "reader: ordinary dotted pair, one fixed element"
       (read-from-string "(a . r)") '(a . r))
(check "reader: ordinary dotted pair, two fixed elements"
       (read-from-string "(a b . r)") '(a b . r))
(check "reader: dotted pair as the head of an outer list"
       (read-from-string "((a . b) c)") '((a . b) c))

;; A symbol merely STARTING with '.' (not a bare dot) must still read
;; correctly as the list's head element, not be misidentified as a dot
;; marker -- this is exactly the case the fix's manual-token-building
;; branch (mirroring read_list_tail's own existing technique) has to get
;; right, not just reject.
(check "reader: '...' as the head element of a list"
       (read-from-string "(... a)") '(... a))
(check "reader: a symbol starting with '.' as the head element"
       (read-from-string "(.foo a)") '(.foo a))
(check "reader: '...' as the ONLY element of a list"
       (read-from-string "(...)") '(...))

;; (scheme case-lambda)'s own legal fully-variadic clause shape (a bare
;; symbol, not "(. r)") must be unaffected.
(check "reader: dotted-pair fix does not affect case-lambda's bare-symbol clause"
       (read-from-string "(case-lambda (r r))")
       '(case-lambda (r r)))

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
