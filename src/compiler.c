/*
 * compiler.c — AST-to-bytecode compiler for Curry.
 *
 * PURPOSE
 *   Translates Scheme ASTs (val_t cons-cell trees produced by the reader)
 *   into Chunk bytecode objects that the VM (vm.c) can execute.  The
 *   public entry points are:
 *
 *     compiler_compile(expr)        — compile a single expression; returns
 *                                     a zero-argument BcClosure.
 *     compiler_compile_script(list) — compile a list of top-level forms as
 *                                     a script; returns a BcClosure that
 *                                     runs all forms left-to-right and
 *                                     returns the last value.
 *
 * SCOPE MODEL
 *   Each lambda creates a nested Compiler struct linked to its enclosing
 *   Compiler via `enclosing`.  Variables are resolved at compile time:
 *     1. Local    — found in c->locals[]         → OP_LOAD/STORE_LOCAL
 *     2. Upvalue  — found in an enclosing frame   → OP_LOAD/STORE_UP
 *     3. Global   — falls through to GLOBAL_ENV   → OP_LOAD/STORE_GLOBAL
 *
 * UPVALUE CAPTURE
 *   When an inner lambda references a variable in an outer lambda, the
 *   outer local is marked `captured = true`.  OP_CLOSURE emits [is_local,
 *   index] pairs for each captured variable; the VM uses them to build
 *   Upvalue chains (open while on the stack, closed to heap on scope exit).
 *
 * SPECIAL FORMS
 *   quote, if, begin, define, set!, lambda, let (including named let),
 *   let*, letrec, letrec*, and, or, cond (with => support), when, unless,
 *   do, values, apply.  Akkadian/cuneiform synonyms are translated by
 *   akk_translate() before dispatch, so Akkadian source compiles identically.
 *
 * TAIL CALLS
 *   The `tail` flag propagates through the compile tree.  Calls in tail
 *   position emit OP_TAIL_CALL instead of OP_CALL, enabling TCO for
 *   self-recursive and mutually recursive BcClosure calls.
 *
 * INTERNAL DEFINES
 *   Lambda bodies are pre-scanned for internal (define …) forms; their
 *   names are pre-declared as locals with a sentinel depth of -1 (uninit).
 *   This gives letrec* semantics: later definitions can reference earlier
 *   ones but not the other way around.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "compiler.h"
#include "chunk.h"
#include "opcode.h"
#include "value.h"
#include "object.h"
#include "symbol.h"
#include "gc.h"
#include "numeric.h"
#include "env.h"
#include "eval.h"
#include "vm.h"
#include "builtins.h"
#include "akkadian_eval.h"
#include "profiling.h"
#include "reader.h"
#include "record_type.h"
#include "syntax_rules.h"
#include "sx_algebra.h"
#include "sx_pattern.h"

/* ── Compiler scope structures ───────────────────────────────────────── */

#define MAX_LOCALS  256
#define MAX_UPVALS  256
#define MAX_SYNTAX_LOCALS 64

typedef struct {
    val_t  name;       /* interned symbol                                */
    int    depth;      /* scope depth at declaration                     */
    bool   captured;   /* captured by an inner closure?                  */
    int    dbg_idx;    /* index into chunk->local_debug for this local   */
} Local;

typedef struct {
    bool  is_local;    /* captures from enclosing frame locals (true)
                          or from enclosing closure's upvalues (false)  */
    int   index;
    val_t name;        /* interned symbol — for JIT upval_names table   */
} UpvalDesc;

/* A macro visible only within this compilation and its nested lambdas —
 * established by let-syntax/letrec-syntax or an internal (non-top-level)
 * define-syntax.  Pure compile-time bookkeeping: `transformer` is the
 * already-evaluated callable (a Closure/Primitive/BcClosure val_t), never
 * touching the VM stack the way a Local does.  See resolve_syntax_local. */
typedef struct {
    val_t name;
    val_t transformer;
    int   depth;
} SyntaxLocal;

typedef struct Compiler {
    struct Compiler *enclosing;

    Chunk     *chunk;
    const char *name;

    Local      locals[MAX_LOCALS];
    int        local_count;
    int        scope_depth;

    UpvalDesc  upvals[MAX_UPVALS];
    int        upval_count;

    SyntaxLocal syntax_locals[MAX_SYNTAX_LOCALS];
    int         syntax_local_count;
} Compiler;

/* ── Forward declarations ────────────────────────────────────────────── */
static void compile(Compiler *c, val_t expr, bool tail, int line);
static void compile_seq(Compiler *c, val_t list, bool tail, int line);
static bool is_quoted_symbol(val_t expr, val_t *out_sym);

/* ── Compiler lifecycle ──────────────────────────────────────────────── */

static _Thread_local const char *g_compile_source_name = NULL;

void compiler_set_source_name(const char *name) { g_compile_source_name = name; }

static void init_compiler(Compiler *c, Compiler *enc, const char *name) {
    c->enclosing   = enc;
    c->chunk       = chunk_new();
    c->name        = name;
    c->local_count = 0;
    c->scope_depth = 0;
    c->upval_count = 0;
    c->syntax_local_count = 0;
    c->chunk->name = name;
    c->chunk->source_name = g_compile_source_name;
}

static Chunk *end_compiler(Compiler *c) {
    /* Return whatever the body left on the stack (compile_seq always leaves
       one value; for tail-called BcClosures the frame is reused so OP_RETURN
       here is dead code, but it still needs to be well-formed). */
    chunk_emit(c->chunk, OP_RETURN, 0);
    c->chunk->upval_count = c->upval_count;
    chunk_local_debug_finalize(c->chunk);
    return c->chunk;
}

/* ── Emit helpers ────────────────────────────────────────────────────── */

static void emit(Compiler *c, uint8_t op, int line) {
    chunk_emit(c->chunk, op, line);
}

static void emit_const(Compiler *c, val_t v, int line) {
    int idx = chunk_add_const(c->chunk, v);
    if (idx < 256) {
        emit(c, OP_CONST, line);
        chunk_emit(c->chunk, (uint8_t)idx, line);
    } else {
        emit(c, OP_CONST_W, line);
        chunk_emit16(c->chunk, (uint16_t)idx, line);
    }
}

static void emit_ab(Compiler *c, uint8_t op, uint8_t a, int line) {
    chunk_emit(c->chunk, op, line);
    chunk_emit(c->chunk, a,  line);
}

/* Emit a jump; returns offset of the 16-bit placeholder to patch later */
static int emit_jump(Compiler *c, uint8_t op, int line) {
    emit(c, op, line);
    int off = chunk_pos(c->chunk);
    chunk_emit16(c->chunk, 0xFFFF, line);  /* placeholder */
    return off;
}

static void patch_jump(Compiler *c, int placeholder) {
    int target = chunk_pos(c->chunk);
    if (target > 0xFFFF)
        fprintf(stderr, "compiler: jump target out of range\n");
    chunk_patch16(c->chunk, placeholder, (uint16_t)target);
}

/* ── Scope / locals ──────────────────────────────────────────────────── */

static void begin_scope(Compiler *c) { c->scope_depth++; }

static void end_scope(Compiler *c, int line) {
    c->scope_depth--;
    int n = 0;
    while (c->local_count > 0 &&
           c->locals[c->local_count - 1].depth > c->scope_depth) {
        if (c->locals[c->local_count - 1].captured)
            /* Close the upvalue for this specific slot without popping. */
            emit_ab(c, OP_CLOSE_UP, (uint8_t)(c->local_count - 1), line);
        chunk_local_debug_end(c->chunk,
                              c->locals[c->local_count - 1].dbg_idx,
                              chunk_pos(c->chunk));
        c->local_count--;
        n++;
    }
    /* Slide TOS (the scope's result) past all the local slots below it. */
    if (n > 0)
        emit_ab(c, OP_SLIDE, (uint8_t)n, line);
    /* Local macros (let-syntax/letrec-syntax/internal define-syntax) leave
     * no VM stack footprint — just drop them from compile-time bookkeeping. */
    while (c->syntax_local_count > 0 &&
           c->syntax_locals[c->syntax_local_count - 1].depth > c->scope_depth)
        c->syntax_local_count--;
}

/* Register a macro visible from here to the end of the enclosing scope
 * (and to any nested lambda compiled within it, via resolve_syntax_local's
 * walk up ->enclosing) — used by internal define-syntax and by
 * let-syntax/letrec-syntax. */
static int add_syntax_local(Compiler *c, val_t name, val_t transformer) {
    if (c->syntax_local_count == MAX_SYNTAX_LOCALS) {
        fprintf(stderr, "compiler: too many local macros\n");
        return 0;
    }
    SyntaxLocal *s = &c->syntax_locals[c->syntax_local_count];
    s->name        = name;
    s->transformer = transformer;
    s->depth       = c->scope_depth;
    return c->syntax_local_count++;
}

/* Walk this compiler's local macro table, then each enclosing compiler in
 * turn (a nested lambda sees its parent's local macros, same as upvalues).
 * Inner bindings shadow outer ones. Returns false (not found) if no local
 * macro matches — the caller should then fall back to a GLOBAL_ENV lookup. */
static bool resolve_syntax_local(Compiler *c, val_t name, val_t *out_transformer) {
    for (Compiler *cc = c; cc; cc = cc->enclosing) {
        for (int i = cc->syntax_local_count - 1; i >= 0; i--)
            if (cc->syntax_locals[i].name == name) {
                *out_transformer = cc->syntax_locals[i].transformer;
                return true;
            }
    }
    return false;
}

static int add_local(Compiler *c, val_t name) {
    if (c->local_count == MAX_LOCALS) {
        fprintf(stderr, "compiler: too many locals\n");
        return 0;
    }
    Local *l = &c->locals[c->local_count];
    l->name     = name;
    l->depth    = c->scope_depth;
    l->captured = false;
    l->dbg_idx  = chunk_local_debug_add(c->chunk, name, c->local_count,
                                        chunk_pos(c->chunk));
    return c->local_count++;
}

/* Mark the most-recently added local as initialised at current depth */
static void mark_initialised(Compiler *c) {
    if (c->local_count > 0)
        c->locals[c->local_count - 1].depth = c->scope_depth;
}

/* ── Upvalue resolution ───────────────────────────────────────────────── */

static int add_upvalue(Compiler *c, int index, bool is_local, val_t name) {
    for (int i = 0; i < c->upval_count; i++)
        if (c->upvals[i].index == index && c->upvals[i].is_local == is_local)
            return i;
    if (c->upval_count == MAX_UPVALS) {
        fprintf(stderr, "compiler: too many upvalues\n");
        return 0;
    }
    c->upvals[c->upval_count].index    = index;
    c->upvals[c->upval_count].is_local = is_local;
    c->upvals[c->upval_count].name     = name;
    c->chunk->upval_count = ++c->upval_count;
    return c->upval_count - 1;
}

static int resolve_local(Compiler *c, val_t name) {
    for (int i = c->local_count - 1; i >= 0; i--)
        if (c->locals[i].name == name) return i;
    return -1;
}

static int resolve_upvalue(Compiler *c, val_t name) {
    if (!c->enclosing) return -1;
    int local = resolve_local(c->enclosing, name);
    if (local >= 0) {
        c->enclosing->locals[local].captured = true;
        return add_upvalue(c, local, true, name);
    }
    int up = resolve_upvalue(c->enclosing, name);
    if (up >= 0) return add_upvalue(c, up, false, name);
    return -1;
}

/* ── Variable load / store ───────────────────────────────────────────── */

static void emit_load(Compiler *c, val_t name, int line) {
    int local = resolve_local(c, name);
    if (local >= 0) { emit_ab(c, OP_LOAD_LOCAL, (uint8_t)local, line); return; }
    int up = resolve_upvalue(c, name);
    if (up >= 0)    { emit_ab(c, OP_LOAD_UP,    (uint8_t)up,    line); return; }
    emit_ab(c, OP_LOAD_GLOBAL, (uint8_t)chunk_add_const(c->chunk, name), line);
}

