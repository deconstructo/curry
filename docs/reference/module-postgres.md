# Module: (curry postgres)

*unreleased*

PostgreSQL client, via `(curry ffi)`. `libpq` is dlopen'd lazily at runtime (not linked at build time — no CMake flag beyond `-DBUILD_FFI=ON`), the same pattern as `(curry hdf5)`/`(curry ncurses)`/`(curry graphviz)`/`(curry zeromq)`. Written to satisfy `(curry sql)`'s `<sql-driver>` protocol (see [`module-sql.md`](module-sql.md) and [`docs/thoughts/sql-abstraction-design.md`](../thoughts/sql-abstraction-design.md)); its procedures are also plain, independently usable PostgreSQL bindings on their own.

Install: `brew install postgresql` (macOS, ships `libpq`), `apt install libpq-dev` (Debian/Ubuntu), `dnf install libpq-devel` (Fedora/RHEL).

## Import

```scheme
(import (curry postgres))
```

## Connecting

### `(pg-connect config)` → connection handle

`config` is an alist of libpq conninfo keys, e.g. `'((host . "localhost") (port . 5432) (dbname . "app") (user . "app") (password . "secret"))`. Every key is written straight through as `key=value` — any conninfo key libpq itself understands (`host`, `hostaddr`, `port`, `dbname`, `user`, `password`, `connect_timeout`, `sslmode`, ...) works without this module needing to know about it specifically. Raises with `PQerrorMessage`'s own text on failure.

### `(pg-connect? x)` → boolean
### `(pg-close conn)`
### `(pg-error conn)` → string

## Running statements

### `(pg-exec conn sql)` → (values rows affected-rows)

`rows` is a list of alists (column-name symbol → value); `affected-rows` comes from `PQcmdTuples`. No parameterized-query protocol (`PQexecParams`) is used — see the module's own header for why; combine with `pg-escape-literal`/`pg-escape-bytea` to build safe query text yourself, or use `(curry sql)` for `?`-placeholder support. Results are always read in libpq's default TEXT format.

Unlike `(curry mariadb)`, this module never has to peek a raw C struct's field offsets for column *names* — `PQfname`/`PQgetvalue`/`PQgetisnull` are ordinary string/int-returning functions.

### Column value types

Every value is coerced from libpq's own OID (`PQftype`) to a native Scheme type, matching `(curry sqlite)`'s own row shape (fixnum/flonum/string/bytevector/`#f`) rather than returning bare strings for everything:

| Postgres type (OID) | Scheme value |
|---|---|
| `boolean` (16) | `#t`/`#f` |
| `int2`/`int4`/`int8`/`oid` (21/23/20/26) | exact integer |
| `real`/`double precision`/`numeric` (700/701/1700) | flonum (`numeric` loses precision beyond a double — a known, accepted lossy conversion) |
| `bytea` (17) | bytevector (via `PQunescapeBytea`, handling both the modern hex and legacy escape text formats transparently) |
| everything else (text, varchar, date, timestamp, json, uuid, ...) | string |

A SQL `NULL` is always `#f`, regardless of column type.

## Errors

### `postgres-error` condition (fields: `sqlstate`)

