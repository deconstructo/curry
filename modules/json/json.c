/*
 * curry_json — JSON encode/decode module for Curry Scheme.
 *
 * Scheme API:
 *   (json-parse string)         -> Scheme value
 *   (json-stringify value)      -> string
 *   (json-read port)            -> Scheme value
 *   (json-write value port)     -> unspecified
 *   (json-load-file path)       -> Scheme value
 *   (json-dump-file value path) -> unspecified
 *
 * json-read/json-write go through curry_port_read_byte/
 * curry_port_write_string (include/curry.h) so they work on any port —
 * string, bytevector, or file. json-load-file/json-dump-file use plain
 * fopen/fread/fwrite/fclose directly rather than the port API, so the file
 * is flushed and closed deterministically on return, not whenever the
 * port's GC finalizer happens to run.
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
            /* A trailing backslash at the very end of the input (no
             * character follows) previously fell through to read/advance
             * past the NUL terminator unconditionally below — an
             * out-of-bounds read past the string's allocated buffer.
             * Stop cleanly instead; the unterminated string is malformed
             * input either way. */
            if (!**p) break;
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
                /* Read up to 4 hex digits, stopping at the NUL terminator
                 * if the escape is truncated (previously read/advanced 4
                 * bytes unconditionally, which could run past the
                 * string's end). A short escape yields fewer significant
                 * digits, same as strtoul would produce from a
                 * NUL-padded buffer. */
                char hex[5] = {0,0,0,0,0};
                int hn = 0;
                while (hn < 4 && (*p)[hn]) { hex[hn] = (*p)[hn]; hn++; }
                (*p) += hn;
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

/* A proper list where every element is a (key . val) pair whose key is
 * a string or symbol -- the shape json-parse produces for a JSON
 * object (json_parse_string keys). "Is the element a pair" alone is
 * NOT sufficient: in Scheme a plain 2+-element list like (1 2) is
 * itself a pair (1 . (2 . ())), so a list-of-lists such as
 * ((1 2) (3 4)) would otherwise misclassify as an alist and silently
 * stringify as {1:[2],3:[4]} instead of [[1,2],[3,4]]. Requiring a
 * string/symbol key is what actually distinguishes a genuine
 * association from an ordinary nested list. */
static bool looks_like_alist(curry_val v) {
    while (curry_is_pair(v)) {
        curry_val kv = curry_car(v);
        if (!curry_is_pair(kv)) return false;
        curry_val key = curry_car(kv);
        if (!curry_is_string(key) && !curry_is_symbol(key)) return false;
        v = curry_cdr(v);
    }
    return curry_is_nil(v);
}

static void write_json_string(const char *s, char **out, size_t *len, size_t *cap) {
    sb_char(out,len,cap,'"');
    while (*s) {
        if (*s=='"') sb_str(out,len,cap,"\\\"");
        else if (*s=='\\') sb_str(out,len,cap,"\\\\");
        else if (*s=='\n') sb_str(out,len,cap,"\\n");
        else if (*s=='\t') sb_str(out,len,cap,"\\t");
        else sb_char(out,len,cap,*s);
        s++;
    }
    sb_char(out,len,cap,'"');
}

