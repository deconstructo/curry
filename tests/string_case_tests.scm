;;; Full Unicode case conversion for strings (R7RS 6.7): string-upcase,
;;; string-downcase and string-foldcase use the multi-character mappings of
;;; SpecialCasing.txt / CaseFolding.txt, string-downcase applies the
;;; final-sigma rule, and string-ci comparisons compare foldcased strings.
;;; Regression tests for issue #355.

(import (scheme base) (scheme write) (scheme char))

(define pass-count 0)
(define fail-count 0)
(define (check name expected actual)
  (if (equal? expected actual)
      (set! pass-count (+ pass-count 1))
      (begin (set! fail-count (+ fail-count 1))
             (display "FAIL: ") (display name)
             (display " expected ") (write expected)
             (display " got ") (write actual) (newline))))

(define (ustr . cps) (list->string (map integer->char cps)))

;; The issue's examples
(check "upcase sharp s" "STRASSE" (string-upcase "stra\xDF;e"))
(check "foldcase sharp s" "strasse" (string-foldcase "Stra\xDF;e"))
(check "ci= sharp s" #t (string-ci=? "Stra\xDF;e" "STRASSE"))

;; Ligatures and other multi-character mappings
(check "upcase fi ligature" "FI" (string-upcase (ustr #xFB01)))
(check "upcase ffl ligature" "FFL" (string-upcase (ustr #xFB04)))
(check "upcase n-apostrophe" (ustr #x2BC #x4E) (string-upcase (ustr #x149)))
(check "upcase j-caron" (ustr #x4A #x30C) (string-upcase (ustr #x1F0)))
(check "foldcase fi ligature" "fi" (string-foldcase (ustr #xFB01)))
(check "downcase I with dot" (ustr #x69 #x307) (string-downcase (ustr #x130)))

;; Simple mappings beyond ASCII and Latin-1 still work in strings
(check "upcase e-acute" (ustr #xC9) (string-upcase (ustr #xE9)))
(check "upcase greek" (ustr #x391 #x392 #x393) (string-upcase (ustr #x3B1 #x3B2 #x3B3)))
(check "downcase cyrillic" (ustr #x43F #x440 #x438) (string-downcase (ustr #x41F #x420 #x418)))
(check "downcase ascii" "hello" (string-downcase "HeLLo"))
(check "upcase ascii" "HELLO" (string-upcase "HeLLo"))
(check "kelvin folds to k" "k" (string-foldcase (ustr #x212A)))

;; Final sigma
(check "final sigma" (ustr #x3BF #x3B4 #x3BF #x3C2) (string-downcase (ustr #x39F #x394 #x39F #x3A3)))
(check "medial sigma" (ustr #x3C3 #x3B1) (string-downcase (ustr #x3A3 #x391)))
(check "word-final sigma, two words"
       (ustr #x3B1 #x3C2 #x20 #x3B2 #x3C2)
       (string-downcase (ustr #x391 #x3A3 #x20 #x392 #x3A3)))

;; Final sigma skips case-ignorable characters (full stop, apostrophe)
(check "final sigma after full stop" (ustr #x3B1 #x2E #x3C2) (string-downcase (ustr #x391 #x2E #x3A3)))
(check "final sigma before apostrophe" (ustr #x3B1 #x3C2 #x27) (string-downcase (ustr #x391 #x3A3 #x27)))
(check "medial sigma across full stop" (ustr #x3B1 #x3C3 #x2E #x3B2) (string-downcase (ustr #x391 #x3A3 #x2E #x392)))
(check "lone sigma" (ustr #x3C3) (string-downcase (ustr #x3A3)))

;; Case-insensitive comparison uses the folded strings
(check "ci= ascii" #t (string-ci=? "Hello" "hELLO"))
(check "ci= non-ascii" #t (string-ci=? (ustr #xC9 #x63 #x6F #x6C #x65) (ustr #xE9 #x43 #x4F #x4C #x45)))
(check "ci= differs" #f (string-ci=? "abc" "abd"))
(check "ci< " #t (string-ci<? "abc" "ABD"))
(check "ci> " #t (string-ci>? "ABD" "abc"))
(check "ci<= equal" #t (string-ci<=? "Abc" "aBC"))
(check "ci>= equal" #t (string-ci>=? "Abc" "aBC"))
(check "ci= three" #t (string-ci=? "Ab" "aB" "AB"))
(check "ci= greek final sigma folds" #t (string-ci=? (ustr #x3C2) (ustr #x3A3)))

;; Embedded NULs take part in comparisons
(check "ci= nul differs" #f (string-ci=? "a\x0;b" "a\x0;c"))
(check "ci= nul same" #t (string-ci=? "A\x0;b" "a\x0;B"))
(check "ci= trailing nul" #f (string-ci=? "ab" "ab\x0;"))
(check "ci< trailing nul" #t (string-ci<? "ab" "ab\x0;"))

;; char-ci comparisons beyond Latin-1
(check "char-ci= e-acute" #t (char-ci=? (integer->char #xC9) (integer->char #xE9)))
(check "char-ci= greek" #t (char-ci=? (integer->char #x391) (integer->char #x3B1)))
(check "char-ci< " #t (char-ci<? #\a #\B))

;; Cuneiform has no case: unchanged
(check "cuneiform unchanged" (ustr #x12079 #x1230B) (string-upcase (ustr #x12079 #x1230B)))
(check "cuneiform foldcase" (ustr #x12079) (string-foldcase (ustr #x12079)))

;; Empty string
(check "empty upcase" "" (string-upcase ""))
(check "empty ci=" #t (string-ci=? "" ""))

(newline)
(display pass-count) (display " passed, ") (display fail-count) (display " failed") (newline)
(if (> fail-count 0) (exit 1))
