#include "eval.h"
#include "object.h"
#include "symbol.h"
#include "numeric.h"
#include "env.h"
#include "port.h"
#include "reader.h"
#include "gc.h"
#include "akkadian.h"
#include "akkadian_eval.h"
#include "symbolic.h"
#include "quantum.h"
#include "surreal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>

/* ---- Exception handling ---- */

_Thread_local ExnHandler *current_handler = NULL;

void scm_raise_val(val_t exn) {
    if (current_handler) {
        current_handler->exn = exn;
        longjmp(current_handler->jmp, 1);
    }
    /* Unhandled: print and abort */
    fprintf(stderr, "Unhandled exception: ");
    scm_write(exn, PORT_STDERR);
    fprintf(stderr, "\n");
    abort();
}

void scm_raise(val_t kind, const char *fmt, ...) {
    (void)kind;
    char msg[512];
    va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);

    /* Prepend Akkadian preamble — as the scribes demanded */
    char preamble[256];
    akkadian_preamble(preamble, sizeof(preamble), msg);
    char full[800];
    snprintf(full, sizeof(full), "%s:\n  %s", preamble, msg);

    /* Build error object */
    ErrorObj *e = CURRY_NEW(ErrorObj);
    e->hdr.type  = T_ERROR;
    e->hdr.flags = 0;

    uint32_t len = (uint32_t)strlen(full);
    String *s = (String *)gc_alloc_atomic(sizeof(String) + len + 1);
    s->hdr.type=T_STRING; s->hdr.flags=0; s->len=len; s->hash=0;
    memcpy(s->data, full, len+1);
    e->message   = vptr(s);
    e->irritants = V_NIL;
    e->kind      = S_ERROR;
    scm_raise_val(vptr(e));
}

/* ---- Helpers ---- */

static val_t make_pair(val_t car, val_t cdr) {
    Pair *p = CURRY_NEW(Pair);
    p->hdr.type = T_PAIR; p->hdr.flags = 0;
    p->car = car; p->cdr = cdr;
    return vptr(p);
}

static int list_length(val_t lst) {
    int n = 0;
    while (vis_pair(lst)) { n++; lst = vcdr(lst); }
    return n;
}

/* Collect a list into a C array; returns count */
static int list_to_arr(val_t lst, val_t *arr, int max) {
    int n = 0;
    while (vis_pair(lst) && n < max) {
        arr[n++] = vcar(lst);
        lst = vcdr(lst);
    }
    return n;
}

/* ---- Quasiquote expansion ---- */

static val_t expand_qq(val_t form, val_t env, int depth);

static val_t expand_qq(val_t form, val_t env, int depth) {
    if (!vis_pair(form)) return make_pair(S_QUOTE, make_pair(form, V_NIL));

    val_t head = vcar(form);
    val_t rest = vcdr(form);

    if (head == S_UNQUOTE) {
        if (depth == 0) return vcar(rest);
        return make_pair(sym_intern_cstr("list"), make_pair(
            make_pair(S_QUOTE, make_pair(S_UNQUOTE, V_NIL)),
            make_pair(expand_qq(vcar(rest), env, depth-1), V_NIL)));
    }
    if (head == S_QUASIQUOTE) {
        return make_pair(sym_intern_cstr("list"), make_pair(
            make_pair(S_QUOTE, make_pair(S_QUASIQUOTE, V_NIL)),
            make_pair(expand_qq(vcar(rest), env, depth+1), V_NIL)));
    }

    /* Check for ,@ (unquote-splicing) in car */
    if (vis_pair(head) && vcar(head) == S_UNQUOTE_SPLICING) {
        val_t splice = vcadr(head);
        val_t tail_qq = expand_qq(rest, env, depth);
        return make_pair(sym_intern_cstr("append"),
               make_pair(depth == 0 ? splice : expand_qq(splice, env, depth-1),
               make_pair(tail_qq, V_NIL)));
    }

    val_t car_qq = expand_qq(head, env, depth);
    val_t cdr_qq = expand_qq(rest, env, depth);
    return make_pair(sym_intern_cstr("cons"), make_pair(car_qq, make_pair(cdr_qq, V_NIL)));
}

/* ---- Let helpers ---- */


/* ---- Evaluator ---- */

