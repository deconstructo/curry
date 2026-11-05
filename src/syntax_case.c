#include "syntax_case.h"
#include "syntax_rules.h"
#include "object.h"
#include "symbol.h"
#include "gc.h"
#include "eval.h"
#include "env.h"
#include "builtins.h"
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

/* ---- Names the generated code uses ----
 *
 * Generated code refers to these through ordinary global lookups, so it
 * works wherever the core builtins are visible. The "\x01" in the two
 * variable names cannot be produced by the reader, so no user variable can
 * collide with them. */

static val_t SC_ENV;     /* %sc\x01env : the list of pattern-variable frames in scope */
static val_t SC_V;       /* %sc\x01v   : the value syntax-case is matching */
static val_t SC_M;       /* %sc\x01m   : the current clause's match frame */
static val_t SC_MATCH, SC_EXTEND, SC_INSTANTIATE, SC_NO_MATCH, SC_CONS, SC_LIST;

static _Atomic long sc_mark_counter = 0;
static _Atomic long sc_temp_counter = 0;

/* The mark of the expansion currently running on this thread; 0 outside
 * any expansion (e.g. #'x typed at the REPL). */
static CURRY_THREAD_LOCAL long sc_cur_mark = 0;

/* Where the transformer being applied was defined: the environment a name
 * a template introduces is checked against to tell a reference to something
 * real (`list`, a helper) from a binder to rename. A tree-walked closure
 * knows its own; for compiled code the compiler has set the current
 * environment to the chunk's target (never a use-site local). */
static CURRY_THREAD_LOCAL val_t sc_cur_def_env = V_FALSE;

/* ---- Syntax objects ---- */

static bool sc_is_ident(val_t v) { return vis_symbol(v) || vis_synobj(v); }

static val_t sc_make_synobj(val_t name, long mark) {
    SynObj *o = CURRY_NEW(SynObj);
    o->hdr.type = T_SYNTAX_OBJ; o->hdr.flags = 0;
    o->name = name;
    o->mark = vfix(mark);
    return vptr(o);
}

/* The ordinary symbol a syntax object stands for once its expansion is
 * over. Mark 0 keeps the source name; any other mark gets a name no user
 * can spell, one per (name, mark), so two expansions never share a binder. */
static val_t sc_resolve(val_t v) {
    SynObj *o = as_synobj(v);
    long mark = (long)vunfix(o->mark);
    if (mark == 0) return o->name;
    char buf[200];
    snprintf(buf, sizeof buf, "%s\x01" "c%ld", sym_cstr(o->name), mark);
    return sym_intern_cstr(buf);
}

/* ---- Small list builder for generated code ---- */

static val_t sc_list(int n, ...) {
    val_t items[16];
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n && i < 16; i++) items[i] = va_arg(ap, val_t);
    va_end(ap);
    val_t r = V_NIL;
    for (int i = n - 1; i >= 0; i--) r = scm_cons(items[i], r);
    return r;
}

static val_t sc_quote(val_t x) { return sc_list(2, S_QUOTE, x); }

/* ---- Walking data: has/strip/syntax->datum/datum->syntax ---- */

static bool sc_contains_synobj(val_t v) {
    check_c_stack_depth("syntax");
    for (;;) {
        if (vis_synobj(v)) return true;
        if (vis_vector(v)) {
            Vector *vec = as_vec(v);
            for (uint32_t i = 0; i < vec->len; i++)
                if (sc_contains_synobj(vec->data[i])) return true;
            return false;
        }
        if (!vis_pair(v)) return false;
        if (sc_contains_synobj(vcar(v))) return true;
        v = vcdr(v);
    }
}

/* Rebuilds v with every syntax object replaced by f(object). Cells keep
 * their source-line flags; the caller has already checked that v contains
 * at least one syntax object. */
typedef val_t (*SynMap)(val_t synobj, void *ud);
typedef val_t (*SymMap)(val_t sym, void *ud);

