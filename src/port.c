#include "port.h"
#include "object.h"
#include "gc.h"
#include "numeric.h"
#include "symbol.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <inttypes.h>

extern void scm_raise(val_t kind, const char *fmt, ...) __attribute__((noreturn));

val_t PORT_STDIN, PORT_STDOUT, PORT_STDERR;

static val_t make_port(int flags) {
    Port *p = CURRY_NEW_PINNED(Port);
    p->hdr.type  = T_PORT;
    p->hdr.flags = 0;
    p->flags     = (uint8_t)flags;
    p->peeked_cp = -2;   /* -2 = lookahead empty; -1 = EOF; ≥0 = codepoint */
    p->line      = 1;
    return vptr(p);
}

void port_init(void) {
    PORT_STDIN  = port_wrap_file(stdin,  PORT_INPUT);
    PORT_STDOUT = port_wrap_file(stdout, PORT_OUTPUT);
    PORT_STDERR = port_wrap_file(stderr, PORT_OUTPUT);
}

val_t port_wrap_file(FILE *fp, int flags) {
    val_t v = make_port(flags);
    as_port(v)->u.fp = fp;
    return v;
}

static void port_gc_finalize(void *obj, void *cd) {
    (void)cd;
    Port *p = (Port *)obj;
    if (!(p->flags & PORT_CLOSED) && !(p->flags & PORT_STRING) &&
        p->u.fp && p->u.fp != stdin && p->u.fp != stdout && p->u.fp != stderr) {
        fclose(p->u.fp);
        p->u.fp = NULL;
        p->flags |= PORT_CLOSED;
    }
}

/* Wrap an already-open FILE* (any kind — plain fd, funopen/fopencookie
 * custom stream, etc.) as a port and register the auto-close finalizer,
 * same as port_open_file's own bookkeeping. The single place that owns
 * "this port is responsible for eventually fclose()-ing its FILE*". */
val_t port_wrap_file_owned(FILE *fp, int flags) {
    val_t v = make_port(flags);
    as_port(v)->u.fp = fp;
    gc_finalizer(as_port(v), port_gc_finalize, NULL);
    return v;
}

static const char *mode_str(int flags) {
    if ((flags & PORT_BINARY))
        return (flags & PORT_OUTPUT) ? "wb" : "rb";
    return (flags & PORT_OUTPUT) ? "w" : "r";
}

val_t port_open_file(const char *path, int flags) {
    FILE *fp = fopen(path, mode_str(flags));
    if (!fp) return V_FALSE;
    return port_wrap_file_owned(fp, flags);
}

val_t port_wrap_fd(int fd, int flags) {
    FILE *fp = fdopen(fd, mode_str(flags));
    if (!fp) return V_FALSE;
    return port_wrap_file_owned(fp, flags);
}

val_t port_open_input_string(const char *str, uint32_t len) {
    val_t v = make_port(PORT_INPUT | PORT_STRING);
    Port *p = as_port(v);
    p->u.str.buf = (char *)gc_alloc_raw_pinned_atomic(len + 1);
    memcpy(p->u.str.buf, str, len);
    p->u.str.buf[len] = '\0';
    p->u.str.pos = 0;
    p->u.str.len = len;
    p->u.str.cap = len;
    return v;
}

val_t port_open_output_string(void) {
    val_t v = make_port(PORT_OUTPUT | PORT_STRING);
    Port *p = as_port(v);
    p->u.str.cap = 64;
    p->u.str.buf = (char *)gc_alloc_raw_pinned_atomic(64);
    p->u.str.buf[0] = '\0';  /* gc_alloc_atomic doesn't zero memory */
    p->u.str.pos = 0;
    p->u.str.len = 0;
    return v;
}

bool port_is_input(val_t p)  { return !!(as_port(p)->flags & PORT_INPUT);  }
bool port_is_output(val_t p) { return !!(as_port(p)->flags & PORT_OUTPUT); }
bool port_is_binary(val_t p) { return !!(as_port(p)->flags & PORT_BINARY); }
bool port_is_open(val_t p)   { return !(as_port(p)->flags & PORT_CLOSED);  }

/* ---- Read/write UTF-8 char (encoded as codepoint) ---- */