val_t eval(val_t expr, val_t env) {
tail:
    /* Non-pointer immediates: fixnum (tag=01), char (tag=10), bool/nil/void/eof (tag=11) */
    if (expr & 3) return expr;
    /* Heap object: dispatch on type */
    {
        uint32_t t = ((Hdr *)(void *)expr)->type;
        if (t == T_SYMBOL) return env_lookup(env, expr);
        if (t != T_PAIR)   return expr;   /* string, number, vector, etc. — self-eval */
    }

    val_t op   = akk_translate(vcar(expr));   /* Akkadian/cuneiform → English */
    val_t rest = vcdr(expr);

    /* ---- Special forms ---- */

    if (op == S_QUOTE) {
        return vis_pair(rest) ? vcar(rest) : V_VOID;
    }

    if (op == S_IF) {
        val_t cond = eval(vcar(rest), env);
        rest = vcdr(rest);
        if (vis_true(cond)) {
            expr = vcar(rest); goto tail;
        } else {
            val_t alt = vcdr(rest);
            if (vis_nil(alt)) return V_VOID;
            expr = vcar(alt); goto tail;
        }
    }

    if (op == S_LAMBDA) {
        Closure *c = CURRY_NEW(Closure);
        c->hdr.type  = T_CLOSURE;
        c->hdr.flags = 0;
        c->params = vcar(rest);
        c->body   = vcdr(rest);
        c->env    = as_env(env);
        c->name   = V_FALSE;
        return vptr(c);
    }

    if (op == S_BEGIN) {
        if (vis_nil(rest)) return V_VOID;
        while (vis_pair(vcdr(rest))) {
            eval(vcar(rest), env);
            rest = vcdr(rest);
        }
        expr = vcar(rest); goto tail;
    }

    if (op == S_DEFINE) {
        val_t name_form = vcar(rest);
        val_t val;
        if (vis_symbol(name_form)) {
            /* (define name expr) */
            val = vis_nil(vcdr(rest)) ? V_VOID : eval(vcadr(rest), env);
            if (vis_closure(val) && vis_false(as_clos(val)->name))
                as_clos(val)->name = name_form;
        } else if (vis_pair(name_form)) {
            /* (define (name params...) body...) */
            val_t name   = vcar(name_form);
            val_t params = vcdr(name_form);
            Closure *c   = CURRY_NEW(Closure);
            c->hdr.type  = T_CLOSURE; c->hdr.flags = 0;
            c->params = params;
            c->body   = vcdr(rest);
            c->env    = as_env(env);
            c->name   = name;
            val  = vptr(c);
            name_form = name;
        } else {
            scm_raise(V_FALSE, "invalid define form");
        }
        env_define(env, name_form, val);
        return V_VOID;
    }

    if (op == S_SET) {
        val_t sym = vcar(rest);
        val_t val = eval(vcadr(rest), env);
        if (!env_set(env, sym, val))
            scm_raise(V_FALSE, "set!: unbound variable: %s", sym_cstr(sym));
        return V_VOID;
    }

    if (op == S_DEFINE_VALUES) {
        /* (define-values (var...) expr) */
        val_t vars = vcar(rest);
        val_t val  = eval(vcadr(rest), env);
        if (!vis_values(val)) {
            /* Single value -> first var */
            if (vis_pair(vars)) env_define(env, vcar(vars), val);
        } else {
            Values *mv = as_vals(val);
            int i = 0;
            while (vis_pair(vars) && (uint32_t)i < mv->count) {
                env_define(env, vcar(vars), mv->vals[i++]);
                vars = vcdr(vars);
            }
        }
        return V_VOID;
    }

    /* (let ((x v) ...) body...) */
    if (op == S_LET) {
        val_t bindings = vcar(rest);
        val_t body     = vcdr(rest);

        /* Named let: (let name ((var init)...) body...) */
        if (vis_symbol(bindings)) {
            val_t loop_name = bindings;
            bindings = vcar(body);
            body     = vcdr(body);

            val_t new_env = env_extend(env);
            /* Collect params and inits */
            val_t params = V_NIL, inits_list = V_NIL;
            val_t b = bindings;
            while (vis_pair(b)) {
                params      = make_pair(vcar(vcar(b)), params);
                inits_list  = make_pair(vcadr(vcar(b)), inits_list);
                b = vcdr(b);
            }
            /* Reverse both */
            val_t rp = V_NIL; b = params;
            while (vis_pair(b)) { rp = make_pair(vcar(b), rp); b = vcdr(b); }
            params = rp;
            val_t ri = V_NIL; b = inits_list;
            while (vis_pair(b)) { ri = make_pair(vcar(b), ri); b = vcdr(b); }
            inits_list = ri;

            Closure *c = CURRY_NEW(Closure);
            c->hdr.type=T_CLOSURE; c->hdr.flags=0;
            c->params=params; c->body=body; c->env=as_env(new_env); c->name=loop_name;
            env_define(new_env, loop_name, vptr(c));

            /* Evaluate inits in original env */
            val_t arg_vals = V_NIL;
            b = inits_list;
            while (vis_pair(b)) {
                arg_vals = make_pair(eval(vcar(b), env), arg_vals); b = vcdr(b);
            }
            /* Reverse */
            val_t ra = V_NIL; b = arg_vals;
            while (vis_pair(b)) { ra = make_pair(vcar(b), ra); b = vcdr(b); }
            arg_vals = ra;

            /* Call: TCO via tail position */
            env = env_bind_args(new_env, params, arg_vals);
            expr = make_pair(S_BEGIN, body); goto tail;
        }

        /* Regular let: evaluate inits in current env */
        val_t new_env = env_extend(env);
        val_t b = bindings;
        while (vis_pair(b)) {
            val_t bind = vcar(b);
            val_t v    = eval(vcadr(bind), env);
            env_define(new_env, vcar(bind), v);
            b = vcdr(b);
        }
        env = new_env;
        if (vis_nil(body)) return V_VOID;
        while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
        expr = vcar(body); goto tail;
    }

    if (op == S_LET_STAR) {
        val_t bindings = vcar(rest), body = vcdr(rest);
        val_t cur_env = env_extend(env);
        while (vis_pair(bindings)) {
            val_t bind = vcar(bindings);
            env_define(cur_env, vcar(bind), eval(vcadr(bind), cur_env));
            bindings = vcdr(bindings);
        }
        env = cur_env;
        while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
        expr = vcar(body); goto tail;
    }

    if (op == S_LETREC || op == S_LETREC_STAR) {
        val_t bindings = vcar(rest), body = vcdr(rest);
        val_t new_env = env_extend(env);
        /* Pre-define all vars as undefined */
        val_t b = bindings;
        while (vis_pair(b)) { env_define(new_env, vcar(vcar(b)), V_UNDEF); b = vcdr(b); }
        /* Evaluate and set */
        b = bindings;
        while (vis_pair(b)) {
            val_t sym = vcar(vcar(b));
            val_t v   = eval(vcadr(vcar(b)), new_env);
            env_set(new_env, sym, v);
            b = vcdr(b);
        }
        env = new_env;
        while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
        expr = vcar(body); goto tail;
    }

    if (op == S_AND) {
        val_t v = V_TRUE;
        while (vis_pair(rest)) {
            if (vis_nil(vcdr(rest))) { expr = vcar(rest); goto tail; }
            v = eval(vcar(rest), env);
            if (vis_false(v)) return V_FALSE;
            rest = vcdr(rest);
        }
        return v;
    }

    if (op == S_OR) {
        while (vis_pair(rest)) {
            if (vis_nil(vcdr(rest))) { expr = vcar(rest); goto tail; }
            val_t v = eval(vcar(rest), env);
            if (vis_true(v)) return v;
            rest = vcdr(rest);
        }
        return V_FALSE;
    }

    if (op == S_COND) {
        while (vis_pair(rest)) {
            val_t clause = vcar(rest);
            val_t test   = vcar(clause);
            val_t body   = vcdr(clause);
            rest = vcdr(rest);

            if (test == S_ELSE) {
                if (vis_nil(body)) return V_VOID;
                while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
                expr = vcar(body); goto tail;
            }
            val_t result = eval(test, env);
            if (vis_true(result)) {
                if (vis_nil(body)) return result;
                if (vcar(body) == S_ARROW) {
                    /* (test => proc) */
                    val_t proc = eval(vcadr(body), env);
                    return apply(proc, make_pair(result, V_NIL));
                }
                while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
                expr = vcar(body); goto tail;
            }
        }
        return V_VOID;
    }

    if (op == S_CASE) {
        val_t key = eval(vcar(rest), env);
        val_t clauses = vcdr(rest);
        while (vis_pair(clauses)) {
            val_t clause = vcar(clauses);
            val_t datums = vcar(clause);
            val_t body   = vcdr(clause);
            clauses = vcdr(clauses);
            if (datums == S_ELSE) {
                if (vis_pair(body) && vcar(body) == S_ARROW) {
                    return apply(eval(vcadr(body), env), make_pair(key, V_NIL));
                }
                while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
                expr = vcar(body); goto tail;
            }
            /* Check key against each datum using eqv? */
            val_t d = datums;
            while (vis_pair(d)) {
                val_t datum = vcar(d);
                /* eqv?: pointer eq or numeric eq */
                bool match = (datum == key) ||
                    (vis_fixnum(datum) && vis_fixnum(key) && vunfix(datum) == vunfix(key)) ||
                    (vis_char(datum) && vis_char(key) && vunchr(datum) == vunchr(key));
                if (match) {
                    if (vis_nil(body)) return V_VOID;
                    while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
                    expr = vcar(body); goto tail;
                }
                d = vcdr(d);
            }
        }
        return V_VOID;
    }

    if (op == S_WHEN) {
        val_t cond = eval(vcar(rest), env);
        if (vis_false(cond)) return V_VOID;
        val_t body = vcdr(rest);
        while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
        expr = vcar(body); goto tail;
    }

    if (op == S_UNLESS) {
        val_t cond = eval(vcar(rest), env);
        if (vis_true(cond)) return V_VOID;
        val_t body = vcdr(rest);
        while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
        expr = vcar(body); goto tail;
    }

    if (op == S_DO) {
        /* (do ((var init step)...) (test expr...) body...) */
        val_t var_specs = vcar(rest);
        val_t term      = vcadr(rest);
        val_t body      = vcddr(rest);

        val_t do_env = env_extend(env);
        /* Initialize */
        val_t vs = var_specs;
        while (vis_pair(vs)) {
            val_t spec = vcar(vs);
            env_define(do_env, vcar(spec), eval(vcadr(spec), env));
            vs = vcdr(vs);
        }

        while (1) {
            val_t test_expr = vcar(term);
            val_t test_val  = eval(test_expr, do_env);
            if (vis_true(test_val)) {
                /* Termination */
                val_t result = vcdr(term);
                if (vis_nil(result)) return V_VOID;
                while (vis_pair(vcdr(result))) { eval(vcar(result), do_env); result = vcdr(result); }
                expr = vcar(result); env = do_env; goto tail;
            }
            /* Execute body */
            val_t b = body;
            while (vis_pair(b)) { eval(vcar(b), do_env); b = vcdr(b); }
            /* Step: evaluate new values in current do_env, then assign */
            val_t new_vals = V_NIL;
            vs = var_specs;
            while (vis_pair(vs)) {
                val_t spec = vcar(vs);
                val_t step = vis_nil(vcddr(spec)) ? env_lookup(do_env, vcar(spec))
                                                   : eval(vcaddr(spec), do_env);
                new_vals = make_pair(make_pair(vcar(spec), step), new_vals);
                vs = vcdr(vs);
            }
            /* Assign */
            while (vis_pair(new_vals)) {
                env_set(do_env, vcar(vcar(new_vals)), vcdr(vcar(new_vals)));
                new_vals = vcdr(new_vals);
            }
        }
    }

    if (op == S_DELAY) {
        Promise *p = CURRY_NEW(Promise);
        p->hdr.type=T_PROMISE; p->hdr.flags=0;
        p->state = PROMISE_LAZY;
        /* Wrap body in a thunk */
        Closure *c = CURRY_NEW(Closure);
        c->hdr.type=T_CLOSURE; c->hdr.flags=0;
        c->params=V_NIL; c->body=rest; c->env=as_env(env); c->name=V_FALSE;
        p->val = vptr(c);
        return vptr(p);
    }

    if (op == S_DELAY_FORCE) {
        Promise *p = CURRY_NEW(Promise);
        p->hdr.type=T_PROMISE; p->hdr.flags=0;
        p->state = PROMISE_LAZY;
        Closure *c = CURRY_NEW(Closure);
        c->hdr.type=T_CLOSURE; c->hdr.flags=0;
        c->params=V_NIL; c->body=rest; c->env=as_env(env); c->name=V_FALSE;
        p->hdr.flags = 1; /* lazy-force flag */
        p->val = vptr(c);
        return vptr(p);
    }

    if (op == S_QUASIQUOTE) {
        val_t expanded = expand_qq(vcar(rest), env, 0);
        expr = expanded; goto tail;
    }

    if (op == S_DEFINE_SYNTAX) {
        val_t name        = vcar(rest);
        val_t transformer = eval(vcadr(rest), env);
        Syntax *syn = CURRY_NEW(Syntax);
        syn->hdr.type=T_SYNTAX; syn->hdr.flags=0;
        syn->transformer = transformer;
        env_define(env, name, vptr(syn));
        return V_VOID;
    }

    if (op == S_LET_SYNTAX || op == S_LETREC_SYNTAX) {
        val_t bindings = vcar(rest), body = vcdr(rest);
        val_t new_env = env_extend(env);
        while (vis_pair(bindings)) {
            val_t bind = vcar(bindings);
            val_t name = vcar(bind);
            val_t xfm  = eval(vcadr(bind), op == S_LET_SYNTAX ? env : new_env);
            Syntax *syn = CURRY_NEW(Syntax);
            syn->hdr.type=T_SYNTAX; syn->hdr.flags=0; syn->transformer=xfm;
            env_define(new_env, name, vptr(syn));
            bindings = vcdr(bindings);
        }
        env = new_env;
        while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
        expr = vcar(body); goto tail;
    }

    if (op == S_DEFINE_RECORD_TYPE) {
        /* (define-record-type <name> (<constructor> <field>...) <predicate> <field-spec>...) */
        val_t name_sym  = vcar(rest);
        val_t ctor_form = vcadr(rest);
        val_t pred_sym  = vcaddr(rest);
        val_t field_specs = vcdr(vcddr(rest));

        uint32_t nfields = (uint32_t)list_length(field_specs);
        RecordType *rtd = (RecordType *)gc_alloc(sizeof(RecordType) + nfields * sizeof(val_t));
        rtd->hdr.type=T_RECORD_TYPE; rtd->hdr.flags=0;
        rtd->name = name_sym; rtd->nfields = nfields;
        val_t fs = field_specs; uint32_t fi = 0;
        while (vis_pair(fs)) { rtd->field_names[fi++] = vcar(vcar(fs)); fs = vcdr(fs); }

        val_t rtd_val = vptr(rtd);

        /* Constructor */
        val_t ctor_name   = vcar(ctor_form);
        val_t ctor_fields = vcdr(ctor_form);
        int nctor_fields  = list_length(ctor_fields);
        /* We build a primitive that allocates and fills a Record */
        (void)nctor_fields; /* used at runtime via closure */
        /* Use a closure: (lambda ctor_fields (make-record rtd ...)) */
        /* For simplicity, register as a primitive with closure-captured rtd */
        /* Build: (lambda (f0 f1 ...) (apply %make-record (list rtd f0 f1 ...))) */
        /* Simpler: pre-build it as a closure */
        {
            /* Constructor lambda */
            Closure *c = CURRY_NEW(Closure);
            c->hdr.type=T_CLOSURE; c->hdr.flags=0;
            c->params = ctor_fields;
            /* Body: (%record-ctor rtd field0 field1 ...) */
            c->body   = make_pair(
                make_pair(sym_intern_cstr("%record-ctor"),
                    make_pair(make_pair(S_QUOTE, make_pair(rtd_val, V_NIL)),
                              ctor_fields)),
                V_NIL);
            c->env    = as_env(env);
            c->name   = ctor_name;
            env_define(env, ctor_name, vptr(c));
        }

        /* Predicate */
        {
            Closure *c = CURRY_NEW(Closure);
            c->hdr.type=T_CLOSURE; c->hdr.flags=0;
            val_t x_sym = sym_intern_cstr("x");
            c->params = make_pair(x_sym, V_NIL);
            c->body   = make_pair(
                make_pair(sym_intern_cstr("%record-pred?"),
                    make_pair(make_pair(S_QUOTE, make_pair(rtd_val, V_NIL)),
                              make_pair(x_sym, V_NIL))),
                V_NIL);
            c->env    = as_env(env);
            c->name   = pred_sym;
            env_define(env, pred_sym, vptr(c));
        }

        /* Field accessors and mutators */
        fs = field_specs; fi = 0;
        while (vis_pair(fs)) {
            val_t spec     = vcar(fs);
            /* spec = (field-name getter [setter]) */
            val_t getter_name = vcadr(spec);
            val_t x_sym = sym_intern_cstr("x");
            val_t fi_val = vfix((intptr_t)fi);

            Closure *getter = CURRY_NEW(Closure);
            getter->hdr.type=T_CLOSURE; getter->hdr.flags=0;
            getter->params = make_pair(x_sym, V_NIL);
            getter->body   = make_pair(
                make_pair(sym_intern_cstr("%record-ref"),
                    make_pair(x_sym,
                    make_pair(make_pair(S_QUOTE, make_pair(fi_val, V_NIL)), V_NIL))),
                V_NIL);
            getter->env    = as_env(env);
            getter->name   = getter_name;
            env_define(env, getter_name, vptr(getter));

            if (vis_pair(vcddr(spec))) {
                val_t setter_name = vcaddr(spec);
                val_t v_sym = sym_intern_cstr("v");
                Closure *setter = CURRY_NEW(Closure);
                setter->hdr.type=T_CLOSURE; setter->hdr.flags=0;
                setter->params = make_pair(x_sym, make_pair(v_sym, V_NIL));
                setter->body   = make_pair(
                    make_pair(sym_intern_cstr("%record-set!"),
                        make_pair(x_sym,
                        make_pair(make_pair(S_QUOTE, make_pair(fi_val, V_NIL)),
                        make_pair(v_sym, V_NIL)))),
                    V_NIL);
                setter->env    = as_env(env);
                setter->name   = setter_name;
                env_define(env, setter_name, vptr(setter));
            }
            fi++; fs = vcdr(fs);
        }
        return V_VOID;
    }

    if (op == S_INCLUDE) {
        val_t r = V_VOID;
        while (vis_pair(rest)) {
            if (!vis_string(vcar(rest)))
                scm_raise(V_FALSE, "include: filename must be a string");
            r = scm_load(as_str(vcar(rest))->data, env);
            rest = vcdr(rest);
        }
        return r;
    }

    if (op == S_VALUES) {
        int n = list_length(rest);
        if (n == 1) return eval(vcar(rest), env);
        Values *mv = (Values *)gc_alloc(sizeof(Values) + (size_t)n * sizeof(val_t));
        mv->hdr.type=T_VALUES; mv->hdr.flags=0; mv->count=(uint32_t)n;
        for (int i = 0; i < n; i++) { mv->vals[i] = eval(vcar(rest), env); rest = vcdr(rest); }
        return vptr(mv);
    }

    if (op == S_CALL_WITH_VALUES) {
        val_t producer = eval(vcar(rest), env);
        val_t consumer = eval(vcadr(rest), env);
        val_t produced = apply(producer, V_NIL);
        if (vis_values(produced)) {
            Values *mv = as_vals(produced);
            val_t args = V_NIL;
            for (int i = (int)mv->count - 1; i >= 0; i--)
                args = make_pair(mv->vals[i], args);
            return apply(consumer, args);
        }
        return apply(consumer, make_pair(produced, V_NIL));
    }

    if (op == S_CALL_CC || op == S_CALL_WITH_CC) {
        val_t proc = eval(vcar(rest), env);
        /* Allocate escape continuation */
        Continuation *cont = CURRY_NEW(Continuation);
        cont->hdr.type=T_CONTINUATION; cont->hdr.flags=0;
        cont->jmpbuf = gc_alloc(sizeof(jmp_buf));
        cont->result = V_UNDEF;
        ExnHandler h;
        val_t ret;
        h.prev = current_handler; current_handler = &h;
        if (setjmp(*(jmp_buf *)cont->jmpbuf) == 0) {
            ret = apply(proc, make_pair(vptr(cont), V_NIL));
            current_handler = h.prev;
        } else {
            current_handler = h.prev;
            ret = cont->result;
        }
        return ret;
    }

    if (op == S_PARAMETERIZE) {
        /* (parameterize ((param val)...) body...) */
        /* Phase 1 implementation: simple dynamic binding via thread-local stack */
        /* TODO: full dynamic-wind integration */
        val_t bindings = vcar(rest), body = vcdr(rest);
        /* Save old values */
        val_t b = bindings; int n = list_length(b);
        val_t *params_arr = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
        val_t *old_vals   = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
        int i = 0;
        while (vis_pair(b)) {
            val_t p   = eval(vcar(vcar(b)), env);
            val_t v   = eval(vcadr(vcar(b)), env);
            params_arr[i] = p;
            old_vals[i]   = as_param(p)->init;
            as_param(p)->init = v;
            i++; b = vcdr(b);
        }
        val_t result = V_VOID;
        ExnHandler h;
        bool raised = false;
        val_t exn_val = V_VOID;
        h.prev = current_handler; current_handler = &h;
        if (setjmp(h.jmp) == 0) {
            while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
            result = eval(vcar(body), env);
            current_handler = h.prev;
        } else {
            current_handler = h.prev;
            raised = true; exn_val = h.exn;
        }
        /* Restore */
        for (int j = 0; j < i; j++) as_param(params_arr[j])->init = old_vals[j];
        if (raised) scm_raise_val(exn_val);
        return result;
    }

    if (op == S_GUARD) {
        /* (guard (var clause...) body...) */
        val_t var_clauses = vcar(rest);
        val_t var         = vcar(var_clauses);
        val_t clauses     = vcdr(var_clauses);
        val_t body        = vcdr(rest);

        val_t result = V_VOID;
        ExnHandler h;
        h.prev = current_handler; current_handler = &h;
        if (setjmp(h.jmp) == 0) {
            while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
            result = eval(vcar(body), env);
            current_handler = h.prev;
            return result;
        }
        current_handler = h.prev;
        val_t exn = h.exn;

        /* Try each clause */
        val_t guard_env = env_extend(env);
        env_define(guard_env, var, exn);
        val_t cs = clauses;
        while (vis_pair(cs)) {
            val_t clause = vcar(cs);
            val_t test   = vcar(clause);
            val_t cbody  = vcdr(clause);
            cs = vcdr(cs);
            if (test == S_ELSE) {
                while (vis_pair(vcdr(cbody))) { eval(vcar(cbody), guard_env); cbody = vcdr(cbody); }
                return eval(vcar(cbody), guard_env);
            }
            val_t tv = eval(test, guard_env);
            if (vis_true(tv)) {
                if (vis_nil(cbody)) return tv;
                while (vis_pair(vcdr(cbody))) { eval(vcar(cbody), guard_env); cbody = vcdr(cbody); }
                return eval(vcar(cbody), guard_env);
            }
        }
        /* No clause matched: re-raise */
        scm_raise_val(exn);
    }

    if (op == S_IMPORT) {
        /* Handled in modules.c; forward to the module system */
        extern val_t modules_import(val_t spec, val_t env);
        while (vis_pair(rest)) {
            modules_import(vcar(rest), env);
            rest = vcdr(rest);
        }
        return V_VOID;
    }

    if (op == S_DEFINE_LIBRARY) {
        extern val_t modules_define_library(val_t form, val_t env);
        return modules_define_library(expr, env);
    }

    /* ---- (symbolic x y z ...) — bind names as symbolic unknowns ---- */
    if (op == S_SYMBOLIC) {
        val_t p = rest;
        while (vis_pair(p)) {
            val_t name = vcar(p);
            if (!vis_symbol(name))
                scm_raise(V_FALSE, "symbolic: expected symbol, got non-symbol");
            env_define(env, name, sx_make_var(name));
            p = vcdr(p);
        }
        return V_VOID;
    }

    /* ---- Macro / syntax transformer ---- */
    val_t op_val = vis_symbol(op) ? env_lookup(env, op) : eval(op, env);
    if (vis_syntax(op_val)) {
        /* Apply transformer */
        val_t transformed = apply(as_syntax(op_val)->transformer, make_pair(expr, V_NIL));
        expr = transformed; goto tail;
    }

    /* ---- Function application ---- */
    {
        val_t proc = vis_symbol(op) ? op_val : eval(op, env);

        /* Evaluate arguments directly into a stack array — no cons allocation */
        val_t arr[64];
        int argc = 0;
        for (val_t r = rest; vis_pair(r) && argc < 64; r = vcdr(r))
            arr[argc++] = eval(vcar(r), env);

        if (vis_prim(proc)) {
            Primitive *prim = as_prim(proc);
            if (prim->min_args >= 0 && argc < prim->min_args)
                scm_raise(V_FALSE, "%s: too few arguments (got %d, need %d)",
                          prim->name, argc, prim->min_args);
            if (prim->max_args >= 0 && argc > prim->max_args)
                scm_raise(V_FALSE, "%s: too many arguments (got %d, max %d)",
                          prim->name, argc, prim->max_args);
            return prim->fn(argc, arr, prim->ud);
        }

        if (vis_closure(proc)) {
            Closure *c = as_clos(proc);
            env = env_bind_arr(vptr(c->env), c->params, argc, arr);
            val_t body = c->body;
            while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
            expr = vcar(body); goto tail;
        }

        if (vis_cont(proc)) {
            Continuation *cont = as_cont(proc);
            cont->result = argc > 0 ? arr[0] : V_VOID;
            longjmp(*(jmp_buf *)cont->jmpbuf, 1);
        }

        if (vis_param(proc)) {
            Parameter *p = as_param(proc);
            if (argc == 0) return p->init;
            val_t newval = arr[0];
            if (!vis_false(p->converter)) newval = apply(p->converter, make_pair(newval, V_NIL));
            p->init = newval;
            return V_VOID;
        }

        scm_raise(V_FALSE, "not a procedure: %s",
                  vis_symbol(op) ? sym_cstr(op) : "#<value>");
    }
}