static val_t sc_rebuild(val_t v, SynMap on_syn, SymMap on_sym, void *ud) {
    check_c_stack_depth("syntax");
    if (vis_synobj(v)) return on_syn ? on_syn(v, ud) : v;
    if (vis_symbol(v)) return on_sym ? on_sym(v, ud) : v;
    if (vis_vector(v)) {
        Vector *src = as_vec(v);
        Vector *dst = CURRY_NEW_FLEX(Vector, src->len);
        dst->hdr.type = T_VECTOR; dst->hdr.flags = src->hdr.flags; dst->len = src->len;
        for (uint32_t i = 0; i < src->len; i++)
            dst->data[i] = sc_rebuild(src->data[i], on_syn, on_sym, ud);
        return vptr(dst);
    }
    if (!vis_pair(v)) return v;
    val_t head = V_NIL, tail = V_NIL;
    for (;;) {
        Pair *cell = as_pair(v);
        val_t nc = scm_cons(sc_rebuild(cell->car, on_syn, on_sym, ud), V_NIL);
        as_pair(nc)->hdr.flags = cell->hdr.flags;
        if (vis_nil(tail)) head = nc; else as_pair(tail)->cdr = nc;
        tail = nc;
        v = cell->cdr;
        if (!vis_pair(v)) {
            if (!vis_nil(v)) as_pair(tail)->cdr = sc_rebuild(v, on_syn, on_sym, ud);
            return head;
        }
    }
}

static val_t on_resolve(val_t o, void *ud) { (void)ud; return sc_resolve(o); }
static val_t on_name(val_t o, void *ud)    { (void)ud; return as_synobj(o)->name; }

/* Datum position (inside quote, a case clause's datums, quasiquoted data):
 * a syntax object is just the symbol it stands for. */
static val_t sc_strip_datum(val_t v) {
    return sc_contains_synobj(v) ? sc_rebuild(v, on_name, NULL, NULL) : v;
}

static bool sc_form2(val_t v, val_t head) {
    return vis_pair(v) && vcar(v) == head && scm_list_length(v) == 2;
}

static val_t sc_strip_code(val_t v);

/* Inside a quasiquote template only the unquoted parts are code. */
static val_t sc_strip_qq(val_t v, int level) {
    check_c_stack_depth("syntax");
    if (!sc_contains_synobj(v)) return v;
    if (sc_form2(v, S_UNQUOTE) || sc_form2(v, S_UNQUOTE_SPLICING)) {
        val_t arg = level == 1 ? sc_strip_code(vcadr(v)) : sc_strip_qq(vcadr(v), level - 1);
        return sc_list(2, vcar(v), arg);
    }
    if (sc_form2(v, S_QUASIQUOTE))
        return sc_list(2, S_QUASIQUOTE, sc_strip_qq(vcadr(v), level + 1));
    if (vis_synobj(v)) return as_synobj(v)->name;
    if (vis_vector(v)) {
        Vector *src = as_vec(v);
        Vector *dst = CURRY_NEW_FLEX(Vector, src->len);
        dst->hdr.type = T_VECTOR; dst->hdr.flags = src->hdr.flags; dst->len = src->len;
        for (uint32_t i = 0; i < src->len; i++) dst->data[i] = sc_strip_qq(src->data[i], level);
        return vptr(dst);
    }
    val_t head = V_NIL, tail = V_NIL;
    for (;;) {
        Pair *cell = as_pair(v);
        val_t nc = scm_cons(sc_strip_qq(cell->car, level), V_NIL);
        as_pair(nc)->hdr.flags = cell->hdr.flags;
        if (vis_nil(tail)) head = nc; else as_pair(tail)->cdr = nc;
        tail = nc;
        v = cell->cdr;
        if (!vis_pair(v)) {
            if (!vis_nil(v)) as_pair(tail)->cdr = sc_strip_qq(v, level);
            return head;
        }
        /* (a . ,b) reads as (a unquote b): the tail is itself an unquote. */
        if (sc_form2(v, S_UNQUOTE) || sc_form2(v, S_UNQUOTE_SPLICING)) {
            as_pair(tail)->cdr = sc_strip_qq(v, level);
            return head;
        }
    }
}

/* Resolves the syntax objects in an expansion's result so the compilers see
 * only symbols. Quoted data keeps its source names; everything in code
 * position gets the per-expansion name. */
