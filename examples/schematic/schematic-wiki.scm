;;; examples/schematic/schematic-wiki.scm — svnwiki generator CLI
;;;
;;; A CLI wrapper around (curry schematic wiki), itself a port of Evan
;;; Hanson's BSD-licensed schematic-wiki (https://git.foldling.org/
;;; schematic/) — see docs/reference/module-schematic.md.
;;;
;;; Reads Scheme source from stdin, scans it for commented definitions,
;;; and writes svnwiki documentation fragments to stdout — suitable for
;;; the CHICKEN wiki (http://wiki.call-cc.org/edit-help), though the
;;; underlying tags (<procedure>, <syntax>, <record>, ...) are generic
;;; enough to be useful wherever that markup convention is understood.
;;;
;;; Usage:
;;;   ./build/curry examples/schematic/schematic-wiki.scm [options]
;;;     -c, --comment-prefix <str>  use <str> as the only line-comment
;;;                                 prefix (default: both ";;;" and ";;")
;;;     -t, --types                 use ": name type" declarations to
;;;                                 populate tags rather than the
;;;                                 associated (define ...) form
;;;     -h, --help                  show this help
;;;
;;; Any limitations of schematic-extract (see (curry schematic extract)'s
;;; own header comment) apply equally here.

(import (scheme base) (curry getopt) (curry schematic wiki))

(define specs
  (list (option 'comment-prefix #\c "comment-prefix" #t #f "custom line-comment prefix")
        (option 'types          #\t "types"          #f #f "use : type declarations instead of definitions")
        (option 'help           #\h "help"           #f #f "show this help")))

(define result (getopt (cdr (command-line)) specs))

(when (opt-get result 'help)
  (display (opt-usage "schematic-wiki" specs))
  (exit 0))

(unless (opt-ok? result)
  (for-each (lambda (e) (display e (current-error-port)) (newline (current-error-port))) (opt-errors result))
  (exit 1))

(define comment-prefixes
  (let ((p (opt-get result 'comment-prefix)))
    (if p (list p) (list ";;;" ";;"))))

(scheme->wiki (current-input-port) (current-output-port)
              (opt-get result 'types) comment-prefixes)
