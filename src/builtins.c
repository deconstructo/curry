#include "builtins.h"
#include "object.h"
#include "eval.h"
#include "symbolic.h"
#include "quantum.h"
#include "surreal.h"
#include "env.h"
#include "symbol.h"
#include "numeric.h"
#include "port.h"
#include "reader.h"
#include "set.h"
#include "actors.h"
#include "gc.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <math.h>
#include <ctype.h>

/* ---- Registration helper ---- */

static void defprim(val_t env, const char *name, PrimFn fn, int min, int max) {
    Primitive *p = CURRY_NEW(Primitive);
    p->hdr.type  = T_PRIMITIVE; p->hdr.flags = 0;
    p->name      = name;
    p->min_args  = min; p->max_args = max;
    p->fn        = fn;  p->ud = NULL;
    env_define(env, sym_intern_cstr(name), vptr(p));
}
#define DEF(name, fn, min, max) defprim(env, name, fn, min, max)

/* ---- List helpers ---- */

val_t scm_cons(val_t car, val_t cdr) {
    Pair *p = CURRY_NEW(Pair);
    p->hdr.type=T_PAIR; p->hdr.flags=0; p->car=car; p->cdr=cdr;
    return vptr(p);
}

int scm_list_length(val_t lst) {
    int n = 0;
    while (vis_pair(lst)) { n++; lst = vcdr(lst); }
    return vis_nil(lst) ? n : -1;
}

val_t scm_list_ref(val_t lst, int n) {
    for (int i = 0; i < n; i++) lst = vcdr(lst);
    return vcar(lst);
}

val_t scm_list_tail(val_t lst, int n) {
    for (int i = 0; i < n; i++) lst = vcdr(lst);
    return lst;
}

val_t scm_append(val_t a, val_t b) {
    if (vis_nil(a)) return b;
    return scm_cons(vcar(a), scm_append(vcdr(a), b));
}

val_t scm_reverse(val_t lst) {
    val_t r = V_NIL;
    while (vis_pair(lst)) { r = scm_cons(vcar(lst), r); lst = vcdr(lst); }
    return r;
}

/* ---- String helpers ---- */

val_t scm_make_string(uint32_t len, int fill) {
    int bytes_per = fill < 0x80 ? 1 : fill < 0x800 ? 2 : fill < 0x10000 ? 3 : 4;
    uint32_t total = len * (uint32_t)bytes_per;
    String *s = (String *)gc_alloc_atomic(sizeof(String) + total + 1);
    s->hdr.type=T_STRING; s->hdr.flags=0; s->len=total; s->hash=0;
    /* Fill with UTF-8 encoded fill_char */
    char enc[5]; int elen;
    uint32_t u = (uint32_t)fill;
    if      (u < 0x80)    { enc[0]=(char)u; elen=1; }
    else if (u < 0x800)   { enc[0]=(char)(0xC0|(u>>6)); enc[1]=(char)(0x80|(u&0x3F)); elen=2; }
    else if (u < 0x10000) { enc[0]=(char)(0xE0|(u>>12)); enc[1]=(char)(0x80|((u>>6)&0x3F)); enc[2]=(char)(0x80|(u&0x3F)); elen=3; }
    else { enc[0]=(char)(0xF0|(u>>18)); enc[1]=(char)(0x80|((u>>12)&0x3F)); enc[2]=(char)(0x80|((u>>6)&0x3F)); enc[3]=(char)(0x80|(u&0x3F)); elen=4; }
    for (uint32_t i = 0; i < len; i++) memcpy(s->data + i*(uint32_t)elen, enc, (size_t)elen);
    s->data[total] = '\0';
    return vptr(s);
}

val_t scm_string_copy(val_t sv) {
    String *s = as_str(sv);
    String *c = (String *)gc_alloc_atomic(sizeof(String) + s->len + 1);
    *c = *s; memcpy(c->data, s->data, s->len + 1);
    return vptr(c);
}

val_t scm_string_append(val_t a, val_t b) {
    String *sa = as_str(a), *sb = as_str(b);
    uint32_t len = sa->len + sb->len;
    String *r = (String *)gc_alloc_atomic(sizeof(String) + len + 1);
    r->hdr.type=T_STRING; r->hdr.flags=0; r->len=len; r->hash=0;
    memcpy(r->data, sa->data, sa->len);
    memcpy(r->data + sa->len, sb->data, sb->len);
    r->data[len] = '\0';
    return vptr(r);
}

val_t scm_string_to_symbol(val_t sv) {
    String *s = as_str(sv);
    return sym_intern(s->data, s->len);
}

val_t scm_symbol_to_string(val_t sym) {
    const char *name = sym_cstr(sym);
    uint32_t len = sym_len(sym);
    String *s = (String *)gc_alloc_atomic(sizeof(String) + len + 1);
    s->hdr.type=T_STRING; s->hdr.flags=0; s->len=len; s->hash=0;
    memcpy(s->data, name, len + 1);
    return vptr(s);
}

/* ---- Type predicates ---- */
#define PRED1(name, test) static val_t prim_##name(int ac, val_t *av, void *ud) { (void)ud; return vbool(test(av[0])); }

PRED1(pair_p,     vis_pair)
PRED1(null_p,     vis_nil)
static val_t prim_list_p(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    val_t v = av[0];
    while (vis_pair(v)) v = vcdr(v);
    return vis_nil(v) ? V_TRUE : V_FALSE;
}
PRED1(boolean_p,  vis_bool)
PRED1(symbol_p,   vis_symbol)
PRED1(string_p,   vis_string)
PRED1(char_p,     vis_char)
PRED1(vector_p,   vis_vector)
PRED1(number_p,   vis_number)
PRED1(integer_p,  num_is_integer)
PRED1(rational_p, vis_exact)
PRED1(real_p,     vis_number)
PRED1(complex_p,  vis_number)
PRED1(exact_p,    vis_exact)
PRED1(inexact_p,  vis_inexact)
PRED1(procedure_p,vis_proc)
PRED1(traced_p,   vis_traced)
PRED1(port_p,     vis_port)
PRED1(eof_object_p,vis_eof)
PRED1(bytevector_p,vis_bytes)
PRED1(set_p,      vis_set)
PRED1(hash_table_p,vis_hash)
PRED1(actor_p,    vis_actor)
PRED1(promise_p,  vis_promise)

static val_t prim_trace(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_symbol(av[0])) scm_raise(V_FALSE, "trace: expected symbol");
    val_t sym  = av[0];
    val_t proc = env_lookup(GLOBAL_ENV, sym);
    if (vis_traced(proc)) return sym;
    Traced *t  = CURRY_NEW(Traced);
    t->hdr.type = T_TRACED; t->hdr.flags = 0;
    t->proc = proc; t->name = sym;
    env_set(GLOBAL_ENV, sym, vptr(t));
    return sym;
}

static val_t prim_untrace(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_symbol(av[0])) scm_raise(V_FALSE, "untrace: expected symbol");
    val_t sym  = av[0];
    val_t proc = env_lookup(GLOBAL_ENV, sym);
    if (!vis_traced(proc)) return sym;
    env_set(GLOBAL_ENV, sym, as_traced(proc)->proc);
    return sym;
}

static val_t prim_zero_p(int ac, val_t *av, void *ud) { (void)ac;(void)ud; return vbool(num_is_zero(av[0])); }
static val_t prim_positive_p(int ac, val_t *av, void *ud) { (void)ac;(void)ud; return vbool(num_is_positive(av[0])); }
static val_t prim_negative_p(int ac, val_t *av, void *ud) { (void)ac;(void)ud; return vbool(num_is_negative(av[0])); }
static val_t prim_nan_p(int ac, val_t *av, void *ud) { (void)ac;(void)ud; return vbool(num_is_nan(av[0])); }
static val_t prim_infinite_p(int ac, val_t *av, void *ud) { (void)ac;(void)ud; return vbool(num_is_infinite(av[0])); }
static val_t prim_finite_p(int ac, val_t *av, void *ud) { (void)ac;(void)ud; return vbool(num_is_finite(av[0])); }
static val_t prim_odd_p(int ac, val_t *av, void *ud) { (void)ac;(void)ud;
    if (vis_fixnum(av[0])) return vbool(vunfix(av[0]) & 1);
    if (vis_bignum(av[0])) return vbool(mpz_odd_p(as_big(av[0])->z));
    scm_raise(V_FALSE, "odd?: not an integer"); }
static val_t prim_even_p(int ac, val_t *av, void *ud) { (void)ac;(void)ud;
    if (vis_fixnum(av[0])) return vbool(!(vunfix(av[0]) & 1));
    if (vis_bignum(av[0])) return vbool(mpz_even_p(as_big(av[0])->z));
    scm_raise(V_FALSE, "even?: not an integer"); }

