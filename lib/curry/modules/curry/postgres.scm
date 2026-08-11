;;; (curry postgres) — PostgreSQL client, via (curry ffi). libpq is
;;; dlopen'd lazily at runtime (not linked at build time — no CMake
;;; flag beyond the general BUILD_FFI=ON), the same pattern (curry
;;; hdf5)/(curry ncurses)/(curry graphviz)/(curry zeromq) use. Install:
;;; `brew install postgresql` (macOS, ships libpq), `apt install
;;; libpq-dev` (Debian/Ubuntu), `dnf install libpq-devel` (Fedora/RHEL).
;;;
;;; This module is written to satisfy (curry sql)'s <sql-driver>
;;; protocol (see docs/thoughts/sql-abstraction-design.md and
;;; lib/curry/modules/curry/sql.scm) — its own procedures below are
;;; plain, independently usable Postgres bindings, but their real
;;; purpose is being wired into sql-connect's 'postgres case.
;;;
;;; Unlike MySQL's C API (see (curry mariadb)'s own header comment),
;;; libpq's result-metadata accessors (PQfname, PQgetvalue, PQgetisnull)
;;; are ordinary string/int-returning functions with no struct layout
;;; to rely on at all — this module never has to peek a raw C struct's
;;; field offsets the way the MariaDB driver does. PQexec's results are
;;; always in TEXT format (the default) here, meaning every value comes
;;; back as an ordinary NUL-free C string — Postgres text values cannot
;;; contain an embedded NUL byte at all, so there is no truncation risk
;;; the way there would be for arbitrary binary data.
;;;
;;; No parameterized query protocol (PQexecParams's char* const* array
;;; arguments) is used here — see (curry sql)'s own design doc §5 for
;;; why: building a native C array of pointers is real, avoidable FFI
;;; complexity this module's escape-and-splice strategy (via
;;; PQescapeLiteral/PQescapeByteaConn, both ordinary single-string-in,
;;; single-string-out calls) doesn't need at all.
;;;
;;; NOTE: pg-connect/pg-exec/pg-escape-literal/pg-escape-bytea have all
;;; been exercised live against a real local PostgreSQL server (a
;;; SELECT round-trip plus both escape functions on real values).
;;; tests/postgres_tests.scm's own automated suite is still limited to
;;; what's reachable without a server present at test-run time
;;; (connection-failure paths only) — see that file's own header for
;;; the reasoning.

