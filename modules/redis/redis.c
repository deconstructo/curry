/*
 * curry_redis — Redis client module for Curry Scheme.
 *
 * Implements the RESP2 wire protocol directly; no hiredis dependency.
 *
 * Scheme API:
 *   ; Connection
 *   (redis-connect host port)                  -> client
 *   (redis-connect host port password)         -> client  (AUTH)
 *   (redis-close!  client)                     -> void
 *   (redis-ping    client)                     -> #t
 *   (redis-select  client db-index)            -> void
 *   (redis-command client cmd arg ...)         -> value   (raw)
 *
 *   ; Strings
 *   (redis-set!    client key value)           -> void
 *   (redis-set!    client key value ttl-secs)  -> void    (SET EX)
 *   (redis-get     client key)                 -> string | #f
 *   (redis-del!    client key ...)             -> integer (deleted count)
 *   (redis-exists? client key)                 -> bool
 *   (redis-incr!   client key)                 -> integer
 *   (redis-incrby! client key delta)           -> integer
 *   (redis-expire! client key seconds)         -> bool
 *   (redis-ttl     client key)                 -> integer (-1=none, -2=missing)
 *   (redis-keys    client pattern)             -> list of strings
 *
 *   ; Hashes
 *   (redis-hset!   client key field value ...) -> integer
 *   (redis-hget    client key field)           -> string | #f
 *   (redis-hgetall client key)                 -> alist ((field . value) ...)
 *   (redis-hdel!   client key field ...)       -> integer
 *   (redis-hkeys   client key)                 -> list
 *   (redis-hvals   client key)                 -> list
 *   (redis-hexists? client key field)          -> bool
 *
 *   ; Lists
 *   (redis-lpush!  client key value ...)       -> integer (new length)
 *   (redis-rpush!  client key value ...)       -> integer
 *   (redis-lpop    client key)                 -> string | #f
 *   (redis-rpop    client key)                 -> string | #f
 *   (redis-llen    client key)                 -> integer
 *   (redis-lrange  client key start stop)      -> list
 *
 *   ; Sets
 *   (redis-sadd!     client key member ...)    -> integer
 *   (redis-srem!     client key member ...)    -> integer
 *   (redis-smembers  client key)               -> list
 *   (redis-sismember client key member)        -> bool
 *   (redis-scard     client key)               -> integer
 *
 *   ; Sorted sets
 *   (redis-zadd!  client key score member)     -> integer
 *   (redis-zrange client key start stop)       -> list of strings
 *   (redis-zrange-withscores client key start stop) -> alist ((member . score) ...)
 *   (redis-zscore client key member)           -> flonum | #f
 *   (redis-zcard  client key)                  -> integer
 *   (redis-zrank  client key member)           -> integer | #f
 *
 *   ; Pub/Sub (simple fire-and-forget publish)
 *   (redis-publish client channel message)     -> integer (subscribers)
 *
 *   ; Server
 *   (redis-flushdb  client)                    -> void
 *   (redis-dbsize   client)                    -> integer
 *   (redis-info     client)                    -> string
 */

#include <curry.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
typedef SOCKET sock_t;
#  define SOCK_INVALID  INVALID_SOCKET
#  define sock_close    closesocket
#  define sock_write(fd, buf, n) send((fd), (buf), (int)(n), 0)
#  define sock_read(fd, buf, n)  recv((fd), (buf), (int)(n), 0)
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netdb.h>
#  include <unistd.h>
typedef int sock_t;
#  define SOCK_INVALID  (-1)
#  define sock_close    close
#  define sock_write    write
#  define sock_read     read
#endif

/* ---- Connection handle ---- */

#define RBUF_SIZE 65536

typedef struct {
    sock_t  fd;
    char    rbuf[RBUF_SIZE];
    size_t  rlen;   /* bytes in rbuf */
    size_t  rpos;   /* read cursor */
    char    line[RBUF_SIZE]; /* per-connection line buffer for rbuf_readline */
} RedisConn;