static void emit_store(Compiler *c, val_t name, int line) {
    int local = resolve_local(c, name);
    if (local >= 0) { emit_ab(c, OP_STORE_LOCAL, (uint8_t)local, line); return; }
    int up = resolve_upvalue(c, name);
    if (up >= 0)    { emit_ab(c, OP_STORE_UP,    (uint8_t)up,    line); return; }
    emit_ab(c, OP_STORE_GLOBAL, (uint8_t)chunk_add_const(c->chunk, name), line);
}

/* ── Parameter list helpers ───────────────────────────────────────────── */

/* Returns arity; -1 if rest param (improper list).
   Declares each param as a local in c. */
static int compile_params(Compiler *c, val_t params) {
    int arity = 0;
    val_t p = params;
    while (vis_pair(p)) {
        val_t name = vcar(p);
        if (!vis_symbol(name)) {
            fprintf(stderr, "compiler: lambda param must be symbol\n");
            return arity;
        }
        add_local(c, name);
        mark_initialised(c);
        arity++;
        p = vcdr(p);
    }
    if (!vis_nil(p)) {
        /* Rest parameter: (lambda (a b . rest) ...) */
        if (vis_symbol(p)) {
            add_local(c, p);
            mark_initialised(c);
        }
        return -(arity + 1);  /* negative signals variadic */
    }
    return arity;
}

/* ── Lambda compilation ───────────────────────────────────────────────── */

static void compile_lambda(Compiler *parent, val_t params, val_t body,
                            const char *name, int line) {
    Compiler c;
    init_compiler(&c, parent, name);

    int arity = compile_params(&c, params);
    c.chunk->arity = arity;
    begin_scope(&c);  /* body scope: depth 1+, so compile_define uses OP_STORE_LOCAL */

    /* Scan for internal defines and pre-declare as locals (letrec* semantics).
     * Also enforce R7RS: definitions must precede all expressions in the body. */
    val_t bscan = body;
    bool body_has_expr = false;
    while (vis_pair(bscan)) {
        val_t form = vcar(bscan);
        bool is_def = vis_pair(form) && vis_symbol(vcar(form)) &&
                      (akk_translate(vcar(form)) == S_DEFINE ||
                       akk_translate(vcar(form)) == S_DEFINE_SYNTAX ||
                       akk_translate(vcar(form)) == S_DEFINE_VALUES ||
                       akk_translate(vcar(form)) == S_DEFINE_RECORD_TYPE ||
                       akk_translate(vcar(form)) == S_DEFINE_RULE ||
                       akk_translate(vcar(form)) == S_DEFINE_RULESET ||
                       akk_translate(vcar(form)) == S_DEFINE_ALGEBRA);
        /* (symbolic x y z ...) binds each name as a fresh runtime value
         * (see compile_symbolic) — not an R7RS-style internal definition
         * (no ordering restriction relative to expressions, matching its
         * pre-existing tree-walker behavior), but it DOES need its bound
         * names reserved as locals ahead of time: compile_define's
         * "New local" fallback (used for a name with no pre-declared slot)
         * assumes the compiled value is already at the correct physical VM
         * stack position for OP_STORE_LOCAL, which only holds when reached
         * through the same prescan-then-store protocol ordinary internal
         * defines use — skipping that reservation corrupted the local slot
         * layout (confirmed: a local `symbolic` variable read back an
         * unrelated value). */
        if (vis_pair(form) && vis_symbol(vcar(form)) &&
            akk_translate(vcar(form)) == S_SYMBOLIC) {
            for (val_t p = vcdr(form); vis_pair(p); p = vcdr(p)) {
                if (!vis_symbol(vcar(p))) continue;
                add_local(&c, vcar(p));
                c.locals[c.local_count - 1].depth = -1; /* uninitialised */
                emit(&c, OP_VOID, line); /* reserve stack slot */
            }
        }

        if (is_def) {
            if (body_has_expr)
                scm_raise(V_FALSE, "internal definition after expression in body (R7RS violation)");
            if (akk_translate(vcar(form)) == S_DEFINE_RECORD_TYPE) {
                /* define-record-type doesn't bind its own type-name symbol
                 * (matching record_type_build_spec/eval.c) — it binds a
                 * constructor, a predicate, and one accessor/mutator pair
                 * per field.  Reserve locals for all of those so the record
                 * type is usable as an internal definition, not just at
                 * top level. */
                RecordTypeSpec spec;
                record_type_build_spec(vcdr(form), V_FALSE, &spec);
                for (int i = 0; i < spec.count; i++) {
                    add_local(&c, spec.bindings[i].name);
                    c.locals[c.local_count - 1].depth = -1; /* uninitialised */
                    emit(&c, OP_VOID, line); /* reserve stack slot */
                }
            } else if (akk_translate(vcar(form)) == S_DEFINE_SYNTAX) {
                /* Macro registration is pure compile-time bookkeeping (see
                 * add_syntax_local/resolve_syntax_local) — no VM stack slot
                 * to reserve.  compile_define_syntax registers it into
                 * c.syntax_locals sequentially as compile_seq reaches this
                 * form for real, so later forms in this same body see it. */
            } else if (akk_translate(vcar(form)) == S_DEFINE_RULE ||
                       akk_translate(vcar(form)) == S_DEFINE_RULESET) {
                /* Neither binds a name into the enclosing scope: define-rule
                 * registers a global rewrite rule and binds nothing;
                 * define-ruleset's "name" argument is a rule-grouping label
                 * (sx_rule_add's ruleset field), not a variable. Nothing to
                 * reserve. Falling into the generic branch below (as these
                 * did before compile_define_rule/compile_define_ruleset
                 * existed) misread the pattern/ruleset-name as a bound
                 * variable name and reserved a bogus, permanently-
                 * uninitialised local slot — confirmed: it could shadow an
                 * unrelated global (e.g. define-algebra's quoted operator
                 * form made the second element `quote`, silently turning a
                 * later bare reference to the special form name `quote`
                 * into a read of an uninitialised local instead of the
                 * expected unbound-variable error). */
            } else if (akk_translate(vcar(form)) == S_DEFINE_ALGEBRA) {
                /* Only the compile-time-literal-quoted-operator case
                 * ((define-algebra 'sym ...), by far the common usage — see
                 * compile_define_algebra) gets a real lexical binding for
                 * its auto-bound operator procedure. A runtime-computed
                 * operator name has no local binding to reserve: the same
                 * fundamental limit as any (define <computed-name> ...) in
                 * a slot-based compiled VM, where every local's slot index
                 * is fixed at compile time. */
                val_t op_expr = vis_pair(vcdr(form)) ? vcar(vcdr(form)) : V_FALSE;
                val_t sym;
                if (is_quoted_symbol(op_expr, &sym)) {
                    add_local(&c, sym);
                    c.locals[c.local_count - 1].depth = -1; /* uninitialised */
                    emit(&c, OP_VOID, line); /* reserve stack slot */
                }
            } else {
                val_t defname = vcar(vcdr(form));
                if (vis_symbol(defname)) {
                    /* simple (define x ...) */
                    add_local(&c, defname);
                    c.locals[c.local_count - 1].depth = -1; /* uninitialised */
                    emit(&c, OP_VOID, line); /* reserve stack slot */
                } else if (vis_pair(defname)) {
                    /* (define (f ...) ...) sugar */
                    add_local(&c, vcar(defname));
                    c.locals[c.local_count - 1].depth = -1;
                    emit(&c, OP_VOID, line); /* reserve stack slot */
                }
            }
        } else {
            body_has_expr = true;
        }
        bscan = vcdr(bscan);
    }

    compile_seq(&c, body, true, line);
    Chunk *ch = end_compiler(&c);

    /* Preserve source AST and upvalue names for tiered JIT hot-swap. */
    ch->src_lambda = scm_cons(S_LAMBDA, scm_cons(params, body));
    if (c.upval_count > 0) {
        ch->upval_names = (val_t *)gc_alloc_raw_pinned((size_t)c.upval_count * sizeof(val_t));
        for (int i = 0; i < c.upval_count; i++)
            ch->upval_names[i] = c.upvals[i].name;
    }

    /* In parent: emit OP_CLOSURE followed by upvalue descriptors */
    int ci = chunk_add_const(parent->chunk, (val_t)(uintptr_t)ch);
    emit_ab(parent, OP_CLOSURE, (uint8_t)ci, line);
    for (int i = 0; i < c.upval_count; i++) {
        chunk_emit(parent->chunk, c.upvals[i].is_local ? 1 : 0, line);
        chunk_emit(parent->chunk, (uint8_t)c.upvals[i].index,   line);
    }
}

/* ── Special forms ───────────────────────────────────────────────────── */

static void compile_if(Compiler *c, val_t args, bool tail, int line) {
    val_t test  = vcar(args);  args = vcdr(args);
    val_t then  = vcar(args);  args = vcdr(args);
    val_t els   = vis_pair(args) ? vcar(args) : V_VOID;

    compile(c, test, false, line);
    int else_jmp = emit_jump(c, OP_JUMP_FALSE, line);

    compile(c, then, tail, line);
    int end_jmp  = emit_jump(c, OP_JUMP, line);

    patch_jump(c, else_jmp);
    compile(c, els, tail, line);
    patch_jump(c, end_jmp);
}

/* Emit the store half of a define: the computed value must already be on
 * top of the stack. Shared by compile_define (after compiling an ordinary
 * value expression) and compile_symbolic (after emitting hand-rolled,
 * lookup-hygienic call bytecode instead of a compilable AST value). */
static void emit_define_store(Compiler *c, val_t name, int line) {
    if (c->scope_depth == 0) {
        /* Top-level: DEF_GLOBAL */
        emit_ab(c, OP_DEF_GLOBAL, (uint8_t)chunk_add_const(c->chunk, name), line);
    } else {
        /* Internal define: check if pre-declared local slot exists */
        int slot = resolve_local(c, name);
        if (slot >= 0 && c->locals[slot].depth < 0) {
            /* Initialise the pre-declared slot */
            c->locals[slot].depth = c->scope_depth;
            emit_ab(c, OP_STORE_LOCAL, (uint8_t)slot, line);
        } else {
            /* New local */
            add_local(c, name);
            mark_initialised(c);
            /* value already on stack; STORE_LOCAL to new slot */
            emit_ab(c, OP_STORE_LOCAL,
                    (uint8_t)(c->local_count - 1), line);
        }
    }
    emit(c, OP_VOID, line);   /* define returns void */
}

static void compile_define(Compiler *c, val_t args, int line) {
    val_t target = vcar(args);
    val_t rest   = vcdr(args);

    val_t name;
    val_t value;

    if (vis_symbol(target)) {
        /* (define x expr) */
        name  = target;
        value = vis_pair(rest) ? vcar(rest) : V_VOID;
        compile(c, value, false, line);
    } else if (vis_pair(target)) {
        /* (define (f params...) body...) → lambda sugar */
        name = vcar(target);
        val_t params = vcdr(target);
        compile_lambda(c, params, rest, as_sym(name)->data, line);
    } else {
        fprintf(stderr, "compiler: bad define form\n");
        emit_const(c, V_VOID, line);
        return;
    }

    emit_define_store(c, name, line);
}

/* (define-record-type name ctor-form pred field-spec...) — R7RS, or
 * (define-record-type name (fields ...) ...) — R6RS. record_type_build_spec
 * (shared with eval.c's tree-walker case, still needed for library bodies)
 * builds the RTD and every (name params body) triple that needs binding;
 * compile each as an ordinary define so the usual local/global/upvalue
 * machinery in compile_define applies — including the letrec* semantics
 * from this function's pre-declared locals when used inside a lambda body.
 *
 * Passes a gensym'd local/global name as record_type_build_spec's rtd_ref,
 * rather than V_FALSE (which would embed the built RTD as a quoted
 * constant independently in each of the ctor/pred/accessor/mutator
 * closures — safe in memory, but each gets serialized to a .scc file as an
 * independent copy and reconstructed into a non-eq? object on load,
 * breaking %record-pred?'s pointer-equality check; found by testing: a
 * cache-hit run of a script using define-record-type failed its own
 * predicate). Emitting (define <gensym> (%make-record-type 'name
 * 'field-names)) once, ahead of the bindings, and having every binding
 * reference that single shared variable instead keeps identity correct
 * within one execution (fresh or replayed from cache) without embedding
 * the RTD as a constant at all.
 *
 * The gensym'd variable itself is NOT covered by compile_lambda's prescan
 * (which only reserves slots for the record's real, user-visible bindings)
 * — so, when local, its own local slot is reserved right here, immediately
 * before compiling its value, rather than relying on compile_define's
 * "New local" fallback (add a slot, store into it, immediately followed by
 * emit_define_store's trailing OP_VOID). That fallback silently corrupts
 * the slot when nothing reserved it ahead of time: OP_STORE_LOCAL writes
 * to a frame-relative position that, at that exact moment, coincides with
 * the current stack top, and the very next instruction — the trailing
 * OP_VOID — pushes onto that same now-freed position, overwriting the
 * value that was just stored (the same bug found and fixed for `symbolic`,
 * confirmed by re-deriving OP_STORE_LOCAL's frame-relative semantics in
 * vm.c). Reserving first (add_local + a placeholder OP_VOID, mirroring
 * what a prescan reservation does) separates the reserved slot from the
 * position the trailing OP_VOID lands at, avoiding the collision — done
 * inline here rather than by extending the prescan, since the gensym only
 * needs to be unique within this one function call, not coordinated across
 * two separate passes over the body. */
