;;; SQL abstraction layer tests — (curry sql), sqlite backend
;;;
;;; See docs/thoughts/sql-abstraction-design.md for the design this
;;; module implements. This suite exercises the sqlite backend end to
;;; end; mariadb/postgres are exercised in their own
;;; tests/mariadb_tests.scm/tests/postgres_tests.scm (no live server is
;;; available here, so those two suites are limited to import,
;;; connection-failure, and escaping-logic checks — see their headers).

(import (curry sql))

(define pass 0)
(define fail 0)

(define (check label result expected)
  (if (equal? result expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (display " expected ") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

(define (check-error label thunk)
  (if (guard (e (#t #t)) (thunk) #f)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label) (display " did not raise") (newline)
             (set! fail (+ fail 1)))))

(define (fresh-conn)
  (let ((conn (sql-connect 'sqlite ':memory:)))
    (sql-exec conn "CREATE TABLE people (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)")
    conn))

;;; Connecting

(check "sql-connect returns a connection" (sql-connection? (fresh-conn)) #t)
(check "sql-connection-kind reports the backend" (sql-connection-kind (fresh-conn)) 'sqlite)
(check-error "sql-connect raises on an unknown backend" (lambda () (sql-connect 'not-a-real-db "x")))
(check-error "sql-connect raises on an unreachable mariadb host"
  (lambda () (sql-connect 'mariadb '((host . "127.0.0.1") (port . 1)))))
(check-error "sql-connect raises on an unreachable postgres host"
  (lambda () (sql-connect 'postgres '((host . "127.0.0.1") (port . 1)))))

;;; Basic exec / query

(let ((conn (fresh-conn)))
  (check "sql-exec on INSERT returns the affected-row count"
    (sql-exec conn "INSERT INTO people (name, age) VALUES (?, ?)" "Alice" 30)
    1)
  (check "sql-query returns a list of row alists"
    (sql-query conn "SELECT * FROM people")
    '(((id . 1) (name . "Alice") (age . 30))))
  (sql-close conn))

;;; Parameter binding — multiple params, multiple rows

(let ((conn (fresh-conn)))
  (sql-exec conn "INSERT INTO people (name, age) VALUES (?, ?)" "Alice" 30)
  (sql-exec conn "INSERT INTO people (name, age) VALUES (?, ?)" "Bob" 25)
  (sql-exec conn "INSERT INTO people (name, age) VALUES (?, ?)" "Carol" 40)
  (check "parameterized query filters correctly"
    (sql-query conn "SELECT name FROM people WHERE age > ? ORDER BY name" 26)
    '(((name . "Alice")) ((name . "Carol"))))
  (check "sql-query-one returns the first row's alist"
    (sql-query-one conn "SELECT * FROM people WHERE name = ?" "Bob")
    '((id . 2) (name . "Bob") (age . 25)))
  (check "sql-query-one returns #f when nothing matches"
    (sql-query-one conn "SELECT * FROM people WHERE name = ?" "Nobody")
    #f)
  (sql-close conn))

;;; NULL parameter binding

(let ((conn (fresh-conn)))
  (sql-exec conn "INSERT INTO people (name, age) VALUES (?, ?)" "Anon" #f)
  (check "a #f parameter binds as SQL NULL"
    (sql-query-one conn "SELECT age FROM people WHERE name = ?" "Anon")
    '((age . #f)))
  (sql-close conn))

;;; A query with no parameters at all (the direct exec-raw path, no
;;; prepared statement needed)

(let ((conn (fresh-conn)))
  (sql-exec conn "INSERT INTO people (name, age) VALUES ('Direct', 99)")
  (check "sql-query with no params uses the direct path"
    (sql-query conn "SELECT * FROM people")
    '(((id . 1) (name . "Direct") (age . 99))))
  (sql-close conn))

;;; last-insert-id

(let ((conn (fresh-conn)))
  (sql-exec conn "INSERT INTO people (name, age) VALUES (?, ?)" "Alice" 30)
  (check "sql-last-insert-id after one insert" (sql-last-insert-id conn) 1)
  (sql-exec conn "INSERT INTO people (name, age) VALUES (?, ?)" "Bob" 25)
  (check "sql-last-insert-id after a second insert" (sql-last-insert-id conn) 2)
  (check "sql-last-insert-id accepts (and ignores) a sequence-name argument"
    (sql-last-insert-id conn "irrelevant-on-sqlite")
    2)
  (sql-close conn))

;;; Transactions — manual

(let ((conn (fresh-conn)))
  (check "sql-in-transaction? is #f before any begin" (sql-in-transaction? conn) #f)
  (sql-begin! conn)
  (check "sql-in-transaction? is #t after sql-begin!" (sql-in-transaction? conn) #t)
  (sql-exec conn "INSERT INTO people (name, age) VALUES (?, ?)" "Alice" 30)
  (sql-commit! conn)
  (check "sql-in-transaction? is #f after sql-commit!" (sql-in-transaction? conn) #f)
  (check "a committed manual transaction's insert persists"
    (sql-query conn "SELECT name FROM people")
    '(((name . "Alice"))))
  (sql-close conn))

(let ((conn (fresh-conn)))
  (sql-begin! conn)
  (sql-exec conn "INSERT INTO people (name, age) VALUES (?, ?)" "Bob" 25)
  (sql-rollback! conn)
  (check "a rolled-back manual transaction's insert does not persist"
    (sql-query conn "SELECT name FROM people")
    '())
  (sql-close conn))

;;; Transactions — sql-with-transaction

(let ((conn (fresh-conn)))
  (check "sql-with-transaction returns the thunk's own value"
    (sql-with-transaction conn (lambda () (sql-exec conn "INSERT INTO people (name, age) VALUES (?, ?)" "Alice" 30)))
    1)
  (check "sql-with-transaction commits on a normal return"
    (sql-query conn "SELECT name FROM people")
    '(((name . "Alice"))))
  (sql-close conn))

(let ((conn (fresh-conn)))
  (check-error "sql-with-transaction propagates an error raised inside the thunk"
    (lambda ()
      (sql-with-transaction conn
        (lambda ()
          (sql-exec conn "INSERT INTO people (name, age) VALUES (?, ?)" "Bob" 25)
          (error "boom")))))
  (check "sql-with-transaction rolled back after the thunk raised"
    (sql-query conn "SELECT name FROM people")
    '())
  (check "sql-in-transaction? is #f after a rollback via sql-with-transaction"
    (sql-in-transaction? conn)
    #f)
  (sql-close conn))

;;; Regression: sqlite has no nested transactions without SAVEPOINT, and
;;; sqlite-exec used to silently swallow the underlying "cannot start a
;;; transaction within a transaction" error instead of raising it (see
;;; the sqlite-exec fix in modules/sqlite/sqlite.c) -- meaning a nested
;;; sql-with-transaction call would silently succeed at the wrong
;;; moment (committing the single shared transaction early) rather
;;; than failing loudly. Now that sqlite-exec raises correctly, nesting
;;; raises immediately at the inner BEGIN, and dynamic-wind unwinds and
;;; rolls back everything cleanly -- confirmed here by checking that
;;; NEITHER insert (the one before the nested call, nor the one after)
;;; ends up persisted.
(let ((conn (fresh-conn)))
  (check-error "nested sql-with-transaction on the same connection raises"
    (lambda ()
      (sql-with-transaction conn
        (lambda ()
          (sql-exec conn "INSERT INTO people (name, age) VALUES (?, ?)" "Outer" 1)
          (sql-with-transaction conn
            (lambda () (sql-exec conn "INSERT INTO people (name, age) VALUES (?, ?)" "Inner" 2)))))))
  (check "neither insert from a failed nested transaction persists"
    (sql-query conn "SELECT name FROM people")
    '())
  (sql-close conn))

(display pass) (display " passed, ") (display fail) (display " failed") (newline)
(when (> fail 0) (exit 1))
