;;; SRFI-272 measure library implementation: char-width-procedure, a
;;; parameter holding a (char) -> (0|1|2|#f) terminal-column-width
;;; function. https://srfi.schemers.org/srfi-272/
;;;
;;; The default procedure follows UAX #11 (East Asian Width) as the spec
;;; asks, using tables generated from the Unicode Character Database by
;;; tools/gen-width-table.py (see width-data.scm):
;;;   - East Asian Wide / Fullwidth and Emoji_Presentation  -> 2
;;;   - general category Mn / Me / Cf (combining marks, zero-width
;;;     joiners, variation selectors, tag characters) and the Hangul
;;;     medial vowels / final consonants                    -> 0
;;;   - general category Cc (C0/C1 controls)                -> #f
;;;   - everything else, including cuneiform (U+12000..U+1254F, East
;;;     Asian Width N) and the Akkadian transliteration letters, which
;;;     are precomposed or base + combining mark            -> 1
;;;
;;; %string-width is the engine's column measure for a whole string. It
;;; adds the few cluster rules a per-character width cannot express, so a
;;; string is measured as it renders on a terminal rather than as a count
;;; of code points: a ZWJ sequence, a regional-indicator flag and an emoji
;;; with a skin-tone modifier each take the columns of their base glyph,
;;; and VARIATION SELECTOR-16 widens a text-default symbol to 2 columns.

(define-library (srfi s272 measure-impl)
  (import (scheme base) (srfi s272 width-data))
  (export char-width-procedure %string-width)
  (begin

    ;; Binary search over a flat (lo hi lo hi ...) vector.
    (define (%in-ranges? cp ranges)
      (let loop ((lo 0) (hi (quotient (vector-length ranges) 2)))
        (if (>= lo hi)
            #f
            (let* ((mid (quotient (+ lo hi) 2))
                   (rlo (vector-ref ranges (* 2 mid)))
                   (rhi (vector-ref ranges (+ 1 (* 2 mid)))))
              (cond ((< cp rlo) (loop lo mid))
                    ((> cp rhi) (loop (+ mid 1) hi))
                    (else #t))))))

    (define (%default-char-width c)
      (let ((cp (char->integer c)))
        (cond
          ((< cp #x20) #f)
          ((< cp #x7F) 1)
          ((%in-ranges? cp %control-ranges) #f)
          ((%in-ranges? cp %zero-width-ranges) 0)
          ((%in-ranges? cp %wide-ranges) 2)
          (else 1))))

    (define char-width-procedure (make-parameter %default-char-width))

    (define (%regional-indicator? cp) (and (>= cp #x1F1E6) (<= cp #x1F1FF)))
    (define (%emoji-modifier? cp) (and (>= cp #x1F3FB) (<= cp #x1F3FF)))

    ;; Symbols whose default presentation is text (width 1) but which
    ;; render as 2-column emoji when followed by U+FE0F.
    (define (%text-default-emoji? cp)
      (or (memv cp '(#xA9 #xAE #x203C #x2049 #x2122 #x2139 #x3030 #x303D #x3297 #x3299))
          (and (>= cp #x2190) (<= cp #x2BFF))
          (and (>= cp #x1F000) (<= cp #x1FAFF))))

    ;; Terminal columns occupied by string s. Control characters and
    ;; anything the width procedure scores #f count as 0.
    (define (%string-width s)
      (let ((len (string-length s)) (width (char-width-procedure)))
        (let loop ((i 0) (total 0) (prev-cp 0) (prev-w 0) (joined? #f))
          (if (>= i len)
              total
              (let* ((c (string-ref s i))
                     (cp (char->integer c))
                     (w (or (width c) 0)))
                (cond
                  ;; Second half of a flag, a skin-tone modifier, and the
                  ;; character after a ZWJ all merge into the preceding
                  ;; glyph.
                  ((or (and joined? (> w 0))
                       (and (%regional-indicator? cp) (%regional-indicator? prev-cp))
                       (and (%emoji-modifier? cp) (= prev-w 2)))
                   (loop (+ i 1) total (if (%regional-indicator? cp) -1 cp) prev-w #f))
                  ;; ZWJ itself is width 0 and joins the next glyph.
                  ((= cp #x200D) (loop (+ i 1) total cp prev-w #t))
                  ;; VS16 widens a preceding text-default symbol.
                  ((and (= cp #xFE0F) (= prev-w 1) (%text-default-emoji? prev-cp))
                   (loop (+ i 1) (+ total 1) cp 2 #f))
                  (else
                   (loop (+ i 1) (+ total w) cp (if (= w 0) prev-w w) #f))))))))
))