static curry_val conn_tag(void) { return curry_make_symbol("redis-conn"); }

static curry_val conn_to_val(RedisConn *c) {
    curry_val bv = curry_make_bytevector(sizeof(RedisConn *), 0);
    for (size_t i = 0; i < sizeof(RedisConn *); i++)
        curry_bytevector_set(bv, (uint32_t)i, ((uint8_t *)&c)[i]);
    return curry_make_pair(conn_tag(), bv);
}

static RedisConn *val_to_conn(curry_val v) {
    if (!curry_is_pair(v) || curry_car(v) != conn_tag())
        curry_error("redis: not a redis client handle");
    curry_val bv = curry_cdr(v);
    RedisConn *c;
    for (size_t i = 0; i < sizeof(RedisConn *); i++)
        ((uint8_t *)&c)[i] = curry_bytevector_ref(bv, (uint32_t)i);
    return c;
}

/* ---- RESP writer ---- */

static void write_all(RedisConn *c, const char *buf, size_t len) {
    while (len > 0) {
        ssize_t n = (ssize_t)sock_write(c->fd, buf, len);
        if (n <= 0) curry_error("redis: write failed: %s", strerror(errno));
        buf += n; len -= (size_t)n;
    }
}

static void resp_start(RedisConn *c, int argc) {
    char hdr[32];
    int n = snprintf(hdr, sizeof(hdr), "*%d\r\n", argc);
    write_all(c, hdr, (size_t)n);
}

static void resp_arg(RedisConn *c, const char *s, size_t len) {
    char hdr[32];
    int n = snprintf(hdr, sizeof(hdr), "$%zu\r\n", len);
    write_all(c, hdr, (size_t)n);
    write_all(c, s, len);
    write_all(c, "\r\n", 2);
}

static void resp_arg_cstr(RedisConn *c, const char *s) {
    resp_arg(c, s, strlen(s));
}

static void resp_arg_int(RedisConn *c, long long n) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%lld", n);
    resp_arg(c, buf, (size_t)len);
}

/* ---- RESP reader ---- */

static int rbuf_getc(RedisConn *c) {
    if (c->rpos >= c->rlen) {
        ssize_t n = (ssize_t)sock_read(c->fd, c->rbuf, RBUF_SIZE);
        if (n <= 0) curry_error("redis: connection closed or read error");
        c->rlen = (size_t)n;
        c->rpos = 0;
    }
    return (unsigned char)c->rbuf[c->rpos++];
}

/* Read until \r\n, return content (without CRLF) in a per-connection buffer */
static const char *rbuf_readline(RedisConn *c) {
    size_t pos = 0;
    for (;;) {
        int ch = rbuf_getc(c);
        if (ch == '\r') {
            rbuf_getc(c);  /* consume \n */
            c->line[pos] = '\0';
            return c->line;
        }
        if (pos < sizeof(c->line) - 1) c->line[pos++] = (char)ch;
    }
}

/* Forward declaration */
static curry_val resp_read(RedisConn *c);

static curry_val resp_read(RedisConn *c) {
    int type = rbuf_getc(c);
    const char *line;

    switch (type) {
    case '+':  /* simple string */
        line = rbuf_readline(c);
        return curry_make_string(line);

    case '-':  /* error */
        line = rbuf_readline(c);
        curry_error("redis: %s", line);

    case ':':  /* integer */
        line = rbuf_readline(c);
        return curry_make_fixnum((intptr_t)atoll(line));

    case '$': {  /* bulk string */
        line = rbuf_readline(c);
        long long len = atoll(line);
        if (len < 0) return curry_make_bool(false);   /* nil bulk string */
        char *buf = malloc((size_t)len + 1);
        if (!buf) curry_error("redis: out of memory");
        size_t got = 0;
        while (got < (size_t)len) {
            int ch = rbuf_getc(c);
            buf[got++] = (char)ch;
        }
        buf[len] = '\0';
        rbuf_getc(c);  /* \r */
        rbuf_getc(c);  /* \n */
        curry_val result = curry_make_string(buf);
        free(buf);
        return result;
    }

    case '*': {  /* array */
        line = rbuf_readline(c);
        long long count = atoll(line);
        if (count < 0) return curry_make_bool(false);   /* nil array */
        /* Build list in reverse, then reverse */
        curry_val lst = curry_nil();
        for (long long i = 0; i < count; i++) {
            curry_val elem = resp_read(c);
            lst = curry_make_pair(elem, lst);
        }
        /* Reverse */
        curry_val rev = curry_nil();
        while (curry_is_pair(lst)) {
            rev = curry_make_pair(curry_car(lst), rev);
            lst = curry_cdr(lst);
        }
        return rev;
    }

    default:
        curry_error("redis: unexpected RESP type byte 0x%02x", type);
    }
}

