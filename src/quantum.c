#include "quantum.h"
#include "object.h"
#include "gc.h"
#include "numeric.h"
#include "port.h"
#include "symbol.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern void scm_raise(val_t kind, const char *fmt, ...) __attribute__((noreturn));
extern void scm_write(val_t v, val_t port);

void quantum_init(void) { /* nothing; uses rand() seeded by main */ }

/* ---- Internal helpers ---- */

static val_t make_quantum(int n, val_t *amps, val_t *vals) {
    Quantum *q = (Quantum *)gc_alloc(sizeof(Quantum) + (size_t)(2 * n) * sizeof(val_t));
    q->hdr.type  = T_QUANTUM;
    q->hdr.flags = 0;
    q->n         = n;
    for (int i = 0; i < n; i++) {
        q->data[2*i]   = amps[i];
        q->data[2*i+1] = vals[i];
    }
    return vptr(q);
}

/* |amplitude|² for a complex or real amplitude */
static double amp_prob(val_t amp) {
    if (vis_complex(amp)) {
        double r = num_to_double(as_cpx(amp)->real);
        double im = num_to_double(as_cpx(amp)->imag);
        return r*r + im*im;
    }
    double a = num_to_double(amp);
    return a * a;
}

/* ---- Constructors ---- */

val_t quantum_from_pairs(val_t pair_list) {
    /* Count pairs */
    int n = 0;
    val_t p = pair_list;
    while (vis_pair(p)) { n++; p = vcdr(p); }
    if (n == 0) scm_raise(V_FALSE, "superpose: empty state list");

    val_t *amps = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
    val_t *vals = (val_t *)gc_alloc((size_t)n * sizeof(val_t));

    p = pair_list;
    for (int i = 0; i < n; i++) {
        val_t pair = vcar(p);
        if (!vis_pair(pair))
            scm_raise(V_FALSE, "superpose: each state must be (amplitude . value)");
        amps[i] = vcar(pair);
        vals[i] = vcdr(pair);
        if (!vis_number(amps[i]))
            scm_raise(V_FALSE, "superpose: amplitude must be a number");
        p = vcdr(p);
    }

    /* Normalize so Σ|α|² = 1 */
    double total = 0.0;
    for (int i = 0; i < n; i++) total += amp_prob(amps[i]);
    if (total <= 0.0) scm_raise(V_FALSE, "superpose: total probability is zero");
    double scale = 1.0 / sqrt(total);
    for (int i = 0; i < n; i++) {
        if (vis_complex(amps[i])) {
            amps[i] = num_make_complex(
                num_mul(as_cpx(amps[i])->real, num_make_float(scale)),
                num_mul(as_cpx(amps[i])->imag, num_make_float(scale)));
        } else {
            amps[i] = num_make_float(num_to_double(amps[i]) * scale);
        }
    }

    return make_quantum(n, amps, vals);
}

val_t quantum_uniform(val_t value_list) {
    int n = 0;
    val_t p = value_list;
    while (vis_pair(p)) { n++; p = vcdr(p); }
    if (n == 0) scm_raise(V_FALSE, "quantum-uniform: empty list");

    double amp = 1.0 / sqrt((double)n);
    val_t *amps = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
    val_t *vals = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
    p = value_list;
    for (int i = 0; i < n; i++) {
        amps[i] = num_make_float(amp);
        vals[i] = vcar(p);
        p = vcdr(p);
    }
    return make_quantum(n, amps, vals);
}

/* ---- Observation ---- */

val_t quantum_observe(val_t q) {
    Quantum *qv = as_quantum(q);
    double r = (double)rand() / ((double)RAND_MAX + 1.0);
    double cumulative = 0.0;
    for (int i = 0; i < qv->n; i++) {
        cumulative += amp_prob(qv->data[2*i]);
        if (r < cumulative) return qv->data[2*i+1];
    }
    return qv->data[2*(qv->n-1)+1];  /* last state (floating point safety) */
}

