/*
 * compiler.c — AST-to-bytecode compiler for Curry: foundation + public API.
 *
 * This is the surviving foundation half of the old single-file compiler.c
 * (split for navigability -- pure code motion, no behavior change): Compiler
 * lifecycle (init_compiler/end_compiler), the low-level emit/scope/local/
 * upvalue helpers every compile path builds on, and the public API entry
 * points (compiler_compile, compiler_compile_script, compiler_set_source_name,
 * compiler_set_target_env/compiler_clear_target_env, compiler_ir_lower_for_jit).
 * The rest of what used to live here now lives in:
 *
 *   compiler_classic.c   -- the classic (pre-IR) compile_ family, compile(),
 *                           and classify_head.
 *   ir_lower.c            -- Tier 2.1 IR lowering (ir_lower/ir_lower_*) and
 *                           Tier 2.2 optimization (ir_optimize).
 *   ir_emit.c             -- Tier 2.1 IR bytecode emission (ir_emit) plus the
 *                           Tier 2.3 local inliner (ir_emit_inline_call).
 *   compiler_ir_checks.c  -- the differential self-check test infrastructure
 *                           (compiler_ir_self_check/compiler_ir_optimize_check/
 *                           compiler_ir_inline_fired_check).
 *
 * Symbols shared across these five files are declared in
 * compiler_internal.h (the real `Compiler` struct body lives there too),
 * the same pattern src/runtime_internal.h uses for eval.c/runtime.c.
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
 *   lang_translate() before dispatch, so Akkadian source compiles identically.
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
#include "compiler_internal.h"
#include "chunk.h"
#include "ir.h"
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
#include "lang_registry.h"
#include "profiling.h"
#include "reader.h"
#include "record_type.h"
#include "syntax_rules.h"
#include "sx_algebra.h"
#include "sx_pattern.h"
#include "curry_features.h"
#include "set.h"

/* ── Compiler lifecycle ──────────────────────────────────────────────── */

static CURRY_THREAD_LOCAL const char *g_compile_source_name = NULL;
static CURRY_THREAD_LOCAL val_t       g_compile_target_env   = V_VOID;
/* Set by compile_let's named-let branch right before compiling the loop
 * lambda's own body, consumed-and-cleared by init_compiler below the same
 * way g_compile_target_env is -- see Compiler::self_tail_name's own
 * comment. Mirrors the established thread-local hand-off pattern used
 * throughout this file rather than adding a parameter to compile_lambda
 * (which has many other callers that don't care about this at all).
 * Declared extern in compiler_internal.h: written by compile_let
 * (compiler_classic.c) right before compiling a named-let's loop lambda,
 * and read/cleared here by init_compiler; also read by ir_emit's own
 * IR_LET/IR_LETREC named-let handling (ir_emit.c), which sets it the same
 * way compile_let does for its own child Compiler. */
CURRY_THREAD_LOCAL val_t g_compile_self_tail_name    = V_FALSE;
CURRY_THREAD_LOCAL bool  g_compile_self_tail_mutated = false;

/* Tier 2.3: guards against mutual/indirect recursion inlining itself
 * arbitrarily deep. body_contains_symbol (ir_lower.c) only rejects DIRECT
 * self-reference (f's own body mentioning f) -- two or more candidates
 * calling each other in a cycle (A's body calls B, B's body calls A)
 * would otherwise pass every per-candidate eligibility gate and only be
 * stopped once the shared inline-effort budget (ir_arena_take_inline_
 * budget) runs out. That bounds total compile cost, but isn't a clean
 * "don't do this" signal -- it wastes budget on doomed splice attempts
 * that could otherwise go to legitimately-inlinable call sites elsewhere
 * in the same top-level form. Tracks the raw body val_t IDENTITY (eq?
 * comparison -- the exact same cons cells are reused every time a given
 * candidate is spliced, since KnownLambda.body is never copied) of every
 * inline currently in progress on the C call stack; ir_emit_inline_call
 * (ir_emit.c) pushes/pops around its own body-processing. Declared extern
 * in compiler_internal.h (MAX_INLINE_DEPTH lives there too, since the
 * array's declared size must match between this definition and ir_emit.c's
 * own bounds check). */
CURRY_THREAD_LOCAL val_t g_inlining_bodies[MAX_INLINE_DEPTH];
CURRY_THREAD_LOCAL int   g_inlining_depth = 0;

bool currently_inlining(val_t body) {
    for (int i = 0; i < g_inlining_depth; i++)
        if (g_inlining_bodies[i] == body) return true;
    return false;
}

void compiler_set_source_name(const char *name) { g_compile_source_name = name; }

void compiler_set_target_env(val_t env) { g_compile_target_env = env; }
void compiler_clear_target_env(void)    { g_compile_target_env = V_VOID; }

