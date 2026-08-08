;;; examples/schematic/schematic-format.scm — Scheme source reindenter CLI
;;;
;;; A CLI wrapper around (curry schematic format), itself a port of Evan
;;; Hanson's BSD-licensed schematic-format (https://git.foldling.org/
;;; schematic/) — see docs/reference/module-schematic.md.
;;;
;;; Reads Scheme source from stdin, reindents it, and writes the result
;;; to stdout. This is NOT a pretty-printer: it never introduces line
;;; breaks or changes intraline spacing, only line indentation.
;;;
;;; Usage:
;;;   ./build/curry examples/schematic/schematic-format.scm [options] [indent-file]
;;;     -b            treat [ ] as ( )
;;;     -c            close all open forms on a stray ]
;;;     -t <tabstop>  indent with tabs of this width instead of spaces
;;;     -h, --help    show this help
;;;
;;;   indent-file, if given, is a path to a file containing a single
;;;   S-expression: a list of (keyword . offset) or (keyword offset ...)
;;;   custom indentation rules overriding (curry schematic format)'s own
;;;   defaults for those keywords — see that module's keyword-indent doc.

(import (scheme base) (scheme read) (scheme write) (scheme file)
        (curry getopt) (curry schematic format))

(define specs
  (list (option 'brackets #\b #f #f #f "treat [ ] as ( )")
        (option 'closure  #\c #f #f #f "close all open forms on a stray ]")
        (option 'tabstop  #\t "tabstop" #t #f "indent with tabs of this width")
        (option 'help     #\h "help"    #f #f "show this help")))

(define result (getopt (cdr (command-line)) specs))

(when (opt-get result 'help)
  (display (opt-usage "schematic-format" specs))
  (exit 0))

(unless (opt-ok? result)
  (for-each (lambda (e) (display e (current-error-port)) (newline (current-error-port))) (opt-errors result))
  (exit 1))

(when (opt-get result 'brackets) (bracket-parentheses? #t))
(when (opt-get result 'closure) (bracket-closure? #t))

(let ((ts (opt-get result 'tabstop)))
  (when ts
    (let ((n (string->number ts)))
      (unless (and n (integer? n) (positive? n))
        (display "schematic-format: invalid tabstop: " (current-error-port))
        (display ts (current-error-port)) (newline (current-error-port))
        (exit 1))
      (tabstop-length n))))

(define indent-file (and (pair? (opt-rest result)) (car (opt-rest result))))

(define keyword-indentation-function
  (if indent-file
      (let ((rules (call-with-input-file indent-file read)))
        (lambda (keyword eol?)
          (let ((r (assq keyword rules)))
            (if r (cdr r) (keyword-indent keyword eol?)))))
      keyword-indent))

(format-scheme (current-input-port) (current-output-port) keyword-indentation-function)