`pg-connect` and `pg-exec` raise a `'postgres-error` condition (via `(curry conditions)`) instead of a plain error, carrying the Postgres SQLSTATE code (`PQresultErrorField`'s `PG_DIAG_SQLSTATE`) as a field — so a caller can dispatch on the code rather than pattern-matching the message text:

```scheme
(import (curry postgres) (curry conditions))
(guard (e (#t (display (condition-field e 'sqlstate)) (newline)))
  (pg-exec conn "SELECT * FROM missing_table"))
; => 42P01
```

A connection-level failure (e.g. a refused TCP connection, before any `PGresult` exists) has no SQLSTATE to read, so the field is `#f` in that case — a real absence, not an unread value. `pg-error` (the plain last-error-message accessor) is unchanged and still available.

## Escaping

Used by `(curry sql)`'s escape-and-splice parameter strategy (this module never builds a `PQexecParams` argument array).

### `(pg-escape-literal conn value)` → string

A fully quoted+escaped SQL literal, e.g. `"O'Brien"` → `"'O''Brien'"`. Needs an already-connected handle (reads the connection's client encoding). Frees libpq's own malloc'd buffer before returning.

### `(pg-escape-bytea conn bv)` → string

A quoted PostgreSQL `bytea` literal in libpq's own hex-escape format, for a bytevector's raw bytes.

## Last insert id

### `(pg-last-insert-id conn [sequence-name])` → integer

PostgreSQL has no connection-independent "ID of the row I just inserted." With no `sequence-name`: `SELECT lastval()` — the last value produced by *any* sequence in this session; raises if none has been used yet. With a `sequence-name`: `SELECT currval(sequence-name)` — that specific sequence's own last value in this session; still raises if that sequence hasn't been used yet. An `INSERT ... RETURNING id` is the genuinely idiomatic PostgreSQL alternative — see [`docs/thoughts/sql-abstraction-design.md`](../thoughts/sql-abstraction-design.md) §9.

## Streaming

Two independent ways to page through a result set too large to comfortably hold in memory at once — see `(curry sql)`'s own [`sql-query-stream`](module-sql.md#streaming) for the portable, cross-backend entry point most callers should reach for first.

### `(pg-cursor-open conn sql)` → cursor handle
### `(pg-cursor-fetch cursor)` → row alist or `#f`
### `(pg-cursor-close cursor)`

Portable SQL-cursor streaming: `DECLARE ... CURSOR WITH HOLD` / `FETCH 1` / `CLOSE`, all plain SQL text through `pg-exec` — no new libpq bindings at all. `WITH HOLD` specifically so the cursor survives outside an explicit transaction. This is the path `(curry sql)`'s own `sql-query-stream` wires up for a postgres connection.

### `(pg-stream-open conn sql)` → async stream handle
### `(pg-stream-next stream)` → row alist or `#f`
### `(pg-stream-close stream)`

A Postgres-only extra, *not* used by `(curry sql)`: native async single-row mode (`PQsendQuery` + `PQsetSingleRowMode` + `PQgetResult`), skipping the cursor's own `FETCH` round-trip. Real tradeoff: this ties up the connection for other use until the stream is fully drained or closed, where a cursor's own `FETCH`es don't. Closing an undrained stream drains every remaining `PQgetResult` first — required by libpq's own async protocol.

## LISTEN/NOTIFY

A Postgres-only extra with no MariaDB/SQLite equivalent.

### `(pg-listen conn channel)`
### `(pg-unlisten conn channel)`
### `(pg-notify conn channel payload)`
### `(pg-notifications conn)` → list of `(channel pid . payload)`

`channel` is escaped as a SQL identifier (`PQescapeIdentifier`, double-quote escaping) since `LISTEN`/`NOTIFY` take an identifier, not a string literal; `payload` goes through `pg-escape-literal`. `pg-notifications` is poll-based (`PQconsumeInput` + drain `PQnotifies`), not a blocking wait — a caller wanting to block until a notification arrives needs their own wait on the connection's socket, out of scope here (this module has no socket-level primitives at all, consistent with never linking libpq at build time).

```scheme
(pg-listen conn "orders")
;; ... elsewhere, another connection:
(pg-notify other-conn "orders" "order-42-shipped")
;; back on the listening connection, any time after:
(pg-notifications conn) ; => (("orders" 12345 . "order-42-shipped"))
```

## COPY

A Postgres-only extra for bulk load/unload, no MariaDB/SQLite equivalent.

### `(pg-copy-from-start conn sql)`
### `(pg-copy-data conn bytes)`
### `(pg-copy-end conn)`

Bulk load: `sql` is a full `COPY ... FROM STDIN [...]` statement. `bytes` is a bytevector, or a string (UTF-8 encoded first). `pg-copy-end` drains the connection's remaining results before returning.

### `(pg-copy-to-start conn sql)`
### `(pg-copy-fetch conn)` → bytevector chunk or `#f` at end

Bulk unload: `sql` is a full `COPY ... TO STDOUT [...]` statement; `pg-copy-fetch` returns `#f` once the connection's remaining results have been drained.

```scheme
(pg-copy-from-start conn "COPY people (name, age) FROM STDIN WITH (FORMAT csv)")
(pg-copy-data conn "Alice,30\n")
(pg-copy-data conn "Bob,25\n")
(pg-copy-end conn)
```

## Notes

- This module makes no attempt to be a general-purpose libpq client covering every C API surface — it exists to satisfy `(curry sql)`'s driver protocol, with everything else kept minimal.
- `pg-escape-literal`/`pg-escape-bytea`/`pg-last-insert-id` need an already-connected handle; they cannot be exercised without one.

## See also

- [`module-sql.md`](module-sql.md) — the cross-database layer this module is a backend for
- [`module-mariadb.md`](module-mariadb.md) — the equivalent MariaDB/MySQL backend
- [`docs/thoughts/sql-abstraction-design.md`](../thoughts/sql-abstraction-design.md) — the full design rationale