static void compile_define_record_type(Compiler *c, val_t rest, int line) {
    static int gensym_counter = 0;
    char buf[32];
    snprintf(buf, sizeof(buf), "%%%%rtd%d", gensym_counter++);
    val_t rtd_ref = sym_intern_cstr(buf);

    RecordTypeSpec spec;
    record_type_build_spec(rest, rtd_ref, &spec);

    RecordType *rtd = as_rtd(spec.rtd_val);
    val_t field_names = V_NIL;
    for (int i = (int)rtd->nfields - 1; i >= 0; i--)
        field_names = scm_cons(rtd->field_names[i], field_names);
    val_t make_rtd_call = scm_cons(sym_intern_cstr("%make-record-type"),
        scm_cons(scm_cons(S_QUOTE, scm_cons(rtd->name, V_NIL)),
         scm_cons(scm_cons(S_QUOTE, scm_cons(field_names, V_NIL)), V_NIL)));

    if (c->scope_depth > 0) {
        add_local(c, rtd_ref);
        c->locals[c->local_count - 1].depth = -1; /* uninitialised */
        emit(c, OP_VOID, line); /* reserve stack slot */
    }
    compile_define(c, scm_cons(rtd_ref, scm_cons(make_rtd_call, V_NIL)), line);

    for (int i = 0; i < spec.count; i++) {
        emit(c, OP_POP, line); /* discard the previous binding's OP_VOID */
        val_t lam = scm_cons(S_LAMBDA,
                     scm_cons(spec.bindings[i].params, spec.bindings[i].body));
        val_t def_args = scm_cons(spec.bindings[i].name, scm_cons(lam, V_NIL));
        compile_define(c, def_args, line);
    }
}

/* (symbolic x y z ...) — bind each name as a fresh symbolic unknown.
 * Unlike define-syntax's transformer, sym-var produces an ordinary RUNTIME
 * value (a SymVar), not a compile-time macro, so this needs no
 * compile_time_eval — it's a plain (sym-var 'name) call per name, using
 * emit_define_store for the same local/global store logic compile_define
 * uses, exactly like define-record-type's bindings.
 *
 * The call to sym-var is emitted by hand — OP_LOAD_GLOBAL for the symbol
 * `sym-var`, bypassing the local/upvalue checks emit_load would otherwise
 * do — rather than compiling an ordinary AST call form referencing the
 * symbol `sym-var`. This isn't optional: a local variable literally named
 * sym-var would otherwise shadow the primitive and silently change what
 * `symbolic` does (found by review — `(let ((sym-var ...)) (symbolic a)
 * a)` returned the shadowing lambda's result instead of a SymVar). The
 * tree-walker's S_SYMBOLIC case never had this hazard: it calls
 * sx_make_var(name) directly in C, never through a Scheme-level binding.
 *
 * An earlier version of this fix instead embedded the ALREADY-RESOLVED
 * sym-var Primitive value directly as a bytecode constant (compile-time
 * env_lookup_or_false, then quote the result). That's wrong for a
 * different reason: a Primitive closes over a C function pointer, which
 * cannot be serialized into a .scc file — confirmed by testing, it
 * segfaults even on a single fresh (non-cached) run, since the
 * script-execution loop unconditionally writes a .scc cache after
 * compiling each top-level form. OP_LOAD_GLOBAL only embeds the SYMBOL
 * `sym-var` (always plain, serializable data) and looks it up in
 * GLOBAL_ENV fresh at runtime — hygienic (bypasses local shadowing) without
 * embedding a non-serializable object, and it still calls sym-var fresh
 * each time this code executes (not caching/sharing sym-var's RESULT,
 * which would incorrectly share one SymVar across repeated calls to an
 * enclosing function — a different, worse bug avoided by construction). */
static void compile_symbolic(Compiler *c, val_t rest, int line) {
    val_t sym_var_sym = sym_intern_cstr("sym-var");
    bool first = true;
    for (val_t p = rest; vis_pair(p); p = vcdr(p)) {
        val_t name = vcar(p);
        if (!vis_symbol(name)) {
            fprintf(stderr, "compiler: symbolic: expected symbol, got non-symbol\n");
            continue;
        }
        if (!first) emit(c, OP_POP, line); /* discard previous intermediate OP_VOID */
        emit_ab(c, OP_LOAD_GLOBAL, (uint8_t)chunk_add_const(c->chunk, sym_var_sym), line);
        emit_const(c, name, line);
        emit_ab(c, OP_CALL, 1, line);
        emit_define_store(c, name, line);
        first = false;
    }
    if (first) emit(c, OP_VOID, line); /* (symbolic) with no names */
}

/* Compile expr as an independent, parent-less top-level unit and run it
 * immediately — used to evaluate macro-transformer expressions
 * (define-syntax/let-syntax/letrec-syntax) at compile time.  Having no
 * parent Compiler means expr cannot resolve an enclosing lambda's locals or
 * upvalues as compiler-tracked variables; it can only see GLOBAL_ENV,
 * exactly like top-level define-syntax always could.  That's not a new
 * restriction: a transformer-constructing expression referencing a
 * not-yet-computed runtime-only local is meaningless anyway, since macro
 * expansion happens at compile time, before the enclosing function has ever
 * run.  compiler_compile()/vm_run() are already invoked reentrantly
 * elsewhere (e.g. apply() during use-site macro expansion below can itself
 * trigger vm_run for a compiled transformer), so nesting them here follows
 * an established pattern rather than introducing a new one — but that
 * pattern requires pushing the closure as the callee before vm_run (see
 * vm_eval's own comment in vm.c): pop_frame's return path assumes a pushed
 * callee+args below the frame, so a reentrant vm_run entered without one
 * (e.g. this call firing while the debugger's `,debug`/`p` command has
 * vm_run paused mid-frame) would corrupt the suspended frame's stack by one
 * slot. Top-level (non-reentrant) callers wouldn't have noticed, since
 * frame_count == 0 takes pop_frame's full-reset path instead.
 *
 * Exception-safe: compiler_compile() brackets its work in
 * gc_inhibit_minor()/gc_resume_minor(), so a transformer expression that
 * raises during compilation or evaluation (a bad syntax-rules form, an
 * unbound reference, etc.) would otherwise longjmp past the matching
 * gc_resume_minor() with nothing to rebalance it — permanently leaking
 * gc_inhibit_count and disabling minor-GC safepoints for the rest of the
 * process.  SCM_PROTECT snapshots and restores it (along with the shadow
 * stack and JIT call depth) regardless of how many nested inhibit/resume
 * calls happened inside, then this re-raises the same exception so normal
 * error reporting (REPL, guard, etc.) is unaffected. */
static val_t compile_time_eval(val_t expr) {
    ExnHandler h;
    val_t result = V_VOID;
    bool  raised = false;
    SCM_PROTECT(h, {
        val_t cl_val  = compiler_compile(expr);
        BcClosure *cl = as_bcclosure(cl_val);
        vm_push(cl_val);
        result = vm_run(cl, 0);
    }, {
        raised = true;
    });
    if (raised) scm_raise_val(h.exn);
    return result;
}

/* (define-syntax name transformer-expr). At top level (scope_depth == 0)
 * this registers into GLOBAL_ENV immediately — eagerly, at compile time,
 * rather than ONLY deferring to a runtime tree-eval call — so a macro
 * defined and used within the very same compiled unit (e.g. the same
 * top-level begin block or script) is visible to its own later forms,
 * which the old tree-eval-punt behavior could not do (it only ever took
 * effect once the *next separately compiled* top-level form ran).
 *
 * The eager registration alone doesn't survive a .scc cache-hit replay,
 * though: on a cache hit the compiler never runs again, so if that were
 * the ONLY effect, the macro would silently vanish from GLOBAL_ENV on
 * every run except the one that originally produced the cache (e.g.
 * breaking `-i` dropping into a REPL after a cached script run). So this
 * ALSO emits runtime bytecode that re-registers the same macro when the
 * bytecode executes (fresh or replayed from cache) — deliberately NOT by
 * embedding the compile-time-evaluated `transformer` procedure value
 * directly as a constant: a syntax-rules transformer is a Primitive
 * closing over a C function pointer, which cannot be serialized into a
 * .scc file (confirmed by testing: doing so segfaults on the next
 * process's cache-hit load).
 *
 * Two cases, both wired through emitting (tree-eval '(define-syntax name
 * runtime-xfm-expr)) — i.e. always going through define-syntax's own
 * single Syntax-wrapping step (in the tree-walker's S_DEFINE_SYNTAX case),
 * never building or embedding a Syntax struct here directly. An earlier
 * version of this code called %rebuild-syntax-rules via a plain (define
 * ...) and wrapped its result in a second, redundant Syntax struct; since
 * %rebuild-syntax-rules is an ordinary, discoverable global primitive, a
 * user writing (define-syntax bogus (%rebuild-syntax-rules ...)) directly
 * produced a Syntax-wrapping-a-Syntax value nothing else expected,
 * corrupting VM state when used (found by review). Routing everything
 * through one (define-syntax ...) form, and having %rebuild-syntax-rules
 * return a bare transformer (the same shape (syntax-rules ...) itself
 * evaluates to — see sr_rebuild_syntax's doc comment), fixes that: direct
 * misuse now just produces an ordinary, correctly-single-wrapped macro (or
 * a normal Scheme error, if the literals/rules/ellipsis are malformed).
 *
 *   - transformer-expr produced an ordinary syntax-rules transformer (by
 *     far the common case): sr_transformer_data extracts its underlying
 *     literals/rules/ellipsis — always plain, serializable pattern/
 *     template data, never evaluated Scheme code — and runtime-xfm-expr
 *     becomes (%rebuild-syntax-rules 'literals 'rules 'ellipsis). This
 *     never re-runs transformer-expr itself, so a transformer-expr with
 *     side-effecting code around the (syntax-rules ...) form (e.g.
 *     `(begin (side-effect!) (syntax-rules ...))`) only actually executes
 *     once, at compile time.
 *   - Anything else (a procedural transformer): there's no way to
 *     decompose an arbitrary closure into serializable pure data, so
 *     runtime-xfm-expr is just the ORIGINAL transformer-expr, re-evaluated
 *     at runtime — exactly what the pre-existing tree-eval punt already
 *     did. This DOES mean transformer-expr's side effects (if any) run
 *     twice on a fresh run: once via compile_time_eval, once via the
 *     runtime tree-eval call. Accepted for this rare case: define-syntax
 *     is a one-shot, load-time form, never a hot path, and a procedural
 *     transformer genuinely cannot survive a .scc cache reload any other
 *     way.
 *
 * Inside a lambda/let-syntax body, the macro is scoped to this
 * compilation's syntax_locals instead of leaking into GLOBAL_ENV (fixing
 * the same local-scope leak that define-record-type had), and needs no
 * runtime reconstruction at all: an internal macro is fully consumed by
 * the compiler expanding its use sites within the same lambda body, which
 * are already baked into that body's bytecode by the time compilation
 * ends — nothing outside that (lexically-scoped, one-shot) compilation
 * could ever need it to exist again later. */