static val_t sc_strip_code(val_t v) {
    check_c_stack_depth("syntax");
    if (!sc_contains_synobj(v)) return v;
    if (vis_synobj(v)) return sc_resolve(v);
    if (vis_vector(v)) return sc_rebuild(v, on_resolve, NULL, NULL);
    if (!vis_pair(v)) return v;
    if (sc_form2(v, S_QUOTE))
        return sc_list(2, S_QUOTE, sc_strip_datum(vcadr(v)));
    if (sc_form2(v, S_QUASIQUOTE))
        return sc_list(2, S_QUASIQUOTE, sc_strip_qq(vcadr(v), 1));
    bool is_case = vcar(v) == S_CASE;
    val_t head = V_NIL, tail = V_NIL;
    int index = 0;
    for (;;) {
        Pair *cell = as_pair(v);
        val_t elem = cell->car;
        /* (case key ((datum ...) expr ...) ...): the datum lists are data. */
        if (is_case && index >= 2 && vis_pair(elem)) {
            val_t data = vcar(elem);
            val_t d = vis_pair(data) || vis_synobj(data) ? sc_strip_datum(data) : data;
            elem = scm_cons(d, sc_strip_code(vcdr(elem)));
        } else {
            elem = sc_strip_code(elem);
        }
        val_t nc = scm_cons(elem, V_NIL);
        as_pair(nc)->hdr.flags = cell->hdr.flags;
        if (vis_nil(tail)) head = nc; else as_pair(tail)->cdr = nc;
        tail = nc;
        index++;
        v = cell->cdr;
        if (!vis_pair(v)) {
            if (!vis_nil(v)) as_pair(tail)->cdr = sc_strip_code(v);
            return head;
        }
    }
}

static val_t sc_strip(val_t v) { return sc_strip_code(v); }

static val_t on_mark_sym(val_t sym, void *ud) { return sc_make_synobj(sym, *(long *)ud); }

/* ---- The macro-apply boundary ---- */

val_t sc_apply_transformer(val_t transformer, val_t form) {
    if (vis_prim(transformer))
        return apply(transformer, scm_cons(form, V_NIL));

    long saved = sc_cur_mark;
    val_t saved_env = sc_cur_def_env;
    sc_cur_mark = (long)atomic_fetch_add(&sc_mark_counter, 1) + 1;
    if (vis_closure(transformer))
        sc_cur_def_env = vptr(as_clos(transformer)->env);
    else
        sc_cur_def_env = sr_get_current_env();
    val_t out = V_VOID;
    ExnHandler h;
    SCM_PROTECT(h,
        out = apply(transformer, scm_cons(form, V_NIL)),
        { sc_cur_mark = saved; sc_cur_def_env = saved_env; scm_raise_val(h.exn); });
    sc_cur_mark = saved;
    sc_cur_def_env = saved_env;
    return sc_strip(out);
}

/* ---- Procedures ---- */

/* (%sc-match form pattern literals) -> #f, or a frame (bindings . ell-bindings) */
static val_t prim_sc_match(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t b = V_NIL, eb = V_NIL;
    if (!sr_match_pattern(av[1], av[0], av[2], sr_default_ellipsis(), &b, &eb))
        return V_FALSE;
    return scm_cons(b, eb);
}

/* (%sc-extend frame env) -> env with frame innermost */
static val_t prim_sc_extend(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    return scm_cons(av[0], av[1]);
}

static bool sc_name_in(val_t name, val_t alist) {
    for (; vis_pair(alist); alist = vcdr(alist))
        if (vis_pair(vcar(alist)) && vcar(vcar(alist)) == name) return true;
    return false;
}

/* Adds frame's entries to *acc, skipping any name an inner frame already
 * bound (in either table): an inner pattern variable shadows an outer one
 * of the same name whatever its ellipsis depth. */
static void sc_merge_frame(val_t frame, val_t *b, val_t *eb, val_t seen_b, val_t seen_eb) {
    if (!vis_pair(frame)) return;
    for (val_t e = vcar(frame); vis_pair(e); e = vcdr(e)) {
        val_t entry = vcar(e);
        if (!vis_pair(entry)) continue;
        if (sc_name_in(vcar(entry), seen_b) || sc_name_in(vcar(entry), seen_eb)) continue;
        *b = scm_cons(entry, *b);
    }
    for (val_t e = vcdr(frame); vis_pair(e); e = vcdr(e)) {
        val_t entry = vcar(e);
        if (!vis_pair(entry)) continue;
        if (sc_name_in(vcar(entry), seen_b) || sc_name_in(vcar(entry), seen_eb)) continue;
        *eb = scm_cons(entry, *eb);
    }
}