static int read_utf8_codepoint(Port *p) {
    int c;
    if (p->flags & PORT_STRING) {
        if (p->u.str.pos >= p->u.str.len) return -1;
        c = (unsigned char)p->u.str.buf[p->u.str.pos++];
    } else {
        c = fgetc(p->u.fp);
        if (c == EOF) return -1;
    }
    /* Decode multi-byte UTF-8 */
    uint32_t cp = (uint32_t)c;
    int extra = 0;
    if      (c < 0x80)               { cp = (uint32_t)c; }
    else if ((c & 0xE0) == 0xC0)    { cp = (uint32_t)(c & 0x1F); extra = 1; }
    else if ((c & 0xF0) == 0xE0)    { cp = (uint32_t)(c & 0x0F); extra = 2; }
    else if ((c & 0xF8) == 0xF0)    { cp = (uint32_t)(c & 0x07); extra = 3; }
    for (int i = 0; i < extra; i++) {
        int b;
        if (p->flags & PORT_STRING) {
            if (p->u.str.pos >= p->u.str.len) return -1;
            b = (unsigned char)p->u.str.buf[p->u.str.pos++];
        } else {
            b = fgetc(p->u.fp);
            if (b == EOF) return -1;
        }
        cp = (cp << 6) | ((uint32_t)(b & 0x3F));
    }
    return (int)cp;
}

int port_read_char(val_t v) {
    Port *p = as_port(v);
    if (p->flags & PORT_CLOSED) return -1;
    int cp;
    /* Drain the one-codepoint lookahead if present */
    if (p->peeked_cp != -2) {
        cp = p->peeked_cp;
        p->peeked_cp = -2;
    } else {
        cp = read_utf8_codepoint(p);
    }
    if (cp == '\n') p->line++;
    return cp;  /* -1 (EOF) or the codepoint */
}

int port_line(val_t v) { return as_port(v)->line; }

int port_peek_char(val_t v) {
    Port *p = as_port(v);
    if (p->flags & PORT_CLOSED) return -1;
    /* If lookahead is already filled, return it without consuming */
    if (p->peeked_cp != -2) return p->peeked_cp;
    /* Read and buffer a full codepoint */
    if (p->flags & PORT_STRING) {
        if (p->u.str.pos >= p->u.str.len) { p->peeked_cp = -1; return -1; }
        size_t saved = p->u.str.pos;
        int c = read_utf8_codepoint(p);
        p->u.str.pos = saved;          /* restore — no lookahead needed for strings */
        return c;                       /* string ports can restore position directly */
    }
    /* File port: read a full UTF-8 codepoint into the lookahead */
    p->peeked_cp = read_utf8_codepoint(p);
    return p->peeked_cp;
}

bool port_char_ready(val_t v) {
    Port *p = as_port(v);
    if (p->flags & PORT_STRING) return p->u.str.pos < p->u.str.len;
    return true; /* conservative */
}

void port_write_char(val_t v, int cp) {
    Port *p = as_port(v);
    if (p->flags & PORT_CLOSED) return;
    /* Encode as UTF-8 */
    char buf[5]; int len;
    uint32_t u = (uint32_t)cp;
    if      (u < 0x80)    { buf[0]=(char)u; len=1; }
    else if (u < 0x800)   { buf[0]=(char)(0xC0|(u>>6)); buf[1]=(char)(0x80|(u&0x3F)); len=2; }
    else if (u < 0x10000) { buf[0]=(char)(0xE0|(u>>12)); buf[1]=(char)(0x80|((u>>6)&0x3F)); buf[2]=(char)(0x80|(u&0x3F)); len=3; }
    else { buf[0]=(char)(0xF0|(u>>18)); buf[1]=(char)(0x80|((u>>12)&0x3F)); buf[2]=(char)(0x80|((u>>6)&0x3F)); buf[3]=(char)(0x80|(u&0x3F)); len=4; }
    buf[len] = '\0';
    if (p->flags & PORT_STRING) {
        while (p->u.str.len + (size_t)len + 1 >= p->u.str.cap) {
            p->u.str.cap *= 2;
            char *nb = (char *)gc_alloc_raw_pinned_atomic(p->u.str.cap);
            memcpy(nb, p->u.str.buf, p->u.str.len);
            p->u.str.buf = nb;
        }
        memcpy(p->u.str.buf + p->u.str.len, buf, (size_t)len);
        p->u.str.len += (size_t)len;
        p->u.str.buf[p->u.str.len] = '\0';
    } else {
        fwrite(buf, 1, (size_t)len, p->u.fp);
    }
}