/* ---- Equivalence ---- */
static val_t prim_eq(int ac, val_t *av, void *ud) {
    (void)ud;
    for (int i = 1; i < ac; i++) if (!scm_eq(av[0], av[i])) return V_FALSE;
    return V_TRUE;
}
static val_t prim_eqv(int ac, val_t *av, void *ud) {
    (void)ud;
    for (int i = 1; i < ac; i++) if (!scm_eqv(av[0], av[i])) return V_FALSE;
    return V_TRUE;
}
static val_t prim_equal(int ac, val_t *av, void *ud) {
    (void)ud;
    for (int i = 1; i < ac; i++) if (!scm_equal(av[0], av[i])) return V_FALSE;
    return V_TRUE;
}

/* ---- Pairs and lists ---- */
static val_t prim_cons(int ac, val_t *av, void *ud) { (void)ac;(void)ud; return scm_cons(av[0], av[1]); }
static val_t prim_car(int ac, val_t *av, void *ud) { (void)ac;(void)ud; if(!vis_pair(av[0])) scm_raise(V_FALSE,"car: not a pair"); return vcar(av[0]); }
static val_t prim_cdr(int ac, val_t *av, void *ud) { (void)ac;(void)ud; if(!vis_pair(av[0])) scm_raise(V_FALSE,"cdr: not a pair"); return vcdr(av[0]); }
static val_t prim_set_car(int ac, val_t *av, void *ud) { (void)ac;(void)ud; as_pair(av[0])->car=av[1]; return V_VOID; }
static val_t prim_set_cdr(int ac, val_t *av, void *ud) { (void)ac;(void)ud; as_pair(av[0])->cdr=av[1]; return V_VOID; }
#define CXR1(n,a)     static val_t prim_c##n##r(int ac,val_t*av,void*ud){(void)ac;(void)ud;return a(av[0]);}
#define CXR2(n,a,b)   static val_t prim_c##n##r(int ac,val_t*av,void*ud){(void)ac;(void)ud;return a(b(av[0]));}
#define CXR3(n,a,b,c) static val_t prim_c##n##r(int ac,val_t*av,void*ud){(void)ac;(void)ud;return a(b(c(av[0])));}
CXR2(aa, vcar, vcar) CXR2(ad, vcar, vcdr) CXR2(da, vcdr, vcar) CXR2(dd, vcdr, vcdr)
CXR3(aaa, vcar, vcar, vcar) CXR3(aad, vcar, vcar, vcdr)
CXR3(ada, vcar, vcdr, vcar) CXR3(add, vcar, vcdr, vcdr)
CXR3(daa, vcdr, vcar, vcar) CXR3(dad, vcdr, vcar, vcdr)
CXR3(dda, vcdr, vcdr, vcar) CXR3(ddd, vcdr, vcdr, vcdr)
#undef CXR1
#undef CXR2
#undef CXR3

static val_t prim_list(int ac, val_t *av, void *ud) {
    (void)ud; val_t r = V_NIL;
    for (int i = ac-1; i >= 0; i--) r = scm_cons(av[i], r);
    return r;
}
static val_t prim_list_star(int ac, val_t *av, void *ud) {
    (void)ud; if (ac == 0) return V_NIL;
    val_t r = av[ac-1];
    for (int i = ac-2; i >= 0; i--) r = scm_cons(av[i], r);
    return r;
}
static val_t prim_length(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    int n = 0; val_t l = av[0];
    while (vis_pair(l)) { n++; l = vcdr(l); }
    if (!vis_nil(l)) scm_raise(V_FALSE, "length: not a proper list");
    return vfix(n);
}
static val_t prim_append(int ac, val_t *av, void *ud) {
    (void)ud; if (ac == 0) return V_NIL;
    val_t r = av[ac-1];
    for (int i = ac-2; i >= 0; i--) r = scm_append(av[i], r);
    return r;
}
static val_t prim_reverse(int ac, val_t *av, void *ud) { (void)ac;(void)ud; return scm_reverse(av[0]); }
static val_t prim_list_tail(int ac, val_t *av, void *ud) { (void)ac;(void)ud; return scm_list_tail(av[0], (int)vunfix(av[1])); }
static val_t prim_list_ref(int ac, val_t *av, void *ud) { (void)ac;(void)ud; return scm_list_ref(av[0], (int)vunfix(av[1])); }
static val_t prim_list_copy(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    val_t lst = av[0], r = V_NIL, *tail = &r;
    while (vis_pair(lst)) {
        Pair *p = CURRY_NEW(Pair); p->hdr.type=T_PAIR; p->hdr.flags=0; p->car=vcar(lst); p->cdr=V_NIL;
        *tail = vptr(p); tail = &p->cdr; lst = vcdr(lst);
    }
    *tail = lst; return r;
}

static val_t prim_member(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    val_t obj=av[0], lst=av[1];
    while (vis_pair(lst)) { if (scm_equal(vcar(lst),obj)) return lst; lst=vcdr(lst); }
    return V_FALSE;
}
static val_t prim_memq(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    val_t obj=av[0], lst=av[1];
    while (vis_pair(lst)) { if (scm_eq(vcar(lst),obj)) return lst; lst=vcdr(lst); }
    return V_FALSE;
}
static val_t prim_memv(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    val_t obj=av[0], lst=av[1];
    while (vis_pair(lst)) { if (scm_eqv(vcar(lst),obj)) return lst; lst=vcdr(lst); }
    return V_FALSE;
}
static val_t prim_assoc(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    val_t key=av[0], alist=av[1];
    while (vis_pair(alist)) { if (scm_equal(vcar(vcar(alist)),key)) return vcar(alist); alist=vcdr(alist); }
    return V_FALSE;
}
static val_t prim_assq(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    val_t key=av[0], alist=av[1];
    while (vis_pair(alist)) { if (scm_eq(vcar(vcar(alist)),key)) return vcar(alist); alist=vcdr(alist); }
    return V_FALSE;
}
static val_t prim_assv(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    val_t key=av[0], alist=av[1];
    while (vis_pair(alist)) { if (scm_eqv(vcar(vcar(alist)),key)) return vcar(alist); alist=vcdr(alist); }
    return V_FALSE;
}

/* ---- Arithmetic ---- */
static val_t prim_add(int ac, val_t *av, void *ud) {
    (void)ud; val_t r = vfix(0);
    for (int i=0; i<ac; i++) r = num_add(r, av[i]);
    return r;
}
static val_t prim_mul(int ac, val_t *av, void *ud) {
    (void)ud; val_t r = vfix(1);
    for (int i=0; i<ac; i++) r = num_mul(r, av[i]);
    return r;
}
static val_t prim_sub(int ac, val_t *av, void *ud) {
    (void)ud;
    if (ac == 1) return num_neg(av[0]);
    val_t r = av[0];
    for (int i=1; i<ac; i++) r = num_sub(r, av[i]);
    return r;
}
static val_t prim_div(int ac, val_t *av, void *ud) {
    (void)ud;
    if (ac == 1) return num_div(vfix(1), av[0]);
    val_t r = av[0];
    for (int i=1; i<ac; i++) r = num_div(r, av[i]);
    return r;
}

