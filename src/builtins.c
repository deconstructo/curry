#include "builtins.h"
#include "object.h"
#include "eval.h"
#include "vm.h"
#include "debug.h"
#include "syntax_rules.h"
#include "env.h"
#include "symbol.h"
#include "numeric.h"
#include "port.h"
#include "reader.h"
#include "set.h"
#include "actors.h"
#include "gc.h"
#include "profiling.h"
#include "numtheory.h"
#include "unicode.h"
#include "lang_registry.h"
#ifdef BUILD_MPFR
#include "mpfr_num.h"
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <time.h>
#include <math.h>
#include <gmp.h>
#include <assert.h>
#include <math.h>
#include <ctype.h>
#include <limits.h>

/* ---- Registration helper ---- */

void defprim(val_t env, const char *name, PrimFn fn, int min, int max) {
    Primitive *p = CURRY_NEW_PINNED(Primitive);
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

static val_t scm_append_inner(val_t a, val_t b) {
    if (vis_nil(a)) return b;
    return scm_cons(vcar(a), scm_append_inner(vcdr(a), b));
}
val_t scm_append(val_t a, val_t b) {
    gc_inhibit_minor();
    val_t r = scm_append_inner(a, b);
    gc_resume_minor();
    return r;
}

val_t scm_reverse(val_t lst) {
    val_t r = V_NIL;
    gc_inhibit_minor();
    while (vis_pair(lst)) { r = scm_cons(vcar(lst), r); lst = vcdr(lst); }
    gc_resume_minor();
    return r;
}

/* ---- String helpers ---- */

val_t scm_make_string(uint32_t len, int fill) {
    int bytes_per = fill < 0x80 ? 1 : fill < 0x800 ? 2 : fill < 0x10000 ? 3 : 4;
    uint32_t total = len * (uint32_t)bytes_per;
    String *s = (String *)gc_alloc_atomic(sizeof(String) + total + 1);
    s->hdr.type=T_STRING; s->hdr.flags=0; s->len=total; s->hash=0;
    s->orig_cap=total; s->ext=NULL;
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
    const char *sdata = str_data(s);
    String *c = (String *)gc_alloc_atomic(sizeof(String) + s->len + 1);
    c->hdr.type = T_STRING; c->hdr.flags = 0; c->len = s->len; c->hash = s->hash;
    c->orig_cap = s->len; c->ext = NULL;
    memcpy(c->data, sdata, s->len + 1);
    return vptr(c);
}

val_t scm_string_append(val_t a, val_t b) {
    String *sa = as_str(a), *sb = as_str(b);
    uint32_t len = sa->len + sb->len;
    String *r = (String *)gc_alloc_atomic(sizeof(String) + len + 1);
    r->hdr.type=T_STRING; r->hdr.flags=0; r->len=len; r->hash=0; r->orig_cap=len; r->ext=NULL;
    memcpy(r->data, str_data(sa), sa->len);
    memcpy(r->data + sa->len, str_data(sb), sb->len);
    r->data[len] = '\0';
    return vptr(r);
}

val_t scm_string_to_symbol(val_t sv) {
    String *s = as_str(sv);
    return sym_intern(str_data(s), s->len);
}

val_t scm_symbol_to_string(val_t sym) {
    const char *name = sym_cstr(sym);
    uint32_t len = sym_len(sym);
    String *s = (String *)gc_alloc_atomic(sizeof(String) + len + 1);
    s->hdr.type=T_STRING; s->hdr.flags=0; s->len=len; s->hash=0; s->orig_cap=len; s->ext=NULL;
    memcpy(s->data, name, len + 1);
    return vptr(s);
}

/* ---- Type predicates ---- */
#define PRED1(name, test) static val_t prim_##name(int ac, val_t *av, void *ud) { (void)ac;(void)ud; return vbool(test(av[0])); }

PRED1(pair_p,     vis_pair)
PRED1(null_p,     vis_nil)
static val_t prim_list_p(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    /* Floyd tortoise-and-hare: circular lists must yield #f, not hang */
    val_t slow = av[0], fast = av[0];
    while (vis_pair(fast)) {
        fast = vcdr(fast);
        if (!vis_pair(fast)) break;
        fast = vcdr(fast);
        slow = vcdr(slow);
        if (fast == slow) return V_FALSE;
    }
    return vis_nil(fast) ? V_TRUE : V_FALSE;
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
static val_t prim_procedure_p(int ac, val_t *av, void *ud) {
    (void)ud; (void)ac;
    return vbool(vis_proc(av[0]) || vis_bcclosure(av[0]));
}
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
static val_t prim_cons(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    /* Allocate first; av[] points into VM stack so GC can update av[0]/av[1].
     * Re-reading after gc_alloc avoids stale nursery pointers in car/cdr. */
    Pair *p = (Pair *)gc_alloc(sizeof(Pair));
    p->hdr.type = T_PAIR; p->hdr.flags = 0;
    p->car = av[0]; p->cdr = av[1];
    return vptr(p);
}
static val_t prim_car(int ac, val_t *av, void *ud) { (void)ac;(void)ud; if(!vis_pair(av[0])) scm_raise_code(EC_WRONG_TYPE_ARGUMENT,"car: not a pair"); return vcar(av[0]); }
static val_t prim_cdr(int ac, val_t *av, void *ud) { (void)ac;(void)ud; if(!vis_pair(av[0])) scm_raise_code(EC_WRONG_TYPE_ARGUMENT,"cdr: not a pair"); return vcdr(av[0]); }
static val_t prim_set_car(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_pair(av[0])) scm_raise_code(EC_WRONG_TYPE_ARGUMENT, "set-car!: not a pair");
    Pair *p = as_pair(av[0]);
    gc_wb_slot(&p->car, av[1]);
    return V_VOID;
}
static val_t prim_set_cdr(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_pair(av[0])) scm_raise_code(EC_WRONG_TYPE_ARGUMENT, "set-cdr!: not a pair");
    Pair *p = as_pair(av[0]);
    gc_wb_slot(&p->cdr, av[1]);
    return V_VOID;
}
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
    gc_inhibit_minor();
    for (int i = ac-1; i >= 0; i--) r = scm_cons(av[i], r);
    gc_resume_minor();
    return r;
}
static val_t prim_list_star(int ac, val_t *av, void *ud) {
    (void)ud; if (ac == 0) return V_NIL;
    val_t r = av[ac-1];
    gc_inhibit_minor();
    for (int i = ac-2; i >= 0; i--) r = scm_cons(av[i], r);
    gc_resume_minor();
    return r;
}
static val_t prim_length(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    /* Floyd tortoise-and-hare: raise on circular lists instead of hanging */
    long n = 0; val_t slow = av[0], fast = av[0];
    while (vis_pair(fast)) {
        fast = vcdr(fast); n++;
        if (!vis_pair(fast)) break;
        fast = vcdr(fast); n++;
        slow = vcdr(slow);
        if (fast == slow) scm_raise(V_FALSE, "length: circular list");
    }
    if (!vis_nil(fast)) scm_raise(V_FALSE, "length: not a proper list");
    return vfix(n);
}
static val_t prim_append(int ac, val_t *av, void *ud) {
    (void)ud; if (ac == 0) return V_NIL;
    val_t r = av[ac-1];
    for (int i = ac-2; i >= 0; i--) r = scm_append(av[i], r);
    return r;
}
static val_t prim_reverse(int ac, val_t *av, void *ud) { (void)ac;(void)ud; return scm_reverse(av[0]); }
static val_t prim_list_tail(int ac, val_t *av, void *ud) { (void)ac;(void)ud; if (!vis_fixnum(av[1])) scm_raise(V_FALSE, "not a number: index must be exact integer"); return scm_list_tail(av[0], (int)vunfix(av[1])); }
static val_t prim_list_ref(int ac, val_t *av, void *ud) { (void)ac;(void)ud; if (!vis_fixnum(av[1])) scm_raise(V_FALSE, "not a number: index must be exact integer"); return scm_list_ref(av[0], (int)vunfix(av[1])); }
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
static val_t prim_log(int ac, val_t *av, void *ud) {(void)ud; if(ac==2) return num_div(num_log(av[0]),num_log(av[1])); return num_log(av[0]);}
static val_t prim_sin(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_sin(av[0]);}
static val_t prim_cos(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_cos(av[0]);}
static val_t prim_tan(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_tan(av[0]);}
static val_t prim_asin(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_asin(av[0]);}
static val_t prim_acos(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_acos(av[0]);}
static val_t prim_atan(int ac, val_t *av, void *ud) {(void)ac;(void)ud; if(ac==2) return num_atan2(av[0],av[1]); return num_atan(av[0]);}
static val_t prim_sinh(int ac, val_t *av, void *ud)  {(void)ac;(void)ud; return num_sinh(av[0]);}
static val_t prim_cosh(int ac, val_t *av, void *ud)  {(void)ac;(void)ud; return num_cosh(av[0]);}
static val_t prim_tanh(int ac, val_t *av, void *ud)  {(void)ac;(void)ud; return num_tanh(av[0]);}
static val_t prim_asinh(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_asinh(av[0]);}
static val_t prim_acosh(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_acosh(av[0]);}
static val_t prim_atanh(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_atanh(av[0]);}
static val_t prim_cot(int ac, val_t *av, void *ud)   {(void)ac;(void)ud; return num_cot(av[0]);}
static val_t prim_sec(int ac, val_t *av, void *ud)   {(void)ac;(void)ud; return num_sec(av[0]);}
static val_t prim_csc(int ac, val_t *av, void *ud)   {(void)ac;(void)ud; return num_csc(av[0]);}
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
static val_t prim_make_quat(int ac, val_t *av, void *ud) {(void)ac;(void)ud;
    double a=num_to_double(av[0]),b=num_to_double(av[1]),c=num_to_double(av[2]),d=num_to_double(av[3]);
    return num_make_quat(a,b,c,d);
}
static val_t prim_quat_p(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vbool(vis_quat(av[0]));}

/* Quaternion accessors and operations */
static val_t prim_quat_w(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_quat(av[0])) scm_raise(V_FALSE, "quaternion-w: not a quaternion");
    return num_quat_a(av[0]);
}
static val_t prim_quat_x(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_quat(av[0])) scm_raise(V_FALSE, "quaternion-x: not a quaternion");
    return num_quat_b(av[0]);
}
static val_t prim_quat_y(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_quat(av[0])) scm_raise(V_FALSE, "quaternion-y: not a quaternion");
    return num_quat_c(av[0]);
}
static val_t prim_quat_z(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_quat(av[0])) scm_raise(V_FALSE, "quaternion-z: not a quaternion");
    return num_quat_d(av[0]);
}
static val_t prim_quat_norm(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_quat(av[0])) scm_raise(V_FALSE, "quaternion-norm: not a quaternion");
    return num_quat_norm(av[0]);
}
static val_t prim_quat_normalize(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_quat(av[0])) scm_raise(V_FALSE, "quaternion-normalize: not a quaternion");
    return num_quat_normalize(av[0]);
}
static val_t prim_quat_conjugate(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_quat(av[0])) scm_raise(V_FALSE, "quaternion-conjugate: not a quaternion");
    return num_quat_conjugate(av[0]);
}
static val_t prim_quat_inverse(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_quat(av[0])) scm_raise(V_FALSE, "quaternion-inverse: not a quaternion");
    return num_quat_inverse(av[0]);
}
static val_t prim_quat_add(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t acc = vfix(0);
    for (int i = 0; i < ac; i++) acc = num_add(acc, av[i]);
    return acc;
}
static val_t prim_quat_mul(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t acc = vfix(1);
    for (int i = 0; i < ac; i++) acc = num_mul(acc, av[i]);
    return acc;
}
static val_t prim_quat_rotate(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_quat(av[0])) scm_raise(V_FALSE, "quaternion-rotate-vector: not a quaternion");
    if (!vis_quat(av[1])) scm_raise(V_FALSE, "quaternion-rotate-vector: vector must be a quaternion");
    return num_quat_rotate(av[0], av[1]);
}

/* Octonion */
static val_t prim_make_oct(int ac, val_t *av, void *ud) {
    (void)ud; double e[8]={0};
    for(int i=0;i<ac&&i<8;i++) e[i]=num_to_double(av[i]);
    return num_make_oct(e);
}
static val_t prim_oct_p(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vbool(vis_oct(av[0]));}
static val_t prim_oct_ref(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_oct(av[0])) scm_raise(V_FALSE, "octonion-ref: not an octonion");
    if (!vis_fixnum(av[1])) scm_raise_code(EC_WRONG_TYPE_ARGUMENT, "octonion-ref: index must be exact integer");
    intptr_t i = vunfix(av[1]);
    if (i < 0 || i >= 8) scm_raise_code(EC_INDEX_OUT_OF_RANGE, "octonion-ref: index out of range (0..7)");
    return num_oct_ref(av[0], (int)i);
}

/* Bitwise */
static val_t prim_bitand(int ac, val_t *av, void *ud) {(void)ud; val_t r=ac?av[0]:vfix(-1); for(int i=1;i<ac;i++) r=num_bitand(r,av[i]); return r;}
static val_t prim_bitor(int ac, val_t *av, void *ud) {(void)ud; val_t r=vfix(0); for(int i=0;i<ac;i++) r=num_bitor(r,av[i]); return r;}
static val_t prim_bitxor(int ac, val_t *av, void *ud) {(void)ud; val_t r=vfix(0); for(int i=0;i<ac;i++) r=num_bitxor(r,av[i]); return r;}
static val_t prim_bitnot(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_bitnot(av[0]);}
static val_t prim_shl(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_shl(av[0],(int)vunfix(av[1]));}

/* number<->string */
static val_t prim_num_str(int ac, val_t *av, void *ud) {
    (void)ud;
    /* Check for symbol second arg (notation name) */
    if (ac > 1 && vis_symbol(av[1])) {
        const char *note = as_sym(av[1])->data;
        int places = -1; /* auto */
        /* Check for #:places keyword: (number->string n 'neugebauer #:places k) */
        if (ac >= 4 && vis_symbol(av[2]) && strcmp(as_sym(av[2])->data, "#:places") == 0
                && vis_fixnum(av[3]))
            places = (int)vunfix(av[3]);
        if (strcmp(note, "neugebauer") == 0 || strcmp(note, "Neugebauer") == 0)
            return sex_to_neugebauer(av[0], places);
        if (strcmp(note, "cuneiform") == 0)
            return sex_to_cuneiform(av[0]);
        scm_raise(V_FALSE, "number->string: unknown notation '%s'", note);
    }
    int radix = ac > 1 ? (int)vunfix(av[1]) : 10;
    return num_to_string(av[0], radix);
}
static val_t prim_str_num(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_string(av[0])) scm_raise(V_FALSE, "string->number: not a string");
    const char *s = str_data(as_str(av[0]));
    if (ac > 1 && vis_symbol(av[1])) {
        const char *note = as_sym(av[1])->data;
        if (strcmp(note, "neugebauer") == 0 || strcmp(note, "Neugebauer") == 0)
            return sex_parse_neugebauer(s);
        if (strcmp(note, "cuneiform") == 0)
            return sex_parse_cuneiform(s);
        scm_raise(V_FALSE, "string->number: unknown notation '%s'", note);
    }
    int radix = ac > 1 ? (int)vunfix(av[1]) : 10;
    return parse_number(s, radix, false, false);
}
/* (current-number-notation) → current notation symbol
 * (current-number-notation 'neugebauer) → set notation */
static val_t prim_current_number_notation(int ac, val_t *av, void *ud) {
    (void)ud;
    if (ac == 0) return g_number_notation == 0 ? V_FALSE : g_number_notation;
    if (!vis_symbol(av[0]) && !vis_false(av[0]))
        scm_raise(V_FALSE, "current-number-notation: expected symbol or #f");
    g_number_notation = vis_false(av[0]) ? 0 : av[0];
    return V_VOID;
}

/* ---- Characters ---- */
static val_t prim_char_to_int(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vfix((intptr_t)vunchr(av[0]));}
static val_t prim_int_to_char(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_fixnum(av[0])) scm_raise(V_FALSE, "integer->char: not an exact integer");
    intptr_t cp = vunfix(av[0]);
    /* Rejected outright rather than cast straight to uint32_t: a negative
     * or too-large value would otherwise silently become a bogus (but not
     * memory-unsafe on its own) codepoint outside valid Unicode, which
     * could then produce garbage UTF-8 output wherever the character is
     * later encoded. */
    if (cp < 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        scm_raise_code(EC_INDEX_OUT_OF_RANGE, "integer->char: not a valid Unicode code point");
    return vchr((uint32_t)cp);
}
static val_t prim_char_upcase(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vchr(unicode_to_upper(vunchr(av[0])));}
static val_t prim_char_downcase(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vchr(unicode_to_lower(vunchr(av[0])));}
static val_t prim_char_foldcase(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vchr(unicode_fold_case(vunchr(av[0])));}
#define CHAR_PRED(nm,test) static val_t prim_char_##nm(int ac, val_t *av, void *ud){(void)ac;(void)ud; return vbool(test(vunchr(av[0])));}
CHAR_PRED(alpha_p, unicode_is_alphabetic) CHAR_PRED(numeric_p, unicode_is_numeric) CHAR_PRED(whitespace_p, unicode_is_whitespace)
CHAR_PRED(upper_p, unicode_is_upper) CHAR_PRED(lower_p, unicode_is_lower)
static val_t prim_char_eq(int ac, val_t *av, void *ud) {(void)ud; for(int i=1;i<ac;i++) if(vunchr(av[i-1])!=vunchr(av[i])) return V_FALSE; return V_TRUE;}
static val_t prim_char_lt(int ac, val_t *av, void *ud) {(void)ud; for(int i=1;i<ac;i++) if(vunchr(av[i-1])>=vunchr(av[i])) return V_FALSE; return V_TRUE;}

