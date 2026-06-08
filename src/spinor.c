/*
 * Spinor type for Curry Scheme.
 *
 * Weyl/Dirac/Majorana spinors with correct SL(2,C) transform law.
 * Spinors are NOT tensors — the transform law is owned by this type.
 *
 * Internal layout: Spinor.data[] = [re0, im0, re1, im1, ...]
 */

#include "spinor.h"
#include "object.h"
#include "gc.h"
#include "numeric.h"
#include "port.h"
#include "env.h"
#include "symbol.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

extern void scm_raise(val_t kind, const char *fmt, ...) __attribute__((noreturn));

static inline double spinor_re(const Spinor *s, int i) { return s->data[2*i];   }
static inline double spinor_im(const Spinor *s, int i) { return s->data[2*i+1]; }

static void val_to_cpx(val_t v, double *re, double *im) {
    if (vis_complex(v)) {
        *re = num_to_double(as_cpx(v)->real);
        *im = num_to_double(as_cpx(v)->imag);
    } else if (vis_flonum(v)) {
        *re = as_flo(v)->value; *im = 0.0;
    } else {
        *re = num_to_double(v); *im = 0.0;
    }
}

static val_t make_cpx(double re, double im) {
    if (im == 0.0) return num_make_float(re);
    return num_make_complex(num_make_float(re), num_make_float(im));
}

val_t spinor_make(uint8_t kind, uint8_t ncomp, const double *re, const double *im) {
    Spinor *s = (Spinor *)gc_alloc_atomic(sizeof(Spinor) + 2*ncomp*sizeof(double));
    s->hdr.type  = T_SPINOR;
    s->hdr.flags = 0;
    s->kind  = kind;
    s->ncomp = ncomp;
    for (uint8_t i = 0; i < ncomp; i++) {
        s->data[2*i]   = re ? re[i] : 0.0;
        s->data[2*i+1] = im ? im[i] : 0.0;
    }
    return vptr(s);
}

void spinor_write(val_t v, val_t port) {
    Spinor *s = as_spinor(v);
    static const char *knames[] = { "weyl-l", "weyl-r", "dirac", "majorana" };
    char buf[64];
    int len = snprintf(buf, sizeof(buf), "#<spinor %s",
                       s->kind < 4 ? knames[s->kind] : "?");
    port_write_string(port, buf, (uint32_t)len);
    for (int i = 0; i < s->ncomp; i++) {
        double re = spinor_re(s, i), im = spinor_im(s, i);
        len = snprintf(buf, sizeof(buf), " %g%+gi", re, im);
        port_write_string(port, buf, (uint32_t)len);
    }
    port_write_char(port, '>');
}

static void parse_sl2c(val_t M, double m[8], const char *who) {
    if (!vis_pair(M))
        scm_raise(V_FALSE, "%s: SL(2,C) matrix must be a list ((a b)(c d))", who);
    val_t row0 = vcar(M), rest = vcdr(M);
    val_t row1 = vis_pair(rest) ? vcar(rest) : V_NIL;
    if (!vis_pair(row0) || !vis_pair(row1))
        scm_raise(V_FALSE, "%s: SL(2,C) matrix must have two rows", who);
    val_t elems[4];
    elems[0] = vcar(row0);
    elems[1] = vis_pair(vcdr(row0)) ? vcar(vcdr(row0)) : V_FALSE;
    elems[2] = vcar(row1);
    elems[3] = vis_pair(vcdr(row1)) ? vcar(vcdr(row1)) : V_FALSE;
    for (int i = 0; i < 4; i++)
        val_to_cpx(elems[i], &m[2*i], &m[2*i+1]);
}

/* SL(2,C) inverse: M^{-1} = [[d,-b],[-c,a]] assuming det=1 */
static void sl2c_inverse(const double m[8], double mi[8]) {
    mi[0] =  m[6]; mi[1] =  m[7];
    mi[2] = -m[2]; mi[3] = -m[3];
    mi[4] = -m[4]; mi[5] = -m[5];
    mi[6] =  m[0]; mi[7] =  m[1];
}

static void cmul(double ar, double ai, double br, double bi, double *rr, double *ri) {
    *rr = ar*br - ai*bi;
    *ri = ar*bi + ai*br;
}