/* Comparison: (= a b c...) etc. */
#define NUM_CMP(fn, op) \
static val_t prim_num_##fn(int ac, val_t *av, void *ud) { \
    (void)ud; for (int i=1;i<ac;i++) if (!num_##op(av[i-1],av[i])) return V_FALSE; return V_TRUE; }
NUM_CMP(eq,eq) NUM_CMP(lt,lt) NUM_CMP(le,le) NUM_CMP(gt,gt) NUM_CMP(ge,ge)

static val_t prim_max(int ac, val_t *av, void *ud) {(void)ud; val_t r=av[0]; for(int i=1;i<ac;i++) r=num_max(r,av[i]); return r;}
static val_t prim_min(int ac, val_t *av, void *ud) {(void)ud; val_t r=av[0]; for(int i=1;i<ac;i++) r=num_min(r,av[i]); return r;}
static val_t prim_abs(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_abs(av[0]);}
static val_t prim_gcd(int ac, val_t *av, void *ud) {
    (void)ud; if(ac==0) return vfix(0);
    val_t r=num_abs(av[0]);
    for(int i=1;i<ac;i++) r=num_gcd(r,av[i]);
    return r;
}
static val_t prim_lcm(int ac, val_t *av, void *ud) {
    (void)ud; if(ac==0) return vfix(1);
    val_t r=num_abs(av[0]);
    for(int i=1;i<ac;i++) r=num_lcm(r,av[i]);
    return r;
}
static val_t prim_quotient(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_quotient(av[0],av[1]);}
static val_t prim_remainder(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_remainder(av[0],av[1]);}
static val_t prim_modulo(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_modulo(av[0],av[1]);}
static val_t prim_floor(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_floor(av[0]);}
static val_t prim_ceiling(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_ceiling(av[0]);}
static val_t prim_truncate(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_truncate(av[0]);}
static val_t prim_round(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_round(av[0]);}
static val_t prim_exact(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_exact(av[0]);}
static val_t prim_inexact(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_inexact(av[0]);}
static val_t prim_expt(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_expt(av[0],av[1]);}
static val_t prim_sqrt(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_sqrt(av[0]);}
static val_t prim_exp(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_exp(av[0]);}
static val_t prim_log(int ac, val_t *av, void *ud) {(void)ac;(void)ud; if(ac==2) return num_log(num_div(av[0],av[1])); return num_log(av[0]);}
static val_t prim_sin(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_sin(av[0]);}
static val_t prim_cos(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_cos(av[0]);}
static val_t prim_tan(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_tan(av[0]);}
static val_t prim_asin(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_asin(av[0]);}
static val_t prim_acos(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_acos(av[0]);}
static val_t prim_atan(int ac, val_t *av, void *ud) {(void)ac;(void)ud; if(ac==2) return num_atan2(av[0],av[1]); return num_atan(av[0]);}
static val_t prim_floor_quotient(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_floor(num_div(av[0],av[1]));}
static val_t prim_floor_remainder(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_sub(av[0],num_mul(prim_floor_quotient(ac,av,ud),av[1]));}
static val_t prim_numerator(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (vis_rational(av[0])) { mpz_t z; mpz_init_set(z, mpq_numref(as_rat(av[0])->q)); val_t r=make_big_from_mpz(z); mpz_clear(z); return r; }
    return av[0];
}
static val_t prim_denominator(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (vis_rational(av[0])) { mpz_t z; mpz_init_set(z, mpq_denref(as_rat(av[0])->q)); val_t r=make_big_from_mpz(z); mpz_clear(z); return r; }
    return vfix(1);
}
static val_t prim_make_rectangular(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_make_complex(av[0],av[1]);}
static val_t prim_make_polar(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    double r=num_to_double(av[0]), theta=num_to_double(av[1]);
    return num_make_complex(num_make_float(r*cos(theta)), num_make_float(r*sin(theta)));
}
static val_t prim_real_part(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_real_part(av[0]);}
static val_t prim_imag_part(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_imag_part(av[0]);}
static val_t prim_magnitude(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_magnitude(av[0]);}
static val_t prim_angle(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_angle(av[0]);}

/* Quaternion */
static val_t prim_make_quat(int ac, val_t *av, void *ud) {(void)ud;
    double a=num_to_double(av[0]),b=num_to_double(av[1]),c=num_to_double(av[2]),d=num_to_double(av[3]);
    return num_make_quat(a,b,c,d);
}
static val_t prim_quat_p(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vbool(vis_quat(av[0]));}

/* Octonion */
static val_t prim_make_oct(int ac, val_t *av, void *ud) {
    (void)ud; double e[8]={0};
    for(int i=0;i<ac&&i<8;i++) e[i]=num_to_double(av[i]);
    return num_make_oct(e);
}
static val_t prim_oct_p(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vbool(vis_oct(av[0]));}
static val_t prim_oct_ref(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_oct_ref(av[0],(int)vunfix(av[1]));}

/* Bitwise */
static val_t prim_bitand(int ac, val_t *av, void *ud) {(void)ud; val_t r=ac?av[0]:vfix(-1); for(int i=1;i<ac;i++) r=num_bitand(r,av[i]); return r;}
static val_t prim_bitor(int ac, val_t *av, void *ud) {(void)ud; val_t r=vfix(0); for(int i=0;i<ac;i++) r=num_bitor(r,av[i]); return r;}
static val_t prim_bitxor(int ac, val_t *av, void *ud) {(void)ud; val_t r=vfix(0); for(int i=0;i<ac;i++) r=num_bitxor(r,av[i]); return r;}
static val_t prim_bitnot(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_bitnot(av[0]);}
static val_t prim_shl(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_shl(av[0],(int)vunfix(av[1]));}
static val_t prim_shr(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_shr(av[0],(int)vunfix(av[1]));}

/* number<->string */
static val_t prim_num_str(int ac, val_t *av, void *ud) {
    (void)ud; int radix = ac>1 ? (int)vunfix(av[1]) : 10;
    return num_to_string(av[0], radix);
}
static val_t prim_str_num(int ac, val_t *av, void *ud) {
    (void)ud; int radix = ac>1 ? (int)vunfix(av[1]) : 10;
    return parse_number(as_str(av[0])->data, radix, false, false);
}

/* ---- Characters ---- */
static val_t prim_char_to_int(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vfix((intptr_t)vunchr(av[0]));}
static val_t prim_int_to_char(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vchr((uint32_t)vunfix(av[0]));}
static val_t prim_char_upcase(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vchr((uint32_t)toupper((int)vunchr(av[0])));}
static val_t prim_char_downcase(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vchr((uint32_t)tolower((int)vunchr(av[0])));}
#define CHAR_PRED(nm,test) static val_t prim_char_##nm(int ac, val_t *av, void *ud){(void)ac;(void)ud; return vbool(test((int)vunchr(av[0])));}
CHAR_PRED(alpha_p, isalpha) CHAR_PRED(numeric_p, isdigit) CHAR_PRED(whitespace_p, isspace)
CHAR_PRED(upper_p, isupper) CHAR_PRED(lower_p, islower)
static val_t prim_char_eq(int ac, val_t *av, void *ud) {(void)ud; for(int i=1;i<ac;i++) if(vunchr(av[i-1])!=vunchr(av[i])) return V_FALSE; return V_TRUE;}
static val_t prim_char_lt(int ac, val_t *av, void *ud) {(void)ud; for(int i=1;i<ac;i++) if(vunchr(av[i-1])>=vunchr(av[i])) return V_FALSE; return V_TRUE;}

/* ---- Strings ---- */
static val_t prim_make_string(int ac, val_t *av, void *ud) {
    (void)ud; int fill = ac>1 ? (int)vunchr(av[1]) : ' ';
    return scm_make_string((uint32_t)vunfix(av[0]), fill);
}
static val_t prim_string(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t port = port_open_output_string();
    for(int i=0;i<ac;i++) port_write_char(port,(int)vunchr(av[i]));
    return port_get_output_string(port);
}
static val_t prim_string_length(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    /* UTF-8 character count (not byte length) */
    String *s = as_str(av[0]);
    uint32_t n=0; const char *p=s->data, *end=p+s->len;
    while (p < end) { if ((*p & 0xC0) != 0x80) n++; p++; }
    return vfix(n);
}
static val_t prim_string_ref(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    String *s = as_str(av[0]); intptr_t idx = vunfix(av[1]);
    const char *p = s->data, *end = p + s->len;
    intptr_t n = 0;
    uint32_t cp = 0;
    while (p < end) {
        if ((*p & 0xC0) != 0x80) { if (n == idx) { /* decode */ } n++; }
        p++;
    }
    /* Simplified: iterate to idx-th char */
    p = s->data; n = 0;
    while (p < (const char *)(s->data + s->len) && n < idx) {
        if ((*p & 0xC0) != 0x80) n++;
        p++;
    }
    /* Decode cp at p */
    unsigned char c = (unsigned char)*p;
    if      (c < 0x80)            cp = c;
    else if ((c & 0xE0) == 0xC0) { cp=(c&0x1F); cp=(cp<<6)|((unsigned char)p[1]&0x3F); }
    else if ((c & 0xF0) == 0xE0) { cp=(c&0x0F); cp=(cp<<6)|((unsigned char)p[1]&0x3F); cp=(cp<<6)|((unsigned char)p[2]&0x3F); }
    else { cp=(c&0x07); for(int i=1;i<4;i++) cp=(cp<<6)|((unsigned char)p[i]&0x3F); }
    return vchr(cp);
}
static val_t prim_string_copy(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return scm_string_copy(av[0]);}
static val_t prim_string_append(int ac, val_t *av, void *ud) {
    (void)ud; if(ac==0) { String *e=CURRY_NEW_ATOM(String); e->hdr.type=T_STRING; e->hdr.flags=0; e->len=0; e->hash=0; e->data[0]=0; return vptr(e); }
    val_t r = av[0];
    for(int i=1;i<ac;i++) r = scm_string_append(r, av[i]);
    return r;
}
static val_t prim_string_to_list(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    String *s = as_str(av[0]);
    val_t r = V_NIL;
    /* Decode UTF-8 in reverse */
    val_t port = port_open_input_string(s->data, s->len);
    val_t chars = V_NIL; int cp;
    while((cp=port_read_char(port))!=-1) chars=scm_cons(vchr((uint32_t)cp),chars);
    return scm_reverse(chars);
}
static val_t prim_list_to_string(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    val_t port = port_open_output_string();
    val_t lst = av[0];
    while(vis_pair(lst)) { port_write_char(port,(int)vunchr(vcar(lst))); lst=vcdr(lst); }
    return port_get_output_string(port);
}
static val_t prim_string_to_symbol(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return scm_string_to_symbol(av[0]);}
static val_t prim_symbol_to_string(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return scm_symbol_to_string(av[0]);}
static val_t prim_string_eq(int ac, val_t *av, void *ud) {
    (void)ud; for(int i=1;i<ac;i++) { String *a=as_str(av[i-1]),*b=as_str(av[i]); if(a->len!=b->len||memcmp(a->data,b->data,a->len)!=0) return V_FALSE; } return V_TRUE;
}
static val_t prim_string_lt(int ac, val_t *av, void *ud) {
    (void)ud; for(int i=1;i<ac;i++) { if(strcmp(as_str(av[i-1])->data,as_str(av[i])->data)>=0) return V_FALSE; } return V_TRUE;
}
static val_t prim_substring(int ac, val_t *av, void *ud) {
    (void)ud;
    /* Byte-level substring for now; TODO: Unicode character indices */
    String *s = as_str(av[0]);
    uint32_t start = (uint32_t)vunfix(av[1]);
    uint32_t end   = ac>2 ? (uint32_t)vunfix(av[2]) : s->len;
    uint32_t len   = end - start;
    String *r = (String *)gc_alloc_atomic(sizeof(String)+len+1);
    r->hdr.type=T_STRING; r->hdr.flags=0; r->len=len; r->hash=0;
    memcpy(r->data, s->data+start, len); r->data[len]='\0';
    return vptr(r);
}
static val_t prim_string_contains(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    const char *haystack=as_str(av[0])->data, *needle=as_str(av[1])->data;
    const char *p = strstr(haystack, needle);
    return p ? vfix((intptr_t)(p - haystack)) : V_FALSE;
}

/* ---- Vectors ---- */
static val_t prim_make_vector(int ac, val_t *av, void *ud) {
    (void)ud;
    intptr_t k;
    if (vis_fixnum(av[0])) {
        k = vunfix(av[0]);
    } else if (vis_flonum(av[0])) {
        double d = vfloat(av[0]);
        k = (intptr_t)d;
        if ((double)k != d || k < 0)
            scm_raise(V_FALSE, "𒀭 ḫiṭītu — make-vector: not an exact non-negative integer");
    } else {
        scm_raise(V_FALSE, "𒀭 ḫiṭītu — make-vector: not an exact non-negative integer");
    }
    if (k < 0) scm_raise(V_FALSE, "𒀭 ḫiṭītu — make-vector: negative size");
    uint32_t n = (uint32_t)k; val_t fill=ac>1?av[1]:V_VOID;
    Vector *v = CURRY_NEW_FLEX(Vector, n);
    v->hdr.type=T_VECTOR; v->hdr.flags=0; v->len=n;
    for(uint32_t i=0;i<n;i++) v->data[i]=fill;
    return vptr(v);
}
static val_t prim_vector(int ac, val_t *av, void *ud) {
    (void)ud; Vector *v=CURRY_NEW_FLEX(Vector,(uint32_t)ac);
    v->hdr.type=T_VECTOR; v->hdr.flags=0; v->len=(uint32_t)ac;
    for(int i=0;i<ac;i++) v->data[i]=av[i];
    return vptr(v);
}
static val_t prim_vector_length(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vfix(as_vec(av[0])->len);}
static val_t prim_vector_ref(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    Vector *v = as_vec(av[0]);
    intptr_t i = vunfix(av[1]);
    if (i < 0 || (uint32_t)i >= v->len)
        scm_raise(V_FALSE, "vector-ref: index %ld out of bounds (length %u)", (long)i, v->len);
    return v->data[i];
}
static val_t prim_vector_set(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    Vector *v = as_vec(av[0]);
    intptr_t i = vunfix(av[1]);
    if (i < 0 || (uint32_t)i >= v->len)
        scm_raise(V_FALSE, "vector-set!: index %ld out of bounds (length %u)", (long)i, v->len);
    v->data[i] = av[2];
    return V_VOID;
}
static val_t prim_vector_to_list(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud; Vector *v=as_vec(av[0]); val_t r=V_NIL;
    for(int i=(int)v->len-1;i>=0;i--) r=scm_cons(v->data[i],r);
    return r;
}
static val_t prim_list_to_vector(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    int n=scm_list_length(av[0]);
    Vector *v=CURRY_NEW_FLEX(Vector,(uint32_t)n);
    v->hdr.type=T_VECTOR; v->hdr.flags=0; v->len=(uint32_t)n;
    val_t lst=av[0]; for(int i=0;i<n;i++){v->data[i]=vcar(lst);lst=vcdr(lst);}
    return vptr(v);
}
static val_t prim_vector_fill(int ac, val_t *av, void *ud) {
    (void)ud; Vector *v=as_vec(av[0]); val_t fill=av[1];
    uint32_t s=ac>2?(uint32_t)vunfix(av[2]):0, e=ac>3?(uint32_t)vunfix(av[3]):v->len;
    for(uint32_t i=s;i<e;i++) v->data[i]=fill;
    return V_VOID;
}
static val_t prim_vector_copy(int ac, val_t *av, void *ud) {
    (void)ud; Vector *v=as_vec(av[0]);
    uint32_t s=ac>1?(uint32_t)vunfix(av[1]):0, e=ac>2?(uint32_t)vunfix(av[2]):v->len;
    uint32_t n=e-s; Vector *r=CURRY_NEW_FLEX(Vector,n);
    r->hdr.type=T_VECTOR; r->hdr.flags=0; r->len=n;
    memcpy(r->data, v->data+s, n*sizeof(val_t));
    return vptr(r);
}

/* ---- Bytevectors ---- */
static val_t prim_make_bytes(int ac, val_t *av, void *ud) {
    (void)ud; uint32_t n=(uint32_t)vunfix(av[0]); uint8_t fill=ac>1?(uint8_t)vunfix(av[1]):0;
    Bytevector *b=CURRY_NEW_FLEX_ATOM(Bytevector,n);
    b->hdr.type=T_BYTEVECTOR; b->hdr.flags=0; b->len=n;
    memset(b->data,fill,n);
    return vptr(b);
}
static val_t prim_bytes_length(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vfix(as_bytes(av[0])->len);}
static val_t prim_bytes_u8_ref(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vfix(as_bytes(av[0])->data[vunfix(av[1])]);}
static val_t prim_bytes_u8_set(int ac, val_t *av, void *ud) {(void)ac;(void)ud; as_bytes(av[0])->data[vunfix(av[1])]=(uint8_t)vunfix(av[2]); return V_VOID;}

/* ---- I/O ---- */
static val_t prim_display(int ac, val_t *av, void *ud) {(void)ud; scm_display(av[0],ac>1?av[1]:PORT_STDOUT); return V_VOID;}
static val_t prim_write(int ac, val_t *av, void *ud) {(void)ud; scm_write(av[0],ac>1?av[1]:PORT_STDOUT); return V_VOID;}
static val_t prim_newline(int ac, val_t *av, void *ud) {(void)ud; scm_newline(ac>0?av[0]:PORT_STDOUT); return V_VOID;}
static val_t prim_write_char(int ac, val_t *av, void *ud) {(void)ud; port_write_char(ac>1?av[1]:PORT_STDOUT,(int)vunchr(av[0])); return V_VOID;}
static val_t prim_read(int ac, val_t *av, void *ud) {(void)ud; return scm_read(ac>0?av[0]:PORT_STDIN);}
static val_t prim_read_char(int ac, val_t *av, void *ud) {(void)ud; int c=port_read_char(ac>0?av[0]:PORT_STDIN); return c<0?V_EOF:vchr((uint32_t)c);}
static val_t prim_peek_char(int ac, val_t *av, void *ud) {(void)ud; int c=port_peek_char(ac>0?av[0]:PORT_STDIN); return c<0?V_EOF:vchr((uint32_t)c);}
static val_t prim_read_line(int ac, val_t *av, void *ud) {(void)ud; return port_read_line(ac>0?av[0]:PORT_STDIN);}
static val_t prim_open_input_string(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return port_open_input_string(as_str(av[0])->data,as_str(av[0])->len);}
static val_t prim_open_output_string(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return port_open_output_string();}
static val_t prim_get_output_string(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return port_get_output_string(av[0]);}
static val_t prim_open_file(int ac, val_t *av, void *ud) {(void)ud;
    int flags = ac>1 && !vis_false(av[1]) ? PORT_OUTPUT : PORT_INPUT;
    return port_open_file(as_str(av[0])->data, flags);
}
static val_t prim_close_port(int ac, val_t *av, void *ud) {(void)ac;(void)ud; port_close(av[0]); return V_VOID;}
static val_t prim_input_port_p(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vbool(vis_port(av[0])&&port_is_input(av[0]));}
static val_t prim_output_port_p(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vbool(vis_port(av[0])&&port_is_output(av[0]));}
static val_t prim_current_input_port(int ac, val_t *av, void *ud) {(void)ac;(void)av;(void)ud; return PORT_STDIN;}
static val_t prim_current_output_port(int ac, val_t *av, void *ud) {(void)ac;(void)av;(void)ud; return PORT_STDOUT;}
static val_t prim_current_error_port(int ac, val_t *av, void *ud) {(void)ac;(void)av;(void)ud; return PORT_STDERR;}
static val_t prim_with_output_to_string(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    val_t port = port_open_output_string();
    /* Temporarily redirect stdout... simplified: just call thunk and return "" */
    apply(av[0], V_NIL);
    return port_get_output_string(port);
}

/* ---- Control ---- */
static val_t prim_apply(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t proc = av[0];
    val_t args = V_NIL;
    /* Last arg is a list; prepend previous args */
    val_t last = av[ac-1];
    for (int i = ac-2; i >= 1; i--) last = scm_cons(av[i], last);
    (void)args;
    return apply(proc, last);
}
static val_t prim_map(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t proc = av[0];
    int nlists = ac - 1;
    val_t *lists = av + 1; /* safe to advance in-place; av is caller's stack frame */
    val_t result = V_NIL;
    for (;;) {
        for (int i = 0; i < nlists; i++)
            if (!vis_pair(lists[i])) return scm_reverse(result);
        val_t args = V_NIL;
        for (int i = nlists - 1; i >= 0; i--)
            args = scm_cons(vcar(lists[i]), args);
        result = scm_cons(apply(proc, args), result);
        for (int i = 0; i < nlists; i++)
            lists[i] = vcdr(lists[i]);
    }
}
static val_t prim_for_each(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t proc = av[0];
    int nlists = ac - 1;
    val_t *lists = av + 1;
    for (;;) {
        for (int i = 0; i < nlists; i++)
            if (!vis_pair(lists[i])) return V_VOID;
        val_t args = V_NIL;
        for (int i = nlists - 1; i >= 0; i--)
            args = scm_cons(vcar(lists[i]), args);
        apply(proc, args);
        for (int i = 0; i < nlists; i++)
            lists[i] = vcdr(lists[i]);
    }
}
static val_t prim_list_head(int ac, val_t *av, void *ud) {
    (void)ud;(void)ac;
    val_t lst=av[0]; intptr_t n=vunfix(av[1]); val_t r=V_NIL;
    while(n-->0 && vis_pair(lst)){ r=scm_cons(vcar(lst),r); lst=vcdr(lst); }
    return scm_reverse(r);
}
static val_t prim_filter(int ac, val_t *av, void *ud) {
    (void)ud; val_t pred=av[0], lst=av[1], r=V_NIL;
    while(vis_pair(lst)) { if(vis_true(apply(pred,scm_cons(vcar(lst),V_NIL)))) r=scm_cons(vcar(lst),r); lst=vcdr(lst); }
    return scm_reverse(r);
}
static val_t prim_fold(int ac, val_t *av, void *ud) {
    (void)ud; val_t proc=av[0], init=av[1], lst=av[2];
    while(vis_pair(lst)) { init=apply(proc,scm_cons(vcar(lst),scm_cons(init,V_NIL))); lst=vcdr(lst); }
    return init;
}
static val_t prim_not(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vbool(vis_false(av[0]));}
static val_t prim_force(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_promise(av[0])) return av[0];
    Promise *p = as_promise(av[0]);
    if (p->state == PROMISE_FORCED) return p->val;
    val_t r = apply(p->val, V_NIL);
    if (p->hdr.flags & 1) { /* delay-force: r should be a promise */
        if (vis_promise(r)) {
            Promise *q = as_promise(r);
            if (q->state == PROMISE_FORCED) r = q->val;
            else { p->val = q->val; return apply(p->val, V_NIL); }
        }
    }
    p->val = r; p->state = PROMISE_FORCED;
    return p->val;
}
static val_t prim_make_promise(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (vis_promise(av[0])) return av[0];
    Promise *p = CURRY_NEW(Promise); p->hdr.type=T_PROMISE; p->hdr.flags=0;
    p->state=PROMISE_FORCED; p->val=av[0]; return vptr(p);
}
static val_t prim_error(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t out = port_open_output_string();
    scm_write(av[0], out); /* message */
    for(int i=1;i<ac;i++) { scm_write(av[i],out); port_write_char(out,' '); }
    val_t msg = port_get_output_string(out);
    ErrorObj *e = CURRY_NEW(ErrorObj);
    e->hdr.type=T_ERROR; e->hdr.flags=0;
    e->message=msg; e->irritants=V_NIL; e->kind=S_ERROR;
    scm_raise_val(vptr(e));
}
static val_t prim_error_message(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return as_err(av[0])->message;}
static val_t prim_error_object_p(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vbool(vis_error(av[0]));}
static val_t prim_error_irritants(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return as_err(av[0])->irritants;}
static val_t prim_raise(int ac, val_t *av, void *ud) {(void)ac;(void)ud; scm_raise_val(av[0]);}
static val_t prim_raise_continuable(int ac, val_t *av, void *ud) {(void)ac;(void)ud; scm_raise_val(av[0]);}
static val_t prim_error_to_string(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if(vis_error(av[0])) return as_err(av[0])->message;
    val_t p=port_open_output_string(); scm_write(av[0],p); return port_get_output_string(p);
}
static val_t prim_with_exception_handler(int ac, val_t *av, void *ud) {
    (void)ud; val_t handler=av[0], thunk=av[1];
    val_t result = V_VOID;
    ExnHandler h;
    h.prev = current_handler; current_handler = &h;
    if (setjmp(h.jmp) == 0) {
        result = apply(thunk, V_NIL);
        current_handler = h.prev;
    } else {
        current_handler = h.prev;
        result = apply(handler, scm_cons(h.exn, V_NIL));
    }
    return result;
}