static void compile_define_syntax(Compiler *c, val_t args, int line) {
    val_t name        = vcar(args);
    val_t xfm_expr    = vcar(vcdr(args));
    val_t transformer = compile_time_eval(xfm_expr);

    if (c->scope_depth == 0) {
        Syntax *syn = CURRY_NEW(Syntax);
        syn->hdr.type = T_SYNTAX; syn->hdr.flags = 0;
        syn->transformer = transformer;
        env_define(GLOBAL_ENV, name, vptr(syn));

        val_t literals, rules, ellipsis;
        val_t runtime_xfm_expr;
        if (sr_transformer_data(transformer, &literals, &rules, &ellipsis)) {
            runtime_xfm_expr = scm_cons(sym_intern_cstr("%rebuild-syntax-rules"),
                scm_cons(scm_cons(S_QUOTE, scm_cons(literals, V_NIL)),
                 scm_cons(scm_cons(S_QUOTE, scm_cons(rules, V_NIL)),
                  scm_cons(scm_cons(S_QUOTE, scm_cons(ellipsis, V_NIL)), V_NIL))));
        } else {
            runtime_xfm_expr = xfm_expr;
        }

        val_t whole_form    = scm_cons(S_DEFINE_SYNTAX,
                                scm_cons(name, scm_cons(runtime_xfm_expr, V_NIL)));
        val_t tree_eval_sym = sym_intern_cstr("tree-eval");
        emit_ab(c, OP_LOAD_GLOBAL,
                (uint8_t)chunk_add_const(c->chunk, tree_eval_sym), line);
        emit_const(c, whole_form, line);
        emit_ab(c, OP_CALL, 1, line);
    } else {
        add_syntax_local(c, name, transformer);
        emit(c, OP_VOID, line); /* define-syntax returns void */
    }
}

/* (let-syntax ((name xfm-expr)...) body...)
 * (letrec-syntax ((name xfm-expr)...) body...)
 * Uses the SAME Compiler `c` (no nested lambda/closure, unlike let/letrec):
 * macros carry no runtime stack footprint, so there is no slot-layout
 * hazard to isolate — begin_scope/end_scope's local-macro trimming (see
 * end_scope) is all the isolation this needs, exactly as it already is for
 * a plain nested block. R7RS distinguishes let-syntax (transformer
 * expressions see only the OUTER scope) from letrec-syntax (transformer
 * expressions see each other too, for mutually-recursive macros); in
 * practice that distinction is moot here, since compile_time_eval compiles
 * each transformer expression as an independent parent-less unit that
 * cannot observe this Compiler's syntax_locals either way — a transformer
 * that itself needs to invoke a sibling local macro during its own
 * construction is an exotic case this implementation doesn't support, no
 * differently for let-syntax vs. letrec-syntax. */
static void compile_let_syntax(Compiler *c, val_t args, bool tail, int line) {
    val_t bindings = vcar(args);
    val_t body     = vcdr(args);

    begin_scope(c);
    val_t b = bindings;
    while (vis_pair(b)) {
        val_t bind = vcar(b);
        val_t name = vcar(bind);
        val_t xfm  = compile_time_eval(vcar(vcdr(bind)));
        add_syntax_local(c, name, xfm);
        b = vcdr(b);
    }
    compile_seq(c, body, tail, line);
    end_scope(c, line);
}

static void compile_set(Compiler *c, val_t args, int line) {
    val_t name = vcar(args);
    val_t expr = vcar(vcdr(args));
    compile(c, expr, false, line);
    emit_store(c, name, line);
    emit(c, OP_VOID, line);
}

static void compile_begin(Compiler *c, val_t body, bool tail, int line) {
    if (vis_nil(body)) { emit(c, OP_VOID, line); return; }
    compile_seq(c, body, tail, line);
}

static void compile_and(Compiler *c, val_t args, bool tail, int line) {
    if (vis_nil(args)) { emit(c, OP_TRUE, line); return; }

    int patches[MAX_LOCALS]; int np = 0;
    while (vis_pair(args)) {
        val_t next = vcdr(args);
        bool last  = vis_nil(next);
        compile(c, vcar(args), last && tail, line);
        if (!last) {
            patches[np++] = emit_jump(c, OP_JUMP_FALSE, line);
        }
        args = next;
    }
    /* All true: fall through with the last value on stack */
    int end = emit_jump(c, OP_JUMP, line);
    /* Patch all short-circuit points to emit #f */
    int false_pos = chunk_pos(c->chunk);
    emit(c, OP_FALSE, line);
    patch_jump(c, end);
    /* Patch all OP_JUMP_FALSE to false_pos */
    for (int i = 0; i < np; i++)
        chunk_patch16(c->chunk, patches[i], (uint16_t)false_pos);
}

static void compile_or(Compiler *c, val_t args, bool tail, int line) {
    (void)tail;
    if (vis_nil(args)) { emit(c, OP_FALSE, line); return; }

    int patches[MAX_LOCALS]; int np = 0;
    while (vis_pair(args)) {
        val_t next = vcdr(args);
        bool last  = vis_nil(next);
        compile(c, vcar(args), false, line);
        if (!last) {
            emit(c, OP_DUP, line);
            patches[np++] = emit_jump(c, OP_JUMP_TRUE, line);
            emit(c, OP_POP, line);
        }
        args = next;
    }
    /* Fall through: last value is the result */
    int end = chunk_pos(c->chunk);
    (void)end;
    for (int i = 0; i < np; i++)
        patch_jump(c, patches[i]);
}

static void compile_let(Compiler *c, val_t args, bool tail, int line) {
    val_t bindings = vcar(args);
    val_t body     = vcdr(args);

    /* Named let: (let loop ((x v) ...) body)
       Semantics: letrec ((loop (lambda (x ...) body))) (loop v ...)

       Compiled as a zero-arg outer wrapper to isolate the loop's frame:
         outer-wrapper:
           slot 0 = loop (void initially, then the closure)
           push void placeholder
           push OP_CLOSURE for loop (captures slot 0 as upvalue)
           OP_STORE_LOCAL 0        ; slot 0 = closure
           OP_LOAD_LOCAL 0         ; push closure as callee
           compile each init value  ; push args
           OP_TAIL_CALL N          ; jump into the loop
         parent: OP_CLOSURE outer-wrapper; OP_CALL/OP_TAIL_CALL 0 */
    if (vis_symbol(bindings)) {
        val_t loop_name = bindings;
        bindings = vcar(body);
        body     = vcdr(body);

        int argc = 0;
        val_t b = bindings;
        while (vis_pair(b)) { argc++; b = vcdr(b); }

        /* Build forward-order params list */
        val_t params = V_NIL;
        b = bindings;
        while (vis_pair(b)) { params = scm_cons(vcar(vcar(b)), params); b = vcdr(b); }
        val_t fwd = V_NIL;
        while (vis_pair(params)) { fwd = scm_cons(vcar(params), fwd); params = vcdr(params); }

        /* Zero-arg outer wrapper */
        Compiler outer;
        init_compiler(&outer, c, as_sym(loop_name)->data);
        outer.chunk->arity = 0;

        /* Slot 0 = loop name (void placeholder) */
        add_local(&outer, loop_name);
        mark_initialised(&outer);
        emit(&outer, OP_VOID, line);

        /* Inner loop lambda; loop_name resolves as upvalue from outer's slot 0 */
        compile_lambda(&outer, fwd, body, as_sym(loop_name)->data, line);
        emit_ab(&outer, OP_STORE_LOCAL, 0, line);  /* store closure → slot 0 */
        emit_ab(&outer, OP_LOAD_LOCAL,  0, line);  /* callee */
        b = bindings;
        while (vis_pair(b)) {
            compile(&outer, vcar(vcdr(vcar(b))), false, line);
            b = vcdr(b);
        }
        emit_ab(&outer, OP_TAIL_CALL, (uint8_t)argc, line);
        Chunk *och = end_compiler(&outer);

        int ci = chunk_add_const(c->chunk, (val_t)(uintptr_t)och);
        emit_ab(c, OP_CLOSURE, (uint8_t)ci, line);
        for (int i = 0; i < outer.upval_count; i++) {
            chunk_emit(c->chunk, outer.upvals[i].is_local ? 1 : 0, line);
            chunk_emit(c->chunk, (uint8_t)outer.upvals[i].index,   line);
        }
        emit_ab(c, tail ? OP_TAIL_CALL : OP_CALL, 0, line);
        return;
    }

    /* Plain let: compile as ((lambda (x y ...) body) v_x v_y ...)
       The lambda creates a fresh frame, so slot 0 = first init arg
       regardless of what else is on the caller's stack. */
    {
        /* Build params list in forward order */
        val_t params = V_NIL;
        int argc = 0;
        val_t b = bindings;
        while (vis_pair(b)) { params = scm_cons(vcar(vcar(b)), params); argc++; b = vcdr(b); }
        val_t fwd = V_NIL;
        while (vis_pair(params)) { fwd = scm_cons(vcar(params), fwd); params = vcdr(params); }

        /* Closure pushed first (callee), then init values.  Named with the
           enclosing chunk's name: a let frame is still lexically "in" it,
           which is what a backtrace should say. */
        compile_lambda(c, fwd, body, c->name, line);
        b = bindings;
        while (vis_pair(b)) {
            compile(c, vcar(vcdr(vcar(b))), false, line);
            b = vcdr(b);
        }
        emit_ab(c, tail ? OP_TAIL_CALL : OP_CALL, (uint8_t)argc, line);
    }
}

static void compile_let_star(Compiler *c, val_t args, bool tail, int line) {
    val_t bindings = vcar(args);
    val_t body     = vcdr(args);

    /* Compile as nested single-binding lambdas:
       (let* ((x v) rest...) body) → ((lambda (x) (let* rest... body)) v)
       This avoids slot-index mismatches when let* appears as a call argument. */
    if (vis_nil(bindings)) {
        compile_seq(c, body, tail, line);
        return;
    }
    val_t binding  = vcar(bindings);
    val_t name     = vcar(binding);
    val_t init     = vcar(vcdr(binding));
    val_t rest     = vcdr(bindings);
    /* Build inner let*: (let* rest... body) as the lambda body.
       We nest it by constructing a synthetic list form. */
    val_t inner_body;
    if (vis_nil(rest)) {
        inner_body = body;
    } else {
        /* Construct (let* rest body) as a list to pass to compile_seq */
        val_t let_star_sym = sym_intern_cstr("let*");
        val_t inner_form   = scm_cons(let_star_sym,
                                      scm_cons(rest, body));
        inner_body = scm_cons(inner_form, V_NIL);
    }
    val_t params = scm_cons(name, V_NIL);
    compile_lambda(c, params, inner_body, c->name, line);
    compile(c, init, false, line);
    emit_ab(c, tail ? OP_TAIL_CALL : OP_CALL, 1, line);
}

static void compile_letrec(Compiler *c, val_t args, bool tail, int line) {
    val_t bindings = vcar(args);
    val_t body     = vcdr(args);

    /* Wrap in a zero-arg lambda to get a clean frame, so slot 0 = first
       letrec binding regardless of what is already on the caller's stack. */
    Compiler lc;
    init_compiler(&lc, c, "<letrec>");
    lc.chunk->arity = 0;

    begin_scope(&lc);

    /* Count bindings */
    int nb = 0;
    val_t bcount = bindings;
    while (vis_pair(bcount)) { nb++; bcount = vcdr(bcount); }

    /* Pre-declare all locals with void placeholders */
    val_t b = bindings;
    while (vis_pair(b)) {
        val_t name = vcar(vcar(b));
        add_local(&lc, name);
        mark_initialised(&lc);
        emit(&lc, OP_VOID, line);
        b = vcdr(b);
    }

    int base_slot = lc.local_count - nb;

    /* Compile and store each init (they can reference each other via upvalues) */
    b = bindings;
    int i = 0;
    while (vis_pair(b)) {
        val_t init = vcar(vcdr(vcar(b)));
        compile(&lc, init, false, line);
        emit_ab(&lc, OP_STORE_LOCAL, (uint8_t)(base_slot + i), line);
        b = vcdr(b); i++;
    }

    compile_seq(&lc, body, true, line);
    end_scope(&lc, line);

    Chunk *lch = end_compiler(&lc);

    int ci = chunk_add_const(c->chunk, (val_t)(uintptr_t)lch);
    emit_ab(c, OP_CLOSURE, (uint8_t)ci, line);
    for (int k = 0; k < lc.upval_count; k++) {
        chunk_emit(c->chunk, lc.upvals[k].is_local ? 1 : 0, line);
        chunk_emit(c->chunk, (uint8_t)lc.upvals[k].index, line);
    }
    emit_ab(c, tail ? OP_TAIL_CALL : OP_CALL, 0, line);
}

