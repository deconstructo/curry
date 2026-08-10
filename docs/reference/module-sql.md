# Module: (curry sql)

*unreleased*

A Scheme-native cross-database layer over `(curry sqlite)`, `(curry mariadb)`, and `(curry postgres)`. See [`docs/thoughts/sql-abstraction-design.md`](../thoughts/sql-abstraction-design.md) for the full design rationale (a critique of PHP's PDO and what a Lisp-native version of the same idea looks like); this module is that design.

## Import

```scheme
(import (curry sql))
```

## Why this exists, in short

A caller always writes `?` placeholders and always gets rows back as alists keyed by column-name symbol — the same shape regardless of which backend a connection actually is, matching `(curry sqlite)`'s own existing convention exactly (this module makes no changes to `(curry sqlite)` itself). Adding a future backend means writing one more internal driver value; nothing about this module's own API changes.

## Connecting

### `(sql-connect kind config)` → connection

`kind` is `'sqlite`, `'mariadb`, or `'postgres`. For `'sqlite`, `config` is a path string, or the literal symbol `':memory:` for an in-memory database. For `'mariadb`/`'postgres`, `config` is an alist of connection parameters passed straight through to `(curry mariadb)`'s `my-connect`/`(curry postgres)`'s `pg-connect` — see those modules' own docs for their accepted keys (`host`, `port`, `user`, `password`, and `database`/`dbname` respectively).

```scheme
(import (curry sql))
(define conn (sql-connect 'sqlite "/path/to.db"))
(define mem  (sql-connect 'sqlite ':memory:))
(define pg   (sql-connect 'postgres '((host . "localhost") (dbname . "app") (user . "app"))))
(define my   (sql-connect 'mariadb '((host . "localhost") (database . "app") (user . "app"))))
```

### `(sql-connection? x)` → boolean
### `(sql-connection-kind conn)` → symbol
### `(sql-close conn)`

## Running statements

### `(sql-exec conn sql . params)` → affected-row count

For `INSERT`/`UPDATE`/`DELETE`/`CREATE TABLE`/etc. `params` are substituted into `?` placeholders, left to right.

### `(sql-query conn sql . params)` → list of row alists

For `SELECT`. Each row is an alist: `((column-name . value) ...)`, column names as symbols.

### `(sql-query-one conn sql . params)` → alist or `#f`

The first row, or `#f` if the query returned none — saves the common `(let ((rows (sql-query ...))) (and (pair? rows) (car rows)))` dance.

```scheme
(import (curry sql))
(define conn (sql-connect 'sqlite ':memory:))
(sql-exec conn "CREATE TABLE people (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)")
(sql-exec conn "INSERT INTO people (name, age) VALUES (?, ?)" "Alice" 30)
(sql-query conn "SELECT * FROM people WHERE age > ?" 20)
; => (((id . 1) (name . "Alice") (age . 30)))
(sql-query-one conn "SELECT * FROM people WHERE name = ?" "Nobody")
; => #f
```

A `#f` parameter binds as SQL `NULL`.

## Transactions

### `(sql-begin! conn)` / `(sql-commit! conn)` / `(sql-rollback! conn)`

Manual control, for the rare case that genuinely needs it. Implemented as plain `BEGIN`/`COMMIT`/`ROLLBACK` SQL text — the one strategy that needs nothing backend-specific at all, and already SQLite's own documented pattern.

### `(sql-in-transaction? conn)` → boolean

### `(sql-with-transaction conn thunk)`

The one nearly everyone should use instead: begins a transaction, calls `(thunk)`, commits on a normal return, and rolls back and re-raises on any escape (an error, an escaping continuation) — via `dynamic-wind`, so the rollback happens even if `thunk` doesn't fail cleanly through an ordinary `guard`. Returns `thunk`'s own value on a normal commit. The same "acquire, run, always release" shape `(curry dot-locking)`'s `with-dot-lock*` already established for a different resource.

```scheme
(import (curry sql))
(sql-with-transaction conn
  (lambda ()
    (sql-exec conn "INSERT INTO people (name, age) VALUES (?, ?)" "Bob" 25)
    (sql-exec conn "UPDATE people SET age = age + 1 WHERE name = ?" "Bob")))
```

## Last insert id

### `(sql-last-insert-id conn . sequence-name)` → integer

Works as expected on SQLite and MariaDB — their own native "just tell me" primitive (`sqlite-last-insert-rowid`, `mysql_insert_id`) — where `sequence-name`, if given, is accepted but ignored, keeping call sites portable. PostgreSQL has no connection-independent equivalent: with no `sequence-name`, it's "the last value produced by any sequence in this session" (`lastval()`), raising if none has been used yet; with a `sequence-name`, it's that specific sequence's own last value (`currval(sequence-name)`), still raising if that sequence hasn't been used yet. See the design doc's §9 for the full rationale; an `INSERT ... RETURNING id` is the genuinely idiomatic PostgreSQL alternative worth reaching for directly.

## Recoverable row errors

A `'sql-row-error` condition type is registered (via `(curry conditions)`) as the root of anything a driver or a caller wants to signal about a specific row without aborting the whole query — see the design doc's §10 for the full rationale and worked examples of a driver registering its own subtype (e.g. a future PostgreSQL encoding error) and a caller registering an application-specific one (e.g. a negative-balance check), both via `(curry conditions)`'s own open, global condition-type registry, with no coordination needed from this module. Nothing in the current SQLite-only backend signals this type itself — SQLite's own value types (integer/float/text/blob/`NULL`) have no "can't decode this column" case the way a future encoding-sensitive backend might — but it's reserved now so later code can build on it without this module changing.

## Notes

- `%find-placeholder`'s `?`-scanning does not skip a literal `?` character that happens to appear inside a quoted string in the SQL text itself — a query containing both parameter placeholders and a literal `?` in a string constant is a real, narrow gap; write that particular `?` as a bound parameter instead (`... WHERE note = ?` with the literal `"?"` passed as a parameter) if it comes up.
- Backends without true prepared-statement support (MariaDB, PostgreSQL) use an escape-and-splice strategy instead — each parameter is rendered as a SQL literal via that database's own connection-aware escaping function and spliced into the query text — see the design doc's §5 for why this is a deliberate, named strategy rather than a compromise.

## See also

- [`docs/thoughts/sql-abstraction-design.md`](../thoughts/sql-abstraction-design.md) — the full design document this module implements
- [`module-sqlite.md`](module-sqlite.md) — the backend this module currently wraps, unmodified
- [`module-dot-locking.md`](module-dot-locking.md) — the `with-dot-lock*`/`dynamic-wind` pattern `sql-with-transaction` follows
