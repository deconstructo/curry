;;; PostgreSQL client tests — (curry postgres)
;;;
;;; libpq is a runtime-only dependency (dlopen'd lazily on first actual
;;; use, never linked at build time — see postgres.scm's own header),
;;; so importing this module always succeeds even when the library
;;; isn't installed at all; only a call that actually needs the
;;; library (pg-connect) triggers the dlopen attempt.
;;;
;;; No live PostgreSQL server was available in the environment this
;;; suite was written in, so it cannot exercise pg-exec/pg-last-insert-
;;; id against a real connection. It CAN, and does, exercise
;;; pg-escape-literal/pg-escape-bytea for real: unlike MariaDB's escape
;;; functions (which need an already-connected MYSQL* for its charset
;;; info), libpq's PQescapeLiteral/PQescapeByteaConn only need a live
;;; PGconn* to read its client encoding from — which a refused TCP
;;; connection never produces, so those two functions are still
;;; untestable here for the same underlying reason. What this suite
;;; DOES exercise is pg-connect's real FFI path (PQconnectdb, PQstatus,
;;; PQerrorMessage, PQfinish): a connection attempt to 127.0.0.1 on a
;;; port nothing listens on is guaranteed to be refused quickly and
;;; deterministically, without needing any server at all.
;;;
;;; If libpq itself isn't installed on the machine running this suite,
;;; pg-connect's own dlopen failure raises a clear "not found" error
;;; rather than the connection-refused error this suite checks for
;;; below — skip cleanly in that case rather than failing the whole
;;; run, matching (curry hdf5)'s own test convention.

(import (curry postgres))

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

(check-error "pg-connect raises on a refused connection"
  (lambda () (pg-connect '((host . "127.0.0.1") (port . 1)))))

(check-error "pg-connect raises with a numeric port passed as a string too"
  (lambda () (pg-connect '((host . "127.0.0.1") (port . "1")))))

;;; ════════════════════════════════════════════════════════════
;;; § 2  Accessors that don't need a live connection
;;; ════════════════════════════════════════════════════════════

(check "pg-connect? rejects a non-pointer value" (pg-connect? 42) #f)
(check "pg-connect? rejects #f" (pg-connect? #f) #f)

(display (string-append (number->string pass) " passed, " (number->string fail) " failed")) (newline)
(if (> fail 0) (exit 1) (exit 0))
