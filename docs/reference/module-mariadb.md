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

### `(my-connect? x)` → boolean
### `(my-close conn)`
### `(my-error conn)` → string

## Running statements

### `(my-exec conn sql)` → (values rows affected-rows)

`rows` is a list of alists (column-name symbol → string, or `#f` for SQL `NULL`); `affected-rows` is an integer (0 for a `SELECT`, matching `(curry postgres)`'s `pg-exec` convention). No parameterized-query protocol is used — see the module's own header for why; combine with `my-escape-literal`/`my-escape-bytea` to build safe query text yourself, or use `(curry sql)` for `?`-placeholder support.

Column-name and row-value extraction reads raw `MYSQL_FIELD`/`MYSQL_ROW` C struct memory directly (no FFI struct binding) — see the module's own comments for the exact offsets relied on.

## Escaping

Used by `(curry sql)`'s escape-and-splice parameter strategy (this module never builds a `MYSQL_BIND` struct array).

### `(my-escape-literal conn value)` → string

A quoted+escaped SQL string literal, e.g. `"O'Brien"` → `"'O\\'Brien'"`. `mysql_real_escape_string` itself only escapes special characters — it does not add the surrounding quotes the way PostgreSQL's `PQescapeLiteral` does, so this adds them explicitly. Needs an already-connected handle (reads the connection's charset).

### `(my-escape-bytea conn bv)` → string

The same, for a bytevector's raw bytes — binary-safe, since `mysql_real_escape_string` is given an explicit length.

## Last insert id

### `(my-last-insert-id conn)` → integer

Via `mysql_insert_id` — a genuine, session-scoped "ID of the last row this connection inserted with an `AUTO_INCREMENT` column." No sequence-name concept needed, unlike PostgreSQL.

## Notes

- This module makes no attempt to be a general-purpose MariaDB client covering every C API surface — it exists to satisfy `(curry sql)`'s driver protocol, with everything else kept minimal.
- `my-escape-literal`/`my-escape-bytea` need an already-connected handle; they cannot be exercised without one.

## See also

- [`module-sql.md`](module-sql.md) — the cross-database layer this module is a backend for
- [`module-postgres.md`](module-postgres.md) — the equivalent PostgreSQL backend
- [`docs/thoughts/sql-abstraction-design.md`](../thoughts/sql-abstraction-design.md) — the full design rationale
