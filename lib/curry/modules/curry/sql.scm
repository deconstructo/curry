;;; (curry sql) — a Scheme-native cross-database layer over (curry
;;; sqlite), (curry mariadb), and (curry postgres). See
;;; docs/thoughts/sql-abstraction-design.md for the full design
;;; rationale (a critique of PHP's PDO and what a Lisp-native version
;;; of the same idea looks like) — this module is that design.
;;;
;;; A caller always writes `?` placeholders and gets rows back as
;;; alists keyed by column-name symbol (matching (curry sqlite)'s own
;;; existing convention exactly), regardless of which backend a given
;;; connection actually is. Two real strategies exist underneath:
;;; sqlite (the only backend with real prepared statements here) binds
;;; `?` placeholders natively; mariadb and postgres escape each value
;;; into a SQL literal and splice it into the query text instead, using
;;; that database's own connection-aware escaping function — see the
;;; design doc's §5 for why that's a deliberate, named strategy and not
;;; a compromise. Adding a further backend later means writing one more
;;; <sql-driver> value (§4); nothing else here changes.
;;;
;;; This module makes no changes to (curry sqlite)/(curry mariadb)/
;;; (curry postgres) themselves — every backend-specific call lives
;;; entirely inside this module's own three driver records, below.
;;;
;;; (curry mariadb)'s and (curry postgres)'s own query-execution paths
;;; have both been exercised live against real local servers (see their
;;; own module header comments); sql-connect's 'mariadb/'postgres cases
;;; wire directly to my-connect/pg-connect and my-exec/pg-exec with no
;;; translation of their own, so nothing here changes that. The
;;; automated tests/mariadb_tests.scm/tests/postgres_tests.scm suites
;;; still only cover what's reachable without a server present at
;;; test-run time — see those files' own headers.

