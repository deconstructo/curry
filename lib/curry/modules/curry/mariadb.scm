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
  (import (scheme base) (curry ffi))
  (export
    my-connect my-connect? my-close
    my-exec my-escape-literal my-escape-bytea
    my-last-insert-id my-error)
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
(define %my-query #f) (define %my-store-result #f) (define %my-field-count #f) (define %my-free-result #f)
(define %my-fetch-row #f) (define %my-fetch-lengths #f) (define %my-num-fields #f) (define %my-fetch-field-direct #f)
(define %my-insert-id #f) (define %my-affected-rows #f) (define %my-real-escape-string #f)

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
  (set! %my-query (let ((fn (%ffi-make-fn %my-lib "mysql_query" 'int '(c-ptr c-string))))
                     (lambda (mysql sql) (%ffi-call fn (list mysql sql)))))
  (set! %my-store-result (let ((fn (%ffi-make-fn %my-lib "mysql_store_result" 'c-ptr '(c-ptr))))
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
      (lambda (mysql to from len) (%ffi-call fn (list mysql to from len))))))

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

;;; ── Constants ────────────────────────────────────────────────────────────────

(define %default-port 3306)

;;; ── Connecting ───────────────────────────────────────────────────────────────

;; (my-connect config) -- config is an alist, e.g.
;;   '((host . "localhost") (port . 3306) (database . "app") (user . "app") (password . "secret"))
;; `database` (not `dbname`, matching MySQL's own terminology) selects
;; the initial schema; omit it to connect without one.
(define (my-connect config)
  (%my-ensure!)
  (let* ((get (lambda (k default) (let ((p (assq k config))) (if p (cdr p) default))))
         (mysql (%my-init)))
    (when (cptr-null? mysql) (error "mariadb: mysql_init failed (out of memory)"))
    (let ((result (%my-real-connect mysql
                                     (get 'host "localhost")
                                     (get 'user #f)
                                     (get 'password #f)
                                     (get 'database #f)
                                     (get 'port %default-port)
                                     #f    ; unix_socket
                                     0)))  ; clientflag
      (when (cptr-null? result)
        (let ((msg (%my-error mysql)))
          (%my-close-raw mysql)
          (error (string-append "mariadb: connection failed: " msg))))
      mysql)))

(define (my-connect? x) (c-ptr? x))
(define (my-close conn) (%my-close-raw conn))
(define (my-error conn) (%my-error conn))

;;; ── Running statements ───────────────────────────────────────────────────────

;; (my-exec conn sql) -> (values rows affected-rows) -- rows is a list
;; of alists (column-name symbol -> string or #f for SQL NULL);
;; affected-rows is an integer, meaningful for INSERT/UPDATE/DELETE
;; (0 for a SELECT, matching (curry postgres)'s own pg-exec convention).
(define (my-exec conn sql)
  (when (not (zero? (%my-query conn sql))) (error (string-append "mariadb: " (%my-error conn))))
  (let ((res (%my-store-result conn)))
    (if (cptr-null? res)
        ;; mysql_store_result returning NULL is ambiguous by itself:
        ;; either this statement genuinely has no result set (DDL/DML,
        ;; the common case) or something went wrong. mysql_field_count
        ;; disambiguates: 0 means "no result set was ever expected."
        (if (zero? (%my-field-count conn))
            (values '() (%my-affected-rows conn))
            (error (string-append "mariadb: " (%my-error conn))))
        (let* ((ncols (%my-num-fields res))
               (names (let loop ((i 0) (acc '()))
                        (if (= i ncols)
                            (reverse acc)
                            (loop (+ i 1)
                                  (cons (string->symbol (%c-string-at (make-cptr (%peek-u64-le (cptr-address (%my-fetch-field-direct res i))))))
                                        acc)))))
               (rows
                 (let row-loop ((racc '()))
                   (let ((row (%my-fetch-row res)))
                     (if (cptr-null? row)
                         (begin (%my-free-result res) (reverse racc))
                         (let ((lengths (%my-fetch-lengths res)))
                           (row-loop
                             (cons
                               (let col-loop ((c 0) (cacc '()))
                                 (if (= c ncols)
                                     (reverse cacc)
                                     (let ((val-ptr (%peek-u64-le (+ (cptr-address row) (* c 8)))))
                                       (col-loop (+ c 1)
                                         (cons (cons (list-ref names c)
                                                     (if (zero? val-ptr)
                                                         #f
                                                         (%string-at (make-cptr val-ptr) (%peek-u64-le (+ (cptr-address lengths) (* c 8))))))
                                               cacc)))))
                               racc)))))))
               )
          (values rows (%my-affected-rows conn))))))

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