static void apply_2x2(const double m[8],
                      const double inr[2], const double ini[2],
                      double outr[2], double outi[2]) {
    double r0,i0,r1,i1;
    cmul(m[0],m[1], inr[0],ini[0], &r0,&i0);
    cmul(m[2],m[3], inr[1],ini[1], &r1,&i1);
    outr[0] = r0+r1; outi[0] = i0+i1;
    cmul(m[4],m[5], inr[0],ini[0], &r0,&i0);
    cmul(m[6],m[7], inr[1],ini[1], &r1,&i1);
    outr[1] = r0+r1; outi[1] = i0+i1;
}

/* Build the (M†)^{-1} matrix used for right-handed Weyl transform */
static void right_weyl_matrix(const double m[8], double mt[8]) {
    double mi[8]; sl2c_inverse(m, mi);
    mt[0] =  mi[0]; mt[1] = -mi[1];
    mt[2] = -mi[4]; mt[3] =  mi[5];
    mt[4] = -mi[2]; mt[5] =  mi[3];
    mt[6] =  mi[6]; mt[7] = -mi[7];
}

static val_t do_spinor_transform(const Spinor *s, const double m[8]) {
    double inr[4]={0}, ini[4]={0}, outr[4]={0}, outi[4]={0};
    for (int i = 0; i < s->ncomp; i++) {
        inr[i] = spinor_re(s, i);
        ini[i] = spinor_im(s, i);
    }
    if (s->kind == SPINOR_WEYL_L) {
        apply_2x2(m, inr, ini, outr, outi);
    } else if (s->kind == SPINOR_WEYL_R) {
        double mt[8]; right_weyl_matrix(m, mt);
        apply_2x2(mt, inr, ini, outr, outi);
    } else {
        apply_2x2(m, inr, ini, outr, outi);
        double mt[8]; right_weyl_matrix(m, mt);
        apply_2x2(mt, inr+2, ini+2, outr+2, outi+2);
    }
    return spinor_make(s->kind, s->ncomp, outr, outi);
}

static void spinor_def(val_t env, const char *name,
                       val_t (*fn)(int, val_t *, void *), int mn, int mx) {
    Primitive *p = (Primitive *)gc_alloc_pinned(sizeof(Primitive));
    p->hdr.type = T_PRIMITIVE; p->hdr.flags = 0;
    p->name = name; p->fn = fn; p->min_args = mn; p->max_args = mx; p->ud = NULL;
    env_define(env, sym_intern_cstr(name), vptr(p));
}