/* ---- Strings ---- */
static val_t prim_make_string(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_fixnum(av[0])) scm_raise(V_FALSE, "make-string: not an exact integer");
    intptr_t k = vunfix(av[0]);
    /* Negative rejected outright (would otherwise wrap to a huge size on
     * the (uint32_t) cast below and attempt a multi-GB allocation); a size
     * beyond UINT32_MAX rejected too, since it would silently wrap down
     * to a small in-range value on that same cast instead of erroring. */
    if (k < 0 || k > (intptr_t)UINT32_MAX)
        scm_raise_code(EC_INDEX_OUT_OF_RANGE, "make-string: invalid size");
    int fill = ac>1 ? (int)vunchr(av[1]) : ' ';
    return scm_make_string((uint32_t)k, fill);
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
    uint32_t n=0; const char *p=str_data(s), *end=p+s->len;
    while (p < end) { if ((*p & 0xC0) != 0x80) n++; p++; }
    return vfix(n);
}
static val_t prim_string_ref(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_string(av[0])) scm_raise_code(EC_WRONG_TYPE_ARGUMENT, "string-ref: not a string");
    if (!vis_fixnum(av[1])) scm_raise_code(EC_WRONG_TYPE_ARGUMENT, "string-ref: not an exact integer");
    String *s = as_str(av[0]); intptr_t idx = vunfix(av[1]);
    /* idx checked against 0 explicitly (not cast to unsigned) so a negative
     * index raises cleanly instead of wrapping to a huge offset. */
    if (idx < 0) scm_raise_code(EC_INDEX_OUT_OF_RANGE, "string-ref: index out of range");
    const char *sd = str_data(s);
    const char *p = sd, *end = p + s->len;
    intptr_t n = 0;
    while (p < end && n < idx) {
        if ((*p & 0xC0) != 0x80) n++;
        p++;
    }
    /* idx >= character count: either p ran off the end, or (for idx == n
     * at loop exit) p landed exactly on end with no character left to
     * decode there — both mean out of range, checked before any decode
     * read past the buffer. */
    if (p >= end || n < idx) scm_raise_code(EC_INDEX_OUT_OF_RANGE, "string-ref: index out of range");
    unsigned char c = (unsigned char)*p;
    uint32_t cp;
    if      (c < 0x80)            cp = c;
    else if ((c & 0xE0) == 0xC0) { cp=(c&0x1F); cp=(cp<<6)|((unsigned char)p[1]&0x3F); }
    else if ((c & 0xF0) == 0xE0) { cp=(c&0x0F); cp=(cp<<6)|((unsigned char)p[1]&0x3F); cp=(cp<<6)|((unsigned char)p[2]&0x3F); }
    else { cp=(c&0x07); for(int i=1;i<4;i++) cp=(cp<<6)|((unsigned char)p[i]&0x3F); }
    return vchr(cp);
}
/* Helper: advance a UTF-8 pointer by n characters; returns byte offset of char n */
static uint32_t utf8_char_offset(const char *data, uint32_t byte_len, uint32_t n) {
    uint32_t i = 0, b = 0;
    while (b < byte_len && i < n) {
        if (((unsigned char)data[b] & 0xC0) != 0x80) i++;
        b++;
    }
    /* Advance past continuation bytes when i==n was hit mid-sequence */
    while (b < byte_len && ((unsigned char)data[b] & 0xC0) == 0x80) b++;
    return b;
}
/* Validate a [start,end) *character* range against a UTF-8 buffer's real
 * character count and convert it to byte offsets — used by every
 * string primitive that takes an optional start/end pair. Unlike calling
 * utf8_char_offset() directly on an unchecked index, this raises on a
 * negative, inverted, or out-of-range index instead of silently clamping
 * to the buffer end (which corrupts rather than errors, since callers
 * then compute lengths as a difference of two independently-clamped
 * offsets that can end up in the wrong order). */
static void string_range_to_bytes(const char *sd, uint32_t byte_len,
                                   int ac, val_t *av, int start_idx,
                                   const char *who,
                                   uint32_t *sb_out, uint32_t *eb_out) {
    uint32_t nchars = 0;
    for (const char *p = sd, *e = sd + byte_len; p < e; p++)
        if (((unsigned char)*p & 0xC0) != 0x80) nchars++;
    if (ac > start_idx && !vis_fixnum(av[start_idx]))
        scm_raise_code(EC_WRONG_TYPE_ARGUMENT, "%s: start must be exact integer", who);
    if (ac > start_idx + 1 && !vis_fixnum(av[start_idx + 1]))
        scm_raise_code(EC_WRONG_TYPE_ARGUMENT, "%s: end must be exact integer", who);
    intptr_t istart = ac > start_idx     ? vunfix(av[start_idx])     : 0;
    intptr_t iend   = ac > start_idx + 1 ? vunfix(av[start_idx + 1]) : (intptr_t)nchars;
    /* Compared against nchars widened to intptr_t, not iend narrowed to
     * uint32_t: nchars always fits in intptr_t (pointer-width here), but
     * a fixnum index larger than UINT32_MAX would wrap to something
     * small on a plain (uint32_t) cast and could slip past the check. */
    if (istart < 0 || iend < istart || iend > (intptr_t)nchars)
        scm_raise_code(EC_INDEX_OUT_OF_RANGE, "%s: index out of range", who);
    *sb_out = utf8_char_offset(sd, byte_len, (uint32_t)istart);
    *eb_out = utf8_char_offset(sd, byte_len, (uint32_t)iend);
}
static val_t prim_string_copy(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_string(av[0])) scm_raise(V_FALSE, "string-copy: not a string");
    String *s = as_str(av[0]);
    const char *sd = str_data(s);
    uint32_t sb, eb;
    string_range_to_bytes(sd, s->len, ac, av, 1, "string-copy", &sb, &eb);
    uint32_t len = eb - sb;
    String *r = (String *)gc_alloc_atomic(sizeof(String) + len + 1);
    r->hdr.type=T_STRING; r->hdr.flags=0; r->len=len; r->hash=0; r->orig_cap=len; r->ext=NULL;
    memcpy(r->data, sd + sb, len); r->data[len] = '\0';
    return vptr(r);
}
static val_t prim_string_append(int ac, val_t *av, void *ud) {
    (void)ud; if(ac==0) { String *e=CURRY_NEW_ATOM(String); e->hdr.type=T_STRING; e->hdr.flags=0; e->len=0; e->hash=0; e->orig_cap=0; e->ext=NULL; e->data[0]=0; return vptr(e); }
    val_t r = av[0];
    for(int i=1;i<ac;i++) r = scm_string_append(r, av[i]);
    return r;
}
static val_t prim_string_to_list(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_string(av[0])) scm_raise(V_FALSE, "string->list: not a string");
    String *s = as_str(av[0]);
    const char *sd = str_data(s);
    uint32_t sb, eb;
    string_range_to_bytes(sd, s->len, ac, av, 1, "string->list", &sb, &eb);
    val_t port = port_open_input_string(sd + sb, eb - sb);
    val_t chars = V_NIL; int cp;
    while((cp=port_read_char(port))!=-1) chars=scm_cons(vchr((uint32_t)cp),chars);
    return scm_reverse(chars);
}
static val_t prim_string_for_each(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t proc = av[0];
    int nstrs = ac - 1;
    /* Collect ports for each string */
    val_t *ports = (val_t *)alloca((size_t)nstrs * sizeof(val_t));
    for (int i = 0; i < nstrs; i++) {
        String *s = as_str(av[i+1]);
        ports[i] = port_open_input_string(str_data(s), s->len);
    }
    for (;;) {
        val_t args = V_NIL; bool any_eof = false;
        for (int i = nstrs - 1; i >= 0; i--) {
            int cp = port_read_char(ports[i]);
            if (cp == -1) { any_eof = true; break; }
            args = scm_cons(vchr((uint32_t)cp), args);
        }
        if (any_eof) break;
        apply(proc, args);
    }
    return V_VOID;
}
static val_t prim_string_fill_bang(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_string(av[0])) scm_raise(V_FALSE, "string-fill!: not a string");
    if (!vis_char(av[1]))   scm_raise(V_FALSE, "string-fill!: not a character");
    String *s = as_str(av[0]);
    char *sd = str_data(s);
    uint32_t sb, eb;
    string_range_to_bytes(sd, s->len, ac, av, 2, "string-fill!", &sb, &eb);
    /* Encode fill char */
    uint32_t u = vunchr(av[1]); char enc[4]; int elen;
    if      (u < 0x80)    { enc[0]=(char)u; elen=1; }
    else if (u < 0x800)   { enc[0]=(char)(0xC0|(u>>6)); enc[1]=(char)(0x80|(u&0x3F)); elen=2; }
    else if (u < 0x10000) { enc[0]=(char)(0xE0|(u>>12)); enc[1]=(char)(0x80|((u>>6)&0x3F)); enc[2]=(char)(0x80|(u&0x3F)); elen=3; }
    else { enc[0]=(char)(0xF0|(u>>18)); enc[1]=(char)(0x80|((u>>12)&0x3F)); enc[2]=(char)(0x80|((u>>6)&0x3F)); enc[3]=(char)(0x80|(u&0x3F)); elen=4; }
    /* Fill byte-by-byte within range (only safe for same-width chars) */
    for (uint32_t b = sb; b + (uint32_t)elen <= eb; b += (uint32_t)elen)
        memcpy(sd + b, enc, (size_t)elen);
    return V_VOID;
}
static val_t prim_string_foldcase(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_string(av[0])) scm_raise(V_FALSE, "string-foldcase: not a string");
    String *s = as_str(av[0]);
    val_t port_in  = port_open_input_string(str_data(s), s->len);
    val_t port_out = port_open_output_string();
    int cp;
    while ((cp = port_read_char(port_in)) != -1)
        port_write_char(port_out, tolower(cp));
    return port_get_output_string(port_out);
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
    (void)ud; for(int i=1;i<ac;i++) { String *a=as_str(av[i-1]),*b=as_str(av[i]); if(a->len!=b->len||memcmp(str_data(a),str_data(b),a->len)!=0) return V_FALSE; } return V_TRUE;
}
static val_t prim_string_lt(int ac, val_t *av, void *ud) {
    (void)ud; for(int i=1;i<ac;i++) { if(strcmp(str_data(as_str(av[i-1])),str_data(as_str(av[i])))>=0) return V_FALSE; } return V_TRUE;
}
static val_t prim_substring(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_string(av[0])) scm_raise(V_FALSE, "substring: not a string");
    String *s = as_str(av[0]);
    const char *sd = str_data(s);
    uint32_t sb, eb;
    string_range_to_bytes(sd, s->len, ac, av, 1, "substring", &sb, &eb);
    uint32_t len = eb - sb;
    String *r = (String *)gc_alloc_atomic(sizeof(String) + len + 1);
    r->hdr.type=T_STRING; r->hdr.flags=0; r->len=len; r->hash=0; r->orig_cap=len; r->ext=NULL;
    memcpy(r->data, sd + sb, len); r->data[len] = '\0';
    return vptr(r);
}
static val_t prim_string_contains(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    const char *haystack=str_data(as_str(av[0])), *needle=str_data(as_str(av[1]));
    const char *p = strstr(haystack, needle);
    return p ? vfix((intptr_t)(p - haystack)) : V_FALSE;
}
static val_t prim_write_string(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_string(av[0])) scm_raise(V_FALSE, "write-string: not a string");
    String *s = as_str(av[0]);
    const char *sd = str_data(s);
    val_t port = ac > 1 ? av[1] : PORT_STDOUT;
    uint32_t sb, eb;
    string_range_to_bytes(sd, s->len, ac, av, 2, "write-string", &sb, &eb);
    for (uint32_t i = sb; i < eb; i++) port_write_byte(port, (uint8_t)sd[i]);
    return V_VOID;
}
static val_t prim_string_to_utf8(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_string(av[0])) scm_raise(V_FALSE, "string->utf8: not a string");
    String *s = as_str(av[0]);
    const char *sd = str_data(s);
    uint32_t sb, eb;
    string_range_to_bytes(sd, s->len, ac, av, 1, "string->utf8", &sb, &eb);
    uint32_t len = eb - sb;
    Bytevector *b = CURRY_NEW_FLEX_ATOM(Bytevector, len);
    b->hdr.type=T_BYTEVECTOR; b->hdr.flags=0; b->len=len;
    memcpy(b->data, sd + sb, len);
    return vptr(b);
}
static val_t prim_utf8_to_string(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_bytes(av[0])) scm_raise(V_FALSE, "utf8->string: not a bytevector");
    Bytevector *bv = as_bytes(av[0]);
    if (ac > 1 && !vis_fixnum(av[1])) scm_raise_code(EC_WRONG_TYPE_ARGUMENT, "utf8->string: start must be exact integer");
    if (ac > 2 && !vis_fixnum(av[2])) scm_raise_code(EC_WRONG_TYPE_ARGUMENT, "utf8->string: end must be exact integer");
    intptr_t istart = ac > 1 ? vunfix(av[1]) : 0;
    intptr_t iend   = ac > 2 ? vunfix(av[2]) : (intptr_t)bv->len;
    if (istart < 0 || iend < istart || iend > (intptr_t)bv->len)
        scm_raise_code(EC_INDEX_OUT_OF_RANGE, "utf8->string: index out of range");
    uint32_t sb = (uint32_t)istart, eb = (uint32_t)iend;
    uint32_t len = eb - sb;
    String *r = (String *)gc_alloc_atomic(sizeof(String) + len + 1);
    r->hdr.type=T_STRING; r->hdr.flags=0; r->len=len; r->hash=0; r->orig_cap=len; r->ext=NULL;
    memcpy(r->data, bv->data + sb, len); r->data[len] = '\0';
    return vptr(r);
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
static val_t prim_vector_length(int ac, val_t *av, void *ud) {(void)ac;(void)ud; if (!vis_vector(av[0])) scm_raise(V_FALSE, "vector-length: not a vector"); return vfix(as_vec(av[0])->len);}
static val_t prim_vector_ref(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_vector(av[0])) scm_raise_code(EC_WRONG_TYPE_ARGUMENT, "vector-ref: not a vector");
    if (!vis_fixnum(av[1])) scm_raise_code(EC_WRONG_TYPE_ARGUMENT, "vector-ref: not an exact integer");
    Vector *v = as_vec(av[0]);
    intptr_t i = vunfix(av[1]);
    if (i < 0 || (uint32_t)i >= v->len)
        scm_raise_code(EC_INDEX_OUT_OF_RANGE, "vector-ref: index %ld out of bounds (length %u)", (long)i, v->len);
    return v->data[i];
}
static val_t prim_vector_set(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_vector(av[0])) scm_raise_code(EC_WRONG_TYPE_ARGUMENT, "vector-set!: not a vector");
    if (!vis_fixnum(av[1])) scm_raise_code(EC_WRONG_TYPE_ARGUMENT, "vector-set!: not an exact integer");
    Vector *v = as_vec(av[0]);
    intptr_t i = vunfix(av[1]);
    if (i < 0 || (uint32_t)i >= v->len)
        scm_raise_code(EC_INDEX_OUT_OF_RANGE, "vector-set!: index %ld out of bounds (length %u)", (long)i, v->len);
    gc_wb_slot(&v->data[i], av[2]);
    return V_VOID;
}
/* Validate a [start,end) index range against len (a vector's/bytevector's
 * real element/byte count) and reject anything out of range instead of
 * silently proceeding with a garbage-clamped or wrapped range — the same
 * bug class fixed for the UTF-8 string primitives' string_range_to_bytes
 * (this is the non-UTF-8 sibling: a plain index range, no multi-byte
 * char-to-byte conversion needed). Compares s/e as intptr_t widened
 * against len rather than len narrowed to match a cast index, so a
 * fixnum index larger than UINT32_MAX can't wrap to something small and
 * slip past the check; a negative index is rejected outright rather than
 * silently becoming a huge uint32_t offset. */