void port_write_string(val_t v, const char *s, uint32_t len) {
    Port *p = as_port(v);
    if (p->flags & PORT_CLOSED) return;
    if (p->flags & PORT_STRING) {
        while (p->u.str.len + len + 1 >= p->u.str.cap) {
            p->u.str.cap *= 2;
            char *nb = (char *)gc_alloc_raw_pinned_atomic(p->u.str.cap);
            memcpy(nb, p->u.str.buf, p->u.str.len);
            p->u.str.buf = nb;
        }
        memcpy(p->u.str.buf + p->u.str.len, s, len);
        p->u.str.len += len;
        p->u.str.buf[p->u.str.len] = '\0';
    } else {
        fwrite(s, 1, len, p->u.fp);
    }
}

val_t port_read_line(val_t v) {
    Port *p = as_port(v);
    if (p->flags & PORT_CLOSED || p->flags & PORT_BINARY) return V_EOF;
    /* Read until newline or EOF */
    val_t out = port_open_output_string();
    int cp;
    bool any = false;
    while ((cp = read_utf8_codepoint(p)) != -1) {
        if (cp == '\n') break;
        port_write_char(out, cp);
        any = true;
    }
    if (!any && cp == -1) return V_EOF;
    return port_get_output_string(out);
}

int port_read_byte(val_t v) {
    Port *p = as_port(v);
    if (p->flags & PORT_CLOSED) return -1;
    if (p->flags & PORT_STRING) {
        if (p->u.str.pos >= p->u.str.len) return -1;
        return (unsigned char)p->u.str.buf[p->u.str.pos++];
    }
    int c = fgetc(p->u.fp);
    return c == EOF ? -1 : (unsigned char)c;
}

int port_peek_byte(val_t v) {
    Port *p = as_port(v);
    if (p->flags & PORT_CLOSED) return -1;
    if (p->flags & PORT_STRING) {
        if (p->u.str.pos >= p->u.str.len) return -1;
        return (unsigned char)p->u.str.buf[p->u.str.pos];
    }
    int c = fgetc(p->u.fp);
    if (c != EOF) ungetc(c, p->u.fp);
    return c == EOF ? -1 : (unsigned char)c;
}

void port_write_byte(val_t v, uint8_t b) {
    Port *p = as_port(v);
    if (p->flags & PORT_CLOSED) return;
    if (p->flags & PORT_STRING) {
        /* Binary string port: append raw byte without UTF-8 encoding. */
        if (p->flags & PORT_BINARY) {
            while (p->u.str.len + 2 >= p->u.str.cap) {
                p->u.str.cap = p->u.str.cap < 16 ? 16 : p->u.str.cap * 2;
                char *nb = (char *)gc_alloc_raw_pinned_atomic(p->u.str.cap);
                memcpy(nb, p->u.str.buf, p->u.str.len);
                p->u.str.buf = nb;
            }
            p->u.str.buf[p->u.str.len++] = (char)b;
            p->u.str.buf[p->u.str.len]   = '\0';
        } else {
            port_write_char(v, b);
        }
    } else {
        fputc(b, p->u.fp);
    }
}

void port_close(val_t v) {
    Port *p = as_port(v);
    if (p->flags & PORT_CLOSED) return;
    if (!(p->flags & PORT_STRING) && p->u.fp &&
        p->u.fp != stdin && p->u.fp != stdout && p->u.fp != stderr) {
        fclose(p->u.fp);
        p->u.fp = NULL;
    }
    p->flags |= PORT_CLOSED;
}

val_t port_open_input_bytevector(const uint8_t *data, uint32_t len) {
    val_t v = make_port(PORT_INPUT | PORT_BINARY | PORT_STRING);
    Port *p = as_port(v);
    p->u.str.buf = (char *)gc_alloc_raw_pinned_atomic(len + 1);
    memcpy(p->u.str.buf, data, len);
    p->u.str.buf[len] = '\0';
    p->u.str.pos = 0;
    p->u.str.len = len;
    p->u.str.cap = len;
    return v;
}

val_t port_open_output_bytevector(void) {
    val_t v = make_port(PORT_OUTPUT | PORT_BINARY | PORT_STRING);
    Port *p = as_port(v);
    p->u.str.cap = 64;
    p->u.str.buf = (char *)gc_alloc_raw_pinned_atomic(64);
    p->u.str.buf[0] = '\0';
    p->u.str.pos = 0;
    p->u.str.len = 0;
    return v;
}