static void compile_cond(Compiler *c, val_t clauses, bool tail, int line) {
    /* (cond (test expr...) ... (else expr...)) */
    int end_patches[64]; int np = 0;

    while (vis_pair(clauses)) {
        val_t clause = vcar(clauses);
        val_t test   = vcar(clause);
        val_t exprs  = vcdr(clause);
        clauses      = vcdr(clauses);
        bool last    = vis_nil(clauses);

        val_t S_ELSE = sym_intern_cstr("else");
        if (test == S_ELSE) {
            compile_seq(c, exprs, tail, line);
            goto cond_done;
        }

        compile(c, test, false, line);

        /* (cond (test) ...) — test is the value */
        if (vis_nil(exprs)) {
            if (!last) {
                emit(c, OP_DUP, line);
                int skip = emit_jump(c, OP_JUMP_TRUE, line);
                emit(c, OP_POP, line);
                end_patches[np++] = skip;
            }
            /* else: test value remains on stack as result */
            continue;
        }

        /* (cond (test => proc)) */
        val_t S_ARROW = sym_intern_cstr("=>");
        if (vis_pair(exprs) && vcar(exprs) == S_ARROW && vis_pair(vcdr(exprs))) {
            val_t proc = vcar(vcdr(exprs));
            /* DUP so test value survives the JUMP_FALSE pop */
            emit(c, OP_DUP, line);
            int skip = emit_jump(c, OP_JUMP_FALSE, line);
            /* truthy: original test is on stack; push proc and swap */
            compile(c, proc, false, line);
            emit(c, OP_SWAP, line);   /* (proc test) */
            emit_ab(c, tail ? OP_TAIL_CALL : OP_CALL, 1, line);
            if (!last) end_patches[np++] = emit_jump(c, OP_JUMP, line);
            patch_jump(c, skip);
            /* #f path: JUMP_FALSE already popped dup; original #f is on stack */
            emit(c, OP_POP, line);
            continue;
        }

        int skip = emit_jump(c, OP_JUMP_FALSE, line);
        /* JUMP_FALSE already popped the test — no OP_POP needed */
        compile_seq(c, exprs, tail && last, line);
        if (!last) end_patches[np++] = emit_jump(c, OP_JUMP, line);
        patch_jump(c, skip);
    }
    emit(c, OP_VOID, line);   /* no clause matched */

cond_done:
    for (int i = 0; i < np; i++) patch_jump(c, end_patches[i]);
}

static void compile_case(Compiler *c, val_t args, bool tail, int line) {
    /* Desugar: (case key clause...) →
     *   (let ((%%k key)) (cond (clause') ...))
     * where non-else clause ((d...) body...) → ((memv %%k '(d...)) body...)
     * and   arrow clause ((d...) => proc)    → ((memv %%k '(d...)) (proc %%k)) */
    val_t key     = vcar(args);
    val_t clauses = vcdr(args);
    val_t ksym    = sym_intern_cstr("%%case-key%%");
    val_t memv    = sym_intern_cstr("memv");

    /* Build cond clauses */
    val_t cond_head = V_NIL, *cond_tail = &cond_head;
    while (vis_pair(clauses)) {
        val_t clause = vcar(clauses);  clauses = vcdr(clauses);
        val_t datums = vcar(clause);
        val_t body   = vcdr(clause);

        val_t test;
        if (datums == S_ELSE) {
            test = S_ELSE;
        } else {
            val_t quoted = scm_cons(S_QUOTE, scm_cons(datums, V_NIL));
            test = scm_cons(memv, scm_cons(ksym, scm_cons(quoted, V_NIL)));
        }

        val_t cond_body;
        if (vis_pair(body) && vcar(body) == S_ARROW && vis_pair(vcdr(body))) {
            /* (case key ((d) => proc)) → call (proc %%k) */
            val_t proc = vcar(vcdr(body));
            cond_body = scm_cons(scm_cons(proc, scm_cons(ksym, V_NIL)), V_NIL);
        } else {
            cond_body = body;
        }

        val_t cc = scm_cons(test, cond_body);
        *cond_tail = scm_cons(cc, V_NIL);
        cond_tail  = &as_pair(*cond_tail)->cdr;
    }

    val_t cond_expr = scm_cons(S_COND, cond_head);
    val_t binding   = scm_cons(scm_cons(ksym, scm_cons(key, V_NIL)), V_NIL);
    val_t let_expr  = scm_cons(S_LET, scm_cons(binding, scm_cons(cond_expr, V_NIL)));
    compile(c, let_expr, tail, line);
}

static void compile_when(Compiler *c, val_t args, bool tail, int line) {
    val_t test = vcar(args);
    val_t body = vcdr(args);
    compile(c, test, false, line);
    int skip = emit_jump(c, OP_JUMP_FALSE, line);
    /* JUMP_FALSE already popped test; compile body for truthy path */
    compile_seq(c, body, tail, line);
    int end = emit_jump(c, OP_JUMP, line);
    patch_jump(c, skip);
    /* #f path: test already popped, push void */
    emit(c, OP_VOID, line);
    patch_jump(c, end);
}

static void compile_unless(Compiler *c, val_t args, bool tail, int line) {
    val_t test = vcar(args);
    val_t body = vcdr(args);
    compile(c, test, false, line);
    int skip = emit_jump(c, OP_JUMP_TRUE, line);
    /* JUMP_TRUE already popped test; compile body for #f path */
    compile_seq(c, body, tail, line);
    int end = emit_jump(c, OP_JUMP, line);
    patch_jump(c, skip);
    /* truthy path: test already popped, push void */
    emit(c, OP_VOID, line);
    patch_jump(c, end);
}

static void compile_do(Compiler *c, val_t args, bool tail, int line) {
    /* (do ((var init step) ...) (test expr...) body...)
       Wrap in a zero-arg lambda so do vars start at slot 0, avoiding
       slot-index conflicts when do appears as a call argument. */
    val_t var_specs = vcar(args);
    val_t term      = vcar(vcdr(args));
    val_t body      = vcdr(vcdr(args));
    val_t test_expr = vcar(term);
    val_t result    = vcdr(term);

    Compiler lc;
    init_compiler(&lc, c, "<do>");
    lc.chunk->arity = 0;
    Compiler *d = &lc;       /* alias so the rest of the function is readable */
    begin_scope(d);

    /* Evaluate and bind init values */
    val_t vs = var_specs;
    while (vis_pair(vs)) {
        val_t spec = vcar(vs);
        val_t init = vcar(vcdr(spec));
        compile(d, init, false, line);
        add_local(d, vcar(spec));
        mark_initialised(d);
        vs = vcdr(vs);
    }

    /* Loop head */
    int loop_start = chunk_pos(d->chunk);

    /* Test — OP_JUMP_TRUE pops its condition in both branches */
    compile(d, test_expr, false, line);
    int exit_jmp = emit_jump(d, OP_JUMP_TRUE, line);

    /* Body (loop-continues path) */
    vs = body;
    while (vis_pair(vs)) {
        val_t next = vcdr(vs);
        compile(d, vcar(vs), false, line);
        emit(d, OP_POP, line);   /* discard body expression results */
        vs = next;
    }

    /* Compute all step values first (so they don't see their own update) */
    int nv = 0;
    vs = var_specs;
    while (vis_pair(vs)) { nv++; vs = vcdr(vs); }

    vs = var_specs;
    while (vis_pair(vs)) {
        val_t spec = vcar(vs);
        val_t step = vis_pair(vcdr(vcdr(spec))) ? vcar(vcdr(vcdr(spec))) : vcar(spec);
        compile(d, step, false, line);
        vs = vcdr(vs);
    }
    int base = d->local_count - nv;
    for (int i = nv - 1; i >= 0; i--)
        emit_ab(d, OP_STORE_LOCAL, (uint8_t)(base + i), line);

    /* Jump back to loop head */
    int back = emit_jump(d, OP_JUMP, line);
    chunk_patch16(d->chunk, back, (uint16_t)loop_start);

    /* Exit (condition already popped by JUMP_TRUE) */
    patch_jump(d, exit_jmp);

    if (vis_nil(result))
        emit(d, OP_VOID, line);
    else
        compile_seq(d, result, true, line);

    end_scope(d, line);

    Chunk *dch = end_compiler(d);
    int ci = chunk_add_const(c->chunk, (val_t)(uintptr_t)dch);
    emit_ab(c, OP_CLOSURE, (uint8_t)ci, line);
    for (int k = 0; k < lc.upval_count; k++) {
        chunk_emit(c->chunk, lc.upvals[k].is_local ? 1 : 0, line);
        chunk_emit(c->chunk, (uint8_t)lc.upvals[k].index, line);
    }
    emit_ab(c, tail ? OP_TAIL_CALL : OP_CALL, 0, line);
}

/* ── with-exception-handler native compilation ───────────────────────── */

static void compile_with_exception_handler(Compiler *c, val_t args,
                                           bool tail, int line) {
    /* (with-exception-handler handler thunk)
     *
     * Emitted bytecode:
     *   <handler>               ← pushed first; lives below the call
     *   OP_PUSH_HANDLER catch   ← installs setjmp; saves sp (past handler)
     *   <thunk>                 ← pushed after the save point
     *   OP_CALL 0               ← (thunk) → result; stack: [handler result]
     *   OP_POP_HANDLER          ← normal exit: remove handler
     *   OP_SWAP; OP_POP         ← discard handler, keep result
     *   OP_JUMP end
     *  catch:
     *   ; sp restored to past handler, exception pushed → [handler exn]
     *   OP_CALL 1               ← (handler exn) → result
     *  end:
     */
    val_t handler_expr = vcar(args);
    val_t thunk_expr   = vcar(vcdr(args));

    /* Compile handler (stays below the protected call on the stack) */
    compile(c, handler_expr, false, line);

    /* OP_PUSH_HANDLER: saves sp at this point (past handler, before thunk) */
    int catch_placeholder = emit_jump(c, OP_PUSH_HANDLER, line);

    /* Compile thunk and call it with no arguments */
    compile(c, thunk_expr, false, line);
    emit_ab(c, OP_CALL, 0, line);          /* (thunk) → result */

    /* Normal path: remove handler, discard it, keep result */
    emit(c, OP_POP_HANDLER, line);
    emit(c, OP_SWAP, line);                /* [result handler] */
    emit(c, OP_POP, line);                 /* [result] */
    int end_jmp = emit_jump(c, OP_JUMP, line);

    /* Catch path: exception is on top, handler is below */
    patch_jump(c, catch_placeholder);
    emit_ab(c, tail ? OP_TAIL_CALL : OP_CALL, 1, line); /* (handler exn) */

    patch_jump(c, end_jmp);
}

/* ── parameterize desugaring ─────────────────────────────────────────── */

