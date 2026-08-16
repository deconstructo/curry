;;; SRFI-14: Character-Set Library.
;;; https://srfi.schemers.org/srfi-14/
;;;
;;; A char-set is an unordered collection of characters. Internally, a
;;; char-set is a sorted, non-overlapping, non-adjacent vector of
;;; inclusive codepoint ranges (lo . hi) -- a run-length representation
;;; chosen because curry chars span the full Unicode codepoint space
;;; (0..#x10FFFF, minus the surrogate gap), where a bitmap or per-char
;;; hash-table would be enormous for sets like char-set:letter. Range
;;; lists also make char-set-contains? an O(log n) binary search and
;;; the set-algebra operations (union/intersection/difference/xor/
;;; complement) simple sweeps over already-sorted ranges, rather than
;;; per-character hash-table walks.
;;;
;;; This is a curry-native implementation (not a port of SRFI-14's own
;;; reference implementation, which assumes a fixed-size bitstring
;;; representation ill-suited to full Unicode) built on two data
;;; sources:
;;;   - char-set:letter, char-set:digit, char-set:whitespace,
;;;     char-set:upper-case, and char-set:lower-case are backed by
;;;     curry's actual Unicode classification tables (the same tables
;;;     char-alphabetic?/char-numeric?/char-whitespace?/char-upper-case?/
;;;     char-lower-case? use -- see src/unicode.c, src/unicode_tables.c),
;;;     exposed in range form via the unicode-property-ranges builtin.
;;;     char-set:digit intentionally matches curry's own (full-Unicode)
;;;     char-numeric?, not just ASCII 0-9.
;;;   - char-set:title-case, char-set:punctuation, char-set:symbol,
;;;     char-set:iso-control, char-set:blank, char-set:hex-digit,
;;;     char-set:ascii and the char-set:graphic/:printing sets derived
;;;     from them are ASCII-only: curry has no generated Unicode
;;;     General-Category (Pc/Pd/Ps/.../Sm/Sc/Sk/So/Cc/Lt) tables, so
;;;     these classes only recognize their ASCII repertoire.
;;;     char-set:title-case is empty for the same reason (no titlecase
;;;     table at all). This matches curry's own existing convention in
;;;     unicode.c of an ASCII fast path plus full-Unicode coverage only
;;;     where a table exists, rather than silently mis-classifying.

