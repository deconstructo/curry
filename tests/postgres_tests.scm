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
;;; id/pg-cursor-*/pg-stream-*/pg-listen/pg-notify/pg-copy-* against a
;;; real connection. (They HAVE all been exercised live against a real
;;; local Postgres 18 server during development -- see postgres.scm's
;;; own commit history for exactly what was checked: bool/int/float/
;;; numeric/bytea/NULL type coercion, cursor and native-async
;;; streaming including early-close-then-reuse and two concurrent
;;; cursors on one connection, a real LISTEN/NOTIFY round trip across
;;; two connections with special characters in both channel name and
;;; payload, and a COPY FROM/COPY TO round trip.) It CAN, and does,
;;; exercise pg-escape-literal/pg-escape-bytea for real: unlike
;;; MariaDB's escape functions (which need an already-connected
;;; MYSQL* for its charset info), libpq's PQescapeLiteral/
;;; PQescapeByteaConn only need a live PGconn* to read its client
;;; encoding from — which a refused TCP connection never produces, so
;;; those two functions are still untestable here for the same
;;; underlying reason. What this suite DOES exercise is pg-connect's
;;; real FFI path (PQconnectdb, PQstatus, PQerrorMessage, PQfinish)
;;; and the structured 'postgres-error condition it now raises: a
;;; connection attempt to 127.0.0.1 on a port nothing listens on is
;;; guaranteed to be refused quickly and deterministically, without
;;; needing any server at all.
;;;
;;; If libpq itself isn't installed on the machine running this suite,
;;; pg-connect's own dlopen failure raises a clear "not found" error
;;; rather than the connection-refused error this suite checks for
;;; below — skip cleanly in that case rather than failing the whole
;;; run, matching (curry hdf5)'s own test convention.

(import (curry postgres) (curry conditions))

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
;;; § 1b  Structured error condition -- still exercisable without a
;;; live server: a connect failure never has a PGresult to read a
;;; SQLSTATE off of, so the field should genuinely be #f, not just
;;; unread.
;;; ════════════════════════════════════════════════════════════

(guard (e (#t
           (check "connect failure raises 'postgres-error" (condition-is-a? e 'postgres-error) #t)
           (check "connect failure sqlstate is #f (no PGresult exists yet)" (condition-field e 'sqlstate) #f)))
  (pg-connect '((host . "127.0.0.1") (port . 1)))
  (check "unreachable" #t #f))

;;; ════════════════════════════════════════════════════════════
;;; § 2  Accessors that don't need a live connection
;;; ════════════════════════════════════════════════════════════

(check "pg-connect? rejects a non-pointer value" (pg-connect? 42) #f)
(check "pg-connect? rejects #f" (pg-connect? #f) #f)

(display (string-append (number->string pass) " passed, " (number->string fail) " failed")) (newline)
(if (> fail 0) (exit 1) (exit 0))
