# Module: (curry redis)

Redis client using the RESP2 wire protocol directly. No hiredis dependency — pure POSIX sockets.

## Installation

No extra packages required. Works with any Redis-compatible server (Redis, Valkey, KeyDB, Dragonfly, etc.).

```bash
# Start Redis locally
redis-server
# or with Docker
docker run -p 6379:6379 redis
```

Enable: `-DBUILD_MODULE_REDIS=ON` (default on).

## Import

```scheme
(import (curry redis))
```

## Connection

```scheme
(redis-connect host port)              ; → client
(redis-connect host port password)     ; with AUTH
(redis-close!  client)
(redis-ping    client)                 ; → #t
(redis-select  client db-index)        ; select database (0–15)
(redis-command client cmd arg ...)     ; raw RESP command → value
```

## String commands

```scheme
(redis-set!    client key value)         ; SET
(redis-set!    client key value ttl)     ; SET EX ttl-seconds
(redis-get     client key)               ; → string | #f
(redis-del!    client key ...)           ; → integer (deleted count)
(redis-exists? client key)               ; → bool
(redis-incr!   client key)               ; → integer
(redis-incrby! client key delta)         ; → integer
(redis-expire! client key seconds)       ; → bool
(redis-ttl     client key)               ; → integer (-1=no TTL, -2=missing)
(redis-keys    client pattern)           ; → list of strings  e.g. "user:*"
```

## Hash commands

```scheme
(redis-hset!   client key field value ...)  ; → integer (fields added)
(redis-hget    client key field)            ; → string | #f
(redis-hgetall client key)                  ; → ((field . value) ...)
(redis-hdel!   client key field ...)        ; → integer
(redis-hkeys   client key)                  ; → list of strings
(redis-hvals   client key)                  ; → list of strings
(redis-hexists? client key field)           ; → bool
```

## List commands

```scheme
(redis-lpush! client key value ...)     ; push left → new length
(redis-rpush! client key value ...)     ; push right → new length
(redis-lpop   client key)               ; → string | #f
(redis-rpop   client key)               ; → string | #f
(redis-llen   client key)               ; → integer
(redis-lrange client key start stop)    ; → list  (0 -1 = full list)
```

## Set commands

```scheme
(redis-sadd!     client key member ...) ; → integer (added)
(redis-srem!     client key member ...) ; → integer (removed)
(redis-smembers  client key)            ; → list of strings
(redis-sismember client key member)     ; → bool
(redis-scard     client key)            ; → integer
```

## Sorted set commands

```scheme
(redis-zadd!            client key score member)  ; → integer
(redis-zrange           client key start stop)    ; → list of strings
(redis-zrange-withscores client key start stop)   ; → ((member . score) ...)
(redis-zscore           client key member)        ; → flonum | #f
(redis-zcard            client key)               ; → integer
(redis-zrank            client key member)        ; → integer | #f
```

## Pub/Sub

```scheme
(redis-publish client channel message)  ; → integer (subscriber count)
```

Subscribe/receive is not implemented (it requires keeping the connection in subscription mode; use a dedicated actor + `redis-command` with `"SUBSCRIBE"` for full pub/sub).

## Server

```scheme
(redis-flushdb  client)   ; flush current database
(redis-dbsize   client)   ; → integer
(redis-info     client)   ; → string (INFO output)
```

## Examples

### Session cache

```scheme
(import (curry redis))

(define r (redis-connect "localhost" 6379))

; Store a session (expire after 1 hour)
(redis-set! r "session:abc123" "{\"user\":42,\"role\":\"admin\"}" 3600)

; Retrieve
(redis-get r "session:abc123")

; Check existence and TTL
(redis-exists? r "session:abc123")   ; => #t
(redis-ttl     r "session:abc123")   ; => (something ≤ 3600)

(redis-close! r)
```

### Leaderboard with sorted sets

```scheme
(import (curry redis))
(import (curry json))

(define r (redis-connect "localhost" 6379))

(redis-zadd! r "scores" 10500.0 "alice")
(redis-zadd! r "scores" 9800.0  "bob")
(redis-zadd! r "scores" 11200.0 "carol")

; Top 3 with scores (highest first: use ZREVRANGE via redis-command)
(redis-command r "ZREVRANGEBYSCORE" "scores" "+inf" "-inf"
               "WITHSCORES" "LIMIT" "0" "3")
; => ("carol" "11200" "alice" "10500" "bob" "9800")

(redis-close! r)
```

### Rate limiting

```scheme
(define (rate-limit! r user-id limit window-secs)
  (let* ((key   (string-append "rl:" user-id))
         (count (redis-incr! r key)))
    (when (= count 1)
      (redis-expire! r key window-secs))
    (<= count limit)))

; Allow 100 requests per minute
(rate-limit! r "user:42" 100 60)   ; => #t or #f
```

### Using actors for concurrent Redis access

```scheme
(import (curry redis))

(define (make-redis-actor)
  (spawn (lambda ()
    (define r (redis-connect "localhost" 6379))
    (let loop ()
      (let ((msg (receive)))
        (cond
          ((and (pair? msg) (eq? (car msg) 'get))
           (send! (cadr msg) (redis-get r (caddr msg))))
          ((and (pair? msg) (eq? (car msg) 'set))
           (redis-set! r (cadr msg) (caddr msg))))
        (loop))))))

(define ra (make-redis-actor))
(send! ra (list 'set "foo" "bar"))
(send! ra (list 'get (self) "foo"))
(receive)   ; => "bar"
```

## Notes

- Nil bulk strings (missing keys) return `#f`, not the string `"nil"`.
- All keys and values are strings. Use `number->string` / `string->number` for numeric values.
- The connection is not thread-safe; use one connection per actor.
- Pipelining is not implemented; each call does one round-trip. For batch operations, use `redis-command` with multiple arguments or wrap in a Lua script via `EVAL`.
- RESP3 (Redis 7+) protocol features (maps, sets, doubles) are not used; RESP2 is sufficient for all common operations.
