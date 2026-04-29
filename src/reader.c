#include "reader.h"
#include "object.h"
#include "port.h"
#include "symbol.h"
#include "numeric.h"
#include "gc.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>

/* ---- Error handling ---- */
/* Defined in eval.c; raises a read-error exception */
extern void scm_raise(val_t kind, const char *fmt, ...) __attribute__((noreturn));

static void __attribute__((noreturn)) read_error(const char *msg) {
    scm_raise(V_FALSE, "read-error: %s", msg);
}

/* ---- Low-level character helpers ---- */

static int next_char(val_t port) { return port_read_char(port); }
static int peek_char_port(val_t port) { return port_peek_char(port); }

static void skip_whitespace(val_t port) {
    int c;
    while ((c = peek_char_port(port)) != -1) {
        if (c == ';') {
            /* Line comment: skip to end of line */
            while ((c = next_char(port)) != -1 && c != '\n') {}
        } else if (isspace(c)) {
            next_char(port);
        } else {
            break;
        }
    }
}

/* ---- Dynamic string builder ---- */
typedef struct { char *buf; size_t len; size_t cap; } StrBuf;

static void sb_init(StrBuf *sb) {
    sb->cap = 64; sb->len = 0;
    sb->buf = (char *)gc_alloc_atomic(sb->cap);
}
static void sb_push(StrBuf *sb, char c) {
    if (sb->len + 1 >= sb->cap) {
        sb->cap *= 2;
        char *nb = (char *)gc_alloc_atomic(sb->cap);
        memcpy(nb, sb->buf, sb->len);
        sb->buf = nb;
    }
    sb->buf[sb->len++] = c;
}
static void sb_push_utf8(StrBuf *sb, uint32_t cp) {
    if (cp < 0x80)   { sb_push(sb, (char)cp); }
    else if (cp < 0x800) {
        sb_push(sb, (char)(0xC0 | (cp>>6)));
        sb_push(sb, (char)(0x80 | (cp&0x3F)));
    } else if (cp < 0x10000) {
        sb_push(sb, (char)(0xE0 | (cp>>12)));
        sb_push(sb, (char)(0x80 | ((cp>>6)&0x3F)));
        sb_push(sb, (char)(0x80 | (cp&0x3F)));
    } else {
        sb_push(sb, (char)(0xF0 | (cp>>18)));
        sb_push(sb, (char)(0x80 | ((cp>>12)&0x3F)));
        sb_push(sb, (char)(0x80 | ((cp>>6)&0x3F)));
        sb_push(sb, (char)(0x80 | (cp&0x3F)));
    }
}

/* ---- Number parsing ---- */

static bool is_delimiter(int c) {
    return c == -1 || isspace(c) || c == '(' || c == ')' || c == '"' || c == ';' || c == '[' || c == ']';
}

val_t parse_number(const char *s, int radix, bool exact_force, bool inexact_force) {
    /* Try integer */
    char *end;
    errno = 0;
    /* Try fixnum range first */
    long i = strtol(s, &end, radix);
    if (errno == 0 && *end == '\0') {
        val_t v = in_fixnum_range(i) ? vfix(i) : num_make_bignum_i(i);
        return inexact_force ? num_inexact(v) : v;
    }
    /* Try bignum */
    if (*end == '\0' || (*end == '/' && *(end+1) != '\0')) {
        /* Try rational a/b */
        const char *slash = strchr(s, '/');
        if (slash) {
            char *num_part = (char *)gc_alloc_atomic((size_t)(slash - s) + 1);
            memcpy(num_part, s, (size_t)(slash - s));
            num_part[slash-s] = '\0';
            val_t n = parse_number(num_part, radix, false, false);
            val_t d = parse_number(slash + 1, radix, false, false);
            if (!vis_number(n) || !vis_number(d)) return V_FALSE;
            return num_make_rational(n, d);
        }
        /* GMP bignum */
        return num_make_bignum_str(s, radix);
    }
    /* Try float */
    double d = strtod(s, &end);
    if (*end == '\0') {
        val_t v = num_make_float(d);
        return exact_force ? num_exact(v) : v;
    }
    return V_FALSE; /* not a number */
}

/* ---- String reading ---- */