/* ---- Map operation ---- */

val_t quantum_map_fn(val_t q, val_t (*fn)(val_t, void *), void *ud) {
    Quantum *qv = as_quantum(q);
    val_t *amps = (val_t *)gc_alloc((size_t)qv->n * sizeof(val_t));
    val_t *vals = (val_t *)gc_alloc((size_t)qv->n * sizeof(val_t));
    for (int i = 0; i < qv->n; i++) {
        amps[i] = qv->data[2*i];
        vals[i] = fn(qv->data[2*i+1], ud);
    }
    return make_quantum(qv->n, amps, vals);
}

/* ---- Scalar arithmetic (map over states) ---- */

static val_t add_s(val_t v, void *ud) { return num_add(v, *(val_t *)ud); }
static val_t sub_s(val_t v, void *ud) { return num_sub(v, *(val_t *)ud); }
static val_t mul_s(val_t v, void *ud) { return num_mul(v, *(val_t *)ud); }
static val_t div_s(val_t v, void *ud) { return num_div(v, *(val_t *)ud); }

val_t quantum_add_scalar(val_t q, val_t s) { return quantum_map_fn(q, add_s, &s); }
val_t quantum_sub_scalar(val_t q, val_t s) { return quantum_map_fn(q, sub_s, &s); }
val_t quantum_mul_scalar(val_t q, val_t s) { return quantum_map_fn(q, mul_s, &s); }
val_t quantum_div_scalar(val_t q, val_t s) { return quantum_map_fn(q, div_s, &s); }

/* ---- Combine two quantum values (superposition; renormalize) ---- */

val_t quantum_superpose(val_t a, val_t b) {
    Quantum *qa = as_quantum(a), *qb = as_quantum(b);
    int na = qa->n, nb = qb->n, n = na + nb;
    /* Scale each by 1/√2 to maintain normalization */
    double scale = 1.0 / sqrt(2.0);
    val_t *amps = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
    val_t *vals = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
    for (int i = 0; i < na; i++) {
        amps[i] = num_mul(qa->data[2*i], num_make_float(scale));
        vals[i] = qa->data[2*i+1];
    }
    for (int i = 0; i < nb; i++) {
        amps[na+i] = num_mul(qb->data[2*i], num_make_float(scale));
        vals[na+i]  = qb->data[2*i+1];
    }
    return make_quantum(n, amps, vals);
}

/* ---- Accessors ---- */

int   quantum_n(val_t q)      { return as_quantum(q)->n; }
val_t quantum_amp(val_t q, int i) { return as_quantum(q)->data[2*i]; }
val_t quantum_val(val_t q, int i) { return as_quantum(q)->data[2*i+1]; }

static val_t q_cons(val_t car, val_t cdr) {
    Pair *p = CURRY_NEW(Pair);
    p->hdr.type = T_PAIR; p->hdr.flags = 0;
    p->car = car; p->cdr = cdr;
    return vptr(p);
}

val_t quantum_to_list(val_t q) {
    Quantum *qv = as_quantum(q);
    val_t result = V_NIL;
    for (int i = qv->n - 1; i >= 0; i--) {
        val_t pair = q_cons(qv->data[2*i], qv->data[2*i+1]);
        result = q_cons(pair, result);
    }
    return result;
}

/* ---- Display ---- */

void quantum_write(val_t q, val_t port) {
    Quantum *qv = as_quantum(q);
    port_write_string(port, "#|", 2);
    for (int i = 0; i < qv->n; i++) {
        if (i) port_write_string(port, " + ", 3);
        /* Write amplitude */
        char buf[32];
        double prob = amp_prob(qv->data[2*i]);
        int n = snprintf(buf, sizeof(buf), "%.4g", sqrt(prob));
        port_write_string(port, buf, (uint32_t)n);
        port_write_char(port, '|');
        scm_write(qv->data[2*i+1], port);
        port_write_char(port, '>');
    }
    port_write_string(port, "|", 1);
}
