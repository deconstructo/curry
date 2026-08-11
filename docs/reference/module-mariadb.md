# Module: (curry mariadb)

*unreleased*

MariaDB/MySQL client, via `(curry ffi)`. `libmariadb`/`libmysqlclient` is dlopen'd lazily at runtime (not linked at build time — no CMake flag beyond `-DBUILD_FFI=ON`), the same pattern as `(curry hdf5)`/`(curry ncurses)`/`(curry graphviz)`/`(curry zeromq)`. Written to satisfy `(curry sql)`'s `<sql-driver>` protocol (see [`module-sql.md`](module-sql.md) and [`docs/thoughts/sql-abstraction-design.md`](../thoughts/sql-abstraction-design.md)); its procedures are also plain, independently usable MariaDB/MySQL bindings on their own.

Install: `brew install mariadb` (macOS), `apt install libmariadb-dev` (Debian/Ubuntu), `dnf install mariadb-connector-c-devel` (Fedora/RHEL).

## Import

```scheme
(import (curry mariadb))
```

## Connecting

### `(my-connect config)` → connection handle

`config` is an alist, e.g. `'((host . "localhost") (port . 3306) (database . "app") (user . "app") (password . "secret"))`. `database` (not `dbname`, matching MySQL's own terminology) selects the initial schema; omit it to connect without one. Raises with `mysql_error`'s own message on failure.

TLS: add any of `ssl-key`/`ssl-cert`/`ssl-ca`/`ssl-capath`/`ssl-cipher` (any combination — all independently optional, matching `mysql_ssl_set`'s own signature) to turn on TLS via `mysql_ssl_set` and `CLIENT_SSL`; with none present, the connection stays plaintext. `(curry postgres)` already gets TLS for free via `sslmode`/`sslcert`/etc. in its own conninfo alist — MySQL's C API needs this explicit, separate option instead.

Server certificate verification (`MYSQL_OPT_SSL_VERIFY_SERVER_CERT`) defaults to **on** whenever any of the above TLS keys is present — `mysql_ssl_set` alone only encrypts the connection, it does not by itself confirm the server presenting the certificate is the intended one. Pass `(ssl-verify-cert . #f)` explicitly to opt out (e.g. a self-signed dev server with no `ssl-ca` configured); this is a deliberate, named opt-out, not a silent default.

### `(my-connect? x)` → boolean
### `(my-close conn)`
### `(my-error conn)` → string

## Running statements

### `(my-exec conn sql)` → (values rows affected-rows)

`rows` is a list of alists (column-name symbol → value); `affected-rows` is an integer (0 for a `SELECT`, matching `(curry postgres)`'s `pg-exec` convention). No parameterized-query protocol is used — see the module's own header for why; combine with `my-escape-literal`/`my-escape-bytea` to build safe query text yourself, or use `(curry sql)` for `?`-placeholder support.

Column-name and row-value extraction reads raw `MYSQL_FIELD`/`MYSQL_ROW` C struct memory directly (no FFI struct binding) — see the module's own comments for the exact offsets relied on.

### Column value types

Every value is coerced from `MYSQL_FIELD.type`/`.flags` to a native Scheme type, matching `(curry sqlite)`'s own row shape (fixnum/flonum/string/bytevector/`#f`) rather than returning bare strings for everything:

| MySQL type | Scheme value |
|---|---|
| `TINYINT`/`SMALLINT`/`INT`/`BIGINT`/`MEDIUMINT` | exact integer |
| `DECIMAL`/`FLOAT`/`DOUBLE`/`NEWDECIMAL` | flonum |
| `BLOB`/`TINYBLOB`/`MEDIUMBLOB`/`LONGBLOB` *with* the `BINARY_FLAG` set | bytevector (a true binary column — `BINARY_FLAG` is how MySQL's C API distinguishes a binary `BLOB` from `TEXT`, which shares the same underlying type code) |
| everything else (`VARCHAR`, `TEXT`-flavored `BLOB` types, `DATE`, `DATETIME`, `JSON`, `ENUM`, ...) | string |

A SQL `NULL` is always `#f`, regardless of column type. This also fixes a latent bug: previously every column (including true binary `BLOB` data) was decoded with `utf8->string`, which would raise or corrupt on bytes that aren't valid UTF-8.

## Streaming

### `(my-query-stream conn sql)` → stream handle
### `(my-stream-next stream)` → row alist or `#f`
### `(my-stream-close stream)`

Unbuffered result fetching via `mysql_use_result` (instead of `my-exec`'s own `mysql_store_result`) — rows come off the wire one at a time rather than being read entirely into memory up front. See `(curry sql)`'s own [`sql-query-stream`](module-sql.md#streaming) for the portable, cross-backend entry point most callers should reach for first; this is the path it wires up for a mariadb connection. One real constraint: a stream must be fully drained (`my-stream-next` returning `#f`) or explicitly closed before another query runs on the same connection — `mysql_use_result`'s own documented contract.

## Escaping

Used by `(curry sql)`'s escape-and-splice parameter strategy (this module never builds a `MYSQL_BIND` struct array).

### `(my-escape-literal conn value)` → string

A quoted+escaped SQL string literal, e.g. `"O'Brien"` → `"'O\\'Brien'"`. `mysql_real_escape_string` itself only escapes special characters — it does not add the surrounding quotes the way PostgreSQL's `PQescapeLiteral` does, so this adds them explicitly. Needs an already-connected handle (reads the connection's charset).

### `(my-escape-bytea conn bv)` → string

The same, for a bytevector's raw bytes — binary-safe, since `mysql_real_escape_string` is given an explicit length.

## Last insert id

### `(my-last-insert-id conn)` → integer

Via `mysql_insert_id` — a genuine, session-scoped "ID of the last row this connection inserted with an `AUTO_INCREMENT` column." No sequence-name concept needed, unlike PostgreSQL.

## Errors

### `mariadb-error` condition (fields: `errno`, `sqlstate`)

`my-connect` and `my-exec` raise a `'mariadb-error` condition (via `(curry conditions)`) instead of a plain error, carrying both `mysql_errno`'s numeric code and `mysql_sqlstate`'s own SQLSTATE text as fields — so a caller can dispatch on either rather than pattern-matching the message text:

```scheme
(import (curry mariadb) (curry conditions))
(guard (e (#t (display (condition-field e 'errno)) (display " ") (display (condition-field e 'sqlstate)) (newline)))
  (my-exec conn "SELECT * FROM missing_table"))
; => 1146 42S02
```

Unlike Postgres (where a connection-level failure has no SQLSTATE to read at all), both fields read directly off the `MYSQL*` handle and are available for every failure path, including a failed connect. `my-error` (the plain last-error-message accessor) is unchanged and still available.

## Notes

- This module makes no attempt to be a general-purpose MariaDB client covering every C API surface — it exists to satisfy `(curry sql)`'s driver protocol, with everything else kept minimal.
- `my-escape-literal`/`my-escape-bytea` need an already-connected handle; they cannot be exercised without one.

## See also

- [`module-sql.md`](module-sql.md) — the cross-database layer this module is a backend for
- [`module-postgres.md`](module-postgres.md) — the equivalent PostgreSQL backend
- [`docs/thoughts/sql-abstraction-design.md`](../thoughts/sql-abstraction-design.md) — the full design rationale