static val_t read_string(val_t port) {
    StrBuf sb; sb_init(&sb);
    int c;
    while ((c = next_char(port)) != -1 && c != '"') {
        if (c == '\\') {
            int esc = next_char(port);
            switch (esc) {
            case 'n':  sb_push(&sb, '\n'); break;
            case 't':  sb_push(&sb, '\t'); break;
            case 'r':  sb_push(&sb, '\r'); break;
            case '"':  sb_push(&sb, '"');  break;
            case '\\': sb_push(&sb, '\\'); break;
            case '0':  sb_push(&sb, '\0'); break;
            case 'a':  sb_push(&sb, '\a'); break;
            case 'b':  sb_push(&sb, '\b'); break;
            case 'x': {
                /* \xHHHH; hex escape */
                StrBuf hex; sb_init(&hex);
                while ((c = peek_char_port(port)) != -1 && c != ';') {
                    sb_push(&hex, (char)c); next_char(port);
                }
                if (peek_char_port(port) == ';') next_char(port);
                hex.buf[hex.len] = '\0';
                uint32_t cp = (uint32_t)strtoul(hex.buf, NULL, 16);
                sb_push_utf8(&sb, cp);
                break;
            }
            case '\n': /* line continuation: skip whitespace */
                while (isspace(peek_char_port(port))) next_char(port);
                break;
            default:
                read_error("unknown escape in string");
            }
        } else {
            sb_push_utf8(&sb, (uint32_t)c);
        }
    }
    if (c == -1) read_error("unterminated string literal");
    sb.buf[sb.len] = '\0';
    String *str = (String *)gc_alloc_atomic(sizeof(String) + sb.len + 1);
    str->hdr.type  = T_STRING;
    str->hdr.flags = 0;
    str->len  = (uint32_t)sb.len;
    str->hash = 0;
    memcpy(str->data, sb.buf, sb.len + 1);
    return vptr(str);
}

/* ---- Character reading ---- */

static val_t read_char_literal(val_t port) {
    /* #\ has been consumed.  Read the rest of the name. */
    StrBuf sb; sb_init(&sb);
    int c = next_char(port);
    if (c == -1) read_error("unexpected EOF in character literal");
    sb_push(&sb, (char)c);
    /* Peek: if more non-delimiter chars follow, it's a named char */
    while (!is_delimiter(peek_char_port(port))) {
        c = next_char(port); sb_push(&sb, (char)c);
    }
    sb.buf[sb.len] = '\0';

    if (sb.len == 1)              return vchr((uint32_t)(unsigned char)sb.buf[0]);
    if (!strcmp(sb.buf,"space"))  return vchr(' ');
    if (!strcmp(sb.buf,"newline"))return vchr('\n');
    if (!strcmp(sb.buf,"tab"))    return vchr('\t');
    if (!strcmp(sb.buf,"return")) return vchr('\r');
    if (!strcmp(sb.buf,"null"))   return vchr(0);
    if (!strcmp(sb.buf,"escape")) return vchr(27);
    if (!strcmp(sb.buf,"delete")) return vchr(127);
    if (!strcmp(sb.buf,"alarm"))  return vchr(7);
    if (!strcmp(sb.buf,"backspace")) return vchr(8);
    if (sb.buf[0] == 'x' && sb.len > 1) {
        uint32_t cp = (uint32_t)strtoul(sb.buf + 1, NULL, 16);
        return vchr(cp);
    }
    read_error("unknown character name");
}

/* ---- List reading ---- */

/* Forward declaration */
static val_t read_datum(val_t port);

