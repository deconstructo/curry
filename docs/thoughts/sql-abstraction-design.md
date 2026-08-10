# curry sql — A Scheme-Native Cross-Database Layer

*Design draft — 2026-08-10. Prompted by a review of PHP's [PDO](https://www.php.net/manual/en/book.pdo.php) as a reference point — not a port of it. Supersedes the ad-hoc plan sketched earlier in this project's own history (an escape-and-interpolate approach for FFI-backed drivers, decided but never written down); this document is that decision, written down properly, plus everything PDO's own design gets in the way of.*

## 1. What PDO actually is, read closely

PDO is one `PDO` connection class plus one `PDOStatement` result/prepared-statement class, with driver-specific behaviour swapped in underneath via a DSN string (`"mysql:host=localhost;dbname=test"`) parsed at connect time. Its core surface: `prepare`/`query`/`exec`, `bindParam`/`bindValue`, `fetch`/`fetchAll` with a `PDO::FETCH_*` mode flag, `beginTransaction`/`commit`/`rollBack`, `lastInsertId`, `quote`, and a `PDO::ATTR_*`-keyed attribute bag for everything else (error mode, emulated-vs-native prepares, fetch defaults, and more).

It is a genuinely useful, long-lived design — "one interface, several drivers underneath" is exactly the shape this project wants too. The critique below is about *which parts of that shape are PHP-idiomatic rather than actually load-bearing*, since those are precisely the parts a Scheme design shouldn't carry over.

## 2. What to keep

- **One connection type, driver swapped underneath.** The right shape. §4 below keeps this, just via closures instead of subclassing.
- **A single placeholder style the caller always writes, translated per-backend underneath.** PDO does this for `:name` params on drivers that don't natively support them; `?` deserves the same treatment for every backend that doesn't speak it natively (only PostgreSQL, of the three this project cares about — see §5).
- **Explicit transaction boundaries, not autocommit-and-hope.** PDO's `beginTransaction`/`commit`/`rollBack` triplet is correct as a *capability*; §6 keeps the capability and fixes the *ergonomics* (see below).
- **A single "what actually happened" error channel** rather than checking a return code after every call, PDO's original (pre-8.0) default and a real historical footgun (see §3).

## 3. What not to keep, and why

### 3.1 The DSN string

`"mysql:host=localhost;dbname=test;charset=utf8mb4"` is untyped, hand-parsed, and its own tiny ad-hoc syntax — a typo in a key name is silently ignored by most drivers rather than rejected, and there is no way to *construct* one programmatically without either string concatenation (an injection surface if any component comes from outside the program) or a second, separate builder API most people never reach for. This is exactly the kind of stringly-typed configuration Lisp environments have never needed, because an alist or a plain argument list is *already* a first-class, typed, composable, injection-proof way to say the same thing.

### 3.2 The attribute bag

`PDO::ATTR_ERRMODE`, `PDO::ATTR_EMULATE_PREPARES`, `PDO::ATTR_DEFAULT_FETCH_MODE`, and the rest are all just integers, set through one untyped `setAttribute($int, $value)` call, with no feedback if a driver silently ignores one it doesn't implement. This is PHP reaching for a constant-flag pattern because PHP has no better idiom for "a small set of named, typed options" — but Scheme does: an ordinary optional-keyword-free argument, or a small alist of `(symbol . value)` pairs the driver can actually inspect and complain about a key it doesn't recognise, rather than silently dropping it. §7 uses this instead.

### 3.3 The FETCH_* mode zoo

`FETCH_ASSOC`, `FETCH_OBJ`, `FETCH_NUM`, `FETCH_BOTH`, `FETCH_CLASS`, `FETCH_INTO`, `FETCH_LAZY`, `FETCH_KEY_PAIR`... — eight-plus ways to shape one row, all as integer flags, several combinable with bitwise OR into further flag combinations. `(curry sqlite)` already picked one shape (an alist keyed by column-name symbol) and it is the right *default* row shape for a Scheme program; a caller who wants a plain positional list or a vector can get there with one `map` over the alist. This document does not add a fetch-mode option at all — see §8.

### 3.4 "Emulated" vs. "native" prepares, silently

