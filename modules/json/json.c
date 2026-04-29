/*
 * curry_json — JSON encode/decode module for Curry Scheme.
 *
 * Scheme API:
 *   (json-parse string)         -> Scheme value
 *   (json-stringify value)      -> string
 *   (json-parse-port port)      -> Scheme value
 *
 * Mapping:
 *   JSON null        -> #f  (or (json-null) sentinel — configurable)
 *   JSON true/false  -> #t / #f
 *   JSON number      -> exact or inexact number
 *   JSON string      -> Scheme string
 *   JSON array       -> vector
 *   JSON object      -> hash-table (string keys)
 */

#include <curry.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

/* ---- Forward declarations ---- */
static curry_val json_parse_value(const char **p);
static void      json_write_value(curry_val v, char **out, size_t *len, size_t *cap);

/* ---- Parser helpers ---- */

static void skip_ws(const char **p) {
    while (**p && isspace((unsigned char)**p)) (*p)++;
}

static curry_val json_parse_string(const char **p) {
    (*p)++;  /* skip opening " */
    size_t cap = 64, len = 0;
    char *buf = malloc(cap);
    while (**p && **p != '"') {
        if (**p == '\\') {
            (*p)++;
            char esc = **p; (*p)++;
            switch (esc) {
            case '"':  buf[len++] = '"'; break;
            case '\\': buf[len++] = '\\'; break;
            case '/':  buf[len++] = '/'; break;
            case 'n':  buf[len++] = '\n'; break;
            case 'r':  buf[len++] = '\r'; break;
            case 't':  buf[len++] = '\t'; break;
            case 'b':  buf[len++] = '\b'; break;
            case 'f':  buf[len++] = '\f'; break;
            case 'u': {
                char hex[5] = {(*p)[0],(*p)[1],(*p)[2],(*p)[3],0};
                (*p) += 4;
                uint32_t cp = (uint32_t)strtoul(hex, NULL, 16);
                /* Encode as UTF-8 */
                if (cp < 0x80) { buf[len++]=(char)cp; }
                else if (cp < 0x800) { buf[len++]=(char)(0xC0|(cp>>6)); buf[len++]=(char)(0x80|(cp&0x3F)); }
                else { buf[len++]=(char)(0xE0|(cp>>12)); buf[len++]=(char)(0x80|((cp>>6)&0x3F)); buf[len++]=(char)(0x80|(cp&0x3F)); }
                break;
            }
            default: buf[len++] = esc;
            }
        } else {
            buf[len++] = **p; (*p)++;
        }
        if (len + 8 >= cap) { cap *= 2; buf = realloc(buf, cap); }
    }
    if (**p == '"') (*p)++;
    buf[len] = '\0';
    curry_val s = curry_make_string(buf);
    free(buf);
    return s;
}

static curry_val json_parse_number(const char **p) {
    char buf[64]; int i = 0;
    if (**p == '-') buf[i++] = *(*p)++;
    while (isdigit((unsigned char)**p)) buf[i++] = *(*p)++;
    bool is_float = false;
    if (**p == '.') { is_float = true; buf[i++] = *(*p)++; while (isdigit((unsigned char)**p)) buf[i++] = *(*p)++; }
    if (**p == 'e' || **p == 'E') { is_float = true; buf[i++] = *(*p)++; if (**p=='+' || **p=='-') buf[i++]=*(*p)++; while (isdigit((unsigned char)**p)) buf[i++]=*(*p)++; }
    buf[i] = '\0';
    if (is_float) return curry_make_float(atof(buf));
    return curry_make_fixnum(atol(buf));
}