static void validate_index_range(int ac, val_t *av, int start_idx, uint32_t len,
                                  const char *who, uint32_t *s_out, uint32_t *e_out) {
    if (ac > start_idx && !vis_fixnum(av[start_idx]))
        scm_raise_code(EC_WRONG_TYPE_ARGUMENT, "%s: start must be exact integer", who);
    if (ac > start_idx + 1 && !vis_fixnum(av[start_idx + 1]))
        scm_raise_code(EC_WRONG_TYPE_ARGUMENT, "%s: end must be exact integer", who);
    intptr_t s = ac > start_idx     ? vunfix(av[start_idx])     : 0;
    intptr_t e = ac > start_idx + 1 ? vunfix(av[start_idx + 1]) : (intptr_t)len;
    if (s < 0 || e < s || e > (intptr_t)len)
        scm_raise_code(EC_INDEX_OUT_OF_RANGE, "%s: index out of range", who);
    *s_out = (uint32_t)s; *e_out = (uint32_t)e;
}
static val_t prim_vector_to_list(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_vector(av[0])) scm_raise(V_FALSE, "vector->list: not a vector");
    Vector *v = as_vec(av[0]);
    uint32_t s, e;
    validate_index_range(ac, av, 1, v->len, "vector->list", &s, &e);
    val_t r = V_NIL;
    for (int i = (int)e - 1; i >= (int)s; i--) r = scm_cons(v->data[i], r);
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
    (void)ud;
    if (!vis_vector(av[0])) scm_raise(V_FALSE, "vector-fill!: not a vector");
    Vector *v=as_vec(av[0]); val_t fill=av[1];
    uint32_t s, e;
    validate_index_range(ac, av, 2, v->len, "vector-fill!", &s, &e);
    for(uint32_t i=s;i<e;i++) v->data[i]=fill;
    return V_VOID;
}
static val_t prim_vector_copy(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_vector(av[0])) scm_raise(V_FALSE, "vector-copy: not a vector");
    Vector *v=as_vec(av[0]);
    uint32_t s, e;
    validate_index_range(ac, av, 1, v->len, "vector-copy", &s, &e);
    uint32_t n=e-s; Vector *r=CURRY_NEW_FLEX(Vector,n);
    r->hdr.type=T_VECTOR; r->hdr.flags=0; r->len=n;
    memcpy(r->data, v->data+s, n*sizeof(val_t));
    return vptr(r);
}
static val_t prim_vector_copy_bang(int ac, val_t *av, void *ud) {
    /* (vector-copy! to at from [start [end]]) */
    (void)ud;
    if (!vis_vector(av[0])) scm_raise(V_FALSE, "vector-copy!: to must be a vector");
    if (!vis_fixnum(av[1])) scm_raise(V_FALSE, "vector-copy!: at must be exact integer");
    if (!vis_vector(av[2])) scm_raise(V_FALSE, "vector-copy!: from must be a vector");
    Vector *to   = as_vec(av[0]);
    Vector *from = as_vec(av[2]);
    uint32_t s, e;
    validate_index_range(ac, av, 3, from->len, "vector-copy!", &s, &e);
    uint32_t n = e - s;
    /* `at` compared as intptr_t against to->len/n (both widened, no
     * uint32_t narrowing beforehand) so an oversized fixnum can't wrap
     * past the check the way a plain (uint32_t) cast would. */
    intptr_t iat = vunfix(av[1]);
    if (iat < 0 || iat > (intptr_t)to->len || iat + (intptr_t)n > (intptr_t)to->len)
        scm_raise_code(EC_INDEX_OUT_OF_RANGE, "vector-copy!: at/length out of range");
    uint32_t at = (uint32_t)iat;
    memmove(to->data + at, from->data + s, n * sizeof(val_t));
    return V_VOID;
}
static val_t prim_vector_map(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t proc = av[0];
    int nvecs = ac - 1;
    Vector **vecs = (Vector **)alloca((size_t)nvecs * sizeof(Vector *));
    for (int i = 0; i < nvecs; i++) {
        if (!vis_vector(av[i+1])) scm_raise(V_FALSE, "vector-map: not a vector");
        vecs[i] = as_vec(av[i+1]);
    }
    uint32_t len = vecs[0]->len;
    Vector *r = CURRY_NEW_FLEX(Vector, len);
    r->hdr.type=T_VECTOR; r->hdr.flags=0; r->len=len;
    for (uint32_t i = 0; i < len; i++) {
        val_t args = V_NIL;
        for (int j = nvecs - 1; j >= 0; j--)
            args = scm_cons(vecs[j]->data[i], args);
        r->data[i] = apply(proc, args);
    }
    return vptr(r);
}
static val_t prim_vector_for_each(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t proc = av[0];
    int nvecs = ac - 1;
    Vector **vecs = (Vector **)alloca((size_t)nvecs * sizeof(Vector *));
    for (int i = 0; i < nvecs; i++) {
        if (!vis_vector(av[i+1])) scm_raise(V_FALSE, "vector-for-each: not a vector");
        vecs[i] = as_vec(av[i+1]);
    }
    uint32_t len = vecs[0]->len;
    for (uint32_t i = 0; i < len; i++) {
        val_t args = V_NIL;
        for (int j = nvecs - 1; j >= 0; j--)
            args = scm_cons(vecs[j]->data[i], args);
        apply(proc, args);
    }
    return V_VOID;
}

/* ---- Batch 3-D projection ----
 *
 * (vec3-project-batch lx ly lz R cx cy scale dist)
 *
 * Project a cloud of N 3-D points through an arbitrary 3×3 rotation matrix
 * and a perspective divide, returning #(sx sy) — two float vectors of N
 * screen coordinates.  The entire loop runs in C with no Scheme overhead.
 *
 *   lx, ly, lz  — float vectors of length N
 *   R           — float vector, 9 elements, row-major 3×3 rotation matrix
 *   cx, cy      — screen centre (flonum, pixels)
 *   scale       — pixels per unit at eye distance (flonum)
 *   dist        — perspective eye distance (flonum)
 *
 *   sx[i] = cx + scale * (R·p)[0] / (dist + (R·p)[2])
 *   sy[i] = cy - scale * (R·p)[1] / (dist + (R·p)[2])
 *
 * The Y axis is negated so that +Y points up on screen.
 *
 * Euler (azimuth around Y, elevation around X) rotation matrix:
 *   (define (mat3-euler az el)
 *     (let ((ca (cos az)) (sa (sin az)) (ce (cos el)) (se (sin el)))
 *       (vector ca 0.0 sa  (* sa se) ce (- (* ca se))  (- (* sa ce)) se (* ca ce))))
 */
static val_t prim_vec3_project_batch(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    bool use_f64 = vis_f64vec(av[0]);
    if (use_f64) {
        if (!vis_f64vec(av[1]) || !vis_f64vec(av[2]))
            scm_raise(V_FALSE, "vec3-project-batch: lx/ly/lz must all be the same type");
    } else if (!vis_vector(av[0]) || !vis_vector(av[1]) || !vis_vector(av[2])) {
        scm_raise(V_FALSE, "vec3-project-batch: lx/ly/lz must be vectors or f64vectors");
    }
    if (!vis_vector(av[3]))
        scm_raise(V_FALSE, "vec3-project-batch: R must be a 9-element vector");
    uint32_t N  = use_f64 ? as_f64v(av[0])->len : as_vec(av[0])->len;
    uint32_t Ny = use_f64 ? as_f64v(av[1])->len : as_vec(av[1])->len;
    uint32_t Nz = use_f64 ? as_f64v(av[2])->len : as_vec(av[2])->len;
    if (Ny != N || Nz != N)
        scm_raise(V_FALSE, "vec3-project-batch: lx/ly/lz must have equal length");
    Vector *Rm = as_vec(av[3]);
    if (Rm->len != 9)
        scm_raise(V_FALSE, "vec3-project-batch: R must have exactly 9 elements");
    double cx   = vfloat(av[4]);
    double cy   = vfloat(av[5]);
    double sc   = vfloat(av[6]);
    double dist = vfloat(av[7]);
    double r00 = vfloat(Rm->data[0]), r01 = vfloat(Rm->data[1]), r02 = vfloat(Rm->data[2]);
    double r10 = vfloat(Rm->data[3]), r11 = vfloat(Rm->data[4]), r12 = vfloat(Rm->data[5]);
    double r20 = vfloat(Rm->data[6]), r21 = vfloat(Rm->data[7]), r22 = vfloat(Rm->data[8]);
    Vector *sx = CURRY_NEW_FLEX(Vector, N);
    sx->hdr.type = T_VECTOR; sx->hdr.flags = 0; sx->len = N;
    Vector *sy = CURRY_NEW_FLEX(Vector, N);
    sy->hdr.type = T_VECTOR; sy->hdr.flags = 0; sy->len = N;
    if (use_f64) {
        double *dxp = as_f64v(av[0])->data;
        double *dyp = as_f64v(av[1])->data;
        double *dzp = as_f64v(av[2])->data;
        for (uint32_t i = 0; i < N; i++) {
            double x = dxp[i], y = dyp[i], z = dzp[i];
            double fx = r00*x + r01*y + r02*z;
            double fy = r10*x + r11*y + r12*z;
            double fz = r20*x + r21*y + r22*z;
            double w  = dist / (dist + fz);
            sx->data[i] = num_make_float(cx + sc * fx * w);
            sy->data[i] = num_make_float(cy - sc * fy * w);
        }
    } else {
        Vector *lx = as_vec(av[0]), *ly = as_vec(av[1]), *lz = as_vec(av[2]);
        for (uint32_t i = 0; i < N; i++) {
            double x = vfloat(lx->data[i]);
            double y = vfloat(ly->data[i]);
            double z = vfloat(lz->data[i]);
            double fx = r00*x + r01*y + r02*z;
            double fy = r10*x + r11*y + r12*z;
            double fz = r20*x + r21*y + r22*z;
            double w  = dist / (dist + fz);
            sx->data[i] = num_make_float(cx + sc * fx * w);
            sy->data[i] = num_make_float(cy - sc * fy * w);
        }
    }
    Vector *result = CURRY_NEW_FLEX(Vector, 2);
    result->hdr.type = T_VECTOR; result->hdr.flags = 0; result->len = 2;
    result->data[0] = vptr(sx);
    result->data[1] = vptr(sy);
    return vptr(result);
}

/* ---- Bytevectors ---- */
static val_t prim_make_bytes(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_fixnum(av[0])) scm_raise(V_FALSE, "make-bytevector: not an exact integer");
    intptr_t k = vunfix(av[0]);
    /* Same guard as make-string: negative wraps to a huge (uint32_t) size
     * (multi-GB allocation attempt) and beyond UINT32_MAX wraps down to a
     * small in-range value, both silently, without this check. */
    if (k < 0 || k > (intptr_t)UINT32_MAX)
        scm_raise_code(EC_INDEX_OUT_OF_RANGE, "make-bytevector: invalid size");
    uint32_t n=(uint32_t)k; uint8_t fill=ac>1?(uint8_t)vunfix(av[1]):0;
    Bytevector *b=CURRY_NEW_FLEX_ATOM(Bytevector,n);
    b->hdr.type=T_BYTEVECTOR; b->hdr.flags=0; b->len=n;
    memset(b->data,fill,n);
    return vptr(b);
}
static val_t prim_bytes_length(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vfix(as_bytes(av[0])->len);}
static val_t prim_bytes_u8_ref(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_bytes(av[0])) scm_raise_code(EC_WRONG_TYPE_ARGUMENT, "bytevector-u8-ref: not a bytevector");
    if (!vis_fixnum(av[1])) scm_raise_code(EC_WRONG_TYPE_ARGUMENT, "bytevector-u8-ref: not an exact integer");
    Bytevector *b = as_bytes(av[0]);
    intptr_t i = vunfix(av[1]);
    if (i < 0 || (uint32_t)i >= b->len)
        scm_raise_code(EC_INDEX_OUT_OF_RANGE, "bytevector-u8-ref: index %ld out of bounds (length %u)", (long)i, b->len);
    return vfix(b->data[i]);
}
static val_t prim_bytes_u8_set(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_bytes(av[0])) scm_raise_code(EC_WRONG_TYPE_ARGUMENT, "bytevector-u8-set!: not a bytevector");
    if (!vis_fixnum(av[1])) scm_raise_code(EC_WRONG_TYPE_ARGUMENT, "bytevector-u8-set!: not an exact integer");
    if (!vis_fixnum(av[2])) scm_raise_code(EC_WRONG_TYPE_ARGUMENT, "bytevector-u8-set!: value must be exact integer");
    Bytevector *b = as_bytes(av[0]);
    intptr_t i = vunfix(av[1]);
    if (i < 0 || (uint32_t)i >= b->len)
        scm_raise_code(EC_INDEX_OUT_OF_RANGE, "bytevector-u8-set!: index %ld out of bounds (length %u)", (long)i, b->len);
    b->data[i] = (uint8_t)vunfix(av[2]);
    return V_VOID;
}

/* ---- I/O ---- */
static val_t prim_display(int ac, val_t *av, void *ud) {(void)ud; scm_display(av[0],ac>1?av[1]:PORT_STDOUT); return V_VOID;}
static val_t prim_write(int ac, val_t *av, void *ud) {(void)ud; scm_write(av[0],ac>1?av[1]:PORT_STDOUT); return V_VOID;}
static val_t prim_newline(int ac, val_t *av, void *ud) {(void)ud; scm_newline(ac>0?av[0]:PORT_STDOUT); return V_VOID;}
static val_t prim_write_char(int ac, val_t *av, void *ud) {(void)ud; port_write_char(ac>1?av[1]:PORT_STDOUT,(int)vunchr(av[0])); return V_VOID;}
static val_t prim_read(int ac, val_t *av, void *ud) {(void)ud; return scm_read(ac>0?av[0]:PORT_STDIN);}
static val_t prim_read_char(int ac, val_t *av, void *ud) {(void)ud; int c=port_read_char(ac>0?av[0]:PORT_STDIN); return c<0?V_EOF:vchr((uint32_t)c);}
static val_t prim_peek_char(int ac, val_t *av, void *ud) {(void)ud; int c=port_peek_char(ac>0?av[0]:PORT_STDIN); return c<0?V_EOF:vchr((uint32_t)c);}
static val_t prim_read_line(int ac, val_t *av, void *ud) {(void)ud; return port_read_line(ac>0?av[0]:PORT_STDIN);}
static val_t prim_open_input_string(int ac, val_t *av, void *ud) {(void)ac;(void)ud; if (!vis_string(av[0])) scm_raise(V_FALSE, "open-input-string: not a string"); return port_open_input_string(str_data(as_str(av[0])),as_str(av[0])->len);}
static val_t prim_open_output_string(int ac, val_t *av, void *ud) {(void)ac;(void)av;(void)ud; return port_open_output_string();}
static val_t prim_get_output_string(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return port_get_output_string(av[0]);}
static val_t prim_open_input_bytevector(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_bytes(av[0])) scm_raise(V_FALSE, "open-input-bytevector: not a bytevector");
    Bytevector *bv = as_bytes(av[0]);
    return port_open_input_bytevector(bv->data, bv->len);
}
static val_t prim_open_output_bytevector(int ac, val_t *av, void *ud) {(void)ac;(void)av;(void)ud; return port_open_output_bytevector();}
static val_t prim_get_output_bytevector(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return port_get_output_bytevector(av[0]);}
static val_t prim_write_shared(int ac, val_t *av, void *ud) {(void)ud; scm_write_shared(av[0], ac>1?av[1]:PORT_STDOUT); return V_VOID;}
static val_t prim_open_input_file(int ac, val_t *av, void *ud) {(void)ac;(void)ud;
    if (!vis_string(av[0])) scm_raise(V_FALSE, "open-input-file: not a string");
    val_t p = port_open_file(str_data(as_str(av[0])), PORT_INPUT);
    if (p == V_FALSE) scm_raise(S_FILE_ERROR, "open-input-file: cannot open '%s'", str_data(as_str(av[0])));
    return p;
}
static val_t prim_open_output_file(int ac, val_t *av, void *ud) {(void)ac;(void)ud;
    if (!vis_string(av[0])) scm_raise(V_FALSE, "open-output-file: not a string");
    val_t p = port_open_file(str_data(as_str(av[0])), PORT_OUTPUT);
    if (p == V_FALSE) scm_raise(S_FILE_ERROR, "open-output-file: cannot create '%s'", str_data(as_str(av[0])));
    return p;
}
/* ---- R7RS gap-fill ---- */

/* Arithmetic */
static val_t prim_square(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return num_mul(av[0], av[0]);}
static val_t prim_exact_integer_p(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vbool(vis_fixnum(av[0]) || vis_bignum(av[0]));}

static val_t prim_truncate_div(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    val_t q = num_quotient(av[0], av[1]);
    val_t r = num_remainder(av[0], av[1]);
    Values *mv = (Values *)gc_alloc(sizeof(Values) + 2*sizeof(val_t));
    mv->hdr.type = T_VALUES; mv->hdr.flags = 0; mv->count = 2;
    mv->vals[0] = q; mv->vals[1] = r;
    return vptr(mv);
}

static val_t prim_exact_integer_sqrt(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    val_t n = av[0];
    if (vis_fixnum(n)) {
        intptr_t k = vunfix(n);
        if (k < 0) scm_raise(V_FALSE, "exact-integer-sqrt: negative argument");
        intptr_t s = (intptr_t)sqrt((double)k);
        while (s > 0 && s * s > k) s--;
        while ((s+1)*(s+1) <= k) s++;
        intptr_t r = k - s * s;
        Values *mv = (Values *)gc_alloc(sizeof(Values) + 2*sizeof(val_t));
        mv->hdr.type = T_VALUES; mv->hdr.flags = 0; mv->count = 2;
        mv->vals[0] = vfix(s); mv->vals[1] = vfix(r);
        return vptr(mv);
    }
    if (vis_bignum(n)) {
        if (mpz_sgn(as_big(n)->z) < 0)
            scm_raise(V_FALSE, "exact-integer-sqrt: negative argument");
        mpz_t s, r;
        mpz_init(s); mpz_init(r);
        mpz_sqrtrem(s, r, as_big(n)->z);
        val_t vs = make_big_from_mpz(s);
        val_t vr = make_big_from_mpz(r);
        mpz_clear(s); mpz_clear(r);
        Values *mv = (Values *)gc_alloc(sizeof(Values) + 2*sizeof(val_t));
        mv->hdr.type = T_VALUES; mv->hdr.flags = 0; mv->count = 2;
        mv->vals[0] = vs; mv->vals[1] = vr;
        return vptr(mv);
    }
    scm_raise(V_FALSE, "exact-integer-sqrt: requires non-negative exact integer");
}

