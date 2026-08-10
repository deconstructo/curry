;;; (curry sql) — a Scheme-native cross-database layer over (curry
;;; sqlite) (and, later, mariadb/postgres FFI backends). See
;;; docs/thoughts/sql-abstraction-design.md for the full design
;;; rationale (a critique of PHP's PDO and what a Lisp-native version
;;; of the same idea looks like) — this module is that design, sqlite
;;; being the first backend.
;;;
;;; A caller always writes `?` placeholders and gets rows back as
;;; alists keyed by column-name symbol (matching (curry sqlite)'s own
;;; existing convention exactly), regardless of which backend a given
;;; connection actually is. Two real strategies exist underneath a
;;; backend that has real prepared statements (sqlite, today) binds
;;; `?` placeholders natively; a backend that doesn't (a future
;;; mariadb/postgres FFI driver) would escape each value into a SQL
;;; literal and splice it into the query text instead, using that
;;; database's own connection-aware escaping function — see the design
;;; doc's §5 for why that's a deliberate, named strategy and not a
;;; compromise. Adding a backend later means writing one more
;;; <sql-driver> value (§4); nothing here changes.
;;;
;;; This module makes no changes to (curry sqlite) itself — every
;;; sqlite-specific call lives entirely inside %sqlite-driver, below.

