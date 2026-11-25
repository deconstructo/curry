;;; #428: a continuation escape out of (load ...) restores the load directory
;;; context.  Before, the stale entry made the next relative load resolve
;;; against sub/ instead of this directory.
(import (scheme base) (scheme write) (scheme load))

(define esc #f)
(call/cc (lambda (k) (set! esc k) (load "sub/escaper.scm")))
(load "ok.scm")

(if (equal? ok-value 7)
    (begin (display "PASS: relative load after an escape through load") (newline) (exit 0))
    (begin (display "FAIL: got ") (write ok-value) (newline) (exit 1)))