/* Characters — missing comparators */
static val_t prim_char_le(int ac, val_t *av, void *ud) {(void)ud; for(int i=1;i<ac;i++) if(vunchr(av[i-1])>vunchr(av[i])) return V_FALSE; return V_TRUE;}
static val_t prim_char_gt(int ac, val_t *av, void *ud) {(void)ud; for(int i=1;i<ac;i++) if(vunchr(av[i-1])<=vunchr(av[i])) return V_FALSE; return V_TRUE;}
static val_t prim_char_ge(int ac, val_t *av, void *ud) {(void)ud; for(int i=1;i<ac;i++) if(vunchr(av[i-1])<vunchr(av[i])) return V_FALSE; return V_TRUE;}

/* Case-insensitive char comparisons (scheme char library) */
static val_t prim_char_ci_eq(int ac, val_t *av, void *ud) {(void)ud; for(int i=1;i<ac;i++) if(tolower((int)vunchr(av[i-1]))!=tolower((int)vunchr(av[i]))) return V_FALSE; return V_TRUE;}
static val_t prim_char_ci_lt(int ac, val_t *av, void *ud) {(void)ud; for(int i=1;i<ac;i++) if(tolower((int)vunchr(av[i-1]))>=tolower((int)vunchr(av[i]))) return V_FALSE; return V_TRUE;}
static val_t prim_char_ci_le(int ac, val_t *av, void *ud) {(void)ud; for(int i=1;i<ac;i++) if(tolower((int)vunchr(av[i-1]))>tolower((int)vunchr(av[i]))) return V_FALSE; return V_TRUE;}
static val_t prim_char_ci_gt(int ac, val_t *av, void *ud) {(void)ud; for(int i=1;i<ac;i++) if(tolower((int)vunchr(av[i-1]))<=tolower((int)vunchr(av[i]))) return V_FALSE; return V_TRUE;}
static val_t prim_char_ci_ge(int ac, val_t *av, void *ud) {(void)ud; for(int i=1;i<ac;i++) if(tolower((int)vunchr(av[i-1]))<tolower((int)vunchr(av[i]))) return V_FALSE; return V_TRUE;}

static val_t prim_digit_value(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    uint32_t c = vunchr(av[0]);
    return (c >= '0' && c <= '9') ? vfix((intptr_t)(c - '0')) : V_FALSE;
}

/* Strings — missing comparators */
static val_t prim_string_le(int ac, val_t *av, void *ud) {(void)ud; for(int i=1;i<ac;i++) if(strcmp(str_data(as_str(av[i-1])),str_data(as_str(av[i])))>0) return V_FALSE; return V_TRUE;}
static val_t prim_string_gt(int ac, val_t *av, void *ud) {(void)ud; for(int i=1;i<ac;i++) if(strcmp(str_data(as_str(av[i-1])),str_data(as_str(av[i])))<=0) return V_FALSE; return V_TRUE;}
static val_t prim_string_ge(int ac, val_t *av, void *ud) {(void)ud; for(int i=1;i<ac;i++) if(strcmp(str_data(as_str(av[i-1])),str_data(as_str(av[i])))<0) return V_FALSE; return V_TRUE;}

/* Case-insensitive string comparisons (scheme char library) */
static int str_ci_cmp(const char *a, const char *b) {
    while (*a && *b) {
        int da = tolower((unsigned char)*a), db = tolower((unsigned char)*b);
        if (da != db) return da - db;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}
static val_t prim_string_ci_eq(int ac, val_t *av, void *ud) {(void)ud; for(int i=1;i<ac;i++) if(str_ci_cmp(str_data(as_str(av[i-1])),str_data(as_str(av[i])))!=0) return V_FALSE; return V_TRUE;}
static val_t prim_string_ci_lt(int ac, val_t *av, void *ud) {(void)ud; for(int i=1;i<ac;i++) if(str_ci_cmp(str_data(as_str(av[i-1])),str_data(as_str(av[i])))>=0) return V_FALSE; return V_TRUE;}
static val_t prim_string_ci_le(int ac, val_t *av, void *ud) {(void)ud; for(int i=1;i<ac;i++) if(str_ci_cmp(str_data(as_str(av[i-1])),str_data(as_str(av[i])))>0) return V_FALSE; return V_TRUE;}
static val_t prim_string_ci_gt(int ac, val_t *av, void *ud) {(void)ud; for(int i=1;i<ac;i++) if(str_ci_cmp(str_data(as_str(av[i-1])),str_data(as_str(av[i])))<=0) return V_FALSE; return V_TRUE;}
static val_t prim_string_ci_ge(int ac, val_t *av, void *ud) {(void)ud; for(int i=1;i<ac;i++) if(str_ci_cmp(str_data(as_str(av[i-1])),str_data(as_str(av[i])))<0) return V_FALSE; return V_TRUE;}

static val_t prim_string_upcase(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_string(av[0])) scm_raise(V_FALSE, "string-upcase: not a string");
    String *s = as_str(av[0]);
    val_t in = port_open_input_string(str_data(s), s->len), out = port_open_output_string();
    int cp; while ((cp = port_read_char(in)) != -1) port_write_char(out, toupper(cp));
    return port_get_output_string(out);
}
static val_t prim_string_downcase(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_string(av[0])) scm_raise(V_FALSE, "string-downcase: not a string");
    String *s = as_str(av[0]);
    val_t in = port_open_input_string(str_data(s), s->len), out = port_open_output_string();
    int cp; while ((cp = port_read_char(in)) != -1) port_write_char(out, tolower(cp));
    return port_get_output_string(out);
}

/* (string-set! str k char) — in-place when same UTF-8 byte width; reallocates ext otherwise */
static val_t prim_string_set_bang(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_string(av[0])) scm_raise(V_FALSE, "string-set!: not a string");
    if (!vis_fixnum(av[1])) scm_raise(V_FALSE, "string-set!: index must be exact integer");
    if (!vis_char(av[2]))   scm_raise(V_FALSE, "string-set!: not a character");
    String *s = as_str(av[0]);
    char *sd = str_data(s);
    intptr_t idx = vunfix(av[1]);
    /* Checked against 0 and the real character count explicitly, not just
     * cast to unsigned and handed to utf8_char_offset: a negative index
     * cast to uint32_t wraps to a huge offset, and an out-of-range index
     * (negative-wrapped or simply too large) both clamp through
     * utf8_char_offset to the end of the buffer instead of raising,
     * silently corrupting the string (splicing the new character in at
     * the wrong place) rather than erroring. */
    uint32_t nchars = 0;
    { const char *p = sd, *end = p + s->len; while (p < end) { if ((*p & 0xC0) != 0x80) nchars++; p++; } }
    if (idx < 0 || (uint32_t)idx >= nchars)
        scm_raise_code(EC_INDEX_OUT_OF_RANGE, "string-set!: index out of range");
    uint32_t k = (uint32_t)idx;
    uint32_t bstart = utf8_char_offset(sd, s->len, k);
    uint32_t bend   = utf8_char_offset(sd, s->len, k + 1);
    uint32_t old_blen = bend - bstart;
    uint32_t u = vunchr(av[2]); char enc[4]; int elen;
    if      (u < 0x80)    { enc[0]=(char)u; elen=1; }
    else if (u < 0x800)   { enc[0]=(char)(0xC0|(u>>6)); enc[1]=(char)(0x80|(u&0x3F)); elen=2; }
    else if (u < 0x10000) { enc[0]=(char)(0xE0|(u>>12)); enc[1]=(char)(0x80|((u>>6)&0x3F)); enc[2]=(char)(0x80|(u&0x3F)); elen=3; }
    else                  { enc[0]=(char)(0xF0|(u>>18)); enc[1]=(char)(0x80|((u>>12)&0x3F)); enc[2]=(char)(0x80|((u>>6)&0x3F)); enc[3]=(char)(0x80|(u&0x3F)); elen=4; }
    if ((uint32_t)elen == old_blen) {
        /* Same width: mutate in-place (works on both inline data[] and ext buffer). */
        memcpy(sd + bstart, enc, (size_t)elen);
    } else {
        /* Different width: allocate a new buffer, copy surrounding bytes, redirect. */
        uint32_t new_len = s->len - old_blen + (uint32_t)elen;
        char *new_buf = (char *)gc_alloc_raw_pinned_atomic(new_len + 1);
        memcpy(new_buf, sd, bstart);
        memcpy(new_buf + bstart, enc, (size_t)elen);
        memcpy(new_buf + bstart + elen, sd + bend, s->len - bend);
        new_buf[new_len] = '\0';
        s->ext = new_buf;
        s->len = new_len;
    }
    s->hash = 0;
    return V_VOID;
}

/* (string-copy! to at from [start [end]]) */
static val_t prim_string_copy_bang(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_string(av[0])) scm_raise(V_FALSE, "string-copy!: to not a string");
    if (!vis_fixnum(av[1])) scm_raise(V_FALSE, "string-copy!: at not exact integer");
    if (!vis_string(av[2])) scm_raise(V_FALSE, "string-copy!: from not a string");
    String *to    = as_str(av[0]);
    String *from  = as_str(av[2]);
    char       *tod   = str_data(to);
    const char *fromd = str_data(from);
    uint32_t sb, eb;
    string_range_to_bytes(fromd, from->len, ac, av, 3, "string-copy!", &sb, &eb);

    /* Count characters in the source slice to find the matching dest range. */
    uint32_t char_count = 0;
    for (uint32_t b = sb; b < eb; b++)
        if (((unsigned char)fromd[b] & 0xC0) != 0x80) char_count++;

    /* `at` (av[1]) validated as an intptr_t directly against to_chars,
     * never cast to uint32_t before that check: a negative index cast
     * straight to uint32_t used to wrap to a huge value that could slip
     * past an at_char+char_count>to_chars guard computed entirely in
     * uint32_t (e.g. wrapping back around to something small on overflow),
     * corrupting the destination instead of raising — and a huge but
     * positive fixnum index could similarly truncate to something small
     * on a plain (uint32_t) cast. Comparing the signed, uncast intptr_t
     * against to_chars (an actual character count, always small) avoids
     * both. */
    uint32_t to_chars = 0;
    for (uint32_t b = 0; b < to->len; b++)
        if (((unsigned char)tod[b] & 0xC0) != 0x80) to_chars++;
    intptr_t iat = vunfix(av[1]);
    /* Compared as intptr_t throughout, never narrowed to uint32_t before
     * the check: to_chars/char_count are uint32_t and always fit in
     * intptr_t (pointer-width, at least 64-bit here), but an oversized
     * fixnum index cast to uint32_t first could itself wrap to a small,
     * seemingly-valid value before ever reaching this comparison. */
    if (iat < 0 || iat > (intptr_t)to_chars || iat + (intptr_t)char_count > (intptr_t)to_chars)
        scm_raise_code(EC_INDEX_OUT_OF_RANGE, "string-copy!: at/length out of range");
    uint32_t at_char = (uint32_t)iat;

    uint32_t at_b     = utf8_char_offset(tod, to->len, at_char);
    uint32_t at_end_b = utf8_char_offset(tod, to->len, at_char + char_count);

    uint32_t src_bytes = eb - sb;
    uint32_t dst_bytes = at_end_b - at_b;
    if (src_bytes == dst_bytes) {
        memmove(tod + at_b, fromd + sb, src_bytes);
    } else {
        uint32_t new_len = to->len - dst_bytes + src_bytes;
        char *new_buf = (char *)gc_alloc_raw_pinned_atomic(new_len + 1);
        memcpy(new_buf, tod, at_b);
        memcpy(new_buf + at_b, fromd + sb, src_bytes);
        memcpy(new_buf + at_b + src_bytes, tod + at_end_b, to->len - at_end_b);
        new_buf[new_len] = '\0';
        to->ext = new_buf;
        to->len = new_len;
    }
    to->hash = 0;
    return V_VOID;
}

/* Bytevectors — missing constructors/operations */
static val_t prim_bytevector(int ac, val_t *av, void *ud) {
    (void)ud;
    Bytevector *b = CURRY_NEW_FLEX_ATOM(Bytevector, (uint32_t)ac);
    b->hdr.type = T_BYTEVECTOR; b->hdr.flags = 0; b->len = (uint32_t)ac;
    for (int i = 0; i < ac; i++) {
        if (!vis_fixnum(av[i])) scm_raise(V_FALSE, "bytevector: not an exact integer");
        b->data[i] = (uint8_t)vunfix(av[i]);
    }
    return vptr(b);
}
static val_t prim_bytes_copy(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_bytes(av[0])) scm_raise(V_FALSE, "bytevector-copy: not a bytevector");
    Bytevector *b = as_bytes(av[0]);
    uint32_t s, e;
    validate_index_range(ac, av, 1, b->len, "bytevector-copy", &s, &e);
    uint32_t n = e - s;
    Bytevector *r = CURRY_NEW_FLEX_ATOM(Bytevector, n);
    r->hdr.type = T_BYTEVECTOR; r->hdr.flags = 0; r->len = n;
    memcpy(r->data, b->data + s, n);
    return vptr(r);
}
static val_t prim_bytes_copy_bang(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_bytes(av[0])) scm_raise(V_FALSE, "bytevector-copy!: to not a bytevector");
    if (!vis_fixnum(av[1])) scm_raise(V_FALSE, "bytevector-copy!: at not exact integer");
    if (!vis_bytes(av[2])) scm_raise(V_FALSE, "bytevector-copy!: from not a bytevector");
    Bytevector *to   = as_bytes(av[0]);
    Bytevector *from = as_bytes(av[2]);
    uint32_t    s, e;
    validate_index_range(ac, av, 3, from->len, "bytevector-copy!", &s, &e);
    uint32_t n = e - s;
    intptr_t iat = vunfix(av[1]);
    if (iat < 0 || iat > (intptr_t)to->len || iat + (intptr_t)n > (intptr_t)to->len)
        scm_raise_code(EC_INDEX_OUT_OF_RANGE, "bytevector-copy!: at/length out of range");
    uint32_t at = (uint32_t)iat;
    memmove(to->data + at, from->data + s, n);
    return V_VOID;
}
static val_t prim_bytes_append(int ac, val_t *av, void *ud) {
    (void)ud;
    uint32_t total = 0;
    for (int i = 0; i < ac; i++) {
        if (!vis_bytes(av[i])) scm_raise(V_FALSE, "bytevector-append: not a bytevector");
        total += as_bytes(av[i])->len;
    }
    Bytevector *r = CURRY_NEW_FLEX_ATOM(Bytevector, total);
    r->hdr.type = T_BYTEVECTOR; r->hdr.flags = 0; r->len = total;
    uint32_t pos = 0;
    for (int i = 0; i < ac; i++) {
        Bytevector *b = as_bytes(av[i]);
        memcpy(r->data + pos, b->data, b->len);
        pos += b->len;
    }
    return vptr(r);
}

/* Vectors */
static val_t prim_vector_append(int ac, val_t *av, void *ud) {
    (void)ud;
    uint32_t total = 0;
    for (int i = 0; i < ac; i++) {
        if (!vis_vector(av[i])) scm_raise(V_FALSE, "vector-append: not a vector");
        total += as_vec(av[i])->len;
    }
    Vector *r = CURRY_NEW_FLEX(Vector, total);
    r->hdr.type = T_VECTOR; r->hdr.flags = 0; r->len = total;
    uint32_t pos = 0;
    for (int i = 0; i < ac; i++) {
        Vector *v = as_vec(av[i]);
        memcpy(r->data + pos, v->data, v->len * sizeof(val_t));
        pos += v->len;
    }
    return vptr(r);
}

/* Error predicates */
static val_t prim_read_error_p(int ac, val_t *av, void *ud)  {(void)ac;(void)ud; return vbool(vis_error(av[0]) && as_err(av[0])->kind == S_READ_ERROR);}
static val_t prim_file_error_p(int ac, val_t *av, void *ud)  {(void)ac;(void)ud; return vbool(vis_error(av[0]) && as_err(av[0])->kind == S_FILE_ERROR);}