/* ---- Generic command sender ---- */

/* Send a pre-formatted RESP command (caller must have already written it)
   and return the parsed reply. */
static curry_val redis_call(RedisConn *c, int argc, ...) {
    va_list ap;
    va_start(ap, argc);
    resp_start(c, argc);
    for (int i = 0; i < argc; i++) {
        const char *arg = va_arg(ap, const char *);
        resp_arg_cstr(c, arg);
    }
    va_end(ap);
    return resp_read(c);
}

/* ---- Connection ---- */

static curry_val fn_connect(int ac, curry_val *av, void *ud) {
    (void)ud;
    const char *host = curry_string(av[0]);
    int port = (int)curry_fixnum(av[1]);
    const char *password = (ac >= 3 && curry_is_string(av[2])) ? curry_string(av[2]) : NULL;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port_str, &hints, &res) != 0)
        curry_error("redis-connect: cannot resolve %s", host);

    sock_t fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd == SOCK_INVALID) { freeaddrinfo(res); curry_error("redis-connect: socket()"); }
    if (connect(fd, res->ai_addr, (socklen_t)res->ai_addrlen) != 0) {
        sock_close(fd); freeaddrinfo(res);
        curry_error("redis-connect: connect to %s:%d failed: %s", host, port, strerror(errno));
    }
    freeaddrinfo(res);

    RedisConn *c = malloc(sizeof(RedisConn));
    if (!c) { sock_close(fd); curry_error("redis-connect: out of memory"); }
    c->fd   = fd;
    c->rlen = 0;
    c->rpos = 0;

    if (password) {
        resp_start(c, 2);
        resp_arg_cstr(c, "AUTH");
        resp_arg_cstr(c, password);
        curry_val r = resp_read(c);
        (void)r;
    }

    return conn_to_val(c);
}

static curry_val fn_close(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 1); resp_arg_cstr(c, "QUIT");
    resp_read(c);
    sock_close(c->fd);
    free(c);
    return curry_void();
}

static curry_val fn_ping(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    curry_val r = redis_call(c, 1, "PING");
    (void)r;
    return curry_make_bool(true);
}

static curry_val fn_select(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    long long db = (long long)curry_fixnum(av[1]);
    resp_start(c, 2);
    resp_arg_cstr(c, "SELECT");
    resp_arg_int(c, db);
    resp_read(c);
    return curry_void();
}

/* Raw command: (redis-command client "GET" "key") */
static curry_val fn_command(int ac, curry_val *av, void *ud) {
    (void)ud;
    RedisConn *c = val_to_conn(av[0]);
    int n = ac - 1;   /* arguments after client */
    resp_start(c, n);
    for (int i = 1; i < ac; i++) {
        const char *s = curry_string(av[i]);
        resp_arg_cstr(c, s);
    }
    return resp_read(c);
}

/* ---- String commands ---- */

