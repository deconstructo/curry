# Module: (curry neo4j)

*v0.7.5.1 — 2026-05-14*

Neo4j graph database client using the Bolt 4.x/5.x wire protocol directly.
No external C dependencies — pure POSIX sockets with PackStream binary encoding,
modelled on the Redis module.

Bolt version is negotiated automatically: the module proposes 5.4, 5.0, and the
full 4.0–4.4 range; Neo4j picks the highest mutually-supported version. Bolt 5.1+
authentication (split HELLO / LOGON) is handled transparently.

## Installation

No extra packages required beyond a running Neo4j instance.

```bash
# Docker (quickest)
docker run --publish=7474:7474 --publish=7687:7687 \
  --env NEO4J_AUTH=neo4j/yourpassword neo4j:latest

# Neo4j Desktop — download from https://neo4j.com/download/
# or Homebrew (macOS)
brew install neo4j
neo4j start
```

Enable the module at build time:

```bash
cmake -B build -DBUILD_MODULE_NEO4J=ON
cmake --build build
```

## Import

```scheme
(import (curry neo4j))
```

## Connection

```scheme
(neo4j-connect host port)               ; → conn  (anonymous / no auth)
(neo4j-connect host port user password) ; → conn  (basic authentication)
(neo4j-disconnect conn)                 ; → void
```

The default Neo4j Bolt port is **7687**.

```scheme
; Anonymous (Neo4j configured with NEO4J_AUTH=none)
(define db (neo4j-connect "localhost" 7687))

; Authenticated
(define db (neo4j-connect "localhost" 7687 "neo4j" "yourpassword"))

(neo4j-disconnect db)
```

## Queries

```scheme
(neo4j-run conn cypher)              ; → list of row alists
(neo4j-run conn cypher params-alist) ; → list of row alists, with parameters
```

Each result row is an alist of `(column-name . value)` pairs. Column names are
interned as symbols. An empty result set returns `'()`.

```scheme
; Simple query
(neo4j-run db "RETURN 1 AS n, 'hello' AS s")
; => (((n . 1) (s . "hello")))

; All nodes of a label
(neo4j-run db "MATCH (p:Person) RETURN p.name AS name, p.age AS age")
; => (((name . "Alice") (age . 30))
;     ((name . "Bob")   (age . 25)) ...)
```

### Parameters

Pass query parameters as an alist of `(symbol . value)` or `(string . value)` pairs.
Cypher refers to them as `$name`.

```scheme
(neo4j-run db
  "MATCH (p:Person) WHERE p.name = $name RETURN p"
  '((name . "Alice")))
; => (((p . ((id . 0) (labels . ("Person")) (properties . ((name . "Alice") (age . 30)))))))
```

Parameters avoid string interpolation and SQL/Cypher injection.

## Transactions

```scheme
(neo4j-begin-tx conn)   ; → tx
(neo4j-commit   tx)     ; → void
(neo4j-rollback tx)     ; → void
```

A transaction handle `tx` is used exactly like a connection handle with
`neo4j-run`. Multiple queries within a transaction see each other's writes and
are committed atomically.

```scheme
(define tx (neo4j-begin-tx db))

(neo4j-run tx "CREATE (a:Account {id: $id, balance: $bal})"
              '((id . 1) (bal . 1000)))
(neo4j-run tx "CREATE (a:Account {id: $id, balance: $bal})"
              '((id . 2) (bal . 500)))
(neo4j-run tx
  "MATCH (a:Account {id:1}), (b:Account {id:2})
   CREATE (a)-[:TRANSFER {amount: 200}]->(b)"
  '())

(neo4j-commit tx)
```

If an error occurs, roll back:

```scheme
(define tx (neo4j-begin-tx db))
(guard (exn (#t (neo4j-rollback tx) (raise exn)))
  (neo4j-run tx "MERGE (n:Node {id: $id}) SET n.val = $val"
                '((id . 42) (val . 99)))
  (neo4j-commit tx))
```

## Type mapping

PackStream values returned by Neo4j are decoded to Scheme values as follows:

| PackStream type | Scheme value |
|----------------|--------------|
| Null | `'()` |
| Boolean | `#t` / `#f` |
| Integer | fixnum |
| Float | flonum |
| String | string |
| Bytes | bytevector |
| List | proper list |
| Map | alist `((key . val) ...)` — keys are strings |
| Node | alist `((id . N) (labels . (str ...)) (properties . alist))` |
| Relationship | alist `((id . N) (type . str) (start . N) (end . N) (properties . alist))` |
| Path | `(path nodes rels sequence)` — raw decoded fields |

For query parameters (Scheme → PackStream), alists are encoded as maps,
proper lists as arrays, booleans, fixnums, flonums, strings, and symbols
are each encoded to their PackStream equivalents. Symbols are encoded as
strings. `'()` encodes as Null.

## Examples

### Populate and query a social graph

```scheme
(import (curry neo4j))

(define db (neo4j-connect "localhost" 7687 "neo4j" "test"))

; Create nodes and relationships
(define setup (neo4j-begin-tx db))
(neo4j-run setup
  "CREATE (a:Person {name:'Alice', age:30}),
          (b:Person {name:'Bob',   age:25}),
          (c:Person {name:'Carol', age:28}),
          (a)-[:KNOWS]->(b),
          (b)-[:KNOWS]->(c)" '())
(neo4j-commit setup)

; Query: friends of Alice
(neo4j-run db
  "MATCH (a:Person {name:'Alice'})-[:KNOWS]->(f) RETURN f.name AS name")
; => (((name . "Bob")))

; Query: mutual acquaintances within 2 hops
(neo4j-run db
  "MATCH (a:Person {name:'Alice'})-[:KNOWS*1..2]->(x) RETURN DISTINCT x.name AS name")
; => (((name . "Bob")) ((name . "Carol")))

(neo4j-disconnect db)
```

### Parameterized upsert

```scheme
(import (curry neo4j))

(define db (neo4j-connect "localhost" 7687 "neo4j" "test"))

(define (upsert-person! name age)
  (neo4j-run db
    "MERGE (p:Person {name: $name}) SET p.age = $age RETURN p.name AS name"
    (list (cons 'name name) (cons 'age age))))

(upsert-person! "Dave" 35)
(upsert-person! "Eve"  29)
```

### Using actors for concurrent access

Each actor owns one connection — connections are not thread-safe.

```scheme
(import (curry neo4j))

(define (make-neo4j-actor host port user pass)
  (spawn (lambda ()
    (define db (neo4j-connect host port user pass))
    (let loop ()
      (let ((msg (receive)))
        (cond
          ((and (pair? msg) (eq? (car msg) 'query))
           (let ((reply-to (cadr msg))
                 (cypher   (caddr msg))
                 (params   (cdddr msg)))
             (send! reply-to
               (guard (e (#t (cons 'error (condition/report-string e))))
                 (cons 'ok (neo4j-run db cypher
                             (if (null? params) '() (car params))))))))
          ((eq? msg 'quit)
           (neo4j-disconnect db)
           #f))
        (when (not (eq? msg 'quit)) (loop)))))))

(define gdb (make-neo4j-actor "localhost" 7687 "neo4j" "test"))

; Send a query and wait for the result
(send! gdb (list 'query (self) "MATCH (n:Person) RETURN count(n) AS total"))
(let ((result (receive)))
  (display (cdr (assq 'total (caadr result)))) (newline))

(send! gdb 'quit)
```

## Notes

- The connection is **not thread-safe**. Use one connection (or actor) per thread.
- Both `conn` and `tx` handles are opaque objects; do not share between threads.
- Large result sets are fully buffered in memory. For streaming, issue multiple
  queries with `SKIP`/`LIMIT`.
- `neo4j-disconnect` sends a Bolt `GOODBYE` message before closing the socket.
  Always disconnect explicitly; do not rely on GC to close connections.
- Bolt protocol errors (authentication failure, bad Cypher, constraint violations)
  raise Scheme errors via `curry-error`; catch them with `guard`.
- Tested against Neo4j 4.4 and 5.x Community and Enterprise editions.
