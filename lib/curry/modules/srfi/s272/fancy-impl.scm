;;; SRFI-272 fancy-tier implementation: everything from advanced, plus
;;; an enhanced pprint-file that preserves line comments and the
;;; blank-line spacing between top-level forms (when pp-decorate is
;;; true) instead of intermediate's naive read+reprint, an Emacs-style
;;; "Local Variables:" trailer recognized for per-symbol pp-style
;;; hints, and pprint-file/html (same formatting, HTML-escaped output
;;; wrapped in a <pre> block, no color-CSS -- (srfi 272 colorize) is
;;; the library responsible for actual color output, and composes with
;;; this one by installing pp-emit/pp-tint before calling it).
;;; https://srfi.schemers.org/srfi-272/
;;;
;;; Scope note (this implementation only, since the SRFI is Draft and
;;; leaves the exact file-preservation and local-variable mechanism
;;; unspecified beyond prose): a top-level form's own boundary is found
;;; by a hand-written balanced-delimiter scanner (parens/brackets,
;;; string/char literals, `;` line comments, `#|...|#` block comments,
;;; `#;` datum comments) operating on raw source text -- not by asking
;;; the reader for a source span, which curry's own `read` doesn't
;;; expose. Blank lines and comment-only lines between forms are
;;; preserved verbatim; a comment on the SAME line as code is not
;;; (curry's writer has nowhere to attach it once the datum is parsed).
;;; The "Local Variables:" trailer is scanned for `pp-style: SYM STYLE`
;;; lines only, associating SYM's pretty-style via `lookup-pp-style` on
;;; a small built-in style registry (`%file-style-registry`, below) --
;;; a deliberately narrow reading of "recognizes ... style comments for
;;; custom formatting rules" sufficient to be genuinely useful without
;;; inventing a whole undocumented DSL.

(define-library (srfi s272 fancy-impl)
  (import (scheme base) (scheme file) (scheme char)
          (srfi s272 engine) (srfi s272 advanced-impl))
  (export
    pp pp* pprint pprint-shared pprint-simple pprint-file pprint-file/html
    pp-width pp-graph pp-circle pp-radix pp-level pp-length
    pp-lines pp-level-stub pp-length-stub pp-lines-stub pp-newline
    pp-inline-width pp-miser-width pp-indent pp-tab pp-max-tab
    pp-code pp-brackets pp-pretty pp-color pp-decorate pp-emit pp-tint
    pp-styles pretty-style add-pp-style lookup-pp-style
    pp-hooks pretty-hook add-pp-hook lookup-pp-hook
    glst-pp-hook bvec-pp-hook atom-pp-hook rmac-pp-hook
    make-pprint-generator)
  (begin

    (define (%read-whole-file path)
      (call-with-input-file path
        (lambda (p)
          (let ((out (open-output-string)))
            (let loop ()
              (let ((c (read-char p)))
                (unless (eof-object? c) (write-char c out) (loop))))
            (get-output-string out)))))

    ;; Splits source text into a list of "chunks", each either
    ;; (blank) (comment . text-with-newline) or (form . text), in
    ;; original order. A run of non-blank/non-comment characters up to
    ;; the end of its balanced top-level datum becomes one 'form chunk.
    (define (%chunk-source text)
      (let ((len (string-length text)))
        (let loop ((i 0) (acc '()))
          (cond
            ((>= i len) (reverse acc))
            ((char=? (string-ref text i) #\newline) (loop (+ i 1) (cons (cons 'blank "\n") acc)))
            ((char-whitespace? (string-ref text i)) (loop (+ i 1) acc))
            ((char=? (string-ref text i) #\;)
             (let ((end (%scan-line-end text i len)))
               (loop end (cons (cons 'comment (substring text i end)) acc))))
            (else
             (let ((end (%scan-form-end text i len)))
               (loop end (cons (cons 'form (substring text i end)) acc))))))))

    (define (%scan-line-end text i len)
      (let loop ((i i)) (if (or (>= i len) (char=? (string-ref text i) #\newline)) (min (+ i 1) len) (loop (+ i 1)))))

    ;; Scans one balanced top-level datum starting at i (text[i] is not
    ;; whitespace and not `;`). Tracks paren depth, string/char literal
    ;; state, and `#|...|#` block comments so a `(` inside a string or
    ;; comment doesn't confuse the depth count.
    (define (%scan-form-end text i len)
      (let loop ((i i) (depth 0) (started #f))
        (cond
          ((>= i len) i)
          ((and started (= depth 0)) i)
          (else
           (let ((c (string-ref text i)))
             (cond
               ((char=? c #\") (loop (%scan-string-end text (+ i 1) len) depth #t))
               ((and (char=? c #\#) (< (+ i 1) len) (char=? (string-ref text (+ i 1)) #\|))
                (loop (%scan-block-comment-end text (+ i 2) len) depth started))
               ((and (char=? c #\#) (< (+ i 1) len) (char=? (string-ref text (+ i 1)) #\\))
                (loop (+ i 3) depth #t))
               ((or (char=? c #\() (char=? c #\[)) (loop (+ i 1) (+ depth 1) #t))
               ((or (char=? c #\)) (char=? c #\])) (loop (+ i 1) (- depth 1) #t))
               ((char=? c #\;) (loop (%scan-line-end text i len) depth started))
               ((and (= depth 0) started) i)
               (else (loop (+ i 1) depth #t))))))))

    (define (%scan-string-end text i len)
      (let loop ((i i))
        (cond
          ((>= i len) i)
          ((char=? (string-ref text i) #\\) (loop (+ i 2)))
          ((char=? (string-ref text i) #\") (+ i 1))
          (else (loop (+ i 1))))))

    (define (%scan-block-comment-end text i len)
      (let loop ((i i))
        (cond
          ((>= i (- len 1)) len)
          ((and (char=? (string-ref text i) #\|) (char=? (string-ref text (+ i 1)) #\#)) (+ i 2))
          (else (loop (+ i 1))))))

    ;; ── Local Variables: pp-style hints ───────────────────────────────────────

    (define %file-style-registry (make-parameter '()))

    (define (%parse-local-variables text)
      (let ((lv-start (%find-substring text "Local Variables:")))
        (if (not lv-start)
            '()
            (let ((lines (%split-nl (substring text lv-start (string-length text)))))
              (let loop ((lines lines) (acc '()))
                (cond
                  ((null? lines) (reverse acc))
                  ((%find-substring (car lines) "End:") (reverse acc))
                  (else
                   (let ((kv (%parse-pp-style-line (car lines))))
                     (loop (cdr lines) (if kv (cons kv acc) acc))))))))))

    (define (%parse-pp-style-line line)
      (let ((tag-pos (%find-substring line "pp-style:")))
        (and tag-pos
             (let* ((rest (substring line (+ tag-pos 9) (string-length line)))
                    (tokens (%split-ws rest)))
               (and (>= (length tokens) 2)
                    (cons (string->symbol (car tokens)) (string->symbol (cadr tokens))))))))

    (define (%split-nl s)
      (let ((len (string-length s)))
        (let loop ((i 0) (start 0) (acc '()))
          (cond
            ((>= i len) (reverse (cons (substring s start i) acc)))
            ((char=? (string-ref s i) #\newline) (loop (+ i 1) (+ i 1) (cons (substring s start i) acc)))
            (else (loop (+ i 1) start acc))))))

    (define (%split-ws s)
      (let ((len (string-length s)))
        (let loop ((i 0) (acc '()))
          (cond
            ((>= i len) (reverse acc))
            ((char-whitespace? (string-ref s i)) (loop (+ i 1) acc))
            (else
             (let scan ((j i)) (if (or (>= j len) (char-whitespace? (string-ref s j)))
                                    (loop j (cons (substring s i j) acc))
                                    (scan (+ j 1)))))))))

    (define (%find-substring haystack needle)
      (let ((hlen (string-length haystack)) (nlen (string-length needle)))
        (let loop ((i 0))
          (cond
            ((> (+ i nlen) hlen) #f)
            ((string=? (substring haystack i (+ i nlen)) needle) i)
            (else (loop (+ i 1)))))))

    ;; ── The enhanced pprint-file itself ───────────────────────────────────────

    ;; Core renderer, parameterized over an already-open output PORT --
    ;; shared by pprint-file (opens a file path) and pprint-file/html
    ;; (renders into a string buffer first, to HTML-escape afterward).
    (define (%pprint-file-to-port infile out kvs)
      (let* ((text (%read-whole-file infile))
             (style-hints (%parse-local-variables text)))
        (%pp-with-kwargs kvs
          (lambda ()
            (parameterize ((pp-styles (append style-hints (pp-styles))))
              (if (not (pp-decorate))
                  (for-each
                    (lambda (chunk) (when (eq? (car chunk) 'form)
                                       (%pp-to-port (%read-from-string (cdr chunk)) out (%current-mode))))
                    (%chunk-source text))
                  (for-each
                    (lambda (chunk)
                      (case (car chunk)
                        ((blank) (write-char #\newline out))
                        ((comment) (write-string (cdr chunk) out))
                        ((form) (%pp-to-port (%read-from-string (cdr chunk)) out (%current-mode)))))
                    (%chunk-source text))))))))

    ;; outfile is a filename string per the spec, but an already-open
    ;; output port is also accepted directly (used verbatim, never
    ;; closed by this procedure) -- see the identical note on
    ;; intermediate-impl's own pprint-file.
    (define (%split-outfile-and-kvs args)
      (cond
        ((and (pair? args) (string? (car args))) (values (open-output-file (car args)) #t (cdr args)))
        ((and (pair? args) (port? (car args))) (values (car args) #f (cdr args)))
        (else (values (current-output-port) #f args))))

    (define (pprint-file infile . args)
      (call-with-values (lambda () (%split-outfile-and-kvs args))
        (lambda (out owns-out? kvs)
          (%pprint-file-to-port infile out kvs)
          (when owns-out? (close-port out)))))

    (define (%read-from-string s)
      (let ((p (open-input-string s))) (read p)))

    ;; ── HTML variant ─────────────────────────────────────────────────────────

    (define (%html-escape s)
      (let ((out (open-output-string)))
        (string-for-each
          (lambda (c)
            (case c
              ((#\<) (write-string "&lt;" out))
              ((#\>) (write-string "&gt;" out))
              ((#\&) (write-string "&amp;" out))
              (else (write-char c out))))
          s)
        (get-output-string out)))

    (define (pprint-file/html infile . args)
      (call-with-values (lambda () (%split-outfile-and-kvs args))
        (lambda (out owns-out? kvs)
          (let ((buf (open-output-string)))
            (%pprint-file-to-port infile buf kvs)
            (write-string "<pre>" out)
            (write-string (%html-escape (get-output-string buf)) out)
            (write-string "</pre>" out)
            (when owns-out? (close-port out))))))))