val_t port_get_output_string(val_t v) {
    Port *p = as_port(v);
    if (!(p->flags & PORT_STRING))
        scm_raise(V_FALSE, "get-output-string: not a string port");
    uint32_t len = (uint32_t)p->u.str.len;
    String *s = (String *)gc_alloc_atomic(sizeof(String) + len + 1);
    s->hdr.type  = T_STRING;
    s->hdr.flags = 0;
    s->len  = len;
    s->hash = 0;
    s->orig_cap = len;
    s->ext = NULL;
    memcpy(s->data, p->u.str.buf, len + 1);
    return vptr(s);
}

val_t port_get_output_bytevector(val_t v) {
    Port *p = as_port(v);
    if (!(p->flags & PORT_STRING) || !(p->flags & PORT_BINARY))
        scm_raise(V_FALSE, "get-output-bytevector: not a binary output port");
    uint32_t len = (uint32_t)p->u.str.len;
    Bytevector *bv = (Bytevector *)gc_alloc_atomic(sizeof(Bytevector) + len);
    bv->hdr.type  = T_BYTEVECTOR;
    bv->hdr.flags = 0;
    bv->len = len;
    memcpy(bv->data, p->u.str.buf, len);
    return vptr(bv);
}

/* ---- Display / Write ---- */

static void write_string_escaped(val_t port, const char *s, uint32_t len) {
    /* Scans byte-by-byte (safe: every UTF-8 continuation/lead byte is
       >= 0x80, so it can never be mistaken for one of the ASCII escape
       triggers below), but a pass-through byte must reach the port as a
       raw byte via port_write_string, not via port_write_char — the
       latter treats its argument as a full Unicode codepoint and
       UTF-8-encodes it, which double-encodes any multi-byte character
       one raw byte at a time. */
    port_write_char(port, '"');
    for (uint32_t i = 0; i < len; i++) {
        char c = s[i];
        if      (c == '"')  { port_write_string(port, "\\\"", 2); }
        else if (c == '\\') { port_write_string(port, "\\\\", 2); }
        else if (c == '\n') { port_write_string(port, "\\n", 2);  }
        else if (c == '\r') { port_write_string(port, "\\r", 2);  }
        else if (c == '\t') { port_write_string(port, "\\t", 2);  }
        else                { port_write_string(port, s + i, 1);  }
    }
    port_write_char(port, '"');
}

/* Write a number val (fixnum/bignum/rational/flonum) respecting current-number-notation. */
static void write_number_notation(val_t v, val_t port) {
    if (g_number_notation != 0 && vis_symbol(g_number_notation)) {
        const char *note = as_sym(g_number_notation)->data;
        val_t s = V_FALSE;
        if (strcmp(note, "neugebauer") == 0 || strcmp(note, "Neugebauer") == 0)
            s = sex_to_neugebauer(v, -1);
        else if (strcmp(note, "cuneiform") == 0)
            s = sex_to_cuneiform(v);
        if (!vis_false(s)) {
            port_write_string(port, str_data(as_str(s)), as_str(s)->len);
            return;
        }
    }
    /* Fallback: decimal */
    char buf[64];
    if (vis_fixnum(v))   { int n=snprintf(buf,sizeof(buf),"%ld",(long)vunfix(v)); port_write_string(port,buf,(uint32_t)n); }
    else if (vis_bignum(v))   { char *s=mpz_get_str(NULL,10,as_big(v)->z); port_write_string(port,s,(uint32_t)strlen(s)); free(s); }
    else if (vis_rational(v)) { char *s=mpq_get_str(NULL,10,as_rat(v)->q); port_write_string(port,s,(uint32_t)strlen(s)); free(s); }
    else if (vis_flonum(v))   { int n=num_flonum_to_shortest_cstr(vfloat(v),buf,sizeof(buf)); port_write_string(port,buf,(uint32_t)n); }
}