void init_compiler(Compiler *c, Compiler *enc, const char *name) {
    c->enclosing   = enc;
    c->chunk       = chunk_new();
    c->name        = name;
    c->local_count = 0;
    c->scope_depth = 0;
    c->upval_count = 0;
    c->syntax_local_count = 0;
    c->chunk->name = name;
    c->chunk->source_name = g_compile_source_name;
    c->chunk->target_env  = g_compile_target_env;
    c->self_tail_name     = g_compile_self_tail_name;
    c->self_tail_mutated  = g_compile_self_tail_mutated;
    g_compile_self_tail_name    = V_FALSE;
    g_compile_self_tail_mutated = false;
    /* Eagerly allocated for every root Compiler (enc == NULL): compile()
     * now genuinely routes through ir_lower/ir_emit for most forms (see
     * IR_OR_CLASSIC), so by the time any caller's compile() call returns
     * this is essentially always needed anyway -- unlike the landing
     * that introduced this field, where compile() never touched
     * ir_lower/ir_emit at all and eager allocation here was pure waste
     * (and, per an independent security review at the time, a leak on
     * every ordinary compile-time error, since nothing freed it on the
     * exception path). That leak risk is now handled at the two real
     * root-Compiler call sites instead of being avoided by not
     * allocating: compiler_compile/compiler_compile_script wrap their
     * whole compile() call in SCM_PROTECT and free c.ir_arena
     * unconditionally afterward (success or raise) -- see their own
     * comments. A child Compiler (enc != NULL) still just inherits its
     * parent's arena, same as always. */
    c->ir_arena = enc ? enc->ir_arena : ir_arena_new();

    /* Tier 2.3: g_inlining_depth (currently_inlining's own guard stack)
     * has no SCM_PROTECT around ir_emit_inline_call's push/pop, so a
     * Scheme condition raised (longjmp) while compiling INSIDE an
     * inlined body's splice -- e.g. a malformed internal define --
     * skips the matching pop, leaking depth for the rest of the
     * thread's lifetime (after MAX_INLINE_DEPTH such leaks, silently
     * disabling cycle detection for every later compile on this thread,
     * caught by independent code review). Rather than wrap every
     * ir_emit_inline_call invocation in its own SCM_PROTECT (real cost,
     * for a guard that's only ever meaningful within ONE top-level
     * compile's own recursive descent anyway), reset it here for every
     * ROOT Compiler only (enc == NULL, never a child -- resetting
     * unconditionally would wrongly wipe an in-progress inline's own
     * tracking the moment any ordinary nested lambda inside that inlined
     * body gets its own child Compiler). A raise abandons its entire
     * enclosing top-level compile (see compiler_compile's own
     * SCM_PROTECT), so it's always safe -- and, after a leak, necessary
     * -- for the NEXT one to start clean. */
    if (!enc) g_inlining_depth = 0;
}

Chunk *end_compiler(Compiler *c) {
    /* Return whatever the body left on the stack (compile_seq always leaves
       one value; for tail-called BcClosures the frame is reused so OP_RETURN
       here is dead code, but it still needs to be well-formed). */
    chunk_emit(c->chunk, OP_RETURN, 0);
    c->chunk->upval_count = c->upval_count;
    chunk_local_debug_finalize(c->chunk);
    return c->chunk;
}

/* ── Emit helpers ────────────────────────────────────────────────────── */

void emit(Compiler *c, uint8_t op, int line) {
    chunk_emit(c->chunk, op, line);
}

void emit_const(Compiler *c, val_t v, int line) {
    int idx = chunk_add_const(c->chunk, v);
    if (idx < 256) {
        emit(c, OP_CONST, line);
        chunk_emit(c->chunk, (uint8_t)idx, line);
    } else {
        emit(c, OP_CONST_W, line);
        chunk_emit16(c->chunk, (uint16_t)idx, line);
    }
}

void emit_ab(Compiler *c, uint8_t op, uint8_t a, int line) {
    chunk_emit(c->chunk, op, line);
    chunk_emit(c->chunk, a,  line);
}

/* Opcode + two single-byte operands (e.g. OP_CALL_GLOBAL's const-pool
 * index and argc) */
void emit_abc(Compiler *c, uint8_t op, uint8_t a, uint8_t b, int line) {
    chunk_emit(c->chunk, op, line);
    chunk_emit(c->chunk, a,  line);
    chunk_emit(c->chunk, b,  line);
}

/* Emit a jump; returns offset of the 16-bit placeholder to patch later */
int emit_jump(Compiler *c, uint8_t op, int line) {
    emit(c, op, line);
    int off = chunk_pos(c->chunk);
    chunk_emit16(c->chunk, 0xFFFF, line);  /* placeholder */
    return off;
}