(define-library (curry sql)
  (import (scheme base) (curry sqlite) (curry conditions))
  (export
    sql-connect sql-connection? sql-connection-kind sql-close
    sql-exec sql-query sql-query-one
    sql-begin! sql-commit! sql-rollback! sql-in-transaction? sql-with-transaction
    sql-last-insert-id)
  (begin

;;; =========================================================================
;;; The driver protocol — a record of closures, not a class hierarchy
;;; (see the design doc's §4 for why: three flat peer backends, no
;;; inheritance or multiple dispatch actually needed).
;;; =========================================================================

(define-record-type <sql-driver>
  (make-sql-driver exec-raw affected-rows prepare?
                    stmt-prepare stmt-bind! stmt-step stmt-finalize!
                    escape-literal last-insert-id close)
  sql-driver?
  (exec-raw       driver-exec-raw)       ; (proc raw sql) -> list of row alists
  (affected-rows  driver-affected-rows)  ; (proc raw) -> integer, valid right after exec-raw
  (prepare?       driver-prepare?)       ; #t: use stmt-*; #f: use escape-literal
  (stmt-prepare   driver-stmt-prepare)   ; (proc raw sql) -> stmt handle
  (stmt-bind!     driver-stmt-bind!)     ; (proc stmt 1-based-index value)
  (stmt-step      driver-stmt-step)      ; (proc stmt) -> row alist or #f
  (stmt-finalize! driver-stmt-finalize!) ; (proc stmt)
  (escape-literal driver-escape-literal) ; (proc raw value) -> SQL literal text, incl. quoting
  (last-insert-id driver-last-insert-id) ; (proc raw) -> integer or #f
  (close          driver-close))         ; (proc raw)

(define-record-type <sql-connection>
  (%make-sql-connection kind raw driver in-transaction?-box)
  sql-connection?
  (kind    sql-connection-kind)   ; 'sqlite (today) | 'mariadb | 'postgres (future)
  (raw     sql-connection-raw)
  (driver  sql-connection-driver)
  (in-transaction?-box sql-connection-in-transaction?-box))

;;; =========================================================================
;;; The sqlite driver — see docs/thoughts/sql-abstraction-design.md §4.1:
;;; sqlite-exec already returns rows in exactly this module's row shape,
;;; and sqlite already has no dedicated transaction procedures (its own
;;; docs' own pattern is BEGIN/COMMIT/ROLLBACK as plain SQL through
;;; sqlite-exec) -- both already match this module's own design, chosen
;;; independently, with nothing to translate.
;;; =========================================================================

(define %sqlite-driver
  (make-sql-driver
    sqlite-exec                          ; exec-raw
    sqlite-changes                       ; affected-rows
    #t                                    ; prepare?: sqlite gets true prepared statements
    sqlite-prepare sqlite-bind sqlite-step sqlite-finalize
    #f                                    ; escape-literal: unused, prepare? is #t
    sqlite-last-insert-rowid
    sqlite-close))

;;; =========================================================================
;;; Condition type for recoverable row-level problems -- see the design
;;; doc's §10. This module registers only the root; a driver or a
;;; caller may register any subtype of 'sql-row-error at any time via
;;; (curry conditions)'s own open, global type registry -- see the
;;; design doc for worked examples of both. Nothing here currently
;;; signals 'sql-row-error itself (sqlite's own type coverage --
;;; integer/float/text/blob/NULL -- has no "can't decode this column"
;;; case the way a future encoding-sensitive backend might); the type
;;; is reserved now so a later backend and any caller-side code can
;;; both build on it without (curry sql) itself needing to change.
;;; =========================================================================

(define-condition sql-row-error (error))

;;; =========================================================================
;;; Connecting
;;; =========================================================================

;; (sql-connect 'sqlite path)          -> a file-backed connection
;; (sql-connect 'sqlite ':memory:)     -> an in-memory connection (any of
;;                                        the symbol/string/keyword-ish
;;                                        value ':memory: -- checked by
;;                                        eq?/equal? against the literal
;;                                        symbol, not by a special string)
(define (sql-connect kind config)
  (case kind
    ((sqlite)
     (%make-sql-connection 'sqlite
       (if (eq? config ':memory:) (sqlite-open-memory) (sqlite-open config))
       %sqlite-driver
       (list #f)))
    ((mariadb postgres)
     (error "sql-connect: backend not yet implemented" kind))
    (else (error "sql-connect: unknown backend" kind))))

(define (sql-close conn) ((driver-close (sql-connection-driver conn)) (sql-connection-raw conn)))

;;; =========================================================================
;;; Value -> SQL literal (used only by escape-and-splice backends, i.e.
;;; whenever driver-prepare? is #f -- sqlite never reaches this, since
;;; it always takes the true-prepared-statement path below).
;;; =========================================================================

(define (%literal driver raw value)
  (cond
    ((not value) "NULL")
    ((number? value) (number->string value))
    (else ((driver-escape-literal driver) raw value))))

(define (%splice-sql sql params driver raw)
  (let loop ((i 0) (out (open-output-string)) (ps params))
    (let ((qpos (%find-placeholder sql i)))
      (cond
        ((not qpos)
         (write-string (substring sql i (string-length sql)) out)
         (get-output-string out))
        ((null? ps) (error "sql: not enough parameters for the placeholders in" sql))
        (else
         (write-string (substring sql i qpos) out)
         (write-string (%literal driver raw (car ps)) out)
         (loop (+ qpos 1) out (cdr ps)))))))

;; The byte offset of the next "?" in sql at or after `start`, or #f.
;; Deliberately does not try to skip "?" characters that appear inside
;; a quoted string literal in the SQL text itself -- a caller mixing a
;; literal "?" character into their own query text alongside `?`
;; placeholders is a real, if narrow, gap; see the module doc's Notes.
(define (%find-placeholder sql start)
  (let ((len (string-length sql)))
    (let loop ((i start))
      (cond ((= i len) #f)
            ((char=? (string-ref sql i) #\?) i)
            (else (loop (+ i 1)))))))

;;; =========================================================================
;;; Running a statement
;;; =========================================================================

(define (%run-prepared driver raw sql params)
  (let ((stmt ((driver-stmt-prepare driver) raw sql)))
    (let loop ((i 1) (ps params))
      (unless (null? ps)
        ((driver-stmt-bind! driver) stmt i (car ps))
        (loop (+ i 1) (cdr ps))))
    (let loop ((rows '()))
      (let ((row ((driver-stmt-step driver) stmt)))
        (if row
            (loop (cons row rows))
            (begin ((driver-stmt-finalize! driver) stmt) (reverse rows)))))))

;; Runs `sql` (with `params` already substituted in, if any) and
;; returns its rows (a list of alists; empty for DDL/DML).
(define (%run conn sql params)
  (let ((driver (sql-connection-driver conn)) (raw (sql-connection-raw conn)))
    (cond
      ((null? params) ((driver-exec-raw driver) raw sql))
      ((driver-prepare? driver) (%run-prepared driver raw sql params))
      (else ((driver-exec-raw driver) raw (%splice-sql sql params driver raw))))))

;; (sql-exec conn sql . params) -> affected-row count (DDL/DML).
(define (sql-exec conn sql . params)
  (%run conn sql params)
  ((driver-affected-rows (sql-connection-driver conn)) (sql-connection-raw conn)))

;; (sql-query conn sql . params) -> list of row alists.
(define (sql-query conn sql . params) (%run conn sql params))

;; (sql-query-one conn sql . params) -> first row's alist, or #f.
(define (sql-query-one conn sql . params)
  (let ((rows (apply sql-query conn sql params)))
    (if (pair? rows) (car rows) #f)))

;;; =========================================================================
;;; Transactions -- BEGIN/COMMIT/ROLLBACK as plain SQL text through
;;; exec-raw, the one strategy that needs nothing backend-specific at
;;; all (see the design doc's §6; this is already sqlite's own
;;; documented pattern with no wrapper of its own).
;;; =========================================================================

(define (sql-begin! conn)
  ((driver-exec-raw (sql-connection-driver conn)) (sql-connection-raw conn) "BEGIN")
  (set-car! (sql-connection-in-transaction?-box conn) #t))

(define (sql-commit! conn)
  ((driver-exec-raw (sql-connection-driver conn)) (sql-connection-raw conn) "COMMIT")
  (set-car! (sql-connection-in-transaction?-box conn) #f))

(define (sql-rollback! conn)
  ((driver-exec-raw (sql-connection-driver conn)) (sql-connection-raw conn) "ROLLBACK")
  (set-car! (sql-connection-in-transaction?-box conn) #f))

(define (sql-in-transaction? conn) (car (sql-connection-in-transaction?-box conn)))

;; (sql-with-transaction conn thunk) -- begins a transaction, calls
;; (thunk), commits on a normal return, rolls back and re-raises on any
;; escape (an error, an escaping continuation), via dynamic-wind. The
;; same "acquire, run, always release" shape (curry dot-locking)'s
;; with-dot-lock* already established for a different resource.
(define (sql-with-transaction conn thunk)
  (sql-begin! conn)
  (dynamic-wind
    (lambda () #t)
    (lambda ()
      (let ((result (thunk)))
        (sql-commit! conn)
        result))
    (lambda () (when (sql-in-transaction? conn) (sql-rollback! conn)))))

;;; =========================================================================
;;; Last insert id -- see the design doc's §9. sqlite always has an
;;; honest answer; the `sequence-name` argument exists for a future
;;; PostgreSQL backend and is accepted-but-ignored here.
;;; =========================================================================

(define (sql-last-insert-id conn . sequence-name)
  ((driver-last-insert-id (sql-connection-driver conn)) (sql-connection-raw conn)))

  )) ;; end begin, define-library