PDO's MySQL driver historically defaulted to *emulating* prepared statements — building the final SQL client-side by interpolating escaped values, exactly the fallback strategy this project already independently chose for the drivers that don't have an FFI-friendly binary prepare protocol — while *calling it* a prepared statement regardless, via one attribute flag (`ATTR_EMULATE_PREPARES`) a great many PDO users never learn exists until they hit a driver-specific edge case it explains. That's the actual lesson here, and it directly validates and sharpens this project's own earlier, previously-undocumented decision: escape-and-interpolate is a legitimate, safe strategy *when the escaping function is the database's own connection-aware one* (`mysql_real_escape_string`, `PQescapeLiteral`) — but it must never be presented to the caller as indistinguishable from a real server-side prepare, the way PDO's naming does. §5 names the two strategies as what they are.

### 3.5 `lastInsertId`'s overloaded, driver-dependent argument

`lastInsertId($name = null)` takes an optional sequence name that PostgreSQL *requires* to mean anything (it has no universal "the row I just inserted" concept the way SQLite/MySQL's autoincrement do — only "the last value a given sequence produced in this session") and that every other driver ignores outright. One argument, two entirely different meanings depending on which driver you're talking to, with no way to tell from the call site alone which behaviour you're getting. §9 keeps the same underlying limitation (Postgres genuinely doesn't have a driver-independent answer) but refuses to paper over it with one overloaded parameter — see below.

### 3.6 Exceptions as the only structured-error mechanism

PDO's `PDOException` (now the default error mode since PHP 8) is a normal, correct choice for a language without anything better — but PHP has nothing better. Scheme, and curry specifically, does: the CL-style condition system already implemented (`handler-bind`, `with-restarts`, `invoke-restart` — see `docs/reference/language.md`'s condition-system section) supports *non-unwinding* handlers and *restarts*, meaning a caller can offer "skip this row and keep fetching," "retry with this corrected value," or "use this default" as concrete, resumable options — not just "unwind the whole operation." §10 uses this for exactly the class of error PDO can only ever abort on.

## 4. Driver dispatch: a protocol record, not a class hierarchy

Each backend (`sqlite`, `mariadb`, `postgres`) already exists (or will exist) as its own module with its own native procedure names (`sqlite-exec`, a forthcoming `mariadb-exec`, etc. — see `docs/thoughts/` history and the pending backend work). `(curry sql)` doesn't reimplement any of that; it wraps each backend's own connection handle together with a small record of closures — a manual vtable, the Scheme-idiomatic answer to "one interface, several implementations" that doesn't need a class hierarchy to express:

```scheme
(define-record-type <sql-driver>
  (make-sql-driver exec query prepare? last-insert-id close)
  sql-driver?
  (exec           driver-exec)            ; (proc raw-conn sql) -> affected-row count
  (query          driver-query)           ; (proc raw-conn sql) -> list of row alists
  (prepare?       driver-supports-prepare?) ; boolean: true only for sqlite today
  (last-insert-id driver-last-insert-id)  ; (proc raw-conn) -> integer or #f
  (close          driver-close))          ; (proc raw-conn) -> unspecified

(define-record-type <sql-connection>
  (%make-sql-connection kind raw-conn driver)
  sql-connection?
  (kind      sql-connection-kind)   ; 'sqlite | 'mariadb | 'postgres
  (raw-conn  sql-connection-raw)    ; the backend's own connection handle
  (driver    sql-connection-driver))
```

Adding a fourth backend later means writing one more `<sql-driver>` value, not touching `(curry sql)`'s own dispatch code at all — every call site just invokes whatever's in the connection's own `driver` slot.

### 4.1 The existing `(curry sqlite)` module needs no changes at all

`(curry sql)` is purely additive: it `import`s `(curry sqlite)` and wraps its already-existing procedures into one `<sql-driver>` value. Nothing in `sqlite.c` or `module-sqlite.md` changes, and any code calling `sqlite-open`/`sqlite-exec`/etc. directly today is completely unaffected. Two things about `(curry sqlite)`'s existing design turn out to already agree with this document's own choices, made independently:

- `sqlite-exec` already returns rows as alists keyed by column-name symbol — exactly §8's row shape. The sqlite driver's `query` closure needs no row-shape translation at all, unlike the mariadb/postgres drivers (which have to build that shape themselves from whatever their C libraries hand back).
- `(curry sqlite)` already has no dedicated transaction procedures — its own docs' "Transaction pattern" is running `BEGIN`/`COMMIT`/`ROLLBACK` as plain SQL through `sqlite-exec`. That is exactly what every `<sql-driver>`'s `exec` closure does for `sql-with-transaction` in §6 — a zero-gap fit, not a workaround.