static val_t read_list(val_t port, int close_ch) {
    skip_whitespace(port);
    int c = peek_char_port(port);
    if (c == close_ch) { next_char(port); return V_NIL; }
    if (c == -1)  read_error("unexpected EOF in list");

    val_t head = read_datum(port);
    skip_whitespace(port);

    c = peek_char_port(port);
    if (c == '.') {
        /* Could be dotted pair or a symbol starting with . */
        next_char(port);
        if (is_delimiter(peek_char_port(port))) {
            /* Dotted pair */
            skip_whitespace(port);
            val_t cdr = read_datum(port);
            skip_whitespace(port);
            if (next_char(port) != close_ch) read_error("expected closing paren after dot");
            /* Make a pair */
            Pair *p = CURRY_NEW(Pair);
            p->hdr.type = T_PAIR; p->hdr.flags = 0;
            p->car = head; p->cdr = cdr;
            return vptr(p);
        }
        /* It's a symbol starting with '.'; push back the dot */
        /* We can't unread, so handle it inline - read as symbol */
        StrBuf sb; sb_init(&sb);
        sb_push(&sb, '.');
        while (!is_delimiter(peek_char_port(port))) {
            sb_push(&sb, (char)next_char(port));
        }
        sb.buf[sb.len] = '\0';
        val_t sym = sym_intern(sb.buf, (uint32_t)sb.len);
        val_t rest2 = read_list(port, close_ch);
        Pair *p2 = CURRY_NEW(Pair);
        p2->hdr.type=T_PAIR; p2->hdr.flags=0; p2->car=sym; p2->cdr=rest2;
        Pair *outer = CURRY_NEW(Pair);
        outer->hdr.type=T_PAIR; outer->hdr.flags=0; outer->car=head; outer->cdr=vptr(p2);
        return vptr(outer);
    }

    val_t rest = read_list(port, close_ch);
    Pair *p = CURRY_NEW(Pair);
    p->hdr.type = T_PAIR; p->hdr.flags = 0;
    p->car = head; p->cdr = rest;
    return vptr(p);
}

/* ---- Vector / bytevector ---- */

static val_t read_vector(val_t port) {
    /* Read a list then convert to vector */
    val_t lst = read_list(port, ')');
    uint32_t len = 0;
    val_t tmp = lst;
    while (vis_pair(tmp)) { len++; tmp = vcdr(tmp); }
    Vector *v = CURRY_NEW_FLEX(Vector, len);
    v->hdr.type = T_VECTOR; v->hdr.flags = 0; v->len = len;
    for (uint32_t i = 0; i < len; i++) { v->data[i] = vcar(lst); lst = vcdr(lst); }
    return vptr(v);
}

static val_t read_bytevector(val_t port) {
    val_t lst = read_list(port, ')');
    uint32_t len = 0;
    val_t tmp = lst;
    while (vis_pair(tmp)) { len++; tmp = vcdr(tmp); }
    Bytevector *bv = CURRY_NEW_FLEX_ATOM(Bytevector, len);
    bv->hdr.type = T_BYTEVECTOR; bv->hdr.flags = 0; bv->len = len;
    for (uint32_t i = 0; i < len; i++) {
        intptr_t byte = vunfix(vcar(lst));
        if (byte < 0 || byte > 255) read_error("bytevector element out of range");
        bv->data[i] = (uint8_t)byte;
        lst = vcdr(lst);
    }
    return vptr(bv);
}

/* ---- Shorthand wrappers ---- */

static val_t wrap1(val_t sym, val_t v) {
    Pair *inner = CURRY_NEW(Pair);
    inner->hdr.type = T_PAIR; inner->hdr.flags = 0;
    inner->car = v; inner->cdr = V_NIL;
    Pair *outer = CURRY_NEW(Pair);
    outer->hdr.type = T_PAIR; outer->hdr.flags = 0;
    outer->car = sym; outer->cdr = vptr(inner);
    return vptr(outer);
}

/* ---- Main reader ---- */