static void compile_guard(Compiler *c, val_t args, bool tail, int line) {
    /* (guard (var clause...) body...)
     * Expands to:
     *   (call/cc (lambda (%guard-k)
     *     (with-exception-handler
     *       (lambda (var)
     *         (cond (test (%guard-k expr)) ... (else (raise var))))
     *       (lambda () body...))))
     * Each (else expr) clause wraps expr in (%guard-k ...) rather than raise. */
    val_t var_and_clauses = vcar(args);
    val_t body            = vcdr(args);
    val_t var             = vcar(var_and_clauses);
    val_t clauses         = vcdr(var_and_clauses);

    val_t S_WEH   = sym_intern_cstr("with-exception-handler");
    val_t S_RAISE = sym_intern_cstr("raise");
    val_t S_COND2 = sym_intern_cstr("cond");
    val_t S_ELSE2 = sym_intern_cstr("else");
    val_t gk      = sym_intern_cstr("%guard-k");

    /* Count clauses and check for else */
    bool has_else = false;
    val_t cl = clauses;
    while (vis_pair(cl)) {
        val_t test = vcar(vcar(cl));
        if (test == S_ELSE2) has_else = true;
        cl = vcdr(cl);
    }

    /* Build cond clauses in forward order (collect into array, then cons) */
    val_t clause_arr[64];
    int ci = 0;
    cl = clauses;
    while (vis_pair(cl) && ci < 64) {
        val_t clause = vcar(cl);
        val_t test   = vcar(clause);
        val_t cbody  = vcdr(clause);
        val_t rest;
        if (!vis_pair(cbody)) {
            /* Bodyless clause (test) — return test's own value without
             * re-evaluating it, via cond's => arrow form (proc receives
             * the test's value as its one argument). */
            rest = scm_cons(S_ARROW, scm_cons(gk, V_NIL));
        } else {
            /* A clause body is (expr ...), i.e. an implicit begin — wrap
             * multi-expression bodies in an explicit (begin ...) so every
             * expression actually runs, not just the first. */
            val_t body_expr = vis_pair(vcdr(cbody))
                ? scm_cons(S_BEGIN, cbody)
                : vcar(cbody);
            rest = scm_cons(scm_cons(gk, scm_cons(body_expr, V_NIL)), V_NIL);
        }
        clause_arr[ci++] = scm_cons(test == S_ELSE2 ? S_ELSE2 : test, rest);
        cl = vcdr(cl);
    }
    /* Append default (else (raise var)) if no else clause present */
    if (!has_else && ci < 64) {
        val_t raise_form = scm_cons(S_RAISE, scm_cons(var, V_NIL));
        clause_arr[ci++] = scm_cons(S_ELSE2, scm_cons(raise_form, V_NIL));
    }
    /* Build cond form from clause_arr in reverse (cons builds reversed list) */
    val_t cond_list = V_NIL;
    for (int i = ci - 1; i >= 0; i--)
        cond_list = scm_cons(clause_arr[i], cond_list);
    val_t handler_cond = scm_cons(S_COND2, cond_list);

    /* (lambda (var) cond-form) */
    val_t handler_lam = scm_cons(S_LAMBDA,
                            scm_cons(scm_cons(var, V_NIL),
                                scm_cons(handler_cond, V_NIL)));
    /* (lambda () body...) */
    val_t thunk_lam = scm_cons(S_LAMBDA, scm_cons(V_NIL, body));
    /* (with-exception-handler handler thunk) */
    val_t weh_form = scm_cons(S_WEH,
                        scm_cons(handler_lam, scm_cons(thunk_lam, V_NIL)));
    /* (lambda (%guard-k) weh-form) */
    val_t outer_lam = scm_cons(S_LAMBDA,
                         scm_cons(scm_cons(gk, V_NIL),
                             scm_cons(weh_form, V_NIL)));
    /* (call/cc outer-lam) */
    val_t expanded = scm_cons(S_CALL_CC, scm_cons(outer_lam, V_NIL));
    compile(c, expanded, tail, line);
}

static void compile_parameterize(Compiler *c, val_t args, bool tail, int line) {
    /* (parameterize ((p1 v1) (p2 v2) ...) body ...)
     * Desugar to:
     *   (let ((%%p0 p1) (%%p1 p2) ...)
     *     (let ((%%old0 (%%p0)) (%%old1 (%%p1)) ...)
     *       (dynamic-wind
     *         (lambda () (%%p0 v1) (%%p1 v2) ...)
     *         (lambda () body ...)
     *         (lambda () (%%p0 %%old0) (%%p1 %%old1) ...))))
     * so local variables in body are captured as upvalues, not looked up
     * in GLOBAL_ENV. */
    val_t param_list = vcar(args);
    val_t body       = vcdr(args);

    /* Count bindings */
    int n = 0;
    { val_t b = param_list; while (vis_pair(b)) { n++; b = vcdr(b); } }

    if (n == 0) {
        compile_seq(c, body, tail, line);
        return;
    }

    /* Extract per-binding data and generate gensym names */
#define MAX_PARAMS 32
    val_t pref[MAX_PARAMS], oref[MAX_PARAMS];
    val_t param_expr[MAX_PARAMS], val_expr[MAX_PARAMS];
    char namebuf[32];
    if (n > MAX_PARAMS) n = MAX_PARAMS;

    val_t b = param_list;
    for (int i = 0; i < n; i++, b = vcdr(b)) {
        val_t binding = vcar(b);
        param_expr[i] = vcar(binding);
        val_expr[i]   = vcar(vcdr(binding));
        snprintf(namebuf, sizeof(namebuf), "%%prm%d", i);
        pref[i] = sym_intern_cstr(namebuf);
        snprintf(namebuf, sizeof(namebuf), "%%old%d", i);
        oref[i] = sym_intern_cstr(namebuf);
    }
#undef MAX_PARAMS

    val_t S_DW = sym_intern_cstr("dynamic-wind");

    /* Build outer let bindings: ((%%p0 p1) ...) */
    val_t outer_bindings = V_NIL;
    for (int i = n - 1; i >= 0; i--)
        outer_bindings = scm_cons(scm_cons(pref[i],
                                  scm_cons(param_expr[i], V_NIL)),
                                  outer_bindings);

    /* Build inner let bindings: ((%%old0 (%%p0)) ...) */
    val_t inner_bindings = V_NIL;
    for (int i = n - 1; i >= 0; i--)
        inner_bindings = scm_cons(scm_cons(oref[i],
                                  scm_cons(scm_cons(pref[i], V_NIL), V_NIL)),
                                  inner_bindings);

    /* before-lambda body: ((%%p0 v1) ...) */
    val_t before_body = V_NIL;
    for (int i = n - 1; i >= 0; i--)
        before_body = scm_cons(scm_cons(pref[i], scm_cons(val_expr[i], V_NIL)),
                               before_body);
    val_t before_lam = scm_cons(S_LAMBDA, scm_cons(V_NIL, before_body));

    /* after-lambda body: ((%%p0 %%old0) ...) */
    val_t after_body = V_NIL;
    for (int i = n - 1; i >= 0; i--)
        after_body = scm_cons(scm_cons(pref[i], scm_cons(oref[i], V_NIL)),
                              after_body);
    val_t after_lam = scm_cons(S_LAMBDA, scm_cons(V_NIL, after_body));

    /* thunk-lambda: (lambda () body ...) */
    val_t thunk_lam = scm_cons(S_LAMBDA, scm_cons(V_NIL, body));

    /* (dynamic-wind before thunk after) */
    val_t dwind = scm_cons(S_DW,
                   scm_cons(before_lam,
                    scm_cons(thunk_lam,
                     scm_cons(after_lam, V_NIL))));

    /* (let inner-bindings (dynamic-wind ...)) */
    val_t inner_let = scm_cons(S_LET,
                       scm_cons(inner_bindings,
                        scm_cons(dwind, V_NIL)));

    /* (let outer-bindings inner-let) */
    val_t outer_let = scm_cons(S_LET,
                       scm_cons(outer_bindings,
                        scm_cons(inner_let, V_NIL)));

    compile(c, outer_let, tail, line);
}

/* True iff expr is literally (quote SYM) for some symbol SYM — the shape a
 * source-level 'sym reader-expands to. Used by compile_define_algebra (and
 * its matching internal-define prescan case in compile_lambda above) to
 * detect the overwhelmingly common case where an operator name is a
 * compile-time-known literal rather than a runtime-computed expression.
 * Compares via akk_translate rather than a raw S_QUOTE check, so an
 * Akkadian/cuneiform spelling of quote (e.g. kīma) is recognized too —
 * found by review: without this, (define-algebra (kīma myop) ...) missed
 * the compile-time-literal fast path and fell back to the tree-eval path,
 * reproducing the global-leak bug this function exists to fix for that
 * one spelling. */
static bool is_quoted_symbol(val_t expr, val_t *out_sym) {
    if (vis_pair(expr) && akk_translate(vcar(expr)) == S_QUOTE &&
        vis_pair(vcdr(expr)) && vis_symbol(vcar(vcdr(expr))) &&
        vcdr(vcdr(expr)) == V_NIL) {
        *out_sym = vcar(vcdr(expr));
        return true;
    }
    return false;
}

/* Shared by compile_define_rule and compile_define_ruleset: parse one
 * (pattern -> template [#:when guard]) clause. Mirrors eval.c's
 * S_DEFINE_RULE/S_DEFINE_RULESET parsing exactly — the arrow token's own
 * value is never checked, only its position (element 1), matching the
 * pre-existing tree-walker's leniency there. */
static void parse_rule_clause(val_t clause, val_t *pattern, val_t *tmpl, val_t *guard_expr) {
    *pattern = vcar(clause);
    *tmpl    = vcar(vcdr(vcdr(clause)));
    val_t trailing = vcdr(vcdr(vcdr(clause)));
    *guard_expr = V_FALSE;
    if (vis_pair(trailing) && vcar(trailing) == S_KW_WHEN && vis_pair(vcdr(trailing)))
        *guard_expr = vcar(vcdr(trailing));
}

/* Build one (%define-rule! 'pattern 'pvars guard-lambda action-lambda
 * ruleset-name-ast) call — the runtime registration this clause desugars
 * to. pvars is computed HERE, at compile time, via sx_pattern_vars: the
 * pattern is always static source syntax (never evaluated, matching the
 * tree-walker), so there is no need to re-derive it at runtime on every
 * call the way eval.c's tree-walker case does on every invocation.
 * ruleset_name_ast is (quote NAME) for define-ruleset or V_FALSE (itself,
 * self-evaluating) for a standalone define-rule. */
static val_t build_define_rule_call(val_t pattern, val_t tmpl, val_t guard_expr,
                                     val_t ruleset_name_ast) {
    val_t pvars = sx_pattern_vars(pattern);

    val_t pattern_quoted = scm_cons(S_QUOTE, scm_cons(pattern, V_NIL));
    val_t pvars_quoted   = scm_cons(S_QUOTE, scm_cons(pvars, V_NIL));

    val_t action_lam = scm_cons(S_LAMBDA, scm_cons(pvars, scm_cons(tmpl, V_NIL)));
    val_t guard_lam  = (guard_expr != V_FALSE)
        ? scm_cons(S_LAMBDA, scm_cons(pvars, scm_cons(guard_expr, V_NIL)))
        : V_FALSE;

    val_t reg_sym = sym_intern_cstr("%define-rule!");
    return scm_cons(reg_sym,
            scm_cons(pattern_quoted,
             scm_cons(pvars_quoted,
              scm_cons(guard_lam,
               scm_cons(action_lam,
                scm_cons(ruleset_name_ast, V_NIL))))));
}

/* (define-rule pattern -> template [#:when guard]) — one rewrite rule.
 * Desugars to a single build_define_rule_call, compiled as an ordinary
 * call. Unlike define-record-type/symbolic/with-assumptions, this needs no
 * internal-define prescan slot reservation: it binds no name into the
 * enclosing scope at all (see compile_lambda's prescan, S_DEFINE_RULE
 * case) — it only registers a rule into sx_rules.c's process-wide rule
 * table, so there is no lexical-scoping bug to fix here, only the
 * tree-walker's guard/template closures being built against GLOBAL_ENV
 * unconditionally (via tree-eval) instead of the actual enclosing lexical
 * environment, which broke any guard/template referencing a local
 * variable. Native codegen fixes that the same way compile_lambda always
 * has: ordinary closures over the real enclosing scope. */
static void compile_define_rule(Compiler *c, val_t rest, bool tail, int line) {
    if (!vis_pair(rest) || !vis_pair(vcdr(rest)) || !vis_pair(vcdr(vcdr(rest)))) {
        fprintf(stderr, "compiler: define-rule: expected (define-rule pattern -> template)\n");
        emit(c, OP_VOID, line);
        return;
    }
    val_t pattern, tmpl, guard_expr;
    parse_rule_clause(rest, &pattern, &tmpl, &guard_expr);
    val_t call = build_define_rule_call(pattern, tmpl, guard_expr, V_FALSE);
    compile(c, call, tail, line);
}