/* ---- Sets ---- */
static val_t prim_make_set(int ac, val_t *av, void *ud) {(void)ud; int cmp=ac>0?(int)vunfix(av[0]):SET_CMP_EQUAL; return set_make(cmp);}
static val_t prim_set_add(int ac, val_t *av, void *ud) {(void)ac;(void)ud; set_add_mut(av[0],av[1]); return V_VOID;}
static val_t prim_set_del(int ac, val_t *av, void *ud) {(void)ac;(void)ud; set_delete_mut(av[0],av[1]); return V_VOID;}
static val_t prim_set_member(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vbool(set_member(av[0],av[1]));}
static val_t prim_set_to_list(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return set_to_list(av[0]);}
static val_t prim_list_to_set(int ac, val_t *av, void *ud) {(void)ud; int cmp=ac>1?(int)vunfix(av[1]):SET_CMP_EQUAL; return list_to_set(av[0],cmp);}
static val_t prim_set_union(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return set_union(av[0],av[1]);}
static val_t prim_set_inter(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return set_intersection(av[0],av[1]);}
static val_t prim_set_diff(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return set_difference(av[0],av[1]);}
static val_t prim_set_subset(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vbool(set_subset(av[0],av[1]));}
static val_t prim_set_size(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vfix(set_size(av[0]));}