(define-library (curry postgres)
  (import (scheme base) (curry ffi) (curry conditions))
  (export
    pg-connect pg-connect? pg-close
    pg-exec pg-escape-literal pg-escape-bytea
    pg-last-insert-id pg-error
    pg-cursor-open pg-cursor-fetch pg-cursor-close pg-cursor?
    pg-stream-open pg-stream-next pg-stream-close pg-async-stream?
    pg-listen pg-unlisten pg-notify pg-notifications
    pg-copy-from-start pg-copy-data pg-copy-end pg-copy-to-start pg-copy-fetch)
  (begin

;;; ── Library discovery ────────────────────────────────────────────────────────

;; Homebrew's dedicated "postgresql@NN" versioned formulas (common when
;; a user has a specific major version installed rather than the
;; unversioned "postgresql" alias) are keg-only and install under their
;; own "lib/postgresql@NN/" directory, never symlinked into the general
;; "lib/" directory the way the unversioned formula's own libpq is --
;; confirmed the hard way (a plain "brew install postgresql@18" puts
;; libpq.dylib at ".../lib/postgresql@18/libpq.dylib", found by none of
;; the other candidates below). %pq-versioned-candidates covers a
;; handful of recent major versions on both Homebrew prefixes; anyone
;; on a version not listed here can still make the bare "libpq.dylib"/
;; "libpq.so.5" attempt succeed by adding their own lib directory to
;; DYLD_LIBRARY_PATH/LD_LIBRARY_PATH.
(define %pq-versioned-candidates
  (apply append
    (map (lambda (v)
           (list (string-append "/opt/homebrew/lib/postgresql@" v "/libpq.dylib")
                 (string-append "/usr/local/lib/postgresql@" v "/libpq.dylib")))
         (list "18" "17" "16" "15" "14"))))

(define %pq-candidates
  (append
    (list
      "libpq.dylib"                                              ; macOS, on loader path
      "libpq.so.5"                                                ; Linux, on loader path
      "/opt/homebrew/lib/libpq.dylib"                             ; Homebrew, Apple Silicon, unversioned formula
      "/opt/homebrew/opt/libpq/lib/libpq.dylib"                   ; Homebrew, keg-only libpq formula
      "/usr/local/lib/libpq.dylib"                                ; Homebrew, Intel Mac, unversioned formula
      "/usr/local/opt/libpq/lib/libpq.dylib")                     ; Homebrew, Intel Mac, keg-only
    %pq-versioned-candidates
    (list
      "/usr/lib/x86_64-linux-gnu/libpq.so.5"                      ; Debian/Ubuntu, x86_64
      "/usr/lib/aarch64-linux-gnu/libpq.so.5"                     ; Debian/Ubuntu, arm64
      "/usr/lib64/libpq.so.5")))                                   ; Fedora/RHEL

(define (%pq-try-load candidates)
  (let loop ((c candidates))
    (if (null? c)
        #f
        (guard (exn (#t (loop (cdr c))))
          (foreign-load-library (car c))))))

;; Loaded lazily, on the first actual pg-connect call, not at import
;; time -- importing this module never requires libpq to be installed.
(define %pq-lib #f)
(define %pq-bound? #f)

(define (%pq-ensure!)
  (unless %pq-lib
    (set! %pq-lib
      (or (%pq-try-load %pq-candidates)
          (error "postgres: could not load libpq — install it first:
  macOS:           brew install postgresql
  Debian/Ubuntu:   apt install libpq-dev
  Fedora/RHEL:     dnf install libpq-devel"))))
  (unless %pq-bound? (%pq-bind-fns!) (set! %pq-bound? #t)))

;;; ── Raw foreign bindings, bound lazily (see (curry zeromq)'s own
;;; header comment for why: define-foreign's #:from is evaluated once
;;; at its own definition time, before %pq-lib is populated). ─────────────────

(define %pq-connectdb #f) (define %pq-status #f) (define %pq-error-message #f) (define %pq-finish #f)
(define %pq-exec #f) (define %pq-result-status #f) (define %pq-result-error-message #f) (define %pq-clear #f)
(define %pq-result-error-field #f)
(define %pq-send-query #f) (define %pq-set-single-row-mode #f) (define %pq-get-result #f)
(define %pq-escape-identifier #f) (define %pq-consume-input #f) (define %pq-notifies #f)
(define %pq-put-copy-data #f) (define %pq-put-copy-end #f) (define %pq-get-copy-data #f)
(define %pq-ntuples #f) (define %pq-nfields #f) (define %pq-fname #f) (define %pq-ftype #f)
(define %pq-getvalue #f) (define %pq-getisnull #f) (define %pq-getlength #f) (define %pq-cmd-tuples #f)
(define %pq-escape-literal #f) (define %pq-escape-bytea-conn #f) (define %pq-unescape-bytea #f) (define %pq-freemem #f)

(define (%pq-bind-fns!)
  (set! %pq-connectdb (let ((fn (%ffi-make-fn %pq-lib "PQconnectdb" 'c-ptr '(c-string))))
                         (lambda (conninfo) (%ffi-call fn (list conninfo)))))
  (set! %pq-status (let ((fn (%ffi-make-fn %pq-lib "PQstatus" 'int '(c-ptr))))
                      (lambda (conn) (%ffi-call fn (list conn)))))
  (set! %pq-error-message (let ((fn (%ffi-make-fn %pq-lib "PQerrorMessage" 'c-string '(c-ptr))))
                             (lambda (conn) (%ffi-call fn (list conn)))))
  (set! %pq-finish (let ((fn (%ffi-make-fn %pq-lib "PQfinish" 'void '(c-ptr))))
                      (lambda (conn) (%ffi-call fn (list conn)))))
  (set! %pq-exec (let ((fn (%ffi-make-fn %pq-lib "PQexec" 'c-ptr '(c-ptr c-string))))
                   (lambda (conn sql) (%ffi-call fn (list conn sql)))))
  (set! %pq-result-status (let ((fn (%ffi-make-fn %pq-lib "PQresultStatus" 'int '(c-ptr))))
                             (lambda (res) (%ffi-call fn (list res)))))
  (set! %pq-result-error-message (let ((fn (%ffi-make-fn %pq-lib "PQresultErrorMessage" 'c-string '(c-ptr))))
                                    (lambda (res) (%ffi-call fn (list res)))))
  (set! %pq-result-error-field (let ((fn (%ffi-make-fn %pq-lib "PQresultErrorField" 'c-string '(c-ptr int))))
                                  (lambda (res field-code) (%ffi-call fn (list res field-code)))))
  (set! %pq-send-query (let ((fn (%ffi-make-fn %pq-lib "PQsendQuery" 'int '(c-ptr c-string))))
                          (lambda (conn sql) (%ffi-call fn (list conn sql)))))
  (set! %pq-set-single-row-mode (let ((fn (%ffi-make-fn %pq-lib "PQsetSingleRowMode" 'int '(c-ptr))))
                                   (lambda (conn) (%ffi-call fn (list conn)))))
  (set! %pq-get-result (let ((fn (%ffi-make-fn %pq-lib "PQgetResult" 'c-ptr '(c-ptr))))
                          (lambda (conn) (%ffi-call fn (list conn)))))
  (set! %pq-escape-identifier (let ((fn (%ffi-make-fn %pq-lib "PQescapeIdentifier" 'c-ptr '(c-ptr c-ptr uint64))))
                                 (lambda (conn str len) (%ffi-call fn (list conn str len)))))
  (set! %pq-consume-input (let ((fn (%ffi-make-fn %pq-lib "PQconsumeInput" 'int '(c-ptr))))
                             (lambda (conn) (%ffi-call fn (list conn)))))
  (set! %pq-notifies (let ((fn (%ffi-make-fn %pq-lib "PQnotifies" 'c-ptr '(c-ptr))))
                        (lambda (conn) (%ffi-call fn (list conn)))))
  (set! %pq-put-copy-data (let ((fn (%ffi-make-fn %pq-lib "PQputCopyData" 'int '(c-ptr c-ptr int))))
                             (lambda (conn buf nbytes) (%ffi-call fn (list conn buf nbytes)))))
  (set! %pq-put-copy-end (let ((fn (%ffi-make-fn %pq-lib "PQputCopyEnd" 'int '(c-ptr c-string))))
                            (lambda (conn errmsg) (%ffi-call fn (list conn errmsg)))))
  (set! %pq-get-copy-data (let ((fn (%ffi-make-fn %pq-lib "PQgetCopyData" 'int '(c-ptr c-ptr int))))
                             (lambda (conn buf-out-ptr async) (%ffi-call fn (list conn buf-out-ptr async)))))
  (set! %pq-clear (let ((fn (%ffi-make-fn %pq-lib "PQclear" 'void '(c-ptr))))
                     (lambda (res) (%ffi-call fn (list res)))))
  (set! %pq-ntuples (let ((fn (%ffi-make-fn %pq-lib "PQntuples" 'int '(c-ptr))))
                       (lambda (res) (%ffi-call fn (list res)))))
  (set! %pq-nfields (let ((fn (%ffi-make-fn %pq-lib "PQnfields" 'int '(c-ptr))))
                       (lambda (res) (%ffi-call fn (list res)))))
  (set! %pq-fname (let ((fn (%ffi-make-fn %pq-lib "PQfname" 'c-string '(c-ptr int))))
                     (lambda (res col) (%ffi-call fn (list res col)))))
  (set! %pq-ftype (let ((fn (%ffi-make-fn %pq-lib "PQftype" 'uint32 '(c-ptr int))))
                     (lambda (res col) (%ffi-call fn (list res col)))))
  (set! %pq-getvalue (let ((fn (%ffi-make-fn %pq-lib "PQgetvalue" 'c-string '(c-ptr int int))))
                        (lambda (res row col) (%ffi-call fn (list res row col)))))
  (set! %pq-getisnull (let ((fn (%ffi-make-fn %pq-lib "PQgetisnull" 'int '(c-ptr int int))))
                         (lambda (res row col) (%ffi-call fn (list res row col)))))
  (set! %pq-getlength (let ((fn (%ffi-make-fn %pq-lib "PQgetlength" 'int '(c-ptr int int))))
                         (lambda (res row col) (%ffi-call fn (list res row col)))))
  (set! %pq-cmd-tuples (let ((fn (%ffi-make-fn %pq-lib "PQcmdTuples" 'c-string '(c-ptr))))
                          (lambda (res) (%ffi-call fn (list res)))))
  (set! %pq-escape-literal (let ((fn (%ffi-make-fn %pq-lib "PQescapeLiteral" 'c-ptr '(c-ptr c-ptr uint64))))
                              (lambda (conn str len) (%ffi-call fn (list conn str len)))))
  (set! %pq-escape-bytea-conn (let ((fn (%ffi-make-fn %pq-lib "PQescapeByteaConn" 'c-ptr '(c-ptr c-ptr uint64 c-ptr))))
                                 (lambda (conn from from-len to-len-ptr) (%ffi-call fn (list conn from from-len to-len-ptr)))))
  (set! %pq-unescape-bytea (let ((fn (%ffi-make-fn %pq-lib "PQunescapeBytea" 'c-ptr '(c-string c-ptr))))
                              (lambda (str to-len-ptr) (%ffi-call fn (list str to-len-ptr)))))
  (set! %pq-freemem (let ((fn (%ffi-make-fn %pq-lib "PQfreemem" 'void '(c-ptr))))
                       (lambda (ptr) (%ffi-call fn (list ptr))))))

;;; ── Constants (from libpq-fe.h's public ConnStatusType/ExecStatusType
;;; enums; see this module's own header for why libpq isn't linked at
;;; build time, so these can't just be #include'd). ───────────────────────────

(define %CONNECTION-OK 0)
(define %PGRES-COMMAND-OK 1) (define %PGRES-TUPLES-OK 2)
(define %PGRES-COPY-OUT 3) (define %PGRES-COPY-IN 4)
(define %PGRES-SINGLE-TUPLE 9)

(define (%pg-ok-status? s) (or (= s %PGRES-COMMAND-OK) (= s %PGRES-TUPLES-OK)))

;;; ── Structured errors (see the design doc's §10: curry's condition
;;; system beats a plain error string here -- a caller can dispatch on
;;; sqlstate via handler-bind/condition-field instead of pattern-
;;; matching the message text). PG_DIAG_SQLSTATE is the ASCII char 'C'
;;; per libpq-fe.h's own PQresultErrorField field-code enum. ─────────────
(define-condition postgres-error (error) #:fields (sqlstate))
(define %PG-DIAG-SQLSTATE 67) ;; #\C

;; (%pg-raise sqlstate-or-#f message) -- condition-error's own signal/
;; raise never returns to its caller (either a handler-bind invokes a
;; restart that jumps elsewhere, or it unwinds via raise), so the
;; sqlstate has to be read out of the PGresult *before* calling this --
;; a caller that also needs to PQclear the result must do that itself,
;; after this call, which it will never reach; do it before instead.
;; sqlstate is #f for a connection-level failure (e.g. PQconnectdb
;; itself failing) that never had a PGresult to read one from at all.
(define (%pg-raise sqlstate message)
  (condition-error 'postgres-error (list (cons 'sqlstate sqlstate)) message))

;;; ── Column type coercion (see this module's design-doc header for why
;;; this exists at all: (curry sqlite)'s own row_to_alist already
;;; returns fixnum/flonum/string/bytevector/#f per SQLite's own column
;;; type -- returning bare strings for everything here was the odd one
;;; out in (curry sql)'s cross-backend contract, not a considered
;;; design). OIDs are from PostgreSQL's own built-in pg_type catalog
;;; (stable, well-known values -- see src/include/catalog/pg_type.dat
;;; upstream); anything not listed here stays a plain string (text,
;;; varchar, date, timestamp, json, uuid, ...). ─────────────────────────────
(define %PG-OID-BOOL 16) (define %PG-OID-BYTEA 17)
(define %PG-OID-INT8 20) (define %PG-OID-INT2 21) (define %PG-OID-INT4 23) (define %PG-OID-OID 26)
(define %PG-OID-FLOAT4 700) (define %PG-OID-FLOAT8 701) (define %PG-OID-NUMERIC 1700)

;; (%pg-coerce oid text) -- text is never #f here (NULL is checked by
;; the caller before this is ever reached); returns the Scheme value
;; `text` should become for a column of type `oid`.
(define (%pg-coerce oid text)
  (cond
    ((= oid %PG-OID-BOOL) (string=? text "t"))
    ((or (= oid %PG-OID-INT8) (= oid %PG-OID-INT2) (= oid %PG-OID-INT4) (= oid %PG-OID-OID))
     (string->number text))
    ;; NUMERIC can carry more precision than a double -- this is a
    ;; known, accepted lossy conversion, the same tradeoff every
    ;; non-decimal-aware SQL binding makes (see this module's header).
    ((or (= oid %PG-OID-FLOAT4) (= oid %PG-OID-FLOAT8) (= oid %PG-OID-NUMERIC))
     (exact->inexact (string->number text)))
    ((= oid %PG-OID-BYTEA) (%pg-unescape-bytea text))
    (else text)))

;; (%pg-unescape-bytea text) -> bytevector. PQunescapeBytea handles both
;; of libpq's own bytea text formats (hex, the modern default, and the
;; legacy octal-escape format) transparently -- this module never needs
;; to know which one a given server is configured to emit.
(define (%pg-unescape-bytea text)
  (let ((len-out (make-bytevector 8 0)))
    (with-pinned-bytevector len-out len-ptr
      (let ((buf (%pq-unescape-bytea text len-ptr)))
        (when (cptr-null? buf) (error "postgres: PQunescapeBytea failed (out of memory)"))
        (let* ((len (let loop ((i 7) (acc 0)) (if (< i 0) acc (loop (- i 1) (+ (* acc 256) (bytevector-u8-ref len-out i))))))
               (bv (peek-bytes buf len)))
          (%pq-freemem buf)
          bv)))))

;;; ── Connecting ───────────────────────────────────────────────────────────────

(define (%conninfo-escape s)
  ;; libpq's own conninfo grammar: a value containing whitespace or a
  ;; single quote must be wrapped in single quotes, with any embedded
  ;; backslash or single quote itself backslash-escaped.
  (let ((out (open-output-string)))
    (write-char #\' out)
    (string-for-each
      (lambda (c) (when (or (char=? c #\') (char=? c #\\)) (write-char #\\ out)) (write-char c out))
      s)
    (write-char #\' out)
    (get-output-string out)))

;; (pg-connect config) -- config is an alist, e.g.
;;   '((host . "localhost") (port . 5432) (dbname . "app") (user . "app") (password . "secret"))
;; Every key is written straight through as "key=value" (space-joined) --
;; any alist key libpq itself understands (host, hostaddr, port, dbname,
;; user, password, connect_timeout, sslmode, ...) works without this
;; module needing to know about it specifically.
(define (pg-connect config)
  (%pq-ensure!)
  (let ((conninfo
          (let loop ((kvs config) (parts '()))
            (if (null? kvs)
                (apply string-append (reverse parts))
                (loop (cdr kvs)
                      (cons (string-append (symbol->string (car (car kvs))) "="
                                           (%conninfo-escape
                                             (let ((v (cdr (car kvs))))
                                               (if (string? v) v (number->string v))))
                                           " ")
                            parts))))))
    (let ((conn (%pq-connectdb conninfo)))
      (when (cptr-null? conn) (error "postgres: PQconnectdb failed (out of memory)"))
      (unless (= (%pq-status conn) %CONNECTION-OK)
        (let ((msg (%pq-error-message conn)))
          (%pq-finish conn)
          ;; No PGresult exists yet at this point -- a connection-level
          ;; failure genuinely has no SQLSTATE, not just an unread one.
          (%pg-raise #f (string-append "postgres: connection failed: " msg))))
      conn)))

(define (pg-connect? x) (c-ptr? x))
(define (pg-close conn) (%pq-finish conn))
(define (pg-error conn) (%pq-error-message conn))

;;; ── Running statements ───────────────────────────────────────────────────────

;; (pg-exec conn sql) -> (values rows affected-rows), where rows is a
;; list of alists (column-name symbol -> string or #f for SQL NULL) and
;; affected-rows is an integer (0 for a SELECT; PQcmdTuples's own text
;; is only meaningful for INSERT/UPDATE/DELETE, and is an empty string
;; otherwise, which this module reads as 0 rather than passing #f
;; through, since "0 rows affected by a non-DML statement" and "this
;; statement doesn't have that concept" collapse to the same practical
;; answer for (curry sql)'s own driver-affected-rows contract).
(define (pg-exec conn sql)
  (let ((res (%pq-exec conn sql)))
    ;; PQexec itself returning NULL is a client-side failure (out of
    ;; memory) with no PGresult to read a sqlstate off of either.
    (when (cptr-null? res) (%pg-raise #f (string-append "postgres: PQexec failed: " (%pq-error-message conn))))
    (let ((status (%pq-result-status res)))
      (unless (%pg-ok-status? status)
        ;; Read both the message and the sqlstate before clearing res --
        ;; PQclear frees it, and %pg-raise never returns to run any
        ;; cleanup written after it (see %pg-raise's own comment).
        (let ((msg (%pq-result-error-message res))
              (sqlstate (%pq-result-error-field res %PG-DIAG-SQLSTATE)))
          (%pq-clear res)
          (%pg-raise sqlstate (string-append "postgres: " msg))))
      (let* ((ncols (%pq-nfields res))
             (names (let loop ((i 0) (acc '()))
                      (if (= i ncols) (reverse acc) (loop (+ i 1) (cons (string->symbol (%pq-fname res i)) acc)))))
             ;; Read every column's type once per result, not once per
             ;; cell -- PQftype is a plain O(1) accessor, but there's no
             ;; reason to call it nrows times over.
             (types (let loop ((i 0) (acc '()))
                      (if (= i ncols) (reverse acc) (loop (+ i 1) (cons (%pq-ftype res i) acc)))))
             (nrows (%pq-ntuples res))
             (rows (let row-loop ((r 0) (racc '()))
                     (if (= r nrows)
                         (reverse racc)
                         (row-loop (+ r 1)
                           (cons (let col-loop ((c 0) (cacc '()))
                                   (if (= c ncols)
                                       (reverse cacc)
                                       (col-loop (+ c 1)
                                         (cons (cons (list-ref names c)
                                                     (if (= (%pq-getisnull res r c) 1)
                                                         #f
                                                         (%pg-coerce (list-ref types c) (%pq-getvalue res r c))))
                                               cacc))))
                                 racc)))))
             (affected (let ((s (%pq-cmd-tuples res))) (if (string=? s "") 0 (or (string->number s) 0)))))
        (%pq-clear res)
        (values rows affected)))))

;;; ── Escaping (used by (curry sql)'s escape-and-splice strategy for
;;; parameters, since PQexecParams's char* const* argument array is
;;; deliberately not used here -- see this module's own header). ─────────────

;; (pg-escape-literal conn value) -> a fully quoted+escaped SQL literal
;; string, e.g. "O'Brien" -> "'O''Brien'". Frees libpq's own malloc'd
;; buffer before returning.
(define (pg-escape-literal conn value)
  (let ((bv (string->utf8 value)))
    (with-pinned-bytevector bv ptr
      (let ((escaped (%pq-escape-literal conn ptr (bytevector-length bv))))
        (when (cptr-null? escaped) (error (string-append "postgres: PQescapeLiteral failed: " (%pq-error-message conn))))
        (let ((s (%c-string-at escaped)))
          (%pq-freemem escaped)
          s)))))

;; (pg-escape-bytea conn bv) -> a quoted PostgreSQL bytea literal
;; (libpq's own hex-escape format, already including the surrounding
;; quotes appropriate for splicing straight into SQL text).
(define (pg-escape-bytea conn bv)
  (let ((to-len (make-bytevector 8 0)))
    (with-pinned-bytevector bv from-ptr
      (with-pinned-bytevector to-len to-len-ptr
        (let ((escaped (%pq-escape-bytea-conn conn from-ptr (bytevector-length bv) to-len-ptr)))
          (when (cptr-null? escaped) (error (string-append "postgres: PQescapeByteaConn failed: " (%pq-error-message conn))))
          ;; PQescapeByteaConn's own result is already-escaped ASCII text
          ;; (e.g. \x-hex format), NUL-terminated -- %c-string-at is exactly
          ;; the right reader for it, same as any other libpq string result.
          (let ((text (%c-string-at escaped)))
            (%pq-freemem escaped)
            (string-append "'" text "'")))))))

;; Reads a NUL-terminated C string starting at `ptr`, one byte at a
;; time, stopping the instant a zero byte is found -- never reads past
;; the string's own terminator, unlike reading some fixed-size chunk
;; and hoping the NUL is inside it.
(define (%c-string-at ptr)
  (let ((out (open-output-bytevector)))
    (let loop ((i 0))
      (let ((b (bytevector-u8-ref (peek-bytes (+ (cptr-address ptr) i) 1) 0)))
        (if (zero? b)
            (utf8->string (get-output-bytevector out))
            (begin (write-u8 b out) (loop (+ i 1))))))))

;;; ── Last insert id ───────────────────────────────────────────────────────────

;; (pg-last-insert-id conn [sequence-name]) -> integer.
;; With no sequence-name: "SELECT lastval()" -- the last value produced
;; by ANY sequence in this session (not column/table-specific; see
;; (curry sql)'s own design doc §9 for why PostgreSQL has no more
;; precise universal answer than this, and why "INSERT ... RETURNING
;; id" is the genuinely idiomatic alternative). Raises if no sequence
;; has been used yet in this session (lastval() itself errors in that
;; case -- a real, honest limitation, not something this module papers
;; over).
;; With a sequence-name: "SELECT currval(<name>)" -- the named
;; sequence's own last value in this session, still raising if that
;; specific sequence has never been used here. sequence-name is passed
;; through pg-escape-literal (needs a live connection, which we have)
;; rather than spliced directly, since it's caller-supplied text.
(define (pg-last-insert-id conn . sequence-name)
  (let ((sql (if (null? sequence-name)
                 "SELECT lastval()"
                 (string-append "SELECT currval(" (pg-escape-literal conn (car sequence-name)) ")"))))
    (let-values (((rows affected) (pg-exec conn sql)))
      ;; lastval()/currval() are int8 columns -- pg-exec's own column
      ;; type coercion already turns those into a Scheme fixnum, not a
      ;; string, so this is a direct pass-through now (used to be
      ;; string->number, back when pg-exec returned bare strings for
      ;; every column regardless of type).
      (cdr (car (car rows))))))

;;; ── Streaming via SQL cursors (see (curry sql)'s own sql-query-
;;; stream/sql-stream-next!/sql-stream-close!) -- portable, no new
;;; libpq bindings needed at all: DECLARE/FETCH/CLOSE are plain SQL
;;; text, the same escape-and-splice-free path pg-exec already runs
;;; everything else through. WITH HOLD specifically so the cursor
;;; survives outside an explicit transaction -- keeps a stream self-
;;; contained rather than forcing every caller through a transaction
;;; of their own. Cursor names are internally generated (never
;;; caller-supplied text), so no identifier escaping is needed here --
;;; contrast (curry postgres)'s own pg-listen/pg-notify (Part 5), whose
;;; channel names genuinely are caller-supplied and do need it. ───────────────

(define-record-type <pg-cursor>
  (%make-pg-cursor name conn)
  pg-cursor?
  (name pg-cursor-name)
  (conn pg-cursor-conn))

(define %pg-cursor-counter 0)

;; (pg-cursor-open conn sql) -- sql should already have any (curry
;; sql)-level `?` params spliced in; this module itself never sees a
;; placeholder.
(define (pg-cursor-open conn sql)
  (set! %pg-cursor-counter (+ %pg-cursor-counter 1))
  (let ((name (string-append "curry_cur_" (number->string %pg-cursor-counter))))
    (pg-exec conn (string-append "DECLARE " name " CURSOR WITH HOLD FOR " sql))
    (%make-pg-cursor name conn)))

;; (pg-cursor-fetch cursor) -> row alist or #f at end.
(define (pg-cursor-fetch cursor)
  (let-values (((rows affected) (pg-exec (pg-cursor-conn cursor) (string-append "FETCH 1 FROM " (pg-cursor-name cursor)))))
    (if (pair? rows) (car rows) #f)))

;; (pg-cursor-close cursor) -- safe to call whether or not the cursor
;; was already fully drained; CLOSE on a cursor with no remaining rows
;; is still a valid, ordinary command.
(define (pg-cursor-close cursor)
  (pg-exec (pg-cursor-conn cursor) (string-append "CLOSE " (pg-cursor-name cursor)))
  (values))

;;; ── Streaming via native async single-row mode (a Postgres-only
;;; extra, not wired into (curry sql)'s own driver record -- that uses
;;; pg-cursor-* above, the portable path. This one skips the cursor
;;; round-trip entirely: PQsendQuery + PQsetSingleRowMode make the
;;; server hand back one PGresult per row directly, as they're
;;; produced, instead of buffering server-side between FETCHes. Real
;;; tradeoff for a caller to weigh: this ties up the connection for
;;; other use until the stream is fully drained or closed, where a
;;; cursor's own FETCHes don't. ─────────────────────────────────────────────

(define-record-type <pg-async-stream>
  (%make-pg-async-stream conn done?-box)
  pg-async-stream?
  (conn      pg-async-stream-conn)
  (done?-box pg-async-stream-done?-box))

;; (%pg-result-row-alist res) -- res is a single-row PGresult
;; (PGRES_SINGLE_TUPLE always carries exactly one row, at index 0).
;; Column types are read fresh per result rather than cached across
;; calls, unlike pg-exec's own per-result-set caching -- simpler and
;; correctness-first for what's meant to stay a narrow, opt-in extra.
(define (%pg-result-row-alist res)
  (let* ((ncols (%pq-nfields res)))
    (let loop ((c 0) (acc '()))
      (if (= c ncols)
          (reverse acc)
          (loop (+ c 1)
                (cons (cons (string->symbol (%pq-fname res c))
                            (if (= (%pq-getisnull res 0 c) 1)
                                #f
                                (%pg-coerce (%pq-ftype res c) (%pq-getvalue res 0 c))))
                      acc))))))

;; (pg-stream-open conn sql) -> stream handle for pg-stream-next/
;; pg-stream-close. sql should already have any (curry sql)-level `?`
;; params spliced in, same as pg-cursor-open.
(define (pg-stream-open conn sql)
  (when (zero? (%pq-send-query conn sql))
    (%pg-raise #f (string-append "postgres: PQsendQuery failed: " (%pq-error-message conn))))
  ;; Must be called after PQsendQuery, before the first PQgetResult --
  ;; libpq's own documented ordering requirement.
  (when (zero? (%pq-set-single-row-mode conn))
    (%pg-raise #f "postgres: PQsetSingleRowMode failed (called too late, or no query in progress)"))
  (%make-pg-async-stream conn (list #f)))

;; (pg-stream-next stream) -> row alist or #f at end.
(define (pg-stream-next stream)
  (if (car (pg-async-stream-done?-box stream))
      #f
      (let ((res (%pq-get-result (pg-async-stream-conn stream))))
        (cond
          ((cptr-null? res)
           (set-car! (pg-async-stream-done?-box stream) #t)
           #f)
          ((= (%pq-result-status res) %PGRES-SINGLE-TUPLE)
           (let ((row (%pg-result-row-alist res)))
             (%pq-clear res)
             row))
          ((%pg-ok-status? (%pq-result-status res))
           ;; The final, row-less terminator result (PGRES_TUPLES_OK/
           ;; COMMAND_OK) -- keep draining until PQgetResult itself
           ;; finally returns NULL, which is what actually marks the
           ;; command complete.
           (%pq-clear res)
           (pg-stream-next stream))
          (else
           (let ((msg (%pq-result-error-message res))
                 (sqlstate (%pq-result-error-field res %PG-DIAG-SQLSTATE)))
             (%pq-clear res)
             (set-car! (pg-async-stream-done?-box stream) #t)
             (%pg-raise sqlstate (string-append "postgres: " msg))))))))

;; (pg-stream-close stream) -- safe to call whether or not the stream
;; was already fully drained. If not, drains every remaining PGresult
;; first: PQgetResult must be called until it returns NULL before
;; another command can run on this connection at all -- an async
;; query left half-read would otherwise corrupt every later query on
;; the same connection, not just this one.
(define (pg-stream-close stream)
  (unless (car (pg-async-stream-done?-box stream))
    (let loop ()
      (let ((res (%pq-get-result (pg-async-stream-conn stream))))
        (unless (cptr-null? res)
          (%pq-clear res)
          (loop))))
    (set-car! (pg-async-stream-done?-box stream) #t)))

;;; ── LISTEN/NOTIFY (a Postgres-only extra -- no equivalent concept in
;;; MariaDB or SQLite, so nothing here fits (curry sql)'s own shared
;;; driver record). Unlike pg-cursor-*'s internally-generated names,
;;; a channel name IS caller-supplied text, so it goes through
;;; PQescapeIdentifier (double-quote identifier escaping) rather than
;;; being spliced raw -- LISTEN/NOTIFY take an unquoted-or-double-
;;; quoted identifier, not a string literal, so pg-escape-literal's
;;; own single-quote-string escaping would be the wrong tool here. ───────────

;; (%pg-escape-identifier conn name) -> a double-quoted, escaped SQL
;; identifier, e.g. "foo\"bar" -> "\"foo\"\"bar\"". Frees libpq's own
;; malloc'd buffer before returning, same pattern as pg-escape-literal.
(define (%pg-escape-identifier conn name)
  (let ((bv (string->utf8 name)))
    (with-pinned-bytevector bv ptr
      (let ((escaped (%pq-escape-identifier conn ptr (bytevector-length bv))))
        (when (cptr-null? escaped) (error (string-append "postgres: PQescapeIdentifier failed: " (%pq-error-message conn))))
        (let ((s (%c-string-at escaped)))
          (%pq-freemem escaped)
          s)))))

;; PGnotify's own struct layout (libpq-fe.h): relname (char*, offset
;; 0), be_pid (int, offset 8), extra (char*, offset 16 -- 4 bytes of
;; padding after the int to realign the next pointer to an 8-byte
;; boundary on both x86_64 and arm64), next (private to libpq, offset
;; 24, never read here). Verified against the actual header shipped
;; with the local libpq 18 install, not just the historical struct
;; comment (libpq-fe.h itself says "so simple it's unlikely to
;; change," and it hasn't since Postgres 6.4).
(define %PGNOTIFY-RELNAME-OFFSET 0) (define %PGNOTIFY-BE-PID-OFFSET 8) (define %PGNOTIFY-EXTRA-OFFSET 16)

(define (%pg-notify->triple notify)
  (let ((addr (cptr-address notify)))
    (list (%c-string-at (make-cptr (%peek-u64-le (+ addr %PGNOTIFY-RELNAME-OFFSET))))
          (%peek-u32-le (+ addr %PGNOTIFY-BE-PID-OFFSET))
          (%c-string-at (make-cptr (%peek-u64-le (+ addr %PGNOTIFY-EXTRA-OFFSET)))))))

;; Reads an 8-byte little-endian value at `addr` as a fixnum -- same
;; rationale (curry mariadb)'s own %peek-u64-le comment gives.
(define (%peek-u64-le addr)
  (let ((bv (peek-bytes addr 8)))
    (let loop ((i 7) (acc 0)) (if (< i 0) acc (loop (- i 1) (+ (* acc 256) (bytevector-u8-ref bv i)))))))

;; Reads a 4-byte little-endian value at `addr` as a fixnum -- for
;; be_pid, an `int`, half the width of the char* members.
(define (%peek-u32-le addr)
  (let ((bv (peek-bytes addr 4)))
    (let loop ((i 3) (acc 0)) (if (< i 0) acc (loop (- i 1) (+ (* acc 256) (bytevector-u8-ref bv i)))))))

;; (pg-listen conn channel) -- subscribes this connection to `channel`.
;; Notifications become visible via pg-notifications, never pushed;
;; see that procedure's own comment for the poll-based caveat.
(define (pg-listen conn channel)
  (pg-exec conn (string-append "LISTEN " (%pg-escape-identifier conn channel)))
  (values))

(define (pg-unlisten conn channel)
  (pg-exec conn (string-append "UNLISTEN " (%pg-escape-identifier conn channel)))
  (values))

;; (pg-notify conn channel payload) -- payload goes through
;; pg-escape-literal (a string literal, not an identifier).
(define (pg-notify conn channel payload)
  (pg-exec conn (string-append "NOTIFY " (%pg-escape-identifier conn channel) ", " (pg-escape-literal conn payload)))
  (values))

;; (pg-notifications conn) -> a list of (channel pid . payload)
;; triples, possibly empty. Poll-based, not a blocking wait: this
;; calls PQconsumeInput (reads any pending server data into libpq's
;; own client-side buffer without blocking on the network) and then
;; drains PQnotifies until it returns NULL. A caller wanting to block
;; until a notification arrives needs its own wait on PQsocket(conn)'s
;; underlying file descriptor -- genuinely out of scope here, not
;; silently missing: this module has no socket-level primitives at all
;; (see this module's own header on why libpq isn't linked at build
;; time in the first place).
(define (pg-notifications conn)
  (when (zero? (%pq-consume-input conn))
    (%pg-raise #f (string-append "postgres: PQconsumeInput failed: " (%pq-error-message conn))))
  (let loop ((acc '()))
    (let ((notify (%pq-notifies conn)))
      (if (cptr-null? notify)
          (reverse acc)
          (let ((triple (%pg-notify->triple notify)))
            (%pq-freemem notify)
            (loop (cons (cons (car triple) (cons (cadr triple) (caddr triple))) acc)))))))

;;; ── COPY (a Postgres-only extra, bulk load/unload -- no equivalent
;;; in MariaDB's or SQLite's own C API, so this doesn't fit (curry
;;; sql)'s shared driver record either). PQputCopyData/PQgetCopyData
;;; both move raw bytes, not text -- passed/read as bytevectors, never
;;; through a c-string marshal, since COPY data can contain arbitrary
;;; binary content (e.g. BINARY-format COPY) that a NUL-terminated
;;; c-string would truncate. ──────────────────────────────────────────────────

;; (pg-copy-from-start conn sql) -- sql is a full "COPY ... FROM STDIN
;; [...]" statement; asserts the server actually entered COPY-IN mode.
(define (pg-copy-from-start conn sql)
  (let ((res (%pq-exec conn sql)))
    (when (cptr-null? res) (%pg-raise #f (string-append "postgres: PQexec failed: " (%pq-error-message conn))))
    (let ((status (%pq-result-status res)))
      (if (= status %PGRES-COPY-IN)
          (begin (%pq-clear res) (values))
          (let ((msg (%pq-result-error-message res))
                (sqlstate (%pq-result-error-field res %PG-DIAG-SQLSTATE)))
            (%pq-clear res)
            (%pg-raise sqlstate (string-append "postgres: expected COPY IN: " msg)))))))

;; (pg-copy-data conn bytes) -- bytes is a bytevector, or a string
;; (encoded to UTF-8 first; COPY TEXT/CSV format expects text, COPY
;; BINARY expects a caller-built bytevector already in that format).
(define (pg-copy-data conn bytes)
  (let ((bv (if (bytevector? bytes) bytes (string->utf8 bytes))))
    (with-pinned-bytevector bv ptr
      (when (< (%pq-put-copy-data conn ptr (bytevector-length bv)) 0)
        (%pg-raise #f (string-append "postgres: PQputCopyData failed: " (%pq-error-message conn)))))
    (values)))

;; (pg-copy-end conn) -- signals normal completion (PQputCopyEnd with
;; no error message) and drains the resulting command-complete result,
;; same "must fully drain PQgetResult" requirement pg-stream-close
;; documents for the async single-row-mode path.
(define (pg-copy-end conn)
  (when (< (%pq-put-copy-end conn #f) 0)
    (%pg-raise #f (string-append "postgres: PQputCopyEnd failed: " (%pq-error-message conn))))
  (let loop ()
    (let ((res (%pq-get-result conn)))
      (unless (cptr-null? res)
        (let ((status (%pq-result-status res)))
          (if (%pg-ok-status? status)
              (begin (%pq-clear res) (loop))
              (let ((msg (%pq-result-error-message res))
                    (sqlstate (%pq-result-error-field res %PG-DIAG-SQLSTATE)))
                (%pq-clear res)
                (%pg-raise sqlstate (string-append "postgres: " msg))))))))
  (values))

;; (pg-copy-to-start conn sql) -- sql is a full "COPY ... TO STDOUT
;; [...]" statement; asserts the server actually entered COPY-OUT mode.
(define (pg-copy-to-start conn sql)
  (let ((res (%pq-exec conn sql)))
    (when (cptr-null? res) (%pg-raise #f (string-append "postgres: PQexec failed: " (%pq-error-message conn))))
    (let ((status (%pq-result-status res)))
      (if (= status %PGRES-COPY-OUT)
          (%pq-clear res)
          (let ((msg (%pq-result-error-message res))
                (sqlstate (%pq-result-error-field res %PG-DIAG-SQLSTATE)))
            (%pq-clear res)
            (%pg-raise sqlstate (string-append "postgres: expected COPY OUT: " msg))))))
  (values))

;; (pg-copy-fetch conn) -> one chunk as a bytevector, or #f at end
;; (having already drained the final result, same as pg-copy-end).
;; PQgetCopyData writes the address of its own malloc'd buffer into
;; the 8-byte out-param this pins -- read straight back out of that
;; same bytevector's own bytes rather than a second peek through a
;; wrapped pointer.
(define (pg-copy-fetch conn)
  (let ((buf-out (make-bytevector 8 0)))
    (with-pinned-bytevector buf-out buf-out-ptr
      (let ((n (%pq-get-copy-data conn buf-out-ptr 0)))
        (cond
          ((> n 0)
           (let* ((data-addr (let loop ((i 7) (acc 0)) (if (< i 0) acc (loop (- i 1) (+ (* acc 256) (bytevector-u8-ref buf-out i))))))
                  (bv (peek-bytes (make-cptr data-addr) n)))
             (%pq-freemem (make-cptr data-addr))
             bv))
          ((= n -1)
           (let loop ()
             (let ((res (%pq-get-result conn)))
               (unless (cptr-null? res) (%pq-clear res) (loop))))
           #f)
          (else (%pg-raise #f (string-append "postgres: PQgetCopyData failed: " (%pq-error-message conn)))))))))

  )) ;; end begin, define-library
