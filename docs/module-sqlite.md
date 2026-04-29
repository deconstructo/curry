# Module: (curry sqlite)

SQLite3 database access.

## Installation

```bash
# Debian/Ubuntu
sudo apt install libsqlite3-dev

# macOS
brew install sqlite
```

Enabled by `-DBUILD_MODULE_SQLITE=ON` (default). Skipped silently if SQLite3 is not found.

## Import

```scheme
(import (curry sqlite))
```

## Procedures

### Opening and closing

```scheme
(sqlite-open path)          ; open or create a database file
(sqlite-open-memory)        ; open an in-memory database
(sqlite-close db)           ; close the database
```

### Simple execution

```scheme
(sqlite-exec db sql)        ; execute SQL; returns list of rows (each row is a list)
```

`sqlite-exec` is convenient for DDL and simple queries. Each row is a list of values; column values are strings, integers, or `#f` for NULL.

### Prepared statements

```scheme
(sqlite-prepare db sql)     ; compile SQL, return statement handle
(sqlite-bind stmt index val); bind parameter (1-based index)
(sqlite-step stmt)          ; step: returns next row as list, or #f when done
(sqlite-finalize stmt)      ; release the statement
```

Bind values: string, integer, flonum, or `#f` (NULL). Parameters in SQL are `?` or `?N`.

### Metadata

```scheme
(sqlite-last-insert-rowid db)  ; integer rowid of last INSERT
(sqlite-changes db)            ; number of rows changed by last statement
```

## Examples

```scheme
(import (curry sqlite))

; Create and populate
(define db (sqlite-open "/tmp/test.db"))
(sqlite-exec db "CREATE TABLE IF NOT EXISTS people (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)")
(sqlite-exec db "INSERT INTO people (name, age) VALUES ('Alice', 30)")
(sqlite-exec db "INSERT INTO people (name, age) VALUES ('Bob', 25)")

; Simple query
(sqlite-exec db "SELECT * FROM people")
; => (("1" "Alice" "30") ("2" "Bob" "25"))

; Prepared statement with parameters
(define stmt (sqlite-prepare db "SELECT name FROM people WHERE age > ?"))
(sqlite-bind stmt 1 27)
(let loop ((row (sqlite-step stmt)))
  (when row
    (display (car row)) (newline)
    (loop (sqlite-step stmt))))
(sqlite-finalize stmt)

(sqlite-close db)
```

## Transaction pattern

```scheme
(sqlite-exec db "BEGIN")
(sqlite-exec db "INSERT INTO ...")
(sqlite-exec db "UPDATE ...")
(sqlite-exec db "COMMIT")
; or (sqlite-exec db "ROLLBACK") on error
```

## Notes

- All column values from `sqlite-exec` and `sqlite-step` are Scheme strings (SQLite returns text by default via the C API). Cast as needed: `(string->number (car row))`.
- For high-performance bulk inserts, use a prepared statement inside a transaction.
- In-memory databases are lost when the handle is closed.
