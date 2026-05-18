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
    bool is_local;     /* captures from enclosing frame locals (true)
                          or from enclosing closure's upvalues (false)  */
    int  index;
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
    /* Implicit (void) return if body didn't tail-return */
    chunk_emit(c->chunk, OP_VOID,   0);
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
    while (c->local_count > 0 &&
           c->locals[c->local_count - 1].depth > c->scope_depth) {
        if (c->locals[c->local_count - 1].captured)
            emit(c, OP_CLOSE_UP, line);
        else
            emit(c, OP_POP, line);
        c->local_count--;
    }
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

static int add_upvalue(Compiler *c, int index, bool is_local) {
    for (int i = 0; i < c->upval_count; i++)
        if (c->upvals[i].index == index && c->upvals[i].is_local == is_local)
            return i;
    if (c->upval_count == MAX_UPVALS) {
        fprintf(stderr, "compiler: too many upvalues\n");
        return 0;
    }
    c->upvals[c->upval_count].index    = index;
    c->upvals[c->upval_count].is_local = is_local;
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
        return add_upvalue(c, local, true);
    }
    int up = resolve_upvalue(c->enclosing, name);
    if (up >= 0) return add_upvalue(c, up, false);
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

    /* Scan for internal defines and pre-declare as locals (letrec* semantics) */
    val_t bscan = body;
    while (vis_pair(bscan)) {
        val_t form = vcar(bscan);
        if (vis_pair(form) && vis_symbol(vcar(form)) &&
            akk_translate(vcar(form)) == S_DEFINE) {
            val_t defname = vcar(vcdr(form));
            if (vis_symbol(defname)) {
                /* simple (define x ...) */
                add_local(&c, defname);
                /* value is initialised below; mark depth to prevent use
                   before initialisation (leave at depth+1 sentinel) */
                c.locals[c.local_count - 1].depth = -1; /* uninitialised */
            } else if (vis_pair(defname)) {
                /* (define (f ...) ...) sugar */
                add_local(&c, vcar(defname));
                c.locals[c.local_count - 1].depth = -1;
            }
        }
        bscan = vcdr(bscan);
    }

    compile_seq(&c, body, true, line);
    Chunk *ch = end_compiler(&c);

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

    /* Named let: (let loop ((x v) ...) body) */
    if (vis_symbol(bindings)) {
        val_t loop_name = bindings;
        bindings = vcar(body);
        body     = vcdr(body);

        /* Collect init values before declaring locals */
        val_t b = bindings;
        while (vis_pair(b)) {
            compile(c, vcar(vcdr(vcar(b))), false, line);
            b = vcdr(b);
        }

        /* Build a self-referential closure */
        Compiler lc;
        init_compiler(&lc, c, as_sym(loop_name)->data);
        b = bindings;
        while (vis_pair(b)) {
            val_t param = vcar(vcar(b));
            add_local(&lc, param);
            mark_initialised(&lc);
            b = vcdr(b);
        }
        lc.chunk->arity = lc.local_count;

        /* Body of named-let loop — loop_name is a local referencing the closure */
        add_local(&lc, loop_name);
        mark_initialised(&lc);
        compile_seq(&lc, body, true, line);
        Chunk *lch = end_compiler(&lc);

        int ci = chunk_add_const(c->chunk, (val_t)(uintptr_t)lch);
        emit_ab(c, OP_CLOSURE, (uint8_t)ci, line);
        for (int i = 0; i < lc.upval_count; i++) {
            chunk_emit(c->chunk, lc.upvals[i].is_local ? 1 : 0, line);
            chunk_emit(c->chunk, (uint8_t)lc.upvals[i].index,   line);
        }

        /* Now call it with the init values already on stack */
        int argc = 0;
        b = bindings; while (vis_pair(b)) { argc++; b = vcdr(b); }
        /* Stack: [v1 v2 ... vN closure] — need to reorder */
        /* Actually: emit closure first then call with arity */
        /* Simpler: put closure first, then args */
        /* We emitted args first above — put closure below them via DUP trick */
        /* Cleanest: just call (TAIL_CALL if tail) */
        if (tail) emit_ab(c, OP_TAIL_CALL, (uint8_t)argc, line);
        else      emit_ab(c, OP_CALL,      (uint8_t)argc, line);
        return;
    }

    begin_scope(c);

    /* Evaluate all init expressions BEFORE declaring locals (parallel binding) */
    val_t b = bindings;
    while (vis_pair(b)) {
        val_t binding = vcar(b);
        val_t init    = vcar(vcdr(binding));
        compile(c, init, false, line);
        b = vcdr(b);
    }

    /* Now declare locals (values already on stack in order) */
    b = bindings;
    while (vis_pair(b)) {
        val_t binding = vcar(b);
        val_t name    = vcar(binding);
        add_local(c, name);
        mark_initialised(c);
        b = vcdr(b);
    }

    compile_seq(c, body, tail, line);
    end_scope(c, line);
}

static void compile_let_star(Compiler *c, val_t args, bool tail, int line) {
    val_t bindings = vcar(args);
    val_t body     = vcdr(args);

    begin_scope(c);
    val_t b = bindings;
    while (vis_pair(b)) {
        val_t binding = vcar(b);
        val_t name    = vcar(binding);
        val_t init    = vcar(vcdr(binding));
        compile(c, init, false, line);
        add_local(c, name);
        mark_initialised(c);
        b = vcdr(b);
    }
    compile_seq(c, body, tail, line);
    end_scope(c, line);
}