void patch_jump(Compiler *c, int placeholder) {
    int target = chunk_pos(c->chunk);
    if (target > 0xFFFF)
        fprintf(stderr, "compiler: jump target out of range\n");
    chunk_patch16(c->chunk, placeholder, (uint16_t)target);
}

/* ── Scope / locals ──────────────────────────────────────────────────── */

void begin_scope(Compiler *c) { c->scope_depth++; }

void end_scope(Compiler *c, int line) {
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
int add_syntax_local(Compiler *c, val_t name, val_t transformer) {
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
 * macro matches — the caller should then fall back to a GLOBAL_ENV lookup.
 *
 * On a match, also marks Chunk::uses_local_macro (chunk.h) on every
 * compiler from `c` up to and including the one whose own syntax_locals
 * table actually owns the match — not just `c->chunk` itself. This
 * matters because the LLVM JIT tier (src/llvm/codegen.cpp's emit_lambda)
 * compiles a nested lambda literal INLINE into its enclosing function's
 * native code, rather than through that nested lambda's own chunk's
 * independent maybe_jit_bcc gate — so if only c->chunk were marked, an
 * outer closure embedding this exact nested lambda body would still get
 * JIT-promoted and inline-compile straight through the unmarked outer
 * chunk, miscompiling the very same local-macro call the inner chunk's
 * own (correctly set, but never independently consulted in that case)
 * flag was meant to guard against. Marking every level from `c` to the
 * owning `cc` covers every chunk whose own JIT promotion could pull this
 * macro use in, however many lambdas deep the reference is (issue #114's
 * fix was originally only chunk-local and missed this). Every caller of
 * resolve_syntax_local gets this for free rather than needing its own
 * copy of this logic, including callers (expr_mentions_set_target,
 * ir_lower.c's head_is_macro) that only use the boolean result for
 * unrelated analysis -- harmless, since a true result there means the
 * same head position will also be classify_head'd for real compilation
 * of the very same body. */
bool resolve_syntax_local(Compiler *c, val_t name, val_t *out_transformer) {
    for (Compiler *cc = c; cc; cc = cc->enclosing) {
        for (int i = cc->syntax_local_count - 1; i >= 0; i--)
            if (cc->syntax_locals[i].name == name) {
                *out_transformer = cc->syntax_locals[i].transformer;
                for (Compiler *m = c; m; m = m->enclosing) {
                    m->chunk->uses_local_macro = true;
                    if (m == cc) break;
                }
                return true;
            }
    }
    return false;
}

int add_local(Compiler *c, val_t name) {
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
    /* Tier 2.3: this physical slot may have most recently held some other,
     * unrelated local from an already-exited scope with a live
     * known-lambda registration (known[] is keyed by physical slot index,
     * not by variable identity) -- clear it so `name` doesn't inherit
     * that stale entry. */
    c->known[c->local_count].valid = false;
    return c->local_count++;
}

/* Tier 2.3 local inliner: reserves a compile-time-only placeholder local
 * slot -- no bytecode emitted, no name, never resolvable by
 * resolve_local -- for a value that is CURRENTLY sitting on the real VM
 * stack but isn't itself a named local: specifically, an already-emitted
 * sibling argument of a call/self-tail-call/tail-call whose OWN
 * evaluation isn't finished yet (more arguments remain to be compiled
 * before the single call/self-tail-call instruction consumes all of
 * them at once).
 *
 * Why this is needed: add_local (and therefore ir_emit_inline_call,
 * which uses it to bind an inlined call's params) assumes `c->local_count`
 * always equals the TRUE current depth of the real VM stack relative to
 * this frame's base -- i.e. that nothing is sitting on the stack above
 * `c->locals[c->local_count-1]` except what add_local itself put there.
 * That invariant holds for every call site that existed before Tier 2.3:
 * a lambda body's own params/internal-defines are the ONLY things ever
 * on the stack when add_local runs, because any nested lambda/let/letrec
 * gets its OWN brand-new child Compiler with local_count starting fresh
 * at 0, and CALLING CONVENTION guarantees that child's frame->slots base
 * is computed fresh (vm->sp - argc) at the exact moment of the real
 * OP_CALL that creates it -- so nothing else can possibly be pending.
 *
 * The local inliner breaks that guarantee: it's the first thing that can
 * call add_local against the SAME, already-in-progress Compiler `c`
 * while evaluating one of several sibling arguments to an OUTER call
 * still being accumulated on the stack (e.g. `(+ (square 3) (square
 * 4))`: while inlining the second `(square 4))`, the first `(square 3))`'s
 * already-computed result is sitting on the stack, untracked by
 * `c->local_count`, and inlining's own add_local would otherwise
 * silently claim that SAME physical stack slot for the second call's own
 * `x` -- confirmed as a real, reproduced miscompilation (`(+ (square 3)
 * (square 4))` returned 90 instead of 25) before this fix.
 *
 * Bracketing every call-argument-evaluation loop with
 * reserve_pending_slot (after each argument is pushed) and
 * release_pending_slots (once, right before the real call instruction
 * that actually consumes them) keeps `c->local_count` accurate for the
 * whole time a nested inline call might run, without emitting any extra
 * bytecode -- the real call instruction consumes the runtime values
 * regardless of how compile-time bookkeeping numbered them. */
/* Capacity note (flagged by independent code review): this is now called
 * for every argument of every call, self-tail-call, and tail/ordinary
 * call -- not just at an inlined call site -- so it widens exposure to
 * MAX_LOCALS(256) overflow beyond what add_local alone already risked
 * (which has the IDENTICAL "silently return an aliased, already-in-use
 * slot on overflow" behavior, unchanged by this file, and whose callers
 * likewise don't check for it). None of this function's own call sites
 * check its -1 return either. Reaching this in practice requires an
 * extreme, deliberately-pathological program (on the order of 256
 * simultaneously pending call arguments to a single call site) -- not
 * something realistic hand-written or generated Scheme code produces --
 * and the actual array accesses here and in add_local are always
 * guarded before the write (see both functions' own bounds checks), so
 * this cannot produce an out-of-bounds access; the failure mode, if ever
 * reached, is a wrong compile of that one pathological call site, with
 * add_local's existing "too many locals" diagnostic on stderr. Accepted
 * as a pre-existing class of limitation this landing makes marginally
 * easier to reach, not a new one -- a proper fix (making Compiler-level
 * local-slot exhaustion a clean, callable-checked failure everywhere,
 * not just here) is a broader change than this landing's own scope. */
int reserve_pending_slot(Compiler *c) {
    if (c->local_count == MAX_LOCALS) return -1;
    int slot = c->local_count++;
    c->locals[slot].name     = V_FALSE;   /* never matches a real name */
    c->locals[slot].captured = false;
    /* Must match c->scope_depth (the CURRENT depth, not deeper): this
     * physical array slot may retain a stale, deeper .depth value from
     * whatever local last occupied it (e.g. an earlier sibling argument's
     * own inlined call's param, already popped). end_scope only pops
     * locals whose depth is STRICTLY GREATER than the scope it's
     * exiting -- setting depth to the current level makes this
     * placeholder look like it belongs to an OUTER scope, so a nested
     * inline call's own end_scope correctly leaves it alone instead of
     * wrongly popping/OP_SLIDE-ing it away along with its own real
     * locals (confirmed as a second real bug, caught immediately after
     * fixing the first: manifested as a runtime type error instead of a
     * silently wrong numeric result). */
    c->locals[slot].depth    = c->scope_depth;
    c->known[slot].valid     = false;
    return slot;
}

void release_pending_slots(Compiler *c, int saved_local_count) {
    c->local_count = saved_local_count;
}

/* Mark the most-recently added local as initialised at current depth */
void mark_initialised(Compiler *c) {
    if (c->local_count > 0)
        c->locals[c->local_count - 1].depth = c->scope_depth;
}

/* ── Upvalue resolution ───────────────────────────────────────────────── */

int add_upvalue(Compiler *c, int index, bool is_local, val_t name) {
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

int resolve_local(Compiler *c, val_t name) {
    for (int i = c->local_count - 1; i >= 0; i--)
        if (c->locals[i].name == name) return i;
    return -1;
}

/* Tier 2.3 local inliner: unconditionally invalidates any known-lambda
 * registration for `name`, walking the ->enclosing chain -- a set!
 * reaching a local via an upvalue (mutated from a NESTED closure, not the
 * frame that originally bound it) must still poison the registration, or
 * a stale, now-wrong known[] entry stays live after a real mutation. No
 * re-marking after poisoning: deliberately simple and conservative. */
void poison_known(Compiler *c, val_t name) {
    for (Compiler *cc = c; cc; cc = cc->enclosing) {
        int slot = resolve_local(cc, name);
        if (slot >= 0) { cc->known[slot].valid = false; return; }
    }
}

int resolve_upvalue(Compiler *c, val_t name) {
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

/* Read-only version of resolve_upvalue's reachability check: does `name`
 * resolve as a local anywhere up the ->enclosing chain, without any of
 * resolve_upvalue's own side effects (marking the enclosing local
 * captured, registering a real upvalue-capture descriptor in
 * c->upvals[]/chunk->upval_count). For a caller that only needs to know
 * WHETHER a name is reachable, not to actually load it -- compile_defined_p
 * is the motivating case (see its own comment): calling the mutating
 * resolve_upvalue there to answer a boolean query gave every closure
 * containing a (defined? outer-var) check a real, otherwise-unused
 * upvalue capture, needlessly enlarging it (independent code review). */
bool is_upvalue_reachable(Compiler *c, val_t name) {
    for (Compiler *cc = c->enclosing; cc; cc = cc->enclosing)
        if (resolve_local(cc, name) >= 0) return true;
    return false;
}

/* ── Variable load / store ───────────────────────────────────────────── */

void emit_load(Compiler *c, val_t name, int line) {
    int local = resolve_local(c, name);
    if (local >= 0) { emit_ab(c, OP_LOAD_LOCAL, (uint8_t)local, line); return; }
    int up = resolve_upvalue(c, name);
    if (up >= 0)    { emit_ab(c, OP_LOAD_UP,    (uint8_t)up,    line); return; }
    emit_ab(c, OP_LOAD_GLOBAL, (uint8_t)chunk_add_const(c->chunk, name), line);
}

void emit_store(Compiler *c, val_t name, int line) {
    int local = resolve_local(c, name);
    if (local >= 0) { emit_ab(c, OP_STORE_LOCAL, (uint8_t)local, line); return; }
    int up = resolve_upvalue(c, name);
    if (up >= 0)    { emit_ab(c, OP_STORE_UP,    (uint8_t)up,    line); return; }
    emit_ab(c, OP_STORE_GLOBAL, (uint8_t)chunk_add_const(c->chunk, name), line);
}

/* ── Parameter list helpers ───────────────────────────────────────────── */

/* Returns arity; -1 if rest param (improper list).
   Declares each param as a local in c. */
int compile_params(Compiler *c, val_t params) {
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

/* Scan a lambda body for internal defines and pre-declare them as locals
 * (letrec* semantics), enforcing R7RS's "definitions must precede all
 * expressions in the body". Factored out of compile_lambda so ir_emit's
 * IR_LAMBDA case (compiler.c, Tier 2.1 IR section) can run the identical
 * prescan against its own freshly-created child Compiler -- this step is
 * inherently imperative (add_local + immediate OP_VOID emission in
 * lockstep, so a later real store lands on the exact stack slot the
 * reservation carved out) and has no natural IR-tree representation, so
 * both callers just run it directly rather than duplicating it. */
void lambda_prescan(Compiler *c, val_t body, int line) {
    val_t bscan = body;
    bool body_has_expr = false;
    while (vis_pair(bscan)) {
        val_t form = vcar(bscan);
        bool is_def = vis_pair(form) && vis_symbol(vcar(form)) &&
                      (lang_translate(vcar(form)) == S_DEFINE ||
                       lang_translate(vcar(form)) == S_DEFINE_SYNTAX ||
                       lang_translate(vcar(form)) == S_DEFINE_VALUES ||
                       lang_translate(vcar(form)) == S_DEFINE_RECORD_TYPE ||
                       lang_translate(vcar(form)) == S_DEFINE_RULE ||
                       lang_translate(vcar(form)) == S_DEFINE_RULESET ||
                       lang_translate(vcar(form)) == S_DEFINE_ALGEBRA);
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
            lang_translate(vcar(form)) == S_SYMBOLIC) {
            for (val_t p = vcdr(form); vis_pair(p); p = vcdr(p)) {
                if (!vis_symbol(vcar(p))) continue;
                add_local(c, vcar(p));
                c->locals[c->local_count - 1].depth = -1; /* uninitialised */
                emit(c, OP_VOID, line); /* reserve stack slot */
            }
        }

        if (is_def) {
            if (body_has_expr)
                scm_raise(V_FALSE, "internal definition after expression in body (R7RS violation)");
            if (lang_translate(vcar(form)) == S_DEFINE_RECORD_TYPE) {
                /* define-record-type doesn't bind its own type-name symbol
                 * (matching record_type_build_spec/eval.c) — it binds a
                 * constructor, a predicate, and one accessor/mutator pair
                 * per field.  Reserve locals for all of those so the record
                 * type is usable as an internal definition, not just at
                 * top level. */
                RecordTypeSpec spec;
                record_type_build_spec(vcdr(form), V_FALSE, &spec);
                for (int i = 0; i < spec.count; i++) {
                    add_local(c, spec.bindings[i].name);
                    c->locals[c->local_count - 1].depth = -1; /* uninitialised */
                    emit(c, OP_VOID, line); /* reserve stack slot */
                }
            } else if (lang_translate(vcar(form)) == S_DEFINE_SYNTAX) {
                /* Macro registration is pure compile-time bookkeeping (see
                 * add_syntax_local/resolve_syntax_local) — no VM stack slot
                 * to reserve.  compile_define_syntax registers it into
                 * c.syntax_locals sequentially as compile_seq reaches this
                 * form for real, so later forms in this same body see it. */
            } else if (lang_translate(vcar(form)) == S_DEFINE_RULE ||
                       lang_translate(vcar(form)) == S_DEFINE_RULESET) {
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
            } else if (lang_translate(vcar(form)) == S_DEFINE_ALGEBRA) {
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
                    add_local(c, sym);
                    c->locals[c->local_count - 1].depth = -1; /* uninitialised */
                    emit(c, OP_VOID, line); /* reserve stack slot */
                }
            } else if (lang_translate(vcar(form)) == S_DEFINE_VALUES) {
                /* (define-values (var...) expr) binds N names, not one --
                 * falling into the generic (define name ...) case below
                 * would misread (var...) itself as a single defname (via
                 * vis_pair(defname) → add_local(vcar(defname))), reserving
                 * exactly one bogus, wrongly-named slot instead of one per
                 * formal. Reserve one uninitialised local per name, same
                 * pattern as every other multi-binding case above
                 * (define-record-type). Only reserves for a well-formed
                 * proper list of symbols -- a bare-symbol or dotted/rest
                 * `vars` reserves nothing here and will raise a clear
                 * compile-time error when compile_define_values itself
                 * runs for real (see that function's own comment); no
                 * need to duplicate that validation in the prescan. */
                val_t vars = vis_pair(vcdr(form)) ? vcar(vcdr(form)) : V_NIL;
                for (val_t p = vars; vis_pair(p) && vis_symbol(vcar(p)); p = vcdr(p)) {
                    add_local(c, vcar(p));
                    c->locals[c->local_count - 1].depth = -1; /* uninitialised */
                    emit(c, OP_VOID, line); /* reserve stack slot */
                }
            } else {
                val_t defname = vcar(vcdr(form));
                if (vis_symbol(defname)) {
                    /* simple (define x ...) */
                    add_local(c, defname);
                    c->locals[c->local_count - 1].depth = -1; /* uninitialised */
                    emit(c, OP_VOID, line); /* reserve stack slot */
                } else if (vis_pair(defname)) {
                    /* (define (f ...) ...) sugar */
                    add_local(c, vcar(defname));
                    c->locals[c->local_count - 1].depth = -1;
                    emit(c, OP_VOID, line); /* reserve stack slot */
                }
            }
        } else {
            body_has_expr = true;
        }
        bscan = vcdr(bscan);
    }
}

/* Raises a catchable EC_WRONG_NUMBER_OF_ARGUMENTS condition if `args` (a
 * special form's own operand list, e.g. the `(test then else)` after
 * `if`) has fewer than `min` elements, instead of letting the caller's
 * own unchecked vcar/vcdr walk off the end of a too-short (or non-pair,
 * e.g. `(if . 5)`) list. Found and fixed as a real, confirmed, WIDESPREAD
 * bug: every special-form compile_* / ir_lower_* function in this file
 * originally read its own operands with bare vcar/vcdr and no arity
 * check at all, so a malformed source form -- `(if)`, `(if 1)`, `(let)`,
 * `(let*)`, `(letrec)`, `(lambda)`, `(define)`, `(set!)`, `(case)`,
 * `(when)`, `(unless)`, `(do)`, `(guard)`, `(let-values)`,
 * `(parameterize)`, `(define-record-type)` -- SIGSEGV'd the whole
 * process instead of raising a catchable condition (confirmed present on
 * `main` too, not introduced by any of tonight's other work; confirmed
 * via direct testing of all of the above). Reuses
 * EC_WRONG_NUMBER_OF_ARGUMENTS rather than minting a new stable error
 * code (docs/reference/error-codes.md): this genuinely is an arity
 * problem, just for a special form's own operand list rather than a
 * procedure call's argument list. Every call site below passes a literal
 * string naming the exact form for the error message, matching every
 * other hand-written error message already in this file. */
void require_min_args(val_t args, int min, const char *form_name) {
    val_t p = args;
    for (int i = 0; i < min; i++) {
        if (!vis_pair(p))
            scm_raise_code(EC_WRONG_NUMBER_OF_ARGUMENTS,
                            "%s: ill-formed special form", form_name);
        p = vcdr(p);
    }
}

/* Tier 2.6 step 1 -- see compiler.h's own comment on this function for
 * the full contract (what "the IR" does and does NOT mean here: lazily
 * lowered, nested lambda bodies stay raw S-expression, variable
 * references stay unresolved symbols -- this is not a standalone tree a
 * from-scratch consumer can just walk). This function's own job is
 * narrow: produce that tree safely, from a context (an eventual JIT
 * trigger, called from C++) that must never let a raised Scheme
 * condition longjmp past it -- ir_lower/ir_optimize can scm_raise on
 * malformed input (unbound variable, bad special-form syntax, ...) the
 * exact same way compiler_ir_self_check's own SCM_PROTECT usage already
 * guards against, but crossing into C++ frames via a raw longjmp (no
 * destructor unwinding) is a strictly worse hazard than the pure-C
 * gc_inhibit_count leak that comment describes -- so a raise here is
 * caught and converted to a plain NULL return, mirroring
 * curry_llvm_jit_compile's own existing "silent failure, fall back to
 * bytecode" philosophy (curry_llvm.cpp) rather than ever propagating an
 * exception across the C/C++ boundary. */
IRNode *compiler_ir_lower_for_jit(val_t expr, IRArena **out_arena) {
    gc_inhibit_minor();

    Compiler c;
    init_compiler(&c, NULL, "<jit>");

    int   line   = g_reader_last_line;
    IRNode *result = NULL;
    bool  raised = false;

    ExnHandler h;
    SCM_PROTECT(h, {
        IRNode *ir = ir_lower(&c, expr, false, line);
        result = ir_optimize(ir);
    }, {
        raised = true;
    });

    gc_resume_minor();

    if (raised) {
        ir_arena_free(c.ir_arena);
        *out_arena = NULL;
        return NULL;
    }
    *out_arena = c.ir_arena;
    return result;
}

/* Tier 2.6 (docs/thoughts/tier2-6-llvm-ir-retargeting-plan-2026-08-25.md,
 * "Phase A"): compiler_ir_lower_for_jit above lowers exactly one top-level
 * expression per call, against a throwaway Compiler that never survives
 * past that single call -- fine for a JIT-eligible chunk's own top-level
 * src_lambda, but not enough for a consumer (an LLVM codegen dispatcher)
 * that needs to lower a WHOLE function body, one form at a time,
 * interleaved with its own per-form consumption -- the same "lower this
 * form, consume it immediately, THEN lower the next one" contract
 * ir_emit's own IR_SEQ/IR_LAMBDA cases already rely on (see ir.h's own
 * comments on IRNode::as.seq and IRNode::as.lambda for why: an internal
 * define-syntax registered by form i must be visible when form i+1 is
 * classified, which only holds if lowering happens interleaved with
 * consumption, never as one eager batch upfront).
 *
 * This is genuinely new territory: every other Compiler in this file
 * lives on one C function's own stack frame for the duration of a single,
 * synchronous compile call. A session Compiler instead needs to survive
 * across multiple, separate calls INTO this file from external (C++)
 * code, so it must be heap-allocated -- gc_alloc_pinned, not GC_MALLOC or
 * a plain malloc, since Compiler holds live val_t references (chunk,
 * syntax_locals[].name/transformer, self_tail_name) that need to stay
 * reachable to Boehm's collector for as long as this session lives, the
 * same way every other long-lived, GC-participating struct in this
 * codebase is allocated (see e.g. chunk_new's own gc_alloc_pinned call).
 * A plain stack Compiler would only be traced by Boehm's conservative
 * stack scan for the lifetime of ONE C stack frame -- wrong here, since
 * the session outlives the call that created it. */
Compiler *compiler_ir_session_new_root(const char *name, IRArena **out_arena) {
    Compiler *c = CURRY_NEW_PINNED(Compiler);
    init_compiler(c, NULL, name);
    *out_arena = c->ir_arena;
    return c;
}

/* Child session for a nested lambda body -- mirrors ir_emit's own
 * IR_LAMBDA case creating a child Compiler for the body it's about to
 * walk. Shares the parent's arena (init_compiler's own enc != NULL
 * branch); needs no separate `out_arena` parameter since it's always the
 * same arena the root session already returned. `enclosing` linkage is
 * the one thing that actually matters for correctness here even though
 * this session never populates real locals[]/upvals[] (nothing during
 * LOWERING reads those -- see this function's own header note below) --
 * resolve_syntax_local walks ->enclosing to find a macro registered in
 * an outer scope, so a nested lambda's own body must chain to its
 * parent's session Compiler to see macros defined above it. */
Compiler *compiler_ir_session_new_child(Compiler *parent, const char *name) {
    Compiler *c = CURRY_NEW_PINNED(Compiler);
    init_compiler(c, parent, name);
    return c;
}

/* Lowers ONE form against this session's Compiler, immediately -- the
 * interleaved half of the "lower this form, consume it immediately" loop
 * a caller (e.g. an LLVM codegen dispatcher walking IR_SEQ/IR_LAMBDA
 * bodies) needs to drive itself. `tail` marks whether this form is in
 * tail position (the last form of a body); `line` is the caller's own
 * source-line tracking for this form (this function does not read
 * g_reader_last_line the way compiler_ir_lower_for_jit does, since a
 * session's forms don't necessarily come from an active read -- an
 * LLVM-JIT-triggered recompile walks an ALREADY-read val_t body list, see
 * Chunk's own per-statement line tracking for the shape a caller should
 * mirror).
 *
 * Verified during this landing that lowering itself never actually reads
 * this Compiler's locals[]/known[]/upvals[]/local_count/chunk fields --
 * only ->enclosing (macro visibility, resolve_syntax_local) and
 * ->syntax_locals[] (macro registration/lookup) are ever touched by
 * ir_lower's own dispatch (classify_head checks resolve_syntax_local,
 * never resolve_local/resolve_upvalue -- variable/call-site resolution
 * is entirely an ir_emit-time concern, deferred by design, see ir.h's own
 * comments on IR_VAR_REF/IR_CALL). This session API deliberately does
 * NOT populate real locals/upvals for that reason: an LLVM consumer's own
 * resolution (its own scope-tracking, entirely separate from this
 * Compiler) is what actually resolves an IR_VAR_REF/IR_CALL once it
 * consumes the node this function returns, exactly mirroring how
 * ir_emit's own resolve_local/resolve_upvalue calls are what resolve
 * those nodes for the VM bytecode backend -- two independent consumers,
 * two independent resolutions, one shared lowering pass.
 *
 * Returns NULL if lowering raises a catchable Scheme condition (malformed
 * special-form syntax, etc.) -- caught here via SCM_PROTECT so it can
 * never cross into a C++ caller as a raw longjmp (the same hazard
 * compiler_ir_lower_for_jit's own header comment describes), converted to
 * a plain NULL instead, mirroring curry_llvm_jit_compile's existing
 * "silent failure, fall back to bytecode" philosophy. On NULL, the caller
 * must abort the whole in-progress session (this function does NOT free
 * the arena itself on a raise -- earlier forms already lowered through
 * this same session may still be referenced by the caller, and a session
 * may have live children sharing the one arena, so freeing here could
 * invalidate state the caller hasn't finished inspecting yet; the caller
 * owns exactly when to call ir_arena_free on the arena
 * compiler_ir_session_new_root returned, same single-owner contract every
 * other IRArena in this codebase already has). Each call brackets its own
 * gc_inhibit_minor/gc_resume_minor pair independently, rather than
 * leaving that bracketing to the caller to hold open across the whole
 * session -- deliberately, after PR #71's call/cc fix found exactly this
 * class of bug (a paired inhibit/resume left unbalanced across multiple
 * external calls into this codebase permanently blocks minor GC on the
 * thread); a self-contained bracket per call cannot leak that way. */
IRNode *compiler_ir_session_lower_next(Compiler *c, val_t form, bool tail, int line) {
    gc_inhibit_minor();

    IRNode *result = NULL;
    bool    raised = false;

    ExnHandler h;
    SCM_PROTECT(h, {
        IRNode *ir = ir_lower(c, form, tail, line);
        result = ir_optimize(ir);
    }, {
        raised = true;
    });

    gc_resume_minor();

    return raised ? NULL : result;
}

/* ── Public API ──────────────────────────────────────────────────────── */

val_t compiler_compile(val_t expr) {
    static bool lang_ready = false;
    if (!lang_ready) { lang_registry_init(); lang_ready = true; }

    gc_inhibit_minor();
    Compiler c;
    init_compiler(&c, NULL, "<toplevel>");

    int   line   = g_reader_last_line;
    val_t result = V_VOID;
    bool  raised = false;
    val_t exn    = V_FALSE;

    /* SCM_PROTECT'd since compile() genuinely raises on malformed input
     * and, now that init_compiler allocates c.ir_arena eagerly for every
     * root Compiler (see its own comment), that's real malloc'd memory
     * that would otherwise leak on every ordinary compile-time error --
     * matches compiler_ir_self_check's own reasoning for the identical
     * pattern. */
    ExnHandler h;
    SCM_PROTECT(h, {
        compile(&c, expr, false, line);
        chunk_emit(c.chunk, OP_RETURN, line);
        c.chunk->upval_count = 0;
        result = vptr(vm_make_closure(c.chunk, 0));
    }, {
        raised = true;
        exn    = h.exn;
    });

    ir_arena_free(c.ir_arena);
    gc_resume_minor();

    if (raised) scm_raise_val(exn);
    return result;
}

val_t compiler_compile_script(val_t expr_list) {
    gc_inhibit_minor();
    Compiler c;
    init_compiler(&c, NULL, "<script>");
    c.chunk->arity = 0;

    val_t result = V_VOID;
    bool  raised = false;
    val_t exn    = V_FALSE;

    /* SCM_PROTECT'd for the same reason compiler_compile is -- see its
     * own comment. */
    ExnHandler h;
    SCM_PROTECT(h, {
        compile_seq(&c, expr_list, false, 0);
        chunk_emit(c.chunk, OP_RETURN, 0);
        c.chunk->upval_count = 0;
        result = vptr(vm_make_closure(c.chunk, 0));
    }, {
        raised = true;
        exn    = h.exn;
    });

    ir_arena_free(c.ir_arena);
    gc_resume_minor();

    if (raised) scm_raise_val(exn);
    return result;
}
