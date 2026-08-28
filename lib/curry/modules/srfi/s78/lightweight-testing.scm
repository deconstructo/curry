;;; SRFI-78: Lightweight testing.
;;;
;;; Follows the reference implementation's actual semantics precisely
;;; (fetched from https://srfi.schemers.org/srfi-78/check.scm), not just
;;; the prose spec, since the two differ in a way that matters: 'off mode
;;; means `expr` is never evaluated at all (wrapped in a thunk that's
;;; simply never called), not merely "evaluated but uncounted/unprinted";
;;; and 'report mode prints EVERY check (pass or fail: "expr => actual ;
;;; correct" or "*** failed ***"), not only failures -- only 'report-failed
;;; restricts inline printing to failures, and 'summary prints nothing
;;; inline at all (only check-report's own summary line).
;;;
;;; `check-ec` (the SRFI-42 eager-comprehension variant) is deliberately
;;; not implemented -- curry has no SRFI-42, and check-ec is purely a
;;; convenience layer over repeated `check` calls, not new checking
;;; logic.
(define-library (srfi s78 lightweight-testing)
  (import (scheme base) (scheme write))
  (export check check-set-mode! check-reset! check-passed? check-report
          %check-proc) ; not part of the public API -- exported only
                       ; because the `check` macro's expansion references
                       ; it, and curry's syntax-rules isn't hygienic
                       ; across define-library boundaries (see
                       ; writing-a-module.md)
  (begin

    (define %check-mode 'report)
    (define %check-correct 0)
    (define %check-failed 0)
    (define %check-first-failure #f) ; #f, or (list quoted-expr expected actual)

    (define (check-set-mode! mode) (set! %check-mode mode))

    (define (check-reset!)
      (set! %check-mode 'report)
      (set! %check-correct 0)
      (set! %check-failed 0)
      (set! %check-first-failure #f))

    (define (check-passed? expected-total-count)
      (and (= %check-failed 0) (= (+ %check-correct %check-failed) expected-total-count)))

    (define (%report-line expr-datum actual pass? expected)
      (write expr-datum) (display " => ") (write actual)
      (if pass?
          (begin (display " ; correct") (newline))
          (begin
            (display " ; *** failed ***") (newline)
            (display "; expected result: ") (write expected) (newline))))

    ;; `thunk` is only ever called here, inside the (not (eq? ... 'off))
    ;; guard -- in 'off mode `expr` is genuinely never evaluated, matching
    ;; the reference implementation exactly (its own check macro doesn't
    ;; even call this procedure at all when mode is off, an equivalent
    ;; short-circuit to gating the call here).
    (define (%check-proc expr-datum thunk equal expected)
      (if (not (eq? %check-mode 'off))
          (let* ((actual (thunk))
                 (pass? (equal actual expected)))
            (if pass?
                (set! %check-correct (+ %check-correct 1))
                (begin
                  (set! %check-failed (+ %check-failed 1))
                  (if (not %check-first-failure)
                      (set! %check-first-failure (list expr-datum expected actual)))))
            (cond
              ((eq? %check-mode 'report) (%report-line expr-datum actual pass? expected))
              ((and (eq? %check-mode 'report-failed) (not pass?))
               (%report-line expr-datum actual pass? expected))
              (else #f))))) ; 'summary: counted, nothing printed inline

    (define (check-report)
      (if (not (eq? %check-mode 'off))
          (begin
            ;; The reference implementation only reprints the first-failure
            ;; detail in 'report-failed/'report mode -- 'summary's whole
            ;; point is to print nothing but the final counts, so a failure
            ;; that happened silently during 'summary must stay silent here
            ;; too, not leak out through check-report.
            (if (and %check-first-failure
                     (or (eq? %check-mode 'report) (eq? %check-mode 'report-failed)))
                (begin
                  (display "; *** first failure ***") (newline)
                  (%report-line (car %check-first-failure)
                                (caddr %check-first-failure)
                                #f
                                (cadr %check-first-failure))))
            (display "; ") (display %check-correct) (display " out of ")
            (display (+ %check-correct %check-failed)) (display " checks passed") (newline))))

    (define-syntax check
      (syntax-rules (=>)
        ((_ expr => expected)
         (check expr (=> equal?) expected))
        ((_ expr (=> equal) expected)
         (%check-proc 'expr (lambda () expr) equal expected))))))