static curry_val fn_set(int ac, curry_val *av, void *ud) {
    (void)ud;
    RedisConn *c = val_to_conn(av[0]);
    const char *key = curry_string(av[1]);
    const char *val = curry_string(av[2]);
    if (ac >= 4) {
        /* SET key value EX ttl */
        long long ttl = (long long)curry_fixnum(av[3]);
        resp_start(c, 5);
        resp_arg_cstr(c, "SET");
        resp_arg_cstr(c, key);
        resp_arg_cstr(c, val);
        resp_arg_cstr(c, "EX");
        resp_arg_int(c, ttl);
    } else {
        resp_start(c, 3);
        resp_arg_cstr(c, "SET");
        resp_arg_cstr(c, key);
        resp_arg_cstr(c, val);
    }
    resp_read(c);
    return curry_void();
}

static curry_val fn_get(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 2);
    resp_arg_cstr(c, "GET");
    resp_arg_cstr(c, curry_string(av[1]));
    return resp_read(c);
}

static curry_val fn_del(int ac, curry_val *av, void *ud) {
    (void)ud;
    RedisConn *c = val_to_conn(av[0]);
    int n = ac - 1;
    resp_start(c, n + 1);
    resp_arg_cstr(c, "DEL");
    for (int i = 1; i < ac; i++) resp_arg_cstr(c, curry_string(av[i]));
    return resp_read(c);
}

static curry_val fn_exists(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 2);
    resp_arg_cstr(c, "EXISTS");
    resp_arg_cstr(c, curry_string(av[1]));
    curry_val r = resp_read(c);
    return curry_make_bool(curry_fixnum(r) > 0);
}

static curry_val fn_incr(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 2);
    resp_arg_cstr(c, "INCR");
    resp_arg_cstr(c, curry_string(av[1]));
    return resp_read(c);
}

static curry_val fn_incrby(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 3);
    resp_arg_cstr(c, "INCRBY");
    resp_arg_cstr(c, curry_string(av[1]));
    resp_arg_int(c, (long long)curry_fixnum(av[2]));
    return resp_read(c);
}

static curry_val fn_expire(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 3);
    resp_arg_cstr(c, "EXPIRE");
    resp_arg_cstr(c, curry_string(av[1]));
    resp_arg_int(c, (long long)curry_fixnum(av[2]));
    curry_val r = resp_read(c);
    return curry_make_bool(curry_fixnum(r) == 1);
}

static curry_val fn_ttl(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 2);
    resp_arg_cstr(c, "TTL");
    resp_arg_cstr(c, curry_string(av[1]));
    return resp_read(c);
}

static curry_val fn_keys(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 2);
    resp_arg_cstr(c, "KEYS");
    resp_arg_cstr(c, curry_string(av[1]));
    return resp_read(c);
}

/* ---- Hash commands ---- */

/* (redis-hset! client key field value ...) — must be even number of field/value pairs */
static curry_val fn_hset(int ac, curry_val *av, void *ud) {
    (void)ud;
    RedisConn *c = val_to_conn(av[0]);
    int n = ac - 2;   /* field/value args */
    resp_start(c, 2 + n);
    resp_arg_cstr(c, "HSET");
    resp_arg_cstr(c, curry_string(av[1]));
    for (int i = 2; i < ac; i++) resp_arg_cstr(c, curry_string(av[i]));
    return resp_read(c);
}

static curry_val fn_hget(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 3);
    resp_arg_cstr(c, "HGET");
    resp_arg_cstr(c, curry_string(av[1]));
    resp_arg_cstr(c, curry_string(av[2]));
    return resp_read(c);
}

/* HGETALL returns flat (field val field val ...) list; convert to alist */
static curry_val fn_hgetall(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 2);
    resp_arg_cstr(c, "HGETALL");
    resp_arg_cstr(c, curry_string(av[1]));
    curry_val flat = resp_read(c);

    curry_val alist = curry_nil();
    while (curry_is_pair(flat) && curry_is_pair(curry_cdr(flat))) {
        curry_val field = curry_car(flat);
        curry_val value = curry_car(curry_cdr(flat));
        flat = curry_cdr(curry_cdr(flat));
        alist = curry_make_pair(curry_make_pair(field, value), alist);
    }
    /* Reverse to preserve order */
    curry_val rev = curry_nil();
    while (curry_is_pair(alist)) {
        rev = curry_make_pair(curry_car(alist), rev);
        alist = curry_cdr(alist);
    }
    return rev;
}