/* I/O — binary byte operations */
static val_t prim_flush_output_port(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t port = ac > 0 ? av[0] : PORT_STDOUT;
    if (!vis_port(port)) scm_raise(V_FALSE, "flush-output-port: not a port");
    FILE *fp = as_port(port)->u.fp;
    if (fp) fflush(fp);
    return V_VOID;
}
static val_t prim_char_ready_p(int ac, val_t *av, void *ud) {(void)ud; return vbool(port_char_ready(ac>0?av[0]:PORT_STDIN));}
static val_t prim_u8_ready_p(int ac, val_t *av, void *ud)   {(void)ud; return vbool(port_char_ready(ac>0?av[0]:PORT_STDIN));}
static val_t prim_read_u8(int ac, val_t *av, void *ud)  {(void)ud; int b=port_read_byte(ac>0?av[0]:PORT_STDIN); return b<0?V_EOF:vfix((intptr_t)b);}
static val_t prim_peek_u8(int ac, val_t *av, void *ud)  {(void)ud; int b=port_peek_byte(ac>0?av[0]:PORT_STDIN); return b<0?V_EOF:vfix((intptr_t)b);}
static val_t prim_write_u8(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_fixnum(av[0])) scm_raise(V_FALSE, "write-u8: not an exact integer");
    port_write_byte(ac>1?av[1]:PORT_STDOUT, (uint8_t)vunfix(av[0]));
    return V_VOID;
}
static val_t prim_read_string(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_fixnum(av[0])) scm_raise(V_FALSE, "read-string: k must be exact integer");
    int k = (int)vunfix(av[0]);
    val_t port = ac > 1 ? av[1] : PORT_STDIN;
    val_t out = port_open_output_string();
    int count = 0, cp;
    while (count < k && (cp = port_read_char(port)) >= 0) { port_write_char(out, cp); count++; }
    return count == 0 ? V_EOF : port_get_output_string(out);
}
static val_t prim_read_bytevector(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_fixnum(av[0])) scm_raise(V_FALSE, "read-bytevector: k must be exact integer");
    intptr_t ik = vunfix(av[0]);
    /* Same guard as make-bytevector: a negative or oversized k cast
     * straight to (uint32_t) would otherwise attempt a multi-GB
     * allocation or wrap to a small in-range size, both silently. Capped
     * at INT_MAX (not UINT32_MAX) since k is used as a plain `int` below. */
    if (ik < 0 || ik > (intptr_t)INT_MAX)
        scm_raise_code(EC_INDEX_OUT_OF_RANGE, "read-bytevector: invalid size");
    int k = (int)ik;
    val_t port = ac > 1 ? av[1] : PORT_STDIN;
    Bytevector *bv = CURRY_NEW_FLEX_ATOM(Bytevector, (uint32_t)k);
    bv->hdr.type = T_BYTEVECTOR; bv->hdr.flags = 0; bv->len = 0;
    int b;
    while (bv->len < (uint32_t)k && (b = port_read_byte(port)) >= 0) bv->data[bv->len++] = (uint8_t)b;
    return bv->len == 0 ? V_EOF : vptr(bv);
}
static val_t prim_read_bytevector_bang(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_bytes(av[0])) scm_raise(V_FALSE, "read-bytevector!: not a bytevector");
    Bytevector *bv = as_bytes(av[0]);
    val_t port = ac > 1 ? av[1] : PORT_STDIN;
    uint32_t s, e;
    validate_index_range(ac, av, 2, bv->len, "read-bytevector!", &s, &e);
    int count = 0, b;
    for (uint32_t i = s; i < e && (b = port_read_byte(port)) >= 0; i++) { bv->data[i] = (uint8_t)b; count++; }
    return count == 0 ? V_EOF : vfix((intptr_t)count);
}
static val_t prim_write_bytevector(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_bytes(av[0])) scm_raise(V_FALSE, "write-bytevector: not a bytevector");
    Bytevector *bv = as_bytes(av[0]);
    val_t port = ac > 1 ? av[1] : PORT_STDOUT;
    uint32_t s, e;
    validate_index_range(ac, av, 2, bv->len, "write-bytevector", &s, &e);
    for (uint32_t i = s; i < e; i++) port_write_byte(port, bv->data[i]);
    return V_VOID;
}
static val_t prim_write_simple(int ac, val_t *av, void *ud) {(void)ud; scm_write(av[0], ac>1?av[1]:PORT_STDOUT); return V_VOID;}

/* File operations */
static val_t prim_delete_file(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_string(av[0])) scm_raise(V_FALSE, "delete-file: not a string");
    if (unlink(str_data(as_str(av[0]))) != 0)
        scm_raise(S_FILE_ERROR, "delete-file: %s: %s", str_data(as_str(av[0])), strerror(errno));
    return V_VOID;
}
static val_t prim_call_with_input_file(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_string(av[0])) scm_raise(V_FALSE, "call-with-input-file: not a string");
    val_t port = port_open_file(str_data(as_str(av[0])), PORT_INPUT);
    if (port == V_FALSE) scm_raise(S_FILE_ERROR, "call-with-input-file: cannot open '%s'", str_data(as_str(av[0])));
    val_t result = apply(av[1], scm_cons(port, V_NIL));
    port_close(port);
    return result;
}
static val_t prim_call_with_output_file(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_string(av[0])) scm_raise(V_FALSE, "call-with-output-file: not a string");
    val_t port = port_open_file(str_data(as_str(av[0])), PORT_OUTPUT);
    if (port == V_FALSE) scm_raise(S_FILE_ERROR, "call-with-output-file: cannot open '%s'", str_data(as_str(av[0])));
    val_t result = apply(av[1], scm_cons(port, V_NIL));
    port_close(port);
    return result;
}
static val_t prim_with_input_from_file(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_string(av[0])) scm_raise(V_FALSE, "with-input-from-file: not a string");
    val_t port = port_open_file(str_data(as_str(av[0])), PORT_INPUT);
    if (port == V_FALSE) scm_raise(S_FILE_ERROR, "with-input-from-file: cannot open '%s'", str_data(as_str(av[0])));
    val_t saved = PORT_STDIN; PORT_STDIN = port;
    ExnHandler h; h.prev = current_handler;
    h.saved_jit_depth = jit_depth_save();
    current_handler = &h;
    val_t result = V_VOID;
    if (setjmp(h.jmp) == 0) { result = apply(av[1], V_NIL); current_handler = h.prev; }
    else { current_handler = h.prev; jit_depth_restore(h.saved_jit_depth); PORT_STDIN = saved; port_close(port); scm_raise_val(h.exn); }
    PORT_STDIN = saved; port_close(port);
    return result;
}
static val_t prim_with_output_to_file(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_string(av[0])) scm_raise(V_FALSE, "with-output-to-file: not a string");
    val_t port = port_open_file(str_data(as_str(av[0])), PORT_OUTPUT);
    if (port == V_FALSE) scm_raise(S_FILE_ERROR, "with-output-to-file: cannot open '%s'", str_data(as_str(av[0])));
    val_t saved = PORT_STDOUT; PORT_STDOUT = port;
    ExnHandler h; h.prev = current_handler;
    h.saved_jit_depth = jit_depth_save();
    current_handler = &h;
    val_t result = V_VOID;
    if (setjmp(h.jmp) == 0) { result = apply(av[1], V_NIL); current_handler = h.prev; }
    else { current_handler = h.prev; jit_depth_restore(h.saved_jit_depth); PORT_STDOUT = saved; port_close(port); scm_raise_val(h.exn); }
    PORT_STDOUT = saved; port_close(port);
    return result;
}

/* Process context */
static val_t prim_get_env_var(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_string(av[0])) scm_raise(V_FALSE, "get-environment-variable: not a string");
    const char *val = getenv(str_data(as_str(av[0])));
    if (!val) return V_FALSE;
    uint32_t len = (uint32_t)strlen(val);
    String *s = (String *)gc_alloc_atomic(sizeof(String) + len + 1);
    s->hdr.type = T_STRING; s->hdr.flags = 0; s->len = len; s->hash = 0; s->orig_cap = len; s->ext = NULL;
    memcpy(s->data, val, len + 1);
    return vptr(s);
}
static val_t prim_get_env_vars(int ac, val_t *av, void *ud) {
    (void)ac;(void)av;(void)ud;
    extern char **environ;
    val_t result = V_NIL;
    for (char **e = environ; *e; e++) {
        char *eq = strchr(*e, '=');
        if (!eq) continue;
        uint32_t nlen = (uint32_t)(eq - *e);
        uint32_t vlen = (uint32_t)strlen(eq + 1);
        String *name  = (String *)gc_alloc_atomic(sizeof(String) + nlen + 1);
        name->hdr.type=T_STRING; name->hdr.flags=0; name->len=nlen; name->hash=0; name->orig_cap=nlen; name->ext=NULL;
        memcpy(name->data, *e, nlen); name->data[nlen] = '\0';
        String *value = (String *)gc_alloc_atomic(sizeof(String) + vlen + 1);
        value->hdr.type=T_STRING; value->hdr.flags=0; value->len=vlen; value->hash=0; value->orig_cap=vlen; value->ext=NULL;
        memcpy(value->data, eq + 1, vlen + 1);
        result = scm_cons(scm_cons(vptr(name), vptr(value)), result);
    }
    return scm_reverse(result);
}
static val_t prim_emergency_exit(int ac, val_t *av, void *ud) {
    (void)ud;
    int code = (ac > 0 && vis_fixnum(av[0])) ? (int)vunfix(av[0]) : (ac > 0 && av[0] == V_FALSE) ? 1 : 0;
    _Exit(code);
}

/* Time */
static val_t prim_current_second(int ac, val_t *av, void *ud) {
    (void)ac;(void)av;(void)ud;
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    return num_make_float((double)ts.tv_sec + (double)ts.tv_nsec * 1e-9);
}
static val_t prim_current_jiffy(int ac, val_t *av, void *ud) {
    (void)ac;(void)av;(void)ud;
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return vfix((intptr_t)ts.tv_sec * 1000000000LL + (intptr_t)ts.tv_nsec);
}
static val_t prim_jiffies_per_second(int ac, val_t *av, void *ud) {
    (void)ac;(void)av;(void)ud; return vfix(1000000000LL);
}
static val_t prim_file_exists_p(int ac, val_t *av, void *ud) {(void)ac;(void)ud;
    if (!vis_string(av[0])) scm_raise(V_FALSE, "file-exists?: not a string");
    return vbool(access(str_data(as_str(av[0])), F_OK) == 0);
}

static val_t prim_close_port(int ac, val_t *av, void *ud) {(void)ac;(void)ud; port_close(av[0]); return V_VOID;}
static val_t prim_input_port_p(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vbool(vis_port(av[0])&&port_is_input(av[0]));}
static val_t prim_input_port_open_p(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vbool(vis_port(av[0])&&port_is_input(av[0])&&!(as_port(av[0])->flags&PORT_CLOSED));}
static val_t prim_output_port_open_p(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vbool(vis_port(av[0])&&port_is_output(av[0])&&!(as_port(av[0])->flags&PORT_CLOSED));}
static val_t prim_output_port_p(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vbool(vis_port(av[0])&&port_is_output(av[0]));}
static val_t prim_current_input_port(int ac, val_t *av, void *ud) {(void)ac;(void)av;(void)ud; return PORT_STDIN;}
static val_t prim_current_output_port(int ac, val_t *av, void *ud) {(void)ac;(void)av;(void)ud; return PORT_STDOUT;}
static val_t prim_current_error_port(int ac, val_t *av, void *ud) {(void)ac;(void)av;(void)ud; return PORT_STDERR;}
static val_t prim_with_output_to_string(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    val_t port = port_open_output_string();
    val_t saved = PORT_STDOUT;
    PORT_STDOUT = port;
    ExnHandler h;
    h.prev = current_handler;
    h.saved_jit_depth = jit_depth_save();
    current_handler = &h;
    if (setjmp(h.jmp) == 0) {
        apply(av[0], V_NIL);
        current_handler = h.prev;
    } else {
        current_handler = h.prev;
        jit_depth_restore(h.saved_jit_depth);
        PORT_STDOUT = saved;
        scm_raise_val(h.exn);
    }
    PORT_STDOUT = saved;
    return port_get_output_string(port);
}

static val_t prim_system(int ac, val_t *av, void *ud) {(void)ac;(void)ud;
    if (!vis_string(av[0])) scm_raise(V_FALSE, "system: not a string");
    int status = system(str_data(as_str(av[0])));
    if (status == -1)
        scm_raise(V_FALSE, "system: failed to run shell");
    /* system(3) returns a wait-status, not a plain exit code — decode it
     * rather than handing the raw packed value back. Negative return means
     * the command was killed by signal N (mirrors Python's subprocess
     * convention), so callers only need to learn one rule. */
    if (WIFEXITED(status))   return vfix(WEXITSTATUS(status));
    if (WIFSIGNALED(status)) return vfix(-WTERMSIG(status));
    return vfix(status);
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
static val_t prim_make_list(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_fixnum(av[0])) scm_raise(V_FALSE, "make-list: not an exact integer");
    intptr_t k = vunfix(av[0]);
    if (k < 0) scm_raise(V_FALSE, "make-list: negative length");
    val_t fill = ac > 1 ? av[1] : V_FALSE;
    val_t r = V_NIL;
    while (k-- > 0) r = scm_cons(fill, r);
    return r;
}
static val_t prim_list_head(int ac, val_t *av, void *ud) {
    (void)ud;(void)ac;
    val_t lst=av[0]; intptr_t n=vunfix(av[1]); val_t r=V_NIL;
    while(n-->0 && vis_pair(lst)){ r=scm_cons(vcar(lst),r); lst=vcdr(lst); }
    return scm_reverse(r);
}
static val_t prim_filter(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud; val_t pred=av[0], lst=av[1], r=V_NIL;
    while(vis_pair(lst)) { if(vis_true(apply(pred,scm_cons(vcar(lst),V_NIL)))) r=scm_cons(vcar(lst),r); lst=vcdr(lst); }
    return scm_reverse(r);
}
static val_t prim_fold(int ac, val_t *av, void *ud) {
    /* fold-left: (proc acc element) — R6RS / standard left-fold convention */
    (void)ac;(void)ud; val_t proc=av[0], init=av[1], lst=av[2];
    while(vis_pair(lst)) { init=apply(proc,scm_cons(init,scm_cons(vcar(lst),V_NIL))); lst=vcdr(lst); }
    return init;
}
static val_t prim_fold_right(int ac, val_t *av, void *ud) {
    /* fold-right: (proc element acc) — recurse to the end, then apply on the way back */
    (void)ud; val_t proc=av[0], init=av[1], lst=av[2];
    if (!vis_pair(lst)) return init;
    val_t rest = prim_fold_right(ac, (val_t[]){proc, init, vcdr(lst)}, ud);
    return apply(proc, scm_cons(vcar(lst), scm_cons(rest, V_NIL)));
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
            else { gc_wb_slot(&p->val, q->val); return apply(p->val, V_NIL); }
        }
    }
    gc_wb_slot(&p->val, r);
    p->state = PROMISE_FORCED;
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
    /* av[0] is the message string; remaining args are irritants */
    val_t msg = av[0];
    if (!vis_string(msg)) {
        val_t out = port_open_output_string();
        scm_write(av[0], out);
        msg = port_get_output_string(out);
    }
    val_t irritants = V_NIL;
    for (int i = ac - 1; i >= 1; i--)
        irritants = scm_cons(av[i], irritants);
    ErrorObj *e = CURRY_NEW(ErrorObj);
    e->hdr.type=T_ERROR; e->hdr.flags=0;
    e->message=msg; e->irritants=irritants; e->kind=S_ERROR;
    e->backtrace = vm_capture_backtrace();
    e->code = V_FALSE; /* (error ...) is user-authored text, not a stable code site */
    scm_raise_val(vptr(e));
}
static val_t prim_eval(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t env = (ac >= 2 && vis_env(av[1])) ? av[1] : GLOBAL_ENV;
    return eval(av[0], env);
}
static val_t prim_interaction_env(int ac, val_t *av, void *ud) { (void)ac;(void)av;(void)ud; return GLOBAL_ENV; }
static val_t prim_error_message(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return as_err(av[0])->message;}
static val_t prim_error_object_p(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vbool(vis_error(av[0]));}
static val_t prim_error_irritants(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return as_err(av[0])->irritants;}
static val_t prim_error_code(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return as_err(av[0])->code;}
static val_t prim_raise(int ac, val_t *av, void *ud) {(void)ac;(void)ud; scm_raise_val(av[0]);}
static val_t prim_raise_continuable(int ac, val_t *av, void *ud) {(void)ac;(void)ud; scm_raise_val(av[0]);}
static val_t prim_error_to_string(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if(vis_error(av[0])) return as_err(av[0])->message;
    val_t p=port_open_output_string(); scm_write(av[0],p); return port_get_output_string(p);
}
/* noinline: the setjmp jmp_buf must stay on this function's stack frame.
 * Avoid a volatile-ret pattern: clang (ARM64 -O2) replaces `ret = cont->result`
 * with V_VOID after longjmp even with volatile, because cont->result was
 * V_VOID at setjmp time and the optimizer caches it.  Instead use an early-
 * return structure: if setjmp returns non-zero the function returns cont->result
 * directly; otherwise falls through to apply_arr and returns its result.
 * All variables read in the longjmp path (cont, saved_*) are in callee-saved
 * registers and are therefore valid after longjmp restores the register file. */
__attribute__((noinline))
static val_t prim_call_cc(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t proc = av[0];
    Continuation *cont = CURRY_NEW_PINNED(Continuation);
    cont->hdr.type = T_CONTINUATION; cont->hdr.flags = 0;
    cont->jmpbuf   = gc_alloc_raw_pinned(sizeof(jmp_buf));
    cont->result   = V_VOID;
    cont->wind_top = current_wind;
    int saved_fc   = vm->frame_count;
    val_t *saved_sp = vm->sp;
    Upvalue *saved_uv = vm->open_upvalues;
    if (setjmp(*(jmp_buf *)cont->jmpbuf) != 0) {
        /* Continuation was invoked — restore VM state and return captured value.
         * Volatile cast: clang (ARM64 -O2) folds cont->result to V_VOID without
         * it, because cont->result was V_VOID at setjmp time.  See eval_call_cc
         * comment for the full explanation. */
        vm->frame_count   = saved_fc;
        vm->sp            = saved_sp;
        vm->open_upvalues = saved_uv;
        return *(volatile val_t *)&cont->result;
    }
    val_t cont_val = vptr(cont);
    return apply_arr(proc, 1, &cont_val);
}

static val_t prim_with_exception_handler(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud; val_t handler=av[0], thunk=av[1];
    val_t result = V_VOID;
    /* Save VM state — a longjmp from inside the thunk may skip vm_run cleanup */
    int saved_frame_count = vm->frame_count;
    val_t *saved_sp       = vm->sp;
    Upvalue *saved_upvals = vm->open_upvalues;
    ExnHandler h;
    h.prev = current_handler;
    h.saved_jit_depth = jit_depth_save();
    current_handler = &h;
    if (setjmp(h.jmp) == 0) {
        result = apply(thunk, V_NIL);
        current_handler = h.prev;
    } else {
        current_handler = h.prev;
        jit_depth_restore(h.saved_jit_depth);
        vm->frame_count   = saved_frame_count;
        vm->sp            = saved_sp;
        vm->open_upvalues = saved_upvals;
        result = apply(handler, scm_cons(h.exn, V_NIL));
    }
    return result;
}

