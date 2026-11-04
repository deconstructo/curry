;;; SRFI-166 (srfi 166 columnar): columnar, tabular, wrapped, justified,
;;; wrapped/list, wrapped/char, from-file, line-numbers.
;;; https://srfi.schemers.org/srfi-166/
;;;
;;; A column runs to completion into a string, which is split into lines
;;; and merged with the other columns line by line. A column marked
;;; 'infinite (line-numbers) cannot run to completion: it is run after the
;;; finite columns, and stops by escaping from its output once it has
;;; produced as many lines as they did. Line breaking for wrapped and
;;; justified minimizes the sum of the cubes of each line's trailing
;;; space (the last line is free), as fmt(1) does.

(define-library (srfi s166 columnar-impl)
  (import (scheme base) (scheme char) (scheme file)
          (srfi s166 base-impl))
  (export columnar tabular wrapped wrapped/list wrapped/char justified
          from-file line-numbers)
  (begin

    (define (%sw st) (%get st string-width))

    ;; ── Column specs ─────────────────────────────────────────────────────────
    ;; A spec is a literal string (a border, repeated on every line) or
    ;; [width] [justification] ['infinite] formatter. width is an exact
    ;; positive integer (columns) or a real between 0 and 1 (a fraction of
    ;; what the fixed columns leave).

    (define-record-type <column>
      (%make-column literal fmt width just infinite?)
      %column?
      (literal %col-literal) (fmt %col-fmt) (width %col-width) (just %col-just) (infinite? %col-infinite?))

    (define (%parse-columns specs)
      (let loop ((specs specs) (w #f) (just #f) (inf #f) (acc '()))
        (if (null? specs)
            (reverse acc)
            (let ((x (car specs)))
              (cond
                ((and (string? x) (not w) (not just) (not inf))
                 (loop (cdr specs) #f #f #f (cons (%make-column x #f #f #f #f) acc)))
                ((and (real? x) (not (formatter? x))) (loop (cdr specs) x just inf acc))
                ((memq x '(left right center)) (loop (cdr specs) w x inf acc))
                ((eq? x 'infinite) (loop (cdr specs) w just #t acc))
                (else
                 (loop (cdr specs) #f #f #f (cons (%make-column #f x w (or just 'left) inf) acc))))))))

    ;; Widths for every column given the total width. Literal columns take
    ;; their own width; integer widths are fixed; fractions share what is
    ;; left after those; unspecified columns share the remainder evenly,
    ;; the extra columns going to the leftmost.
    (define (%resolve-widths st cols total)
      (let* ((sw (%sw st))
             (fixed (apply + (map (lambda (c)
                                    (cond ((%col-literal c) (sw (%col-literal c)))
                                          ((exact-integer? (%col-width c)) (%col-width c))
                                          (else 0)))
                                  cols)))
             (avail (max 0 (- total fixed)))
             (fracs (map (lambda (c) (if (and (%col-width c) (not (exact-integer? (%col-width c))))
                                         (inexact->exact (floor (* (%col-width c) avail)))
                                         0))
                         cols))
             (rest (- avail (apply + fracs)))
             (n-free (length (%filter (lambda (c) (and (not (%col-literal c)) (not (%col-width c)))) cols))))
        (let loop ((cs cols) (fs fracs) (free-seen 0) (acc '()))
          (if (null? cs)
              (reverse acc)
              (let ((c (car cs)))
                (cond
                  ((%col-literal c) (loop (cdr cs) (cdr fs) free-seen (cons (sw (%col-literal c)) acc)))
                  ((exact-integer? (%col-width c)) (loop (cdr cs) (cdr fs) free-seen (cons (%col-width c) acc)))
                  ((%col-width c) (loop (cdr cs) (cdr fs) free-seen (cons (car fs) acc)))
                  (else
                   (let ((share (+ (quotient rest (max 1 n-free))
                                   (if (< free-seen (remainder rest (max 1 n-free))) 1 0))))
                     (loop (cdr cs) (cdr fs) (+ free-seen 1) (cons share acc))))))))))

    (define (%filter pred lst)
      (cond ((null? lst) '()) ((pred (car lst)) (cons (car lst) (%filter pred (cdr lst)))) (else (%filter pred (cdr lst)))))

    ;; ── Running columns ──────────────────────────────────────────────────────

    (define (%column-state st w)
      (%make-fstate (cons (cons width w) (%fstate-vars st)) 0 0))

    ;; Lines of a finite column rendered at width w.
    (define (%run-column st c w)
      (let* ((p (open-output-string))
             (st2 (%make-fstate (cons (cons port p) (cons (cons width w) (%fstate-vars st))) 0 0)))
        (%run (%fmt (%col-fmt c)) st2)
        (%split-lines (get-output-string p))))

    ;; The first n lines of an infinite column rendered at width w.
    (define (%run-infinite-column st c w n)
      (let ((buf (open-output-string)) (newlines 0))
        (%run-until (%column-state st w) (list (%col-fmt c))
          (lambda (escape)
            (lambda (str)
              (write-string str buf)
              (string-for-each (lambda (ch) (when (char=? ch #\newline) (set! newlines (+ newlines 1)))) str)
              (when (>= newlines n) (escape #f)))))
        (let ((lines (%split-lines (get-output-string buf))))
          (if (> (length lines) n) (%list-head lines n) lines))))

    (define (%list-head lst n) (if (or (= n 0) (null? lst)) '() (cons (car lst) (%list-head (cdr lst) (- n 1)))))

    ;; Pads text to w columns by justification. The last column is not
    ;; padded on the right.
    (define (%pad-cell st text w just last?)
      (let* ((n (max 0 (- w ((%sw st) text))))
             (pc (%get st pad-char)))
        (case just
          ((right) (string-append (%pad-string pc n) text))
          ((center) (let ((l (quotient n 2))) (string-append (%pad-string pc l) text (%pad-string pc (- n l)))))
          (else (if last? text (string-append text (%pad-string pc n)))))))

    ;; Merges the columns' lines and writes them.
    (define (%emit-columns st cols widths lines-per-col)
      (let* ((finite-lens (map (lambda (c ls) (if (or (%col-literal c) (%col-infinite? c)) 0 (length ls)))
                               cols lines-per-col))
             ;; Only borders (or nothing): one line. Otherwise as many
             ;; lines as the longest finite column.
             (n (if (null? (%filter (lambda (c) (not (%col-literal c))) cols))
                    1
                    (apply max 0 finite-lens)))
             (last-index (- (length cols) 1))
             ;; a vector per column, so a cell is found without rescanning
             (columns (map list->vector lines-per-col)))
        (let line-loop ((i 0))
          (when (< i n)
            (%emit! st
              (let cell-loop ((cs cols) (ws widths) (ls columns) (j 0) (acc '()))
                (if (null? cs)
                    (apply string-append (reverse (cons "\n" acc)))
                    (let* ((c (car cs))
                           (text (cond ((%col-literal c) (%col-literal c))
                                       ((< i (vector-length (car ls))) (vector-ref (car ls) i))
                                       (else "")))
                           (cell (if (%col-literal c)
                                     text
                                     (%pad-cell st text (car ws) (%col-just c) (= j last-index)))))
                      (cell-loop (cdr cs) (cdr ws) (cdr ls) (+ j 1) (cons cell acc))))))
            (line-loop (+ i 1))))))

    ;; The lines of every column: finite columns first, then infinite ones
    ;; limited to the longest finite column.
    (define (%column-lines st cols widths)
      (let* ((finite (map (lambda (c w) (if (or (%col-literal c) (%col-infinite? c)) '() (%run-column st c w)))
                          cols widths))
             (n (apply max 0 (map length finite))))
        (map (lambda (c w ls)
               (if (and (not (%col-literal c)) (%col-infinite? c))
                   (%run-infinite-column st c w n)
                   ls))
             cols widths finite)))

    (define (columnar . specs)
      (%make-formatter
        (lambda (st)
          (let* ((cols (%parse-columns specs))
                 (widths (%resolve-widths st cols (%get st width))))
            (%emit-columns st cols widths (%column-lines st cols widths))))))

    ;; tabular: columns without a width take the width of their widest line.
    (define (tabular . specs)
      (%make-formatter
        (lambda (st)
          (let* ((cols (%parse-columns specs))
                 (total (%get st width))
                 (nominal (%resolve-widths st cols total))
                 (sw (%sw st))
                 (measured
                   (map (lambda (c w)
                          (cond
                            ((or (%col-literal c) (%col-width c)) w)
                            ((%col-infinite? c) w)
                            (else (apply max 0 (map sw (%run-column st c total))))))
                        cols nominal)))
            (%emit-columns st cols measured (%column-lines st cols measured))))))

    ;; ── Wrapping ─────────────────────────────────────────────────────────────

    (define (%words st str)
      (let ((sep? (%get st word-separator?)) (len (string-length str)))
        (let loop ((i 0) (start #f) (acc '()))
          (cond
            ((>= i len) (reverse (if start (cons (substring str start i) acc) acc)))
            ((sep? (string-ref str i)) (loop (+ i 1) #f (if start (cons (substring str start i) acc) acc)))
            (else (loop (+ i 1) (or start i) acc))))))

    ;; Breaks words (a list of strings) into lines of at most target
    ;; columns by minimum raggedness. Returns a list of word lists.
    (define (%break-lines words widths target)
      (let* ((n (length words))
             (wv (list->vector words)) (wd (list->vector widths))
             (best (make-vector (+ n 1) 0)) (next (make-vector (+ n 1) n)))
        ;; best[i]: least cost of laying words i.. out, a line starting at i
        (let fill ((i (- n 1)))
          (when (>= i 0)
            (let try ((j (+ i 1)) (line-width (vector-ref wd i)) (best-cost #f) (best-j (+ i 1)))
              (let* ((cost (if (= j n) 0 (let ((slack (max 0 (- target line-width)))) (* slack slack slack))))
                     (cost (if (and (> line-width target) (> j (+ i 1))) #f cost))
                     (total (and cost (+ cost (vector-ref best j))))
                     (better? (and total (or (not best-cost) (< total best-cost)))))
                (if (and (< j n) (<= (+ line-width 1 (vector-ref wd j)) target))
                    (try (+ j 1) (+ line-width 1 (vector-ref wd j))
                         (if better? total best-cost) (if better? j best-j))
                    (let ((final-cost (if better? total best-cost)) (final-j (if better? j best-j)))
                      (vector-set! best i final-cost)
                      (vector-set! next i final-j)))))
            (fill (- i 1))))
        (let collect ((i 0) (acc '()))
          (if (>= i n)
              (reverse acc)
              (let ((j (vector-ref next i)))
                (collect j (cons (%list-head (list-tail words i) (- j i)) acc)))))))

    (define (%layout-words st str justify?)
      (let* ((sw (%sw st)) (target (%get st width))
             (words (%words st str)) (widths (map sw words)))
        (if (null? words)
            ""
            (let* ((lines (%break-lines words widths target))
                   (last-line (car (reverse lines))))
              (let loop ((ls lines) (acc '()))
                (if (null? ls)
                    (%join-lines (reverse acc))
                    (let* ((line (car ls)) (last? (null? (cdr ls))))
                      (loop (cdr ls)
                            (cons (if (and justify? (not last?) (pair? (cdr line)))
                                      (%justify-line line sw target (%get st pad-char))
                                      (%join-words line (%get st pad-char)))
                                  acc)))))))))

    (define (%join-lines lines)
      (cond ((null? lines) "") ((null? (cdr lines)) (car lines))
            (else (string-append (car lines) "\n" (%join-lines (cdr lines))))))

    (define (%join-words words pc)
      (let ((sp (if (char? pc) (string pc) pc)))
        (cond ((null? words) "") ((null? (cdr words)) (car words))
              (else (string-append (car words) sp (%join-words (cdr words) pc))))))

    ;; Spreads the line's spaces over the gaps, the extra ones to the left.
    (define (%justify-line words sw target pc)
      (let* ((gaps (- (length words) 1))
             (text-width (apply + (map sw words)))
             (space (max gaps (- target text-width)))
             (each (quotient space gaps))
             (extra (remainder space gaps)))
        (let loop ((ws words) (i 0) (acc '()))
          (if (null? (cdr ws))
              (apply string-append (reverse (cons (car ws) acc)))
              (loop (cdr ws) (+ i 1)
                    (cons (string-append (car ws) (%pad-string pc (+ each (if (< i extra) 1 0)))) acc))))))

    (define (wrapped . fmts)
      (%make-formatter (lambda (st) (%emit! st (%layout-words st (%capture st fmts) #f)))))

    (define (justified . fmts)
      (%make-formatter (lambda (st) (%emit! st (%layout-words st (%capture st fmts) #t)))))

    (define (wrapped/list words)
      (%make-formatter
        (lambda (st)
          (let* ((sw (%sw st)) (target (%get st width)))
            (%emit! st (if (null? words)
                           ""
                           (%join-lines (map (lambda (line) (%join-words line (%get st pad-char)))
                                             (%break-lines words (map sw words) target)))))))))

    ;; Breaks at the width, character by character: no word logic.
    (define (wrapped/char . fmts)
      (%make-formatter
        (lambda (st)
          (let* ((str (%capture st fmts)) (target (%get st width)) (sw (%sw st)) (len (string-length str)))
            (let loop ((i 0) (line-start 0) (used 0))
              (cond
                ((>= i len) (%emit! st (substring str line-start len)))
                ((char=? (string-ref str i) #\newline)
                 (%emit! st (substring str line-start (+ i 1)))
                 (loop (+ i 1) (+ i 1) 0))
                (else
                 (let ((cw (sw (string (string-ref str i)))))
                   (if (and (> used 0) (> (+ used cw) target))
                       (begin (%emit! st (substring str line-start i))
                              (%emit! st "\n")
                              (loop i i 0))
                       (loop (+ i 1) line-start (+ used cw)))))))))))

    ;; ── Files and numbering ──────────────────────────────────────────────────

    (define (from-file pathname)
      (%make-formatter
        (lambda (st)
          (call-with-input-file pathname
            (lambda (in)
              (let loop ()
                (let ((line (read-line in)))
                  (unless (eof-object? line)
                    (%emit! st line) (%emit! st "\n")
                    (loop)))))))))

    ;; An endless stream of numbers, one per line, in the current radix.
    ;; Meant for an 'infinite column of columnar.
    (define (line-numbers . maybe-start)
      (%make-formatter
        (lambda (st)
          (let loop ((i (if (pair? maybe-start) (car maybe-start) 1)))
            (%run (numeric i) st)
            (%emit! st "\n")
            (loop (+ i 1))))))))