void scm_write(val_t v, val_t port) {
    char buf[64];
    if (vis_nil(v))      { port_write_string(port, "()", 2); return; }
    if (vis_void(v))     { return; /* unspecified - write nothing */ }
    if (vis_eof(v))      { port_write_string(port, "#<eof>", 6); return; }
    if (v == V_FALSE)    { port_write_string(port, "#f", 2); return; }
    if (v == V_TRUE)     { port_write_string(port, "#t", 2); return; }
    if (vis_fixnum(v))   { write_number_notation(v, port); return; }
    if (vis_char(v))     {
        uint32_t cp = vunchr(v);
        port_write_string(port, "#\\", 2);
        if      (cp == ' ')  { port_write_string(port, "space", 5);   }
        else if (cp == '\n') { port_write_string(port, "newline", 7); }
        else if (cp == '\t') { port_write_string(port, "tab", 3);     }
        else                 { port_write_char(port, (int)cp);          }
        return;
    }
    if (vis_string(v))   { write_string_escaped(port, str_data(as_str(v)), as_str(v)->len); return; }
    if (vis_symbol(v))   { port_write_string(port, as_sym(v)->data, as_sym(v)->len); return; }
    if (vis_flonum(v))   { write_number_notation(v, port); return; }
    if (vis_bignum(v))   { write_number_notation(v, port); return; }
    if (vis_rational(v)) { write_number_notation(v, port); return; }
#ifdef BUILD_MPFR
    if (vis_mpfr(v)) {
        val_t s = num_to_string(v, 10);
        port_write_string(port, str_data(as_str(s)), as_str(s)->len);
        return;
    }
    if (vis_ival(v)) {
        port_write_string(port, "#<interval ", 11);
        scm_write(as_ival(v)->lo, port);
        port_write_string(port, " ", 1);
        scm_write(as_ival(v)->hi, port);
        port_write_string(port, ">", 1);
        return;
    }
#endif
    if (vis_complex(v)) {
        scm_write(as_cpx(v)->real, port);
        val_t im = as_cpx(v)->imag;
        if (!num_is_negative(im)) port_write_char(port, '+');
        scm_write(im, port);
        port_write_char(port, 'i');
        return;
    }
    if (vis_quat(v)) {
        Quaternion *q = as_quat(v);
        int n = snprintf(buf, sizeof(buf), "%g%s%gi%s%gj%s%gk",
            q->a,
            q->b >= 0 ? "+" : "", q->b,
            q->c >= 0 ? "+" : "", q->c,
            q->d >= 0 ? "+" : "", q->d);
        port_write_string(port, buf, (uint32_t)n); return;
    }
    if (vis_oct(v)) {
        Octonion *o = as_oct(v);
        port_write_string(port, "#oct(", 5);
        for (int i = 0; i < 8; i++) {
            int n = snprintf(buf, sizeof(buf), "%s%g", i?",":"", o->e[i]);
            port_write_string(port, buf, (uint32_t)n);
        }
        port_write_char(port, ')'); return;
    }
    if (vis_pair(v)) {
        port_write_char(port, '(');
        scm_write(vcar(v), port);
        val_t rest = vcdr(v);
        while (vis_pair(rest)) {
            port_write_char(port, ' ');
            scm_write(vcar(rest), port);
            rest = vcdr(rest);
        }
        if (!vis_nil(rest)) {
            port_write_string(port, " . ", 3);
            scm_write(rest, port);
        }
        port_write_char(port, ')');
        return;
    }
    if (vis_vector(v)) {
        Vector *vec = as_vec(v);
        port_write_string(port, "#(", 2);
        for (uint32_t i = 0; i < vec->len; i++) {
            if (i) port_write_char(port, ' ');
            scm_write(vec->data[i], port);
        }
        port_write_char(port, ')'); return;
    }
    if (vis_closure(v)) {
        Closure *c = as_clos(v);
        if (vis_symbol(c->name)) {
            int n = snprintf(buf, sizeof(buf), "#<procedure %s>", sym_cstr(c->name));
            port_write_string(port, buf, (uint32_t)n);
        } else {
            port_write_string(port, "#<procedure>", 12);
        }
        return;
    }
    if (vis_prim(v))   { int n=snprintf(buf,sizeof(buf),"#<primitive %s>",as_prim(v)->name); port_write_string(port,buf,(uint32_t)n); return; }
    if (vis_actor(v))  { int n=snprintf(buf,sizeof(buf),"#<actor %"PRIu64">",as_actor(v)->id); port_write_string(port,buf,(uint32_t)n); return; }
    if (vis_port(v))   { port_write_string(port, "#<port>", 7); return; }
    if (vis_error(v))  { port_write_string(port, "#<error>", 8); return; }
    if (vis_promise(v)){ port_write_string(port, "#<promise>", 10); return; }
    if (vis_symbolic(v)) { extern void sx_write(val_t, val_t); sx_write(v, port); return; }
    if (vis_quantum(v))  { extern void quantum_write(val_t, val_t); quantum_write(v, port); return; }
    if (vis_surreal(v))  { extern void sur_write(val_t, val_t); sur_write(v, port); return; }
    if (vis_mv(v))       { extern void mv_write(val_t, val_t); mv_write(v, port); return; }
    if (vis_matrix(v))  { extern void mat_write(val_t, val_t); mat_write(v, port); return; }
    if (vis_tensor(v))  { extern void tensor_write(val_t, val_t); tensor_write(v, port); return; }
    if (vis_f64vec(v)) {
        F64Vec *fv = as_f64v(v);
        port_write_string(port, "#f64(", 5);
        for (uint32_t i = 0; i < fv->len; i++) {
            if (i) port_write_char(port, ' ');
            int n = num_flonum_to_shortest_cstr(fv->data[i], buf, sizeof(buf));
            port_write_string(port, buf, (uint32_t)n);
        }
        port_write_char(port, ')');
        return;
    }
    if (vis_traced(v)) {
        Traced *t = as_traced(v);
        if (vis_symbol(t->name)) {
            int n = snprintf(buf, sizeof(buf), "#<traced-procedure %s>", as_sym(t->name)->data);
            port_write_string(port, buf, (uint32_t)n);
        } else {
            port_write_string(port, "#<traced-procedure>", 19);
        }
        return;
    }
    if (vis_tuple(v)) {
        Tuple *t = as_tuple(v);
        const char *tag = vis_up(v) ? "(up" : "(down";
        port_write_string(port, tag, (uint32_t)strlen(tag));
        for (uint32_t i = 0; i < t->len; i++) {
            port_write_char(port, ' ');
            scm_write(t->data[i], port);
        }
        port_write_char(port, ')');
        return;
    }
    /* fallback */
    int n = snprintf(buf, sizeof(buf), "#<object %u>", vtype(v));
    port_write_string(port, buf, (uint32_t)n);
}