```scheme
(define sqlite-driver
  (make-sql-driver
    (lambda (raw sql) (length (sqlite-exec raw sql)))   ; exec: affected-row count
    sqlite-exec                                          ; query: already the right shape
    #t                                                    ; prepare?: sqlite gets the true-prepared-statement
                                                           ; path in §5, not escape-and-splice
    sqlite-last-insert-rowid                              ; last-insert-id: direct pass-through
    sqlite-close))
```

Sqlite is the smallest of the three driver records to write, and the only one that's mostly direct pass-through rather than new translation code.

## 5. Placeholders: one style, two real strategies underneath

The caller always writes `?` — matching SQLite's and MariaDB's own native syntax, and the one this project's earlier (undocumented) decision already picked. Underneath, two genuinely different things happen, and this document names them rather than hiding the difference the way PDO's `EMULATE_PREPARES` flag does:

- **True prepared statements** (SQLite only, today): `?` placeholders pass straight through to `sqlite-prepare`/`sqlite-bind`, unmodified. The safest and most correct path where it's available.
- **Escape-and-splice** (MariaDB, PostgreSQL): each `?` is replaced, left to right, with the corresponding value rendered as a SQL literal — a number as bare digits (never escaped, never quoted: a number can't carry an injection payload), `#f` as bare `NULL`, a string through the connection's own escaping function (`mysql_real_escape_string`, `PQescapeLiteral`) and then quoted, a bytevector through the equivalent binary-safe escape (`PQescapeByteaConn` for Postgres; MariaDB's own string-escape function is already binary-safe). This is exactly PDO's "emulated prepares" strategy, used deliberately and named as what it is — not a compromise this document apologizes for, since it's the only FFI-tractable option for these two drivers without marshaling `MYSQL_BIND` struct arrays or `PQexecParams`'s `char**` argument arrays (both real, avoidable FFI complexity for a first pass — see the earlier, narrower design note on the MariaDB/Postgres backends themselves).

## 6. Transactions: a macro, not a triplet of methods to remember to pair

PDO gives you `beginTransaction()`/`commit()`/`rollBack()` as three independent method calls — nothing stops a caller from forgetting the `rollBack()` in a `catch` block, or committing on one code path and forgetting to on another. `(curry dot-locking)`'s own `with-dot-lock*` already established the right pattern for "acquire, run, always release, even on a non-local exit" via `dynamic-wind`; transactions get the identical treatment:

```scheme
;; (sql-with-transaction conn thunk) -- begins a transaction, calls (thunk),
;; commits on a normal return, rolls back and re-raises on any escape
;; (an error, an escaping continuation) via dynamic-wind. All three
;; backends implement BEGIN/COMMIT/ROLLBACK as ordinary SQL text, so
;; this needs nothing backend-specific beyond driver-exec.
(define (sql-with-transaction conn thunk)
  (sql-exec conn "BEGIN")
  (dynamic-wind
    (lambda () #t)
    (lambda ()
      (let ((result (thunk)))
        (sql-exec conn "COMMIT")
        result))
    (lambda () (when (sql-in-transaction? conn) (sql-exec conn "ROLLBACK")))))
```

The explicit `sql-begin!`/`sql-commit!`/`sql-rollback!` procedures still exist underneath for the rare caller who genuinely needs manual control — `sql-with-transaction` is the one nearly everyone should reach for, the same relationship `with-dot-lock*` has to `obtain-dot-lock`/`release-dot-lock`.

## 7. Options: symbols and alists, not integer flags

Where PDO would reach for an `ATTR_*` constant, this design uses a plain optional argument or a small `(symbol . value)` alist the driver can actually validate:

```scheme
(sql-connect 'sqlite "/path/to.db")
(sql-connect 'mariadb '((host . "localhost") (user . "app") (password . "secret") (database . "app_db")))
(sql-connect 'postgres '((host . "localhost") (port . 5432) (dbname . "app_db") (user . "app")))
```

An alist of named, typed key/value pairs is the direct Scheme equivalent of a DSN string, with none of its problems: it's constructed with ordinary data (no string-escaping-a-string-inside-a-string risk), a typo'd key is a key the driver can check for and complain about by name, and it composes normally with anything else in the program (read from a config file into an alist directly, build one programmatically, merge two together with `append`).

## 8. Rows, queries, and the one shape decision that matters

`(sql-query conn sql . params)` always returns a list of row alists (`((col-name . value) ...)`), matching `(curry sqlite)`'s own existing convention exactly — no fetch-mode option, because there is exactly one shape worth defaulting to and everything else is one `map` away:

```scheme
(sql-query conn "SELECT id, name FROM users WHERE age > ?" 21)
; => (((id . 1) (name . "Alice")) ((id . 2) (name . "Bob")))

;; Column-list-shaped instead, if that's genuinely what's wanted:
(map (lambda (row) (map cdr row)) (sql-query conn "..."))
```

`(sql-exec conn sql . params)` is the DDL/DML counterpart — `INSERT`/`UPDATE`/`DELETE`/`CREATE TABLE`/etc. — returning the affected-row count rather than a row list. `(sql-query-one conn sql . params)` is a documented convenience for "I know this returns at most one row," returning that row's alist or `#f`, saving the very common `(let ((rows (sql-query ...))) (and (pair? rows) (car rows)))` dance.

## 9. `last-insert-id`: honest about the one real asymmetry

PostgreSQL genuinely has no cross-driver-equivalent "ID of the row I just inserted" — only "the last value a named sequence produced in this session" (`lastval()`), or a value pulled back via an explicit `RETURNING` clause on the `INSERT` itself, which is the actually-idiomatic Postgres pattern and arguably what this document should be steering callers toward rather than papering over. Rather than PDO's one `lastInsertId($name = null)` whose meaning silently changes per driver:

- `(sql-last-insert-id conn)` — works as expected on SQLite and MariaDB (their own native "just tell me" primitive). On a PostgreSQL connection with no sequence name given, it raises rather than silently returning something wrong or `#f`, since neither guess is actually correct.
- `(sql-last-insert-id conn sequence-name)` — the escape hatch for PostgreSQL callers who do know their sequence name (ignored, with a note in the docstring, by the other two drivers rather than erroring — accepting the argument everywhere keeps call sites portable even though only one backend needs it).
- The docs for this procedure say, in plain terms, exactly what PDO's own docs bury in a "Notes" callout: *"PostgreSQL has no universal equivalent to this; prefer an `INSERT ... RETURNING id` and read the id back from the query's own result instead."*

## 10. Where curry's condition system genuinely beats PDO: recoverable row errors

PDO's only failure mode inside a fetch loop is an exception — the whole operation aborts, and if the caller wants to survive one malformed row (a value that doesn't convert cleanly to the type the calling code expected, say) they have to wrap *each* `fetch()` call in its own `try`/`catch`, discarding the loop's own state in the process. curry already has `handler-bind`/`with-restarts`/`invoke-restart` (CL-style, non-unwinding handlers plus resumable restarts, `(curry conditions)`) and this is exactly the place they earn their keep.

### 10.1 The condition-type hierarchy is open by construction — no fixed enum to design

`(curry conditions)`'s type registry (`%condition-type-register!`) is a plain, global, run-time-mutable table mapping a type symbol to its parent types — it is not owned by any module, class, or driver, so "can the set of failure conditions be extended" is already answered by the mechanism itself: yes, by anyone, at any time, with no closed set to enumerate up front. `(curry sql)` only needs to reserve *one* root and let both drivers and callers hang whatever they need off it:

```scheme
;; (curry sql) itself registers just the root, once, at load time:
(%condition-type-register! 'sql-row-error '(error))
```

**Extensible within a driver** — a backend that knows about a failure mode the others don't (say, PostgreSQL's own encoding errors, which have no MariaDB or SQLite equivalent) registers its own subtype and signals that instead of the bare root, with zero coordination needed from `(curry sql)` or the other two drivers:

