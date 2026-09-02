#include "record_type.h"
#include "object.h"
#include "symbol.h"
#include "gc.h"
#include "eval.h"
#include <string.h>
#include <stdio.h>

/* Cons without going through the Scheme-level allocator wrappers — same
 * shape as runtime_internal.h's make_pair, duplicated here rather than
 * shared across a third translation unit since it's a one-line helper. */
static val_t rp_cons(val_t car, val_t cdr) {
    Pair *p = CURRY_NEW(Pair);
    p->hdr.type = T_PAIR; p->hdr.flags = 0;
    p->car = car; p->cdr = cdr;
    return vptr(p);
}

static int rp_list_length(val_t lst) {
    int n = 0;
    while (vis_pair(lst)) { n++; lst = vcdr(lst); }
    return n;
}

/* (%record-ctor <rtd-ref-expr> field0 field1 ...) */
static val_t rp_ctor_body(val_t rtd_ref_expr, val_t ctor_fields) {
    return rp_cons(
        rp_cons(sym_intern_cstr("%record-ctor"),
            rp_cons(rtd_ref_expr, ctor_fields)),
        V_NIL);
}

/* (%record-pred? <rtd-ref-expr> x) */
static val_t rp_pred_body(val_t rtd_ref_expr, val_t x_sym) {
    return rp_cons(
        rp_cons(sym_intern_cstr("%record-pred?"),
            rp_cons(rtd_ref_expr, rp_cons(x_sym, V_NIL))),
        V_NIL);
}

/* (%record-ref x 'field-index) */
static val_t rp_getter_body(val_t x_sym, val_t fi_val) {
    return rp_cons(
        rp_cons(sym_intern_cstr("%record-ref"),
            rp_cons(x_sym,
                rp_cons(rp_cons(S_QUOTE, rp_cons(fi_val, V_NIL)), V_NIL))),
        V_NIL);
}

/* (%record-set! x 'field-index v) */
static val_t rp_setter_body(val_t x_sym, val_t fi_val, val_t v_sym) {
    return rp_cons(
        rp_cons(sym_intern_cstr("%record-set!"),
            rp_cons(x_sym,
                rp_cons(rp_cons(S_QUOTE, rp_cons(fi_val, V_NIL)),
                        rp_cons(v_sym, V_NIL)))),
        V_NIL);
}