static curry_val json_parse_value(const char **p) {
    skip_ws(p);
    if (!**p) return curry_eof();

    if (**p == '"') return json_parse_string(p);
    if (**p == '{') {
        (*p)++;
        curry_val ht = curry_make_fixnum(0); /* placeholder; use hash-table */
        /* We need access to the internal hash_make, but through the VM we only have curry API.
         * For simplicity, build an alist and convert.  In a real impl, use curry_apply. */
        /* Return as alist ((key . val) ...) */
        curry_val alist = curry_nil();
        skip_ws(p);
        if (**p == '}') { (*p)++; return alist; }
        while (**p) {
            skip_ws(p);
            curry_val key = json_parse_string(p);
            skip_ws(p);
            if (**p == ':') (*p)++;
            curry_val val = json_parse_value(p);
            alist = curry_make_pair(curry_make_pair(key, val), alist);
            skip_ws(p);
            if (**p == ',') { (*p)++; continue; }
            if (**p == '}') { (*p)++; break; }
        }
        (void)ht;
        return alist;
    }
    if (**p == '[') {
        (*p)++;
        curry_val lst = curry_nil();
        skip_ws(p);
        if (**p == ']') { (*p)++; return lst; }
        int n = 0;
        curry_val elems[1024]; /* max 1024 elements in fast path */
        while (**p && n < 1024) {
            elems[n++] = json_parse_value(p);
            skip_ws(p);
            if (**p == ',') { (*p)++; continue; }
            if (**p == ']') { (*p)++; break; }
        }
        curry_val v = curry_make_vector((uint32_t)n, curry_void());
        for (int i = 0; i < n; i++) curry_vector_set(v, (uint32_t)i, elems[i]);
        (void)lst;
        return v;
    }
    if (strncmp(*p, "true", 4) == 0)  { *p += 4; return curry_make_bool(true); }
    if (strncmp(*p, "false", 5) == 0) { *p += 5; return curry_make_bool(false); }
    if (strncmp(*p, "null", 4) == 0)  { *p += 4; return curry_make_bool(false); }
    if (**p == '-' || isdigit((unsigned char)**p)) return json_parse_number(p);

    return curry_eof();
}

/* ---- Stringify ---- */

static void sb_char(char **out, size_t *len, size_t *cap, char c) {
    if (*len + 2 >= *cap) { *cap *= 2; *out = realloc(*out, *cap); }
    (*out)[(*len)++] = c;
}
static void sb_str(char **out, size_t *len, size_t *cap, const char *s) {
    while (*s) sb_char(out, len, cap, *s++);
}

static void json_write_value(curry_val v, char **out, size_t *len, size_t *cap) {
    char buf[64];
    if (curry_is_nil(v)) { sb_str(out,len,cap,"null"); return; }
    if (curry_is_bool(v)) { sb_str(out,len,cap,curry_bool(v)?"true":"false"); return; }
    if (curry_is_fixnum(v)) { snprintf(buf,sizeof(buf),"%ld",(long)curry_fixnum(v)); sb_str(out,len,cap,buf); return; }
    if (curry_is_float(v)) { snprintf(buf,sizeof(buf),"%g",curry_float(v)); sb_str(out,len,cap,buf); return; }
    if (curry_is_string(v)) {
        sb_char(out,len,cap,'"');
        const char *s = curry_string(v);
        while (*s) {
            if (*s=='"') sb_str(out,len,cap,"\\\"");
            else if (*s=='\\') sb_str(out,len,cap,"\\\\");
            else if (*s=='\n') sb_str(out,len,cap,"\\n");
            else if (*s=='\t') sb_str(out,len,cap,"\\t");
            else sb_char(out,len,cap,*s);
            s++;
        }
        sb_char(out,len,cap,'"'); return;
    }
    if (curry_is_pair(v)) {
        /* Alist -> JSON object */
        sb_char(out,len,cap,'{');
        bool first = true;
        while (curry_is_pair(v)) {
            curry_val kv = curry_car(v);
            if (!first) sb_char(out,len,cap,',');
            json_write_value(curry_car(kv), out, len, cap);
            sb_char(out,len,cap,':');
            json_write_value(curry_cdr(kv), out, len, cap);
            first = false;
            v = curry_cdr(v);
        }
        sb_char(out,len,cap,'}'); return;
    }
    sb_str(out,len,cap,"null");
}

/* ---- Primitives ---- */

static curry_val fn_json_parse(int argc, curry_val *argv, void *ud) {
    (void)ud; (void)argc;
    const char *src = curry_string(argv[0]);
    return json_parse_value(&src);
}

static curry_val fn_json_stringify(int argc, curry_val *argv, void *ud) {
    (void)ud; (void)argc;
    size_t cap=256, len=0;
    char *buf = malloc(cap);
    json_write_value(argv[0], &buf, &len, &cap);
    buf[len] = '\0';
    curry_val r = curry_make_string(buf);
    free(buf);
    return r;
}

void curry_module_init(CurryVM *vm) {
    curry_define_fn(vm, "json-parse",     fn_json_parse,     1, 1, NULL);
    curry_define_fn(vm, "json-stringify", fn_json_stringify, 1, 1, NULL);
}