/* ---- Hash tables ---- */
static val_t prim_make_hash(int ac, val_t *av, void *ud) {(void)ud; int cmp=ac>0?(int)vunfix(av[0]):SET_CMP_EQUAL; return hash_make(cmp);}
static val_t prim_hash_set(int ac, val_t *av, void *ud) {(void)ac;(void)ud; hash_set(av[0],av[1],av[2]); return V_VOID;}
static val_t prim_hash_ref(int ac, val_t *av, void *ud) {(void)ud; val_t def=ac>2?av[2]:V_FALSE; return hash_ref(av[0],av[1],def);}
static val_t prim_hash_del(int ac, val_t *av, void *ud) {(void)ac;(void)ud; hash_delete(av[0],av[1]); return V_VOID;}
static val_t prim_hash_has(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vbool(hash_has(av[0],av[1]));}
static val_t prim_hash_keys(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return hash_keys(av[0]);}
static val_t prim_hash_vals(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return hash_values(av[0]);}
static val_t prim_hash_to_alist(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return hash_to_alist(av[0]);}
static val_t prim_hash_size(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vfix(hash_size(av[0]));}

/* ---- Actors ---- */
static val_t prim_spawn(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t closure = av[0];
    val_t args = V_NIL;
    for(int i=ac-1;i>=1;i--) args=scm_cons(av[i],args);
    return actor_spawn(closure, args);
}
static val_t prim_send(int ac, val_t *av, void *ud) {(void)ac;(void)ud; actor_send(av[0],av[1]); return V_VOID;}
static val_t prim_receive(int ac, val_t *av, void *ud) {(void)ud;
    long timeout = ac>0 ? (long)vunfix(av[0]) : -1;
    return actor_receive(actor_self(), timeout);
}
static val_t prim_self(int ac, val_t *av, void *ud) {(void)ac;(void)av;(void)ud; return actor_self();}
static val_t prim_actor_alive(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vbool(actor_alive(av[0]));}

/* ---- Dynamic parameters ---- */
static val_t prim_make_parameter(int ac, val_t *av, void *ud) {
    (void)ud;
    Parameter *p = CURRY_NEW(Parameter);
    p->hdr.type=T_PARAMETER; p->hdr.flags=0;
    p->init = av[0];
    p->converter = ac>1 ? av[1] : V_FALSE;
    /* A parameter is a procedure: called with no args returns value, with 1 arg sets it */
    /* We return the Parameter object; the evaluator handles parameterize */
    return vptr(p);
}
static val_t prim_call_parameter(int ac, val_t *av, void *ud) {
    (void)ud;
    /* This is called when a parameter object is invoked as a procedure */
    Parameter *p = as_param(av[0]);
    if (ac == 1) return p->init;
    /* 2 args: set value */
    val_t newval = av[1];
    if (!vis_false(p->converter)) newval = apply(p->converter, scm_cons(newval, V_NIL));
    p->init = newval;
    return V_VOID;
}

