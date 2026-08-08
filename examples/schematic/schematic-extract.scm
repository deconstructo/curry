;;; examples/schematic/schematic-extract.scm — commented-definition
;;; extractor CLI
;;;
;;; A CLI wrapper around (curry schematic extract), itself a port of
;;; Evan Hanson's BSD-licensed schematic-extract (https://git.foldling
;;; .org/schematic/) — see docs/reference/module-schematic.md.
;;;
;;; Reads Scheme source from stdin, scans it for commented definitions,
;;; and writes s-expressive specifications to stdout — see (curry
;;; schematic extract)'s own header comment for the exact output shape.
;;;
;;; Usage:
;;;   ./build/curry examples/schematic/schematic-extract.scm [options]
;;;     -c, --comment-prefix <str>  use <str> as the only line-comment
;;;                                 prefix (default: both ";;;" and ";;")
;;;     -t, --types                 use ": name type" declarations to
;;;                                 populate specs rather than the
;;;                                 associated (define ...) form
;;;     -h, --help                  show this help

(import (scheme base) (scheme write) (curry getopt) (curry schematic extract))

(define specs
  (list (option 'comment-prefix #\c "comment-prefix" #t #f "custom line-comment prefix")
        (option 'types          #\t "types"          #f #f "use : type declarations instead of definitions")
        (option 'help           #\h "help"           #f #f "show this help")))

(define result (getopt (cdr (command-line)) specs))

(when (opt-get result 'help)
  (display (opt-usage "schematic-extract" specs))
  (exit 0))

(unless (opt-ok? result)
  (for-each (lambda (e) (display e (current-error-port)) (newline (current-error-port))) (opt-errors result))
  (exit 1))

(define comment-prefixes
  (let ((p (opt-get result 'comment-prefix)))
    (if p (list p) (list ";;;" ";;"))))

(extract-definitions (current-input-port) (current-output-port)
                      (opt-get result 'types) comment-prefixes)