/* (define-ruleset name (pattern -> template [#:when guard]) ...) — same
 * per-clause desugaring as compile_define_rule, looped over every clause
 * and tagged with the ruleset name (a bare, never-evaluated symbol,
 * matching the tree-walker — quoted here since it's compile-time-known).
 * Malformed clauses are skipped, matching eval.c's per-clause `continue`. */
static void compile_define_ruleset(Compiler *c, val_t rest, bool tail, int line) {
    if (!vis_pair(rest)) {
        fprintf(stderr, "compiler: define-ruleset: expected (define-ruleset name clause ...)\n");
        emit(c, OP_VOID, line);
        return;
    }
    val_t name         = vcar(rest);
    val_t name_quoted  = scm_cons(S_QUOTE, scm_cons(name, V_NIL));

    /* Collect in reverse (cheap prepend), then reverse back so rules
     * register — and therefore fire, per sx_rules.c's append-to-chain-end
     * ordering — in source order, matching the tree-walker. */
    val_t calls_rev = V_NIL;
    int n = 0;
    for (val_t cl = vcdr(rest); vis_pair(cl); cl = vcdr(cl)) {
        val_t clause = vcar(cl);
        if (!vis_pair(clause) || !vis_pair(vcdr(clause)) || !vis_pair(vcdr(vcdr(clause))))
            continue; /* skip malformed clause, matching eval.c */
        val_t pattern, tmpl, guard_expr;
        parse_rule_clause(clause, &pattern, &tmpl, &guard_expr);
        calls_rev = scm_cons(build_define_rule_call(pattern, tmpl, guard_expr, name_quoted),
                              calls_rev);
        n++;
    }
    if (n == 0) { emit(c, OP_VOID, line); return; }

    val_t calls = V_NIL;
    for (val_t p = calls_rev; vis_pair(p); p = vcdr(p)) calls = scm_cons(vcar(p), calls);

    compile_seq(c, calls, tail, line);
}

/* (define-algebra op-expr [#:commutative? b] [#:associative? b]
 *                 [#:identity v] [#:absorbing v] [#:relations fn])
 * Registers algebra info for op-expr's value and auto-binds it to
 * (lambda args (apply sym-expr 'op args)) — see eval.c's S_DEFINE_ALGEBRA.
 *
 * The registration call is always the same shape: (%define-algebra!
 * op-expr kw1 val1-expr kw2 val2-expr ...), with keyword values compiled
 * as ordinary runtime expressions (matching the tree-walker, which
 * evaluates them too — only the keyword tokens themselves are literal).
 *
 * The auto-bind's scoping is where this diverges from a pure mechanical
 * translation, and is worth spelling out: when op-expr is a literal
 * (quote sym) — by far the common usage, see tests/sx_algebra_tests.scm —
 * the bound name is known at COMPILE time, so it gets a real lexical
 * binding via an ordinary (define sym ...), exactly like
 * compile_define_record_type's bindings: correct at top level AND
 * correctly local when used inside a lambda body, unlike the tree-walker's
 * env_define(env, op_name, proc), which — reached only via tree-eval's
 * hardcoded GLOBAL_ENV — always leaked the binding to global scope even
 * when define-algebra appeared inside a function (confirmed: `(define (f)
 * (define-algebra 'myop ...) ...)` left `myop` callable at top level after
 * calling f once).
 *
 * When op-expr is NOT a literal quoted symbol (its value is only known at
 * runtime), there is no way to give it a real lexical binding in a
 * slot-based compiled VM — every local's stack slot is fixed at compile
 * time, the same fundamental limit R7RS's own (define <computed-name> ...)
 * runs into. This rare case is left on the pre-existing tree-eval path
 * (dynamic env_define against GLOBAL_ENV), which is the correct place for
 * a genuinely dynamic top-level-only binding to live, not a bug to fix. */
static void compile_define_algebra(Compiler *c, val_t rest, bool tail, int line) {
    if (!vis_pair(rest)) {
        fprintf(stderr, "compiler: define-algebra: expected operator as first argument\n");
        emit(c, OP_VOID, line);
        return;
    }
    val_t op_expr = vcar(rest);
    val_t kws     = vcdr(rest);

    val_t sym;
    if (!is_quoted_symbol(op_expr, &sym)) {
        /* Dynamic operator name: keep the existing tree-eval behavior. */
        val_t tree_eval_sym = sym_intern_cstr("tree-eval");
        emit_ab(c, OP_LOAD_GLOBAL,
                (uint8_t)chunk_add_const(c->chunk, tree_eval_sym), line);
        emit_const(c, scm_cons(S_DEFINE_ALGEBRA, rest), line);
        emit_ab(c, tail ? OP_TAIL_CALL : OP_CALL, 1, line);
        return;
    }

    val_t reg_sym  = sym_intern_cstr("%define-algebra!");
    val_t reg_call = scm_cons(reg_sym, scm_cons(op_expr, kws));

    val_t sym_expr_sym = sym_intern_cstr("sym-expr");
    val_t apply_sym    = sym_intern_cstr("apply");
    val_t args_sym     = sym_intern_cstr("args");
    val_t op_quoted    = scm_cons(S_QUOTE, scm_cons(sym, V_NIL));
    val_t body = scm_cons(apply_sym,
                  scm_cons(sym_expr_sym,
                   scm_cons(op_quoted,
                    scm_cons(args_sym, V_NIL))));
    val_t lam      = scm_cons(S_LAMBDA, scm_cons(args_sym, scm_cons(body, V_NIL)));
    val_t def_form = scm_cons(S_DEFINE, scm_cons(sym, scm_cons(lam, V_NIL)));

    compile_seq(c, scm_cons(reg_call, scm_cons(def_form, V_NIL)), tail, line);
}

/* (with-assumptions ((var assumption...) ...) body...) — mirrors
 * compile_parameterize immediately above almost exactly: same
 * capture-old/set-new/dynamic-wind-restore shape, just swapping the
 * parameter-procedure call for two tiny new primitives
 * (%assumption-flags, %assumption-set!, %assumption-restore! — see
 * builtins_curry.c) that read/OR-in/overwrite a SymVar's assumption
 * bits directly, matching the tree-walker's S_WITH_ASSUMPTIONS case
 * (eval.c) which this replaces for compiled code. Each clause's
 * assumption keywords (bare symbols, never evaluated — matching the
 * tree-walker) are resolved to a flag bitmask at COMPILE time via
 * sx_assumption_flag and embedded as a self-evaluating fixnum
 * constant, so there's no runtime keyword-lookup cost per entry.
 *
 * One deliberate behavioral divergence from the tree-walker, found by
 * review: eval.c's S_WITH_ASSUMPTIONS interleaves each clause's
 * snapshot-then-set, so if the SAME SymVar appears in two clauses of
 * one with-assumptions form, the second clause's snapshot already
 * includes the first clause's flags, and only the first clause's
 * original value is restored — leaving a residual flag set after the
 * form exits. Here all clauses' original flags are snapshotted
 * upfront (the inner let, before any %assumption-set! runs), so a
 * repeated var is restored to its TRUE original state. Strictly more
 * correct; not expected to be relied upon either way, so not treated
 * as a compatibility break — see tests/sx_algebra_tests.scm.
 * Desugars to:
 *   (let ((%%wa-v0 var-expr0) ...)
 *     (let ((%%wa-o0 (%assumption-flags %%wa-v0)) ...)
 *       (dynamic-wind
 *         (lambda () (%assumption-set! %%wa-v0 FLAGS0) ...)
 *         (lambda () body...)
 *         (lambda () (%assumption-restore! %%wa-v0 %%wa-o0) ...)))) */
static void compile_with_assumptions(Compiler *c, val_t args, bool tail, int line) {
    if (!vis_pair(args)) {
        fprintf(stderr, "compiler: with-assumptions: missing binding list\n");
        emit(c, OP_VOID, line);
        return;
    }
    val_t clauses = vcar(args);
    val_t body    = vcdr(args);

    int n = 0;
    { val_t cl = clauses; while (vis_pair(cl)) { n++; cl = vcdr(cl); } }

#define WA_MAX_CLAUSES 32
    val_t vref[WA_MAX_CLAUSES], oref[WA_MAX_CLAUSES];
    val_t var_expr[WA_MAX_CLAUSES], flags_const[WA_MAX_CLAUSES];
    char namebuf[32];
    if (n > WA_MAX_CLAUSES) n = WA_MAX_CLAUSES;

    val_t cl = clauses;
    for (int i = 0; i < n; i++, cl = vcdr(cl)) {
        val_t clause = vcar(cl);
        if (!vis_pair(clause)) { i--; n--; continue; }
        var_expr[i] = vcar(clause);
        uint32_t flags = 0;
        for (val_t a = vcdr(clause); vis_pair(a); a = vcdr(a))
            flags |= sx_assumption_flag(vcar(a));
        flags_const[i] = vfix((intptr_t)flags);
        snprintf(namebuf, sizeof(namebuf), "%%wa-v%d", i);
        vref[i] = sym_intern_cstr(namebuf);
        snprintf(namebuf, sizeof(namebuf), "%%wa-o%d", i);
        oref[i] = sym_intern_cstr(namebuf);
    }
#undef WA_MAX_CLAUSES

    if (n == 0) {
        compile_seq(c, body, tail, line);
        return;
    }

    val_t flags_sym   = sym_intern_cstr("%assumption-flags");
    val_t set_sym     = sym_intern_cstr("%assumption-set!");
    val_t restore_sym = sym_intern_cstr("%assumption-restore!");
    val_t S_DW        = sym_intern_cstr("dynamic-wind");

    /* Outer let bindings: ((%wa-v0 var-expr0) ...) */
    val_t outer_bindings = V_NIL;
    for (int i = n - 1; i >= 0; i--)
        outer_bindings = scm_cons(scm_cons(vref[i], scm_cons(var_expr[i], V_NIL)),
                                  outer_bindings);

    /* Inner let bindings: ((%wa-o0 (%assumption-flags %wa-v0)) ...) */
    val_t inner_bindings = V_NIL;
    for (int i = n - 1; i >= 0; i--)
        inner_bindings = scm_cons(
            scm_cons(oref[i],
             scm_cons(scm_cons(flags_sym, scm_cons(vref[i], V_NIL)), V_NIL)),
            inner_bindings);

    /* before-lambda body: ((%assumption-set! %wa-v0 FLAGS0) ...) */
    val_t before_body = V_NIL;
    for (int i = n - 1; i >= 0; i--)
        before_body = scm_cons(
            scm_cons(set_sym, scm_cons(vref[i], scm_cons(flags_const[i], V_NIL))),
            before_body);
    val_t before_lam = scm_cons(S_LAMBDA, scm_cons(V_NIL, before_body));

    /* after-lambda body: ((%assumption-restore! %wa-v0 %wa-o0) ...) */
    val_t after_body = V_NIL;
    for (int i = n - 1; i >= 0; i--)
        after_body = scm_cons(
            scm_cons(restore_sym, scm_cons(vref[i], scm_cons(oref[i], V_NIL))),
            after_body);
    val_t after_lam = scm_cons(S_LAMBDA, scm_cons(V_NIL, after_body));

    /* thunk-lambda: (lambda () body...) */
    if (body == V_NIL) body = scm_cons(V_VOID, V_NIL);
    val_t thunk_lam = scm_cons(S_LAMBDA, scm_cons(V_NIL, body));

    val_t dwind = scm_cons(S_DW,
                   scm_cons(before_lam,
                    scm_cons(thunk_lam,
                     scm_cons(after_lam, V_NIL))));

    val_t inner_let = scm_cons(S_LET, scm_cons(inner_bindings, scm_cons(dwind, V_NIL)));
    val_t outer_let = scm_cons(S_LET, scm_cons(outer_bindings, scm_cons(inner_let, V_NIL)));

    compile(c, outer_let, tail, line);
}

/* (receive formals producer-expr body...) — R7RS sugar over
 * call-with-values, which is already a plain builtin primitive (no VM
 * support needed).  Desugar to:
 *   (call-with-values (lambda () producer-expr) (lambda formals body...)) */
