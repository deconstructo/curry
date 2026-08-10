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

(define (check-error name thunk)
  (if (guard (e (#t #t)) (thunk) #f)
      (set! pass (+ pass 1))
      (begin (set! fail (+ fail 1))
             (display "FAIL: ") (display name) (display " did not raise") (newline))))

;;; Regression: sqlite-exec's step loop used to distinguish only
;;; SQLITE_ROW from "anything else", silently treating a genuine
;;; step-time error (e.g. ROLLBACK with no active transaction) exactly
;;; like a normal end-of-results -- returning an empty row list rather
;;; than raising. This mattered in practice for (curry sql)'s own
;;; sql-with-transaction, whose dynamic-wind-based rollback guarantee
;;; depended on sqlite-exec actually raising when a rollback fails.
(check-error "exec-rollback-with-no-active-transaction-raises"
  (lambda () (sqlite-exec db "ROLLBACK")))

;;; Regression: sqlite-bind had no catch-all case -- a boolean #t, a
;;; symbol, a pair, a vector, or a bytevector all fell through every
;;; branch silently, leaving that parameter position bound to NULL
;;; rather than raising. A caller's type mistake (a symbol where a
;;; string was meant, say) would then silently corrupt the bound row.
(let ((stmt (sqlite-prepare db "SELECT 1 WHERE 1 = ?")))
  (check-error "bind-symbol-raises" (lambda () (sqlite-bind stmt 1 'not-a-valid-type)))
  (check-error "bind-boolean-true-raises" (lambda () (sqlite-bind stmt 1 #t)))
  (sqlite-finalize stmt))

(sqlite-close db)

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
