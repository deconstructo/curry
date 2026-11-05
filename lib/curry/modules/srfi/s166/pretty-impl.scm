;;; SRFI-166 (srfi 166 pretty): pretty, pretty-shared, pretty-simply and
;;; pretty-with-color, formatters backed by the SRFI-272 pretty printer.
;;; The SRFI gives no layout rules, so the layout is that engine's: forms
;;; use the style table, data lists print one element per line. The
;;; output starts at the current column, later lines are indented to it,
;;; and the line width is the `width` state variable.
;;; https://srfi.schemers.org/srfi-166/

(define-library (srfi s166 pretty-impl)
  (import (scheme base)
          (srfi s166 base-impl)
          (srfi s272 engine)
          ;; Loaded so pp-color #t has its default mapper.
          (only (srfi s272 colorize-impl) sgr-support-tier))
  (export pretty pretty-shared pretty-simply pretty-with-color)
  (begin

    (define (%pretty obj mode color?)
      (%make-formatter
        (lambda (st)
          (let ((number-hook
                  (cons number?
                        (atom-pp-hook #f #f (lambda (n r) (list (%format-number-from-state st n)))))))
            (%emit! st
              (parameterize ((pp-width (%get st width))
                             (pp-indent (%fstate-col st))
                             (pp-newline #f)
                             (pp-pretty #t)
                             (pp-code #t)
                             (pp-color color?)
                             ;; Asking for color gets color even when the
                             ;; terminal was not detected.
                             (sgr-support-tier (if color? (or (sgr-support-tier) 0) (sgr-support-tier)))
                             (pp-hooks (cons number-hook (pp-hooks))))
                (%render-to-string obj mode)))))))

    (define (pretty obj) (%pretty obj 'circle #f))
    (define (pretty-shared obj) (%pretty obj 'graph #f))
    (define (pretty-simply obj) (%pretty obj 'simple #f))
    (define (pretty-with-color obj) (%pretty obj 'circle #t))))
