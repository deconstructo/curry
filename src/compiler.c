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

/* ── Compiler scope structures ───────────────────────────────────────── */

#define MAX_LOCALS  256
#define MAX_UPVALS  256

typedef struct {
    val_t  name;       /* interned symbol                                */
    int    depth;      /* scope depth at declaration                     */
    bool   captured;   /* captured by an inner closure?                  */
} Local;

typedef struct {
    bool  is_local;    /* captures from enclosing frame locals (true)
                          or from enclosing closure's upvalues (false)  */
    int   index;
    val_t name;        /* interned symbol — for JIT upval_names table   */
} UpvalDesc;

typedef struct Compiler {
    struct Compiler *enclosing;

    Chunk     *chunk;
    const char *name;

    Local      locals[MAX_LOCALS];
    int        local_count;
    int        scope_depth;

    UpvalDesc  upvals[MAX_UPVALS];
    int        upval_count;
} Compiler;

/* ── Forward declarations ────────────────────────────────────────────── */
static void compile(Compiler *c, val_t expr, bool tail, int line);
static void compile_seq(Compiler *c, val_t list, bool tail, int line);

/* ── Compiler lifecycle ──────────────────────────────────────────────── */

static void init_compiler(Compiler *c, Compiler *enc, const char *name) {
    c->enclosing   = enc;
    c->chunk       = chunk_new();
    c->name        = name;
    c->local_count = 0;
    c->scope_depth = 0;
    c->upval_count = 0;
    c->chunk->name = name;
}

static Chunk *end_compiler(Compiler *c) {
    /* Return whatever the body left on the stack (compile_seq always leaves
       one value; for tail-called BcClosures the frame is reused so OP_RETURN
       here is dead code, but it still needs to be well-formed). */
    chunk_emit(c->chunk, OP_RETURN, 0);
    c->chunk->upval_count = c->upval_count;
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
        c->local_count--;
        n++;
    }
    /* Slide TOS (the scope's result) past all the local slots below it. */
    if (n > 0)
        emit_ab(c, OP_SLIDE, (uint8_t)n, line);
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
                       akk_translate(vcar(form)) == S_DEFINE_RECORD_TYPE);
        if (is_def) {
            if (body_has_expr)
                scm_raise(V_FALSE, "internal definition after expression in body (R7RS violation)");
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
        ch->upval_names = (val_t *)gc_alloc((size_t)c.upval_count * sizeof(val_t));
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

    /* Emit store */
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

        /* Closure pushed first (callee), then init values */
        compile_lambda(c, fwd, body, NULL, line);
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
    compile_lambda(c, params, inner_body, NULL, line);
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
        val_t expr   = vis_pair(vcdr(clause)) ? vcar(vcdr(clause)) : V_VOID;
        val_t wrapped = scm_cons(gk, scm_cons(expr, V_NIL));
        if (test == S_ELSE2)
            clause_arr[ci++] = scm_cons(S_ELSE2, scm_cons(wrapped, V_NIL));
        else
            clause_arr[ci++] = scm_cons(test, scm_cons(wrapped, V_NIL));
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

    /* Forms the compiler delegates to the tree-walker at runtime:
       import, define-syntax, let-syntax, letrec-syntax,
       define-record-type, guard, receive, syntax-rules.
       Emitted as: (tree-eval '<form>) */
    if (head == S_IMPORT        || head == S_DEFINE_SYNTAX  ||
        head == S_LET_SYNTAX    || head == S_LETREC_SYNTAX  ||
        head == S_DEFINE_RECORD_TYPE ||
        head == S_DEFINE_LIBRARY || head == S_LIBRARY       ||
        head == S_RECEIVE       || head == S_SYNTAX_RULES   ||
        head == S_SYMBOLIC) {
        val_t tree_eval_sym = sym_intern_cstr("tree-eval");
        emit_ab(c, OP_LOAD_GLOBAL,
                (uint8_t)chunk_add_const(c->chunk, tree_eval_sym), line);
        emit_const(c, expr, line);
        emit_ab(c, tail ? OP_TAIL_CALL : OP_CALL, 1, line);
        return;
    }

    /* Macro expansion: if head is a symbol bound to T_SYNTAX in GLOBAL_ENV
       (i.e. a user-defined syntax-rules macro), expand and recompile.
       Expansion errors are deferred to runtime via (raise ...) so that
       guard forms inside lambdas can catch zero-clause or no-match errors. */
    if (vis_symbol(head)) {
        val_t macro = env_lookup_or_false(GLOBAL_ENV, head);
        if (vis_syntax(macro)) {
            ExnHandler h;
            val_t expanded = V_FALSE;
            val_t exn_val  = V_FALSE;
            SCM_PROTECT(h,
                expanded = apply(as_syntax(macro)->transformer,
                                 scm_cons(expr, V_NIL)),
                exn_val  = h.exn);
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
        compile(c, expr, tail && last, line);
        if (!last) emit(c, OP_POP, line);
        list = rest;
    }
}

/* ── Public API ──────────────────────────────────────────────────────── */

val_t compiler_compile(val_t expr) {
    static bool akk_ready = false;
    if (!akk_ready) { akk_eval_setup(); akk_ready = true; }

    Compiler c;
    init_compiler(&c, NULL, "<toplevel>");

    compile(&c, expr, false, 0);
    chunk_emit(c.chunk, OP_RETURN, 0);
    c.chunk->upval_count = 0;

    BcClosure *cl = vm_make_closure(c.chunk, 0);
    return vptr(cl);
}

val_t compiler_compile_script(val_t expr_list) {
    Compiler c;
    init_compiler(&c, NULL, "<script>");
    c.chunk->arity = 0;

    compile_seq(&c, expr_list, false, 0);
    chunk_emit(c.chunk, OP_RETURN, 0);
    c.chunk->upval_count = 0;

    BcClosure *cl = vm_make_closure(c.chunk, 0);
    return vptr(cl);
}
