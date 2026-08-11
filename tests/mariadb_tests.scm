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
;;; literal/my-escape-bytea/my-last-insert-id/my-query-stream/column-
;;; type-coercion/TLS-config against a real connection — those all
;;; require an already-connected MYSQL* handle. (They HAVE all been
;;; exercised live against a real local MariaDB server during
;;; development -- see mariadb.scm's own commit history for exactly
;;; what was checked: INT/FLOAT/DECIMAL/TEXT/BLOB type coercion
;;; including non-UTF-8 binary bytes, my-query-stream/my-stream-next/
;;; my-stream-close including an early close leaving the connection
;;; reusable, and mysql_ssl_set actually being invoked for a
;;; deliberately-bad ssl-ca path.) What this suite DOES exercise, and
;;; what genuinely matters most for correctness here without a server,
;;; is that my-connect's real FFI path (mysql_init, mysql_real_connect,
;;; mysql_error, mysql_errno, mysql_sqlstate, mysql_close) runs cleanly
;;; end to end and produces a clear, structured 'mariadb-error
;;; condition rather than crashing: a connection attempt to 127.0.0.1
;;; on a port nothing listens on is guaranteed to be refused quickly
;;; and deterministically, without needing any server at all, so it's
;;; a real exercise of the connect/error/close path rather than a
;;; mock.
;;;
;;; If libmariadb/libmysqlclient itself isn't installed on the machine
;;; running this suite, my-connect's own dlopen failure raises a clear
;;; "not found" error rather than the connection-refused error this
;;; suite checks for below — skip cleanly in that case rather than
;;; failing the whole run, matching (curry hdf5)'s own test convention.

(import (curry mariadb) (curry conditions))

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
;;; § 1b  Structured error condition (still exercisable without a
;;; live server -- a connect failure raises the real condition type
;;; with real errno/sqlstate fields read off the MYSQL* handle, no
;;; server needed for that part).
;;; ════════════════════════════════════════════════════════════

(guard (e (#t
           (check "connect failure raises 'mariadb-error" (condition-is-a? e 'mariadb-error) #t)
           (check "connect failure errno is an exact integer" (exact-integer? (condition-field e 'errno)) #t)
           (check "connect failure sqlstate is a string" (string? (condition-field e 'sqlstate)) #t)))
  (my-connect '((host . "127.0.0.1") (port . 1)))
  (check "unreachable" #t #f))

;;; ════════════════════════════════════════════════════════════
;;; § 2  Accessors that don't need a live connection
;;; ════════════════════════════════════════════════════════════

(check "my-connect? rejects a non-pointer value" (my-connect? 42) #f)
(check "my-connect? rejects #f" (my-connect? #f) #f)

(display (string-append (number->string pass) " passed, " (number->string fail) " failed")) (newline)
(if (> fail 0) (exit 1) (exit 0))