static val_t prim_dynamic_wind(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t before = av[0], thunk = av[1], after = av[2];

    apply(before, V_NIL);

    /* GC-heap allocation: longjmp cannot invalidate a stack frame we no
     * longer own, so WindFrame must outlive any potential escape longjmp. */
    WindFrame *wf = gc_alloc_raw_pinned(sizeof(WindFrame));
    wf->before = before;
    wf->after  = after;
    wf->prev   = current_wind;
    current_wind = wf;

    val_t result = V_VOID;
    ExnHandler h;
    h.prev = current_handler;
    h.saved_jit_depth = jit_depth_save();
    current_handler = &h;
    bool raised = false;
    val_t exn_val = V_VOID;
    if (setjmp(h.jmp) == 0) {
        result = apply(thunk, V_NIL);
        current_handler = h.prev;
    } else {
        current_handler = h.prev;
        jit_depth_restore(h.saved_jit_depth);
        raised = true; exn_val = h.exn;
    }

    current_wind = wf->prev;
    apply(after, V_NIL);

    if (raised) scm_raise_val(exn_val);
    return result;
}

static val_t prim_call_with_port(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t port = av[0], proc = av[1];
    val_t result = V_VOID;
    ExnHandler h;
    h.prev = current_handler;
    h.saved_jit_depth = jit_depth_save();
    current_handler = &h;
    bool raised = false; val_t exn_val = V_VOID;
    if (setjmp(h.jmp) == 0) {
        result = apply(proc, scm_cons(port, V_NIL));
        current_handler = h.prev;
    } else {
        current_handler = h.prev;
        jit_depth_restore(h.saved_jit_depth);
        raised = true; exn_val = h.exn;
    }
    port_close(port);
    if (raised) scm_raise_val(exn_val);
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
static val_t prim_set_equal(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vbool(set_equal(av[0],av[1]));}
static val_t prim_set_empty(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return vbool(set_size(av[0])==0);}
static val_t prim_set_copy(int ac, val_t *av, void *ud)  {(void)ac;(void)ud; return set_copy(av[0]);}
static val_t prim_set_sym_diff(int ac, val_t *av, void *ud) {(void)ac;(void)ud; return set_sym_diff(av[0],av[1]);}

static val_t prim_set_adjoin(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t r = set_copy(av[0]);
    for (int i = 1; i < ac; i++) set_add_mut(r, av[i]);
    return r;
}
static val_t prim_set_adjoin_mut(int ac, val_t *av, void *ud) {
    (void)ud;
    for (int i = 1; i < ac; i++) set_add_mut(av[0], av[i]);
    return V_VOID;
}
static val_t prim_set_delete_one(int ac, val_t *av, void *ud) {
    /* Non-destructive single-element delete */
    (void)ac;(void)ud;
    val_t r = set_copy(av[0]);
    set_delete_mut(r, av[1]);
    return r;
}
static val_t prim_set_for_each(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    val_t proc = av[0], lst = set_to_list(av[1]);
    while (vis_pair(lst)) { apply(proc, scm_cons(vcar(lst), V_NIL)); lst = vcdr(lst); }
    return V_VOID;
}
static val_t prim_set_map(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    val_t proc = av[0], sv = av[1];
    val_t r = set_make(as_set(sv)->cmp);
    val_t lst = set_to_list(sv);
    while (vis_pair(lst)) {
        set_add_mut(r, apply(proc, scm_cons(vcar(lst), V_NIL)));
        lst = vcdr(lst);
    }
    return r;
}
static val_t prim_set_filter(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    val_t pred = av[0], sv = av[1];
    val_t r = set_make(as_set(sv)->cmp);
    val_t lst = set_to_list(sv);
    while (vis_pair(lst)) {
        val_t e = vcar(lst);
        if (vis_true(apply(pred, scm_cons(e, V_NIL)))) set_add_mut(r, e);
        lst = vcdr(lst);
    }
    return r;
}
static val_t prim_set_filter_mut(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    val_t pred = av[0], sv = av[1];
    val_t lst = set_to_list(sv);  /* snapshot before mutating */
    while (vis_pair(lst)) {
        val_t e = vcar(lst);
        if (!vis_true(apply(pred, scm_cons(e, V_NIL)))) set_delete_mut(sv, e);
        lst = vcdr(lst);
    }
    return V_VOID;
}
static val_t prim_set_fold(int ac, val_t *av, void *ud) {
    /* (set-fold proc init set) — (proc acc elem) */
    (void)ac;(void)ud;
    val_t proc = av[0], acc = av[1], lst = set_to_list(av[2]);
    while (vis_pair(lst)) {
        acc = apply(proc, scm_cons(acc, scm_cons(vcar(lst), V_NIL)));
        lst = vcdr(lst);
    }
    return acc;
}
static val_t prim_set_any(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    val_t pred = av[0], lst = set_to_list(av[1]);
    while (vis_pair(lst)) {
        if (vis_true(apply(pred, scm_cons(vcar(lst), V_NIL)))) return V_TRUE;
        lst = vcdr(lst);
    }
    return V_FALSE;
}
static val_t prim_set_every(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    val_t pred = av[0], lst = set_to_list(av[1]);
    while (vis_pair(lst)) {
        if (!vis_true(apply(pred, scm_cons(vcar(lst), V_NIL)))) return V_FALSE;
        lst = vcdr(lst);
    }
    return V_TRUE;
}
static val_t prim_set_count(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    val_t pred = av[0], lst = set_to_list(av[1]);
    intptr_t n = 0;
    while (vis_pair(lst)) {
        if (vis_true(apply(pred, scm_cons(vcar(lst), V_NIL)))) n++;
        lst = vcdr(lst);
    }
    return vfix(n);
}
static val_t prim_set_find(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t pred = av[0], lst = set_to_list(av[1]);
    val_t def  = ac > 2 ? av[2] : V_FALSE;
    while (vis_pair(lst)) {
        val_t e = vcar(lst);
        if (vis_true(apply(pred, scm_cons(e, V_NIL)))) return e;
        lst = vcdr(lst);
    }
    return def;
}

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

static val_t prim_actor_stats(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    return actor_stats(av[0]);
}

static val_t prim_actor_set_name(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_actor(av[0])) return V_VOID;
    as_actor(av[0])->name = av[1];
    return V_VOID;
}

static val_t prim_actor_id(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    return vfix((intptr_t)actor_id(av[0]));
}

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
/* ---- Record types (internal primitives) ----
 * These are ordinary discoverable globals — nothing stops user code from
 * calling them directly, bypassing the generated constructor/predicate/
 * accessor/mutator closures define-record-type normally hands out (same
 * class of hazard already found and fixed for %make-record-type and
 * %rebuild-syntax-rules). Validate types and field-index bounds so direct
 * misuse raises an ordinary Scheme error instead of type-confusing a
 * non-RTD/non-Record value or indexing past the field array.
 *
 * Field mutability (immutable vs. mutable, per R7RS record-type syntax) is
 * NOT tracked here: RecordType (object.h) has no per-field mutability
 * bitmask, only the field count/names, so %record-set! cannot itself tell
 * an immutable field from a mutable one — that contract is enforced one
 * layer up, by define-record-type simply never generating/exposing a
 * mutator closure for an immutable field (record_type.c). Calling
 * %record-set! directly on such a field bypasses that (a real gap, not
 * addressed by this fix, since encoding mutability into RecordType would
 * touch its .scc serialization format — out of scope for a bounds/type
 * safety fix). */
static val_t prim_record_ctor(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_rtd(av[0])) scm_raise_code(EC_WRONG_TYPE_ARGUMENT, "%%record-ctor: not a record type");
    RecordType *rtd = vunptr(RecordType, av[0]);
    uint32_t n = rtd->nfields;
    Record *r = (Record *)gc_alloc(sizeof(Record) + n * sizeof(val_t));
    r->hdr.type=T_RECORD; r->hdr.flags=0;
    r->rtd = rtd;
    for (uint32_t i=0; i<n && (int)(i+1)<ac; i++) r->fields[i] = av[i+1];
    return vptr(r);
}
static val_t prim_record_pred(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_rtd(av[0])) scm_raise_code(EC_WRONG_TYPE_ARGUMENT, "%%record-pred?: not a record type");
    RecordType *rtd=vunptr(RecordType,av[0]);
    return vbool(vis_record(av[1]) && as_rec(av[1])->rtd == rtd);
}
static val_t prim_record_ref(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_record(av[0])) scm_raise_code(EC_WRONG_TYPE_ARGUMENT, "%%record-ref: not a record");
    if (!vis_fixnum(av[1])) scm_raise_code(EC_WRONG_TYPE_ARGUMENT, "%%record-ref: not an exact integer");
    Record *r = as_rec(av[0]);
    intptr_t i = vunfix(av[1]);
    if (i < 0 || (uint32_t)i >= r->rtd->nfields)
        scm_raise_code(EC_INDEX_OUT_OF_RANGE, "%%record-ref: field index %ld out of bounds (nfields %u)", (long)i, r->rtd->nfields);
    return r->fields[i];
}
static val_t prim_record_set(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_record(av[0])) scm_raise_code(EC_WRONG_TYPE_ARGUMENT, "%%record-set!: not a record");
    if (!vis_fixnum(av[1])) scm_raise_code(EC_WRONG_TYPE_ARGUMENT, "%%record-set!: not an exact integer");
    Record *r = as_rec(av[0]);
    intptr_t i = vunfix(av[1]);
    if (i < 0 || (uint32_t)i >= r->rtd->nfields)
        scm_raise_code(EC_INDEX_OUT_OF_RANGE, "%%record-set!: field index %ld out of bounds (nfields %u)", (long)i, r->rtd->nfields);
    r->fields[i] = av[2];
    return V_VOID;
}
/* Build a fresh RecordType (RTD) from a name and a list of field-name
 * symbols. Used by the compiler's define-record-type codegen to
 * reconstruct the RTD at runtime — see compile_define_record_type in
 * compiler.c — rather than ever embedding a RecordType* as a bytecode
 * constant, which cannot survive .scc serialization with identity
 * preserved across the constructor/predicate/accessor/mutator closures
 * that all need to agree it's the same type. */
static val_t prim_make_record_type(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t name = av[0];
    int len = scm_list_length(av[1]);
    /* An ordinary, addressable global primitive — nothing stops user code
     * from calling it directly (matching a real bug already found and
     * fixed for %rebuild-syntax-rules): scm_list_length returns -1 for a
     * non-list/improper-list argument, and casting that to uint32_t before
     * using it as an allocation size and a vcar/vcdr loop bound would walk
     * off the end of an arbitrary value (or attempt a ~4-billion-entry
     * allocation) instead of raising a normal Scheme error. */
    if (len < 0)
        scm_raise(V_FALSE, "%%make-record-type: field names must be a proper list");
    uint32_t nfields = (uint32_t)len;
    RecordType *rtd = (RecordType *)gc_alloc_pinned(
        sizeof(RecordType) + nfields * sizeof(val_t));
    rtd->hdr.type = T_RECORD_TYPE; rtd->hdr.flags = 0;
    rtd->name = name; rtd->nfields = nfields;
    val_t f = av[1];
    for (uint32_t i = 0; i < nfields; i++) { rtd->field_names[i] = vcar(f); f = vcdr(f); }
    return vptr(rtd);
}

/* ---- Misc ---- */
static val_t prim_gensym(int ac, val_t *av, void *ud) {
    (void)ud; static int counter = 0;
    char buf[32];
    const char *pfx = (ac>0 && vis_string(av[0])) ? str_data(as_str(av[0])) : "g";
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
static val_t prim_call_with_values(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t produced = apply_arr(av[0], 0, NULL);
    if (vis_values(produced)) {
        Values *mv = as_vals(produced);
        return apply_arr(av[1], (int)mv->count, mv->vals);
    }
    return apply_arr(av[1], 1, &produced);
}
static val_t prim_void(int ac, val_t *av, void *ud) {(void)ac;(void)av;(void)ud; return V_VOID;}
static val_t prim_eof_object(int ac, val_t *av, void *ud) {(void)ac;(void)av;(void)ud; return V_EOF;}
static val_t prim_boolean_eq(int ac, val_t *av, void *ud) {(void)ud; for(int i=1;i<ac;i++) if(av[i-1]!=av[i]) return V_FALSE; return V_TRUE;}
static val_t prim_load(int ac, val_t *av, void *ud) {(void)ac;(void)ud; if (!vis_string(av[0])) scm_raise(V_FALSE, "load: not a string"); return scm_load(str_data(as_str(av[0])), GLOBAL_ENV);}
static val_t prim_exit(int ac, val_t *av, void *ud) {(void)ud; exit(ac>0 ? (int)vunfix(av[0]) : 0);}

/* (breakpoint) — drop into the interactive debugger at the next VM
 * instruction after this call returns. */
static val_t prim_breakpoint(int ac, val_t *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    vm_debug_request_step();
    return V_VOID;
}
static val_t prim_gc(int ac, val_t *av, void *ud) {(void)ac;(void)av;(void)ud; gc_collect(); return V_VOID;}
static val_t prim_gc_mode(int ac, val_t *av, void *ud) {
    (void)ac;(void)av;(void)ud;
    extern gc_ops_t gc_gen_ops;
    extern gc_ops_t *gc_ops;
    return sym_intern_cstr(gc_ops == &gc_gen_ops ? "generational" : "boehm");
}
static val_t prim_gc_heap_size(int ac, val_t *av, void *ud)  {(void)ac;(void)av;(void)ud; return vfix((intptr_t)gc_heap_size());}
static val_t prim_gc_free_bytes(int ac, val_t *av, void *ud) {(void)ac;(void)av;(void)ud; return vfix((intptr_t)gc_free_bytes());}
static val_t prim_gc_total_bytes(int ac, val_t *av, void *ud){(void)ac;(void)av;(void)ud; return vfix((intptr_t)gc_total_bytes());}
static val_t prim_gc_enable_incremental(int ac, val_t *av, void *ud) {
    (void)ac;(void)av;(void)ud; gc_enable_incremental(); return V_VOID;
}
static val_t prim_gc_set_fsd(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_fixnum(av[0]) || vunfix(av[0]) < 1)
        scm_raise(V_FALSE, "gc-set-free-space-divisor!: expected positive integer");
    gc_set_free_space_divisor((int)vunfix(av[0]));
    return V_VOID;
}
static val_t prim_gc_set_max_heap(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_fixnum(av[0]) || vunfix(av[0]) < 0)
        scm_raise(V_FALSE, "gc-set-max-heap!: expected non-negative integer (bytes); 0 = unlimited");
    gc_set_max_heap((size_t)vunfix(av[0]));
    return V_VOID;
}
/* (gc-stats) → alist of GC counters and the pause ring as a vector of fixnums.
 * Works under both Boehm (minor-count always 0) and generational backends. */
static val_t prim_gc_stats(int ac, val_t *av, void *ud) {
    (void)ac;(void)av;(void)ud;

    /* Pause ring: copy into a Scheme vector */
    uint64_t ring_buf[GC_PAUSE_RING_N];
    size_t   ring_n = gc_get_pause_ring(ring_buf);
    Vector  *rv = CURRY_NEW_FLEX(Vector, (uint32_t)ring_n);
    rv->hdr.type = T_VECTOR; rv->hdr.flags = 0; rv->len = (uint32_t)ring_n;
    for (size_t i = 0; i < ring_n; i++)
        rv->data[i] = vfix((intptr_t)ring_buf[i]);
    val_t ring_val = vptr(rv);

    /* Nursery used (main thread) */
    size_t nursery_used = (size_t)(gc_nursery.top - gc_nursery.base);

    /* Build alist front-to-back (will be reversed — canonical key order) */
#define APAIR(k, v) result = scm_cons(scm_cons(sym_intern_cstr(k), (v)), result)
    val_t result = V_NIL;
    APAIR("nursery-used",     vfix((intptr_t)nursery_used));
    APAIR("free-bytes",       vfix((intptr_t)gc_free_bytes()));
    APAIR("heap-size",        vfix((intptr_t)gc_heap_size()));
    APAIR("pause-ring-us",    ring_val);
    APAIR("minor-max-us",     vfix((intptr_t)atomic_load_explicit(&gc_stat_minor_max_us,   memory_order_relaxed)));
    APAIR("minor-total-us",   vfix((intptr_t)atomic_load_explicit(&gc_stat_minor_total_us, memory_order_relaxed)));
    APAIR("major-count",      vfix((intptr_t)atomic_load_explicit(&gc_stat_major_count,    memory_order_relaxed)));
    APAIR("minor-count",      vfix((intptr_t)atomic_load_explicit(&gc_stat_minor_count,    memory_order_relaxed)));
#undef APAIR
    return result;
}

static val_t prim_gc_stats_reset(int ac, val_t *av, void *ud) {
    (void)ac;(void)av;(void)ud;
    atomic_store_explicit(&gc_stat_minor_count,    0, memory_order_relaxed);
    atomic_store_explicit(&gc_stat_major_count,    0, memory_order_relaxed);
    atomic_store_explicit(&gc_stat_minor_total_us, 0, memory_order_relaxed);
    atomic_store_explicit(&gc_stat_minor_max_us,   0, memory_order_relaxed);
    gc_reset_pause_ring();
    return V_VOID;
}

