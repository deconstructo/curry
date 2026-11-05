;;; SRFI-166 (srfi 166 unicode): terminal-column widths, width-based
;;; substrings, and case conversion that leaves ANSI sequences alone.
;;; https://srfi.schemers.org/srfi-166/
;;;
;;; Widths come from (srfi 272 measure)'s tables (UAX #11, Unicode 16.0):
;;; wide and fullwidth characters take 2 columns, format characters and
;;; nonspacing marks 0, everything else 1 (cuneiform signs and Akkadian
;;; transliteration letters included). The /wide variants count East Asian
;;; Ambiguous characters as 2. An ANSI escape sequence takes 0 columns.
;;; Substrings never split a cluster (a base character with its marks, a
;;; ZWJ sequence, a flag).

(define-library (srfi s166 unicode-impl)
  (import (scheme base) (scheme char)
          (srfi s166 base-impl)
          (srfi s272 measure-impl))
  (export terminal-aware
          string-terminal-width string-terminal-width/wide
          substring-terminal-width substring-terminal-width/wide
          substring-terminal-preserve
          upcased downcased)
  (begin

    ;; ── ANSI sequences ───────────────────────────────────────────────────────

    (define ESC (integer->char 27))

    ;; The end of the escape sequence starting at i (text[i] is ESC), or i
    ;; if ESC does not start a complete one (an unterminated sequence is not
    ;; one). CSI ends at a final byte 0x40-0x7E; OSC ends at BEL or ESC \.
    (define (%ansi-end s i)
      (let ((len (string-length s)))
        (if (>= (+ i 1) len)
            i
            (let ((c (string-ref s (+ i 1))))
              (cond
                ((char=? c #\[)
                 (let loop ((j (+ i 2)))
                   (cond ((>= j len) i)
                         ((let ((n (char->integer (string-ref s j)))) (and (>= n #x40) (<= n #x7E))) (+ j 1))
                         (else (loop (+ j 1))))))
                ((char=? c #\])
                 (let loop ((j (+ i 2)))
                   (cond ((>= j len) i)
                         ((char=? (string-ref s j) (integer->char 7)) (+ j 1))
                         ((and (char=? (string-ref s j) ESC) (< (+ j 1) len) (char=? (string-ref s (+ j 1)) #\\))
                          (+ j 2))
                         (else (loop (+ j 1))))))
                (else i))))))

    ;; Splits s into segments (kind start end): kind is 'ansi or 'text.
    (define (%segments s)
      (let ((len (string-length s)))
        (let loop ((i 0) (start 0) (acc '()))
          (cond
            ((>= i len) (reverse (if (> i start) (cons (list 'text start i) acc) acc)))
            ((and (char=? (string-ref s i) ESC) (> (%ansi-end s i) i))
             (let ((e (%ansi-end s i)))
               (loop e e (cons (list 'ansi i e) (if (> i start) (cons (list 'text start i) acc) acc)))))
            (else (loop (+ i 1) start acc))))))

    (define (%strip-ansi s)
      (apply string-append
             (map (lambda (seg) (substring s (cadr seg) (caddr seg)))
                  (let loop ((segs (%segments s)) (acc '()))
                    (cond ((null? segs) (reverse acc))
                          ((eq? (caar segs) 'text) (loop (cdr segs) (cons (car segs) acc)))
                          (else (loop (cdr segs) acc)))))))

    ;; ── Widths ───────────────────────────────────────────────────────────────

    (define (%wide-width-procedure)
      (let ((base (char-width-procedure)))
        (lambda (c)
          (let ((w (base c))) (if (and (eqv? w 1) (%ambiguous-char? c)) 2 w)))))

    (define (%optional-range s args)
      (if (pair? args)
          (substring s (car args) (if (pair? (cdr args)) (cadr args) (string-length s)))
          s))

    (define (string-terminal-width str . range)
      (%string-width (%strip-ansi (%optional-range str range))))

    (define (string-terminal-width/wide str . range)
      (parameterize ((char-width-procedure (%wide-width-procedure)))
        (apply string-terminal-width str range)))

    ;; The clusters of s as (start end width), with each ANSI sequence a
    ;; zero-width cluster of its own.
    (define (%clusters s)
      (let loop ((segs (%segments s)) (acc '()))
        (if (null? segs)
            (reverse acc)
            (let ((seg (car segs)))
              (if (eq? (car seg) 'ansi)
                  (loop (cdr segs) (cons (list (cadr seg) (caddr seg) 0) acc))
                  (let ((offset (cadr seg)))
                    (loop (cdr segs)
                          (append (reverse (map (lambda (c) (list (+ offset (car c)) (+ offset (cadr c)) (caddr c)))
                                                (%string-clusters (substring s (cadr seg) (caddr seg)))))
                                  acc))))))))

    ;; The text between columns from and to: every cluster whose end column
    ;; lies after from and at or before to. A negative from takes in
    ;; zero-width characters at the start.
    (define (substring-terminal-width str from to)
      (let loop ((cs (%clusters str)) (cum 0) (acc '()))
        (if (null? cs)
            (apply string-append (reverse acc))
            (let* ((c (car cs)) (end-col (+ cum (caddr c))))
              (loop (cdr cs) end-col
                    (if (and (> end-col from) (<= end-col to))
                        (cons (substring str (car c) (cadr c)) acc)
                        acc))))))

    (define (substring-terminal-width/wide str from to)
      (parameterize ((char-width-procedure (%wide-width-procedure)))
        (substring-terminal-width str from to)))

    ;; The parts of str that act on the terminal without taking columns:
    ;; ANSI sequences and bidirectional formatting characters (UAX #9).
    (define (%bidi-control? c)
      (let ((n (char->integer c)))
        (or (and (>= n #x202A) (<= n #x202E)) (and (>= n #x2066) (<= n #x2069))
            (= n #x200E) (= n #x200F) (= n #x061C))))

    (define (substring-terminal-preserve str)
      (let ((out (open-output-string)))
        (for-each
          (lambda (seg)
            (if (eq? (car seg) 'ansi)
                (write-string (substring str (cadr seg) (caddr seg)) out)
                (string-for-each (lambda (c) (when (%bidi-control? c) (write-char c out)))
                                 (substring str (cadr seg) (caddr seg)))))
          (%segments str))
        (get-output-string out)))

    (define (terminal-aware . fmts)
      (%make-formatter
        (lambda (st)
          (let ((wide? (%get st ambiguous-is-wide?)))
            (%run (%with (list (cons string-width (if wide? string-terminal-width/wide string-terminal-width))
                               (cons substring/width (if wide? substring-terminal-width/wide substring-terminal-width))
                               ;; trimming passes the columns being cut; keep what
                               ;; acts on the terminal in them
                               (cons substring/preserve
                                     (lambda (str from to)
                                       (substring-terminal-preserve
                                         ((if wide? substring-terminal-width/wide substring-terminal-width)
                                          str from to)))))
                         fmts)
                  st)))))

    ;; ── Case ─────────────────────────────────────────────────────────────────

    ;; Applies convert to the text between ANSI sequences.
    (define (%convert-text str convert)
      (apply string-append
             (map (lambda (seg)
                    (let ((piece (substring str (cadr seg) (caddr seg))))
                      (if (eq? (car seg) 'ansi) piece (convert piece))))
                  (%segments str))))

    ;; Greek capital sigma is a final sigma at the end of a word:
    ;; preceded by a letter and not followed by one.
    (define (%downcase-with-final-sigma s)
      (let ((len (string-length s)))
        (let loop ((i 0) (acc '()))
          (if (>= i len)
              (list->string (reverse acc))
              (let ((c (string-ref s i)))
                (loop (+ i 1)
                      (cons (if (char=? c (integer->char #x3A3))
                                (if (and (> i 0) (char-alphabetic? (string-ref s (- i 1)))
                                         (or (= (+ i 1) len) (not (char-alphabetic? (string-ref s (+ i 1))))))
                                    (integer->char #x3C2)
                                    (integer->char #x3C3))
                                (char-downcase c))
                            acc)))))))

    (define (upcased . fmts)
      (%make-formatter
        (lambda (st) (%emit! st (%convert-text (%capture st fmts) string-upcase)))))

    (define (downcased . fmts)
      (%make-formatter
        (lambda (st) (%emit! st (%convert-text (%capture st fmts) %downcase-with-final-sigma)))))))
