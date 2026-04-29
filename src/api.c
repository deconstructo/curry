/*
 * api.c — public C embedding API (curry_* functions declared in include/curry.h)
 *
 * These are thin wrappers around internal Curry types and functions.
 * They live in curry_core so that dynamically-loaded modules resolve them
 * from the main binary (via --export-dynamic), sharing one symbol table.
 */

#include "curry.h"
#include "value.h"
#include "object.h"
#include "symbol.h"
#include "numeric.h"
#include "eval.h"
#include "builtins.h"
#include "gc.h"
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <alloca.h>

/* ---- Value constructors ---- */

curry_val curry_make_fixnum(intptr_t n)  { return vfix(n); }
curry_val curry_make_float(double d)     { return num_make_float(d); }
curry_val curry_make_bool(bool b)        { return b ? V_TRUE : V_FALSE; }
curry_val curry_make_char(uint32_t cp)   { return vchr(cp); }
curry_val curry_nil(void)               { return V_NIL; }
curry_val curry_void(void)              { return V_VOID; }
curry_val curry_eof(void)               { return V_EOF; }

curry_val curry_make_string(const char *s) {
    size_t len = strlen(s);
    String *str = (String *)gc_alloc_atomic(sizeof(String) + len + 1);
    str->hdr.type = T_STRING; str->hdr.flags = 0;
    str->len = (uint32_t)len; str->hash = 0;
    memcpy(str->data, s, len + 1);
    return vptr(str);
}

curry_val curry_make_symbol(const char *s) {
    return sym_intern_cstr(s);
}

curry_val curry_make_pair(curry_val car, curry_val cdr) {
    return scm_cons(car, cdr);
}

/* ---- Value predicates ---- */

bool curry_is_fixnum(curry_val v)    { return vis_fixnum(v); }
bool curry_is_float(curry_val v)     { return vis_flonum(v); }
bool curry_is_bool(curry_val v)      { return vis_bool(v); }
bool curry_is_char(curry_val v)      { return vis_char(v); }
bool curry_is_string(curry_val v)    { return vis_string(v); }
bool curry_is_symbol(curry_val v)    { return vis_symbol(v); }
bool curry_is_pair(curry_val v)      { return vis_pair(v); }
bool curry_is_nil(curry_val v)       { return vis_nil(v); }
bool curry_is_void(curry_val v)      { return vis_void(v); }
bool curry_is_eof(curry_val v)       { return vis_eof(v); }
bool curry_is_procedure(curry_val v) { return vis_proc(v); }
bool curry_is_true(curry_val v)      { return vis_true(v); }

/* ---- Value accessors ---- */

intptr_t    curry_fixnum(curry_val v)  { return vunfix(v); }
double      curry_float(curry_val v)   { return vis_fixnum(v) ? (double)vunfix(v) : vfloat(v); }
bool        curry_bool(curry_val v)    { return v != V_FALSE; }
uint32_t    curry_char(curry_val v)    { return vunchr(v); }
const char *curry_string(curry_val v)  { return as_str(v)->data; }
const char *curry_symbol(curry_val v)  { return sym_cstr(v); }
curry_val   curry_car(curry_val v)     { return vcar(v); }
curry_val   curry_cdr(curry_val v)     { return vcdr(v); }

/* ---- Calling Scheme from C ---- */

curry_val curry_apply(curry_val proc, int argc, curry_val *argv) {
    val_t args = V_NIL;
    for (int i = argc - 1; i >= 0; i--)
        args = scm_cons(argv[i], args);
    return apply(proc, args);
}

/* ---- Error handling ---- */

void curry_error(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    scm_raise(V_FALSE, "%s", buf);
}

/* ---- Vector ---- */

curry_val curry_make_vector(uint32_t len, curry_val fill) {
    Vector *v = CURRY_NEW_FLEX(Vector, len);
    v->hdr.type = T_VECTOR; v->hdr.flags = 0; v->len = len;
    for (uint32_t i = 0; i < len; i++) v->data[i] = fill;
    return vptr(v);
}

uint32_t  curry_vector_length(curry_val v)              { return as_vec(v)->len; }
curry_val curry_vector_ref(curry_val v, uint32_t i)     { return as_vec(v)->data[i]; }
void      curry_vector_set(curry_val v, uint32_t i, curry_val val) {
    as_vec(v)->data[i] = val;
}

/* ---- Bytevector ---- */

curry_val curry_make_bytevector(uint32_t len, uint8_t fill) {
    Bytevector *b = CURRY_NEW_FLEX_ATOM(Bytevector, len);
    b->hdr.type = T_BYTEVECTOR; b->hdr.flags = 0; b->len = len;
    memset(b->data, fill, len);
    return vptr(b);
}

uint32_t curry_bytevector_length(curry_val v)                  { return as_bytes(v)->len; }
uint8_t  curry_bytevector_ref(curry_val v, uint32_t i)         { return as_bytes(v)->data[i]; }
void     curry_bytevector_set(curry_val v, uint32_t i, uint8_t b) {
    as_bytes(v)->data[i] = b;
}

/* ---- Numeric tower ---- */

curry_val curry_make_complex(curry_val real, curry_val imag) {
    return num_make_complex(real, imag);
}

curry_val curry_make_quaternion(double a, double b, double c, double d) {
    return num_make_quat(a, b, c, d);
}

curry_val curry_make_octonion(const double e[8]) {
    return num_make_oct(e);
}

/* ---- List helpers ---- */

curry_val curry_list(int n, ...) {
    va_list ap;
    va_start(ap, n);
    curry_val *tmp = (curry_val *)alloca((size_t)n * sizeof(curry_val));
    for (int i = 0; i < n; i++) tmp[i] = va_arg(ap, curry_val);
    va_end(ap);
    curry_val r = V_NIL;
    for (int i = n - 1; i >= 0; i--) r = scm_cons(tmp[i], r);
    return r;
}
