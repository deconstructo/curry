;;; (curry mariadb) — MariaDB/MySQL client, via (curry ffi). libmariadb
;;; (or libmysqlclient — both implement the same historic C client API)
;;; is dlopen'd lazily at runtime (not linked at build time — no CMake
;;; flag beyond the general BUILD_FFI=ON), the same pattern (curry
;;; hdf5)/(curry ncurses)/(curry graphviz)/(curry zeromq)/(curry
;;; postgres) use. Install: `brew install mariadb` (macOS, ships
;;; libmariadb), `apt install libmariadb-dev` (Debian/Ubuntu), `dnf
;;; install mariadb-connector-c-devel` (Fedora/RHEL).
;;;
;;; This module is written to satisfy (curry sql)'s <sql-driver>
;;; protocol (see docs/thoughts/sql-abstraction-design.md and
;;; lib/curry/modules/curry/sql.scm) — its own procedures below are
;;; plain, independently usable MariaDB bindings, but their real
;;; purpose is being wired into sql-connect's 'mariadb case.
;;;
;;; MySQL's C API has no libpq-style "give me the column name as a
;;; plain string" accessor: mysql_fetch_field_direct returns a pointer
;;; to a MYSQL_FIELD struct, and getting the name out of it means
;;; reading that struct's own memory directly. This module reads
;;; exactly one thing from it: the `name` field, which is documented
;;; (mysql.h) and has been for the entire public history of this
;;; struct to be its FIRST member — meaning its address is always
;;; identical to the struct's own address, regardless of anything else
;;; declared later in the struct (padding, reordering, added fields
;;; all only ever affect offsets AFTER the first member, by the C
;;; language's own layout rules). This is the same pattern essentially
;;; every other language's MySQL FFI binding uses (Python's
;;; mysqlclient, Ruby's mysql2, etc. all read MYSQL_FIELD->name this
;;; same way) — not a shortcut unique to this module.
;;;
;;; mysql_fetch_row similarly returns a MYSQL_ROW, which IS simply a
;;; `char**` (an array of column-value pointers, one per column, NULL
;;; for a SQL NULL) — no struct-layout assumption needed there at all,
;;; since "array of pointers" has an unambiguous, universally-agreed
;;; memory layout in C regardless of any library's own internal
;;; struct choices.
;;;
;;; No native prepared-statement binary protocol (mysql_stmt_prepare/
;;; mysql_stmt_bind_param, which needs an array of MYSQL_BIND structs)
;;; is used here — see (curry sql)'s own design doc §5 for why:
;;; building that struct array is real, avoidable FFI complexity this
;;; module's escape-and-splice strategy (via mysql_real_escape_string,
;;; an ordinary buffer-in-buffer-out call) doesn't need at all.
;;;
;;; NOTE: my-connect/my-exec have been exercised live against a real
;;; local MariaDB server (a SELECT round-trip, verifying the row-alist
;;; and column-name decoding this module builds by raw pointer
;;; arithmetic — see my-exec's own header). my-escape-literal/
;;; my-escape-bytea still have not, since exercising them needs an
;;; already-connected MYSQL* handle whose charset info was read from a
;;; live server, and no such server was available for the specific
;;; developer session that wrote them — see tests/mariadb_tests.scm's
;;; own header for exactly what this module's automated test coverage
;;; does and doesn't reach.

(define-library (curry mariadb)
  (import (scheme base) (curry ffi) (curry conditions))
  (export
    my-connect my-connect? my-close
    my-exec my-escape-literal my-escape-bytea
    my-last-insert-id my-error
    my-query-stream my-stream-next my-stream-close my-stream?)
  (begin

;;; ── Library discovery ────────────────────────────────────────────────────────

(define %my-versioned-candidates
  (apply append
    (map (lambda (v)
           (list (string-append "/opt/homebrew/lib/mariadb@" v "/libmariadb.dylib")
                 (string-append "/usr/local/lib/mariadb@" v "/libmariadb.dylib")))
         (list "12" "11" "10"))))

(define %my-candidates
  (append
    (list
      "libmariadb.dylib"                                           ; macOS, on loader path
      "libmariadb.so.3"                                             ; Linux, on loader path
      "libmysqlclient.dylib"                                        ; macOS, MySQL's own client lib name
      "libmysqlclient.so.21"                                        ; Linux, MySQL's own client lib name
      "/opt/homebrew/lib/libmariadb.dylib"                          ; Homebrew, Apple Silicon
      "/opt/homebrew/opt/mariadb-connector-c/lib/libmariadb.dylib"   ; Homebrew, keg-only connector-c formula
      "/usr/local/lib/libmariadb.dylib"                             ; Homebrew, Intel Mac
      "/usr/local/opt/mariadb-connector-c/lib/libmariadb.dylib")     ; Homebrew, Intel Mac, keg-only
    %my-versioned-candidates
    (list
      "/usr/lib/x86_64-linux-gnu/libmariadb.so.3"                   ; Debian/Ubuntu, x86_64
      "/usr/lib/aarch64-linux-gnu/libmariadb.so.3"                  ; Debian/Ubuntu, arm64
      "/usr/lib64/libmariadb.so.3")))                                ; Fedora/RHEL

(define (%my-try-load candidates)
  (let loop ((c candidates))
    (if (null? c)
        #f
        (guard (exn (#t (loop (cdr c))))
          (foreign-load-library (car c))))))

;; Loaded lazily, on the first actual my-connect call, not at import
;; time -- importing this module never requires libmariadb to be installed.
(define %my-lib #f)
(define %my-bound? #f)

(define (%my-ensure!)
  (unless %my-lib
    (set! %my-lib
      (or (%my-try-load %my-candidates)
          (error "mariadb: could not load libmariadb — install it first:
  macOS:           brew install mariadb
  Debian/Ubuntu:   apt install libmariadb-dev
  Fedora/RHEL:     dnf install mariadb-connector-c-devel"))))
  (unless %my-bound? (%my-bind-fns!) (set! %my-bound? #t)))

;;; ── Raw foreign bindings, bound lazily (see (curry zeromq)/(curry
;;; postgres)'s own header comments for why). ─────────────────────────────────

(define %my-init #f) (define %my-real-connect #f) (define %my-close-raw #f) (define %my-error #f)
(define %my-errno #f) (define %my-sqlstate #f)
(define %my-query #f) (define %my-store-result #f) (define %my-use-result #f) (define %my-field-count #f) (define %my-free-result #f)
(define %my-fetch-row #f) (define %my-fetch-lengths #f) (define %my-num-fields #f) (define %my-fetch-field-direct #f)
(define %my-insert-id #f) (define %my-affected-rows #f) (define %my-real-escape-string #f) (define %my-ssl-set #f)

(define (%my-bind-fns!)
  (set! %my-init (let ((fn (%ffi-make-fn %my-lib "mysql_init" 'c-ptr '(c-ptr))))
                    (lambda () (%ffi-call fn (list #f)))))
  (set! %my-real-connect
    (let ((fn (%ffi-make-fn %my-lib "mysql_real_connect" 'c-ptr '(c-ptr c-string c-string c-string c-string uint uint32 uint64))))
      (lambda (mysql host user passwd db port unix-socket clientflag)
        (%ffi-call fn (list mysql host user passwd db port unix-socket clientflag)))))
  (set! %my-close-raw (let ((fn (%ffi-make-fn %my-lib "mysql_close" 'void '(c-ptr))))
                         (lambda (mysql) (%ffi-call fn (list mysql)))))
  (set! %my-error (let ((fn (%ffi-make-fn %my-lib "mysql_error" 'c-string '(c-ptr))))
                     (lambda (mysql) (%ffi-call fn (list mysql)))))
  (set! %my-errno (let ((fn (%ffi-make-fn %my-lib "mysql_errno" 'uint '(c-ptr))))
                     (lambda (mysql) (%ffi-call fn (list mysql)))))
  (set! %my-sqlstate (let ((fn (%ffi-make-fn %my-lib "mysql_sqlstate" 'c-string '(c-ptr))))
                        (lambda (mysql) (%ffi-call fn (list mysql)))))
  (set! %my-query (let ((fn (%ffi-make-fn %my-lib "mysql_query" 'int '(c-ptr c-string))))
                     (lambda (mysql sql) (%ffi-call fn (list mysql sql)))))
  (set! %my-store-result (let ((fn (%ffi-make-fn %my-lib "mysql_store_result" 'c-ptr '(c-ptr))))
                            (lambda (mysql) (%ffi-call fn (list mysql)))))
  ;; mysql_use_result -- same signature as mysql_store_result, but
  ;; leaves the result set unbuffered on the wire instead of reading it
  ;; all into memory up front. Used by my-query-stream (see below).
  (set! %my-use-result (let ((fn (%ffi-make-fn %my-lib "mysql_use_result" 'c-ptr '(c-ptr))))
                          (lambda (mysql) (%ffi-call fn (list mysql)))))
  (set! %my-field-count (let ((fn (%ffi-make-fn %my-lib "mysql_field_count" 'uint '(c-ptr))))
                           (lambda (mysql) (%ffi-call fn (list mysql)))))
  (set! %my-free-result (let ((fn (%ffi-make-fn %my-lib "mysql_free_result" 'void '(c-ptr))))
                           (lambda (res) (%ffi-call fn (list res)))))
  (set! %my-fetch-row (let ((fn (%ffi-make-fn %my-lib "mysql_fetch_row" 'c-ptr '(c-ptr))))
                         (lambda (res) (%ffi-call fn (list res)))))
  (set! %my-fetch-lengths (let ((fn (%ffi-make-fn %my-lib "mysql_fetch_lengths" 'c-ptr '(c-ptr))))
                             (lambda (res) (%ffi-call fn (list res)))))
  (set! %my-num-fields (let ((fn (%ffi-make-fn %my-lib "mysql_num_fields" 'uint '(c-ptr))))
                          (lambda (res) (%ffi-call fn (list res)))))
  (set! %my-fetch-field-direct (let ((fn (%ffi-make-fn %my-lib "mysql_fetch_field_direct" 'c-ptr '(c-ptr uint))))
                                  (lambda (res i) (%ffi-call fn (list res i)))))
  (set! %my-insert-id (let ((fn (%ffi-make-fn %my-lib "mysql_insert_id" 'uint64 '(c-ptr))))
                         (lambda (mysql) (%ffi-call fn (list mysql)))))
  (set! %my-affected-rows (let ((fn (%ffi-make-fn %my-lib "mysql_affected_rows" 'uint64 '(c-ptr))))
                             (lambda (mysql) (%ffi-call fn (list mysql)))))
  (set! %my-real-escape-string
    (let ((fn (%ffi-make-fn %my-lib "mysql_real_escape_string" 'uint64 '(c-ptr c-ptr c-ptr uint64))))
      (lambda (mysql to from len) (%ffi-call fn (list mysql to from len)))))
  (set! %my-ssl-set
    (let ((fn (%ffi-make-fn %my-lib "mysql_ssl_set" 'bool '(c-ptr c-string c-string c-string c-string c-string))))
      (lambda (mysql key cert ca capath cipher) (%ffi-call fn (list mysql key cert ca capath cipher))))))

;;; ── Raw pointer readers (see this module's own header comment) ─────────────

;; Reads a NUL-terminated C string starting at `ptr`, one byte at a
;; time, stopping the instant a zero byte is found -- never reads past
;; the string's own terminator.
(define (%c-string-at ptr)
  (if (or (not ptr) (cptr-null? ptr))
      #f
      (let ((out (open-output-bytevector)))
        (let loop ((i 0))
          (let ((b (bytevector-u8-ref (peek-bytes (+ (cptr-address ptr) i) 1) 0)))
            (if (zero? b)
                (utf8->string (get-output-bytevector out))
                (begin (write-u8 b out) (loop (+ i 1)))))))))

;; Reads exactly `len` bytes starting at `ptr` as a Scheme string
;; (used for column VALUES, where the byte length is already known
;; from mysql_fetch_lengths -- correct even for a value containing an
;; embedded NUL, unlike NUL-scanning).
(define (%string-at ptr len) (utf8->string (peek-bytes ptr len)))

;; Reads an 8-byte little-endian value at `ptr` as a fixnum -- used to
;; read one element of a char** (MYSQL_ROW) or unsigned long* (mysql_
;; fetch_lengths' own return) array, and to read MYSQL_FIELD's first
;; member (its `name` char*) directly from the struct's own address.
;; Little-endian is correct for every platform curry actually builds
;; for (x86_64/arm64) -- same rationale (curry zeromq)'s own manual
;; byte-packing comments give.
(define (%peek-u64-le addr)
  (let ((bv (peek-bytes addr 8)))
    (let loop ((i 7) (acc 0)) (if (< i 0) acc (loop (- i 1) (+ (* acc 256) (bytevector-u8-ref bv i)))))))

;; Reads a 4-byte little-endian value at `ptr` as a fixnum -- for
;; MYSQL_FIELD's `unsigned int` members (flags, type), which are half
;; the width of the `char*`/`unsigned long` members %peek-u64-le reads.
(define (%peek-u32-le addr)
  (let ((bv (peek-bytes addr 4)))
    (let loop ((i 3) (acc 0)) (if (< i 0) acc (loop (- i 1) (+ (* acc 256) (bytevector-u8-ref bv i)))))))

;;; ── Constants ────────────────────────────────────────────────────────────────

(define %default-port 3306)

;;; ── Structured errors (see (curry postgres)'s own equivalent section
;;; for the rationale; the design doc's §10 is what both are chasing).
;;; mysql_errno/mysql_sqlstate both read directly off the MYSQL*
;;; handle, no result object needed -- unlike Postgres's PGresult-
;;; scoped PQresultErrorField, every failure path here (including
;;; connect) has one available. ─────────────────────────────────────────────
(define-condition mariadb-error (error) #:fields (errno sqlstate))

(define (%my-raise mysql message)
  (condition-error 'mariadb-error
    (list (cons 'errno (%my-errno mysql)) (cons 'sqlstate (%my-sqlstate mysql)))
    message))

;;; ── Column type coercion (see (curry postgres)'s own equivalent
;;; section for why this exists at all: (curry sqlite)'s row_to_alist
;;; already returns fixnum/flonum/string/bytevector/#f per SQLite's own
;;; column type -- bare strings for everything here was the odd one out
;;; in (curry sql)'s cross-backend contract). MYSQL_FIELD.flags and
;;; .type are read by the same raw-offset trick this module's header
;;; already uses for .name, just two members further into the struct --
;;; verified against mariadb-connector-c 12.3.2's own mysql.h layout:
;;; 7 char* (name..def, 56 bytes) + 2 unsigned long (length, max_length,
;;; 16 bytes) + 7 unsigned int (name_length..def_length, 28 bytes) =
;;; offset 100 for flags, +4 for decimals (104), +4 for charsetnr (108),
;;; so type sits at offset 112. Both flags and type are `unsigned int`
;;; (4 bytes), read with %peek-u32-le, not %peek-u64-le. ─────────────────────
(define %MYSQL-FIELD-FLAGS-OFFSET 100)
(define %MYSQL-FIELD-TYPE-OFFSET 112)
(define %BINARY-FLAG #x80)

;; enum_field_types values (mariadb_com.h) this module coerces --
;; everything else (VARCHAR/STRING/DATE/DATETIME/JSON/ENUM/... ) stays
;; a plain string.
(define %MYSQL-TYPE-DECIMAL 0) (define %MYSQL-TYPE-TINY 1) (define %MYSQL-TYPE-SHORT 2)
(define %MYSQL-TYPE-LONG 3) (define %MYSQL-TYPE-FLOAT 4) (define %MYSQL-TYPE-DOUBLE 5)
(define %MYSQL-TYPE-LONGLONG 8) (define %MYSQL-TYPE-INT24 9) (define %MYSQL-TYPE-NEWDECIMAL 246)
(define %MYSQL-TYPE-TINY-BLOB 249) (define %MYSQL-TYPE-MEDIUM-BLOB 250)
(define %MYSQL-TYPE-LONG-BLOB 251) (define %MYSQL-TYPE-BLOB 252)

(define (%my-field-flags res i) (%peek-u32-le (+ (cptr-address (%my-fetch-field-direct res i)) %MYSQL-FIELD-FLAGS-OFFSET)))
(define (%my-field-type res i) (%peek-u32-le (+ (cptr-address (%my-fetch-field-direct res i)) %MYSQL-FIELD-TYPE-OFFSET)))

(define (%my-blob-type? ty)
  (or (= ty %MYSQL-TYPE-TINY-BLOB) (= ty %MYSQL-TYPE-MEDIUM-BLOB)
      (= ty %MYSQL-TYPE-LONG-BLOB) (= ty %MYSQL-TYPE-BLOB)))

;; (%my-coerce type flags ptr len) -- ptr/len are the raw value's own
;; address and byte length (from mysql_fetch_row/mysql_fetch_lengths);
;; never called for a NULL cell (the caller checks that first). A true
;; binary BLOB (BINARY_FLAG set -- MySQL's C API reuses the BLOB type
;; code for both TEXT and binary BLOB columns, BINARY_FLAG is the
;; documented way to tell them apart) is read as a raw bytevector,
;; skipping utf8->string entirely -- fixing a latent bug where binary
;; data that isn't valid UTF-8 would previously raise or corrupt
;; through %string-at's own utf8->string call.
(define (%my-coerce type flags ptr len)
  (cond
    ((and (%my-blob-type? type) (= (bitwise-and flags %BINARY-FLAG) %BINARY-FLAG))
     (peek-bytes ptr len))
    (else
      (let ((text (%string-at ptr len)))
        (cond
          ((or (= type %MYSQL-TYPE-TINY) (= type %MYSQL-TYPE-SHORT) (= type %MYSQL-TYPE-LONG)
               (= type %MYSQL-TYPE-LONGLONG) (= type %MYSQL-TYPE-INT24))
           (string->number text))
          ((or (= type %MYSQL-TYPE-DECIMAL) (= type %MYSQL-TYPE-FLOAT) (= type %MYSQL-TYPE-DOUBLE)
               (= type %MYSQL-TYPE-NEWDECIMAL))
           (exact->inexact (string->number text)))
          (else text))))))

;;; ── Connecting ───────────────────────────────────────────────────────────────

;; (my-connect config) -- config is an alist, e.g.
;;   '((host . "localhost") (port . 3306) (database . "app") (user . "app") (password . "secret"))
;; `database` (not `dbname`, matching MySQL's own terminology) selects
;; the initial schema; omit it to connect without one.
;;
;; TLS: any of ssl-key/ssl-cert/ssl-ca/ssl-capath/ssl-cipher present
;; (any combination -- all five are independently nullable in
;; mysql_ssl_set itself) turns on mysql_ssl_set before connecting and
;; ORs CLIENT_SSL into the connect flags. With none present, the
;; connection is plaintext, same as before this option existed --
;; unlike (curry postgres), which already gets TLS for free via
;; sslmode/sslcert/etc. passed straight through its own conninfo alist,
;; MySQL's C API needs this explicit, separate call.
(define %CLIENT-SSL #x0800)

(define (my-connect config)
  (%my-ensure!)
  (let* ((get (lambda (k default) (let ((p (assq k config))) (if p (cdr p) default))))
         (mysql (%my-init))
         (ssl? (or (assq 'ssl-key config) (assq 'ssl-cert config) (assq 'ssl-ca config)
                   (assq 'ssl-capath config) (assq 'ssl-cipher config))))
    (when (cptr-null? mysql) (error "mariadb: mysql_init failed (out of memory)"))
    (when ssl?
      (%my-ssl-set mysql (get 'ssl-key #f) (get 'ssl-cert #f) (get 'ssl-ca #f) (get 'ssl-capath #f) (get 'ssl-cipher #f)))
    (let ((result (%my-real-connect mysql
                                     (get 'host "localhost")
                                     (get 'user #f)
                                     (get 'password #f)
                                     (get 'database #f)
                                     (get 'port %default-port)
                                     #f    ; unix_socket
                                     (if ssl? %CLIENT-SSL 0))))  ; clientflag
      (when (cptr-null? result)
        ;; Read errno/sqlstate/message before mysql_close, which frees
        ;; the handle -- %my-raise itself never returns to run any
        ;; cleanup written after it (same reasoning as (curry
        ;; postgres)'s %pg-raise).
        (let ((msg (%my-error mysql))
              (errno (%my-errno mysql))
              (sqlstate (%my-sqlstate mysql)))
          (%my-close-raw mysql)
          (condition-error 'mariadb-error
            (list (cons 'errno errno) (cons 'sqlstate sqlstate))
            (string-append "mariadb: connection failed: " msg))))
      mysql)))

(define (my-connect? x) (c-ptr? x))
(define (my-close conn) (%my-close-raw conn))
(define (my-error conn) (%my-error conn))

;;; ── Running statements ───────────────────────────────────────────────────────

;; (%my-read-column-meta res) -> (values names types flags), one entry
;; per column, each read once per result rather than once per cell
;; (same reasoning as (curry postgres)'s own pg-exec). Shared by
;; my-exec's buffered path and my-query-stream's unbuffered one below.
(define (%my-read-column-meta res)
  (let ((ncols (%my-num-fields res)))
    (values
      (let loop ((i 0) (acc '()))
        (if (= i ncols)
            (reverse acc)
            (loop (+ i 1)
                  (cons (string->symbol (%c-string-at (make-cptr (%peek-u64-le (cptr-address (%my-fetch-field-direct res i))))))
                        acc))))
      (let loop ((i 0) (acc '())) (if (= i ncols) (reverse acc) (loop (+ i 1) (cons (%my-field-type res i) acc))))
      (let loop ((i 0) (acc '())) (if (= i ncols) (reverse acc) (loop (+ i 1) (cons (%my-field-flags res i) acc)))))))

;; (%my-read-one-row res ncols names types flags) -> row alist or #f at
;; end. Does NOT free `res` -- callers decide when (my-exec frees it as
;; soon as this returns #f; my-query-stream's caller frees it either at
;; end-of-stream or via an explicit my-stream-close, whichever comes
;; first).
(define (%my-read-one-row res ncols names types flags)
  (let ((row (%my-fetch-row res)))
    (if (cptr-null? row)
        #f
        (let ((lengths (%my-fetch-lengths res)))
          (let col-loop ((c 0) (cacc '()))
            (if (= c ncols)
                (reverse cacc)
                (let ((val-ptr (%peek-u64-le (+ (cptr-address row) (* c 8)))))
                  (col-loop (+ c 1)
                    (cons (cons (list-ref names c)
                                (if (zero? val-ptr)
                                    #f
                                    (%my-coerce (list-ref types c) (list-ref flags c)
                                                (make-cptr val-ptr)
                                                (%peek-u64-le (+ (cptr-address lengths) (* c 8))))))
                          cacc)))))))))

;; (my-exec conn sql) -> (values rows affected-rows) -- rows is a list
;; of alists (column-name symbol -> string or #f for SQL NULL);
;; affected-rows is an integer, meaningful for INSERT/UPDATE/DELETE
;; (0 for a SELECT, matching (curry postgres)'s own pg-exec convention).
(define (my-exec conn sql)
  (when (not (zero? (%my-query conn sql))) (%my-raise conn (string-append "mariadb: " (%my-error conn))))
  (let ((res (%my-store-result conn)))
    (if (cptr-null? res)
        ;; mysql_store_result returning NULL is ambiguous by itself:
        ;; either this statement genuinely has no result set (DDL/DML,
        ;; the common case) or something went wrong. mysql_field_count
        ;; disambiguates: 0 means "no result set was ever expected."
        (if (zero? (%my-field-count conn))
            (values '() (%my-affected-rows conn))
            (%my-raise conn (string-append "mariadb: " (%my-error conn))))
        (let-values (((names types flags) (%my-read-column-meta res)))
          (let ((ncols (%my-num-fields res)))
            (let row-loop ((racc '()))
              (let ((row (%my-read-one-row res ncols names types flags)))
                (if (not row)
                    (begin (%my-free-result res) (values (reverse racc) (%my-affected-rows conn)))
                    (row-loop (cons row racc))))))))))

;;; ── Streaming (see (curry sql)'s own sql-query-stream/sql-stream-
;;; next!/sql-stream-close! -- mysql_use_result leaves the result set
;;; unbuffered on the wire, fetching one row at a time on demand,
;;; instead of mysql_store_result's own "read the whole thing into
;;; memory first" behaviour my-exec uses. One real constraint, not
;;; enforced here but worth knowing: per mysql_use_result's own
;;; documented contract, a stream must be fully drained (my-stream-next
;;; returning #f) or explicitly closed (my-stream-close) before another
;;; query runs on the same connection. ─────────────────────────────────────────

(define-record-type <my-stream>
  (%make-my-stream res conn ncols names types flags freed?-box)
  my-stream?
  (res    %my-stream-res)
  (conn   %my-stream-conn)
  (ncols  %my-stream-ncols)
  (names  %my-stream-names)
  (types  %my-stream-types)
  (flags  %my-stream-flags)
  (freed?-box %my-stream-freed?-box))

;; (my-query-stream conn sql) -> a stream handle for my-stream-next/
;; my-stream-close. DDL/DML (no result set at all) still returns a
;; valid, immediately-exhausted stream rather than erroring, so a
;; caller doesn't need to special-case statement kind up front.
(define (my-query-stream conn sql)
  (when (not (zero? (%my-query conn sql))) (%my-raise conn (string-append "mariadb: " (%my-error conn))))
  (let ((res (%my-use-result conn)))
    (if (cptr-null? res)
        (if (zero? (%my-field-count conn))
            (%make-my-stream (cptr-null) conn 0 '() '() '() (list #t))
            (%my-raise conn (string-append "mariadb: " (%my-error conn))))
        (let-values (((names types flags) (%my-read-column-meta res)))
          (%make-my-stream res conn (%my-num-fields res) names types flags (list #f))))))

;; (my-stream-next stream) -> row alist or #f at end. Frees the
;; underlying MYSQL_RES the instant it's exhausted, same as my-exec.
(define (my-stream-next stream)
  (if (car (%my-stream-freed?-box stream))
      #f
      (let ((row (%my-read-one-row (%my-stream-res stream) (%my-stream-ncols stream)
                                    (%my-stream-names stream) (%my-stream-types stream) (%my-stream-flags stream))))
        (unless row
          (%my-free-result (%my-stream-res stream))
          (set-car! (%my-stream-freed?-box stream) #t))
        row)))

;; (my-stream-close stream) -- safe to call whether or not the stream
;; was already fully drained (my-stream-next already frees on
;; exhaustion; this guards against a double mysql_free_result, which
;; is undefined behaviour).
(define (my-stream-close stream)
  (unless (car (%my-stream-freed?-box stream))
    (%my-free-result (%my-stream-res stream))
    (set-car! (%my-stream-freed?-box stream) #t)))

;;; ── Escaping (used by (curry sql)'s escape-and-splice strategy for
;;; parameters, since this module never builds a MYSQL_BIND struct
;;; array -- see this module's own header). ───────────────────────────────────

;; (my-escape-literal conn value) -> a quoted+escaped SQL string
;; literal, e.g. "O'Brien" -> "'O\\'Brien'". mysql_real_escape_string
;; itself only escapes special characters -- it does NOT add the
;; surrounding quotes the way PostgreSQL's PQescapeLiteral does, so
;; this module adds them explicitly.
(define (my-escape-literal conn value) (string-append "'" (%my-escape-bytes conn (string->utf8 value)) "'"))

;; (my-escape-bytea conn bv) -> a quoted SQL string literal containing
;; the raw bytes of `bv`, escaped the same binary-safe way as any
;; other string (mysql_real_escape_string is documented to be safe for
;; arbitrary binary data given an explicit length, which this module
;; always provides -- there is no separate bytea-specific escape
;; function in the MySQL C API the way libpq has PQescapeByteaConn).
(define (my-escape-bytea conn bv) (string-append "'" (%my-escape-bytes conn bv) "'"))

(define (%my-escape-bytes conn bv)
  (let ((to (make-bytevector (+ (* 2 (bytevector-length bv)) 1) 0)))
    (with-pinned-bytevector bv from-ptr
      (with-pinned-bytevector to to-ptr
        (let ((written (%my-real-escape-string conn to-ptr from-ptr (bytevector-length bv))))
          (utf8->string (peek-bytes to-ptr written)))))))

;;; ── Last insert id ───────────────────────────────────────────────────────────

;; (my-last-insert-id conn) -> integer. Unlike PostgreSQL, MariaDB has
;; a genuine, session-scoped "ID of the last row this connection
;; inserted with an AUTO_INCREMENT column" -- no sequence-name argument
;; needed, matching SQLite's own sqlite-last-insert-rowid.
(define (my-last-insert-id conn) (%my-insert-id conn))

  )) ;; end begin, define-library
