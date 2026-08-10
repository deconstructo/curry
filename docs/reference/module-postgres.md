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

`rows` is a list of alists (column-name symbol → string, or `#f` for SQL `NULL`); `affected-rows` comes from `PQcmdTuples`. No parameterized-query protocol (`PQexecParams`) is used — see the module's own header for why; combine with `pg-escape-literal`/`pg-escape-bytea` to build safe query text yourself, or use `(curry sql)` for `?`-placeholder support. Results are always read in libpq's default TEXT format.

Unlike `(curry mariadb)`, this module never has to peek a raw C struct's field offsets — `PQfname`/`PQgetvalue`/`PQgetisnull` are ordinary string/int-returning functions.

## Escaping

Used by `(curry sql)`'s escape-and-splice parameter strategy (this module never builds a `PQexecParams` argument array).

### `(pg-escape-literal conn value)` → string

A fully quoted+escaped SQL literal, e.g. `"O'Brien"` → `"'O''Brien'"`. Needs an already-connected handle (reads the connection's client encoding). Frees libpq's own malloc'd buffer before returning.

### `(pg-escape-bytea conn bv)` → string

A quoted PostgreSQL `bytea` literal in libpq's own hex-escape format, for a bytevector's raw bytes.

## Last insert id

### `(pg-last-insert-id conn [sequence-name])` → integer

PostgreSQL has no connection-independent "ID of the row I just inserted." With no `sequence-name`: `SELECT lastval()` — the last value produced by *any* sequence in this session; raises if none has been used yet. With a `sequence-name`: `SELECT currval(sequence-name)` — that specific sequence's own last value in this session; still raises if that sequence hasn't been used yet. An `INSERT ... RETURNING id` is the genuinely idiomatic PostgreSQL alternative — see [`docs/thoughts/sql-abstraction-design.md`](../thoughts/sql-abstraction-design.md) §9.

## Notes

- This module makes no attempt to be a general-purpose libpq client covering every C API surface — it exists to satisfy `(curry sql)`'s driver protocol, with everything else kept minimal.
- `pg-escape-literal`/`pg-escape-bytea`/`pg-last-insert-id` need an already-connected handle; they cannot be exercised without one.

## See also

- [`module-sql.md`](module-sql.md) — the cross-database layer this module is a backend for
- [`module-mariadb.md`](module-mariadb.md) — the equivalent MariaDB/MySQL backend
- [`docs/thoughts/sql-abstraction-design.md`](../thoughts/sql-abstraction-design.md) — the full design rationale