static curry_val fn_hdel(int ac, curry_val *av, void *ud) {
    (void)ud;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, ac - 1);
    resp_arg_cstr(c, "HDEL");
    for (int i = 1; i < ac; i++) resp_arg_cstr(c, curry_string(av[i]));
    return resp_read(c);
}

static curry_val fn_hkeys(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 2);
    resp_arg_cstr(c, "HKEYS");
    resp_arg_cstr(c, curry_string(av[1]));
    return resp_read(c);
}

static curry_val fn_hvals(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 2);
    resp_arg_cstr(c, "HVALS");
    resp_arg_cstr(c, curry_string(av[1]));
    return resp_read(c);
}

static curry_val fn_hexists(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 3);
    resp_arg_cstr(c, "HEXISTS");
    resp_arg_cstr(c, curry_string(av[1]));
    resp_arg_cstr(c, curry_string(av[2]));
    curry_val r = resp_read(c);
    return curry_make_bool(curry_fixnum(r) == 1);
}

/* ---- List commands ---- */

static curry_val fn_lpush(int ac, curry_val *av, void *ud) {
    (void)ud;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, ac - 1);
    resp_arg_cstr(c, "LPUSH");
    for (int i = 1; i < ac; i++) resp_arg_cstr(c, curry_string(av[i]));
    return resp_read(c);
}

static curry_val fn_rpush(int ac, curry_val *av, void *ud) {
    (void)ud;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, ac - 1);
    resp_arg_cstr(c, "RPUSH");
    for (int i = 1; i < ac; i++) resp_arg_cstr(c, curry_string(av[i]));
    return resp_read(c);
}

static curry_val fn_lpop(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 2);
    resp_arg_cstr(c, "LPOP");
    resp_arg_cstr(c, curry_string(av[1]));
    return resp_read(c);
}

static curry_val fn_rpop(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 2);
    resp_arg_cstr(c, "RPOP");
    resp_arg_cstr(c, curry_string(av[1]));
    return resp_read(c);
}

static curry_val fn_llen(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 2);
    resp_arg_cstr(c, "LLEN");
    resp_arg_cstr(c, curry_string(av[1]));
    return resp_read(c);
}

static curry_val fn_lrange(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 4);
    resp_arg_cstr(c, "LRANGE");
    resp_arg_cstr(c, curry_string(av[1]));
    resp_arg_int(c, (long long)curry_fixnum(av[2]));
    resp_arg_int(c, (long long)curry_fixnum(av[3]));
    return resp_read(c);
}

/* ---- Set commands ---- */

static curry_val fn_sadd(int ac, curry_val *av, void *ud) {
    (void)ud;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, ac - 1);
    resp_arg_cstr(c, "SADD");
    for (int i = 1; i < ac; i++) resp_arg_cstr(c, curry_string(av[i]));
    return resp_read(c);
}

static curry_val fn_srem(int ac, curry_val *av, void *ud) {
    (void)ud;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, ac - 1);
    resp_arg_cstr(c, "SREM");
    for (int i = 1; i < ac; i++) resp_arg_cstr(c, curry_string(av[i]));
    return resp_read(c);
}

static curry_val fn_smembers(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 2);
    resp_arg_cstr(c, "SMEMBERS");
    resp_arg_cstr(c, curry_string(av[1]));
    return resp_read(c);
}

static curry_val fn_sismember(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 3);
    resp_arg_cstr(c, "SISMEMBER");
    resp_arg_cstr(c, curry_string(av[1]));
    resp_arg_cstr(c, curry_string(av[2]));
    curry_val r = resp_read(c);
    return curry_make_bool(curry_fixnum(r) == 1);
}

static curry_val fn_scard(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 2);
    resp_arg_cstr(c, "SCARD");
    resp_arg_cstr(c, curry_string(av[1]));
    return resp_read(c);
}

