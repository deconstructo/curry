;;; Redis module tests — plain TCP and TLS connections.
;;;
;;; Usage (normally invoked via test_redis.sh):
;;;   curry redis_tests.scm [plain-port [tls-port [tls-ca-cert-path]]]
;;;
;;; All sections skip gracefully when the server is not reachable, so the
;;; test exits 0 even on machines with no Redis installed.

(import (curry redis))

;;; ---- Command-line args ----
;;; curry passes each CLI arg through the reader, so numbers become fixnums
;;; and paths become symbols.  The script name is command-line-args[0].

(define raw-args (cdr command-line-args))  ; drop script name

(define (arg-port lst default)
  (let ((v (and (pair? lst) (car lst))))
    (or (and (number? v) (exact v))
        (and (symbol? v) (string->number (symbol->string v)))
        default)))

(define (arg-path lst)
  (let ((v (and (pair? lst) (car lst))))
    (cond
      ((string? v) (if (string=? v "") #f v))
      ((symbol? v) (let ((s (symbol->string v)))
                     (if (string=? s "") #f s)))
      (else #f))))

(define plain-port (arg-port raw-args             6379))
(define tls-port   (arg-port (cdr raw-args)       6380))
(define tls-ca     (arg-path (cddr raw-args)))

;;; ---- Harness ----

(define pass 0)
(define fail 0)
(define skip 0)

(define (check label got expected)
  (if (equal? got expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " — got ") (write got)
             (display " expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))

(define (skip! label)
  (display "SKIP: ") (display label) (newline)
  (set! skip (+ skip 1)))

;;; ---- Plain TCP tests ----

(define c #f)
(guard (exn (#t (skip! (string-append "Redis not available on 127.0.0.1:"
                                      (number->string plain-port)))))
  (set! c (redis-connect "127.0.0.1" plain-port)))

(when c
  (redis-flushdb c)

  ;;; ping
  (check "ping"              (redis-ping c)                    #t)

  ;;; strings
  (redis-set! c "t:str" "hello")
  (check "get"               (redis-get    c "t:str")          "hello")
  (check "exists-yes"        (redis-exists? c "t:str")         #t)
  (check "exists-no"         (redis-exists? c "t:none")        #f)
  (redis-set! c "t:n" "0")
  (redis-incr! c "t:n")
  (redis-incr! c "t:n")
  (redis-incrby! c "t:n" 3)
  (check "incr+incrby"       (redis-get    c "t:n")            "5")
  (redis-del! c "t:str" "t:n")
  (check "del"               (redis-exists? c "t:str")         #f)

  ;;; TTL / expire
  (redis-set! c "t:exp" "x")
  (redis-expire! c "t:exp" 60)
  (check "ttl>0"             (> (redis-ttl c "t:exp") 0)       #t)
  (redis-del! c "t:exp")

  ;;; keys
  (redis-set! c "t:k1" "a")
  (redis-set! c "t:k2" "b")
  (check "keys-count"        (length (redis-keys c "t:*"))     2)
  (redis-del! c "t:k1" "t:k2")

  ;;; hashes
  (redis-hset! c "t:h" "f1" "v1" "f2" "v2")
  (check "hget"              (redis-hget    c "t:h" "f1")      "v1")
  (check "hexists-yes"       (redis-hexists? c "t:h" "f2")     #t)
  (check "hexists-no"        (redis-hexists? c "t:h" "f9")     #f)
  (check "hkeys-len"         (length (redis-hkeys c "t:h"))    2)
  (check "hvals-len"         (length (redis-hvals c "t:h"))    2)
  (check "hgetall-len"       (length (redis-hgetall c "t:h"))  2)
  (redis-hdel! c "t:h" "f1")
  (check "hdel"              (redis-hexists? c "t:h" "f1")     #f)
  (redis-del! c "t:h")

  ;;; lists
  (redis-rpush! c "t:l" "a" "b" "c")
  (check "llen"              (redis-llen   c "t:l")            3)
  (check "lrange"            (redis-lrange c "t:l" 0 -1)       '("a" "b" "c"))
  (check "lpop"              (redis-lpop   c "t:l")            "a")
  (check "rpop"              (redis-rpop   c "t:l")            "c")
  (redis-lpush! c "t:l" "z")
  (check "lpush"             (redis-lpop   c "t:l")            "z")
  (redis-del! c "t:l")

  ;;; sets
  (redis-sadd! c "t:s" "x" "y" "z")
  (check "scard"             (redis-scard     c "t:s")         3)
  (check "sismember-yes"     (redis-sismember c "t:s" "y")     #t)
  (check "sismember-no"      (redis-sismember c "t:s" "w")     #f)
  (redis-srem! c "t:s" "z")
  (check "srem"              (redis-scard     c "t:s")         2)
  (check "smembers-len"      (length (redis-smembers c "t:s")) 2)
  (redis-del! c "t:s")

  ;;; sorted sets
  (redis-zadd! c "t:z" 1.0 "a")
  (redis-zadd! c "t:z" 2.0 "b")
  (redis-zadd! c "t:z" 3.0 "c")
  (check "zcard"             (redis-zcard  c "t:z")            3)
  (check "zrange"            (redis-zrange c "t:z" 0 -1)       '("a" "b" "c"))
  (check "zscore"            (redis-zscore c "t:z" "b")        2.0)
  (check "zrank"             (redis-zrank  c "t:z" "c")        2)
  (let ((ws (redis-zrange-withscores c "t:z" 0 -1)))
    (check "zwithscores-len" (length ws)                        3)
    (check "zwithscores-a"   (cdar ws)                          1.0))
  (redis-del! c "t:z")

  ;;; select / multi-db
  (redis-select c 1)
  (redis-set! c "t:db1" "here")
  (check "select-db1"        (redis-get c "t:db1")             "here")
  (redis-flushdb c)
  (redis-select c 0)

  ;;; raw command passthrough
  (redis-set! c "t:raw" "rawval")
  (check "raw-command"       (redis-command c "GET" "t:raw")   "rawval")
  (redis-del! c "t:raw")

  (check "dbsize-zero"       (redis-dbsize c)                  0)
  (redis-close! c))

;;; ---- TLS tests ----
;;; Guard catches both "unbound variable: redis-connect-tls" (not compiled in)
;;; and any connection error (no TLS server running).

(guard (exn (#t (skip! "Redis TLS unavailable (not compiled in or no server on TLS port)")))
  (let ((c-tls (if tls-ca
                   (redis-connect-tls "127.0.0.1" tls-port #f tls-ca)
                   (redis-connect-tls "127.0.0.1" tls-port))))
    (redis-flushdb c-tls)
    (check "tls:ping"        (redis-ping    c-tls)              #t)
    (redis-set! c-tls "tls:str" "secure")
    (check "tls:get"         (redis-get     c-tls "tls:str")    "secure")
    (redis-rpush! c-tls "tls:l" "a" "b" "c")
    (check "tls:llen"        (redis-llen    c-tls "tls:l")      3)
    (check "tls:lrange"      (redis-lrange  c-tls "tls:l" 0 -1) '("a" "b" "c"))
    (redis-sadd! c-tls "tls:s" "x" "y")
    (check "tls:scard"       (redis-scard   c-tls "tls:s")      2)
    (redis-hset! c-tls "tls:h" "k" "v")
    (check "tls:hget"        (redis-hget    c-tls "tls:h" "k")  "v")
    (redis-del! c-tls "tls:str" "tls:l" "tls:s" "tls:h")
    (redis-close! c-tls)))

;;; ---- Summary ----

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed, ")
(display skip) (display " skipped")
(newline)
(if (> fail 0) (exit 1) (exit 0))
