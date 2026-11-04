;;; SRFI-272 (srfi 272 show): the pretty printer as a SRFI-166 formatter.
;;; https://srfi.schemers.org/srfi-272/
;;;
;;; (pprinted obj [key value] ...) is a formatter like (srfi 166 pretty)'s
;;; `pretty`, but takes the advanced tier's keyword arguments. It starts at
;;; the current column and indents later lines to it. The SRFI-166 `width`
;;; state variable overrides pp-width, as the SRFI requires. Like `pretty`
;;; it writes no trailing newline; follow it with `nl` for one.
;;;
;;; The layout comes from the same engine as `pp`, which is also what
;;; make-pprint-generator reads, so the output is identical line for line.

(define-library (srfi 272 show)
  (import (scheme base)
          (srfi s166 base-impl)
          (srfi s272 engine))
  (export pprinted pprinted*
          pp-width pp-graph pp-circle pp-radix pp-level pp-length
          pp-lines pp-level-stub pp-length-stub pp-lines-stub pp-newline
          pp-inline-width pp-miser-width pp-indent pp-tab pp-max-tab
          pp-code pp-brackets pp-pretty pp-color pp-decorate pp-emit pp-tint
          pp-styles pp-hooks)
  (begin

    (define (%pprinted obj kvs)
      (%make-formatter
        (lambda (st)
          (%emit! st
            (%pp-with-kwargs kvs
              (lambda ()
                (parameterize ((pp-width (%get st width))
                               (pp-indent (%fstate-col st)))
                  (%render-to-string obj (%current-mode)))))))))

    (define (pprinted obj . kvs) (%pprinted obj kvs))

    ;; The last argument is a list of keys and values, spliced in as by apply.
    (define (pprinted* obj . args)
      (let loop ((rest args) (front '()))
        (if (or (null? rest) (null? (cdr rest)))
            (%pprinted obj (append (reverse front) (if (null? rest) '() (car rest))))
            (loop (cdr rest) (cons (car rest) front)))))))