```scheme
;; inside the postgres driver's own module:
(%condition-type-register! 'sql-encoding-error '(sql-row-error))
(signal (make-condition 'sql-encoding-error
          (list (cons 'column col-name) (cons 'raw-bytes raw))
          "column value is not valid in the connection's encoding"))
```

**Extensible by a call** — an application with its own domain-specific notion of "this row is bad" (say, a ledger system that wants to treat a negative balance as a recoverable row problem, not a SQL-level one at all) registers its *own* subtype the exact same way, entirely outside `(curry sql)`'s or any driver's code:

```scheme
(%condition-type-register! 'negative-balance-row '(sql-row-error))
;; ... later, in application code that wraps sql-query's own row-building:
(when (negative? amount)
  (signal (make-condition 'negative-balance-row (list (cons 'amount amount)) "negative balance")))
```

Either way, a handler installed with `handler-bind` can catch at whatever level of the hierarchy it actually cares about — `'sql-row-error` to catch everything uniformly, or `'sql-encoding-error`/`'negative-balance-row` specifically — via `condition-is-a?`, without `(curry sql)` needing to know in advance that either subtype would ever exist.

### 10.2 Restarts are symbols, not a fixed set either

The restarts a `with-restarts` call offers aren't drawn from an enum either — they're just whatever names make sense at that call site, matched against whatever `invoke-restart` a handler further up chooses to call:

