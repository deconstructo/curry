;;; SRFI-272 measure library implementation: char-width-procedure, a
;;; parameter holding a (char) -> (0|1|2|#f) terminal-column-width
;;; function. https://srfi.schemers.org/srfi-272/
;;;
;;; Scope note: a fully spec-faithful implementation needs the complete
;;; Unicode "East Asian Width" (UAX #11) property table plus the
;;; combining-mark and default-ignorable tables, none of which curry
;;; ships (no bundled Unicode Character Database). This is a practical
;;; approximation in the same family as the widely-deployed POSIX
;;; `wcwidth`: known Wide/Fullwidth block ranges score 2, known
;;; combining-mark/zero-width ranges score 0, C0/C1 controls and other
;;; non-printable codepoints score #f, everything else scores 1. It
;;; covers the common cases (CJK text, combining diacritics, control
;;; characters) correctly but is not exhaustive across all of Unicode --
;;; documented here rather than silently claimed as complete.

(define-library (srfi s272 measure-impl)
  (import (scheme base) (scheme char))
  (export char-width-procedure)
  (begin

    (define (%in-any-range? cp ranges)
      (let loop ((rs ranges))
        (cond ((null? rs) #f)
              ((and (>= cp (caar rs)) (<= cp (cdar rs))) #t)
              (else (loop (cdr rs))))))

    ;; Representative Wide/Fullwidth ranges (Hangul jamo/syllables, CJK
    ;; radicals/punctuation/ideographs incl. Ext A/B, kana, CJK
    ;; compatibility, fullwidth forms, and common emoji blocks).
    (define %wide-ranges
      '((#x1100 . #x115F) (#x2E80 . #x303E) (#x3041 . #x33FF)
        (#x3400 . #x4DBF) (#x4E00 . #x9FFF) (#xA000 . #xA4CF)
        (#xAC00 . #xD7A3) (#xF900 . #xFAFF) (#xFE30 . #xFE4F)
        (#xFF00 . #xFF60) (#xFFE0 . #xFFE6)
        (#x1F300 . #x1F64F) (#x1F900 . #x1F9FF) (#x20000 . #x2FFFD) (#x30000 . #x3FFFD)))

    ;; Representative combining-mark / zero-width ranges.
    (define %zero-width-ranges
      '((#x0300 . #x036F) (#x0483 . #x0489) (#x0591 . #x05BD)
        (#x0610 . #x061A) (#x064B . #x065F) (#x06D6 . #x06DC)
        (#x0E31 . #x0E31) (#x0E34 . #x0E3A) (#x200B . #x200F)
        (#x20D0 . #x20FF) (#xFE00 . #xFE0F) (#xFE20 . #xFE2F)))

    (define (%control-char? cp)
      (or (<= cp #x1F) (= cp #x7F) (and (>= cp #x80) (<= cp #x9F))))

    (define (%default-char-width c)
      (let ((cp (char->integer c)))
        (cond
          ((%control-char? cp) #f)
          ((%in-any-range? cp %zero-width-ranges) 0)
          ((%in-any-range? cp %wide-ranges) 2)
          (else 1))))

    (define char-width-procedure (make-parameter %default-char-width))))