(define-library (curry sql)
  (import (scheme base) (curry sqlite) (curry mariadb) (curry postgres) (curry conditions))
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

;; exec-raw's contract is (proc raw sql) -> (values rows affected-rows),
;; not just a row list -- this is the one place the three backends
;; genuinely can't share a simpler shape. sqlite's own affected-row
;; count (sqlite-changes) is connection-level state, queryable any time
;; after an exec; but PostgreSQL's is a property of the specific
;; PGresult a query produced (PQcmdTuples), which pg-exec has already
;; cleared by the time it returns a plain row list -- so both values
;; have to come back together, from the one call that still has the
;; result object alive, rather than as two independently-callable
;; driver procedures.
;; The prepared-statement path (stmt-prepare/bind!/step/finalize!, used
;; only when prepare? is #t -- sqlite, today) has its own separate
;; stmt-affected-rows accessor rather than reusing exec-raw's (values
;; rows affected) contract: running a parameterized statement never
;; goes through exec-raw at all, and sqlite's own affected-row count
;; (sqlite-changes) is plain connection-level state, safe to read any
;; time after stmt-finalize! runs -- there's no result object it needs
;; to come bundled with the way PostgreSQL's PQcmdTuples does.
(define-record-type <sql-driver>
  (make-sql-driver exec-raw prepare?
                    stmt-prepare stmt-bind! stmt-step stmt-finalize! stmt-affected-rows
                    escape-literal last-insert-id close)
  sql-driver?
  (exec-raw          driver-exec-raw)          ; (proc raw sql) -> (values row-alist-list affected-row-count)
  (prepare?           driver-prepare?)          ; #t: use stmt-*; #f: use escape-literal
  (stmt-prepare       driver-stmt-prepare)      ; (proc raw sql) -> stmt handle
  (stmt-bind!         driver-stmt-bind!)        ; (proc stmt 1-based-index value)
  (stmt-step          driver-stmt-step)         ; (proc stmt) -> row alist or #f
  (stmt-finalize!     driver-stmt-finalize!)    ; (proc stmt)
  (stmt-affected-rows driver-stmt-affected-rows) ; (proc raw) -> integer, valid right after stmt-finalize!
  (escape-literal     driver-escape-literal)    ; (proc raw value) -> SQL literal text, incl. quoting
  (last-insert-id     driver-last-insert-id)    ; (proc raw . sequence-name) -> integer
  (close              driver-close))            ; (proc raw)

(define-record-type <sql-connection>
  (%make-sql-connection kind raw driver in-transaction?-box)
  sql-connection?
  (kind    sql-connection-kind)   ; 'sqlite | 'mariadb | 'postgres
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

;; Wraps sqlite-exec (which just returns a row list) to match every
;; driver's own shared exec-raw contract of (values rows affected).
;; sqlite-changes is connection-level state, safe to read immediately
;; after sqlite-exec returns.
(define (%sqlite-exec-raw raw sql) (values (sqlite-exec raw sql) (sqlite-changes raw)))

;; sqlite-last-insert-rowid never needs a sequence name (see the design
;; doc's §9), but every driver's last-insert-id closure is called as
;; (proc raw . sequence-name) -- this wrapper ignores any extra
;; argument rather than erroring on arity.
(define (%sqlite-last-insert-id raw . sequence-name) (sqlite-last-insert-rowid raw))

(define %sqlite-driver
  (make-sql-driver
    %sqlite-exec-raw                     ; exec-raw
    #t                                    ; prepare?: sqlite gets true prepared statements
    sqlite-prepare sqlite-bind sqlite-step sqlite-finalize
    sqlite-changes                       ; stmt-affected-rows
    #f                                    ; escape-literal: unused, prepare? is #t
    %sqlite-last-insert-id
    sqlite-close))

;;; =========================================================================
;;; The mariadb and postgres drivers -- both use the escape-and-splice
;;; strategy (prepare? #f; see the design doc's §5). escape-literal's
;;; own single field has to cover TWO different underlying escape
;;; functions per backend (a string goes through my-escape-literal/
;;; pg-escape-literal, a bytevector through my-escape-bytea/
;;; pg-escape-bytea -- each database has a different, incompatible text
;;; representation for binary data), so each driver's own escape-
;;; literal closure dispatches on the value's own type before calling
;;; through to the right one.
;;; =========================================================================

(define (%mariadb-escape-value raw value)
  (if (bytevector? value) (my-escape-bytea raw value) (my-escape-literal raw value)))

;; my-last-insert-id never needs a sequence name either (see the design
;; doc's §9) -- same arity-ignoring wrapper as %sqlite-last-insert-id.
(define (%mariadb-last-insert-id raw . sequence-name) (my-last-insert-id raw))

(define %mariadb-driver
  (make-sql-driver
    my-exec                              ; exec-raw: already (values rows affected)
    #f                                    ; prepare?: escape-and-splice
    #f #f #f #f #f                        ; stmt-*: unused, prepare? is #f
    %mariadb-escape-value
    %mariadb-last-insert-id
    my-close))

(define (%postgres-escape-value raw value)
  (if (bytevector? value) (pg-escape-bytea raw value) (pg-escape-literal raw value)))

(define %postgres-driver
  (make-sql-driver
    pg-exec                              ; exec-raw: already (values rows affected)
    #f                                    ; prepare?: escape-and-splice
    #f #f #f #f #f                        ; stmt-*: unused, prepare? is #f
    %postgres-escape-value
    pg-last-insert-id
    pg-close))

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
    ((mariadb)
     (%make-sql-connection 'mariadb (my-connect config) %mariadb-driver (list #f)))
    ((postgres)
     (%make-sql-connection 'postgres (pg-connect config) %postgres-driver (list #f)))
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

;; -> (values rows affected) -- stmt-affected-rows is queried right
;; after stmt-finalize!, matching sqlite-changes' own "connection-level
;; state, safe to read any time after execution" character (the only
;; backend that reaches this path today).
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
            (begin
              ((driver-stmt-finalize! driver) stmt)
              (values (reverse rows) ((driver-stmt-affected-rows driver) raw))))))))

;; Runs `sql` (with `params` substituted in via whichever strategy this
;; connection's driver uses) and returns (values rows affected) --
;; rows is a list of alists (empty for DDL/DML), affected is an integer
;; (meaningless, and typically 0, for a SELECT).
(define (%run conn sql params)
  (let ((driver (sql-connection-driver conn)) (raw (sql-connection-raw conn)))
    (cond
      ((null? params) ((driver-exec-raw driver) raw sql))
      ((driver-prepare? driver) (%run-prepared driver raw sql params))
      (else ((driver-exec-raw driver) raw (%splice-sql sql params driver raw))))))

;; (sql-exec conn sql . params) -> affected-row count (DDL/DML).
(define (sql-exec conn sql . params)
  (let-values (((rows affected) (%run conn sql params))) affected))

;; (sql-query conn sql . params) -> list of row alists.
(define (sql-query conn sql . params)
  (let-values (((rows affected) (%run conn sql params))) rows))

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
;;; Last insert id -- see the design doc's §9. sqlite and mariadb always
;;; have an honest answer with no sequence name needed; postgres has no
;;; connection-independent answer at all and genuinely uses
;;; `sequence-name` when one is given (pg-last-insert-id's own
;;; currval() path) -- every driver's last-insert-id closure takes
;;; (proc raw . sequence-name) so this call site doesn't need to know
;;; which backend it's talking to.
;;; =========================================================================

(define (sql-last-insert-id conn . sequence-name)
  (apply (driver-last-insert-id (sql-connection-driver conn)) (sql-connection-raw conn) sequence-name))

  )) ;; end begin, define-library