/* ---- Sorted set commands ---- */

static curry_val fn_zadd(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    double score = curry_is_fixnum(av[2]) ? (double)curry_fixnum(av[2]) : curry_float(av[2]);
    char score_str[64];
    snprintf(score_str, sizeof(score_str), "%.17g", score);
    resp_start(c, 4);
    resp_arg_cstr(c, "ZADD");
    resp_arg_cstr(c, curry_string(av[1]));
    resp_arg_cstr(c, score_str);
    resp_arg_cstr(c, curry_string(av[3]));
    return resp_read(c);
}

static curry_val fn_zrange(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 4);
    resp_arg_cstr(c, "ZRANGE");
    resp_arg_cstr(c, curry_string(av[1]));
    resp_arg_int(c, (long long)curry_fixnum(av[2]));
    resp_arg_int(c, (long long)curry_fixnum(av[3]));
    return resp_read(c);
}

/* Returns alist ((member . score) ...) */
static curry_val fn_zrange_withscores(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 5);
    resp_arg_cstr(c, "ZRANGE");
    resp_arg_cstr(c, curry_string(av[1]));
    resp_arg_int(c, (long long)curry_fixnum(av[2]));
    resp_arg_int(c, (long long)curry_fixnum(av[3]));
    resp_arg_cstr(c, "WITHSCORES");
    curry_val flat = resp_read(c);

    curry_val alist = curry_nil();
    while (curry_is_pair(flat) && curry_is_pair(curry_cdr(flat))) {
        curry_val member = curry_car(flat);
        curry_val score_str = curry_car(curry_cdr(flat));
        flat = curry_cdr(curry_cdr(flat));
        double score = atof(curry_string(score_str));
        alist = curry_make_pair(curry_make_pair(member, curry_make_float(score)), alist);
    }
    curry_val rev = curry_nil();
    while (curry_is_pair(alist)) { rev = curry_make_pair(curry_car(alist), rev); alist = curry_cdr(alist); }
    return rev;
}

static curry_val fn_zscore(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 3);
    resp_arg_cstr(c, "ZSCORE");
    resp_arg_cstr(c, curry_string(av[1]));
    resp_arg_cstr(c, curry_string(av[2]));
    curry_val r = resp_read(c);
    if (!curry_is_string(r)) return curry_make_bool(false);
    return curry_make_float(atof(curry_string(r)));
}

static curry_val fn_zcard(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 2);
    resp_arg_cstr(c, "ZCARD");
    resp_arg_cstr(c, curry_string(av[1]));
    return resp_read(c);
}

static curry_val fn_zrank(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 3);
    resp_arg_cstr(c, "ZRANK");
    resp_arg_cstr(c, curry_string(av[1]));
    resp_arg_cstr(c, curry_string(av[2]));
    return resp_read(c);
}

/* ---- Pub/Sub — publish only ---- */

static curry_val fn_publish(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 3);
    resp_arg_cstr(c, "PUBLISH");
    resp_arg_cstr(c, curry_string(av[1]));
    resp_arg_cstr(c, curry_string(av[2]));
    return resp_read(c);
}

/* ---- Server commands ---- */

static curry_val fn_flushdb(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 1); resp_arg_cstr(c, "FLUSHDB");
    resp_read(c);
    return curry_void();
}

static curry_val fn_dbsize(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 1); resp_arg_cstr(c, "DBSIZE");
    return resp_read(c);
}

static curry_val fn_info(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RedisConn *c = val_to_conn(av[0]);
    resp_start(c, 1); resp_arg_cstr(c, "INFO");
    return resp_read(c);
}

/* ---- Module entry point ---- */