static void compile_letrec(Compiler *c, val_t args, bool tail, int line) {
    val_t bindings = vcar(args);
    val_t body     = vcdr(args);

    begin_scope(c);

    /* Pre-declare all locals with void placeholders */
    val_t b = bindings;
    while (vis_pair(b)) {
        val_t name = vcar(vcar(b));
        add_local(c, name);
        mark_initialised(c);
        emit(c, OP_VOID, line);   /* placeholder on stack */
        b = vcdr(b);
    }

    /* Now compile and store each init (they can reference each other) */
    b = bindings;
    int slot = c->local_count - c->upval_count;
    /* find the starting slot */
    val_t bcount = bindings; int nb = 0;
    while (vis_pair(bcount)) { nb++; bcount = vcdr(bcount); }
    int base_slot = c->local_count - nb;

    b = bindings; int i = 0;
    while (vis_pair(b)) {
        val_t init = vcar(vcdr(vcar(b)));
        compile(c, init, false, line);
        emit_ab(c, OP_STORE_LOCAL, (uint8_t)(base_slot + i), line);
        b = vcdr(b); i++;
    }
    (void)slot;

    compile_seq(c, body, tail, line);
    end_scope(c, line);
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
            int skip = emit_jump(c, OP_JUMP_FALSE, line);
            compile(c, proc, false, line);
            emit(c, OP_SWAP, line);   /* proc result→ (proc test) */
            emit_ab(c, tail ? OP_TAIL_CALL : OP_CALL, 1, line);
            if (!last) end_patches[np++] = emit_jump(c, OP_JUMP, line);
            patch_jump(c, skip);
            continue;
        }

        int skip = emit_jump(c, OP_JUMP_FALSE, line);
        emit(c, OP_POP, line);   /* discard test value */
        compile_seq(c, exprs, tail && last, line);
        if (!last) end_patches[np++] = emit_jump(c, OP_JUMP, line);
        patch_jump(c, skip);
    }
    emit(c, OP_VOID, line);   /* no clause matched */

cond_done:
    for (int i = 0; i < np; i++) patch_jump(c, end_patches[i]);
}

static void compile_when(Compiler *c, val_t args, bool tail, int line) {
    val_t test = vcar(args);
    val_t body = vcdr(args);
    compile(c, test, false, line);
    int skip = emit_jump(c, OP_JUMP_FALSE, line);
    emit(c, OP_POP, line);
    compile_seq(c, body, tail, line);
    int end = emit_jump(c, OP_JUMP, line);
    patch_jump(c, skip);
    emit(c, OP_POP,  line);
    emit(c, OP_VOID, line);
    patch_jump(c, end);
}

static void compile_unless(Compiler *c, val_t args, bool tail, int line) {
    val_t test = vcar(args);
    val_t body = vcdr(args);
    compile(c, test, false, line);
    int skip = emit_jump(c, OP_JUMP_TRUE, line);
    emit(c, OP_POP, line);
    compile_seq(c, body, tail, line);
    int end = emit_jump(c, OP_JUMP, line);
    patch_jump(c, skip);
    emit(c, OP_POP,  line);
    emit(c, OP_VOID, line);
    patch_jump(c, end);
}

static void compile_do(Compiler *c, val_t args, bool tail, int line) {
    /* (do ((var init step) ...) (test expr...) body...) */
    val_t var_specs = vcar(args);
    val_t term      = vcar(vcdr(args));
    val_t body      = vcdr(vcdr(args));
    val_t test_expr = vcar(term);
    val_t result    = vcdr(term);

    begin_scope(c);

    /* Evaluate and bind init values */
    val_t vs = var_specs;
    while (vis_pair(vs)) {
        val_t spec = vcar(vs);
        val_t init = vcar(vcdr(spec));
        compile(c, init, false, line);
        add_local(c, vcar(spec));
        mark_initialised(c);
        vs = vcdr(vs);
    }

    /* Loop head */
    int loop_start = chunk_pos(c->chunk);

    /* Test */
    compile(c, test_expr, false, line);
    int exit_jmp = emit_jump(c, OP_JUMP_TRUE, line);
    emit(c, OP_POP, line);

    /* Body */
    vs = body;
    while (vis_pair(vs)) {
        val_t next = vcdr(vs);
        compile(c, vcar(vs), false, line);
        if (!vis_nil(next)) emit(c, OP_POP, line);
        vs = next;
    }
    if (!vis_nil(body)) emit(c, OP_POP, line);

    /* Compute all step values first (so they don't see their own update) */
    int nv = 0;
    vs = var_specs;
    while (vis_pair(vs)) { nv++; vs = vcdr(vs); }

    vs = var_specs;
    while (vis_pair(vs)) {
        val_t spec = vcar(vs);
        val_t step = vis_pair(vcdr(vcdr(spec))) ? vcar(vcdr(vcdr(spec))) : vcar(spec);
        compile(c, step, false, line);
        vs = vcdr(vs);
    }
    /* Stack now has: ... [local_0 .. local_nv-1] [step_0 .. step_nv-1]
       Store in reverse order: top-of-stack (step_nv-1) → local[base+nv-1] etc. */
    int base = c->local_count - nv;
    for (int i = nv - 1; i >= 0; i--)
        emit_ab(c, OP_STORE_LOCAL, (uint8_t)(base + i), line);

    /* Jump back to loop head */
    int back = emit_jump(c, OP_JUMP, line);
    chunk_patch16(c->chunk, back, (uint16_t)loop_start);

    /* Exit: test was true, test value on stack */
    patch_jump(c, exit_jmp);
    emit(c, OP_POP, line);   /* discard test */

    if (vis_nil(result))
        emit(c, OP_VOID, line);
    else
        compile_seq(c, result, tail, line);

    end_scope(c, line);
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

    /* cond */
    if (head == S_COND) { compile_cond(c, args, tail, line); return; }

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
