;;; Tests for (srfi s14 char-sets) / (srfi 14) -- char-sets. Covers
;;; construction, membership, iteration, set algebra, comparisons, and
;;; the standard predefined char-sets (both the full-Unicode ones backed
;;; by curry's own classification tables, and the ASCII-only ones).

(import (srfi 14) (scheme write))

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

;;; ════════════════════════════════════════════════════════════
;;; § 1  construction / basic membership
;;; ════════════════════════════════════════════════════════════

(check "char-set? on a real char-set" (char-set? (char-set #\a #\b)) #t)
(check "char-set? on a non-char-set" (char-set? 42) #f)
(check "char-set contains its own members" (char-set-contains? (char-set #\a #\b #\c) #\b) #t)
(check "char-set does not contain a non-member" (char-set-contains? (char-set #\a #\b) #\z) #f)
(check "empty char-set contains nothing" (char-set-contains? char-set:empty #\a) #f)
(check "list->char-set" (char-set->string (list->char-set (list #\c #\a #\b))) "abc")
(check "string->char-set" (char-set->string (string->char-set "cab")) "abc")
(check "duplicate members collapse" (char-set-size (char-set #\a #\a #\a)) 1)
(check "char-set-copy is independent"
  (let* ((cs (char-set #\a)) (cp (char-set-copy cs)))
    (char-set-adjoin! cs #\b)
    (list (char-set->string cs) (char-set->string cp)))
  '("ab" "a"))

;;; ════════════════════════════════════════════════════════════
;;; § 2  ->char-set / ucs-range->char-set
;;; ════════════════════════════════════════════════════════════

(check "->char-set on a char" (char-set->string (->char-set #\x)) "x")
(check "->char-set on a string" (char-set->string (->char-set "zyx")) "xyz")
(check "->char-set on a char-set is identity" (eq? (->char-set char-set:empty) char-set:empty) #t)
(check "ucs-range->char-set is half-open [lo, hi)"
  (char-set->list (ucs-range->char-set 65 68)) '(#\A #\B #\C))
(check "ucs-range->char-set splits around the surrogate gap"
  (char-set-size (ucs-range->char-set #xD7FE #xE002)) 4) ; D7FE,D7FF,E000,E001 -- D800..DFFF excluded

;;; ════════════════════════════════════════════════════════════
;;; § 3  set algebra
;;; ════════════════════════════════════════════════════════════

(check "union" (char-set->string (char-set-union (char-set #\a #\b) (char-set #\b #\c))) "abc")
(check "intersection" (char-set->string (char-set-intersection (char-set #\a #\b) (char-set #\b #\c))) "b")
(check "difference" (char-set->string (char-set-difference (char-set #\a #\b #\c) (char-set #\b))) "ac")
(check "xor" (char-set->string (char-set-xor (char-set #\a #\b) (char-set #\b #\c))) "ac")
(check "complement of complement is identity"
  (char-set= (char-set-complement (char-set-complement (char-set #\a #\b))) (char-set #\a #\b)) #t)
(check "adjoin" (char-set->string (char-set-adjoin (char-set #\a) #\b #\c)) "abc")
(check "delete" (char-set->string (char-set-delete (char-set #\a #\b #\c) #\b)) "ac")

(check "union! mutates in place"
  (let ((cs (char-set #\a))) (char-set-union! cs (char-set #\b)) (char-set->string cs)) "ab")
(check "intersection! mutates in place"
  (let ((cs (char-set #\a #\b))) (char-set-intersection! cs (char-set #\b #\c)) (char-set->string cs)) "b")
(check "difference! mutates in place"
  (let ((cs (char-set #\a #\b))) (char-set-difference! cs (char-set #\a)) (char-set->string cs)) "b")
(check "complement! mutates in place"
  (let ((cs (char-set #\a))) (char-set-complement! cs) (char-set-contains? cs #\a)) #f)

(let-values (((d i) (char-set-diff+intersection (char-set #\a #\b #\c) (char-set #\b #\c #\d))))
  (check "diff+intersection: difference" (char-set->string d) "a")
  (check "diff+intersection: intersection" (char-set->string i) "bc"))

;;; ════════════════════════════════════════════════════════════
;;; § 4  comparison
;;; ════════════════════════════════════════════════════════════

(check "char-set= reflexive/order-independent"
  (char-set= (char-set #\a #\b) (char-set #\b #\a)) #t)
(check "char-set= detects difference" (char-set= (char-set #\a) (char-set #\a #\b)) #f)
(check "char-set<= subset" (char-set<= (char-set #\a) (char-set #\a #\b)) #t)
(check "char-set<= non-subset" (char-set<= (char-set #\a #\z) (char-set #\a #\b)) #f)
(check "char-set-hash is stable across equal sets"
  (= (char-set-hash (char-set #\a #\b)) (char-set-hash (char-set #\b #\a))) #t)

;;; ════════════════════════════════════════════════════════════
;;; § 5  iteration: cursor protocol, fold, for-each, map, unfold
;;; ════════════════════════════════════════════════════════════

(check "cursor walk collects every member in order"
  (let loop ((c (char-set-cursor (char-set #\c #\a #\b))) (acc '()))
    (if (end-of-char-set? c) (reverse acc)
        (loop (char-set-cursor-next (char-set #\c #\a #\b) c) (cons (char-set-ref (char-set #\c #\a #\b) c) acc))))
  '(#\a #\b #\c))
(check "end-of-char-set? on an empty set's cursor" (end-of-char-set? (char-set-cursor char-set:empty)) #t)

(check "char-set-fold collects members"
  (char-set-fold (lambda (c acc) (cons c acc)) '() (char-set #\a #\b #\c))
  '(#\c #\b #\a))

(check "char-set-for-each visits every member"
  (let ((acc '()))
    (char-set-for-each (lambda (c) (set! acc (cons c acc))) (char-set #\a #\b))
    (reverse acc))
  '(#\a #\b))

(check "char-set-map transforms every member"
  (char-set->string (char-set-map char-upcase (char-set #\a #\b))) "AB")
(check "char-set-map raises cleanly if proc returns a non-char, rather than silently building a garbage range"
  (guard (e (#t 'raised)) (char-set-map (lambda (c) 42) (char-set #\a)) 'not-raised)
  'raised)

(check "char-set-unfold builds from a generator"
  (char-set->string
    (char-set-unfold (lambda (i) (> i 2))
                      (lambda (i) (integer->char (+ 97 i)))
                      (lambda (i) (+ i 1))
                      0))
  "abc")

;;; ════════════════════════════════════════════════════════════
;;; § 6  querying: ->list, ->string, size, count, every, any, filter
;;; ════════════════════════════════════════════════════════════

(define (char-vowel? c) (memv c '(#\a #\e #\i #\o #\u)))

(check "char-set->list is sorted ascending" (char-set->list (char-set #\c #\a #\b)) '(#\a #\b #\c))
(check "char-set-size" (char-set-size (char-set #\a #\b #\c)) 3)
(check "char-set-count" (char-set-count char-vowel? (char-set #\a #\b #\c #\e)) 2)
(check "char-set-every: true" (char-set-every char-alphabetic? (char-set #\a #\b)) #t)
(check "char-set-every: false" (char-set-every char-alphabetic? (char-set #\a #\5)) #f)
(check "char-set-any: true" (char-set-any char-numeric? (char-set #\a #\5)) #t)
(check "char-set-any: false" (char-set-any char-numeric? (char-set #\a #\b)) #f)
(check "char-set-filter" (char-set->string (char-set-filter char-numeric? (char-set #\a #\1 #\b #\2))) "12")

;;; ════════════════════════════════════════════════════════════
;;; § 7  standard char-sets
;;; ════════════════════════════════════════════════════════════

(check "char-set:letter contains ASCII letters" (char-set-contains? char-set:letter #\q) #t)
(check "char-set:letter excludes digits" (char-set-contains? char-set:letter #\5) #f)
(check "char-set:digit contains ASCII digits" (char-set-contains? char-set:digit #\7) #t)
(check "char-set:upper-case / lower-case are disjoint"
  (char-set-size (char-set-intersection char-set:upper-case char-set:lower-case)) 0)
(check "char-set:letter+digit is letter union digit"
  (char-set= char-set:letter+digit (char-set-union char-set:letter char-set:digit)) #t)
(check "char-set:whitespace contains space and tab"
  (list (char-set-contains? char-set:whitespace #\space) (char-set-contains? char-set:whitespace #\tab))
  '(#t #t))
(check "char-set:ascii is exactly 0..127" (char-set-size char-set:ascii) 128)
(check "char-set:iso-control contains NUL and DEL"
  (list (char-set-contains? char-set:iso-control (integer->char 0))
        (char-set-contains? char-set:iso-control (integer->char 127)))
  '(#t #t))
(check "char-set:blank contains space and tab, not newline"
  (list (char-set-contains? char-set:blank #\space)
        (char-set-contains? char-set:blank #\tab)
        (char-set-contains? char-set:blank #\newline))
  '(#t #t #f))
(check "char-set:hex-digit contains 0-9a-fA-F, not g"
  (list (char-set-contains? char-set:hex-digit #\9)
        (char-set-contains? char-set:hex-digit #\a)
        (char-set-contains? char-set:hex-digit #\F)
        (char-set-contains? char-set:hex-digit #\g))
  '(#t #t #t #f))
(check "char-set:punctuation contains ! but not $ (a symbol)"
  (list (char-set-contains? char-set:punctuation #\!) (char-set-contains? char-set:punctuation #\$))
  '(#t #f))
(check "char-set:symbol contains $ but not !"
  (list (char-set-contains? char-set:symbol #\$) (char-set-contains? char-set:symbol #\!))
  '(#t #f))
(check "char-set:graphic excludes space, includes letters/digits/punct/symbol"
  (list (char-set-contains? char-set:graphic #\space)
        (char-set-contains? char-set:graphic #\a)
        (char-set-contains? char-set:graphic #\!)
        (char-set-contains? char-set:graphic #\$))
  '(#f #t #t #t))
(check "char-set:printing is graphic plus space"
  (char-set= char-set:printing (char-set-union char-set:graphic (char-set #\space))) #t)
(check "char-set:title-case is empty (no titlecase table available)"
  (char-set-size char-set:title-case) 0)
(check "char-set:empty really is empty" (char-set-size char-set:empty) 0)
(check "char-set:full contains an arbitrary high codepoint, excludes surrogates"
  (list (char-set-contains? char-set:full (integer->char #x1F600))
        (char-set-size (char-set-intersection char-set:full (ucs-range->char-set #xD800 #xE000))))
  '(#t 0))

;;; ════════════════════════════════════════════════════════════
;;; § 8  full-Unicode coverage smoke test (not just ASCII)
;;; ════════════════════════════════════════════════════════════

;; Ω (U+03A9 GREEK CAPITAL LETTER OMEGA) and ω (U+03C9, lowercase) --
;; exercises the >=128 portion of the underlying classification tables.
(check "char-set:letter recognizes a non-ASCII letter"
  (char-set-contains? char-set:letter (integer->char #x03A9)) #t)
(check "char-set:upper-case recognizes non-ASCII uppercase"
  (char-set-contains? char-set:upper-case (integer->char #x03A9)) #t)
(check "char-set:lower-case recognizes non-ASCII lowercase"
  (char-set-contains? char-set:lower-case (integer->char #x03C9)) #t)
(check "char-set:letter excludes a non-ASCII non-letter (emoji)"
  (char-set-contains? char-set:letter (integer->char #x1F600)) #f)

;; U+20000 (a CJK Unified Ideograph in Extension B) -- exercises real
;; classification-table data above the BMP (>0xFFFF), not just the
;; surrogate-exclusion logic char-set:full's own test above already
;; covers at U+1F600.
(check "char-set:letter recognizes a supplementary-plane letter (>0xFFFF)"
  (char-set-contains? char-set:letter (integer->char #x20000)) #t)

;;; Summary
(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