/* ---- Apply ---- */

val_t apply(val_t proc, val_t args) {
    if (vis_prim(proc)) {
        Primitive *prim = as_prim(proc);
        int argc = list_length(args);
        val_t arr[64];
        int n = list_to_arr(args, arr, 64);
        return prim->fn(n, arr, prim->ud);
    }
    if (vis_closure(proc)) {
        Closure *c = as_clos(proc);
        val_t env  = env_bind_args(vptr(c->env), c->params, args);
        return eval_body(c->body, env);
    }
    if (vis_cont(proc)) {
        Continuation *cont = as_cont(proc);
        cont->result = vis_pair(args) ? vcar(args) : V_VOID;
        longjmp(*(jmp_buf *)cont->jmpbuf, 1);
    }
    if (vis_param(proc)) {
        Parameter *p = as_param(proc);
        if (vis_nil(args)) return p->init;
        val_t newval = vcar(args);
        if (!vis_false(p->converter)) newval = apply(p->converter, make_pair(newval, V_NIL));
        p->init = newval;
        return V_VOID;
    }
    scm_raise(V_FALSE, "apply: not a procedure");
}

val_t apply_arr(val_t proc, int argc, val_t *argv) {
    if (vis_prim(proc)) {
        Primitive *prim = as_prim(proc);
        return prim->fn(argc, argv, prim->ud);
    }
    if (vis_closure(proc)) {
        Closure *c = as_clos(proc);
        val_t env = env_bind_arr(vptr(c->env), c->params, argc, argv);
        return eval_body(c->body, env);
    }
    /* cont, param, error — rare paths, build list */
    val_t args = V_NIL;
    for (int i = argc - 1; i >= 0; i--)
        args = make_pair(argv[i], args);
    return apply(proc, args);
}

val_t eval_body(val_t exprs, val_t env) {
    if (vis_nil(exprs)) return V_VOID;
    while (vis_pair(vcdr(exprs))) {
        eval(vcar(exprs), env);
        exprs = vcdr(exprs);
    }
    return eval(vcar(exprs), env);
}

/* ---- Load file ---- */

val_t scm_load(const char *path, val_t env) {
    val_t port = port_open_file(path, PORT_INPUT);
    if (vis_false(port))
        scm_raise(V_FALSE, "load: cannot open file: %s", path);
    val_t result = V_VOID;
    val_t v;
    while (!vis_eof((v = scm_read(port)))) {
        result = eval(v, env);
    }
    port_close(port);
    return result;
}

void eval_init(void) { akk_eval_setup(); symbolic_init(); surreal_init(); }
