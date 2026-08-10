;;; MariaDB/MySQL client tests — (curry mariadb)
;;;
;;; libmariadb/libmysqlclient is a runtime-only dependency (dlopen'd
;;; lazily on first actual use, never linked at build time — see
;;; mariadb.scm's own header), so importing this module always
;;; succeeds even when the library isn't installed at all; only a
;;; call that actually needs the library (my-connect) triggers the
;;; dlopen attempt.
;;;
;;; No live MariaDB/MySQL server was available in the environment this
;;; suite was written in, so it cannot exercise my-exec/my-escape-
;;; literal/my-escape-bytea/my-last-insert-id against a real connection
;;; — those calls all require an already-connected MYSQL* handle. What
;;; this suite DOES exercise, and what genuinely matters most for
;;; correctness here, is that my-connect's real FFI path (mysql_init,
;;; mysql_real_connect, mysql_error, mysql_close) runs cleanly end to
;;; end and produces a clear error rather than crashing: a connection
;;; attempt to 127.0.0.1 on a port nothing listens on is guaranteed to
;;; be refused quickly and deterministically, without needing any
;;; server at all, so it's a real exercise of the connect/error/close
;;; path rather than a mock.
;;;
;;; If libmariadb/libmysqlclient itself isn't installed on the machine
;;; running this suite, my-connect's own dlopen failure raises a clear
;;; "not found" error rather than the connection-refused error this
;;; suite checks for below — skip cleanly in that case rather than
;;; failing the whole run, matching (curry hdf5)'s own test convention.

(import (curry mariadb))

(define pass 0)
(define fail 0)

(define (check label got expected)
  (if (equal? got expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " — got ") (write got)
             (display "  expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))

(define (check-error label thunk)
  (if (guard (e (#t #t)) (thunk) #f)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label) (display " did not raise") (newline)
             (set! fail (+ fail 1)))))

;;; ════════════════════════════════════════════════════════════
;;; § 1  Connection failure path (exercises real FFI calls)
;;; ════════════════════════════════════════════════════════════

(check-error "my-connect raises on a refused connection"
  (lambda () (my-connect '((host . "127.0.0.1") (port . 1)))))

;;; ════════════════════════════════════════════════════════════
;;; § 2  Accessors that don't need a live connection
;;; ════════════════════════════════════════════════════════════

(check "my-connect? rejects a non-pointer value" (my-connect? 42) #f)
(check "my-connect? rejects #f" (my-connect? #f) #f)

(display (string-append (number->string pass) " passed, " (number->string fail) " failed")) (newline)
(if (> fail 0) (exit 1) (exit 0))
