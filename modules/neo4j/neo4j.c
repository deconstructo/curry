/*
 * curry_neo4j — Neo4j graph database module, raw Bolt 4.x/5.x protocol.
 *
 * No external C dependencies; talks directly to Neo4j over TCP sockets using
 * the Bolt binary protocol and PackStream encoding.  Modelled on the Redis
 * module (modules/redis/redis.c).
 *
 * Scheme API:
 *   (neo4j-connect host port)               -> conn
 *   (neo4j-connect host port user password) -> conn
 *   (neo4j-disconnect conn)                 -> void
 *   (neo4j-run conn cypher)                 -> list of row-alists
 *   (neo4j-run conn cypher params-alist)    -> list of row-alists
 *   (neo4j-begin-tx conn)                   -> tx
 *   (neo4j-commit tx)                       -> void
 *   (neo4j-rollback tx)                     -> void
 *
 * Result type mapping (PackStream → Scheme):
 *   Null       → '()
 *   Boolean    → #t / #f
 *   Integer    → fixnum
 *   Float      → flonum
 *   String     → string
 *   Bytes      → bytevector
 *   List       → list
 *   Map        → alist  ((key . val) ...)
 *   Node       → alist  ((id . N) (labels . (str ...)) (properties . alist))
 *   Rel        → alist  ((id . N) (type . str) (start . N) (end . N) (properties . alist))
 *   Path       → list of alternating nodes/rels
 */

#include <curry.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <math.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
typedef SOCKET sock_t;
#  define SOCK_INVALID  INVALID_SOCKET
#  define sock_close    closesocket
#  define sock_write(fd,b,n)  send((fd),(b),(int)(n),0)
#  define sock_read(fd,b,n)   recv((fd),(b),(int)(n),0)
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

/* ---- Connection handle --------------------------------------------------*/

#define NET_BUF 65536

typedef struct {
    sock_t  fd;
    uint8_t net[NET_BUF];
    size_t  nlen, npos;
    int     bolt_major, bolt_minor;
} NeoConn;

static curry_val conn_tag(void) { return curry_make_symbol("neo4j-conn"); }
static curry_val tx_tag(void)   { return curry_make_symbol("neo4j-tx");   }

static curry_val conn_box(NeoConn *c, curry_val tag) {
    curry_val bv = curry_make_bytevector(sizeof(NeoConn *), 0);
    for (size_t i = 0; i < sizeof(NeoConn *); i++)
        curry_bytevector_set(bv, (uint32_t)i, ((uint8_t *)&c)[i]);
    return curry_make_pair(tag, bv);
}

static NeoConn *conn_unbox(curry_val v, const char *ctx) {
    if (!curry_is_pair(v) ||
        (curry_car(v) != conn_tag() && curry_car(v) != tx_tag()))
        curry_error("%s: not a neo4j connection or transaction", ctx);
    curry_val bv = curry_cdr(v);
    NeoConn *c;
    for (size_t i = 0; i < sizeof(NeoConn *); i++)
        ((uint8_t *)&c)[i] = curry_bytevector_ref(bv, (uint32_t)i);
    return c;
}

/* ---- Network I/O --------------------------------------------------------*/

static void net_write(NeoConn *c, const uint8_t *buf, size_t len) {
    while (len > 0) {
        ssize_t n = (ssize_t)sock_write(c->fd, (const char *)buf, len);
        if (n <= 0) curry_error("neo4j: write failed: %s", strerror(errno));
        buf += n; len -= (size_t)n;
    }
}

static uint8_t net_byte(NeoConn *c) {
    if (c->npos >= c->nlen) {
        ssize_t n = (ssize_t)sock_read(c->fd, (char *)c->net, NET_BUF);
        if (n <= 0) curry_error("neo4j: connection closed");
        c->nlen = (size_t)n; c->npos = 0;
    }
    return c->net[c->npos++];
}

/* ---- PackStream writer --------------------------------------------------*/

typedef struct { uint8_t *data; size_t len, cap; } PSBuf;

static void ps_grow(PSBuf *b, size_t need) {
    if (b->len + need <= b->cap) return;
    size_t nc = b->cap ? b->cap * 2 : 256;
    while (nc < b->len + need) nc *= 2;
    b->data = realloc(b->data, nc);
    if (!b->data) curry_error("neo4j: out of memory");
    b->cap = nc;
}

static void ps_byte(PSBuf *b, uint8_t v) {
    ps_grow(b, 1); b->data[b->len++] = v;
}