/* ---- Record types (internal primitives) ---- */
static val_t prim_record_ctor(int ac, val_t *av, void *ud) {
    (void)ud;
    RecordType *rtd = vunptr(RecordType, av[0]);
    uint32_t n = rtd->nfields;
    Record *r = (Record *)gc_alloc(sizeof(Record) + n * sizeof(val_t));
    r->hdr.type=T_RECORD; r->hdr.flags=0;
    r->rtd = rtd;
    for (uint32_t i=0; i<n && (int)(i+1)<ac; i++) r->fields[i] = av[i+1];
    return vptr(r);
}
static val_t prim_record_pred(int ac, val_t *av, void *ud) {
    (void)ud; RecordType *rtd=vunptr(RecordType,av[0]);
    return vbool(vis_record(av[1]) && as_rec(av[1])->rtd == rtd);
}
static val_t prim_record_ref(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    return as_rec(av[0])->fields[vunfix(av[1])];
}
static val_t prim_record_set(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    as_rec(av[0])->fields[vunfix(av[1])] = av[2];
    return V_VOID;
}

/* ---- Misc ---- */
static val_t prim_gensym(int ac, val_t *av, void *ud) {
    (void)ud; static int counter = 0;
    char buf[32];
    const char *pfx = (ac>0 && vis_string(av[0])) ? as_str(av[0])->data : "g";
    snprintf(buf, sizeof(buf), "%s%d", pfx, counter++);
    return sym_intern_cstr(buf);
}
static val_t prim_values(int ac, val_t *av, void *ud) {
    (void)ud; if(ac==1) return av[0];
    Values *mv=(Values *)gc_alloc(sizeof(Values)+(size_t)ac*sizeof(val_t));
    mv->hdr.type=T_VALUES; mv->hdr.flags=0; mv->count=(uint32_t)ac;
    for(int i=0;i<ac;i++) mv->vals[i]=av[i];
    return vptr(mv);
}
static val_t prim_void(int ac, val_t *av, void *ud) {(void)ac;(void)av;(void)ud; return V_VOID;}
static val_t prim_boolean_eq(int ac, val_t *av, void *ud) {(void)ud; for(int i=1;i<ac;i++) if(av[i-1]!=av[i]) return V_FALSE; return V_TRUE;}
static val_t prim_load(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return scm_load(as_str(av[0])->data, GLOBAL_ENV);}
static val_t prim_exit(int ac, val_t *av, void *ud) {(void)ud; exit(ac>0 ? (int)vunfix(av[0]) : 0);}
static val_t prim_gc(int ac, val_t *av, void *ud) {(void)ac;(void)av;(void)ud; gc_collect(); return V_VOID;}
static val_t prim_floor_div(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    val_t q=prim_floor_quotient(ac,av,ud), r2=num_sub(av[0],num_mul(q,av[1]));
    Values *mv=(Values *)gc_alloc(sizeof(Values)+2*sizeof(val_t));
    mv->hdr.type=T_VALUES; mv->hdr.flags=0; mv->count=2; mv->vals[0]=q; mv->vals[1]=r2;
    return vptr(mv);
}

/* ---- Symbolic / CAS primitives ---- */
extern void scm_raise(val_t, const char *, ...) __attribute__((noreturn));

static val_t prim_sx_diff(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_diff(av[0], av[1]); }
static val_t prim_sx_simplify(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_simplify(av[0]); }
static val_t prim_sx_substitute(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_substitute(av[0], av[1], av[2]); }
static val_t prim_sym_var(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_symbol(av[0])) scm_raise(V_FALSE, "sym-var: argument must be a symbol");
    return sx_make_var(av[0]);
}
static val_t prim_sym_var_p(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return vbool(vis_symvar(av[0])); }
static val_t prim_sym_expr_p(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return vbool(vis_symexpr(av[0])); }
static val_t prim_symbolic_p(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return vbool(vis_symbolic(av[0])); }
static val_t prim_sym_var_name(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_symvar(av[0])) scm_raise(V_FALSE, "sym-var-name: not a symbolic variable");
    return sx_var_name(av[0]);
}

/* ---- Quantum primitives ---- */
static val_t prim_superpose(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return quantum_from_pairs(av[0]); }
static val_t prim_quantum_uniform(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return quantum_uniform(av[0]); }
static val_t prim_observe(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_quantum(av[0])) scm_raise(V_FALSE, "observe: not a quantum value");
    return quantum_observe(av[0]);
}
static val_t prim_quantum_p(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return vbool(vis_quantum(av[0])); }
static val_t prim_quantum_states(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_quantum(av[0])) scm_raise(V_FALSE, "quantum-states: not a quantum value");
    return quantum_to_list(av[0]);
}
static val_t prim_quantum_n(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return vfix(quantum_n(av[0])); }

/* ---- Surreal primitives ---- */

static val_t prim_surreal_p(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return vis_surreal(av[0]) ? V_TRUE : V_FALSE; }

static val_t prim_surreal_infinite_p(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud;
      if (!vis_surreal(av[0])) return V_FALSE;
      return sur_infinite_p(av[0]) ? V_TRUE : V_FALSE; }

static val_t prim_surreal_finite_p(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud;
      if (!vis_surreal(av[0])) return V_TRUE; /* all normal numbers are finite */
      return sur_finite_p(av[0]) ? V_TRUE : V_FALSE; }

static val_t prim_surreal_infinitesimal_p(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud;
      if (!vis_surreal(av[0])) return V_FALSE;
      return sur_infinitesimal_p(av[0]) ? V_TRUE : V_FALSE; }

static val_t prim_surreal_real_part(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud;
      if (!vis_surreal(av[0])) return av[0];
      return sur_real_part(av[0]); }

static val_t prim_surreal_omega_part(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud;
      if (!vis_surreal(av[0])) return vfix(0);
      return sur_omega_part(av[0]); }

static val_t prim_surreal_epsilon_part(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud;
      if (!vis_surreal(av[0])) return vfix(0);
      return sur_epsilon_part(av[0]); }

static val_t prim_surreal_birthday(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud;
      if (!vis_surreal(av[0])) return num_abs(av[0]);
      return sur_birthday(av[0]); }

static val_t prim_surreal_to_val(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud;
      if (!vis_surreal(av[0])) return av[0];
      return sur_to_val(av[0]); }

static val_t prim_surreal_nterms(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud;
      if (!vis_surreal(av[0])) return vfix(1);
      return vfix(sur_nterms(av[0])); }

/* Build a surreal from a list of (exponent . coefficient) pairs */
static val_t prim_make_surreal(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t lst = av[0];
    int n = 0;
    val_t p = lst;
    while (vis_pair(p)) { n++; p = vcdr(p); }
    if (n == 0) return SUR_ZERO;

    val_t *exps   = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
    val_t *coeffs = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
    p = lst;
    for (int i = 0; i < n; i++) {
        val_t pair = vcar(p);
        if (!vis_pair(pair))
            scm_raise(V_FALSE, "make-surreal: each element must be (exponent . coefficient)");
        exps[i]   = vcar(pair);
        coeffs[i] = vcdr(pair);
        if (!vis_number(exps[i]) || !vis_number(coeffs[i]))
            scm_raise(V_FALSE, "make-surreal: exponents and coefficients must be numbers");
        p = vcdr(p);
    }
    return sur_make(n, exps, coeffs);
}

/* Return list of (exponent . coefficient) pairs */
static val_t prim_surreal_terms(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_surreal(av[0])) {
        /* wrap a plain number as a single-term list */
        val_t pair = scm_cons(vfix(0), av[0]);
        return scm_cons(pair, V_NIL);
    }
    Surreal *s = as_surreal(av[0]);
    val_t result = V_NIL;
    for (int i = s->nterms - 1; i >= 0; i--) {
        val_t pair = scm_cons(s->data[2*i], s->data[2*i+1]);
        result = scm_cons(pair, result);
    }
    return result;
}

/* Auto-differentiation: f'(x) via f(x + ε) */
static val_t prim_auto_diff(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t f = av[0];
    val_t x = av[1];
    val_t x_eps = sur_add(sur_from_val(x), SUR_EPSILON);
    val_t fval = apply(f, scm_cons(x_eps, V_NIL));
    if (vis_surreal(fval)) return sur_epsilon_part(fval);
    return vfix(0);
}

/* ---- Registration ---- */