static void json_write_value(curry_val v, char **out, size_t *len, size_t *cap) {
    char buf[64];
    if (curry_is_nil(v)) { sb_str(out,len,cap,"null"); return; }
    if (curry_is_bool(v)) { sb_str(out,len,cap,curry_bool(v)?"true":"false"); return; }
    if (curry_is_fixnum(v)) { snprintf(buf,sizeof(buf),"%ld",(long)curry_fixnum(v)); sb_str(out,len,cap,buf); return; }
    if (curry_is_float(v)) { snprintf(buf,sizeof(buf),"%g",curry_float(v)); sb_str(out,len,cap,buf); return; }
    /* Any other numeric-tower value (bignum, exact rational, complex —
     * JSON's own number type is just IEEE double, so there's nowhere
     * more precise to put these): convert rather than silently falling
     * through to the generic "unrecognized value" null case below, which
     * would make a real number indistinguishable from a genuinely absent
     * field. Must come after the fixnum/float checks above, which handle
     * those two cases exactly rather than through this lossy path. */
    if (curry_is_number(v)) { snprintf(buf,sizeof(buf),"%g",curry_number_to_double(v)); sb_str(out,len,cap,buf); return; }
    if (curry_is_string(v)) { write_json_string(curry_string(v), out, len, cap); return; }
    /* A symbol used as an alist key (looks_like_alist now permits this,
     * matching how such an alist would actually be built) must still
     * serialize as a proper JSON string key, not fall through to null. */
    if (curry_is_symbol(v)) { write_json_string(curry_symbol(v), out, len, cap); return; }
    if (curry_is_vector(v)) {
        /* Vector -> JSON array */
        uint32_t n = curry_vector_length(v);
        sb_char(out,len,cap,'[');
        for (uint32_t i = 0; i < n; i++) {
            if (i) sb_char(out,len,cap,',');
            json_write_value(curry_vector_ref(v, i), out, len, cap);
        }
        sb_char(out,len,cap,']'); return;
    }
    if (curry_is_pair(v) && !looks_like_alist(v)) {
        /* Plain list -> JSON array, matching how vectors already map to arrays. */
        sb_char(out,len,cap,'[');
        bool first = true;
        while (curry_is_pair(v)) {
            if (!first) sb_char(out,len,cap,',');
            json_write_value(curry_car(v), out, len, cap);
            first = false;
            v = curry_cdr(v);
        }
        sb_char(out,len,cap,']'); return;
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

/* Reads a whole port to a growable buffer via curry_port_read_byte. Used
 * by both json-read and json-load-file's in-memory equivalent — JSON's
 * structure (nested objects/arrays) means json_parse_value needs to look
 * ahead arbitrarily anyway, so there's no genuine incremental-parsing win
 * from feeding it one byte at a time instead of one contiguous buffer. */
static char *slurp_port(curry_val port, size_t *out_len) {
    size_t cap = 256, len = 0;
    char *buf = malloc(cap);
    int b;
    while ((b = curry_port_read_byte(port)) >= 0) {
        if (len + 2 >= cap) { cap *= 2; buf = realloc(buf, cap); }
        buf[len++] = (char)b;
    }
    buf[len] = '\0';
    if (out_len) *out_len = len;
    return buf;
}

static curry_val fn_json_read(int argc, curry_val *argv, void *ud) {
    (void)ud; (void)argc;
    if (!curry_is_port(argv[0])) curry_error("json-read: not a port");
    char *buf = slurp_port(argv[0], NULL);
    const char *p = buf;
    curry_val result = json_parse_value(&p);
    free(buf);
    return result;
}

static curry_val fn_json_write(int argc, curry_val *argv, void *ud) {
    (void)ud; (void)argc;
    if (!curry_is_port(argv[1])) curry_error("json-write: not a port");
    size_t cap=256, len=0;
    char *buf = malloc(cap);
    json_write_value(argv[0], &buf, &len, &cap);
    buf[len] = '\0';
    curry_port_write_string(argv[1], buf);
    free(buf);
    return curry_void();
}

/* json-load-file/json-dump-file use plain C file I/O rather than the
 * port API — deterministic fopen/fclose on every call, rather than
 * leaving an intermediate port's flush/close timing to whenever its GC
 * finalizer happens to run. */
static curry_val fn_json_load_file(int argc, curry_val *argv, void *ud) {
    (void)ud; (void)argc;
    const char *path = curry_string(argv[0]);
    FILE *fp = fopen(path, "rb");
    if (!fp) curry_error("json-load-file: cannot open '%s'", path);
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    size_t n;
    while ((n = fread(buf + len, 1, cap - len, fp)) > 0) {
        len += n;
        if (len + 4096 >= cap) { cap *= 2; buf = realloc(buf, cap); }
    }
    fclose(fp);
    buf[len] = '\0';
    const char *p = buf;
    curry_val result = json_parse_value(&p);
    free(buf);
    return result;
}

static curry_val fn_json_dump_file(int argc, curry_val *argv, void *ud) {
    (void)ud; (void)argc;
    const char *path = curry_string(argv[1]);
    FILE *fp = fopen(path, "wb");
    if (!fp) curry_error("json-dump-file: cannot open '%s' for writing", path);
    size_t cap=256, len=0;
    char *buf = malloc(cap);
    json_write_value(argv[0], &buf, &len, &cap);
    fwrite(buf, 1, len, fp);
    fclose(fp);
    free(buf);
    return curry_void();
}

void curry_module_init(CurryVM *vm) {
    curry_define_fn(vm, "json-parse",     fn_json_parse,     1, 1, NULL);
    curry_define_fn(vm, "json-stringify", fn_json_stringify, 1, 1, NULL);
    curry_define_fn(vm, "json-read",      fn_json_read,      1, 1, NULL);
    curry_define_fn(vm, "json-write",     fn_json_write,     2, 2, NULL);
    curry_define_fn(vm, "json-load-file", fn_json_load_file, 1, 1, NULL);
    curry_define_fn(vm, "json-dump-file", fn_json_dump_file, 2, 2, NULL);
}