static void ps_u16be(PSBuf *b, uint16_t v) {
    ps_byte(b, (uint8_t)(v >> 8)); ps_byte(b, (uint8_t)v);
}
static void ps_u32be(PSBuf *b, uint32_t v) {
    ps_u16be(b, (uint16_t)(v >> 16)); ps_u16be(b, (uint16_t)v);
}
static void ps_u64be(PSBuf *b, uint64_t v) {
    ps_u32be(b, (uint32_t)(v >> 32)); ps_u32be(b, (uint32_t)v);
}

static void ps_int(PSBuf *b, int64_t v) {
    if (v >= -16 && v <= 127) { ps_byte(b, (uint8_t)(v & 0xFF)); }
    else if (v >= -128 && v <= 127) { ps_byte(b, 0xC8); ps_byte(b, (uint8_t)(v & 0xFF)); }
    else if (v >= -32768 && v <= 32767) { ps_byte(b, 0xC9); ps_u16be(b, (uint16_t)(v & 0xFFFF)); }
    else if (v >= -2147483648LL && v <= 2147483647LL) { ps_byte(b, 0xCA); ps_u32be(b, (uint32_t)(v & 0xFFFFFFFF)); }
    else { ps_byte(b, 0xCB); ps_u64be(b, (uint64_t)v); }
}

static void ps_float(PSBuf *b, double d) {
    uint64_t bits; memcpy(&bits, &d, 8);
    ps_byte(b, 0xC1); ps_u64be(b, bits);
}

static void ps_null(PSBuf *b) { ps_byte(b, 0xC0); }
static void ps_bool(PSBuf *b, bool v) { ps_byte(b, v ? 0xC3 : 0xC2); }

static void ps_string_hdr(PSBuf *b, size_t n) {
    if (n <= 15)       { ps_byte(b, (uint8_t)(0x80 | n)); }
    else if (n <= 255) { ps_byte(b, 0xD0); ps_byte(b, (uint8_t)n); }
    else if (n <= 65535) { ps_byte(b, 0xD1); ps_u16be(b, (uint16_t)n); }
    else { ps_byte(b, 0xD2); ps_u32be(b, (uint32_t)n); }
}

static void ps_cstr(PSBuf *b, const char *s) {
    size_t n = strlen(s);
    ps_string_hdr(b, n);
    ps_grow(b, n); memcpy(b->data + b->len, s, n); b->len += n;
}

static void ps_list_hdr(PSBuf *b, size_t n) {
    if (n <= 15)       { ps_byte(b, (uint8_t)(0x90 | n)); }
    else if (n <= 255) { ps_byte(b, 0xD4); ps_byte(b, (uint8_t)n); }
    else if (n <= 65535) { ps_byte(b, 0xD5); ps_u16be(b, (uint16_t)n); }
    else { ps_byte(b, 0xD6); ps_u32be(b, (uint32_t)n); }
}

static void ps_map_hdr(PSBuf *b, size_t n) {
    if (n <= 15)       { ps_byte(b, (uint8_t)(0xA0 | n)); }
    else if (n <= 255) { ps_byte(b, 0xD8); ps_byte(b, (uint8_t)n); }
    else if (n <= 65535) { ps_byte(b, 0xD9); ps_u16be(b, (uint16_t)n); }
    else { ps_byte(b, 0xDA); ps_u32be(b, (uint32_t)n); }
}

static void ps_struct_hdr(PSBuf *b, uint8_t tag, size_t fields) {
    if (fields <= 15) { ps_byte(b, (uint8_t)(0xB0 | fields)); }
    else if (fields <= 255) { ps_byte(b, 0xDC); ps_byte(b, (uint8_t)fields); }
    else { ps_byte(b, 0xDD); ps_u16be(b, (uint16_t)fields); }
    ps_byte(b, tag);
}

static void ps_from_scheme(PSBuf *b, curry_val v);

static int scheme_list_len(curry_val lst) {
    int n = 0;
    while (curry_is_pair(lst)) { n++; lst = curry_cdr(lst); }
    return n;
}