(define-library (srfi s14 char-sets)
  (export
    ;; predicates / comparison
    char-set? char-set= char-set<= char-set-hash
    ;; iteration
    char-set-cursor char-set-ref char-set-cursor-next end-of-char-set?
    char-set-fold char-set-unfold char-set-unfold!
    char-set-for-each char-set-map
    ;; creation
    char-set list->char-set list->char-set!
    string->char-set string->char-set!
    char-set-filter char-set-filter!
    ->char-set ucs-range->char-set ucs-range->char-set! char-set-copy
    ;; querying
    char-set->list char-set->string char-set-size char-set-count
    char-set-contains? char-set-every char-set-any
    ;; set algebra
    char-set-adjoin char-set-adjoin! char-set-delete char-set-delete!
    char-set-complement char-set-complement!
    char-set-union char-set-union!
    char-set-intersection char-set-intersection!
    char-set-difference char-set-difference!
    char-set-xor char-set-xor!
    char-set-diff+intersection char-set-diff+intersection!
    ;; standard char-sets
    char-set:lower-case char-set:upper-case char-set:title-case
    char-set:letter char-set:digit char-set:letter+digit
    char-set:graphic char-set:printing char-set:whitespace
    char-set:iso-control char-set:punctuation char-set:symbol
    char-set:hex-digit char-set:blank char-set:ascii
    char-set:empty char-set:full)
  (import (scheme base))
  (import (srfi s1 lists))
  (import (srfi s132 sorting))
  (begin

;;; ── representation ──────────────────────────────────────────────────
;;; rvec: a vector of (lo . hi) pairs, sorted ascending by lo, with no
;;; two entries overlapping or touching (adjacent ranges are always
;;; merged) -- the canonical form every constructor/mutator maintains.

(define-record-type <char-set>
  (%make-cs-raw rvec)
  char-set?
  (rvec %cs-rvec %cs-set-rvec!))

;; 0..#xD7FF, #xE000..#x10FFFF -- the full valid codepoint domain,
;; matching integer->char's own validity check (src/builtins.c).
(define %full-domain-ranges (list (cons 0 #xD7FF) (cons #xE000 #x10FFFF)))

(define (%merge-sorted-ranges sorted)
  (if (null? sorted)
      '()
      (let loop ((cur (car sorted)) (rest (cdr sorted)) (acc '()))
        (if (null? rest)
            (reverse (cons cur acc))
            (let ((nxt (car rest)))
              (if (<= (car nxt) (+ (cdr cur) 1))
                  (loop (cons (car cur) (max (cdr cur) (cdr nxt))) (cdr rest) acc)
                  (loop nxt (cdr rest) (cons cur acc))))))))

(define (%normalize-ranges ranges)
  (%merge-sorted-ranges (list-sort (lambda (a b) (< (car a) (car b))) ranges)))

(define (%make-cs ranges) (%make-cs-raw (list->vector (%normalize-ranges ranges))))

(define (%cs-ranges-list cs) (vector->list (%cs-rvec cs)))

;; char->integer (like most char primitives) doesn't itself check its
;; argument is really a char -- a bad value would otherwise either
;; silently produce a garbage-but-in-range codepoint or defer a clean
;; error to some unrelated later operation (e.g. char-set->string), far
;; from the actual mistake. Every codepoint entering a char-set's ranges
;; goes through here, so this is the one place that needs to check.
(define (%char->cp c)
  (unless (char? c) (error "char-set: not a char" c))
  (char->integer c))
(define (%chars->ranges chars) (map (lambda (c) (cons (%char->cp c) (%char->cp c))) chars))

;;; ── membership (binary search over the sorted range vector) ────────

(define (%vec-range-contains? rv cp)
  (let loop ((lo 0) (hi (vector-length rv)))
    (if (>= lo hi)
        #f
        (let* ((mid (quotient (+ lo hi) 2)) (r (vector-ref rv mid)))
          (cond ((< cp (car r)) (loop lo mid))
                ((> cp (cdr r)) (loop (+ mid 1) hi))
                (else #t))))))

(define (char-set-contains? cs ch) (%vec-range-contains? (%cs-rvec cs) (%char->cp ch)))

;;; ── range-list set algebra (inputs assumed normalized) ──────────────

;; a minus b, both sorted/merged range lists -> sorted/merged result.
;;
;; Deliberately ONE textual call to `loop` (see (srfi s132 sorting)'s
;; list-merge for the full explanation): curry's compiler/VM does not
;; correctly tail-call-optimize a named let with two or more DIFFERENT
;; self-recursive call sites -- each call genuinely pushes a fresh VM
;; frame instead of reusing one, overflowing the 256-frame guard once a
;; real (non-degenerate) range list is long enough. The four-call-site
;; version this replaced is the natural way to write this sweep;
;; computing next-a/next-b/next-acc as plain values first and making a
;; single parameterized tail call sidesteps the bug.
(define (%ranges-difference a b)
  (let loop ((a a) (b b) (acc '()))
    (cond
      ((null? a) (reverse acc))
      ((null? b) (append (reverse acc) a))
      (else
       (let* ((ra (car a)) (rb (car b))
              (b-before-a? (< (cdr rb) (car ra)))
              (b-after-a? (and (not b-before-a?) (> (car rb) (cdr ra))))
              (overlap? (and (not b-before-a?) (not b-after-a?)))
              (left  (and overlap? (< (car ra) (car rb)) (cons (car ra) (- (car rb) 1))))
              (right (and overlap? (> (cdr ra) (cdr rb)) (cons (+ (cdr rb) 1) (cdr ra))))
              (next-a (cond (b-before-a? a) (b-after-a? (cdr a)) (right (cons right (cdr a))) (else (cdr a))))
              (next-b (cond (b-before-a? (cdr b)) (b-after-a? b) (right (cdr b)) (else b)))
              (next-acc (cond (b-before-a? acc) (b-after-a? (cons ra acc)) (left (cons left acc)) (else acc))))
         (loop next-a next-b next-acc))))))

(define (%ranges-union a b) (%normalize-ranges (append a b)))
;; A ∩ B = A - (A - B) — avoids a third sweep algorithm.
(define (%ranges-intersection a b) (%ranges-difference a (%ranges-difference a b)))
(define (%ranges-xor a b) (%ranges-union (%ranges-difference a b) (%ranges-difference b a)))
(define (%ranges-complement a) (%ranges-difference %full-domain-ranges a))

;;; ── copy / mutators ──────────────────────────────────────────────────

(define (char-set-copy cs) (%make-cs-raw (%cs-rvec cs)))  ; rvec is never mutated in place

(define (%cs-replace! cs ranges) (%cs-set-rvec! cs (list->vector (%normalize-ranges ranges))) cs)

;;; ── creation ─────────────────────────────────────────────────────────

(define (char-set . chars) (%make-cs (%chars->ranges chars)))

(define (list->char-set chars . opt)
  (%make-cs (append (%chars->ranges chars) (if (pair? opt) (%cs-ranges-list (car opt)) '()))))

(define (list->char-set! chars base-cs)
  (%cs-replace! base-cs (append (%chars->ranges chars) (%cs-ranges-list base-cs))))

(define (string->char-set s . opt) (apply list->char-set (string->list s) opt))
(define (string->char-set! s base-cs) (list->char-set! (string->list s) base-cs))

(define (char-set-filter pred cs . opt)
  (%make-cs (append (%chars->ranges (filter pred (char-set->list cs)))
                     (if (pair? opt) (%cs-ranges-list (car opt)) '()))))

(define (char-set-filter! pred cs base-cs)
  (%cs-replace! base-cs (append (%chars->ranges (filter pred (char-set->list cs)))
                                 (%cs-ranges-list base-cs))))

(define (->char-set x)
  (cond ((char-set? x) x)
        ((char? x) (char-set x))
        ((string? x) (string->char-set x))
        (else (error "->char-set: not a char, string, or char-set" x))))

;; SRFI-14 range is half-open [lower, upper); clipped to the valid
;; codepoint domain (splitting around the surrogate gap if straddled).
(define (%valid-subranges lo hi)
  (filter (lambda (r) (<= (car r) (cdr r)))
          (list (cons (max lo 0) (min hi #xD7FF))
                (cons (max lo #xE000) (min hi #x10FFFF)))))

(define (%ucs-range-ranges lower upper error?)
  (when (and error? (or (< lower 0) (> upper (+ #x10FFFF 1))
                         (and (< lower #xE000) (> upper #xD800))))
    (error "ucs-range->char-set: range includes invalid code points" lower upper))
  (%valid-subranges lower (- upper 1)))

(define (ucs-range->char-set lower upper . opt)
  (let ((error? (if (pair? opt) (car opt) #f))
        (base   (if (and (pair? opt) (pair? (cdr opt))) (cadr opt) #f)))
    (%make-cs (append (%ucs-range-ranges lower upper error?)
                       (if base (%cs-ranges-list base) '())))))

(define (ucs-range->char-set! lower upper error? base-cs)
  (%cs-replace! base-cs (append (%ucs-range-ranges lower upper error?) (%cs-ranges-list base-cs))))

;;; ── set algebra ──────────────────────────────────────────────────────

;; `(srfi s1 lists)`'s `fold` calls its callback as (proc element acc) --
;; element first, accumulator last -- per SRFI-1's own convention. Every
;; fold below uses that (x acc) parameter order accordingly. (An earlier
;; version of this file was written against a bug in this codebase's own
;; `fold`, which called the callback with the arguments swapped -- fixed
;; alongside this SRFI-1 session, which is why every call site below
;; needed updating to match the corrected argument order.)

;; Collect every operand's ranges and normalize ONCE at the end, rather
;; than folding through %ranges-union (itself a full re-sort) once per
;; operand -- union doesn't care which input set a range came from, so
;; concatenate-then-merge is exactly equivalent to iterated pairwise
;; union, just O(k log k) over all ranges instead of O(k) re-sorts of
;; a growing accumulator. (Unlike union, xor below is NOT safe to
;; flatten this way -- see its own comment.)
(define (char-set-union . css)
  (%make-cs (fold (lambda (cs acc) (append (%cs-ranges-list cs) acc)) '() css)))

(define (char-set-union! cs1 . css)
  (%cs-replace! cs1 (fold (lambda (cs acc) (append (%cs-ranges-list cs) acc))
                           (%cs-ranges-list cs1) css)))

(define (char-set-intersection cs1 . css)
  (%make-cs (fold (lambda (cs acc) (%ranges-intersection acc (%cs-ranges-list cs)))
                   (%cs-ranges-list cs1) css)))

(define (char-set-intersection! cs1 . css)
  (%cs-replace! cs1 (fold (lambda (cs acc) (%ranges-intersection acc (%cs-ranges-list cs)))
                           (%cs-ranges-list cs1) css)))

(define (char-set-difference cs1 . css)
  (%make-cs (fold (lambda (cs acc) (%ranges-difference acc (%cs-ranges-list cs)))
                   (%cs-ranges-list cs1) css)))

(define (char-set-difference! cs1 . css)
  (%cs-replace! cs1 (fold (lambda (cs acc) (%ranges-difference acc (%cs-ranges-list cs)))
                           (%cs-ranges-list cs1) css)))

;; xor genuinely needs sequential pairwise combination (unlike union
;; above): a codepoint covered by exactly 2 of 3 input sets must NOT
;; appear in the result, so it can't be reduced to a single flatten-
;; and-merge over all operands' ranges -- parity matters, not just
;; coverage.
(define (char-set-xor . css)
  (%make-cs (fold (lambda (cs acc) (%ranges-xor acc (%cs-ranges-list cs))) '() css)))

(define (char-set-xor! cs1 . css)
  (%cs-replace! cs1 (fold (lambda (cs acc) (%ranges-xor acc (%cs-ranges-list cs)))
                           (%cs-ranges-list cs1) css)))

(define (char-set-complement cs) (%make-cs (%ranges-complement (%cs-ranges-list cs))))
(define (char-set-complement! cs) (%cs-replace! cs (%ranges-complement (%cs-ranges-list cs))))

(define (char-set-diff+intersection cs1 . css)
  (values (apply char-set-difference cs1 css) (apply char-set-intersection cs1 css)))

(define (char-set-diff+intersection! cs1 cs2 . css)
  (let ((d (apply char-set-difference cs1 cs2 css))
        (i (apply char-set-intersection cs1 cs2 css)))
    (%cs-replace! cs1 (%cs-ranges-list d))
    (%cs-replace! cs2 (%cs-ranges-list i))
    (values cs1 cs2)))

(define (char-set-adjoin cs . chars) (char-set-union cs (apply char-set chars)))
(define (char-set-adjoin! cs . chars) (char-set-union! cs (apply char-set chars)))
(define (char-set-delete cs . chars) (char-set-difference cs (apply char-set chars)))
(define (char-set-delete! cs . chars) (char-set-difference! cs (apply char-set chars)))

;;; ── comparison ───────────────────────────────────────────────────────

(define (char-set= . css)
  (or (null? css)
      (let ((r0 (%cs-ranges-list (car css))))
        (every (lambda (cs) (equal? r0 (%cs-ranges-list cs))) (cdr css)))))

(define (char-set<= . css)
  (or (null? css)
      (let loop ((cs1 (car css)) (rest (cdr css)))
        (or (null? rest)
            (and (null? (%ranges-difference (%cs-ranges-list cs1) (%cs-ranges-list (car rest))))
                 (loop (car rest) (cdr rest)))))))

(define (char-set-hash cs . opt)
  ;; modulo at every step, not just the end -- otherwise the accumulator
  ;; grows as a base-31 bignum across hundreds of ranges (e.g.
  ;; char-set:letter) instead of staying bounded.
  (let ((bound (max (if (pair? opt) (car opt) 4194304) 1)))
    (fold (lambda (r acc) (modulo (+ (* acc 31) (car r) (* 31 (cdr r))) bound))
          0 (%cs-ranges-list cs))))

;;; ── iteration ────────────────────────────────────────────────────────
;;; A cursor is (range-index . offset-within-range), or the unique
;;; %cs-cursor-end marker once exhausted.

(define %cs-cursor-end (list 'char-set-cursor-end))
(define (end-of-char-set? cursor) (eq? cursor %cs-cursor-end))

(define (char-set-cursor cs)
  (if (zero? (vector-length (%cs-rvec cs))) %cs-cursor-end (cons 0 0)))

(define (char-set-ref cs cursor)
  (integer->char (+ (car (vector-ref (%cs-rvec cs) (car cursor))) (cdr cursor))))

(define (char-set-cursor-next cs cursor)
  (let* ((rv (%cs-rvec cs)) (ri (car cursor)) (off (cdr cursor)) (r (vector-ref rv ri)))
    (if (< (+ (car r) off) (cdr r))
        (cons ri (+ off 1))
        (if (< (+ ri 1) (vector-length rv)) (cons (+ ri 1) 0) %cs-cursor-end))))

(define (char-set-fold kons knil cs)
  (let loop ((c (char-set-cursor cs)) (acc knil))
    (if (end-of-char-set? c)
        acc
        (loop (char-set-cursor-next cs c) (kons (char-set-ref cs c) acc)))))

(define (char-set-for-each proc cs)
  (let loop ((c (char-set-cursor cs)))
    (unless (end-of-char-set? c)
      (proc (char-set-ref cs c))
      (loop (char-set-cursor-next cs c)))))

(define (char-set-map proc cs)
  (%make-cs (%chars->ranges (map proc (char-set->list cs)))))

(define (char-set-unfold p f g seed . opt)
  (let loop ((seed seed) (chars '()))
    (if (p seed)
        (apply list->char-set chars opt)
        (loop (g seed) (cons (f seed) chars)))))

(define (char-set-unfold! p f g seed base-cs)
  (let loop ((seed seed) (chars '()))
    (if (p seed)
        (list->char-set! chars base-cs)
        (loop (g seed) (cons (f seed) chars)))))

;;; ── querying ─────────────────────────────────────────────────────────

(define (%range-chars-onto lo hi acc)
  (if (> lo hi) acc (%range-chars-onto lo (- hi 1) (cons (integer->char hi) acc))))

(define (char-set->list cs)
  (fold-right (lambda (r acc) (%range-chars-onto (car r) (cdr r) acc)) '() (%cs-ranges-list cs)))

(define (char-set->string cs) (list->string (char-set->list cs)))

(define (char-set-size cs)
  (fold (lambda (r acc) (+ acc 1 (- (cdr r) (car r)))) 0 (%cs-ranges-list cs)))

(define (char-set-count pred cs) (count pred (char-set->list cs)))
(define (char-set-every pred cs) (every pred (char-set->list cs)))
(define (char-set-any pred cs) (any pred (char-set->list cs)))

;;; ── standard char-sets ───────────────────────────────────────────────
;;; letter/digit/whitespace/upper-case/lower-case are full-Unicode
;;; (backed by curry's own classification tables); everything else here
;;; is ASCII-only -- see the file header comment.

(define char-set:upper-case (%make-cs (unicode-property-ranges "uppercase")))
(define char-set:lower-case (%make-cs (unicode-property-ranges "lowercase")))
(define char-set:letter     (%make-cs (unicode-property-ranges "alphabetic")))
(define char-set:digit      (%make-cs (unicode-property-ranges "numeric")))
(define char-set:whitespace (%make-cs (unicode-property-ranges "whitespace")))
(define char-set:letter+digit (char-set-union char-set:letter char-set:digit))

(define char-set:title-case (%make-cs '()))

(define char-set:iso-control (%make-cs (list (cons 0 31) (cons 127 127))))
(define char-set:blank       (%make-cs (list (cons 9 9) (cons 32 32))))
(define char-set:hex-digit   (%make-cs (list (cons 48 57) (cons 65 70) (cons 97 102))))
(define char-set:ascii       (%make-cs (list (cons 0 127))))

;; ASCII ispunct (33-47,58-64,91-96,123-126) split into Unicode
;; Sm/Sc/Sk/So ("symbol") vs. everything else ("punctuation").
(define char-set:symbol
  (%make-cs (list (cons 36 36) (cons 43 43) (cons 60 62)
                   (cons 94 94) (cons 96 96) (cons 124 124) (cons 126 126))))
(define char-set:punctuation
  (%make-cs (list (cons 33 35) (cons 37 42) (cons 44 47) (cons 58 59)
                   (cons 63 64) (cons 91 93) (cons 95 95) (cons 123 123) (cons 125 125))))

(define char-set:graphic
  (char-set-union char-set:letter+digit char-set:punctuation char-set:symbol))
(define char-set:printing (char-set-union char-set:graphic (char-set #\space)))

(define char-set:empty (%make-cs '()))
(define char-set:full (%make-cs %full-domain-ranges))

  )) ;; end begin, define-library