static val_t sc_mk_ident(val_t sym, void *ud) {
    (void)ud;
    return sc_make_synobj(sym, sc_cur_mark);
}

/* (%sc-instantiate template env [frame]) : the value of (syntax template) */
static val_t prim_sc_instantiate(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t b = V_NIL, eb = V_NIL;
    /* Innermost first, so an inner variable hides an outer one. */
    val_t seen_b = V_NIL, seen_eb = V_NIL;
    val_t frames = av[1];
    if (ac > 2 && vis_pair(av[2])) frames = scm_cons(av[2], frames);
    for (val_t f = frames; vis_pair(f); f = vcdr(f)) {
        val_t fb = V_NIL, feb = V_NIL;
        sc_merge_frame(vcar(f), &fb, &feb, seen_b, seen_eb);
        for (val_t e = fb;  vis_pair(e); e = vcdr(e)) { b  = scm_cons(vcar(e), b);  seen_b  = scm_cons(vcar(e), seen_b); }
        for (val_t e = feb; vis_pair(e); e = vcdr(e)) { eb = scm_cons(vcar(e), eb); seen_eb = scm_cons(vcar(e), seen_eb); }
    }
    val_t env = sc_cur_def_env;
    if (env == V_FALSE || env == V_VOID) env = sr_get_current_env();
    if (env == V_FALSE || env == V_VOID) env = GLOBAL_ENV;
    return sr_instantiate(av[0], b, eb, sr_default_ellipsis(), env,
                          sr_get_current_local_macros(), sc_mk_ident, NULL);
}

static val_t prim_sc_no_match(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud; (void)av;
    scm_raise(V_FALSE, "syntax-case: no matching clause");
}

static val_t prim_sc_cons(int ac, val_t *av, void *ud) { (void)ac; (void)ud; return scm_cons(av[0], av[1]); }
static val_t prim_sc_list(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t r = V_NIL;
    for (int i = ac - 1; i >= 0; i--) r = scm_cons(av[i], r);
    return r;
}

static val_t prim_syntax_to_datum(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    return sc_contains_synobj(av[0]) ? sc_rebuild(av[0], on_name, NULL, NULL) : av[0];
}

/* (datum->syntax context datum): the symbols of datum take the context
 * identifier's mark, so a macro can introduce a name its caller can see
 * (or, with an introduced context, one it cannot). A use-site identifier
 * carries no mark, so datum comes back unchanged. */
static val_t prim_datum_to_syntax(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_synobj(av[0])) return av[1];
    long mark = (long)vunfix(as_synobj(av[0])->mark);
    if (mark == 0) return av[1];
    /* sc_rebuild only walks into containers, so wrap the symbol case here. */
    if (vis_symbol(av[1])) return sc_make_synobj(av[1], mark);
    return sc_rebuild(av[1], NULL, on_mark_sym, &mark);
}

static val_t prim_identifier_p(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    return vbool(sc_is_ident(av[0]));
}

static long sc_ident_mark(val_t v) { return vis_synobj(v) ? (long)vunfix(as_synobj(v)->mark) : 0; }
static val_t sc_ident_name(val_t v) { return vis_synobj(v) ? as_synobj(v)->name : v; }

static val_t prim_bound_identifier_eq(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!sc_is_ident(av[0]) || !sc_is_ident(av[1]))
        scm_raise(V_FALSE, "bound-identifier=?: expected two identifiers");
    return vbool(sc_ident_name(av[0]) == sc_ident_name(av[1]) &&
                 sc_ident_mark(av[0]) == sc_ident_mark(av[1]));
}

/* Compares names only, for now: resolving two identifiers to their bindings
 * needs the scope information the later phases add. */