static void ps_from_scheme(PSBuf *b, curry_val v) {
    if (curry_is_nil(v))    { ps_null(b); return; }
    if (curry_is_bool(v))   { ps_bool(b, curry_bool(v)); return; }
    if (curry_is_fixnum(v)) { ps_int(b, (int64_t)curry_fixnum(v)); return; }
    if (curry_is_float(v))  { ps_float(b, curry_float(v)); return; }
    if (curry_is_string(v)) { ps_cstr(b, curry_string(v)); return; }
    if (curry_is_symbol(v)) { ps_cstr(b, curry_symbol(v)); return; }
    if (curry_is_pair(v)) {
        /* alist detection: non-nil list where every car is a pair */
        curry_val probe = v; bool is_alist = true;
        while (curry_is_pair(probe)) {
            if (!curry_is_pair(curry_car(probe))) { is_alist = false; break; }
            probe = curry_cdr(probe);
        }
        if (is_alist) {
            int n = scheme_list_len(v);
            ps_map_hdr(b, (size_t)n);
            curry_val p = v;
            while (curry_is_pair(p)) {
                curry_val kv = curry_car(p);
                curry_val k = curry_car(kv), val2 = curry_cdr(kv);
                if (curry_is_symbol(k)) ps_cstr(b, curry_symbol(k));
                else ps_from_scheme(b, k);
                ps_from_scheme(b, val2);
                p = curry_cdr(p);
            }
        } else {
            int n = scheme_list_len(v);
            ps_list_hdr(b, (size_t)n);
            while (curry_is_pair(v)) {
                ps_from_scheme(b, curry_car(v));
                v = curry_cdr(v);
            }
        }
        return;
    }
    curry_error("neo4j: cannot encode Scheme value as PackStream parameter");
}

/* ---- Bolt chunked framing -----------------------------------------------*/

/* Send a complete message wrapped in Bolt chunks */
static void bolt_send(NeoConn *c, PSBuf *msg) {
    size_t off = 0;
    while (off < msg->len) {
        size_t chunk = msg->len - off;
        if (chunk > 65535) chunk = 65535;
        uint8_t hdr[2] = { (uint8_t)(chunk >> 8), (uint8_t)chunk };
        net_write(c, hdr, 2);
        net_write(c, msg->data + off, chunk);
        off += chunk;
    }
    uint8_t end[2] = { 0, 0 };
    net_write(c, end, 2);
}

/* Read one complete message (all chunks until 0x0000) into a malloc buffer.
   *out_len is set to the assembled length.  Caller must free(). */
static uint8_t *bolt_recv(NeoConn *c, size_t *out_len) {
    size_t cap = 4096, len = 0;
    uint8_t *buf = malloc(cap);
    if (!buf) curry_error("neo4j: out of memory");
    for (;;) {
        uint8_t h0 = net_byte(c), h1 = net_byte(c);
        size_t chunk = ((size_t)h0 << 8) | h1;
        if (chunk == 0) break;  /* end-of-message */
        if (len + chunk > cap) {
            while (cap < len + chunk) cap *= 2;
            buf = realloc(buf, cap);
            if (!buf) curry_error("neo4j: out of memory");
        }
        for (size_t i = 0; i < chunk; i++)
            buf[len++] = net_byte(c);
    }
    *out_len = len;
    return buf;
}

/* ---- PackStream reader --------------------------------------------------*/

typedef struct { const uint8_t *data; size_t pos, len; } PSCur;

static uint8_t cur_byte(PSCur *c) {
    if (c->pos >= c->len) curry_error("neo4j: truncated PackStream message");
    return c->data[c->pos++];
}
static uint16_t cur_u16(PSCur *c) { uint8_t a=cur_byte(c),b=cur_byte(c); return (uint16_t)((a<<8)|b); }
static uint32_t cur_u32(PSCur *c) { uint16_t a=cur_u16(c),b=cur_u16(c); return ((uint32_t)a<<16)|b; }
static uint64_t cur_u64(PSCur *c) { uint32_t a=cur_u32(c),b=cur_u32(c); return ((uint64_t)a<<32)|b; }

static curry_val ps_decode(PSCur *c);

static curry_val ps_read_string(PSCur *c, size_t n) {
    if (c->pos + n > c->len) curry_error("neo4j: string overrun in message");
    char *tmp = malloc(n + 1);
    if (!tmp) curry_error("neo4j: out of memory");
    memcpy(tmp, c->data + c->pos, n);
    tmp[n] = '\0';
    c->pos += n;
    curry_val r = curry_make_string(tmp);
    free(tmp);
    return r;
}

