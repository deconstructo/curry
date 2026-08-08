;;; examples/schematic/schematic-markdown.scm — Markdown generator CLI
;;;
;;; A CLI wrapper around (curry schematic markdown), itself a port of
;;; Evan Hanson's BSD-licensed schematic-markdown (https://git.foldling
;;; .org/schematic/) — see docs/reference/module-schematic.md.
;;;
;;; Reads Scheme source from stdin and writes Markdown to stdout: every
;;; comment/code section becomes a paragraph of prose followed by a
;;; 4-space-indented code block.
;;;
;;; Usage:
;;;   ./build/curry examples/schematic/schematic-markdown.scm [options]
;;;     -c, --comment-prefix <str>  use <str> as the only line-comment
;;;                                 prefix (default: both ";;;" and ";;")
;;;     -h, --help                  show this help

(import (scheme base) (curry getopt) (curry schematic markdown))

(define specs
  (list (option 'comment-prefix #\c "comment-prefix" #t #f "custom line-comment prefix")
        (option 'help           #\h "help"           #f #f "show this help")))

(define result (getopt (cdr (command-line)) specs))

(when (opt-get result 'help)
  (display (opt-usage "schematic-markdown" specs))
  (exit 0))

(unless (opt-ok? result)
  (for-each (lambda (e) (display e (current-error-port)) (newline (current-error-port))) (opt-errors result))
  (exit 1))

(define comment-prefixes
  (let ((p (opt-get result 'comment-prefix)))
    (if p (list p) (list ";;;" ";;"))))

(scheme->markdown (current-input-port) (current-output-port) comment-prefixes)