static val_t prim_free_identifier_eq(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!sc_is_ident(av[0]) || !sc_is_ident(av[1]))
        scm_raise(V_FALSE, "free-identifier=?: expected two identifiers");
    return vbool(sc_ident_name(av[0]) == sc_ident_name(av[1]));
}

static val_t prim_generate_temporaries(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    int n = scm_list_length(av[0]);
    if (n < 0) scm_raise(V_FALSE, "generate-temporaries: expected a list");
    val_t r = V_NIL;
    for (int i = 0; i < n; i++) {
        char buf[64];
        snprintf(buf, sizeof buf, "tmp\x01g%ld", (long)atomic_fetch_add(&sc_temp_counter, 1));
        r = scm_cons(sym_intern_cstr(buf), r);
    }
    return r;
}

static const char *sc_text(val_t v, char *buf, size_t n) {
    if (vis_string(v)) { snprintf(buf, n, "%.*s", (int)as_str(v)->len, str_data(as_str(v))); return buf; }
    if (vis_symbol(v)) return sym_cstr(v);
    if (vis_synobj(v)) return sym_cstr(as_synobj(v)->name);
    return NULL;
}

/* (syntax-violation who message form [subform]) */
static val_t prim_syntax_violation(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    char wbuf[256], mbuf[512];
    const char *who = sc_text(av[0], wbuf, sizeof wbuf);
    const char *msg = sc_text(av[1], mbuf, sizeof mbuf);
    if (!msg) msg = "invalid syntax";
    if (who) scm_raise(V_FALSE, "%s: %s", who, msg);
    scm_raise(V_FALSE, "%s", msg);
}

/* ---- Macros (T_SYNTAX transformers: whole form in, expansion out) ---- */

static void sc_syntax_error(const char *who, const char *what) {
    scm_raise(V_FALSE, "%s: %s", who, what);
}

/* (let ((%sc\x01env (%sc-extend %sc\x01m %sc\x01env))) body): body runs with the
 * current clause's pattern variables innermost. */
static val_t sc_push_frame(val_t body) {
    return sc_list(3, S_LET,
                   sc_list(1, sc_list(2, SC_ENV, sc_list(3, SC_EXTEND, SC_M, SC_ENV))),
                   body);
}

/* (syntax-case expr (literal ...) clause ...) where clause is
 * (pattern body) or (pattern fender body). Expands to nested lets that try
 * each clause in turn; the clause's pattern variables ride along in the
 * lexical variable %sc\x01env so that syntax templates in the body (and in
 * nested syntax-case forms) can find them. */
static val_t mac_syntax_case(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t form = av[0];
    int n = scm_list_length(form);
    if (n < 3) sc_syntax_error("syntax-case", "expected (syntax-case expr (literal ...) clause ...)");
    val_t expr = vcadr(form);
    val_t lits = vcaddr(form);
    if (scm_list_length(lits) < 0) sc_syntax_error("syntax-case", "the literals must be a list");
    for (val_t l = lits; vis_pair(l); l = vcdr(l))
        if (!vis_symbol(vcar(l))) sc_syntax_error("syntax-case", "a literal must be an identifier");

    /* Build the chain from the last clause to the first. */
    val_t clauses = vcdr(vcddr(form));
    val_t rev = scm_reverse(clauses);
    val_t next = sc_list(2, SC_NO_MATCH, SC_V);
    for (val_t c = rev; vis_pair(c); c = vcdr(c)) {
        val_t clause = vcar(c);
        int cl = scm_list_length(clause);
        if (cl != 2 && cl != 3) sc_syntax_error("syntax-case", "a clause is (pattern body) or (pattern fender body)");
        val_t pat = vcar(clause);
        val_t body = (cl == 2) ? vcadr(clause) : vcaddr(clause);
        val_t body_form = sc_push_frame(body);
        val_t test;
        if (cl == 3)
            test = sc_list(4, S_IF, SC_M, sc_push_frame(vcadr(clause)), V_FALSE);
        else
            test = SC_M;
        val_t match = sc_list(3, SC_MATCH, SC_V, sc_quote(pat));
        match = scm_append(match, sc_list(1, sc_quote(lits)));
        val_t ifform = sc_list(4, S_IF, test, body_form, next);
        next = sc_list(3, S_LET, sc_list(1, sc_list(2, SC_M, match)), ifform);
    }
    return sc_list(3, S_LET, sc_list(1, sc_list(2, SC_V, expr)), next);
}