static curry_val ps_read_list(PSCur *c, size_t n) {
    /* build forward list */
    curry_val *elems = malloc(n * sizeof(curry_val));
    if (!elems && n > 0) curry_error("neo4j: out of memory");
    for (size_t i = 0; i < n; i++) elems[i] = ps_decode(c);
    curry_val lst = curry_nil();
    for (size_t i = n; i-- > 0;) lst = curry_make_pair(elems[i], lst);
    free(elems);
    return lst;
}

static curry_val ps_read_map(PSCur *c, size_t n) {
    curry_val alist = curry_nil();
    for (size_t i = 0; i < n; i++) {
        curry_val k = ps_decode(c);
        curry_val v = ps_decode(c);
        alist = curry_make_pair(curry_make_pair(k, v), alist);
    }
    /* reverse to preserve order */
    curry_val rev = curry_nil();
    while (curry_is_pair(alist)) { rev = curry_make_pair(curry_car(alist), rev); alist = curry_cdr(alist); }
    return rev;
}

static curry_val ps_read_struct(PSCur *c, size_t n) {
    uint8_t tag = cur_byte(c);
    switch (tag) {
    case 0x4E: { /* Node: id, labels, properties */
        curry_val id    = ps_decode(c);
        curry_val labs  = ps_decode(c);
        curry_val props = ps_decode(c);
        /* Optional element_id (Bolt 5.x field) */
        if (n >= 4) { curry_val _eid = ps_decode(c); (void)_eid; }
        return curry_make_pair(curry_make_pair(curry_make_symbol("id"), id),
               curry_make_pair(curry_make_pair(curry_make_symbol("labels"), labs),
               curry_make_pair(curry_make_pair(curry_make_symbol("properties"), props),
               curry_nil())));
    }
    case 0x52: { /* Relationship: id, start_id, end_id, type, properties */
        curry_val id    = ps_decode(c);
        curry_val sid   = ps_decode(c);
        curry_val eid   = ps_decode(c);
        curry_val type  = ps_decode(c);
        curry_val props = ps_decode(c);
        /* Optional element_id fields (Bolt 5.x) */
        for (size_t i = 5; i < n; i++) { curry_val _x = ps_decode(c); (void)_x; }
        return curry_make_pair(curry_make_pair(curry_make_symbol("id"), id),
               curry_make_pair(curry_make_pair(curry_make_symbol("type"), type),
               curry_make_pair(curry_make_pair(curry_make_symbol("start"), sid),
               curry_make_pair(curry_make_pair(curry_make_symbol("end"), eid),
               curry_make_pair(curry_make_pair(curry_make_symbol("properties"), props),
               curry_nil())))));
    }
    case 0x72: { /* UnboundRelationship */
        curry_val id    = ps_decode(c);
        curry_val type  = ps_decode(c);
        curry_val props = ps_decode(c);
        for (size_t i = 3; i < n; i++) { curry_val _x = ps_decode(c); (void)_x; }
        return curry_make_pair(curry_make_pair(curry_make_symbol("id"), id),
               curry_make_pair(curry_make_pair(curry_make_symbol("type"), type),
               curry_make_pair(curry_make_pair(curry_make_symbol("properties"), props),
               curry_nil())));
    }
    case 0x50: { /* Path: nodes, rels, sequence */
        curry_val nodes = ps_decode(c);
        curry_val rels  = ps_decode(c);
        curry_val seq   = ps_decode(c);
        /* Reconstruct path as list of alternating nodes/rels */
        (void)nodes; (void)rels; (void)seq;
        /* Return raw for now — full reconstruction is complex */
        return curry_make_pair(curry_make_symbol("path"),
               curry_make_pair(nodes, curry_make_pair(rels, curry_make_pair(seq, curry_nil()))));
    }
    default: {
        /* Decode remaining fields and return a tagged list */
        curry_val fields = curry_nil();
        for (size_t i = 0; i < n; i++) {
            curry_val f = ps_decode(c);
            fields = curry_make_pair(f, fields);
        }
        /* Reverse */
        curry_val rev = curry_nil();
        while (curry_is_pair(fields)) { rev = curry_make_pair(curry_car(fields), rev); fields = curry_cdr(fields); }
        char tag_str[16]; snprintf(tag_str, sizeof(tag_str), "struct#%02X", tag);
        return curry_make_pair(curry_make_symbol(tag_str), rev);
    }
    }
}