void builtins_register(val_t env) {
    /* Type predicates */
    DEF("pair?",        prim_pair_p,      1,1); DEF("null?",       prim_null_p,      1,1); DEF("list?", prim_list_p, 1,1);
    DEF("boolean?",     prim_boolean_p,   1,1); DEF("symbol?",     prim_symbol_p,    1,1);
    DEF("string?",      prim_string_p,    1,1); DEF("char?",       prim_char_p,      1,1);
    DEF("vector?",      prim_vector_p,    1,1); DEF("number?",     prim_number_p,    1,1);
    DEF("integer?",     prim_integer_p,   1,1); DEF("rational?",   prim_rational_p,  1,1);
    DEF("real?",        prim_real_p,      1,1); DEF("complex?",    prim_complex_p,   1,1);
    DEF("exact?",       prim_exact_p,     1,1); DEF("inexact?",    prim_inexact_p,   1,1);
    DEF("procedure?",   prim_procedure_p, 1,1); DEF("port?",       prim_port_p,      1,1);
    DEF("traced?",      prim_traced_p,    1,1);
    DEF("trace",        prim_trace,       1,1);
    DEF("untrace",      prim_untrace,     1,1);
    DEF("eof-object?",  prim_eof_object_p,1,1); DEF("bytevector?", prim_bytevector_p,1,1);
    DEF("set?",         prim_set_p,       1,1); DEF("hash-table?", prim_hash_table_p,1,1);
    DEF("actor?",       prim_actor_p,     1,1); DEF("promise?",    prim_promise_p,   1,1);
    DEF("zero?",        prim_zero_p,      1,1); DEF("positive?",   prim_positive_p,  1,1);
    DEF("negative?",    prim_negative_p,  1,1); DEF("nan?",        prim_nan_p,       1,1);
    DEF("infinite?",    prim_infinite_p,  1,1); DEF("finite?",     prim_finite_p,    1,1);
    DEF("odd?",         prim_odd_p,       1,1); DEF("even?",       prim_even_p,      1,1);
    DEF("quaternion?",  prim_quat_p,      1,1); DEF("octonion?",   prim_oct_p,       1,1);

    /* Equivalence */
    DEF("eq?",    prim_eq,    2,-1); DEF("eqv?",   prim_eqv,   2,-1); DEF("equal?", prim_equal, 2,-1);

    /* Pairs */
    DEF("cons",       prim_cons,      2,2); DEF("car",        prim_car,       1,1);
    DEF("cdr",        prim_cdr,       1,1); DEF("set-car!",   prim_set_car,   2,2);
    DEF("caar",prim_caar,1,1); DEF("cadr",prim_cadr,1,1); DEF("cdar",prim_cdar,1,1); DEF("cddr",prim_cddr,1,1);
    DEF("caaar",prim_caaar,1,1); DEF("caadr",prim_caadr,1,1); DEF("cadar",prim_cadar,1,1); DEF("caddr",prim_caddr,1,1);
    DEF("cdaar",prim_cdaar,1,1); DEF("cdadr",prim_cdadr,1,1); DEF("cddar",prim_cddar,1,1); DEF("cdddr",prim_cdddr,1,1);
    DEF("set-cdr!",   prim_set_cdr,   2,2); DEF("list",       prim_list,      0,-1);
    DEF("list*",      prim_list_star, 1,-1); DEF("length",    prim_length,    1,1);
    DEF("append",     prim_append,    0,-1); DEF("reverse",   prim_reverse,   1,1);
    DEF("list-tail",  prim_list_tail, 2,2); DEF("list-head",  prim_list_head, 2,2); DEF("list-ref",   prim_list_ref,  2,2);
    DEF("list-copy",  prim_list_copy, 1,1);
    DEF("member",  prim_member,  2,2); DEF("memq",   prim_memq,   2,2); DEF("memv", prim_memv, 2,2);
    DEF("assoc",   prim_assoc,   2,2); DEF("assq",   prim_assq,   2,2); DEF("assv", prim_assv, 2,2);

    /* Arithmetic */
    DEF("+",  prim_add, 0,-1); DEF("-",  prim_sub, 1,-1);
    DEF("*",  prim_mul, 0,-1); DEF("/",  prim_div, 1,-1);
    DEF("=",  prim_num_eq,  2,-1); DEF("<",  prim_num_lt,  2,-1);
    DEF("<=", prim_num_le,  2,-1); DEF(">",  prim_num_gt,  2,-1);
    DEF(">=", prim_num_ge,  2,-1);
    DEF("max",prim_max,1,-1); DEF("min",prim_min,1,-1); DEF("abs",prim_abs,1,1);
    DEF("gcd",prim_gcd,0,-1); DEF("lcm",prim_lcm,0,-1);
    DEF("quotient",prim_quotient,2,2); DEF("remainder",prim_remainder,2,2);
    DEF("modulo",prim_modulo,2,2);
    DEF("floor",prim_floor,1,1); DEF("ceiling",prim_ceiling,1,1);
    DEF("truncate",prim_truncate,1,1); DEF("round",prim_round,1,1);
    DEF("exact",prim_exact,1,1); DEF("inexact",prim_inexact,1,1);
    DEF("exact->inexact",prim_inexact,1,1); DEF("inexact->exact",prim_exact,1,1);
    DEF("expt",prim_expt,2,2); DEF("sqrt",prim_sqrt,1,1);
    DEF("exp",prim_exp,1,1); DEF("log",prim_log,1,2);
    DEF("sin",prim_sin,1,1); DEF("cos",prim_cos,1,1); DEF("tan",prim_tan,1,1);
    DEF("asin",prim_asin,1,1); DEF("acos",prim_acos,1,1); DEF("atan",prim_atan,1,2);
    DEF("floor-quotient",prim_floor_quotient,2,2);
    DEF("floor-remainder",prim_floor_remainder,2,2);
    DEF("floor/",prim_floor_div,2,2);
    DEF("numerator",prim_numerator,1,1); DEF("denominator",prim_denominator,1,1);
    DEF("make-rectangular",prim_make_rectangular,2,2);
    DEF("make-polar",prim_make_polar,2,2);
    DEF("real-part",prim_real_part,1,1); DEF("imag-part",prim_imag_part,1,1);
    DEF("magnitude",prim_magnitude,1,1); DEF("angle",prim_angle,1,1);
    DEF("number->string",prim_num_str,1,2); DEF("string->number",prim_str_num,1,2);
    /* Bitwise (SRFI-151 / R7RS-large) */
    DEF("bitwise-and",prim_bitand,0,-1); DEF("bitwise-or",prim_bitor,0,-1);
    DEF("bitwise-xor",prim_bitxor,0,-1); DEF("bitwise-not",prim_bitnot,1,1);
    DEF("arithmetic-shift",prim_shl,2,2);
    /* Quaternion */
    DEF("make-quaternion",prim_make_quat,4,4);
    DEF("make-octonion",prim_make_oct,8,8); DEF("octonion-ref",prim_oct_ref,2,2);

    /* Characters */
    DEF("char->integer",prim_char_to_int,1,1); DEF("integer->char",prim_int_to_char,1,1);
    DEF("char-upcase",prim_char_upcase,1,1); DEF("char-downcase",prim_char_downcase,1,1);
    DEF("char-alphabetic?",prim_char_alpha_p,1,1); DEF("char-numeric?",prim_char_numeric_p,1,1);
    DEF("char-whitespace?",prim_char_whitespace_p,1,1);
    DEF("char-upper-case?",prim_char_upper_p,1,1); DEF("char-lower-case?",prim_char_lower_p,1,1);
    DEF("char=?",prim_char_eq,2,-1); DEF("char<?",prim_char_lt,2,-1);

    /* Strings */
    DEF("make-string",prim_make_string,1,2); DEF("string",prim_string,0,-1);
    DEF("string-length",prim_string_length,1,1); DEF("string-ref",prim_string_ref,2,2);
    DEF("string-copy",prim_string_copy,1,1); DEF("string-append",prim_string_append,0,-1);
    DEF("string->list",prim_string_to_list,1,1); DEF("list->string",prim_list_to_string,1,1);
    DEF("string->symbol",prim_string_to_symbol,1,1); DEF("symbol->string",prim_symbol_to_string,1,1);
    DEF("string=?",prim_string_eq,2,-1); DEF("string<?",prim_string_lt,2,-1);
    DEF("substring",prim_substring,2,3); DEF("string-contains",prim_string_contains,2,2);

    /* Vectors */
    DEF("make-vector",prim_make_vector,1,2); DEF("vector",prim_vector,0,-1);
    DEF("vector-length",prim_vector_length,1,1); DEF("vector-ref",prim_vector_ref,2,2);
    DEF("vector-set!",prim_vector_set,3,3); DEF("vector->list",prim_vector_to_list,1,1);
    DEF("list->vector",prim_list_to_vector,1,1); DEF("vector-fill!",prim_vector_fill,2,4);
    DEF("vector-copy",prim_vector_copy,1,3);

    /* Bytevectors */
    DEF("make-bytevector",prim_make_bytes,1,2); DEF("bytevector-length",prim_bytes_length,1,1);
    DEF("bytevector-u8-ref",prim_bytes_u8_ref,2,2); DEF("bytevector-u8-set!",prim_bytes_u8_set,3,3);

    /* I/O */
    DEF("display",prim_display,1,2); DEF("write",prim_write,1,2);
    DEF("newline",prim_newline,0,1); DEF("write-char",prim_write_char,1,2);
    DEF("read",prim_read,0,1); DEF("read-char",prim_read_char,0,1);
    DEF("peek-char",prim_peek_char,0,1); DEF("read-line",prim_read_line,0,1);
    DEF("open-input-string",prim_open_input_string,1,1);
    DEF("open-output-string",prim_open_output_string,0,0);
    DEF("get-output-string",prim_get_output_string,1,1);
    DEF("open-input-file",prim_open_file,1,1);
    DEF("open-output-file",prim_open_file,1,2);
    DEF("close-port",prim_close_port,1,1); DEF("close-input-port",prim_close_port,1,1);
    DEF("close-output-port",prim_close_port,1,1);
    DEF("input-port?",prim_input_port_p,1,1); DEF("output-port?",prim_output_port_p,1,1);
    DEF("current-input-port",prim_current_input_port,0,0);
    DEF("current-output-port",prim_current_output_port,0,0);
    DEF("current-error-port",prim_current_error_port,0,0);
    DEF("with-output-to-string",prim_with_output_to_string,1,1);

    /* Control */
    DEF("apply",prim_apply,2,-1); DEF("map",prim_map,2,-1); DEF("for-each",prim_for_each,2,-1);
    DEF("filter",prim_filter,2,2); DEF("fold-left",prim_fold,3,3);
    DEF("not",prim_not,1,1);
    DEF("force",prim_force,1,1); DEF("make-promise",prim_make_promise,1,1);
    DEF("error",prim_error,1,-1); DEF("raise",prim_raise,1,1);
    DEF("raise-continuable",prim_raise_continuable,1,1);
    DEF("error-message",prim_error_message,1,1);
    DEF("error-object?",prim_error_object_p,1,1);
    DEF("error-object-irritants",prim_error_irritants,1,1);
    DEF("error-object->string",prim_error_to_string,1,1);
    DEF("with-exception-handler",prim_with_exception_handler,2,2);
    DEF("values",prim_values,0,-1);
    DEF("boolean=?",prim_boolean_eq,2,-1);

    /* Sets */
    DEF("make-set",        prim_make_set,    0,1); DEF("set-add!",     prim_set_add,     2,2);
    DEF("set-delete!",     prim_set_del,     2,2); DEF("set-member?",  prim_set_member,  2,2);
    DEF("set->list",       prim_set_to_list, 1,1); DEF("list->set",    prim_list_to_set, 1,2);
    DEF("set-union",       prim_set_union,   2,2); DEF("set-intersection",prim_set_inter,2,2);
    DEF("set-difference",  prim_set_diff,    2,2); DEF("set-subset?",  prim_set_subset,  2,2);
    DEF("set-size",        prim_set_size,    1,1);

    /* Hash tables */
    DEF("make-hash-table", prim_make_hash,   0,1); DEF("hash-table-set!", prim_hash_set, 3,3);
    DEF("hash-table-ref",  prim_hash_ref,    2,3); DEF("hash-table-delete!", prim_hash_del, 2,2);
    DEF("hash-table-exists?", prim_hash_has, 2,2);
    DEF("hash-table-keys", prim_hash_keys,   1,1); DEF("hash-table-values", prim_hash_vals, 1,1);
    DEF("hash-table->alist",prim_hash_to_alist,1,1); DEF("hash-table-size", prim_hash_size, 1,1);

    /* Actors */
    DEF("spawn",      prim_spawn,       1,-1); DEF("send!",      prim_send,        2,2);
    DEF("receive",    prim_receive,     0,1);  DEF("self",       prim_self,        0,0);
    DEF("actor-alive?",prim_actor_alive,1,1);

    /* Parameters */
    DEF("make-parameter", prim_make_parameter, 1,2);

    /* Internal record helpers */
    DEF("%record-ctor",   prim_record_ctor, 1,-1);
    DEF("%record-pred?",  prim_record_pred, 2,2);
    DEF("%record-ref",    prim_record_ref,  2,2);
    DEF("%record-set!",   prim_record_set,  3,3);

    /* Misc */
    DEF("gensym",     prim_gensym,  0,1);
    DEF("void",       prim_void,    0,0);
    DEF("load",       prim_load,    1,1);
    DEF("exit",       prim_exit,    0,1);
    DEF("gc",         prim_gc,      0,0);
    DEF("eof-object", prim_void,    0,0); /* placeholder; EOF created by reader */

    /* Constants */
    env_define(env, sym_intern_cstr("#t"),    V_TRUE);
    env_define(env, sym_intern_cstr("#f"),    V_FALSE);
    env_define(env, sym_intern_cstr("else"),  V_TRUE);   /* for cond/case */
    env_define(env, sym_intern_cstr("pi"),    num_make_float(3.14159265358979323846));
    env_define(env, sym_intern_cstr("+inf.0"),num_make_float(1.0/0.0));
    env_define(env, sym_intern_cstr("-inf.0"),num_make_float(-1.0/0.0));
    env_define(env, sym_intern_cstr("+nan.0"),num_make_float(0.0/0.0));
    env_define(env, sym_intern_cstr("SET-EQ"),    vfix(SET_CMP_EQ));
    env_define(env, sym_intern_cstr("SET-EQV"),   vfix(SET_CMP_EQV));
    env_define(env, sym_intern_cstr("SET-EQUAL"), vfix(SET_CMP_EQUAL));

    /* ---- Akkadian and cuneiform procedure aliases ---- */
    /* For each AKK_PR entry, look up the English binding and register
     * both transliterated and cuneiform names pointing to the same value. */
    {
#define AKK(e, t, c)    /* special form — handled in eval.c */
#define AKK_SF(e, t, c) /* special form — skip */
#define AKK_PR(e, t, c) \
        { \
            val_t _v = env_lookup_or_false(env, sym_intern_cstr(e)); \
            if (!vis_false(_v)) { \
                env_define(env, sym_intern_cstr(t), _v); \
                env_define(env, sym_intern_cstr(c), _v); \
            } \
        }
#include "akkadian_names.h"
        /* macros cleaned up by akkadian_names.h */
#undef AKK
    }

    /* Cuneiform/Akkadian constants */
    env_define(env, sym_intern_cstr("𒌋𒉡"),  V_TRUE);   /* U.NU = "and-not" = #t (truth) */
    env_define(env, sym_intern_cstr("𒉡"),    V_FALSE);  /* NU = "not" = #f */
    env_define(env, sym_intern_cstr("𒊭"),    V_NIL);    /* ŠA3 = inside/empty = '() */
    env_define(env, sym_intern_cstr("ṣifrum"),vfix(0));  /* zero */
    env_define(env, sym_intern_cstr("𒄿𒀭"),  num_make_float(3.14159265358979323846)); /* π */

    /* ---- Symbolic / CAS and Quantum ---- */
    DEF("∂",              prim_sx_diff,         2, 2);
    DEF("simplify",       prim_sx_simplify,     1, 1);
    DEF("substitute",     prim_sx_substitute,   3, 3);
    DEF("sym-var",        prim_sym_var,         1, 1);
    DEF("sym-var?",       prim_sym_var_p,       1, 1);
    DEF("sym-expr?",      prim_sym_expr_p,      1, 1);
    DEF("symbolic?",      prim_symbolic_p,      1, 1);
    DEF("sym-var-name",   prim_sym_var_name,    1, 1);
    DEF("superpose",      prim_superpose,       1, 1);
    DEF("quantum-uniform",prim_quantum_uniform, 1, 1);
    DEF("observe",        prim_observe,         1, 1);
    DEF("quantum?",       prim_quantum_p,       1, 1);
    DEF("quantum-states", prim_quantum_states,  1, 1);
    DEF("quantum-n",      prim_quantum_n,       1, 1);

    /* Surreal numbers */
    env_define(env, sym_intern_cstr("omega"),   SUR_OMEGA);
    env_define(env, sym_intern_cstr("epsilon"),  SUR_EPSILON);
    DEF("surreal?",             prim_surreal_p,             1, 1);
    DEF("surreal-infinite?",    prim_surreal_infinite_p,    1, 1);
    DEF("surreal-finite?",      prim_surreal_finite_p,      1, 1);
    DEF("surreal-infinitesimal?",prim_surreal_infinitesimal_p,1,1);
    DEF("surreal-real-part",    prim_surreal_real_part,     1, 1);
    DEF("surreal-omega-part",   prim_surreal_omega_part,    1, 1);
    DEF("surreal-epsilon-part", prim_surreal_epsilon_part,  1, 1);
    DEF("surreal-birthday",     prim_surreal_birthday,      1, 1);
    DEF("surreal-nterms",       prim_surreal_nterms,        1, 1);
    DEF("surreal->number",      prim_surreal_to_val,        1, 1);
    DEF("make-surreal",         prim_make_surreal,          1, 1);
    DEF("surreal-terms",        prim_surreal_terms,         1, 1);
    DEF("auto-diff",            prim_auto_diff,             2, 2);

    /* Multivectors — Clifford algebra Cl(p,q,r) */
    extern void mv_register_builtins(val_t env);
    mv_register_builtins(env);
}