/* (syntax template) */
static val_t mac_syntax(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (scm_list_length(av[0]) != 2) sc_syntax_error("syntax", "expected (syntax template)");
    return sc_list(3, SC_INSTANTIATE, sc_quote(vcadr(av[0])), SC_ENV);
}

/* ---- quasisyntax ---- */

typedef struct { val_t holes; val_t splices; long counter; } QsState;

static val_t qs_fresh(QsState *st) {
    char buf[48];
    snprintf(buf, sizeof buf, "%%sc\x01q%ld", st->counter++);
    return sym_intern_cstr(buf);
}

static bool qs_is_form(val_t t, val_t head) {
    return vis_pair(t) && vcar(t) == head && scm_list_length(t) == 2;
}

static val_t qs_walk(val_t t, int level, QsState *st);

static val_t qs_walk_list(val_t t, int level, QsState *st) {
    val_t out = V_NIL;
    bool first = true;
    while (vis_pair(t)) {
        /* (a . #,b) reads as (a unsyntax b): the tail is itself an unsyntax. */
        if (!first && (qs_is_form(t, S_UNSYNTAX) || qs_is_form(t, S_QUASISYNTAX))) break;
        first = false;
        val_t e = vcar(t);
        if (level == 1 && qs_is_form(e, S_UNSYNTAX_SPLICING)) {
            val_t ph = qs_fresh(st);
            st->splices = scm_cons(sc_list(3, SC_CONS, sc_quote(ph), vcadr(e)), st->splices);
            out = scm_cons(sr_default_ellipsis(), scm_cons(ph, out));
        } else {
            out = scm_cons(qs_walk(e, level, st), out);
        }
        t = vcdr(t);
    }
    val_t tail = vis_nil(t) ? V_NIL : qs_walk(t, level, st);
    val_t r = tail;
    for (; vis_pair(out); out = vcdr(out)) r = scm_cons(vcar(out), r);
    return r;
}

static val_t qs_walk(val_t t, int level, QsState *st) {
    check_c_stack_depth("quasisyntax");
    if (qs_is_form(t, S_UNSYNTAX)) {
        if (level == 1) {
            val_t ph = qs_fresh(st);
            st->holes = scm_cons(sc_list(3, SC_CONS, sc_quote(ph), vcadr(t)), st->holes);
            return ph;
        }
        return sc_list(2, S_UNSYNTAX, qs_walk(vcadr(t), level - 1, st));
    }
    if (qs_is_form(t, S_QUASISYNTAX))
        return sc_list(2, S_QUASISYNTAX, qs_walk(vcadr(t), level + 1, st));
    if (vis_pair(t)) return qs_walk_list(t, level, st);
    if (vis_vector(t)) {
        Vector *v = as_vec(t);
        val_t lst = V_NIL;
        for (int i = (int)v->len - 1; i >= 0; i--) lst = scm_cons(v->data[i], lst);
        val_t walked = qs_walk_list(lst, level, st);
        int n = scm_list_length(walked);
        Vector *r = CURRY_NEW_FLEX(Vector, (uint32_t)n);
        r->hdr.type = T_VECTOR; r->hdr.flags = 0; r->len = (uint32_t)n;
        for (int i = 0; i < n; i++, walked = vcdr(walked)) r->data[i] = vcar(walked);
        return vptr(r);
    }
    return t;
}

/* (quasisyntax template): #,e splices one value in, #,@e a list of them.
 * Each hole becomes a placeholder pattern variable bound to e's value. */
static val_t mac_quasisyntax(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (scm_list_length(av[0]) != 2) sc_syntax_error("quasisyntax", "expected (quasisyntax template)");
    QsState st = { V_NIL, V_NIL, 0 };
    val_t tmpl = qs_walk(vcadr(av[0]), 1, &st);
    val_t holes   = scm_cons(SC_LIST, scm_reverse(st.holes));
    val_t splices = scm_cons(SC_LIST, scm_reverse(st.splices));
    val_t frame = sc_list(3, SC_CONS, holes, splices);
    return sc_list(4, SC_INSTANTIATE, sc_quote(tmpl), SC_ENV, frame);
}