static val_t prim_profiling_report(int ac, val_t *av, void *ud) {(void)ac;(void)av;(void)ud; return profiling_report();}
static val_t prim_profiling_reset(int ac, val_t *av, void *ud)  {(void)ac;(void)av;(void)ud; profiling_reset(); return V_VOID;}
static val_t prim_floor_div(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    val_t q=prim_floor_quotient(ac,av,ud), r2=num_sub(av[0],num_mul(q,av[1]));
    Values *mv=(Values *)gc_alloc(sizeof(Values)+2*sizeof(val_t));
    mv->hdr.type=T_VALUES; mv->hdr.flags=0; mv->count=2; mv->vals[0]=q; mv->vals[1]=r2;
    return vptr(mv);
}

#ifdef BUILD_MPFR
/* ============================================================ */
/* MPFR primitives                                              */
/* ============================================================ */

static mpfr_prec_t opt_prec_arg(int ac, val_t *av, int idx) {
    if (ac > idx) {
        if (!vis_fixnum(av[idx])) scm_raise(V_FALSE, "precision must be a fixnum");
        long p = (long)vunfix(av[idx]);
        if (p < MPFR_PREC_MIN || p > MPFR_PREC_MAX)
            scm_raise(V_FALSE, "precision out of range");
        return (mpfr_prec_t)p;
    }
    return 0;
}

static val_t prim_mpfr(int ac, val_t *av, void *ud) {
    (void)ud;
    mpfr_prec_t p = opt_prec_arg(ac, av, 1);
    return mpfr_coerce(av[0], p);
}

static val_t prim_mpfr_p(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud; return vbool(vis_mpfr(av[0]));
}

static val_t prim_mpfr_prec(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_mpfr(av[0])) scm_raise(V_FALSE, "mpfr-precision: not an mpfr");
    return num_make_bignum_i((long)mpfr_precision(av[0]));
}

static val_t prim_mpfr_set_prec(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_fixnum(av[1])) scm_raise(V_FALSE, "mpfr-set-precision: precision must be a fixnum");
    return mpfr_set_precision(av[0], (mpfr_prec_t)vunfix(av[1]));
}

/* Constants — variadic optional-precision form */
#define MPFR_CONST_PRIM(NAME, FN)                                              \
static val_t NAME(int ac, val_t *av, void *ud) {                               \
    (void)ud;                                                                  \
    return FN(opt_prec_arg(ac, av, 0));                                        \
}
MPFR_CONST_PRIM(prim_mpfr_pi,      mpfr_c_pi)
MPFR_CONST_PRIM(prim_mpfr_e,       mpfr_c_e)
MPFR_CONST_PRIM(prim_mpfr_phi,     mpfr_c_phi)
MPFR_CONST_PRIM(prim_mpfr_log2c,   mpfr_c_log2)
MPFR_CONST_PRIM(prim_mpfr_euler,   mpfr_c_euler)
MPFR_CONST_PRIM(prim_mpfr_catalan, mpfr_c_catalan)
MPFR_CONST_PRIM(prim_mpfr_apery,   mpfr_c_apery)

/* Dynamic precision via call-with-precision. */
static val_t prim_call_with_precision(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_fixnum(av[0])) scm_raise(V_FALSE, "call-with-precision: precision must be a fixnum");
    long p = (long)vunfix(av[0]);
    if (p < MPFR_PREC_MIN || p > MPFR_PREC_MAX)
        scm_raise(V_FALSE, "call-with-precision: precision out of range");
    mpfr_prec_t saved = tl_mpfr_prec;
    tl_mpfr_prec = (mpfr_prec_t)p;
    val_t result = V_VOID;
    /* Reset the precision even on exceptional exit. */
    ExnHandler h;
    SCM_PROTECT(h,
        result = apply_arr(av[1], 0, NULL),
        { tl_mpfr_prec = saved; scm_raise_val(h.exn); });
    tl_mpfr_prec = saved;
    return result;
}

static val_t prim_current_precision(int ac, val_t *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    return num_make_bignum_i((long)mpfr_ctx_prec());
}

static val_t prim_mpfr_rnd_mode(int ac, val_t *av, void *ud) {
    (void)ud;
    if (ac == 0) {
        const char *s;
        switch (tl_mpfr_rnd) {
            case MPFR_RNDN: s = "rndn"; break;
            case MPFR_RNDZ: s = "rndz"; break;
            case MPFR_RNDU: s = "rndu"; break;
            case MPFR_RNDD: s = "rndd"; break;
            case MPFR_RNDA: s = "rnda"; break;
            default: s = "rndn"; break;
        }
        return sym_intern_cstr(s);
    }
    if (!vis_symbol(av[0])) scm_raise(V_FALSE, "mpfr-rounding-mode: expected symbol");
    const char *n = as_sym(av[0])->data;
    if      (strcmp(n, "rndn") == 0) tl_mpfr_rnd = MPFR_RNDN;
    else if (strcmp(n, "rndz") == 0) tl_mpfr_rnd = MPFR_RNDZ;
    else if (strcmp(n, "rndu") == 0) tl_mpfr_rnd = MPFR_RNDU;
    else if (strcmp(n, "rndd") == 0) tl_mpfr_rnd = MPFR_RNDD;
    else if (strcmp(n, "rnda") == 0) tl_mpfr_rnd = MPFR_RNDA;
    else scm_raise(V_FALSE, "mpfr-rounding-mode: unknown mode");
    return V_VOID;
}

/* MPFR-specific transcendentals */
#define MPFR_UNARY_PRIM(NAME, FN)                                              \
static val_t NAME(int ac, val_t *av, void *ud) {                               \
    (void)ac; (void)ud; return FN(av[0]);                                      \
}
MPFR_UNARY_PRIM(prim_mpfr_gamma,  mpfr_num_gamma)
MPFR_UNARY_PRIM(prim_mpfr_lgamma, mpfr_num_lgamma)
MPFR_UNARY_PRIM(prim_mpfr_zeta,   mpfr_num_zeta)
MPFR_UNARY_PRIM(prim_mpfr_erf,    mpfr_num_erf)
MPFR_UNARY_PRIM(prim_mpfr_erfc,   mpfr_num_erfc)
MPFR_UNARY_PRIM(prim_mpfr_j0,     mpfr_num_j0)
MPFR_UNARY_PRIM(prim_mpfr_j1,     mpfr_num_j1)
MPFR_UNARY_PRIM(prim_mpfr_log2,   mpfr_num_log2)
MPFR_UNARY_PRIM(prim_mpfr_log10,  mpfr_num_log10)
MPFR_UNARY_PRIM(prim_mpfr_sqrt,   mpfr_num_sqrt)
MPFR_UNARY_PRIM(prim_mpfr_exp,    mpfr_num_exp)
MPFR_UNARY_PRIM(prim_mpfr_log,    mpfr_num_log)

static val_t prim_mpfr_hypot(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud; return mpfr_num_hypot(av[0], av[1]);
}
static val_t prim_mpfr_fma(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud; return mpfr_num_fma(av[0], av[1], av[2]);
}

/* Interval arithmetic */
static val_t prim_make_interval(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud; return interval_make(av[0], av[1]);
}
static val_t prim_interval_point(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud; return interval_from_number(av[0]);
}
static val_t prim_interval_p(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud; return vbool(vis_ival(av[0]));
}
static val_t prim_interval_lo(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_ival(av[0])) scm_raise(V_FALSE, "interval-lo: not an interval");
    return as_ival(av[0])->lo;
}
static val_t prim_interval_hi(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_ival(av[0])) scm_raise(V_FALSE, "interval-hi: not an interval");
    return as_ival(av[0])->hi;
}
static val_t prim_interval_mid(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_ival(av[0])) scm_raise(V_FALSE, "interval-midpoint: not an interval");
    Interval *iv = as_ival(av[0]);
    return num_div(num_add(iv->lo, iv->hi), vfix(2));
}
static val_t prim_interval_width(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_ival(av[0])) scm_raise(V_FALSE, "interval-width: not an interval");
    Interval *iv = as_ival(av[0]);
    return num_sub(iv->hi, iv->lo);
}
static val_t prim_interval_contains(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_ival(av[0])) scm_raise(V_FALSE, "interval-contains?: not an interval");
    Interval *iv = as_ival(av[0]);
    return vbool(num_le(iv->lo, av[1]) && num_le(av[1], iv->hi));
}
#endif /* BUILD_MPFR */

/* ---- Registration ---- */

/* Build exact rational: decimal_digits / 10^(strlen(decimal_digits) - 1).
 * E.g. "314159" -> 314159/100000 = 3.14159 as an exact mpq. */
