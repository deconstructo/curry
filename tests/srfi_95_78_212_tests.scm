;;; srfi_95_78_212_tests.scm — SRFI-95 (Sorting and Merging), SRFI-78
;;; (Lightweight Testing), SRFI-212 (Aliases). Each SRFI is exercised via
;;; all three library paths: the real (srfi sN name) implementation, the
;;; bare-numbered (srfi N) shim, and the dashed (srfi srfi-N) shim.

(import (scheme base)
        (srfi s95 sorting) (srfi 95) (srfi srfi-95)
        (srfi s78 lightweight-testing) (srfi 78) (srfi srfi-78)
        (srfi s212 aliases) (srfi 212) (srfi srfi-212))

(define pass 0)
(define fail 0)

(define (check-result label result expected)
  (if (equal? result expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (display " expected ") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

;;; SRFI-95: sorted?, sort, sort!, merge, merge!

(check-result "srfi-95 sort: list" (sort (list 3 1 2) <) (list 1 2 3))
(check-result "srfi-95 sort: vector" (sort (vector 3 1 2) <) (vector 1 2 3))
(check-result "srfi-95 sort: optional key"
              (sort (list "bb" "a" "ccc") < string-length)
              (list "a" "bb" "ccc"))
(check-result "srfi-95 sorted?" (sorted? (list 1 2 3) <) #t)
(check-result "srfi-95 sorted? false case" (sorted? (list 3 2 1) <) #f)
(check-result "srfi-95 merge" (merge (list 1 3 5) (list 2 4 6) <) (list 1 2 3 4 5 6))
(check-result "srfi-95 merge!" (merge! (list 1 3 5) (list 2 4 6) <) (list 1 2 3 4 5 6))
(check-result "srfi-95 sort!: vector" (sort! (vector 5 3 1 4) <) (vector 1 3 4 5))

;;; SRFI-78: check, check-set-mode!, check-reset!, check-passed?, check-report

;; check-reset! restores the default 'report mode (per spec, it resets
;; to "the state immediately after loading the module"). 'summary mode
;; (not 'off -- 'off means the checks are literally never evaluated or
;; counted at all, per the reference implementation) still counts every
;; check while suppressing per-check inline printing.
(check-reset!)
(check-set-mode! 'summary)
(check (+ 1 1) => 2)
(check (+ 1 1) => 3)
(check (+ 1 1) (=> =) 2.0)
(check-set-mode! 'report)

(check-result "srfi-78 check: records passes and failures correctly"
              (check-passed? 3) #f) ; one of the three checks above failed
(check-reset!)
(check (+ 1 1) => 2)
(check (* 2 3) => 6)
(check-result "srfi-78 check-passed?: all-pass case" (check-passed? 2) #t)
(check-reset!)

;; check-report: 'summary mode must print only the pass/fail counts, never
;; the failure detail -- matching the reference implementation exactly
;; (curry's first cut at this got it wrong: it printed the failure detail
;; in every mode with a recorded failure, 'summary included).
(check-reset!)
(check-set-mode! 'summary)
(check (+ 1 1) => 3)
(define %s78-summary-report (with-output-to-string (lambda () (check-report))))
(check-result "srfi-78 check-report: 'summary mode omits failure detail"
              (string-contains %s78-summary-report "first failure") #f)
(check-result "srfi-78 check-report: 'summary mode still shows the counts"
              (number? (string-contains %s78-summary-report "0 out of 1")) #t)

(check-reset!)
(check-set-mode! 'report-failed)
(check (+ 1 1) => 3)
(define %s78-failed-report (with-output-to-string (lambda () (check-report))))
(check-result "srfi-78 check-report: 'report-failed mode includes failure detail"
              (number? (string-contains %s78-failed-report "first failure")) #t)
(check-set-mode! 'report)
(check-reset!)

;;; SRFI-212: define-alias

(define (%s212-original x) (* x 10))
(define-alias %s212-alias %s212-original)
(check-result "srfi-212 define-alias: same behavior as original" (%s212-alias 4) 40)
(check-result "srfi-212 define-alias: usable as a first-class value"
              (map %s212-alias (list 1 2 3)) (list 10 20 30))

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