void curry_module_init(CurryVM *vm) {
    /* Connection */
    curry_define_fn(vm, "redis-connect",  fn_connect,  2, 3, NULL);
    curry_define_fn(vm, "redis-close!",   fn_close,    1, 1, NULL);
    curry_define_fn(vm, "redis-ping",     fn_ping,     1, 1, NULL);
    curry_define_fn(vm, "redis-select",   fn_select,   2, 2, NULL);
    curry_define_fn(vm, "redis-command",  fn_command,  2, -1, NULL);

    /* Strings */
    curry_define_fn(vm, "redis-set!",     fn_set,      3, 4, NULL);
    curry_define_fn(vm, "redis-get",      fn_get,      2, 2, NULL);
    curry_define_fn(vm, "redis-del!",     fn_del,      2, -1, NULL);
    curry_define_fn(vm, "redis-exists?",  fn_exists,   2, 2, NULL);
    curry_define_fn(vm, "redis-incr!",    fn_incr,     2, 2, NULL);
    curry_define_fn(vm, "redis-incrby!",  fn_incrby,   3, 3, NULL);
    curry_define_fn(vm, "redis-expire!",  fn_expire,   3, 3, NULL);
    curry_define_fn(vm, "redis-ttl",      fn_ttl,      2, 2, NULL);
    curry_define_fn(vm, "redis-keys",     fn_keys,     2, 2, NULL);

    /* Hashes */
    curry_define_fn(vm, "redis-hset!",    fn_hset,     4, -1, NULL);
    curry_define_fn(vm, "redis-hget",     fn_hget,     3, 3, NULL);
    curry_define_fn(vm, "redis-hgetall",  fn_hgetall,  2, 2, NULL);
    curry_define_fn(vm, "redis-hdel!",    fn_hdel,     3, -1, NULL);
    curry_define_fn(vm, "redis-hkeys",    fn_hkeys,    2, 2, NULL);
    curry_define_fn(vm, "redis-hvals",    fn_hvals,    2, 2, NULL);
    curry_define_fn(vm, "redis-hexists?", fn_hexists,  3, 3, NULL);

    /* Lists */
    curry_define_fn(vm, "redis-lpush!",   fn_lpush,    3, -1, NULL);
    curry_define_fn(vm, "redis-rpush!",   fn_rpush,    3, -1, NULL);
    curry_define_fn(vm, "redis-lpop",     fn_lpop,     2, 2, NULL);
    curry_define_fn(vm, "redis-rpop",     fn_rpop,     2, 2, NULL);
    curry_define_fn(vm, "redis-llen",     fn_llen,     2, 2, NULL);
    curry_define_fn(vm, "redis-lrange",   fn_lrange,   4, 4, NULL);

    /* Sets */
    curry_define_fn(vm, "redis-sadd!",     fn_sadd,      3, -1, NULL);
    curry_define_fn(vm, "redis-srem!",     fn_srem,      3, -1, NULL);
    curry_define_fn(vm, "redis-smembers",  fn_smembers,  2, 2, NULL);
    curry_define_fn(vm, "redis-sismember", fn_sismember, 3, 3, NULL);
    curry_define_fn(vm, "redis-scard",     fn_scard,     2, 2, NULL);

    /* Sorted sets */
    curry_define_fn(vm, "redis-zadd!",             fn_zadd,             4, 4, NULL);
    curry_define_fn(vm, "redis-zrange",             fn_zrange,           4, 4, NULL);
    curry_define_fn(vm, "redis-zrange-withscores",  fn_zrange_withscores,4, 4, NULL);
    curry_define_fn(vm, "redis-zscore",             fn_zscore,           3, 3, NULL);
    curry_define_fn(vm, "redis-zcard",              fn_zcard,            2, 2, NULL);
    curry_define_fn(vm, "redis-zrank",              fn_zrank,            3, 3, NULL);

    /* Pub/Sub */
    curry_define_fn(vm, "redis-publish",  fn_publish,  3, 3, NULL);

    /* Server */
    curry_define_fn(vm, "redis-flushdb",  fn_flushdb,  1, 1, NULL);
    curry_define_fn(vm, "redis-dbsize",   fn_dbsize,   1, 1, NULL);
    curry_define_fn(vm, "redis-info",     fn_info,     1, 1, NULL);
}
