;;; (curry schematic markdown) — Markdown generator for Scheme source.
;;;
;;; Ported from Evan Hanson's BSD-licensed schematic
;;; (https://git.foldling.org/schematic/), specifically the
;;; schematic-markdown.scm script — see docs/reference/module-schematic.md.
;;; Doesn't use (curry schematic extract) at all: every comment/code
;;; section from (curry schematic read) becomes a paragraph of prose
;;; (the comment) followed by a fenced/indented code block (the code),
;;; which is upstream's own design — no definition-shape recognition
;;; happens here, unlike schematic-wiki.

(define-library (curry schematic markdown)
  (import (scheme base) (curry schematic read))
  (export scheme->markdown)
  (begin

(define (%string-split str ch)
  (let ((len (string-length str)))
    (let loop ((start 0) (acc '()))
      (let scan ((i start))
        (cond
          ((= i len) (reverse (cons (substring str start i) acc)))
          ((char=? (string-ref str i) ch) (loop (+ i 1) (cons (substring str start i) acc)))
          (else (scan (+ i 1))))))))

;; Converts the Scheme source on `input` to Markdown on `output`: each
;; comment/code section becomes a paragraph of prose followed by a
;; 4-space-indented code block (Markdown's plainest "this is code"
;; convention, needing no fence-syntax awareness from the reader).
;; `comment-prefixes` (default '(";;;" ";;")) is passed straight through
;; to port-fold-source-sections.
(define (scheme->markdown input output . opts)
  (let ((comment-prefixes (if (pair? opts) (car opts) (list ";;;" ";;"))))
    (port-fold-source-sections
      (lambda (docs code carry)
        (unless (string=? docs "")
          (display docs output) (newline output) (newline output))
        (unless (string=? code "")
          (for-each (lambda (line) (display "    " output) (display line output) (newline output))
                    (%string-split code #\newline))
          (newline output))
        carry)
      #f
      comment-prefixes
      input)))

  )) ;; end begin, define-library