static val_t read_datum(val_t port) {
    skip_whitespace(port);
    int c = next_char(port);
    if (c == -1) return V_EOF;

    switch (c) {
    case '(':  return read_list(port, ')');
    case '[':  return read_list(port, ']');
    case ')': case ']': read_error("unexpected closing delimiter");
    case '"':  return read_string(port);
    case '\'': return wrap1(S_QUOTE,            read_datum(port));
    case '`':  return wrap1(S_QUASIQUOTE,       read_datum(port));
    case ',': {
        int next = peek_char_port(port);
        if (next == '@') { next_char(port); return wrap1(S_UNQUOTE_SPLICING, read_datum(port)); }
        return wrap1(S_UNQUOTE, read_datum(port));
    }
    case '#': {
        int h = next_char(port);
        switch (h) {
        case 't': {
            /* #t or #true */
            int p = peek_char_port(port);
            if (p == 'r') { /* consume 'rue' */ for(int i=0;i<3;i++) next_char(port); }
            return V_TRUE;
        }
        case 'f': {
            int p = peek_char_port(port);
            if (p == 'a') { for(int i=0;i<4;i++) next_char(port); }
            return V_FALSE;
        }
        case '\\': return read_char_literal(port);
        case '(':  return read_vector(port);
        case 'u':  {
            /* #u8( */
            if (next_char(port) != '8' || next_char(port) != '(')
                read_error("expected #u8(");
            return read_bytevector(port);
        }
        case '|': {
            /* Block comment #| ... |# (nestable) */
            int depth = 1, prev = 0;
            while (depth > 0) {
                int ch = next_char(port);
                if (ch == -1) read_error("unterminated block comment");
                if (prev == '#' && ch == '|') depth++;
                if (prev == '|' && ch == '#') depth--;
                prev = ch;
            }
            return read_datum(port); /* continue reading */
        }
        case ';': /* datum comment: skip next datum */
            read_datum(port);
            return read_datum(port);
        /* Numeric prefixes */
        case 'b': case 'B': {
            /* binary number follows */
            StrBuf sb; sb_init(&sb);
            while (!is_delimiter(peek_char_port(port))) sb_push(&sb, (char)next_char(port));
            sb.buf[sb.len]='\0';
            return parse_number(sb.buf, 2, false, false);
        }
        case 'o': case 'O': {
            StrBuf sb; sb_init(&sb);
            while (!is_delimiter(peek_char_port(port))) sb_push(&sb, (char)next_char(port));
            sb.buf[sb.len]='\0';
            return parse_number(sb.buf, 8, false, false);
        }
        case 'd': case 'D': {
            StrBuf sb; sb_init(&sb);
            while (!is_delimiter(peek_char_port(port))) sb_push(&sb, (char)next_char(port));
            sb.buf[sb.len]='\0';
            return parse_number(sb.buf, 10, false, false);
        }
        case 'x': case 'X': {
            StrBuf sb; sb_init(&sb);
            while (!is_delimiter(peek_char_port(port))) sb_push(&sb, (char)next_char(port));
            sb.buf[sb.len]='\0';
            return parse_number(sb.buf, 16, false, false);
        }
        case 'e': case 'E': {
            val_t v = read_datum(port);
            return num_exact(v);
        }
        case 'i': case 'I': {
            val_t v = read_datum(port);
            return num_inexact(v);
        }
        default:
            read_error("unknown # syntax");
        }
        break;
    }
    default: {
        /* Symbol or number */
        StrBuf sb; sb_init(&sb);
        sb_push_utf8(&sb, (uint32_t)c);
        while (!is_delimiter(peek_char_port(port))) {
            sb_push_utf8(&sb, (uint32_t)next_char(port));
        }
        sb.buf[sb.len] = '\0';

        /* Special symbol literals */
        if (!strcmp(sb.buf, "+inf.0")) return num_make_float(1.0/0.0);
        if (!strcmp(sb.buf, "-inf.0")) return num_make_float(-1.0/0.0);
        if (!strcmp(sb.buf, "+nan.0") || !strcmp(sb.buf, "-nan.0"))
            return num_make_float(0.0/0.0);

        /* Try number */
        val_t num = parse_number(sb.buf, 10, false, false);
        if (!vis_false(num)) return num;

        /* Symbol */
        return sym_intern(sb.buf, (uint32_t)sb.len);
    }
    }
    return V_UNDEF;
}

val_t scm_read(val_t port) {
    return read_datum(port);
}

val_t scm_read_cstr(const char *src) {
    val_t p = port_open_input_string(src, (uint32_t)strlen(src));
    return read_datum(p);
}

val_t scm_read_all(val_t port) {
    /* Build list of all datums in reverse, then reverse */
    val_t head = V_NIL;
    val_t v;
    while (!vis_eof((v = read_datum(port)))) {
        Pair *p = CURRY_NEW(Pair);
        p->hdr.type = T_PAIR; p->hdr.flags = 0;
        p->car = v; p->cdr = head;
        head = vptr(p);
    }
    /* Reverse */
    val_t result = V_NIL;
    while (vis_pair(head)) {
        Pair *p = CURRY_NEW(Pair);
        p->hdr.type = T_PAIR; p->hdr.flags = 0;
        p->car = vcar(head); p->cdr = result;
        result = vptr(p); head = vcdr(head);
    }
    return result;
}