/* ---- write-shared: write with datum labels for shared/cyclic structure ---- */

/*
 * Two-pass algorithm:
 *   Pass 1: walk the structure; for each heap compound (pair, vector, string,
 *           bytevector), count references.  Objects seen more than once get a
 *           label.
 *   Pass 2: write, emitting "#N=" before the first occurrence of a labeled
 *           object and "#N#" for subsequent references.
 *
 * We only label compound mutable objects (pairs, vectors, strings, bytevectors).
 * Symbols are unique by interning; immediates are value-equal, never shared.
 */

#define WSHARED_CAP_INIT 32

typedef struct {
    val_t  key;    /* heap pointer; 0 = empty */
    int    count;  /* reference count (pass 1) */
    int    label;  /* -1 = unlabeled, >= 0 = assigned label (pass 2) */
} WSharedEntry;

typedef struct {
    WSharedEntry *entries;
    int           cap;
    int           count;
    int           next_label;
} WSharedMap;

static WSharedMap *ws_new(void) {
    WSharedMap *m = (WSharedMap *)gc_alloc(sizeof(WSharedMap));
    m->cap = WSHARED_CAP_INIT;
    m->entries = (WSharedEntry *)gc_alloc(m->cap * sizeof(WSharedEntry));
    memset(m->entries, 0, (size_t)m->cap * sizeof(WSharedEntry));
    m->count = 0;
    m->next_label = 0;
    return m;
}

static WSharedEntry *ws_find(WSharedMap *m, val_t key) {
    int h = (int)((uint64_t)(uintptr_t)key % (unsigned)m->cap);
    for (int i = 0; i < m->cap; i++) {
        int idx = (h + i) % m->cap;
        if (m->entries[idx].key == 0) return NULL;
        if (m->entries[idx].key == key) return &m->entries[idx];
    }
    return NULL;
}

