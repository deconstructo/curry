;;; (srfi s98 os-environment-variables), (srfi s59 vicinity), (srfi s194 random-data-samples)

(import (srfi s98 os-environment-variables) (srfi s59 vicinity) (srfi s194 random-data-samples))

(define pass 0)
(define fail 0)

(define (check label result expected)
  (if (equal? result expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (display " expected ") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

;;; s98

(check "get-environment-variables returns a list" (list? (get-environment-variables)) #t)

;;; s59

(check "pathname->vicinity strips the filename" (pathname->vicinity "/a/b/c.scm") "/a/b/")
(check "make-vicinity adds a trailing slash" (make-vicinity "/a/b") "/a/b/")
(check "make-vicinity is idempotent" (make-vicinity "/a/b/") "/a/b/")
(check "in-vicinity concatenates" (in-vicinity "/a/b/" "c.scm") "/a/b/c.scm")
(check "sub-vicinity appends and re-normalizes" (sub-vicinity "/a/b/" "c") "/a/b/c/")
(check "vicinity:suffix? recognizes /" (vicinity:suffix? #\/) #t)
(check "vicinity:suffix? rejects other chars" (vicinity:suffix? #\a) #f)

;;; s194

(define ig (make-random-integer-generator 0 10))
(let ((v (ig))) (check "random-integer-generator stays in range" (and (>= v 0) (< v 10)) #t))

(define rg (make-random-real-generator 0.0 1.0))
(let ((v (rg))) (check "random-real-generator stays in range" (and (>= v 0.0) (< v 1.0)) #t))

(define bg (make-random-boolean-generator 1.0))
(check "random-boolean-generator with p=1.0 always true" (bg) #t)

(define bg0 (make-random-boolean-generator 0.0))
(check "random-boolean-generator with p=0.0 always false" (bg0) #f)

(define cg (make-random-char-generator "x"))
(check "random-char-generator draws only from the given string" (cg) #\x)

(define eg (make-exponential-generator 1.0))
(check "exponential-generator is non-negative" (>= (eg) 0.0) #t)

(define bin (make-binomial-generator 10 1.0))
(check "binomial-generator with p=1.0 always succeeds n times" (bin) 10)

(define geo (make-geometric-generator 1.0))
(check "geometric-generator with p=1.0 always takes one trial" (geo) 1)

(define catg (make-categorical-generator (list (cons 'only 1))))
(check "categorical-generator with a single option" (catg) 'only)

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