void record_type_build_spec(val_t rest, val_t rtd_ref, RecordTypeSpec *spec) {
    /* A malformed `(define-record-type)` (rest==V_NIL) used to SIGSEGV
     * here on `vcar(rest)` -- confirmed present on main too, unrelated to
     * any of tonight's other work; part of a wider, project-wide sweep of
     * this exact bug class across every special form's own compile-time
     * operand-list handling (see compiler.c's require_min_args). */
    if (!vis_pair(rest))
        scm_raise_code(EC_WRONG_NUMBER_OF_ARGUMENTS,
                        "define-record-type: ill-formed special form");
    val_t name_sym = vcar(rest);
    bool is_r6rs = vis_pair(vcdr(rest)) &&
                   vis_pair(vcadr(rest)) &&
                   (vcar(vcadr(rest)) == S_FIELDS  ||
                    vcar(vcadr(rest)) == sym_intern_cstr("parent") ||
                    vcar(vcadr(rest)) == sym_intern_cstr("protocol") ||
                    vcar(vcadr(rest)) == sym_intern_cstr("sealed") ||
                    vcar(vcadr(rest)) == sym_intern_cstr("opaque") ||
                    vcar(vcadr(rest)) == sym_intern_cstr("nongenerative"));

    val_t x_sym = sym_intern_cstr("x");
    val_t v_sym = sym_intern_cstr("v");

    if (is_r6rs) {
        /* R6RS define-record-type: auto-generate make-<name>, <name>?,
         * <name>-<field>, and <name>-<field>-set! from the (fields ...)
         * clause. */
        const char *ns = sym_cstr(name_sym);
        char buf[256];

        val_t field_list = V_NIL;
        val_t c = vcdr(rest);
        while (vis_pair(c)) {
            val_t cl = vcar(c);
            if (vis_pair(cl) && vcar(cl) == S_FIELDS) { field_list = vcdr(cl); break; }
            c = vcdr(c);
        }

        uint32_t nfields = (uint32_t)rp_list_length(field_list);
        RecordType *rtd = (RecordType *)gc_alloc_pinned(
            sizeof(RecordType) + nfields * sizeof(val_t));
        rtd->hdr.type = T_RECORD_TYPE; rtd->hdr.flags = 0;
        rtd->name = name_sym; rtd->nfields = nfields;
        rtd->constructor = V_FALSE; rtd->predicate = V_FALSE;
        rtd->accessors = (val_t *)gc_alloc_raw_pinned(nfields * sizeof(val_t));
        rtd->mutators  = (val_t *)gc_alloc_raw_pinned(nfields * sizeof(val_t));
        for (uint32_t k = 0; k < nfields; k++) { rtd->accessors[k] = V_FALSE; rtd->mutators[k] = V_FALSE; }

        val_t fs = field_list; uint32_t fi = 0;
        while (vis_pair(fs)) {
            val_t fspec = vcar(fs);
            val_t fname = vis_pair(fspec) ? vcadr(fspec) : fspec;
            rtd->field_names[fi++] = fname;
            fs = vcdr(fs);
        }
        val_t rtd_val = vptr(rtd);
        spec->rtd_val = rtd_val;
        val_t rtd_ref_expr = vis_false(rtd_ref)
            ? rp_cons(S_QUOTE, rp_cons(rtd_val, V_NIL)) : rtd_ref;

        /* Build ordered field-name param list for constructor */
        val_t ctor_fields = V_NIL;
        fs = field_list;
        while (vis_pair(fs)) {
            val_t fspec = vcar(fs);
            val_t fname = vis_pair(fspec) ? vcadr(fspec) : fspec;
            ctor_fields = rp_cons(fname, ctor_fields);
            fs = vcdr(fs);
        }
        { val_t rev = V_NIL, p = ctor_fields;
          while (vis_pair(p)) { rev = rp_cons(vcar(p), rev); p = vcdr(p); }
          ctor_fields = rev; }

        spec->bindings = (RecordBinding *)gc_alloc_raw_pinned(
            (size_t)(2 + 2 * (int)nfields) * sizeof(RecordBinding));
        int n = 0;

        /* Constructor: make-<name> */
        snprintf(buf, sizeof(buf), "make-%s", ns);
        spec->bindings[n].name   = sym_intern_cstr(buf);
        spec->bindings[n].params = ctor_fields;
        spec->bindings[n].body   = rp_ctor_body(rtd_ref_expr, ctor_fields);
        spec->bindings[n].role   = RTD_ROLE_CONSTRUCTOR;
        n++;

        /* Predicate: <name>? */
        snprintf(buf, sizeof(buf), "%s?", ns);
        spec->bindings[n].name   = sym_intern_cstr(buf);
        spec->bindings[n].params = rp_cons(x_sym, V_NIL);
        spec->bindings[n].body   = rp_pred_body(rtd_ref_expr, x_sym);
        spec->bindings[n].role   = RTD_ROLE_PREDICATE;
        n++;

        /* Accessors and mutators */
        fs = field_list; fi = 0;
        while (vis_pair(fs)) {
            val_t fspec      = vcar(fs);
            val_t fname      = vis_pair(fspec) ? vcadr(fspec) : fspec;
            bool  is_mutable = !vis_pair(fspec) || (vcar(fspec) == S_MUTABLE);
            val_t fi_val     = vfix((intptr_t)fi);

            snprintf(buf, sizeof(buf), "%s-%s", ns, sym_cstr(fname));
            spec->bindings[n].name        = sym_intern_cstr(buf);
            spec->bindings[n].params      = rp_cons(x_sym, V_NIL);
            spec->bindings[n].body        = rp_getter_body(x_sym, fi_val);
            spec->bindings[n].role        = RTD_ROLE_ACCESSOR;
            spec->bindings[n].field_index = (int)fi;
            n++;

            if (is_mutable) {
                snprintf(buf, sizeof(buf), "%s-%s-set!", ns, sym_cstr(fname));
                spec->bindings[n].name        = sym_intern_cstr(buf);
                spec->bindings[n].params      = rp_cons(x_sym, rp_cons(v_sym, V_NIL));
                spec->bindings[n].body        = rp_setter_body(x_sym, fi_val, v_sym);
                spec->bindings[n].role        = RTD_ROLE_MUTATOR;
                spec->bindings[n].field_index = (int)fi;
                n++;
            }
            fi++; fs = vcdr(fs);
        }
        spec->count = n;
        return;
    }

    /* R7RS: (define-record-type name (ctor-name field...) pred
     *        (field acc [mut])...) */
    /* Issue #135: unlike every other special form in curry (#124-#132),
     * this shape is destructured identically by BOTH the compiler and
     * the tree-walker via this one shared function, so a single fix
     * here closes both paths at once. `(define-record-type x)` (no
     * ctor-form/pred at all) and `(define-record-type (x))` (name_sym
     * itself a list, so vcdr(rest) is nil) previously fell straight
     * into vcadr(rest)/vcaddr(rest) below and SIGSEGVed. */
    if (!vis_pair(vcdr(rest)) || !vis_pair(vcdr(vcdr(rest))))
        scm_raise_code(EC_WRONG_NUMBER_OF_ARGUMENTS,
                        "define-record-type: ill-formed special form");
    val_t ctor_form    = vcadr(rest);
    val_t pred_sym     = vcaddr(rest);
    val_t field_specs  = vcdr(vcddr(rest));

    /* `(define-record-type point ctor-name point? ...)` -- ctor_form
     * itself not a `(ctor-name field...)` list -- previously reached
     * vcar(ctor_form)/vcdr(ctor_form) below on a non-pair. */
    if (!vis_pair(ctor_form))
        scm_raise_code(EC_WRONG_NUMBER_OF_ARGUMENTS,
                        "define-record-type: ill-formed special form");

    /* `(define-record-type point (mk-point x) point? y)` -- a bare
     * field-spec, not `(field-name getter [setter])` -- previously
     * reached vcar(vcar(fs)) (the nfields-counting loop below) and
     * vcadr(fspec) (the binding-building loop further down) on a
     * non-pair. Both loops re-derive fspec from the same field_specs
     * list, so validating once up front covers both. */
    for (val_t fchk = field_specs; vis_pair(fchk); fchk = vcdr(fchk))
        if (!vis_pair(vcar(fchk)) || !vis_pair(vcdr(vcar(fchk))))
            scm_raise_code(EC_WRONG_NUMBER_OF_ARGUMENTS,
                            "define-record-type: ill-formed field spec");

    uint32_t nfields = (uint32_t)rp_list_length(field_specs);
    RecordType *rtd = (RecordType *)gc_alloc_pinned(sizeof(RecordType) + nfields * sizeof(val_t));
    rtd->hdr.type = T_RECORD_TYPE; rtd->hdr.flags = 0;
    rtd->name = name_sym; rtd->nfields = nfields;
    rtd->constructor = V_FALSE; rtd->predicate = V_FALSE;
    rtd->accessors = (val_t *)gc_alloc_raw_pinned(nfields * sizeof(val_t));
    rtd->mutators  = (val_t *)gc_alloc_raw_pinned(nfields * sizeof(val_t));
    for (uint32_t k = 0; k < nfields; k++) { rtd->accessors[k] = V_FALSE; rtd->mutators[k] = V_FALSE; }
    val_t fs = field_specs; uint32_t fi = 0;
    while (vis_pair(fs)) { rtd->field_names[fi++] = vcar(vcar(fs)); fs = vcdr(fs); }

    val_t rtd_val = vptr(rtd);
    spec->rtd_val = rtd_val;
    val_t rtd_ref_expr = vis_false(rtd_ref)
        ? rp_cons(S_QUOTE, rp_cons(rtd_val, V_NIL)) : rtd_ref;

    val_t ctor_name   = vcar(ctor_form);
    val_t ctor_fields = vcdr(ctor_form);

    spec->bindings = (RecordBinding *)gc_alloc_raw_pinned(
        (size_t)(2 + 2 * (int)nfields) * sizeof(RecordBinding));
    int n = 0;

    /* Constructor */
    spec->bindings[n].name   = ctor_name;
    spec->bindings[n].params = ctor_fields;
    spec->bindings[n].body   = rp_ctor_body(rtd_ref_expr, ctor_fields);
    spec->bindings[n].role   = RTD_ROLE_CONSTRUCTOR;
    n++;

    /* Predicate */
    spec->bindings[n].name   = pred_sym;
    spec->bindings[n].params = rp_cons(x_sym, V_NIL);
    spec->bindings[n].body   = rp_pred_body(rtd_ref_expr, x_sym);
    spec->bindings[n].role   = RTD_ROLE_PREDICATE;
    n++;

    /* Field accessors and mutators */
    fs = field_specs; fi = 0;
    while (vis_pair(fs)) {
        val_t fspec       = vcar(fs);
        /* fspec = (field-name getter [setter]) */
        val_t getter_name = vcadr(fspec);
        val_t fi_val      = vfix((intptr_t)fi);

        spec->bindings[n].name        = getter_name;
        spec->bindings[n].params      = rp_cons(x_sym, V_NIL);
        spec->bindings[n].body        = rp_getter_body(x_sym, fi_val);
        spec->bindings[n].role        = RTD_ROLE_ACCESSOR;
        spec->bindings[n].field_index = (int)fi;
        n++;

        if (vis_pair(vcddr(fspec))) {
            val_t setter_name = vcaddr(fspec);
            spec->bindings[n].name        = setter_name;
            spec->bindings[n].params      = rp_cons(x_sym, rp_cons(v_sym, V_NIL));
            spec->bindings[n].body        = rp_setter_body(x_sym, fi_val, v_sym);
            spec->bindings[n].role        = RTD_ROLE_MUTATOR;
            spec->bindings[n].field_index = (int)fi;
            n++;
        }
        fi++; fs = vcdr(fs);
    }
    spec->count = n;
}