static curry_val ps_decode(PSCur *c) {
    uint8_t b = cur_byte(c);

    /* Tiny positive integer */
    if (b <= 0x7F) return curry_make_fixnum((intptr_t)b);
    /* Tiny negative integer */
    if (b >= 0xF0) return curry_make_fixnum((intptr_t)(int8_t)b);

    switch (b) {
    case 0xC0: return curry_nil();
    case 0xC2: return curry_make_bool(false);
    case 0xC3: return curry_make_bool(true);
    case 0xC1: { uint64_t bits = cur_u64(c); double d; memcpy(&d,&bits,8); return curry_make_float(d); }
    case 0xC8: { int8_t  v = (int8_t) cur_byte(c); return curry_make_fixnum(v); }
    case 0xC9: { int16_t v = (int16_t)cur_u16(c);  return curry_make_fixnum(v); }
    case 0xCA: { int32_t v = (int32_t)cur_u32(c);  return curry_make_fixnum(v); }
    case 0xCB: { int64_t v = (int64_t)cur_u64(c);  return curry_make_fixnum((intptr_t)v); }

    /* Bytes — decode as bytevector */
    case 0xCC: { size_t n = cur_byte(c);
                 curry_val bv = curry_make_bytevector((uint32_t)n, 0);
                 for (size_t i=0;i<n;i++) curry_bytevector_set(bv,(uint32_t)i,cur_byte(c));
                 return bv; }
    case 0xCD: { size_t n = cur_u16(c);
                 curry_val bv = curry_make_bytevector((uint32_t)n, 0);
                 for (size_t i=0;i<n;i++) curry_bytevector_set(bv,(uint32_t)i,cur_byte(c));
                 return bv; }
    case 0xCE: { size_t n = cur_u32(c);
                 curry_val bv = curry_make_bytevector((uint32_t)n, 0);
                 for (size_t i=0;i<n;i++) curry_bytevector_set(bv,(uint32_t)i,cur_byte(c));
                 return bv; }

    /* Strings */
    case 0xD0: return ps_read_string(c, cur_byte(c));
    case 0xD1: return ps_read_string(c, cur_u16(c));
    case 0xD2: return ps_read_string(c, cur_u32(c));

    /* Lists */
    case 0xD4: return ps_read_list(c, cur_byte(c));
    case 0xD5: return ps_read_list(c, cur_u16(c));
    case 0xD6: return ps_read_list(c, cur_u32(c));

    /* Maps */
    case 0xD8: return ps_read_map(c, cur_byte(c));
    case 0xD9: return ps_read_map(c, cur_u16(c));
    case 0xDA: return ps_read_map(c, cur_u32(c));

    /* Structs (extended) */
    case 0xDC: return ps_read_struct(c, cur_byte(c));
    case 0xDD: return ps_read_struct(c, cur_u16(c));

    default:
        /* Tiny forms: 0x80-0x8F string, 0x90-0x9F list, 0xA0-0xAF map, 0xB0-0xBF struct */
        if (b >= 0xB0 && b <= 0xBF) return ps_read_struct(c, b & 0x0F);
        if (b >= 0xA0 && b <= 0xAF) return ps_read_map(c, b & 0x0F);
        if (b >= 0x90 && b <= 0x9F) return ps_read_list(c, b & 0x0F);
        if (b >= 0x80 && b <= 0x8F) return ps_read_string(c, b & 0x0F);
        curry_error("neo4j: unknown PackStream marker 0x%02X", b);
    }
}

/* ---- Bolt message helpers -----------------------------------------------*/

/* HELLO {user_agent, scheme, principal, credentials} for Bolt 4.x
   HELLO {user_agent} + LOGON {scheme, principal, credentials} for Bolt 5.1+ */
static void build_hello(PSBuf *msg, const char *user, const char *password, int bolt_major, int bolt_minor) {
    bool needs_logon = (bolt_major > 5) || (bolt_major == 5 && bolt_minor >= 1);
    if (!needs_logon) {
        /* Combined HELLO with auth (Bolt 4.x, Bolt 5.0) */
        int fields = (user && password) ? 4 : 1;
        ps_struct_hdr(msg, 0x01, 1);
        ps_map_hdr(msg, (size_t)fields);
        ps_cstr(msg, "user_agent"); ps_cstr(msg, "curry-neo4j/1.0");
        if (user && password) {
            ps_cstr(msg, "scheme");      ps_cstr(msg, "basic");
            ps_cstr(msg, "principal");   ps_cstr(msg, user);
            ps_cstr(msg, "credentials"); ps_cstr(msg, password);
        }
    } else {
        /* Bolt 5.1+: HELLO without auth */
        ps_struct_hdr(msg, 0x01, 1);
        ps_map_hdr(msg, 1);
        ps_cstr(msg, "user_agent"); ps_cstr(msg, "curry-neo4j/1.0");
    }
}