```scheme
(with-restarts ((skip-row (lambda () 'skipped))
                (use-default (lambda (v) v))
                (retry-as-string (lambda () 'coerced)))   ; nothing stops a caller
                                                            ; adding a third option
                                                            ; specific to this query
  (sql-query conn "SELECT amount FROM ledger"))

(handler-bind
  (('sql-encoding-error (lambda (c) (invoke-restart 'use-default "")))
   ('negative-balance-row (lambda (c) (invoke-restart 'skip-row))))
  ...)
```

A `handler-bind` clause installed around one call can `invoke-restart` `'skip-row` to drop just that one row and keep going, or `'use-default` to substitute a value and continue — without the caller needing to wrap every individual row fetch in its own exception handler the way PDO structurally requires, and without `(curry sql)` needing to have anticipated `'retry-as-string` (or any other restart name) when it was written. This is not a PDO feature translated into Scheme syntax — it's a capability PDO's own language has no way to offer at all, and the single best argument for why this shouldn't just be "PDO, but with parentheses."

## 11. Public API summary

| Procedure | Behaviour |
|---|---|
| `(sql-connect kind config)` | `kind` is `'sqlite`/`'mariadb`/`'postgres`; `config` is a path (sqlite) or an alist (mariadb/postgres, see §7). |
| `(sql-exec conn sql . params)` | DDL/DML; returns affected-row count. |
| `(sql-query conn sql . params)` | Returns a list of row alists (§8). |
| `(sql-query-one conn sql . params)` | First row's alist, or `#f`. |
| `(sql-begin!/commit!/rollback! conn)` | Manual transaction control. |
| `(sql-with-transaction conn thunk)` | The one nearly everyone should use instead (§6). |
| `(sql-last-insert-id conn [sequence-name])` | See §9's honesty note. |
| `(sql-close conn)` | |

## 12. Summary Recommendation

| PDO's choice | What this design does instead | Why |
|---|---|---|
| DSN string | Path (sqlite) or typed alist (mariadb/postgres) | No ad-hoc string syntax to hand-parse or inject into (§2.1) |
| `ATTR_*` integer flags | Symbols / small alists | Driver can validate a key by name instead of silently ignoring an unknown flag (§3.2) |
| `FETCH_*` mode zoo | One row shape (alist), documented `map` for anything else | One good default beats eight flag combinations (§3.3) |
| Silent emulated-vs-native prepares | Named explicitly per backend in the docs (§5) | The PDO experience of discovering this by accident is the thing being avoided |
| One overloaded `lastInsertId($name)` | Two procedures, one that raises honestly on Postgres without a sequence name (§9) | An asymmetry that's real shouldn't be hidden behind one silently-reinterpreted argument |
| Exceptions only | Conditions + restarts for recoverable per-row problems (§10) | The one place curry's own language genuinely has more to offer than PDO's |
| `beginTransaction`/`commit`/`rollBack` as three calls to remember to pair | `sql-with-transaction` (dynamic-wind), manual triplet still available | Matches `(curry dot-locking)`'s own `with-dot-lock*` precedent |

**One thing to get right before implementation starts:** §10's restart-based recoverable-row-error design is the part of this document doing something PDO has no equivalent for at all, which means there's no existing behaviour to check it against — worth deciding concretely which specific failure conditions (type-conversion mismatches? encoding errors? a column curry's numeric tower can't represent?) actually get this treatment, versus which stay as ordinary unwinding errors, before the first line of `(curry sql)` is written.
