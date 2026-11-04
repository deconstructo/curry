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
  (export char-width-procedure %string-width %string-clusters %ambiguous-char?)
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
          (%in-any? cp '((#x2194 . #x2199) (#x21A9 . #x21AA) (#x231A . #x231B) (#x2328 . #x2328)
                         (#x23CF . #x23CF) (#x23E9 . #x23F3) (#x23F8 . #x23FA) (#x24C2 . #x24C2)
                         (#x25AA . #x25AB) (#x25B6 . #x25B6) (#x25C0 . #x25C0) (#x25FB . #x25FE)
                         (#x2600 . #x27BF) (#x2934 . #x2935) (#x2B05 . #x2B07) (#x2B1B . #x2B1C)
                         (#x2B50 . #x2B50) (#x2B55 . #x2B55)))
          (and (>= cp #x1F000) (<= cp #x1FAFF))))

    (define (%in-any? cp ranges)
      (let loop ((rs ranges))
        (and (pair? rs) (or (and (>= cp (caar rs)) (<= cp (cdar rs))) (loop (cdr rs))))))

    ;; A character a skin-tone modifier can attach to: the emoji blocks
    ;; and the text-default symbols.
    (define (%emoji-base? cp)
      (and (integer? cp) (%text-default-emoji? cp)))

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
                  ((or (and joined? (> w 0) (or (= w 2) (%text-default-emoji? cp)))
                       (and (%regional-indicator? cp) (%regional-indicator? prev-cp))
                       (and (%emoji-modifier? cp) (= prev-w 2) (%emoji-base? prev-cp)))
                   (loop (+ i 1) total (if (%regional-indicator? cp) -1 cp) prev-w #f))
                  ;; ZWJ itself is width 0 and joins the next glyph.
                  ((= cp #x200D) (loop (+ i 1) total cp prev-w #t))
                  ;; VS16 widens a preceding text-default symbol.
                  ((and (= cp #xFE0F) (= prev-w 1) (%text-default-emoji? prev-cp))
                   (loop (+ i 1) (+ total 1) cp 2 #f))
                  (else
                   ;; A zero-width character (a combining mark) keeps the
                   ;; previous glyph's state, including a pending ZWJ.
                   (loop (+ i 1) (+ total w) (if (= w 0) prev-cp cp) (if (= w 0) prev-w w)
                         (and (= w 0) joined?)))))))))

    ;; East Asian Ambiguous characters: 1 column normally, 2 in a CJK
    ;; context (SRFI 166's terminal-aware /wide variants).
    (define (%ambiguous-char? c) (%in-ranges? (char->integer c) %ambiguous-ranges))

    ;; The grapheme-like clusters of s as a list of (start end width), the
    ;; same units %string-width adds up: a base character with its
    ;; combining marks, a ZWJ sequence, a flag, an emoji with a modifier.
    (define (%string-clusters s)
      (let ((len (string-length s)) (width (char-width-procedure)))
        (let loop ((i 0) (acc '()) (prev-cp 0) (prev-w 0) (joined? #f))
          (if (>= i len)
              (reverse acc)
              (let* ((c (string-ref s i))
                     (cp (char->integer c))
                     (cw (width c))
                     (w (or cw 0))
                     (head (and (pair? acc) (car acc))))
                (define (extend! new-w)
                  (cons (list (car head) (+ i 1) new-w) (cdr acc)))
                (cond
                  ;; Merges into the cluster before it, adding no width.
                  ((and head cw
                        (or (and joined? (> w 0) (or (= w 2) (%text-default-emoji? cp)))
                            (and (%regional-indicator? cp) (%regional-indicator? prev-cp))
                            (and (%emoji-modifier? cp) (= prev-w 2) (%emoji-base? prev-cp))))
                   (loop (+ i 1) (extend! (caddr head)) (if (%regional-indicator? cp) -1 cp) prev-w #f))
                  ;; ZWJ joins the next glyph.
                  ((and head (= cp #x200D))
                   (loop (+ i 1) (extend! (caddr head)) cp prev-w #t))
                  ;; VS16 widens a preceding text-default symbol.
                  ((and head (= cp #xFE0F) (= prev-w 1) (%text-default-emoji? prev-cp))
                   (loop (+ i 1) (extend! (+ (caddr head) 1)) cp 2 #f))
                  ;; A zero-width character (a combining mark) joins the
                  ;; cluster before it.
                  ((and head cw (= w 0))
                   (loop (+ i 1) (extend! (caddr head)) prev-cp prev-w joined?))
                  (else
                   (loop (+ i 1) (cons (list i (+ i 1) w) acc) cp w #f))))))))
))