static val_t prim_spinor_p(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc; return vis_spinor(av[0]) ? V_TRUE : V_FALSE; }
static val_t prim_make_spinor(int argc, val_t *av, void *ud) {
    (void)ud;
    if (!vis_symbol(av[0])) scm_raise(V_FALSE, "make-spinor: first argument must be a kind symbol");
    const char *kname = as_sym(av[0])->data;
    uint8_t kind, ncomp;
    if      (strcmp(kname,"weyl-l"  )==0) { kind=SPINOR_WEYL_L;   ncomp=2; }
    else if (strcmp(kname,"weyl-r"  )==0) { kind=SPINOR_WEYL_R;   ncomp=2; }
    else if (strcmp(kname,"dirac"   )==0) { kind=SPINOR_DIRAC;    ncomp=4; }
    else if (strcmp(kname,"majorana")==0) { kind=SPINOR_MAJORANA; ncomp=4; }
    else scm_raise(V_FALSE, "make-spinor: unknown kind '%s'", kname);
    if (argc != 1 + ncomp)
        scm_raise(V_FALSE, "make-spinor: %s needs %d components, got %d", kname, ncomp, argc-1);
    double re[4]={0}, im[4]={0};
    for (int i = 0; i < ncomp; i++) val_to_cpx(av[1+i], &re[i], &im[i]);
    return spinor_make(kind, ncomp, re, im);
}
static val_t prim_spinor_kind(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_spinor(av[0])) scm_raise(V_FALSE, "spinor-kind: not a spinor");
    static const char *kn[] = {"weyl-l","weyl-r","dirac","majorana"};
    uint8_t k = as_spinor(av[0])->kind;
    return sym_intern_cstr(k < 4 ? kn[k] : "unknown");
}
static val_t prim_spinor_ncomp(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_spinor(av[0])) scm_raise(V_FALSE, "spinor-ncomp: not a spinor");
    return vfix(as_spinor(av[0])->ncomp);
}
static val_t prim_spinor_ref(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_spinor(av[0])) scm_raise(V_FALSE, "spinor-ref: not a spinor");
    if (!vis_fixnum(av[1]))  scm_raise(V_FALSE, "spinor-ref: index must be integer");
    Spinor *s = as_spinor(av[0]); int i = (int)vunfix(av[1]);
    if (i < 0 || i >= s->ncomp) scm_raise(V_FALSE, "spinor-ref: index %d out of range", i);
    return make_cpx(spinor_re(s,i), spinor_im(s,i));
}
static val_t prim_spinor_set(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_spinor(av[0])) scm_raise(V_FALSE, "spinor-set!: not a spinor");
    if (!vis_fixnum(av[1]))  scm_raise(V_FALSE, "spinor-set!: index must be integer");
    Spinor *s = as_spinor(av[0]); int i = (int)vunfix(av[1]);
    if (i < 0 || i >= s->ncomp) scm_raise(V_FALSE, "spinor-set!: index %d out of range", i);
    val_to_cpx(av[2], &s->data[2*i], &s->data[2*i+1]);
    return V_VOID;
}
static val_t prim_spinor_add(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_spinor(av[0])||!vis_spinor(av[1])) scm_raise(V_FALSE, "spinor+: arguments must be spinors");
    Spinor *a=as_spinor(av[0]), *b=as_spinor(av[1]);
    if (a->kind!=b->kind||a->ncomp!=b->ncomp) scm_raise(V_FALSE, "spinor+: incompatible kinds");
    double re[4], im[4];
    for (int i=0;i<a->ncomp;i++){re[i]=spinor_re(a,i)+spinor_re(b,i);im[i]=spinor_im(a,i)+spinor_im(b,i);}
    return spinor_make(a->kind,a->ncomp,re,im);
}
static val_t prim_spinor_sub(int argc, val_t *av, void *ud) {
    (void)ud;
    if (!vis_spinor(av[0])) scm_raise(V_FALSE, "spinor-: not a spinor");
    Spinor *a = as_spinor(av[0]); double re[4], im[4];
    if (argc == 1) {
        for (int i=0;i<a->ncomp;i++){re[i]=-spinor_re(a,i);im[i]=-spinor_im(a,i);}
        return spinor_make(a->kind,a->ncomp,re,im);
    }
    if (!vis_spinor(av[1])) scm_raise(V_FALSE, "spinor-: second arg not a spinor");
    Spinor *b=as_spinor(av[1]);
    if (a->kind!=b->kind||a->ncomp!=b->ncomp) scm_raise(V_FALSE,"spinor-: incompatible kinds");
    for (int i=0;i<a->ncomp;i++){re[i]=spinor_re(a,i)-spinor_re(b,i);im[i]=spinor_im(a,i)-spinor_im(b,i);}
    return spinor_make(a->kind,a->ncomp,re,im);
}
static val_t prim_spinor_scale(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_spinor(av[0])) scm_raise(V_FALSE, "spinor-scale: not a spinor");
    Spinor *s=as_spinor(av[0]); double zr,zi; val_to_cpx(av[1],&zr,&zi); double re[4],im[4];
    for (int i=0;i<s->ncomp;i++){double ar=spinor_re(s,i),ai=spinor_im(s,i);re[i]=ar*zr-ai*zi;im[i]=ar*zi+ai*zr;}
    return spinor_make(s->kind,s->ncomp,re,im);
}
static val_t prim_spinor_transform(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_spinor(av[0])) scm_raise(V_FALSE, "spinor-transform: not a spinor");
    double m[8]; parse_sl2c(av[1], m, "spinor-transform");
    return do_spinor_transform(as_spinor(av[0]), m);
}
static val_t prim_spinor_conjugate(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_spinor(av[0])) scm_raise(V_FALSE, "spinor-conjugate: not a spinor");
    Spinor *s=as_spinor(av[0]); double re[4],im[4];
    for (int i=0;i<s->ncomp;i++){re[i]=spinor_re(s,i);im[i]=-spinor_im(s,i);}
    uint8_t nk=s->kind;
    if      (s->kind==SPINOR_WEYL_L) nk=SPINOR_WEYL_R;
    else if (s->kind==SPINOR_WEYL_R) nk=SPINOR_WEYL_L;
    if (s->kind==SPINOR_DIRAC||s->kind==SPINOR_MAJORANA) {
        double tr[4],ti[4];
        tr[0]=re[2];ti[0]=im[2]; tr[1]=re[3];ti[1]=im[3];
        tr[2]=re[0];ti[2]=im[0]; tr[3]=re[1];ti[3]=im[1];
        return spinor_make(nk,s->ncomp,tr,ti);
    }
    return spinor_make(nk,s->ncomp,re,im);
}
static val_t prim_spinor_adjoint(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_spinor(av[0])) scm_raise(V_FALSE, "spinor-adjoint: not a spinor");
    Spinor *s=as_spinor(av[0]);
    if (s->kind!=SPINOR_DIRAC&&s->kind!=SPINOR_MAJORANA)
        scm_raise(V_FALSE,"spinor-adjoint: only defined for dirac/majorana");
    /* ψ̄ = ψ†γ⁰; Dirac rep γ⁰=diag(1,1,-1,-1) */
    double re[4],im[4];
    for (int i=0;i<4;i++){double sign=(i<2)?1.0:-1.0;re[i]=sign*spinor_re(s,i);im[i]=-sign*spinor_im(s,i);}
    return spinor_make(s->kind,s->ncomp,re,im);
}
static val_t prim_spinor_inner(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_spinor(av[0])||!vis_spinor(av[1])) scm_raise(V_FALSE,"spinor-inner: arguments must be spinors");
    Spinor *a=as_spinor(av[0]),*b=as_spinor(av[1]);
    if (a->ncomp!=b->ncomp) scm_raise(V_FALSE,"spinor-inner: ncomp mismatch");
    double re=0,im=0;
    for (int i=0;i<a->ncomp;i++){
        double ar=spinor_re(a,i),ai=-spinor_im(a,i),br=spinor_re(b,i),bi=spinor_im(b,i);
        re+=ar*br-ai*bi; im+=ar*bi+ai*br;
    }
    return make_cpx(re,im);
}
static val_t prim_spinor_to_list(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_spinor(av[0])) scm_raise(V_FALSE,"spinor->list: not a spinor");
    Spinor *s=as_spinor(av[0]); val_t lst=V_NIL;
    for (int i=s->ncomp-1;i>=0;i--) {
        Pair *p=(Pair *)gc_alloc(sizeof(Pair));
        p->hdr.type=T_PAIR; p->hdr.flags=0;
        p->car=make_cpx(spinor_re(s,i),spinor_im(s,i)); p->cdr=lst; lst=vptr(p);
    }
    return lst;
}

void spinor_register_builtins(val_t env) {
    spinor_def(env,"spinor?",          prim_spinor_p,          1, 1);
    spinor_def(env,"make-spinor",      prim_make_spinor,       3, 5);
    spinor_def(env,"spinor-kind",      prim_spinor_kind,       1, 1);
    spinor_def(env,"spinor-ncomp",     prim_spinor_ncomp,      1, 1);
    spinor_def(env,"spinor-ref",       prim_spinor_ref,        2, 2);
    spinor_def(env,"spinor-set!",      prim_spinor_set,        3, 3);
    spinor_def(env,"spinor+",          prim_spinor_add,        2, 2);
    spinor_def(env,"spinor-",          prim_spinor_sub,        1, 2);
    spinor_def(env,"spinor-scale",     prim_spinor_scale,      2, 2);
    spinor_def(env,"spinor-transform", prim_spinor_transform,  2, 2);
    spinor_def(env,"spinor-conjugate", prim_spinor_conjugate,  1, 1);
    spinor_def(env,"spinor-adjoint",   prim_spinor_adjoint,    1, 1);
    spinor_def(env,"spinor-inner",     prim_spinor_inner,      2, 2);
    spinor_def(env,"spinor->list",     prim_spinor_to_list,    1, 1);
}