static void compile_receive(Compiler *c, val_t args, bool tail, int line) {
    val_t formals  = vcar(args);
    val_t producer = vcar(vcdr(args));
    val_t body     = vcdr(vcdr(args));

    val_t producer_lam = scm_cons(S_LAMBDA, scm_cons(V_NIL, scm_cons(producer, V_NIL)));
    val_t consumer_lam = scm_cons(S_LAMBDA, scm_cons(formals, body));
    val_t cwv = scm_cons(S_CALL_WITH_VALUES,
                 scm_cons(producer_lam,
                  scm_cons(consumer_lam, V_NIL)));

    compile(c, cwv, tail, line);
}

/* ── Application compilation ─────────────────────────────────────────── */

static void compile_call(Compiler *c, val_t head, val_t args, bool tail, int line) {
    /* Count args */
    int argc = 0;
    val_t a = args;
    while (vis_pair(a)) { argc++; a = vcdr(a); }

    /* Compile callee */
    compile(c, head, false, line);

    /* Compile arguments */
    a = args;
    while (vis_pair(a)) {
        compile(c, vcar(a), false, line);
        a = vcdr(a);
    }

    emit_ab(c, tail ? OP_TAIL_CALL : OP_CALL, (uint8_t)argc, line);
}

/* ── Main dispatch ───────────────────────────────────────────────────── */

static void compile(Compiler *c, val_t expr, bool tail, int line) {

    /* Reader-annotated source line: the reader stamps each cons cell's
       hdr.flags with the 1-based line its car's datum started on (0 for
       pairs built by macros/runtime, which inherit the enclosing line). */
    if (vis_pair(expr) && as_pair(expr)->hdr.flags)
        line = (int)as_pair(expr)->hdr.flags;

    /* ── Self-evaluating atoms ── */
    if (vis_fixnum(expr) || vis_flonum(expr) || vis_bignum(expr) ||
        vis_rational(expr) || vis_complex(expr) || vis_string(expr) ||
        vis_char(expr)) {
        emit_const(c, expr, line);
        return;
    }
    if (expr == V_TRUE)  { emit(c, OP_TRUE,  line); return; }
    if (expr == V_FALSE) { emit(c, OP_FALSE, line); return; }
    if (expr == V_NIL)   { emit(c, OP_NIL,   line); return; }
    if (expr == V_VOID)  { emit(c, OP_VOID,  line); return; }

    /* ── Symbol → variable load ── */
    if (vis_symbol(expr)) {
        /* #:keyword symbols (Guile/Racket-style) are self-evaluating */
        Symbol *ksym = as_sym(expr);
        if (ksym->len >= 2 && ksym->data[0] == '#' && ksym->data[1] == ':') {
            emit_const(c, expr, line);
            return;
        }
        emit_load(c, expr, line);
        return;
    }

    /* ── Non-pair non-symbol: quote it ── */
    if (!vis_pair(expr)) {
        emit_const(c, expr, line);
        return;
    }

    /* ── Compound form (head . args) ── */
    val_t head = akk_translate(vcar(expr));
    val_t args = vcdr(expr);

    /* quote */
    if (head == S_QUOTE) {
        emit_const(c, vis_pair(args) ? vcar(args) : V_NIL, line);
        return;
    }

    /* quasiquote — expand to list-construction code, then compile that */
    if (head == S_QUASIQUOTE) {
        val_t expanded = expand_qq(vis_pair(args) ? vcar(args) : V_NIL, GLOBAL_ENV, 0);
        compile(c, expanded, tail, line);
        return;
    }

    /* if */
    if (head == S_IF) { compile_if(c, args, tail, line); return; }

    /* begin */
    if (head == S_BEGIN) { compile_begin(c, args, tail, line); return; }

    /* define */
    if (head == S_DEFINE) { compile_define(c, args, line); return; }

    /* set! */
    if (head == S_SET) { compile_set(c, args, line); return; }

    /* lambda */
    if (head == S_LAMBDA) {
        val_t params = vcar(args);
        val_t body   = vcdr(args);
        compile_lambda(c, params, body, NULL, line);
        return;
    }

    /* let */
    if (head == S_LET)      { compile_let(c, args, tail, line);      return; }
    if (head == S_LET_STAR) { compile_let_star(c, args, tail, line); return; }
    if (head == S_LETREC || head == S_LETREC_STAR)
                            { compile_letrec(c, args, tail, line);   return; }

    /* and / or */
    if (head == S_AND) { compile_and(c, args, tail, line); return; }
    if (head == S_OR)  { compile_or(c, args, tail, line);  return; }

    /* cond / case */
    if (head == S_COND) { compile_cond(c, args, tail, line); return; }
    if (head == S_CASE) { compile_case(c, args, tail, line); return; }

    /* when / unless */
    if (head == S_WHEN)   { compile_when(c, args, tail, line);   return; }
    if (head == S_UNLESS) { compile_unless(c, args, tail, line); return; }

    /* do */
    if (head == S_DO) { compile_do(c, args, tail, line); return; }

    /* values */
    if (head == S_VALUES) {
        int n = 0;
        val_t a = args;
        while (vis_pair(a)) {
            compile(c, vcar(a), false, line);
            n++; a = vcdr(a);
        }
        emit_ab(c, OP_VALUES, (uint8_t)n, line);
        return;
    }

    /* apply — (apply f arg1 ... rest-list) */
    if (head == S_APPLY) {
        int n = 0;
        val_t a = args;
        while (vis_pair(a)) {
            compile(c, vcar(a), false, line);
            n++; a = vcdr(a);
        }
        emit_ab(c, OP_APPLY, (uint8_t)n, line); /* n = fn + intermediates + last-list */
        return;
    }

    /* parameterize — desugar to let + dynamic-wind in the compiler so that
       local variables in the body are captured as upvalues, not looked up
       in GLOBAL_ENV (which would fail for BcClosure-local bindings). */
    if (head == S_PARAMETERIZE) {
        compile_parameterize(c, args, tail, line);
        return;
    }

    if (head == S_GUARD) {
        compile_guard(c, args, tail, line);
        return;
    }

    if (head == S_WITH_EXCEPTION_HANDLER) {
        compile_with_exception_handler(c, args, tail, line);
        return;
    }

    /* `receive` is ambiguous: it's both the R7RS special form (requires at
     * least 2 forms after it: formals and a producer expression, body...
     * optional) and — pre-existing in this codebase — the actor-mailbox
     * primitive (arity 0-1: optional timeout). The two are unambiguous by
     * argument count alone, so only treat it as the special form when the
     * shape can't be the primitive; otherwise fall through to an ordinary
     * call so (receive) and (receive timeout) keep working. */
    if (head == S_RECEIVE && vis_pair(args) && vis_pair(vcdr(args))) {
        compile_receive(c, args, tail, line);
        return;
    }

    if (head == S_DEFINE_RECORD_TYPE) {
        compile_define_record_type(c, args, line);
        return;
    }

    if (head == S_DEFINE_SYNTAX) {
        compile_define_syntax(c, args, line);
        return;
    }

    if (head == S_LET_SYNTAX || head == S_LETREC_SYNTAX) {
        compile_let_syntax(c, args, tail, line);
        return;
    }

    if (head == S_SYMBOLIC) {
        compile_symbolic(c, args, line);
        return;
    }

    if (head == S_WITH_ASSUMPTIONS) {
        compile_with_assumptions(c, args, tail, line);
        return;
    }

    if (head == S_DEFINE_RULE) {
        compile_define_rule(c, args, tail, line);
        return;
    }

    if (head == S_DEFINE_RULESET) {
        compile_define_ruleset(c, args, tail, line);
        return;
    }

    if (head == S_DEFINE_ALGEBRA) {
        compile_define_algebra(c, args, tail, line);
        return;
    }

    /* Note: syntax-rules itself is deliberately NOT special-cased here.
     * syntax_rules_register() binds the symbol `syntax-rules` to a T_SYNTAX
     * in GLOBAL_ENV whose transformer (sr_compile_fn) takes the raw
     * (syntax-rules literals rule...) form and returns a self-evaluating
     * T_PRIMITIVE transformer — so a (syntax-rules ...) expression, e.g. as
     * define-syntax's transformer-expr, already gets handled by the
     * ordinary "macro expansion" check below like any other macro use,
     * with no eval()/tree-eval dependency at all. */

    /* Forms the compiler delegates to the tree-walker at runtime:
       import, define-library, library.
       Emitted as: (tree-eval '<form>) */
    if (head == S_IMPORT        ||
        head == S_DEFINE_LIBRARY || head == S_LIBRARY) {
        val_t tree_eval_sym = sym_intern_cstr("tree-eval");
        emit_ab(c, OP_LOAD_GLOBAL,
                (uint8_t)chunk_add_const(c->chunk, tree_eval_sym), line);
        emit_const(c, expr, line);
        emit_ab(c, tail ? OP_TAIL_CALL : OP_CALL, 1, line);
        return;
    }

    /* Macro expansion: if head is a symbol bound to a macro, expand and
       recompile. A local macro (let-syntax/letrec-syntax/internal
       define-syntax, tracked in c->syntax_locals — see resolve_syntax_local)
       shadows a same-named global one, matching ordinary lexical scoping.
       Expansion errors are deferred to runtime via (raise ...) so that
       guard forms inside lambdas can catch zero-clause or no-match errors. */
    if (vis_symbol(head)) {
        val_t transformer;
        bool is_macro = resolve_syntax_local(c, head, &transformer);
        if (!is_macro) {
            val_t macro = env_lookup_or_false(GLOBAL_ENV, head);
            is_macro = vis_syntax(macro);
            if (is_macro) transformer = as_syntax(macro)->transformer;
        }
        if (is_macro) {
            ExnHandler h;
            val_t expanded = V_FALSE;
            val_t exn_val  = V_FALSE;
            uint64_t _expand_t0 = curry_timings_enabled ? profiling_now_ns() : 0;
            SCM_PROTECT(h,
                expanded = apply(transformer, scm_cons(expr, V_NIL)),
                exn_val  = h.exn);
            if (curry_timings_enabled)
                curry_timing_expand_ns += profiling_now_ns() - _expand_t0;
            if (exn_val != V_FALSE) {
                /* Expansion failed — emit (raise <error>) to defer to runtime */
                val_t raise_sym = sym_intern_cstr("raise");
                val_t raise_form = scm_cons(raise_sym,
                                       scm_cons(scm_cons(S_QUOTE,
                                                    scm_cons(exn_val, V_NIL)),
                                                V_NIL));
                compile(c, raise_form, tail, line);
            } else {
                compile(c, expanded, tail, line);
            }
            return;
        }
    }

    /* Fallthrough: function call */
    compile_call(c, head, args, tail, line);
}

/* Compile a list of expressions; only the last is in tail position */
static void compile_seq(Compiler *c, val_t list, bool tail, int line) {
    if (vis_nil(list)) { emit(c, OP_VOID, line); return; }
    while (vis_pair(list)) {
        val_t expr = vcar(list);
        val_t rest = vcdr(list);
        bool  last = vis_nil(rest);
        /* track the sequence's own cons-cell line so glue ops (OP_POP)
           are stamped with the form they follow, not the seq's first line */
        if (as_pair(list)->hdr.flags)
            line = (int)as_pair(list)->hdr.flags;
        compile(c, expr, tail && last, line);
        if (!last) emit(c, OP_POP, line);
        list = rest;
    }
}

/* ── Public API ──────────────────────────────────────────────────────── */

val_t compiler_compile(val_t expr) {
    static bool akk_ready = false;
    if (!akk_ready) { akk_eval_setup(); akk_ready = true; }

    gc_inhibit_minor();
    Compiler c;
    init_compiler(&c, NULL, "<toplevel>");

    int line = g_reader_last_line;
    compile(&c, expr, false, line);
    chunk_emit(c.chunk, OP_RETURN, line);
    c.chunk->upval_count = 0;

    BcClosure *cl = vm_make_closure(c.chunk, 0);
    gc_resume_minor();
    return vptr(cl);
}

val_t compiler_compile_script(val_t expr_list) {
    gc_inhibit_minor();
    Compiler c;
    init_compiler(&c, NULL, "<script>");
    c.chunk->arity = 0;

    compile_seq(&c, expr_list, false, 0);
    chunk_emit(c.chunk, OP_RETURN, 0);
    c.chunk->upval_count = 0;

    BcClosure *cl = vm_make_closure(c.chunk, 0);
    gc_resume_minor();
    return vptr(cl);
}