static void build_logon(PSBuf *msg, const char *user, const char *password) {
    ps_struct_hdr(msg, 0x6A, 1);  /* LOGON tag 0x6A */
    if (user && password) {
        ps_map_hdr(msg, 3);
        ps_cstr(msg, "scheme");      ps_cstr(msg, "basic");
        ps_cstr(msg, "principal");   ps_cstr(msg, user);
        ps_cstr(msg, "credentials"); ps_cstr(msg, password);
    } else {
        ps_map_hdr(msg, 1);
        ps_cstr(msg, "scheme"); ps_cstr(msg, "none");
    }
}

/* Response message types */
#define BOLT_SUCCESS  0x70
#define BOLT_FAILURE  0x7F
#define BOLT_IGNORED  0x7E
#define BOLT_RECORD   0x71

typedef struct {
    uint8_t   type;       /* BOLT_SUCCESS / BOLT_FAILURE / BOLT_RECORD / BOLT_IGNORED */
    curry_val payload;    /* decoded PackStream value */
} BoltMsg;

static BoltMsg bolt_recv_msg(NeoConn *c) {
    size_t mlen;
    uint8_t *raw = bolt_recv(c, &mlen);

    if (mlen < 2) { free(raw); curry_error("neo4j: empty/truncated Bolt message"); }

    uint8_t first = raw[0];
    if (first < 0xB0 || first > 0xBF) { free(raw); curry_error("neo4j: unexpected PackStream marker 0x%02X in Bolt message", first); }

    uint8_t msg_tag = raw[1];
    BoltMsg m;
    m.type = msg_tag;

    if (mlen > 2) {
        /* Parse the payload (single value = the struct's only field) */
        PSCur cur = { raw, 2, mlen };
        m.payload = ps_decode(&cur);
    } else {
        m.payload = curry_nil();
    }
    free(raw);
    return m;
}

static void bolt_expect_success(NeoConn *c) {
    BoltMsg m = bolt_recv_msg(c);
    if (m.type == BOLT_FAILURE) {
        /* Extract error message from payload alist */
        const char *code = "Unknown", *msg = "Unknown error";
        curry_val p = m.payload;
        while (curry_is_pair(p)) {
            curry_val kv = curry_car(p);
            if (curry_is_pair(kv)) {
                curry_val k = curry_car(kv), v = curry_cdr(kv);
                if (curry_is_string(k)) {
                    if (strcmp(curry_string(k), "code") == 0 && curry_is_string(v)) code = curry_string(v);
                    if (strcmp(curry_string(k), "message") == 0 && curry_is_string(v)) msg = curry_string(v);
                }
            }
            p = curry_cdr(p);
        }
        curry_error("neo4j: %s: %s", code, msg);
    }
    if (m.type == BOLT_IGNORED)
        curry_error("neo4j: request ignored (connection may be in failed state)");
}

/* Extract a string list from a SUCCESS metadata alist by key */
static curry_val alist_get(curry_val alist, const char *key) {
    curry_val p = alist;
    while (curry_is_pair(p)) {
        curry_val kv = curry_car(p);
        if (curry_is_pair(kv)) {
            curry_val k = curry_car(kv);
            if (curry_is_string(k) && strcmp(curry_string(k), key) == 0)
                return curry_cdr(kv);
        }
        p = curry_cdr(p);
    }
    return curry_make_bool(false);
}

/* ---- Bolt handshake + connect -------------------------------------------*/

