;;; sqlite_tests.scm — regression tests for (curry sqlite)

(import (curry sqlite))

(define pass 0)
(define fail 0)

(define (check name got expect)
  (if (equal? got expect)
      (set! pass (+ pass 1))
      (begin
        (set! fail (+ fail 1))
        (display "FAIL: ") (display name)
        (display " got=") (write got)
        (display " expect=") (write expect)
        (newline))))

(define db (sqlite-open-memory))
(sqlite-exec db "CREATE TABLE t (k TEXT, v TEXT)")

(check "exec-empty" (sqlite-exec db "SELECT * FROM t") '())

(sqlite-exec db "INSERT INTO t (k, v) VALUES ('a', 'alpha')")
(check "select-basic"
       (cdr (assq 'v (car (sqlite-exec db "SELECT * FROM t WHERE k = 'a'"))))
       "alpha")

;;; embedded NUL byte: strings are length-prefixed, not NUL-terminated at the
;;; language level — sqlite3_bind_text/sqlite3_column_text are strlen-based
;;; C APIs and must be given explicit lengths or they silently truncate.
(let ((nul-str (string #\a #\b (integer->char 0) #\c #\d)))
  (let ((stmt (sqlite-prepare db "INSERT INTO t (k, v) VALUES (?, ?)")))
    (sqlite-bind stmt 1 "nulkey")
    (sqlite-bind stmt 2 nul-str)
    (sqlite-step stmt)
    (sqlite-finalize stmt))
  (let* ((rows (sqlite-exec db "SELECT v FROM t WHERE k = 'nulkey'"))
         (got  (cdr (assq 'v (car rows)))))
    (check "nul-byte-length" (string-length got) (string-length nul-str))
    (check "nul-byte-roundtrip" got nul-str)))

(sqlite-close db)

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
