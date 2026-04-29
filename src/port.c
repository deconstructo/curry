#include "port.h"
#include "object.h"
#include "gc.h"
#include "numeric.h"
#include "symbol.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>

val_t PORT_STDIN, PORT_STDOUT, PORT_STDERR;

static val_t make_port(int flags) {
    Port *p = CURRY_NEW(Port);
    p->hdr.type  = T_PORT;
    p->hdr.flags = 0;
    p->flags = (uint8_t)flags;
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

val_t port_open_file(const char *path, int flags) {
    const char *mode;
    if ((flags & PORT_BINARY)) {
        mode = (flags & PORT_OUTPUT) ? "wb" : "rb";
    } else {
        mode = (flags & PORT_OUTPUT) ? "w" : "r";
    }
    FILE *fp = fopen(path, mode);
    if (!fp) return V_FALSE;
    val_t v = make_port(flags);
    as_port(v)->u.fp = fp;
    return v;
}

val_t port_open_input_string(const char *str, uint32_t len) {
    val_t v = make_port(PORT_INPUT | PORT_STRING);
    Port *p = as_port(v);
    p->u.str.buf = (char *)gc_alloc_atomic(len + 1);
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
    p->u.str.buf = (char *)gc_alloc_atomic(64);
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
    return read_utf8_codepoint(p);
}

int port_peek_char(val_t v) {
    Port *p = as_port(v);
    if (p->flags & PORT_CLOSED) return -1;
    if (p->flags & PORT_STRING) {
        if (p->u.str.pos >= p->u.str.len) return -1;
        size_t saved = p->u.str.pos;
        int c = read_utf8_codepoint(p);
        p->u.str.pos = saved;
        return c;
    }
    /* For file ports, use ungetc trick */
    int c = fgetc(p->u.fp);
    if (c != EOF) ungetc(c, p->u.fp);
    return c;  /* ASCII approximation - full peek requires buffering */
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
            char *nb = (char *)gc_alloc_atomic(p->u.str.cap);
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
            char *nb = (char *)gc_alloc_atomic(p->u.str.cap);
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
        port_write_char(v, b);
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

val_t port_get_output_string(val_t v) {
    Port *p = as_port(v);
    assert(p->flags & PORT_STRING);
    uint32_t len = (uint32_t)p->u.str.len;
    String *s = (String *)gc_alloc_atomic(sizeof(String) + len + 1);
    s->hdr.type  = T_STRING;
    s->hdr.flags = 0;
    s->len  = len;
    s->hash = 0;
    memcpy(s->data, p->u.str.buf, len + 1);
    return vptr(s);
}

/* ---- Display / Write ---- */

static void write_string_escaped(val_t port, const char *s, uint32_t len) {
    port_write_char(port, '"');
    for (uint32_t i = 0; i < len; i++) {
        char c = s[i];
        if      (c == '"')  { port_write_string(port, "\\\"", 2); }
        else if (c == '\\') { port_write_string(port, "\\\\", 2); }
        else if (c == '\n') { port_write_string(port, "\\n", 2);  }
        else if (c == '\r') { port_write_string(port, "\\r", 2);  }
        else if (c == '\t') { port_write_string(port, "\\t", 2);  }
        else                { port_write_char(port, (unsigned char)c); }
    }
    port_write_char(port, '"');
}

void scm_write(val_t v, val_t port) {
    char buf[64];
    if (vis_nil(v))      { port_write_string(port, "()", 2); return; }
    if (vis_void(v))     { return; /* unspecified - write nothing */ }
    if (vis_eof(v))      { port_write_string(port, "#<eof>", 6); return; }
    if (v == V_FALSE)    { port_write_string(port, "#f", 2); return; }
    if (v == V_TRUE)     { port_write_string(port, "#t", 2); return; }
    if (vis_fixnum(v))   { int n=snprintf(buf,sizeof(buf),"%ld",(long)vunfix(v)); port_write_string(port,buf,(uint32_t)n); return; }
    if (vis_char(v))     {
        uint32_t cp = vunchr(v);
        port_write_string(port, "#\\", 2);
        if      (cp == ' ')  { port_write_string(port, "space", 5);   }
        else if (cp == '\n') { port_write_string(port, "newline", 7); }
        else if (cp == '\t') { port_write_string(port, "tab", 3);     }
        else                 { port_write_char(port, (int)cp);          }
        return;
    }
    if (vis_string(v))   { write_string_escaped(port, as_str(v)->data, as_str(v)->len); return; }
    if (vis_symbol(v))   { port_write_string(port, as_sym(v)->data, as_sym(v)->len); return; }
    if (vis_flonum(v))   { int n=snprintf(buf,sizeof(buf),"%g",vfloat(v)); port_write_string(port,buf,(uint32_t)n); return; }
    if (vis_bignum(v))   { char *s=mpz_get_str(NULL,10,as_big(v)->z); port_write_string(port,s,(uint32_t)strlen(s)); free(s); return; }
    if (vis_rational(v)) {
        char *s = mpq_get_str(NULL, 10, as_rat(v)->q);
        port_write_string(port, s, (uint32_t)strlen(s)); free(s); return;
    }
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
        int n = snprintf(buf, sizeof(buf), "%g+%gi+%gj+%gk", q->a, q->b, q->c, q->d);
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
    if (vis_actor(v))  { int n=snprintf(buf,sizeof(buf),"#<actor %lu>",as_actor(v)->id); port_write_string(port,buf,(uint32_t)n); return; }
    if (vis_port(v))   { port_write_string(port, "#<port>", 7); return; }
    if (vis_error(v))  { port_write_string(port, "#<error>", 8); return; }
    if (vis_promise(v)){ port_write_string(port, "#<promise>", 10); return; }
    if (vis_symbolic(v)) { extern void sx_write(val_t, val_t); sx_write(v, port); return; }
    if (vis_quantum(v))  { extern void quantum_write(val_t, val_t); quantum_write(v, port); return; }
    if (vis_surreal(v))  { extern void sur_write(val_t, val_t); sur_write(v, port); return; }
    if (vis_mv(v))       { extern void mv_write(val_t, val_t); mv_write(v, port); return; }
    /* fallback */
    int n = snprintf(buf, sizeof(buf), "#<object %u>", vtype(v));
    port_write_string(port, buf, (uint32_t)n);
}

void scm_display(val_t v, val_t port) {
    /* Like write but strings/chars printed without quotes/escapes */
    if (vis_string(v)) {
        port_write_string(port, as_str(v)->data, as_str(v)->len);
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