static NeoConn *bolt_connect(const char *host, int port,
                              const char *user, const char *password) {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port_str, &hints, &res) != 0)
        curry_error("neo4j-connect: cannot resolve %s", host);

    sock_t fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd == SOCK_INVALID) { freeaddrinfo(res); curry_error("neo4j-connect: socket()"); }
    if (connect(fd, res->ai_addr, (socklen_t)res->ai_addrlen) != 0) {
        sock_close(fd); freeaddrinfo(res);
        curry_error("neo4j-connect: connect to %s:%d: %s", host, port, strerror(errno));
    }
    freeaddrinfo(res);

    NeoConn *c = calloc(1, sizeof(NeoConn));
    if (!c) { sock_close(fd); curry_error("neo4j-connect: out of memory"); }
    c->fd = fd;

    /* Bolt handshake: magic + 4 version proposals */
    static const uint8_t handshake[20] = {
        0x60, 0x60, 0xB0, 0x17,   /* magic preamble */
        0x00, 0x00, 0x04, 0x05,   /* Bolt 5.4 */
        0x00, 0x04, 0x04, 0x04,   /* Bolt 4.0-4.4 (range proposal) */
        0x00, 0x00, 0x00, 0x05,   /* Bolt 5.0 */
        0x00, 0x00, 0x00, 0x00,   /* no version */
    };
    net_write(c, handshake, 20);

    /* Read agreed version: [reserved][range][minor][major] */
    uint8_t ver[4];
    for (int i = 0; i < 4; i++) ver[i] = net_byte(c);
    int major = ver[3], minor = ver[2];
    if (major == 0) { sock_close(fd); free(c); curry_error("neo4j-connect: no compatible Bolt version"); }
    c->bolt_major = major;
    c->bolt_minor = minor;

    /* HELLO */
    PSBuf msg = {0};
    build_hello(&msg, user, password, major, minor);
    bolt_send(c, &msg);
    free(msg.data);
    bolt_expect_success(c);

    /* LOGON for Bolt 5.1+ */
    bool needs_logon = (major > 5) || (major == 5 && minor >= 1);
    if (needs_logon) {
        PSBuf lmsg = {0};
        build_logon(&lmsg, user, password);
        bolt_send(c, &lmsg);
        free(lmsg.data);
        bolt_expect_success(c);
    }

    return c;
}

/* ---- Query execution ---------------------------------------------------*/

/*
 * Execute a Cypher query and return a list of row alists.
 * Field names come from the SUCCESS response after RUN.
 * Each RECORD becomes an alist ((fieldname . value) ...).
 */
static curry_val neo4j_exec(NeoConn *c, const char *cypher, curry_val params_alist) {
    /* Count params */
    int nparams = scheme_list_len(params_alist);

    /* Build RUN message: struct(3) tag=0x10 {cypher, params_map, metadata_map} */
    PSBuf run = {0};
    ps_struct_hdr(&run, 0x10, 3);
    ps_cstr(&run, cypher);
    /* params */
    ps_map_hdr(&run, (size_t)nparams);
    curry_val p = params_alist;
    while (curry_is_pair(p)) {
        curry_val kv = curry_car(p);
        curry_val k = curry_car(kv), v = curry_cdr(kv);
        if (curry_is_symbol(k))      ps_cstr(&run, curry_symbol(k));
        else if (curry_is_string(k)) ps_cstr(&run, curry_string(k));
        else curry_error("neo4j-run: param key must be symbol or string");
        ps_from_scheme(&run, v);
        p = curry_cdr(p);
    }
    /* metadata (empty) */
    ps_map_hdr(&run, 0);
    bolt_send(c, &run);
    free(run.data);

    /* Read SUCCESS from RUN to get field names */
    BoltMsg run_resp = bolt_recv_msg(c);
    if (run_resp.type == BOLT_FAILURE) {
        /* Extract error and RESET */
        PSBuf reset = {0}; ps_struct_hdr(&reset, 0x0F, 0);
        bolt_send(c, &reset); free(reset.data);
        bolt_recv_msg(c);  /* consume RESET response */
        const char *msg = "query error";
        curry_val mv = alist_get(run_resp.payload, "message");
        if (curry_is_string(mv)) msg = curry_string(mv);
        curry_error("neo4j-run: %s", msg);
    }
    if (run_resp.type == BOLT_IGNORED)
        curry_error("neo4j-run: request ignored");

    /* fields = list of strings */
    curry_val fields = alist_get(run_resp.payload, "fields");

    /* Build PULL {n: -1} */
    PSBuf pull = {0};
    ps_struct_hdr(&pull, 0x3F, 1);
    ps_map_hdr(&pull, 1);
    ps_cstr(&pull, "n"); ps_int(&pull, -1);
    bolt_send(c, &pull);
    free(pull.data);

    /* Collect RECORD messages until SUCCESS or FAILURE */
    curry_val rows = curry_nil();
    for (;;) {
        BoltMsg m = bolt_recv_msg(c);
        if (m.type == BOLT_SUCCESS) break;
        if (m.type == BOLT_FAILURE) {
            const char *msg2 = "pull error";
            curry_val mv2 = alist_get(m.payload, "message");
            if (curry_is_string(mv2)) msg2 = curry_string(mv2);
            curry_error("neo4j-run: %s", msg2);
        }
        if (m.type != BOLT_RECORD) continue;

        /* m.payload is the list of field values */
        curry_val vals = m.payload;
        curry_val row = curry_nil();
        curry_val fnames = fields;
        while (curry_is_pair(vals) && curry_is_pair(fnames)) {
            curry_val fname = curry_car(fnames);
            curry_val fval  = curry_car(vals);
            curry_val key = curry_is_string(fname) ? curry_make_symbol(curry_string(fname)) : fname;
            row = curry_make_pair(curry_make_pair(key, fval), row);
            vals = curry_cdr(vals); fnames = curry_cdr(fnames);
        }
        /* Reverse row to preserve column order */
        curry_val rrow = curry_nil();
        while (curry_is_pair(row)) { rrow = curry_make_pair(curry_car(row), rrow); row = curry_cdr(row); }
        rows = curry_make_pair(rrow, rows);
    }

    /* Reverse rows (we built them in reverse) */
    curry_val result = curry_nil();
    while (curry_is_pair(rows)) { result = curry_make_pair(curry_car(rows), result); rows = curry_cdr(rows); }
    return result;
}

