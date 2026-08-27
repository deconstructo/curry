;;; srfi_170_tests.scm — tests for (srfi 170) additions not already covered
;;; by posix_tests.scm's (curry posix) coverage: owner/unchanged,
;;; group/unchanged, user-info:parsed-full-name.

(import (srfi 170))

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

;;; owner/unchanged, group/unchanged -- both -1, the same sentinel chown(2)
;;; itself treats as "leave this id alone".

(check "owner/unchanged is -1" owner/unchanged -1)
(check "group/unchanged is -1" group/unchanged -1)

;;; user-info:parsed-full-name -- the full-name portion of the GECOS field,
;;; up to the first comma (or the whole string if there is none). Can't
;;; fabricate a comma-containing GECOS field for the real current user, so
;;; this checks it agrees with user-info:full-name whenever the real value
;;; has no comma (the common case), which is still a real behavioral check.

(let* ((me (user-info (user-uid)))
       (full (user-info:full-name me))
       (parsed (user-info:parsed-full-name me)))
  (check "user-info:parsed-full-name returns a string" (string? parsed) #t)
  (if (not (memv #\, (string->list full)))
      (check "user-info:parsed-full-name matches full-name when no comma present"
             parsed full)
      (check "user-info:parsed-full-name is a prefix of full-name when a comma is present"
             (string=? (substring full 0 (string-length parsed)) parsed)
             #t)))

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