/* (with-syntax ((pattern expr) ...) body ...) */
static val_t mac_with_syntax(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t form = av[0];
    if (scm_list_length(form) < 3) sc_syntax_error("with-syntax", "expected (with-syntax ((pattern expr) ...) body ...)");
    val_t binds = vcadr(form);
    if (scm_list_length(binds) < 0) sc_syntax_error("with-syntax", "the bindings must be a list");
    val_t pats = V_NIL, exprs = V_NIL;
    for (val_t b = binds; vis_pair(b); b = vcdr(b)) {
        if (scm_list_length(vcar(b)) != 2) sc_syntax_error("with-syntax", "a binding is (pattern expr)");
        pats  = scm_cons(vcar(vcar(b)), pats);
        exprs = scm_cons(vcadr(vcar(b)), exprs);
    }
    pats = scm_reverse(pats); exprs = scm_reverse(exprs);
    val_t body = scm_cons(S_LET, scm_cons(V_NIL, vcddr(form)));
    val_t subject = scm_cons(SC_LIST, exprs);
    val_t clause = sc_list(2, pats, body);
    return sc_list(4, sym_intern_cstr("syntax-case"), subject, V_NIL, clause);
}

/* ---- Registration ---- */

static void sc_def_macro(val_t env, const char *name, PrimFn fn) {
    Primitive *p = CURRY_NEW_PINNED(Primitive);
    p->hdr.type = T_PRIMITIVE; p->hdr.flags = 0;
    p->name = name; p->min_args = 1; p->max_args = 1;
    p->fn = fn; p->ud = NULL;
    Syntax *syn = CURRY_NEW(Syntax);
    syn->hdr.type = T_SYNTAX; syn->hdr.flags = 0;
    syn->transformer = vptr(p);
    env_define(env, sym_intern_cstr(name), vptr(syn));
}

void syntax_case_register(val_t env) {
    SC_ENV         = sym_intern_cstr("%sc\x01" "env");
    SC_V           = sym_intern_cstr("%sc\x01" "v");
    SC_M           = sym_intern_cstr("%sc\x01" "m");
    SC_MATCH       = sym_intern_cstr("%sc-match");
    SC_EXTEND      = sym_intern_cstr("%sc-extend");
    SC_INSTANTIATE = sym_intern_cstr("%sc-instantiate");
    SC_NO_MATCH    = sym_intern_cstr("%sc-no-match");
    SC_CONS        = sym_intern_cstr("%sc-cons");
    SC_LIST        = sym_intern_cstr("%sc-list");

    /* Outside any syntax-case there are no pattern variables. */
    env_define(env, SC_ENV, V_NIL);

    defprim(env, "%sc-match",       prim_sc_match,       3, 3);
    defprim(env, "%sc-extend",      prim_sc_extend,      2, 2);
    defprim(env, "%sc-instantiate", prim_sc_instantiate, 2, 3);
    defprim(env, "%sc-no-match",    prim_sc_no_match,    1, 1);
    defprim(env, "%sc-cons",        prim_sc_cons,        2, 2);
    defprim(env, "%sc-list",        prim_sc_list,        0, -1);

    defprim(env, "syntax->datum",       prim_syntax_to_datum,        1, 1);
    defprim(env, "datum->syntax",       prim_datum_to_syntax,        2, 2);
    defprim(env, "identifier?",         prim_identifier_p,           1, 1);
    defprim(env, "bound-identifier=?",  prim_bound_identifier_eq,    2, 2);
    defprim(env, "free-identifier=?",   prim_free_identifier_eq,     2, 2);
    defprim(env, "generate-temporaries", prim_generate_temporaries,  1, 1);
    defprim(env, "syntax-violation",    prim_syntax_violation,       3, 4);

    sc_def_macro(env, "syntax-case",  mac_syntax_case);
    sc_def_macro(env, "syntax",       mac_syntax);
    sc_def_macro(env, "quasisyntax",  mac_quasisyntax);
    sc_def_macro(env, "with-syntax",  mac_with_syntax);
}