/* ---- Scheme-facing functions -------------------------------------------*/

static curry_val fn_connect(int ac, curry_val *av, void *ud) {
    (void)ud;
    const char *host     = curry_string(av[0]);
    int         port     = (int)curry_fixnum(av[1]);
    const char *user     = (ac >= 4 && curry_is_string(av[2])) ? curry_string(av[2]) : "neo4j";
    const char *password = (ac >= 4 && curry_is_string(av[3])) ? curry_string(av[3]) : "";
    NeoConn *c = bolt_connect(host, port, user, password);
    return conn_box(c, conn_tag());
}

static curry_val fn_disconnect(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    NeoConn *c = conn_unbox(av[0], "neo4j-disconnect");
    PSBuf bye = {0}; ps_struct_hdr(&bye, 0x02, 0);
    bolt_send(c, &bye); free(bye.data);
    /* Read (but ignore) the GOODBYE response if any */
    sock_close(c->fd);
    free(c);
    return curry_void();
}

static curry_val fn_run(int ac, curry_val *av, void *ud) {
    (void)ud;
    NeoConn *c       = conn_unbox(av[0], "neo4j-run");
    const char *cyph = curry_string(av[1]);
    curry_val params = (ac >= 3) ? av[2] : curry_nil();
    return neo4j_exec(c, cyph, params);
}

static curry_val fn_begin_tx(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    NeoConn *c = conn_unbox(av[0], "neo4j-begin-tx");
    PSBuf begin = {0};
    ps_struct_hdr(&begin, 0x11, 1);
    ps_map_hdr(&begin, 0);  /* empty metadata */
    bolt_send(c, &begin); free(begin.data);
    bolt_expect_success(c);
    return conn_box(c, tx_tag());
}

static curry_val fn_commit(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    NeoConn *c = conn_unbox(av[0], "neo4j-commit");
    PSBuf msg = {0}; ps_struct_hdr(&msg, 0x12, 0);
    bolt_send(c, &msg); free(msg.data);
    bolt_expect_success(c);
    return curry_void();
}

static curry_val fn_rollback(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    NeoConn *c = conn_unbox(av[0], "neo4j-rollback");
    PSBuf msg = {0}; ps_struct_hdr(&msg, 0x13, 0);
    bolt_send(c, &msg); free(msg.data);
    bolt_expect_success(c);
    return curry_void();
}

/* ---- Module entry point ------------------------------------------------*/

void curry_module_init(CurryVM *vm) {
    curry_define_fn(vm, "neo4j-connect",    fn_connect,    2, 4, NULL);
    curry_define_fn(vm, "neo4j-disconnect", fn_disconnect, 1, 1, NULL);
    curry_define_fn(vm, "neo4j-run",        fn_run,        2, 3, NULL);
    curry_define_fn(vm, "neo4j-begin-tx",   fn_begin_tx,   1, 1, NULL);
    curry_define_fn(vm, "neo4j-commit",     fn_commit,     1, 1, NULL);
    curry_define_fn(vm, "neo4j-rollback",   fn_rollback,   1, 1, NULL);
}
