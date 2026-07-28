;;; Codesets module tests (SRFI-238) — requires (curry codesets)

(import (curry codesets))

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

;;; codeset?

(check "errno is a known codeset" (codeset? 'errno) #t)
(check "signal is a known codeset" (codeset? 'signal) #t)
(check "http-status is a known codeset" (codeset? 'http-status) #t)
(check "unknown symbol is not a codeset" (codeset? 'bogus) #f)
(check "a non-symbol is not a codeset" (codeset? 42) #f)

;;; errno

(check "codeset-symbol errno by number" (codeset-symbol 'errno 2) 'ENOENT)
(check "codeset-number errno by symbol" (codeset-number 'errno 'ENOENT) 2)
(check "codeset-symbol passthrough for an already-valid symbol"
       (codeset-symbol 'errno 'ENOENT) 'ENOENT)
(check "codeset-number passthrough for an already-valid number"
       (codeset-number 'errno 2) 2)
(check "codeset-message errno" (codeset-message 'errno 'ENOENT) "No such file or directory")
(check "codeset-symbol errno unknown number" (codeset-symbol 'errno 999999) #f)
(check "codeset-number errno unknown symbol" (codeset-number 'errno 'ENOTAREALCODE) #f)
(check "codeset-message errno unknown" (codeset-message 'errno 999999) #f)
(check "codeset-symbols errno is non-empty" (> (length (codeset-symbols 'errno)) 0) #t)
(check "codeset-symbols errno contains ENOENT"
       (and (member 'ENOENT (codeset-symbols 'errno)) #t) #t)

;;; signal

(check "codeset-symbol signal SIGKILL" (codeset-symbol 'signal 9) 'SIGKILL)
(check "codeset-number signal SIGSEGV" (integer? (codeset-number 'signal 'SIGSEGV)) #t)
(check "codeset-message signal is a string"
       (string? (codeset-message 'signal 'SIGTERM)) #t)

;;; http-status

(check "codeset-symbol http-status 404" (codeset-symbol 'http-status 404) 'not-found)
(check "codeset-number http-status not-found" (codeset-number 'http-status 'not-found) 404)
(check "codeset-message http-status 200" (codeset-message 'http-status 200) "OK")
(check "codeset-message http-status 500" (codeset-message 'http-status 500) "Internal Server Error")
(check "codeset-symbol http-status unknown code" (codeset-symbol 'http-status 999) #f)
(check "codeset-symbols http-status is a reasonable size"
       (> (length (codeset-symbols 'http-status)) 30) #t)

;;; Unknown codeset raises an error rather than returning garbage

(check "codeset-symbols on unknown codeset raises"
       (guard (e (#t 'caught)) (codeset-symbols 'bogus))
       'caught)

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