static val_t make_decimal_rat(const char *digits) {
    int n = (int)strlen(digits);
    char *den = malloc((size_t)(n + 1));
    den[0] = '1';
    memset(den + 1, '0', (size_t)(n - 1));
    den[n] = '\0';
    val_t result = num_make_rational(num_make_bignum_str(digits, 10),
                                     num_make_bignum_str(den, 10));
    free(den);
    return result;
}

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
    DEF("make-list",  prim_make_list, 1,2);
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
    DEF("truncate-quotient",prim_quotient,2,2); DEF("truncate-remainder",prim_remainder,2,2);
    DEF("truncate/",prim_truncate_div,2,2);
    DEF("square",prim_square,1,1); DEF("exact-integer?",prim_exact_integer_p,1,1);
    DEF("exact-integer-sqrt",prim_exact_integer_sqrt,1,1);
    DEF("modulo",prim_modulo,2,2);
    DEF("floor",prim_floor,1,1); DEF("ceiling",prim_ceiling,1,1);
    DEF("truncate",prim_truncate,1,1); DEF("round",prim_round,1,1);
    DEF("exact",prim_exact,1,1); DEF("inexact",prim_inexact,1,1);
    DEF("exact->inexact",prim_inexact,1,1); DEF("inexact->exact",prim_exact,1,1);
    DEF("expt",prim_expt,2,2); DEF("sqrt",prim_sqrt,1,1);
    DEF("exp",prim_exp,1,1); DEF("log",prim_log,1,2);
    DEF("sin",prim_sin,1,1); DEF("cos",prim_cos,1,1); DEF("tan",prim_tan,1,1);
    DEF("asin",prim_asin,1,1); DEF("acos",prim_acos,1,1); DEF("atan",prim_atan,1,2);
    DEF("sinh",prim_sinh,1,1); DEF("cosh",prim_cosh,1,1); DEF("tanh",prim_tanh,1,1);
    DEF("asinh",prim_asinh,1,1); DEF("acosh",prim_acosh,1,1); DEF("atanh",prim_atanh,1,1);
    DEF("cot",prim_cot,1,1); DEF("sec",prim_sec,1,1); DEF("csc",prim_csc,1,1);
    DEF("floor-quotient",prim_floor_quotient,2,2);
    DEF("floor-remainder",prim_floor_remainder,2,2);
    DEF("floor/",prim_floor_div,2,2);
    DEF("numerator",prim_numerator,1,1); DEF("denominator",prim_denominator,1,1);
    DEF("make-rectangular",prim_make_rectangular,2,2);
    DEF("make-polar",prim_make_polar,2,2);
    DEF("real-part",prim_real_part,1,1); DEF("imag-part",prim_imag_part,1,1);
    DEF("magnitude",prim_magnitude,1,1); DEF("angle",prim_angle,1,1);
    DEF("number->string",prim_num_str,1,4); DEF("string->number",prim_str_num,1,2);
    DEF("current-number-notation",prim_current_number_notation,0,1);
    /* Bitwise (SRFI-151 / R7RS-large) */
    DEF("bitwise-and",prim_bitand,0,-1); DEF("bitwise-or",prim_bitor,0,-1);
    DEF("bitwise-xor",prim_bitxor,0,-1); DEF("bitwise-not",prim_bitnot,1,1);
    DEF("arithmetic-shift",prim_shl,2,2);
    /* Quaternion */
    DEF("make-quaternion",    prim_make_quat,       4,4);
    DEF("quaternion-w",       prim_quat_w,          1,1);
    DEF("quaternion-x",       prim_quat_x,          1,1);
    DEF("quaternion-y",       prim_quat_y,          1,1);
    DEF("quaternion-z",       prim_quat_z,          1,1);
    DEF("quaternion-norm",    prim_quat_norm,       1,1);
    DEF("quaternion-normalize",prim_quat_normalize, 1,1);
    DEF("quaternion-conjugate",prim_quat_conjugate, 1,1);
    DEF("quaternion-inverse",  prim_quat_inverse,   1,1);
    DEF("quaternion+",         prim_quat_add,       0,-1);
    DEF("quaternion*",         prim_quat_mul,       0,-1);
    DEF("quaternion-rotate-vector",prim_quat_rotate,2,2);
    DEF("make-octonion",prim_make_oct,8,8); DEF("octonion-ref",prim_oct_ref,2,2);

    /* Characters */
    DEF("char->integer",prim_char_to_int,1,1); DEF("integer->char",prim_int_to_char,1,1);
    DEF("char-upcase",prim_char_upcase,1,1); DEF("char-downcase",prim_char_downcase,1,1);
    DEF("char-foldcase",prim_char_foldcase,1,1);
    DEF("char-alphabetic?",prim_char_alpha_p,1,1); DEF("char-numeric?",prim_char_numeric_p,1,1);
    DEF("char-whitespace?",prim_char_whitespace_p,1,1);
    DEF("char-upper-case?",prim_char_upper_p,1,1); DEF("char-lower-case?",prim_char_lower_p,1,1);
    DEF("char=?",prim_char_eq,2,-1); DEF("char<?",prim_char_lt,2,-1);
    DEF("char<=?",prim_char_le,2,-1); DEF("char>?",prim_char_gt,2,-1); DEF("char>=?",prim_char_ge,2,-1);
    DEF("char-ci=?",prim_char_ci_eq,2,-1); DEF("char-ci<?",prim_char_ci_lt,2,-1);
    DEF("char-ci<=?",prim_char_ci_le,2,-1); DEF("char-ci>?",prim_char_ci_gt,2,-1); DEF("char-ci>=?",prim_char_ci_ge,2,-1);
    DEF("digit-value",prim_digit_value,1,1);

    /* Strings */
    DEF("make-string",prim_make_string,1,2); DEF("string",prim_string,0,-1);
    DEF("string-length",prim_string_length,1,1); DEF("string-ref",prim_string_ref,2,2);
    DEF("string-copy",prim_string_copy,1,3); DEF("string-append",prim_string_append,0,-1);
    DEF("string->list",prim_string_to_list,1,3); DEF("list->string",prim_list_to_string,1,1);
    DEF("string->symbol",prim_string_to_symbol,1,1); DEF("symbol->string",prim_symbol_to_string,1,1);
    DEF("string=?",prim_string_eq,2,-1); DEF("string<?",prim_string_lt,2,-1);
    DEF("string<=?",prim_string_le,2,-1); DEF("string>?",prim_string_gt,2,-1); DEF("string>=?",prim_string_ge,2,-1);
    DEF("string-ci=?",prim_string_ci_eq,2,-1); DEF("string-ci<?",prim_string_ci_lt,2,-1);
    DEF("string-ci<=?",prim_string_ci_le,2,-1); DEF("string-ci>?",prim_string_ci_gt,2,-1); DEF("string-ci>=?",prim_string_ci_ge,2,-1);
    DEF("string-upcase",prim_string_upcase,1,1); DEF("string-downcase",prim_string_downcase,1,1);
    DEF("string-set!",prim_string_set_bang,3,3); DEF("string-copy!",prim_string_copy_bang,3,5);
    DEF("substring",prim_substring,2,3); DEF("string-contains",prim_string_contains,2,2);
    DEF("string-for-each",prim_string_for_each,2,-1);
    DEF("string-fill!",prim_string_fill_bang,2,4);
    DEF("string-foldcase",prim_string_foldcase,1,1);
    DEF("write-string",prim_write_string,1,4);
    DEF("string->utf8",prim_string_to_utf8,1,3);
    DEF("utf8->string",prim_utf8_to_string,1,3);

    /* Vectors */
    DEF("vector-append",prim_vector_append,0,-1);
    DEF("make-vector",prim_make_vector,1,2); DEF("vector",prim_vector,0,-1);
    DEF("vector-length",prim_vector_length,1,1); DEF("vector-ref",prim_vector_ref,2,2);
    DEF("vector-set!",prim_vector_set,3,3); DEF("vector->list",prim_vector_to_list,1,3);
    DEF("list->vector",prim_list_to_vector,1,1); DEF("vector-fill!",prim_vector_fill,2,4);
    DEF("vector-copy",prim_vector_copy,1,3);
    DEF("vector-copy!",prim_vector_copy_bang,3,5);
    DEF("vector-map",prim_vector_map,2,-1);
    DEF("vector-for-each",prim_vector_for_each,2,-1);
    DEF("vec3-project-batch",prim_vec3_project_batch,8,8);

    /* Bytevectors */
    DEF("make-bytevector",prim_make_bytes,1,2); DEF("bytevector-length",prim_bytes_length,1,1);
    DEF("bytevector-u8-ref",prim_bytes_u8_ref,2,2); DEF("bytevector-u8-set!",prim_bytes_u8_set,3,3);
    DEF("bytevector",prim_bytevector,0,-1);
    DEF("bytevector-copy",prim_bytes_copy,1,3); DEF("bytevector-copy!",prim_bytes_copy_bang,3,5);
    DEF("bytevector-append",prim_bytes_append,0,-1);

    /* I/O */
    DEF("display",prim_display,1,2); DEF("write",prim_write,1,2);
    DEF("newline",prim_newline,0,1); DEF("write-char",prim_write_char,1,2);
    DEF("read",prim_read,0,1); DEF("read-char",prim_read_char,0,1);
    DEF("peek-char",prim_peek_char,0,1); DEF("read-line",prim_read_line,0,1);
    DEF("read-string",prim_read_string,1,2);
    DEF("read-u8",prim_read_u8,0,1); DEF("peek-u8",prim_peek_u8,0,1);
    DEF("read-bytevector",prim_read_bytevector,1,2); DEF("read-bytevector!",prim_read_bytevector_bang,1,4);
    DEF("write-u8",prim_write_u8,1,2); DEF("write-bytevector",prim_write_bytevector,1,4);
    DEF("write-simple",prim_write_simple,1,2);
    DEF("char-ready?",prim_char_ready_p,0,1); DEF("u8-ready?",prim_u8_ready_p,0,1);
    DEF("flush-output-port",prim_flush_output_port,0,1);
    DEF("open-input-string",prim_open_input_string,1,1);
    DEF("open-output-string",prim_open_output_string,0,0);
    DEF("get-output-string",prim_get_output_string,1,1);
    DEF("open-input-bytevector",prim_open_input_bytevector,1,1);
    DEF("open-output-bytevector",prim_open_output_bytevector,0,0);
    DEF("get-output-bytevector",prim_get_output_bytevector,1,1);
    DEF("write-shared",prim_write_shared,1,2);
    DEF("open-input-file",prim_open_input_file,1,1);
    DEF("open-output-file",prim_open_output_file,1,1);
    DEF("file-exists?",prim_file_exists_p,1,1);
    DEF("delete-file",prim_delete_file,1,1);
    DEF("call-with-input-file",prim_call_with_input_file,2,2);
    DEF("call-with-output-file",prim_call_with_output_file,2,2);
    DEF("with-input-from-file",prim_with_input_from_file,2,2);
    DEF("with-output-to-file",prim_with_output_to_file,2,2);
    DEF("close-port",prim_close_port,1,1); DEF("close-input-port",prim_close_port,1,1);
    DEF("close-output-port",prim_close_port,1,1);
    DEF("input-port?",prim_input_port_p,1,1); DEF("output-port?",prim_output_port_p,1,1);
    DEF("input-port-open?",prim_input_port_open_p,1,1); DEF("output-port-open?",prim_output_port_open_p,1,1);
    DEF("current-input-port",prim_current_input_port,0,0);
    DEF("current-output-port",prim_current_output_port,0,0);
    DEF("current-error-port",prim_current_error_port,0,0);
    DEF("with-output-to-string",prim_with_output_to_string,1,1);

    /* Control */
    DEF("eval",prim_eval,1,2); DEF("interaction-environment",prim_interaction_env,0,0);
    DEF("apply",prim_apply,2,-1);
    DEF("for-each",prim_for_each,2,-1);
    DEF("filter",prim_filter,2,2); DEF("fold-left",prim_fold,3,3); DEF("fold-right",prim_fold_right,3,3);
    DEF("not",prim_not,1,1);
    DEF("force",prim_force,1,1); DEF("make-promise",prim_make_promise,1,1);
    DEF("error",prim_error,1,-1); DEF("raise",prim_raise,1,1);
    DEF("raise-continuable",prim_raise_continuable,1,1);
    DEF("error-message",prim_error_message,1,1);
    DEF("error-object-message",prim_error_message,1,1);
    DEF("error-object?",prim_error_object_p,1,1);
    DEF("error-object-irritants",prim_error_irritants,1,1);
    DEF("error-object-code",prim_error_code,1,1);
    DEF("read-error?",prim_read_error_p,1,1); DEF("file-error?",prim_file_error_p,1,1);
    DEF("error-object->string",prim_error_to_string,1,1);
    DEF("with-exception-handler",prim_with_exception_handler,2,2);
    DEF("dynamic-wind",           prim_dynamic_wind,           3,3);
    DEF("call-with-port",         prim_call_with_port,         2,2);
    DEF("values",prim_values,0,-1);
    DEF("call-with-values",prim_call_with_values,2,2);
    DEF("call-with-current-continuation",prim_call_cc,1,1);
    DEF("call/cc",prim_call_cc,1,1);
    DEF("boolean=?",prim_boolean_eq,2,-1);

    /* Sets — core */
    DEF("make-set",        prim_make_set,    0,1); DEF("set-add!",     prim_set_add,     2,2);
    DEF("set-delete!",     prim_set_del,     2,2); DEF("set-member?",  prim_set_member,  2,2);
    DEF("set->list",       prim_set_to_list, 1,1); DEF("list->set",    prim_list_to_set, 1,2);
    DEF("set-union",       prim_set_union,   2,2); DEF("set-intersection",prim_set_inter,2,2);
    DEF("set-difference",  prim_set_diff,    2,2); DEF("set-subset?",  prim_set_subset,  2,2);
    DEF("set-size",        prim_set_size,    1,1);
    /* Sets — structural */
    DEF("set=?",           prim_set_equal,   2,2); DEF("set-empty?",   prim_set_empty,   1,1);
    DEF("set-copy",        prim_set_copy,    1,1);
    DEF("set-adjoin",      prim_set_adjoin,  1,-1); DEF("set-adjoin!", prim_set_adjoin_mut, 1,-1);
    DEF("set-delete",      prim_set_delete_one, 2,2);
    DEF("set-symmetric-difference", prim_set_sym_diff, 2,2);
    /* Sets — higher-order */
    DEF("set-for-each",    prim_set_for_each,2,2); DEF("set-map",      prim_set_map,     2,2);
    DEF("set-filter",      prim_set_filter,  2,2); DEF("set-filter!",  prim_set_filter_mut,2,2);
    DEF("set-fold",        prim_set_fold,    3,3);
    DEF("set-any?",        prim_set_any,     2,2); DEF("set-every?",   prim_set_every,   2,2);
    DEF("set-count",       prim_set_count,   2,2); DEF("set-find",     prim_set_find,    2,3);

    /* Hash tables */
    DEF("make-hash-table", prim_make_hash,   0,1); DEF("hash-table-set!", prim_hash_set, 3,3);
    DEF("hash-table-ref",  prim_hash_ref,    2,3); DEF("hash-table-delete!", prim_hash_del, 2,2);
    DEF("hash-table-exists?", prim_hash_has, 2,2);
    DEF("hash-table-keys", prim_hash_keys,   1,1); DEF("hash-table-values", prim_hash_vals, 1,1);
    DEF("hash-table->alist",prim_hash_to_alist,1,1); DEF("hash-table-size", prim_hash_size, 1,1);

    /* Actors */
    DEF("spawn",      prim_spawn,       1,-1); DEF("send!",      prim_send,        2,2);
    DEF("receive",    prim_receive,     0,1);  DEF("self",       prim_self,        0,0);
    DEF("actor-alive?",   prim_actor_alive,   1,1);
    DEF("actor-stats",    prim_actor_stats,   1,1);
    DEF("actor-set-name!",prim_actor_set_name,2,2);
    DEF("actor-id",       prim_actor_id,      1,1);

    /* Parameters */
    DEF("make-parameter", prim_make_parameter, 1,2);

    /* Internal record helpers */
    DEF("%record-ctor",   prim_record_ctor, 1,-1);
    DEF("%record-pred?",  prim_record_pred, 2,2);
    DEF("%record-ref",    prim_record_ref,  2,2);
    DEF("%record-set!",   prim_record_set,  3,3);
    DEF("%make-record-type", prim_make_record_type, 2,2);

    /* Misc */
    DEF("gensym",     prim_gensym,  0,1);
    DEF("void",       prim_void,    0,0);
    DEF("load",       prim_load,    1,1);
    DEF("exit",       prim_exit,    0,1);
    DEF("quit",       prim_exit,    0,1);
    DEF("breakpoint", prim_breakpoint, 0,0);
    DEF("emergency-exit",prim_emergency_exit,0,1);
    DEF("system",     prim_system,  1,1);
    DEF("get-environment-variable", prim_get_env_var,  1,1);
    DEF("get-environment-variables",prim_get_env_vars, 0,0);
    DEF("current-second",      prim_current_second,      0,0);
    DEF("current-jiffy",       prim_current_jiffy,       0,0);
    DEF("jiffies-per-second",  prim_jiffies_per_second,  0,0);
    DEF("gc",                          prim_gc,                    0,0);
    DEF("gc-mode",                     prim_gc_mode,               0,0);
    DEF("gc-heap-size",                prim_gc_heap_size,          0,0);
    DEF("gc-free-bytes",               prim_gc_free_bytes,         0,0);
    DEF("gc-total-bytes",              prim_gc_total_bytes,        0,0);
    DEF("gc-enable-incremental!",      prim_gc_enable_incremental, 0,0);
    DEF("gc-set-free-space-divisor!",  prim_gc_set_fsd,            1,1);
    DEF("gc-stats",                    prim_gc_stats,              0,0);
    DEF("gc-stats-reset!",             prim_gc_stats_reset,        0,0);
    DEF("gc-set-max-heap!",            prim_gc_set_max_heap,       1,1);
    DEF("profiling-report",  prim_profiling_report, 0,0);
    DEF("profiling-reset",   prim_profiling_reset,  0,0);
    DEF("eof-object", prim_eof_object, 0,0);

    /* Constants */
    env_define(env, sym_intern_cstr("#t"),    V_TRUE);
    env_define(env, sym_intern_cstr("#f"),    V_FALSE);
    env_define(env, sym_intern_cstr("else"),  V_TRUE);   /* for cond/case */

    /* Inexact transcendental constants (standard R7RS — (exact? pi) => #f) */
    {
        val_t fpi = num_make_float(M_PI);
        val_t fe  = num_make_float(M_E);
        env_define(env, sym_intern_cstr("pi"), fpi);
        env_define(env, sym_intern_cstr("π"),  fpi);   /* U+03C0 alias */
        env_define(env, sym_intern_cstr("e"),  fe);
    }

    /* Exact rational approximations — 100 significant decimal digits.
     * The numerator is a ~330-bit GMP bignum; denominator is 10^99.
     * (exact pi) gives only the IEEE 754 double as a small rational;
     * exact-pi/exact-e carry far more precision. */
    {
        val_t ep = make_decimal_rat(
            "3141592653589793238462643383279502884197"
            "169399375105820974944592307816406286208998628034825342117067");
        val_t ee = make_decimal_rat(
            "2718281828459045235360287471352662497757"
            "247093699959574966967627724076630353547594571382178525166427");
        env_define(env, sym_intern_cstr("exact-pi"), ep);
        env_define(env, sym_intern_cstr("exact-π"),  ep);  /* U+03C0 alias */
        env_define(env, sym_intern_cstr("exact-e"),  ee);
    }

    env_define(env, sym_intern_cstr("+inf.0"),num_make_float(1.0/0.0));
    env_define(env, sym_intern_cstr("-inf.0"),num_make_float(-1.0/0.0));
    env_define(env, sym_intern_cstr("+nan.0"),num_make_float(0.0/0.0));
    env_define(env, sym_intern_cstr("SET-EQ"),    vfix(SET_CMP_EQ));
    env_define(env, sym_intern_cstr("SET-EQV"),   vfix(SET_CMP_EQV));
    env_define(env, sym_intern_cstr("SET-EQUAL"), vfix(SET_CMP_EQUAL));

    /* Cuneiform/Akkadian constants */
    env_define(env, sym_intern_cstr("𒌋𒉡"),  V_TRUE);   /* U.NU = "and-not" = #t (truth) */
    env_define(env, sym_intern_cstr("𒉡"),    V_FALSE);  /* NU = "not" = #f */
    env_define(env, sym_intern_cstr("𒊭"),    V_NIL);    /* ŠA3 = inside/empty = '() */
    env_define(env, sym_intern_cstr("ṣifrum"),vfix(0));  /* zero */
    env_define(env, sym_intern_cstr("𒄿𒀭"),  num_make_float(3.14159265358979323846)); /* π */


    /* Multivectors — Clifford algebra Cl(p,q,r) */
    extern void mv_register_builtins(val_t env);
    mv_register_builtins(env);

    /* Condition system */
    extern void condition_register_builtins(val_t env);
    condition_register_builtins(env);

#ifdef BUILD_FFI
    /* FFI */
    extern void ffi_register_builtins(val_t env);
    ffi_register_builtins(env);
#endif

    /* Matrices, tensors, and spinors */
    extern void mat_register_builtins(val_t env);
    mat_register_builtins(env);
    extern void spinor_register_builtins(val_t env);
    spinor_register_builtins(env);

    /* syntax-rules keyword */
    syntax_rules_register(env);

    builtins_curry_register(env);

    /* Number theory (GMP-only; always built) */
    builtins_numtheory_register(env);

#ifdef BUILD_MPFR
    /* MPFR constructors / predicates */
    DEF("mpfr",                 prim_mpfr,             1, 2);
    DEF("mpfr?",                prim_mpfr_p,           1, 1);
    DEF("mpfr-precision",       prim_mpfr_prec,        1, 1);
    DEF("mpfr-set-precision",   prim_mpfr_set_prec,    2, 2);
    /* Constants */
    DEF("mpfr-pi",              prim_mpfr_pi,          0, 1);
    DEF("mpfr-e",               prim_mpfr_e,           0, 1);
    DEF("mpfr-phi",             prim_mpfr_phi,         0, 1);
    DEF("mpfr-log2",            prim_mpfr_log2c,       0, 1);
    DEF("mpfr-euler",           prim_mpfr_euler,       0, 1);
    DEF("mpfr-catalan",         prim_mpfr_catalan,     0, 1);
    DEF("mpfr-apery",           prim_mpfr_apery,       0, 1);
    /* Dynamic precision */
    DEF("call-with-precision",  prim_call_with_precision, 2, 2);
    DEF("current-precision",    prim_current_precision,   0, 0);
    DEF("mpfr-rounding-mode",   prim_mpfr_rnd_mode,       0, 1);
    /* MPFR-specific math */
    DEF("mpfr-sqrt",            prim_mpfr_sqrt,        1, 1);
    DEF("mpfr-exp",             prim_mpfr_exp,         1, 1);
    DEF("mpfr-log",             prim_mpfr_log,         1, 1);
    DEF("mpfr-gamma",           prim_mpfr_gamma,       1, 1);
    DEF("mpfr-lgamma",          prim_mpfr_lgamma,      1, 1);
    DEF("mpfr-zeta",            prim_mpfr_zeta,        1, 1);
    DEF("mpfr-erf",             prim_mpfr_erf,         1, 1);
    DEF("mpfr-erfc",            prim_mpfr_erfc,        1, 1);
    DEF("mpfr-j0",              prim_mpfr_j0,          1, 1);
    DEF("mpfr-j1",              prim_mpfr_j1,          1, 1);
    DEF("mpfr-log2-of",         prim_mpfr_log2,        1, 1);
    DEF("mpfr-log10",           prim_mpfr_log10,       1, 1);
    DEF("mpfr-hypot",           prim_mpfr_hypot,       2, 2);
    DEF("mpfr-fma",             prim_mpfr_fma,         3, 3);
    /* Interval arithmetic */
    DEF("make-interval",        prim_make_interval,    2, 2);
    DEF("interval",             prim_interval_point,   1, 1);
    DEF("interval?",            prim_interval_p,       1, 1);
    DEF("interval-lo",          prim_interval_lo,      1, 1);
    DEF("interval-hi",          prim_interval_hi,      1, 1);
    DEF("interval-midpoint",    prim_interval_mid,     1, 1);
    DEF("interval-width",       prim_interval_width,   1, 1);
    DEF("interval-contains?",   prim_interval_contains, 2, 2);

    /* Install `(with-precision N body ...)` as syntax-rules sugar.
     * Safety: the `form` argument is a fixed compile-time literal — it is the
     * curry-Scheme syntax-rules definition of the macro itself.  No user input
     * reaches this eval(); it is the standard mechanism for installing
     * Scheme-level macros from C startup code. */
    {
        val_t form = scm_read_cstr(
            "(define-syntax with-precision"
            "  (syntax-rules ()"
            "    ((_ bits body ...)"
            "     (call-with-precision bits (lambda () body ...)))))");
        if (!vis_eof(form)) eval(form, env);
    }

    mpfr_num_init();
#endif

    /* ---- Foreign-language procedure aliases (Akkadian and any other
     * registered language pack — see lang_registry.h) ---- */
    /* For each registered procedure entry, look up the English binding
     * and register every foreign written form pointing to the same
     * value. This must run LAST in builtins_register() — every other
     * registration call above (condition_register_builtins,
     * builtins_numtheory_register, mv/ffi/mat/spinor_register_builtins,
     * syntax_rules_register, builtins_curry_register, the MPFR block)
     * defines names this loop looks up; running the loop any earlier
     * silently drops aliases for whatever hasn't been registered yet
     * (env_lookup_or_false just returns false and the alias is skipped,
     * no error). This bit us once already for a stray syntax-rules entry
     * (commit 31c71a9) and would otherwise silently drop every
     * numtheory/condition alias too. */
    {
        int n = lang_pr_count();
        for (int idx = 0; idx < n; idx++) {
            val_t eng;
            val_t forms[LANG_PR_MAX_FORMS];
            int nforms;
            if (!lang_pr_at(idx, &eng, forms, &nforms)) continue;
            val_t v = env_lookup_or_false(env, eng);
            if (vis_false(v)) continue;
            for (int f = 0; f < nforms; f++) env_define(env, forms[f], v);
        }
    }
}