static WSharedEntry *ws_insert(WSharedMap *m, val_t key) {
    if (m->count * 2 >= m->cap) {
        int old_cap = m->cap;
        WSharedEntry *old = m->entries;
        m->cap *= 2;
        m->entries = (WSharedEntry *)gc_alloc(m->cap * sizeof(WSharedEntry));
        memset(m->entries, 0, (size_t)m->cap * sizeof(WSharedEntry));
        for (int i = 0; i < old_cap; i++) {
            if (old[i].key) {
                int h2 = (int)((uint64_t)(uintptr_t)old[i].key % (unsigned)m->cap);
                for (int j = 0; j < m->cap; j++) {
                    int idx = (h2 + j) % m->cap;
                    if (!m->entries[idx].key) { m->entries[idx] = old[i]; break; }
                }
            }
        }
    }
    int h = (int)((uint64_t)(uintptr_t)key % (unsigned)m->cap);
    for (int i = 0; i < m->cap; i++) {
        int idx = (h + i) % m->cap;
        if (!m->entries[idx].key) {
            m->entries[idx].key   = key;
            m->entries[idx].count = 0;
            m->entries[idx].label = -1;
            m->count++;
            return &m->entries[idx];
        }
        if (m->entries[idx].key == key) return &m->entries[idx];
    }
    return NULL;
}

static bool ws_is_compound(val_t v) {
    return vis_pair(v) || vis_vector(v) || vis_string(v) || vis_bytes(v);
}

static void ws_count_refs(val_t v, WSharedMap *m) {
    if (!ws_is_compound(v)) return;
    WSharedEntry *e = ws_find(m, v);
    if (e) { e->count++; return; }
    e = ws_insert(m, v);
    e->count = 1;
    if (vis_pair(v)) {
        ws_count_refs(vcar(v), m);
        ws_count_refs(vcdr(v), m);
    } else if (vis_vector(v)) {
        Vector *vec = as_vec(v);
        for (uint32_t i = 0; i < vec->len; i++) ws_count_refs(vec->data[i], m);
    }
    /* strings and bytevectors have no sub-values */
}

static void ws_write(val_t v, val_t port, WSharedMap *m);

static void ws_write_list(val_t v, val_t port, WSharedMap *m) {
    port_write_char(port, '(');
    ws_write(vcar(v), port, m);
    val_t rest = vcdr(v);
    while (vis_pair(rest)) {
        WSharedEntry *e2 = ws_find(m, rest);
        if (e2 && e2->count > 1) {
            port_write_string(port, " . ", 3);
            ws_write(rest, port, m);
            port_write_char(port, ')');
            return;
        }
        port_write_char(port, ' ');
        ws_write(vcar(rest), port, m);
        rest = vcdr(rest);
    }
    if (!vis_nil(rest)) { port_write_string(port, " . ", 3); ws_write(rest, port, m); }
    port_write_char(port, ')');
}

static void ws_write(val_t v, val_t port, WSharedMap *m) {
    if (!ws_is_compound(v)) { scm_write(v, port); return; }
    WSharedEntry *e = ws_find(m, v);
    if (e && e->count > 1) {
        if (e->label >= 0) {
            char buf[32]; int n = snprintf(buf, sizeof(buf), "#%d#", e->label);
            port_write_string(port, buf, (uint32_t)n);
            return;
        }
        e->label = m->next_label++;
        char buf[32]; int n = snprintf(buf, sizeof(buf), "#%d=", e->label);
        port_write_string(port, buf, (uint32_t)n);
    }
    if (vis_pair(v))   { ws_write_list(v, port, m); return; }
    if (vis_vector(v)) {
        Vector *vec = as_vec(v);
        port_write_string(port, "#(", 2);
        for (uint32_t i = 0; i < vec->len; i++) {
            if (i) port_write_char(port, ' ');
            ws_write(vec->data[i], port, m);
        }
        port_write_char(port, ')');
        return;
    }
    scm_write(v, port);
}

void scm_write_shared(val_t v, val_t port) {
    WSharedMap *m = ws_new();
    ws_count_refs(v, m);
    ws_write(v, port, m);
}

void scm_display(val_t v, val_t port) {
    /* Like write but strings/chars printed without quotes/escapes */
    if (vis_string(v)) {
        port_write_string(port, str_data(as_str(v)), as_str(v)->len);
        return;
    }
    if (vis_char(v)) {
        port_write_char(port, (int)vunchr(v));
        return;
    }
    scm_write(v, port);
}

void scm_newline(val_t port) {
    port_write_char(port, '\n');
}
