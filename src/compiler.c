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

/* ── Compiler scope structures ───────────────────────────────────────── */

#define MAX_LOCALS  256
#define MAX_UPVALS  256
#define MAX_SYNTAX_LOCALS 64

/* Tier 2.3 local inliner: size budget on a candidate lambda's raw body
 * (see ir_count_ast_nodes). Deliberately generous for a first landing --
 * this bounds worst-case code growth per inlined call site, not overall
 * compile-tree effort (see IR_ARENA_DEFAULT_INLINE_BUDGET, ir.c, for
 * that). */
#define INLINE_MAX_BODY_NODES 64

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

/* Tier 2.3 local inliner: recorded for a local slot bound by an internal
 * `(define name (lambda ...))` whose value compiled as a fully CLOSED
 * lambda (zero captured upvalues -- see g_last_lambda_upval_count's own
 * comment for why that's the soundness condition that makes it safe to
 * re-lower `params`/`body` unchanged at a different call site later).
 * `params`/`body` are raw, unprocessed val_t forms, same representation
 * IRNode::as.lambda already uses (ir.h) -- ir_emit_inline_call re-lowers
 * them fresh against the calling Compiler at each inlined call site, the
 * same way ir_lower_let already re-lowers a let's own lambda body lazily.
 * `valid` is cleared (never re-set once cleared) the moment this local is
 * `set!` anywhere reachable (see poison_known) or when its physical slot
 * index is reused by an unrelated later local (see add_local) -- kept
 * deliberately simple/conservative: no re-marking after either event. */
typedef struct {
    bool  valid;
    val_t params;
    val_t body;
    int   argc;
} KnownLambda;

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

    /* Tier 2.3 local inliner -- parallel to locals[] by physical slot
     * index (known[i] describes locals[i]). See KnownLambda's own comment
     * above. */
    KnownLambda known[MAX_LOCALS];

    UpvalDesc  upvals[MAX_UPVALS];
    int        upval_count;

    SyntaxLocal syntax_locals[MAX_SYNTAX_LOCALS];
    int         syntax_local_count;

    /* Named-let self-tail-call (OP_SELF_TAIL_CALL, see compile_call and
     * compile_let's named-let branch): self_tail_name is the loop's own
     * symbol when this Compiler is compiling exactly that loop's body
     * (V_FALSE otherwise); self_tail_mutated is set once, before body
     * compilation begins, if the raw body ever textually targets that
     * name with set! anywhere (conservative, shadowing-unaware --see
     * body_mentions_set_target) -- when true, every self-tail-call site
     * in this body falls back to the ordinary call path instead. */
    val_t self_tail_name;
    bool  self_tail_mutated;

    /* Tier 2.1 IR arena (src/ir.h) -- one per top-level compiled form,
     * shared by every nested Compiler in that form's compile tree (a
     * child Compiler inherits its enclosing's pointer rather than
     * allocating its own, see init_compiler). Created by the root
     * Compiler, freed once by whichever of compiler_compile/
     * compiler_compile_script created that root -- never freed by a
     * child. */
    IRArena *ir_arena;
} Compiler;

/* ── Forward declarations ────────────────────────────────────────────── */
static void compile(Compiler *c, val_t expr, bool tail, int line);
static void compile_seq(Compiler *c, val_t list, bool tail, int line);
/* Tier 2.1/2.2 IR pipeline (src/ir.h; defined much later in this file,
 * forward-declared here so compile() -- defined well before them -- can
 * route through them; see compile()'s own IR_OR_CLASSIC macro). */
static IRNode *ir_lower(Compiler *c, val_t expr, bool tail, int line);
static IRNode *ir_optimize(IRNode *n);
static void    ir_emit(Compiler *c, IRNode *n);
static bool is_quoted_symbol(val_t expr, val_t *out_sym);

/* ── Compiler lifecycle ──────────────────────────────────────────────── */

static _Thread_local const char *g_compile_source_name = NULL;
static _Thread_local val_t       g_compile_target_env   = V_VOID;
/* Set by compile_let's named-let branch right before compiling the loop
 * lambda's own body, consumed-and-cleared by init_compiler below the same
 * way g_compile_target_env is -- see Compiler::self_tail_name's own
 * comment. Mirrors the established thread-local hand-off pattern used
 * throughout this file rather than adding a parameter to compile_lambda
 * (which has many other callers that don't care about this at all). */
static _Thread_local val_t       g_compile_self_tail_name    = V_FALSE;
static _Thread_local bool        g_compile_self_tail_mutated = false;

/* Tier 2.3 local inliner. IR_LAMBDA's ir_emit case (below) creates a real
 * child Compiler, compiles the lambda, and tears the child down
 * (end_compiler) before returning -- so child.upval_count is gone by the
 * time a caller like IR_DEFINE's case, which just called ir_emit(c,
 * n->as.def.value), would want to inspect it. Set as literally the last
 * statement in IR_LAMBDA's case before it returns, mirroring
 * g_compile_self_tail_name/g_compile_self_tail_mutated immediately above
 * -- the same "child Compiler learned something only the parent's next
 * statement needs" hand-off shape, at the same granularity (read
 * immediately after the one ir_emit call that produced it, before any
 * other ir_emit call could overwrite it). child.upval_count == 0 means
 * the lambda captured no free variables from its defining scope -- the
 * soundness condition that makes it safe to re-lower its raw params/body
 * unchanged at a different call site later (see KnownLambda's comment):
 * with no free variables, there is nothing for re-lowering elsewhere to
 * mis-resolve, since every non-param name it references either fails to
 * resolve locally at every level (falls through to global lookup, which
 * is scope-independent) or is a global to begin with. Deliberately NOT
 * reset to some sentinel after IR_LAMBDA's case -- every caller that
 * cares reads it immediately after its own single ir_emit(..., IR_LAMBDA)
 * call, never after any other node kind, so a stale value from an
 * unrelated earlier IR_LAMBDA can never be misread as this one's. */
static _Thread_local int         g_last_lambda_upval_count   = -1;

/* Tier 2.3: guards against mutual/indirect recursion inlining itself
 * arbitrarily deep. body_contains_symbol (below) only rejects DIRECT
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
 * pushes/pops around its own body-processing. */
#define MAX_INLINE_DEPTH 64
static _Thread_local val_t g_inlining_bodies[MAX_INLINE_DEPTH];
static _Thread_local int   g_inlining_depth = 0;

static bool currently_inlining(val_t body) {
    for (int i = 0; i < g_inlining_depth; i++)
        if (g_inlining_bodies[i] == body) return true;
    return false;
}

/* Forces compile()'s own dispatch (specifically IR_OR_CLASSIC, below) to
 * always take the classic compile_X path, never ir_lower/ir_emit --
 * checked once per compile() call, so it applies to the WHOLE recursive
 * compile tree for as long as it's set, not just the top-level call.
 * That matters because compile_if/compile_let/etc. call compile()
 * itself (not some pluggable alternative) for their own subexpressions
 * -- a thread-local flag compile() checks at its own entry is what makes
 * "classic all the way down" achievable without duplicating compile()'s
 * entire dispatch switch a second time just to keep it classic-only
 * (which would also need to stay in sync with the real one forever).
 * Set/cleared by compile_classic (below), never anywhere else -- see its
 * own comment for why compiler_ir_self_check/compiler_ir_optimize_check
 * need this now that compile() itself routes through the IR. */
static _Thread_local bool        g_force_classic_compile = false;

void compiler_set_source_name(const char *name) { g_compile_source_name = name; }

void compiler_set_target_env(val_t env) { g_compile_target_env = env; }
void compiler_clear_target_env(void)    { g_compile_target_env = V_VOID; }

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

/* Opcode + two single-byte operands (e.g. OP_CALL_GLOBAL's const-pool
 * index and argc) */
static void emit_abc(Compiler *c, uint8_t op, uint8_t a, uint8_t b, int line) {
    chunk_emit(c->chunk, op, line);
    chunk_emit(c->chunk, a,  line);
    chunk_emit(c->chunk, b,  line);
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
static int reserve_pending_slot(Compiler *c) {
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

static void release_pending_slots(Compiler *c, int saved_local_count) {
    c->local_count = saved_local_count;
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

/* Tier 2.3 local inliner: unconditionally invalidates any known-lambda
 * registration for `name`, walking the ->enclosing chain -- a set!
 * reaching a local via an upvalue (mutated from a NESTED closure, not the
 * frame that originally bound it) must still poison the registration, or
 * a stale, now-wrong known[] entry stays live after a real mutation. No
 * re-marking after poisoning: deliberately simple and conservative. */
static void poison_known(Compiler *c, val_t name) {
    for (Compiler *cc = c; cc; cc = cc->enclosing) {
        int slot = resolve_local(cc, name);
        if (slot >= 0) { cc->known[slot].valid = false; return; }
    }
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
static bool is_upvalue_reachable(Compiler *c, val_t name) {
    for (Compiler *cc = c->enclosing; cc; cc = cc->enclosing)
        if (resolve_local(cc, name) >= 0) return true;
    return false;
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

/* Scan a lambda body for internal defines and pre-declare them as locals
 * (letrec* semantics), enforcing R7RS's "definitions must precede all
 * expressions in the body". Factored out of compile_lambda so ir_emit's
 * IR_LAMBDA case (compiler.c, Tier 2.1 IR section) can run the identical
 * prescan against its own freshly-created child Compiler -- this step is
 * inherently imperative (add_local + immediate OP_VOID emission in
 * lockstep, so a later real store lands on the exact stack slot the
 * reservation carved out) and has no natural IR-tree representation, so
 * both callers just run it directly rather than duplicating it. */
static void lambda_prescan(Compiler *c, val_t body, int line) {
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
static void require_min_args(val_t args, int min, const char *form_name) {
    val_t p = args;
    for (int i = 0; i < min; i++) {
        if (!vis_pair(p))
            scm_raise_code(EC_WRONG_NUMBER_OF_ARGUMENTS,
                            "%s: ill-formed special form", form_name);
        p = vcdr(p);
    }
}

static void compile_lambda(Compiler *parent, val_t params, val_t body,
                            const char *name, int line) {
    Compiler c;
    init_compiler(&c, parent, name);

    int arity = compile_params(&c, params);
    c.chunk->arity = arity;
    begin_scope(&c);  /* body scope: depth 1+, so compile_define uses OP_STORE_LOCAL */

    lambda_prescan(&c, body, line);

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
    require_min_args(args, 2, "if");
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

/* Store half of a define, minus the trailing "define returns void": the
 * computed value must already be on top of the stack. Factored out of
 * emit_define_store (below) so compile_define_values can call it once
 * per bound name without accumulating one OP_VOID per name -- only ONE
 * OP_VOID should represent the whole define-values form's return, not
 * one per variable. */
static void emit_define_store_novoid(Compiler *c, val_t name, int line) {
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
}

/* Emit the store half of a define: the computed value must already be on
 * top of the stack. Shared by compile_define (after compiling an ordinary
 * value expression) and compile_symbolic (after emitting hand-rolled,
 * lookup-hygienic call bytecode instead of a compilable AST value). */
static void emit_define_store(Compiler *c, val_t name, int line) {
    emit_define_store_novoid(c, name, line);
    emit(c, OP_VOID, line);   /* define returns void */
}

static void compile_define(Compiler *c, val_t args, int line) {
    require_min_args(args, 1, "define");
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

/* (define-values (var...) expr) — R7RS. Unlike `receive` (compile_receive,
 * below), this can't desugar to a call-with-values + consumer lambda: its
 * targets are bindings in the ENCLOSING scope, and a consumer lambda's
 * formals are a different, inner scope with no way to reach back out and
 * populate the outer scope's locals/globals via ordinary recompiled
 * Scheme source. Direct codegen instead: compile expr once, then peel
 * off one component at a time with OP_VALUES_REF (the read-side
 * counterpart to OP_VALUES; see its own comment in opcode.h) and store
 * each with the same global/local dispatch an ordinary define uses.
 *
 * Scope for this landing: `vars` must be a proper list of symbols.
 * Neither a bare symbol (`(define-values all expr)`, meant to collect
 * every value into a list) nor a dotted/rest tail
 * (`(define-values (a . rest) expr)`) is supported -- raises a clear
 * compile-time error rather than silently defining nothing. This is not
 * a regression: eval.c's own S_DEFINE_VALUES case guards its binding
 * loop with vis_pair(vars), so a bare-symbol `vars` already silently
 * defines nothing today in the tree-walker either -- this just replaces
 * silent no-op with an honest diagnostic. Full rest-arg support, if ever
 * needed, would reuse compile_params' existing formals parsing (already
 * shared by compile_receive) as its template. */
static void compile_define_values(Compiler *c, val_t args, int line) {
    /* vcar/vcdr are unchecked casts (object.h) -- (define-values) with no
     * formals/expr at all makes `args` V_NIL, and vcar(V_NIL) dereferences
     * near-address 0xB, segfaulting the whole process rather than raising
     * a catchable compile error (independent code review, confirmed via
     * repro: `curry -e '(define-values)'` crashed before this guard). */
    if (!vis_pair(args))
        scm_raise(V_FALSE, "define-values: missing formals and expression");
    val_t vars = vcar(args);
    val_t expr = vis_pair(vcdr(args)) ? vcar(vcdr(args)) : V_VOID;

    int count = 0;
    for (val_t p = vars; vis_pair(p); p = vcdr(p)) {
        if (!vis_symbol(vcar(p)))
            scm_raise(V_FALSE, "define-values: expected a symbol in formals");
        /* OP_VALUES_REF's operand is a uint8_t component index (0-255,
         * matching every other single-byte opcode operand in this VM).
         * Past 256 formals, (uint8_t)i below would silently wrap --
         * v256 would overwrite v0's value, v257 v1's, etc., with no
         * error, just wrong bindings (independent security review).
         * Raise a clear compiler-limit error instead, mirroring
         * chunk_add_const's identical 256-constant bound. */
        if (count >= 256)
            scm_raise(V_FALSE,
                "define-values: too many formals (compiler limit: max 256)");
        count++;
    }
    if (!vis_nil(vars) && count == 0)
        scm_raise(V_FALSE,
            "define-values: rest/bare-symbol formals not yet supported");
    {
        val_t p = vars;
        for (int i = 0; i < count; i++) p = vcdr(p);
        if (!vis_nil(p))
            scm_raise(V_FALSE,
                "define-values: rest/bare-symbol formals not yet supported");
    }

    compile(c, expr, false, line);

    if (count == 0) {
        emit(c, OP_POP, line);   /* evaluated for effect only, R7RS */
    } else {
        val_t p = vars;
        for (int i = 0; i < count; i++) {
            val_t name = vcar(p);
            bool  last = (i == count - 1);
            if (!last) emit(c, OP_DUP, line);
            emit_ab(c, OP_VALUES_REF, (uint8_t)i, line);
            emit_define_store_novoid(c, name, line);
            p = vcdr(p);
        }
    }
    emit(c, OP_VOID, line);   /* define-values returns void, like define */
}

/* (defined? sym) — R7RS-adjacent extension (see eval.c's own S_DEFINED_P
 * case). Special form so `sym` is never evaluated. Resolution is split
 * between compile time and runtime:
 *
 *   - A local or upvalue reference (resolve_local/resolve_upvalue) is
 *     lexically ALWAYS bound once compiled -- emit #t directly, no
 *     runtime check.
 *   - Otherwise, a genuine runtime check against TARGET_ENV/GLOBAL_ENV
 *     (OP_DEFINED_GLOBAL, opcode.h/vm.c), since a global can be defined
 *     or not across separately-compiled top-level forms.
 *
 * Documented divergence from eval.c's dynamic env_lookup_or_false check:
 * the tree-walker's defined? on a forward-referenced internal define
 * (letrec* semantics) reads #f before that define's own form has
 * executed, since env_define only happens at that point in sequential
 * execution. Compiled locals don't have an equivalent "not yet run"
 * runtime state to check -- once compile_lambda's prescan reserves a
 * local's slot, it's lexically real for the rest of that scope, same as
 * every other forward-declared internal define this compiler already
 * treats this way (e.g. mutual recursion). defined? on such a name
 * therefore reads #t as soon as it's in lexical scope, not only after
 * its own definition has run. This matches the ordinary/expected use of
 * defined? (checking whether some top-level/global binding exists) and
 * diverges only on the edge case of asking whether a not-yet-reached
 * internal define has run yet -- not tracked, since the VM has no
 * per-slot "assigned yet" bit today. */
static void compile_defined_p(Compiler *c, val_t args, int line) {
    if (!vis_pair(args) || !vis_symbol(vcar(args)))
        scm_raise(V_FALSE, "defined?: expected a symbol");
    val_t sym = vcar(args);
    /* is_upvalue_reachable, not resolve_upvalue: a boolean query has no
     * business registering a real (otherwise unused) upvalue capture as
     * a side effect -- see that function's own comment. */
    if (resolve_local(c, sym) >= 0 || is_upvalue_reachable(c, sym)) {
        emit(c, OP_TRUE, line);
        return;
    }
    int ci = chunk_add_const(c->chunk, sym);
    emit_ab(c, OP_DEFINED_GLOBAL, (uint8_t)ci, line);
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

        /* Stash this binding's freshly-defined closure back onto the
         * RUNTIME RTD (referenced via rtd_ref, not the compile-time-only
         * `rtd` above -- that one only exists to extract name/nfields/
         * field_names for the %make-record-type call emitted earlier and
         * is never itself the object %make-record-type builds at
         * runtime) so record-type-constructor/-predicate/-accessors/
         * -mutators can retrieve it later (SRFI-279's rtd-properties). */
        val_t stash_call;
        switch (spec.bindings[i].role) {
            case RTD_ROLE_CONSTRUCTOR:
                stash_call = scm_cons(sym_intern_cstr("%rtd-set-constructor!"),
                    scm_cons(rtd_ref, scm_cons(spec.bindings[i].name, V_NIL)));
                break;
            case RTD_ROLE_PREDICATE:
                stash_call = scm_cons(sym_intern_cstr("%rtd-set-predicate!"),
                    scm_cons(rtd_ref, scm_cons(spec.bindings[i].name, V_NIL)));
                break;
            case RTD_ROLE_ACCESSOR:
                stash_call = scm_cons(sym_intern_cstr("%rtd-set-accessor!"),
                    scm_cons(rtd_ref, scm_cons(vfix((intptr_t)spec.bindings[i].field_index),
                        scm_cons(spec.bindings[i].name, V_NIL))));
                break;
            case RTD_ROLE_MUTATOR:
                stash_call = scm_cons(sym_intern_cstr("%rtd-set-mutator!"),
                    scm_cons(rtd_ref, scm_cons(vfix((intptr_t)spec.bindings[i].field_index),
                        scm_cons(spec.bindings[i].name, V_NIL))));
                break;
            default:
                stash_call = V_VOID;
                break;
        }
        emit(c, OP_POP, line); /* discard this binding's own OP_VOID */
        compile(c, stash_call, false, line);
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
    require_min_args(args, 2, "define-syntax");
    val_t name        = vcar(args);
    val_t xfm_expr    = vcar(vcdr(args));
    /* Make `name` itself visible to sr_is_protected (syntax_rules.c)
     * while its OWN (syntax-rules ...) is being compiled, so a self-
     * recursive locally-scoped macro's own name in its template doesn't
     * get incorrectly renamed (it lives only in this Compiler's
     * syntax_locals, never in any runtime env sr_current_env's def_env
     * could see) -- harmless to also do this for a top-level
     * (scope_depth == 0) define-syntax, where GLOBAL_ENV already covers
     * it via add_syntax_local below being unreachable for that branch. */
    val_t saved_locals = sr_get_current_local_macros();
    sr_set_current_local_macros(scm_cons(name, saved_locals));
    val_t transformer = V_VOID;
    ExnHandler ds_h;
    SCM_PROTECT(ds_h,
        transformer = compile_time_eval(xfm_expr),
        { sr_set_current_local_macros(saved_locals); scm_raise_val(ds_h.exn); });
    sr_set_current_local_macros(saved_locals);

    if (c->scope_depth == 0) {
        Syntax *syn = CURRY_NEW(Syntax);
        syn->hdr.type = T_SYNTAX; syn->hdr.flags = 0;
        syn->transformer = transformer;
        env_define(GLOBAL_ENV, name, vptr(syn));

        val_t literals, rules, ellipsis;
        val_t runtime_xfm_expr;
        if (sr_transformer_data(transformer, &literals, &rules, &ellipsis)) {
            /* Pass this chunk's own target_env (chunk.h) through as a 4th,
             * quoted argument so the runtime-rebuilt transformer's def_env
             * matches the compile-time one (sr_rebuild_syntax_env,
             * syntax_rules.c) instead of always defaulting to GLOBAL_ENV --
             * without this, a target_env-scoped macro's runtime transformer
             * couldn't see its own library's local helpers as "protected"
             * from hygienic renaming (see sr_is_protected). Embedding a
             * live env value here is fine for this same-process run (quote
             * just returns it verbatim, same as literals/rules/ellipsis
             * above); it shares the same not-yet-.scc-safe gap as
             * Chunk::target_env itself (both deferred together). */
            val_t rebuild_args = scm_cons(scm_cons(S_QUOTE, scm_cons(literals, V_NIL)),
                 scm_cons(scm_cons(S_QUOTE, scm_cons(rules, V_NIL)),
                  scm_cons(scm_cons(S_QUOTE, scm_cons(ellipsis, V_NIL)), V_NIL)));
            if (c->chunk->target_env != V_VOID)
                rebuild_args = scm_append(rebuild_args,
                    scm_cons(scm_cons(S_QUOTE, scm_cons(c->chunk->target_env, V_NIL)), V_NIL));
            runtime_xfm_expr = scm_cons(sym_intern_cstr("%rebuild-syntax-rules"), rebuild_args);
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
 * that itself needs to PROCEDURALLY INVOKE a sibling local macro during
 * its own construction is an exotic case this implementation doesn't
 * support, no differently for let-syntax vs. letrec-syntax.
 *
 * A narrower, much more common case IS supported, though: a sibling
 * macro's own NAME appearing in another sibling's (or its own) template,
 * for sr_is_protected's (syntax_rules.c) hygiene decision at LATER
 * macro-use time -- e.g. two mutually-recursive local macros, or one
 * self-recursive local macro. All these binding names are pushed onto
 * sr_current_local_macros (syntax_rules.c) up front, before any of
 * their transformer expressions are compiled, treating let-syntax and
 * letrec-syntax identically (matching this function's own existing
 * behavior elsewhere) — this is a static namelist for a rename
 * decision, not construction-time visibility, so it doesn't need the
 * unsupported procedural-invocation case above. */
static void compile_let_syntax(Compiler *c, val_t args, bool tail, int line) {
    require_min_args(args, 1, "let-syntax");  /* shared with letrec-syntax */
    val_t bindings = vcar(args);
    val_t body     = vcdr(args);

    begin_scope(c);

    val_t saved_locals = sr_get_current_local_macros();
    val_t names = saved_locals;
    for (val_t b = bindings; vis_pair(b); b = vcdr(b))
        names = scm_cons(vcar(vcar(b)), names);
    sr_set_current_local_macros(names);

    val_t b = bindings;
    ExnHandler ls_h;
    SCM_PROTECT(ls_h, {
        while (vis_pair(b)) {
            val_t bind = vcar(b);
            val_t name = vcar(bind);
            val_t xfm  = compile_time_eval(vcar(vcdr(bind)));
            add_syntax_local(c, name, xfm);
            b = vcdr(b);
        }
    }, { sr_set_current_local_macros(saved_locals); scm_raise_val(ls_h.exn); });
    sr_set_current_local_macros(saved_locals);

    compile_seq(c, body, tail, line);
    end_scope(c, line);
}

static void compile_set(Compiler *c, val_t args, int line) {
    require_min_args(args, 2, "set!");
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

/* Conservative textual scan: does `expr` (or anything nested inside it,
 * except inside a (quote ...) subform) contain (set! name ...) anywhere,
 * OR any macro use at all (local syntax-local, or a global/target_env
 * T_SYNTAX binding)? Deliberately ignores lexical shadowing for the
 * set!-target check -- a false positive there (an inner binding that
 * happens to share `name` but isn't really the same variable) only costs
 * a missed optimization. The macro-use check is a SEPARATE, unconditional
 * bail-out (found by code review, confirmed via repro): a plain textual
 * scan of the RAW body can only ever see set! forms written literally in
 * source -- it is blind to a macro that itself EXPANDS to (set! name
 * ...), since expansion happens later, during compile(). A named-let
 * loop whose body used a macro that expanded to `(set! loop other-fn)`
 * previously still got OP_SELF_TAIL_CALL (the scan found no literal
 * set!), so the compiler kept trusting "this call always means myself"
 * even after the loop variable was reassigned to something else --
 * confirmed: the loop silently kept calling the stale closure instead of
 * the reassigned one. Rather than expanding every macro here just to
 * scan the result (duplicating compile()'s own expansion machinery), any
 * macro use anywhere in the body unconditionally disables the
 * optimization -- correct by construction (no expansion result can ever
 * need scanning if the whole body already bails out), at the cost of
 * also declining the optimization for named-let loops that happen to use
 * some unrelated macro that doesn't touch the loop variable at all. That
 * tradeoff is deliberate: only a hard NO is guaranteed to never regress
 * to a wrong answer as new macros get written.
 *
 * Mirrors sr_collect_free_symbols's own "don't descend into quote"
 * pattern (syntax_rules.c), and compile()'s own is-macro check (checked
 * local-syntax first, then target_env, then GLOBAL_ENV) for the macro
 * detection. lang_translate on the head catches an Akkadian-spelled
 * set! too, matching how compile()'s own dispatch translates every head
 * before comparing against S_SET. */
static bool expr_mentions_set_target(Compiler *c, val_t expr, val_t name) {
    if (!vis_pair(expr)) return false;
    val_t raw_head = vcar(expr);
    val_t head = lang_translate(raw_head);
    if (head == S_QUOTE) return false;
    if (head == S_SET && vis_pair(vcdr(expr)) && vcar(vcdr(expr)) == name)
        return true;
    if (vis_symbol(raw_head)) {
        val_t transformer;
        bool is_macro = resolve_syntax_local(c, raw_head, &transformer);
        if (!is_macro && c->chunk->target_env != V_VOID)
            is_macro = vis_syntax(env_lookup_or_false(c->chunk->target_env, raw_head));
        if (!is_macro)
            is_macro = vis_syntax(env_lookup_or_false(GLOBAL_ENV, raw_head));
        if (is_macro) return true;  /* unconditional bail-out, see comment above */
    }
    for (val_t p = expr; vis_pair(p); p = vcdr(p))
        if (expr_mentions_set_target(c, vcar(p), name)) return true;
    return false;
}

static bool body_mentions_set_target(Compiler *c, val_t body, val_t name) {
    for (val_t p = body; vis_pair(p); p = vcdr(p))
        if (expr_mentions_set_target(c, vcar(p), name)) return true;
    return false;
}

/* Tier 2.3 local inliner: conservative self-recursion check for an
 * inlining candidate -- does `expr` contain `name` as a symbol ANYWHERE
 * (car or cdr position, including inside a quoted datum -- unlike
 * expr_mentions_set_target above, this deliberately does NOT special-case
 * quote or macro use, since any occurrence at all, even inert data, is
 * grounds to reject: over-conservative on purpose. A stray unrelated
 * occurrence just means a safe, correct candidate gets left un-inlined,
 * never the other way around. Does not descend into vector literals --
 * missing a symbol occurrence there is still safe, since a vector isn't
 * something that gets called. */
static bool expr_contains_symbol(val_t expr, val_t name) {
    if (expr == name) return true;
    if (!vis_pair(expr)) return false;
    return expr_contains_symbol(vcar(expr), name) ||
           expr_contains_symbol(vcdr(expr), name);
}

static bool body_contains_symbol(val_t body, val_t name) {
    for (val_t p = body; vis_pair(p); p = vcdr(p))
        if (expr_contains_symbol(vcar(p), name)) return true;
    return false;
}

/* Tier 2.4: multi-name sibling-reference guard for let-bound candidates
 * (let* only ever has one binding to check per registration -- see
 * ir_lower_let_star's own use of body_contains_symbol above -- but plain
 * let can have several siblings at once). Independent code review
 * caught a real, confirmed miscompilation this closes: lambda_is_closed
 * (below) only checks a candidate's free variables against the
 * ENCLOSING Compiler `c` -- correctly, since that's the only real scope
 * that exists yet at ir_lower_let time -- but a let's OWN sibling
 * binding names (including the candidate's own name) are NEITHER
 * locally bound within the candidate's own params NOR yet resolvable in
 * `c` at this point, so lambda_is_closed's existing check silently
 * treats a reference to one of them as "global, fine". Once
 * ir_emit_inline_call later splices the body into the wrapper's own
 * frame, that name IS a real local there if the call site happens to be
 * inside the same let's body -- so the reference silently resolves
 * instead of correctly raising unbound-variable, exactly the way a
 * genuine letrec-style self/mutual-reference would (but plain `let`
 * bindings are NOT visible to each other's inits or to any sibling's
 * own captured-closure environment -- only letrec makes that true).
 * Confirmed via direct repro before this fix: `(let ((f (lambda (x) f)))
 * (procedure? (f 1)))` printed #t (should raise unbound-variable);
 * `(let ((y 10) (f (lambda (x) (+ x y)))) (f 1))` returned 11 (should
 * also raise unbound-variable, since y is a SIBLING binding, not
 * visible to f's own closure under real let semantics). Any reference
 * to ANY of the let's own binding names -- checked as a flat list,
 * ignoring which specific sibling it is, including the candidate's own
 * name -- is grounds to reject: same over-conservative-but-safe
 * tradeoff body_contains_symbol's own single-name self-recursion check
 * already uses (a candidate that merely shares a name with an unrelated
 * OUTER variable also gets conservatively rejected here; that just
 * means a safe candidate falls back to a real, correct, non-inlined
 * call, never a miscompile). */
static bool expr_contains_any_symbol(val_t expr, val_t names) {
    if (vis_symbol(expr)) {
        for (val_t p = names; vis_pair(p); p = vcdr(p))
            if (vcar(p) == expr) return true;
        return false;
    }
    if (!vis_pair(expr)) return false;
    return expr_contains_any_symbol(vcar(expr), names) ||
           expr_contains_any_symbol(vcdr(expr), names);
}

static bool body_contains_any_symbol(val_t body, val_t names) {
    for (val_t p = body; vis_pair(p); p = vcdr(p))
        if (expr_contains_any_symbol(vcar(p), names)) return true;
    return false;
}

/* Tier 2.3: total raw AST node count (every pair cell plus every atom
 * leaf counts as one) -- the inliner's size budget. Iterative over the
 * cdr-spine, not recursive -- a wide, flat body (many top-level forms,
 * plausible from macro-generated code) costs O(1) C-stack depth this
 * way, not one C frame per element; only car sub-structure still
 * recurses, bounded by genuine Scheme nesting depth rather than the
 * body's overall size. Also stops early, without finishing the walk,
 * once already over `budget` -- a plain unconditional recursive count
 * (the original shape of this function) doesn't actually avoid deep
 * C-stack recursion for a pathologically large candidate just by being
 * CALLED before body_contains_symbol's own (unbounded) walk, the way an
 * earlier version's ordering comment claimed -- caught by independent
 * code review. */
static int ir_count_ast_nodes(val_t expr, int budget) {
    int count = 0;
    while (vis_pair(expr)) {
        count += 1 + ir_count_ast_nodes(vcar(expr), budget);
        if (count > budget) return count;
        expr = vcdr(expr);
    }
    return count + 1;   /* the terminal atom -- nil, or a dotted tail */
}

/* Tier 2.3: true (with *argc_out set) iff `params` is a proper list --
 * rest/dotted param lists are rejected as inlining candidates to keep
 * call-site argument substitution simple (exact argc match only).
 * scm_list_length (src/builtins.c) already performs exactly this walk
 * and returns -1 for an improper list. */
static bool params_proper_arity(val_t params, int *argc_out) {
    int n = scm_list_length(params);
    if (n < 0) return false;
    *argc_out = n;
    return true;
}

/* Tier 2.4 (let/let*-bound local inlining): lambda_is_closed and its
 * helpers below. A new, side-effect-free, purely syntactic walker,
 * modeled directly on expr_mentions_set_target's own established shape
 * (conservative pre-expansion syntactic scanning, unconditional bail-out
 * on anything it can't cleanly reason about) -- see IRKnownParam's own
 * comment in ir.h for why this has to run at ir_lower time, before
 * either the candidate or its would-be enclosing wrapper exist as real
 * Compilers, unlike IR_DEFINE's "compile it, then read what it proved"
 * registration.
 *
 * A linked list of symbol sets, one per lexical scope nesting level
 * introduced while walking a candidate body -- NOT Compiler::locals[]:
 * no physical stack slots exist for any of this yet, this is pure
 * syntactic bookkeeping over raw val_t forms. */
typedef struct BoundScope {
    struct BoundScope *parent;
    val_t               names;   /* flat list of symbols bound at this level */
} BoundScope;

static bool bound_scope_has(BoundScope *scope, val_t name) {
    for (BoundScope *s = scope; s; s = s->parent)
        for (val_t p = s->names; vis_pair(p); p = vcdr(p))
            if (vcar(p) == name) return true;
    return false;
}

/* Flattens a param spec (proper list, dotted rest-arg, or a bare "all
 * rest" symbol) into a plain list of every name it binds -- order
 * doesn't matter, only membership. */
static val_t collect_param_names(val_t params) {
    if (vis_symbol(params)) return scm_cons(params, V_NIL);
    val_t result = V_NIL;
    val_t p = params;
    while (vis_pair(p)) { result = scm_cons(vcar(p), result); p = vcdr(p); }
    if (vis_symbol(p)) result = scm_cons(p, result);
    return result;
}

/* Same three-tier lookup expr_mentions_set_target already uses (local
 * syntax -> target_env -> GLOBAL_ENV) to decide "is this head a macro
 * use" -- kept as its own helper here since lambda_is_closed needs the
 * same check from more than one call site below. */
static bool head_is_macro(Compiler *c, val_t raw_head) {
    if (!vis_symbol(raw_head)) return false;
    val_t transformer;
    if (resolve_syntax_local(c, raw_head, &transformer)) return true;
    if (c->chunk->target_env != V_VOID &&
        vis_syntax(env_lookup_or_false(c->chunk->target_env, raw_head)))
        return true;
    if (vis_syntax(env_lookup_or_false(GLOBAL_ENV, raw_head))) return true;
    return false;
}

/* Collects every name a body's own front-of-body internal-definition-like
 * forms bind, mirroring lambda_prescan's dispatch table (compiler.c) --
 * reusing only the recognized-form list, never lambda_prescan's own
 * side-effecting add_local/emit calls. Returns false (give up on the
 * WHOLE candidate, treated as not-closed by every caller) the moment it
 * sees define-record-type/define-rule/define-ruleset/define-algebra/
 * define-syntax: these either need real, non-trivial parsing to know
 * what they bind (define-record-type goes through
 * record_type_build_spec) or don't bind a plain variable name at all in
 * a way worth specially modeling here -- reproducing that logic purely
 * to let a FEW more candidates qualify isn't worth the risk of getting
 * it subtly wrong; bailing out is always safe, just conservative. Scans
 * the whole body (not just a strict R7RS-ordered prefix): this function
 * only needs to avoid UNDER-binding a name that's genuinely visible
 * later (which would cause a false "captures!" verdict, safe but overly
 * conservative) -- it doesn't need to replicate R7RS's own
 * internal-definition-ordering validation, since a genuinely malformed
 * body still gets a real compile-time error at real compile time
 * regardless of whether this candidate gets inlined. */
static bool collect_body_def_names(val_t body, val_t *names_out) {
    val_t names = V_NIL;
    for (val_t p = body; vis_pair(p); p = vcdr(p)) {
        val_t form = vcar(p);
        if (!vis_pair(form) || !vis_symbol(vcar(form))) continue;
        val_t head = lang_translate(vcar(form));
        if (head == S_DEFINE_RECORD_TYPE || head == S_DEFINE_RULE ||
            head == S_DEFINE_RULESET || head == S_DEFINE_ALGEBRA ||
            head == S_DEFINE_SYNTAX) {
            return false;
        }
        if (head == S_SYMBOLIC) {
            for (val_t q = vcdr(form); vis_pair(q); q = vcdr(q))
                if (vis_symbol(vcar(q))) names = scm_cons(vcar(q), names);
        } else if (head == S_DEFINE_VALUES) {
            val_t vars = vis_pair(vcdr(form)) ? vcar(vcdr(form)) : V_NIL;
            for (val_t q = vars; vis_pair(q) && vis_symbol(vcar(q)); q = vcdr(q))
                names = scm_cons(vcar(q), names);
        } else if (head == S_DEFINE) {
            val_t defname = vcar(vcdr(form));
            if (vis_symbol(defname)) names = scm_cons(defname, names);
            else if (vis_pair(defname)) names = scm_cons(vcar(defname), names);
        }
    }
    *names_out = names;
    return true;
}

static bool expr_is_closed(Compiler *c, val_t expr, BoundScope *scope, int *budget);

/* Walks a body (a list of forms, e.g. a lambda/let/do body): collects
 * its own front-of-body internal-definition names FIRST (matching
 * lambda_prescan's letrec*-style "every internal define visible from
 * the first one onward" semantics -- not incremental, or a later
 * internal define referencing an earlier one would be wrongly flagged
 * as capturing something), extends `scope` with them, then checks every
 * form against the extended scope. */
static bool body_is_closed(Compiler *c, val_t body, BoundScope *scope, int *budget) {
    val_t def_names;
    if (!collect_body_def_names(body, &def_names)) return false;
    BoundScope inner = { scope, def_names };
    for (val_t p = body; vis_pair(p); p = vcdr(p))
        if (!expr_is_closed(c, vcar(p), &inner, budget)) return false;
    return true;
}

/* Checks a `let`-shaped bindings list `((name init) ...)` where every
 * init is checked against `outer_scope` (siblings NOT visible to each
 * other -- correct, not just conservative, for plain let: see
 * lambda_is_closed's own top-level comment) and returns the collected
 * names as a fresh scope layer for the caller to check body against. */
static bool let_bindings_closed(Compiler *c, val_t bindings, BoundScope *outer_scope,
                                 int *budget, BoundScope *out_scope) {
    val_t names = V_NIL;
    for (val_t b = bindings; vis_pair(b); b = vcdr(b)) {
        val_t binding = vcar(b);
        val_t name = vcar(binding);
        val_t init = vis_pair(vcdr(binding)) ? vcar(vcdr(binding)) : V_FALSE;
        if (!expr_is_closed(c, init, outer_scope, budget)) return false;
        names = scm_cons(name, names);
    }
    out_scope->parent = outer_scope;
    out_scope->names  = names;
    return true;
}

static bool expr_is_closed(Compiler *c, val_t expr, BoundScope *scope, int *budget) {
    if (*budget <= 0) return false;   /* exhausted -- fail safe: not closed */
    (*budget)--;

    if (vis_symbol(expr)) {
        if (bound_scope_has(scope, expr)) return true;
        if (resolve_local(c, expr) >= 0 || is_upvalue_reachable(c, expr))
            return false;   /* a genuine free-variable capture */
        return true;        /* global, or genuinely unbound -- either way, fine */
    }
    if (!vis_pair(expr)) return true;   /* self-evaluating atom -- fine */

    val_t raw_head = vcar(expr);
    val_t head = vis_symbol(raw_head) ? lang_translate(raw_head) : raw_head;

    if (head == S_QUOTE) return true;   /* quoted data, not a reference */

    /* Macro bail-out -- see this function's own header comment. Checked
     * before any of the special-form dispatch below, since a locally
     * shadowed macro name could otherwise be misread as one of these
     * keywords (the same risk classify_head's own dispatch is built to
     * avoid, mirrored here). */
    if (vis_symbol(raw_head) && head_is_macro(c, raw_head)) return false;
    if (head == S_DEFINE_SYNTAX || head == S_LET_SYNTAX || head == S_LETREC_SYNTAX)
        return false;

    if (head == S_LAMBDA) {
        val_t params2 = vis_pair(vcdr(expr)) ? vcar(vcdr(expr)) : V_NIL;
        val_t body2   = vis_pair(vcdr(expr)) ? vcdr(vcdr(expr)) : V_NIL;
        BoundScope inner = { scope, collect_param_names(params2) };
        return body_is_closed(c, body2, &inner, budget);
    }

    if (head == S_LET) {
        val_t rest = vcdr(expr);
        if (vis_pair(rest) && vis_symbol(vcar(rest))) {
            /* Named let: (let loop ((x v) ...) body...) -- loop's own
             * name IS visible within its own body (it's how the loop
             * recurses), unlike a plain let's sibling bindings. */
            val_t loop_name = vcar(rest);
            val_t bindings  = vis_pair(vcdr(rest)) ? vcar(vcdr(rest)) : V_NIL;
            val_t body2     = vis_pair(vcdr(rest)) ? vcdr(vcdr(rest)) : V_NIL;
            BoundScope inner;
            if (!let_bindings_closed(c, bindings, scope, budget, &inner)) return false;
            inner.names = scm_cons(loop_name, inner.names);
            return body_is_closed(c, body2, &inner, budget);
        }
        val_t bindings = vis_pair(rest) ? vcar(rest) : V_NIL;
        val_t body2    = vis_pair(rest) ? vcdr(rest) : V_NIL;
        BoundScope inner;
        if (!let_bindings_closed(c, bindings, scope, budget, &inner)) return false;
        return body_is_closed(c, body2, &inner, budget);
    }

    if (head == S_LET_STAR) {
        val_t bindings = vis_pair(vcdr(expr)) ? vcar(vcdr(expr)) : V_NIL;
        val_t body2    = vis_pair(vcdr(expr)) ? vcdr(vcdr(expr)) : V_NIL;
        BoundScope *cur = scope;
        for (val_t b = bindings; vis_pair(b); b = vcdr(b)) {
            val_t binding = vcar(b);
            val_t name    = vcar(binding);
            val_t init    = vis_pair(vcdr(binding)) ? vcar(vcdr(binding)) : V_FALSE;
            /* Sequential: each init is checked against everything bound
             * by EARLIER bindings in this same let*, not later ones. */
            if (!expr_is_closed(c, init, cur, budget)) return false;
            BoundScope *level = (BoundScope *)ir_arena_alloc(
                c->ir_arena, sizeof(BoundScope));
            level->parent = cur;
            level->names  = scm_cons(name, V_NIL);
            cur = level;
        }
        return body_is_closed(c, body2, cur, budget);
    }

    if (head == S_LETREC || head == S_LETREC_STAR) {
        val_t bindings = vis_pair(vcdr(expr)) ? vcar(vcdr(expr)) : V_NIL;
        val_t body2    = vis_pair(vcdr(expr)) ? vcdr(vcdr(expr)) : V_NIL;
        /* letrec(*): every binding name is visible to every init AND to
         * the body -- build the whole scope layer first. */
        val_t names = V_NIL;
        for (val_t b = bindings; vis_pair(b); b = vcdr(b))
            names = scm_cons(vcar(vcar(b)), names);
        BoundScope inner = { scope, names };
        for (val_t b = bindings; vis_pair(b); b = vcdr(b)) {
            val_t binding = vcar(b);
            val_t init = vis_pair(vcdr(binding)) ? vcar(vcdr(binding)) : V_FALSE;
            if (!expr_is_closed(c, init, &inner, budget)) return false;
        }
        return body_is_closed(c, body2, &inner, budget);
    }

    if (head == S_DO) {
        /* (do ((var init step) ...) (test result...) body...) */
        val_t var_specs = vis_pair(vcdr(expr)) ? vcar(vcdr(expr)) : V_NIL;
        val_t rest2     = vis_pair(vcdr(expr)) ? vcdr(vcdr(expr)) : V_NIL;
        val_t term      = vis_pair(rest2) ? vcar(rest2) : V_NIL;
        val_t body2     = vis_pair(rest2) ? vcdr(rest2) : V_NIL;
        val_t names = V_NIL;
        for (val_t vs = var_specs; vis_pair(vs); vs = vcdr(vs)) {
            val_t spec = vcar(vs);
            val_t init = vis_pair(vcdr(spec)) ? vcar(vcdr(spec)) : V_FALSE;
            /* init exprs see the OUTER scope only, matching do's own
             * semantics (vars aren't bound yet for their own inits). */
            if (!expr_is_closed(c, init, scope, budget)) return false;
            names = scm_cons(vcar(spec), names);
        }
        BoundScope inner = { scope, names };
        for (val_t vs = var_specs; vis_pair(vs); vs = vcdr(vs)) {
            val_t spec = vcar(vs);
            val_t step = vis_pair(vcdr(spec)) && vis_pair(vcdr(vcdr(spec)))
                             ? vcar(vcdr(vcdr(spec))) : vcar(spec);
            if (!expr_is_closed(c, step, &inner, budget)) return false;
        }
        for (val_t t = term; vis_pair(t); t = vcdr(t))
            if (!expr_is_closed(c, vcar(t), &inner, budget)) return false;
        return body_is_closed(c, body2, &inner, budget);
    }

    if (head == S_RECEIVE) {
        val_t formals  = vis_pair(vcdr(expr)) ? vcar(vcdr(expr)) : V_NIL;
        val_t rest2    = vis_pair(vcdr(expr)) ? vcdr(vcdr(expr)) : V_NIL;
        val_t producer = vis_pair(rest2) ? vcar(rest2) : V_FALSE;
        val_t body2    = vis_pair(rest2) ? vcdr(rest2) : V_NIL;
        if (!expr_is_closed(c, producer, scope, budget)) return false;
        BoundScope inner = { scope, collect_param_names(formals) };
        return body_is_closed(c, body2, &inner, budget);
    }

    if (head == S_LET_VALUES || head == S_LET_STAR_VALUES) {
        val_t bindings = vis_pair(vcdr(expr)) ? vcar(vcdr(expr)) : V_NIL;
        val_t body2    = vis_pair(vcdr(expr)) ? vcdr(vcdr(expr)) : V_NIL;
        bool star = (head == S_LET_STAR_VALUES);
        BoundScope *cur = scope;
        val_t all_names = V_NIL;
        for (val_t b = bindings; vis_pair(b); b = vcdr(b)) {
            val_t binding  = vcar(b);
            val_t formals  = vcar(binding);
            val_t producer = vis_pair(vcdr(binding)) ? vcar(vcdr(binding)) : V_FALSE;
            /* let-values: every producer sees only the OUTER scope.
             * let*-values: sequential, like let*. */
            if (!expr_is_closed(c, producer, star ? cur : scope, budget)) return false;
            if (star) {
                BoundScope *level = (BoundScope *)ir_arena_alloc(
                    c->ir_arena, sizeof(BoundScope));
                level->parent = cur;
                level->names  = collect_param_names(formals);
                cur = level;
            } else {
                val_t names = collect_param_names(formals);
                for (val_t q = names; vis_pair(q); q = vcdr(q))
                    all_names = scm_cons(vcar(q), all_names);
            }
        }
        BoundScope inner_flat = { scope, all_names };
        return body_is_closed(c, body2, star ? cur : &inner_flat, budget);
    }

    if (head == S_GUARD) {
        /* (guard (var clause...) body...) -- var is bound ONLY within
         * clause, never within body (compile_guard, compiler.c: body
         * gets its own separate zero-arg thunk that never sees var). */
        val_t var_and_clauses = vis_pair(vcdr(expr)) ? vcar(vcdr(expr)) : V_NIL;
        val_t body2   = vis_pair(vcdr(expr)) ? vcdr(vcdr(expr)) : V_NIL;
        val_t var     = vis_pair(var_and_clauses) ? vcar(var_and_clauses) : V_FALSE;
        val_t clauses = vis_pair(var_and_clauses) ? vcdr(var_and_clauses) : V_NIL;
        BoundScope inner = { scope, vis_symbol(var) ? scm_cons(var, V_NIL) : V_NIL };
        for (val_t cl = clauses; vis_pair(cl); cl = vcdr(cl))
            if (!expr_is_closed(c, vcar(cl), &inner, budget)) return false;
        return body_is_closed(c, body2, scope, budget);
    }

    /* Ordinary form (call, or any special form with no extra binding
     * semantics of its own -- if/and/or/begin/when/unless/cond/case/
     * set!/quasiquote/unquote/parameterize/etc.): recurse into every
     * element, head included -- a head symbol shadowed by an outer local
     * (a rare edge case) is correctly caught by the same vis_symbol
     * branch above when THIS same function is re-entered for it. */
    val_t p = expr;
    for (; vis_pair(p); p = vcdr(p))
        if (!expr_is_closed(c, vcar(p), scope, budget)) return false;
    /* The final, non-pair cdr -- almost always V_NIL for a well-formed
     * form, but a genuinely dotted tail (nonstandard for a call/special
     * form, though not something this walker should simply assume can't
     * occur) could itself be a reference. Checking it too costs nothing
     * for the overwhelmingly common nil case and closes what would
     * otherwise be a silent under-shadowing gap for the uncommon one. */
    return expr_is_closed(c, p, scope, budget);
}

/* Tier 2.4 public entry point: true iff `body` (compiled with `params`
 * as its own parameters) captures NO free variable reachable from `c` --
 * see this whole block's header comment for why this has to be a static,
 * pre-compile, pre-expansion check rather than IR_DEFINE's "compile it,
 * then read what it proved" approach. `*budget` is decremented by every
 * AST node visited and must already be positive on entry; exhaustion
 * fails safe (returns false, i.e. "not closed") rather than continuing
 * to recurse -- this must be independent of call-order relative to any
 * other budget check (see ir_count_ast_nodes's own comment for why an
 * earlier version of THAT function got exactly this wrong: relying on
 * being called first doesn't bound this function's OWN recursion if it
 * doesn't carry its own budget). */
static bool lambda_is_closed(Compiler *c, val_t params, val_t body, int *budget) {
    BoundScope top = { NULL, collect_param_names(params) };
    return body_is_closed(c, body, &top, budget);
}

static void compile_let(Compiler *c, val_t args, bool tail, int line) {
    require_min_args(args, 1, "let");
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
        require_min_args(body, 1, "let");  /* named-let's own bindings list */
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

        /* Inner loop lambda; loop_name resolves as upvalue from outer's slot 0.
         * Arm the self-tail-call thread-local so a tail-position call to
         * loop_name within this exact body compiles to OP_SELF_TAIL_CALL
         * instead of the ordinary upvalue-load-then-call sequence -- see
         * Compiler::self_tail_name's comment and compile_call. The
         * set!-scan runs over the RAW (uncompiled) body once, up front,
         * so every self-tail-call site compiled anywhere in this body
         * consistently sees the same answer regardless of where in the
         * body a set! to loop_name might textually appear relative to
         * a given tail call. */
        g_compile_self_tail_name    = loop_name;
        g_compile_self_tail_mutated = body_mentions_set_target(c, body, loop_name);
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
    require_min_args(args, 1, "let*");
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
    require_min_args(args, 1, "letrec");
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

/* Walks a lambda-formals shape (proper list, dotted list, or a bare
 * rest-symbol) and returns a same-shaped structure with every identifier
 * replaced by a fresh "%%lvB_P" temp symbol (B = binding index within the
 * enclosing let-values form, P = position within this binding's formals).
 * Prepends each (original . temp) pair onto *pairs (order doesn't matter --
 * it only ever becomes an unordered `let` binding list). */
static val_t let_values_temp_formals(val_t formals, int bi, int *pi, val_t *pairs) {
    if (vis_nil(formals)) return V_NIL;
    char namebuf[40];
    if (vis_symbol(formals)) {
        snprintf(namebuf, sizeof(namebuf), "%%lv%d_%d", bi, (*pi)++);
        val_t tmp = sym_intern_cstr(namebuf);
        *pairs = scm_cons(scm_cons(formals, scm_cons(tmp, V_NIL)), *pairs);
        return tmp;
    }
    val_t orig_name = vcar(formals);
    snprintf(namebuf, sizeof(namebuf), "%%lv%d_%d", bi, (*pi)++);
    val_t tmp = sym_intern_cstr(namebuf);
    *pairs = scm_cons(scm_cons(orig_name, scm_cons(tmp, V_NIL)), *pairs);
    return scm_cons(tmp, let_values_temp_formals(vcdr(formals), bi, pi, pairs));
}

/* (let-values (((a b) e1) ((c) e2) ...) body ...)
 * Desugars to nested call-with-values, one per binding, each consumer
 * lambda using FRESH temp names rather than the source-level formal
 * names -- so a later producer expression (e.g. e2) can never observe an
 * earlier binding (e.g. a/b), matching let-values' parallel-binding
 * semantics (as opposed to let*-values' sequential visibility). The
 * outermost consumer, once every producer has run, closes with a single
 * `let` that binds the real names to their temps:
 *   (call-with-values (lambda () e1)
 *     (lambda (%%lv0_0 %%lv0_1)
 *       (call-with-values (lambda () e2)
 *         (lambda (%%lv1_0)
 *           (let ((a %%lv0_0) (b %%lv0_1) (c %%lv1_0)) body ...)))))
 *
 * Each nested call-with-values is the sole/last expression of its
 * enclosing consumer lambda's body, so it's always compiled tail=true
 * (compile_lambda already treats a lambda's own body as tail position
 * regardless of the lambda's own calling context) -- meaning the whole
 * chain, and this desugaring's use above it, gets genuine TCO via
 * OP_TAIL_CALL_WITH_VALUES (see the dispatch for S_CALL_WITH_VALUES in
 * the main compile() switch) when the overall let-values form itself
 * sits in tail position. */
static val_t let_values_expand(val_t bindings, val_t body, int bi, val_t pairs) {
    if (vis_nil(bindings)) {
        val_t let_bindings = V_NIL;
        val_t p = pairs;
        while (vis_pair(p)) { let_bindings = scm_cons(vcar(p), let_bindings); p = vcdr(p); }
        return scm_cons(S_LET, scm_cons(let_bindings, body));
    }
    require_min_args(bindings, 1, "let-values");
    val_t binding  = vcar(bindings);
    require_min_args(binding, 2, "let-values");  /* one (formals producer) pair */
    val_t formals  = vcar(binding);
    val_t producer = vcar(vcdr(binding));
    int pi = 0;
    val_t temp_formals = let_values_temp_formals(formals, bi, &pi, &pairs);
    val_t inner = let_values_expand(vcdr(bindings), body, bi + 1, pairs);
    val_t producer_lam = scm_cons(S_LAMBDA, scm_cons(V_NIL, scm_cons(producer, V_NIL)));
    val_t consumer_lam = scm_cons(S_LAMBDA, scm_cons(temp_formals, scm_cons(inner, V_NIL)));
    val_t cwv = sym_intern_cstr("call-with-values");
    return scm_cons(cwv, scm_cons(producer_lam, scm_cons(consumer_lam, V_NIL)));
}

static void compile_let_values(Compiler *c, val_t args, bool tail, int line) {
    require_min_args(args, 1, "let-values");
    val_t bindings = vcar(args);
    val_t body     = vcdr(args);
    if (vis_nil(bindings)) { compile_seq(c, body, tail, line); return; }
    compile(c, let_values_expand(bindings, body, 0, V_NIL), tail, line);
}

/* (let*-values (((a b) e1) rest...) body ...)
 * Desugars to nested call-with-values using the REAL formal names directly
 * (no temp-name indirection needed): each subsequent producer expression
 * is compiled as part of the previous binding's consumer-lambda body, so
 * it correctly sees earlier bindings -- exactly let*-values' sequential
 * semantics. Mirrors compile_let_star's existing self-embedding recursion
 * (reusing S_LET_STAR_VALUES for the inner form) rather than the
 * expand-then-compile-once style of compile_let_values above. */
static void compile_let_star_values(Compiler *c, val_t args, bool tail, int line) {
    require_min_args(args, 1, "let*-values");
    val_t bindings = vcar(args);
    val_t body     = vcdr(args);
    if (vis_nil(bindings)) { compile_seq(c, body, tail, line); return; }

    require_min_args(bindings, 1, "let*-values");
    val_t binding  = vcar(bindings);
    require_min_args(binding, 2, "let*-values");  /* one (formals producer) pair */
    val_t formals  = vcar(binding);
    val_t producer = vcar(vcdr(binding));
    val_t rest     = vcdr(bindings);

    val_t inner_body;
    if (vis_nil(rest)) {
        inner_body = body;
    } else {
        val_t inner_form = scm_cons(S_LET_STAR_VALUES, scm_cons(rest, body));
        inner_body = scm_cons(inner_form, V_NIL);
    }

    val_t producer_lam = scm_cons(S_LAMBDA, scm_cons(V_NIL, scm_cons(producer, V_NIL)));
    val_t consumer_lam = scm_cons(S_LAMBDA, scm_cons(formals, inner_body));
    val_t cwv = sym_intern_cstr("call-with-values");
    val_t expanded = scm_cons(cwv, scm_cons(producer_lam, scm_cons(consumer_lam, V_NIL)));
    compile(c, expanded, tail, line);
}

static void compile_cond(Compiler *c, val_t clauses, bool tail, int line) {
    /* (cond (test expr...) ... (else expr...)) */
    /* Every non-else clause now pushes exactly one end_patches entry
     * (the "jump past the trailing OP_VOID fallback" fix applies
     * uniformly to every clause, not just non-last ones anymore -- see
     * this function's own clause-by-clause comments), so a cond with N
     * non-else clauses needs N slots here, not N-1 as before. 512 is a
     * generous ceiling (previously 64, unchecked, so a >64-clause plain
     * cond already silently overran this array before this diff --
     * confirmed by review); the explicit check below turns any case
     * that still exceeds it into a clean compile-time error instead of
     * stack corruption. */
    int end_patches[512]; int np = 0;
#define COND_PUSH_PATCH(off) do { \
        if (np >= (int)(sizeof(end_patches) / sizeof(end_patches[0]))) \
            scm_raise(V_FALSE, "cond: too many clauses (compiler limit)"); \
        end_patches[np++] = (off); \
    } while (0)

    while (vis_pair(clauses)) {
        val_t clause = vcar(clauses);
        val_t test   = vcar(clause);
        val_t exprs  = vcdr(clause);
        clauses      = vcdr(clauses);
        /* No `last`-gating left anywhere below: every non-else clause
         * (value-only, => arrow, and plain-body alike) now always jumps
         * past the trailing "no clause matched" OP_VOID fallback and
         * always compiles its body with the cond's own `tail` flag,
         * regardless of clause position -- see this loop's own
         * per-clause comments for why. */

        val_t S_ELSE = sym_intern_cstr("else");
        if (test == S_ELSE) {
            compile_seq(c, exprs, tail, line);
            goto cond_done;
        }

        compile(c, test, false, line);

        /* (cond (test) ...) — test is the value. Always DUP/jump, even
         * as the last clause -- gating this on `!last` (as this code
         * used to) left a matched last clause's single test value on
         * the stack with nothing to jump past the trailing "no clause
         * matched" OP_VOID fallback below, which then unconditionally
         * pushed a second value on top of it, corrupting the stack for
         * the caller (confirmed: `(cond (5))` raised a spurious "not a
         * procedure" error). Same bug class, and same fix, as the
         * plain-body clause below and the => arrow clause just below
         * that. */
        if (vis_nil(exprs)) {
            emit(c, OP_DUP, line);
            int skip = emit_jump(c, OP_JUMP_TRUE, line);
            emit(c, OP_POP, line);
            COND_PUSH_PATCH(skip);
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
            /* Always jump past the trailing OP_VOID fallback, even as
             * the last clause -- same bug class as the plain-body and
             * test-only clauses above/below (confirmed: a matched,
             * textually-last `=> ` clause silently discarded its
             * result, falling through into the OP_POP meant only for
             * the #f path and then the fallback OP_VOID). */
            COND_PUSH_PATCH(emit_jump(c, OP_JUMP, line));
            patch_jump(c, skip);
            /* #f path: JUMP_FALSE already popped dup; original #f is on stack */
            emit(c, OP_POP, line);
            continue;
        }

        int skip = emit_jump(c, OP_JUMP_FALSE, line);
        /* JUMP_FALSE already popped the test — no OP_POP needed.
         * Compile with the cond's own `tail` flag, not `tail && last`:
         * exactly one clause's body ever executes per evaluation of the
         * whole cond, so EVERY clause's body is in the same tail
         * position the whole cond expression is, not just the textually
         * last one. Gating on `last` here meant a call in any non-last
         * clause's body never got OP_TAIL_CALL, silently growing the
         * stack on every iteration of a tail-recursive loop written as
         * `(let loop (...) (cond (test1 (loop ...)) (test2 (loop ...))
         * (else result)))` -- exactly the shape of a state-machine-style
         * loop with an early-exit clause, and exactly what (srfi s252
         * property-testing)'s %run-property-trials does (its recursive
         * call sits in a cond's *middle* clause), which is how this was
         * found: `(test-property ... 150000)` overflowed the VM's
         * 256-frame guard despite the recursive call already being
         * outside `guard`, single-call-site, and documented as tail-
         * position. The matching `=> ` clause branch just above already
         * gets this right (uses plain `tail`, not `tail && last`) --
         * this brings the plain-body clause in line with it. */
        compile_seq(c, exprs, tail, line);
        /* Always jump past the "no clause matched" OP_VOID fallback
         * below, even for the last clause — omitting it here (as this
         * code previously did for `last`) let a matched last clause's
         * value fall straight through into that OP_VOID, silently
         * discarding it and corrupting the stack for the caller (e.g.
         * `(cond (#f 1) (#t 2))` returned void instead of 2, and in a
         * call-argument position desynced the stack enough to raise a
         * spurious "not a procedure" error). Dead code when the clause
         * body ends in a tail call (control never reaches it), harmless
         * either way. */
        COND_PUSH_PATCH(emit_jump(c, OP_JUMP, line));
        patch_jump(c, skip);
    }
    emit(c, OP_VOID, line);   /* no clause matched */

cond_done:
    for (int i = 0; i < np; i++) patch_jump(c, end_patches[i]);
}
#undef COND_PUSH_PATCH

static void compile_case(Compiler *c, val_t args, bool tail, int line) {
    /* Desugar: (case key clause...) →
     *   (let ((%%k key)) (cond (clause') ...))
     * where non-else clause ((d...) body...) → ((memv %%k '(d...)) body...)
     * and   arrow clause ((d...) => proc)    → ((memv %%k '(d...)) (proc %%k)) */
    require_min_args(args, 1, "case");
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
    require_min_args(args, 1, "when");
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

/* (delay expr...) / (delay-force expr...): compile the body as an
 * ordinary zero-arg thunk (reusing compile_lambda -- no new closure-
 * construction codegen needed), then call %delay-promise/
 * %delay-force-promise (builtins.c) to wrap it into a lazy Promise.
 * Was previously handled only by eval.c's tree-walker (S_DELAY/
 * S_DELAY_FORCE) with no compiler equivalent at all -- (delay ...) at
 * top level or anywhere else compiled raised unbound-variable, since
 * the compiler had never heard of it. Found while checking compiler/
 * tree-walker parity before switching define-library bodies to
 * compiled execution (they'd previously only worked because that one
 * path happened to still be tree-walked). */
static void compile_delay(Compiler *c, val_t body, bool is_force, bool tail, int line) {
    val_t prim_sym = sym_intern_cstr(is_force ? "%delay-force-promise" : "%delay-promise");
    emit_ab(c, OP_LOAD_GLOBAL, (uint8_t)chunk_add_const(c->chunk, prim_sym), line);
    compile_lambda(c, V_NIL, body, NULL, line);
    emit_ab(c, tail ? OP_TAIL_CALL : OP_CALL, 1, line);
}

static void compile_unless(Compiler *c, val_t args, bool tail, int line) {
    require_min_args(args, 1, "unless");
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
    require_min_args(args, 2, "do");
    val_t var_specs = vcar(args);
    val_t term      = vcar(vcdr(args));
    val_t body      = vcdr(vcdr(args));
    require_min_args(term, 1, "do");  /* do's own (test expr...) clause */
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

    /* Tier 2.4 fix (found by independent code review; same bug class,
     * same fix, as SF_VALUES/SF_APPLY/SF_CALL_WITH_VALUES's own "Tier 2.4
     * fix" comments above): this loop predates reserve_pending_slot and
     * was never updated for it. A step expression that is itself a
     * let / let* / letrec / letrec* / named-let now splices real locals
     * directly into `d` -- without this bracketing, a LATER step's own
     * splice computes its new locals' slot indices against a
     * d->local_count that doesn't account for the EARLIER steps' own
     * already-pushed, still-pending values, aliasing physical stack
     * positions those earlier steps are still using.
     *
     * `base` itself is unrelated to this bracketing: it identifies the nv
     * PERSISTENT loop-variable slots (added via add_local during the init
     * phase above, at the very top of this function), which is exactly
     * `saved - nv` -- `saved` (== d->local_count right before this loop)
     * only equals `d->local_count - nv` measured AFTER the loop when
     * nothing in between changes local_count, which no longer holds once
     * a step can splice new locals of its own; capturing `saved` before
     * this loop and subtracting nv from IT is what stays correct in both
     * cases. Confirmed as a real, silent wrong-value bug: `(do ((i 0 (+ i
     * 1)) (acc 0 (+ acc (let ((x 1)) x)))) ((= i 3) acc))` returned 0
     * instead of 3 before this fix. */
    int saved = d->local_count;
    vs = var_specs;
    while (vis_pair(vs)) {
        val_t spec = vcar(vs);
        val_t step = vis_pair(vcdr(vcdr(spec))) ? vcar(vcdr(vcdr(spec))) : vcar(spec);
        compile(d, step, false, line);
        reserve_pending_slot(d);
        vs = vcdr(vs);
    }
    int base = saved - nv;
    release_pending_slots(d, saved);
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
    require_min_args(args, 2, "with-exception-handler");
    val_t handler_expr = vcar(args);
    val_t thunk_expr   = vcar(vcdr(args));

    /* Tier 2.4 fix (same bug class, same fix, as SF_VALUES/SF_APPLY/
     * SF_CALL_WITH_VALUES/compile_do's own "Tier 2.4 fix" comments
     * elsewhere in this file): handler_expr's own fresh result is a
     * still-pending value on the physical stack while thunk_expr
     * compiles below -- if thunk_expr is itself (or evaluates via) a
     * let / let* / letrec / letrec* / named-let, its own splice must not treat
     * `c->local_count` as if it already accounted for handler's value,
     * or its own new locals alias handler's physical stack slot.
     * Confirmed as a real bug: `(with-exception-handler (lambda (e)
     * 'handled) (let ((f (lambda () 42))) f))` returned 'handled instead
     * of calling the thunk and returning 42. */
    int saved = c->local_count;

    /* Compile handler (stays below the protected call on the stack) */
    compile(c, handler_expr, false, line);
    reserve_pending_slot(c);

    /* OP_PUSH_HANDLER: saves sp at this point (past handler, before thunk) */
    int catch_placeholder = emit_jump(c, OP_PUSH_HANDLER, line);

    /* Compile thunk and call it with no arguments */
    compile(c, thunk_expr, false, line);
    reserve_pending_slot(c);
    release_pending_slots(c, saved);
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
    require_min_args(args, 1, "guard");
    val_t var_and_clauses = vcar(args);
    val_t body            = vcdr(args);
    require_min_args(var_and_clauses, 1, "guard");  /* guard's own (var clause...) */
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
    require_min_args(args, 1, "parameterize");
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
        require_min_args(binding, 2, "parameterize");  /* one (param val) pair */
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
 * Compares via lang_translate rather than a raw S_QUOTE check, so an
 * Akkadian/cuneiform spelling of quote (e.g. kīma) is recognized too —
 * found by review: without this, (define-algebra (kīma myop) ...) missed
 * the compile-time-literal fast path and fell back to the tree-eval path,
 * reproducing the global-leak bug this function exists to fix for that
 * one spelling. */
static bool is_quoted_symbol(val_t expr, val_t *out_sym) {
    if (vis_pair(expr) && lang_translate(vcar(expr)) == S_QUOTE &&
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

/* Matches compile()'s own "#:keyword symbols are self-evaluating" check
 * (below) so compile_call's fused-global-call fast path can defer to it:
 * a #:keyword head must still compile to "push the symbol itself as a
 * constant, then try to call it" (raising not-a-procedure at runtime),
 * not "look it up as a global variable" (raising unbound-variable) --
 * the fast path bypasses compile()'s general symbol handling entirely,
 * so without this guard it silently changed the error a caller sees for
 * `(#:foo 1 2)` from one condition type to another (found by code
 * review). */
static bool is_keyword_symbol(val_t v) {
    Symbol *s = as_sym(v);
    return s->len >= 2 && s->data[0] == '#' && s->data[1] == ':';
}

static void compile_call(Compiler *c, val_t head, val_t args, bool tail, int line) {
    /* Count args */
    int argc = 0;
    val_t a = args;
    while (vis_pair(a)) { argc++; a = vcdr(a); }

    /* Self-tail-call: a tail-position call whose head is exactly the
     * enclosing named-let loop's own self-reference (Compiler::
     * self_tail_name, set up by compile_let's named-let branch). Trusted
     * at compile time ONLY because that reference is a private upvalue
     * slot no code outside this one lambda body can ever reach, and the
     * set!-scan (body_mentions_set_target, run once up front over the
     * whole raw body) already ruled out this body ever mutating it.
     * resolve_local guards against an inner local of the same name
     * shadowing the loop variable at THIS call site specifically (e.g. a
     * nested (let ((loop 5)) (loop)) inside the loop body must resolve to
     * the LOCAL 5, not the outer loop closure). */
    if (tail && vis_symbol(head) && c->self_tail_name != V_FALSE &&
        head == c->self_tail_name && !c->self_tail_mutated &&
        resolve_local(c, head) < 0) {
        /* Still register the self-reference as a captured upvalue (for
         * its SIDE EFFECT, discarding the returned index -- the bytecode
         * itself never loads it via OP_LOAD_UP, since OP_SELF_TAIL_CALL
         * needs no callee on the stack at all) rather than skipping this
         * call entirely. Without this, the closure's own upval_count/
         * upvals[] never gets the self-capture entry that maybe_jit_bcc
         * (src/runtime.c) depends on to detect "this is a self-
         * referencing named-let loop, never promote it to native code" --
         * omitting it let a hot loop cross the JIT threshold and hit a
         * real, previously-dormant JIT codegen bug for self-referencing
         * closures the very guard this restores exists to prevent
         * (confirmed by reproducing: an identical loop failed with
         * "unbound variable: loop" past ~50 iterations before this fix,
         * worked correctly after). */
        resolve_upvalue(c, head);
        /* Tier 2.3: reserve/release brackets this loop -- see
         * reserve_pending_slot's own comment (compiler.c) for why: an
         * argument compiled here can recurse back into ir_emit's IR_CALL
         * inline branch (via compile()'s own IR_OR_CLASSIC), which must
         * not alias an earlier, still-pending sibling argument's stack
         * slot. */
        {
            int saved = c->local_count;
            a = args;
            while (vis_pair(a)) {
                compile(c, vcar(a), false, line);
                reserve_pending_slot(c);
                a = vcdr(a);
            }
            release_pending_slots(c, saved);
        }
        emit_ab(c, OP_SELF_TAIL_CALL, (uint8_t)argc, line);
        return;
    }

    /* Fused global call: a call whose head is a symbol resolving to
     * neither a local nor an upvalue (the same condition emit_load uses
     * to fall through to OP_LOAD_GLOBAL) -- look the global up and call
     * it in one dispatch instead of two. Every non-BcClosure callee type
     * still goes through call_foreign inside the opcode handler itself,
     * exactly as OP_CALL/OP_TAIL_CALL already do -- see the vm.c handler's
     * own comment for why this is safe for every callee type curry has
     * (primitives, continuations, parameter objects, FFI functions), not
     * just closures -- the specific failure Kaappi's own postmortem
     * documented for a naively-fused call opcode that only dispatched two
     * of the possible callee types directly. resolve_upvalue has the
     * side effect of marking an enclosing local captured/registering the
     * upvalue slot if `head` turns out to resolve as one -- unavoidable
     * and harmless here: that bookkeeping is required regardless of
     * which call opcode ultimately gets emitted. */
    if (vis_symbol(head) && !is_keyword_symbol(head) &&
        resolve_local(c, head) < 0 && resolve_upvalue(c, head) < 0) {
        int ci = chunk_add_const(c->chunk, head);
        /* Tier 2.3: see the self-tail-call branch's own comment above. */
        {
            int saved = c->local_count;
            a = args;
            while (vis_pair(a)) {
                compile(c, vcar(a), false, line);
                reserve_pending_slot(c);
                a = vcdr(a);
            }
            release_pending_slots(c, saved);
        }
        emit_abc(c, tail ? OP_TAIL_CALL_GLOBAL : OP_CALL_GLOBAL,
                 (uint8_t)ci, (uint8_t)argc, line);
        return;
    }

    /* Tier 2.3: the callee itself is ALSO a pending value while args are
     * compiled below -- reserve it too, not just the args. */
    {
        int saved = c->local_count;
        compile(c, head, false, line);
        reserve_pending_slot(c);
        a = args;
        while (vis_pair(a)) {
            compile(c, vcar(a), false, line);
            reserve_pending_slot(c);
            a = vcdr(a);
        }
        release_pending_slots(c, saved);
    }

    emit_ab(c, tail ? OP_TAIL_CALL : OP_CALL, (uint8_t)argc, line);
}

/* ── Main dispatch ───────────────────────────────────────────────────── */

/* Every compound-form classification compile()'s own dispatch below can
 * reach, in the same order compile() tests for them. Shared with
 * ir_lower (via classify_head, below) so the Tier 2.1 IR's own "is this
 * actually a plain call?" decision for IR_CALL can never drift out of
 * sync with what compile() itself would do -- a hand-duplicated second
 * copy of this same ~35-symbol chain was rejected as exactly the
 * two-lists-that-must-stay-in-sync hazard this project's own IR_SEQ/
 * IR_VAR_REF postmortems (see ir.h) warn about. */
typedef enum {
    SF_NONE = 0,    /* none of the below matched -- an ordinary call */
    SF_QUOTE, SF_QUASIQUOTE, SF_IF, SF_BEGIN, SF_COND_EXPAND, SF_DEFINE,
    SF_DEFINE_VALUES, SF_DEFINED_P, SF_SET, SF_LAMBDA, SF_LET, SF_LET_STAR,
    SF_LETREC, SF_LET_VALUES, SF_LET_STAR_VALUES, SF_AND, SF_OR, SF_COND,
    SF_CASE, SF_WHEN, SF_UNLESS, SF_DELAY, SF_DELAY_FORCE, SF_DO,
    SF_VALUES, SF_APPLY, SF_CALL_WITH_VALUES, SF_PARAMETERIZE, SF_GUARD,
    SF_WITH_EXCEPTION_HANDLER, SF_RECEIVE, SF_DEFINE_RECORD_TYPE,
    SF_DEFINE_SYNTAX, SF_LET_SYNTAX, SF_SYMBOLIC, SF_WITH_ASSUMPTIONS,
    SF_DEFINE_RULE, SF_DEFINE_RULESET, SF_DEFINE_ALGEBRA, SF_TREE_EVAL,
    SF_MACRO,
} SpecialForm;

/* Classification only -- never executes a handler, never mutates `c`
 * except via the macro-lookup path's own resolve_syntax_local/env_lookup
 * calls, which are read-only queries (unlike resolve_local/
 * resolve_upvalue, they register nothing). Sets *transformer_out only
 * when returning SF_MACRO. Conditions and their order are copied
 * verbatim from compile()'s own dispatch chain below -- keep the two in
 * sync by construction: compile() switches on this function's result
 * rather than re-testing `head` itself. */
static SpecialForm classify_head(Compiler *c, val_t head, val_t args,
                                  val_t *transformer_out) {
    if (head == S_QUOTE) return SF_QUOTE;
    if (head == S_QUASIQUOTE) return SF_QUASIQUOTE;
    if (head == S_IF) return SF_IF;
    if (head == S_BEGIN) return SF_BEGIN;
    if (head == S_COND_EXPAND) return SF_COND_EXPAND;
    if (head == S_DEFINE) return SF_DEFINE;
    if (head == S_DEFINE_VALUES) return SF_DEFINE_VALUES;
    if (head == S_DEFINED_P) return SF_DEFINED_P;
    if (head == S_SET) return SF_SET;
    if (head == S_LAMBDA) return SF_LAMBDA;
    if (head == S_LET) return SF_LET;
    if (head == S_LET_STAR) return SF_LET_STAR;
    if (head == S_LETREC || head == S_LETREC_STAR) return SF_LETREC;
    if (head == S_LET_VALUES) return SF_LET_VALUES;
    if (head == S_LET_STAR_VALUES) return SF_LET_STAR_VALUES;
    if (head == S_AND) return SF_AND;
    if (head == S_OR) return SF_OR;
    if (head == S_COND) return SF_COND;
    if (head == S_CASE) return SF_CASE;
    if (head == S_WHEN) return SF_WHEN;
    if (head == S_UNLESS) return SF_UNLESS;
    if (head == S_DELAY) return SF_DELAY;
    if (head == S_DELAY_FORCE) return SF_DELAY_FORCE;
    if (head == S_DO) return SF_DO;
    if (head == S_VALUES) return SF_VALUES;
    if (head == S_APPLY) return SF_APPLY;
    if (head == S_CALL_WITH_VALUES && vis_pair(args) &&
        vis_pair(vcdr(args)) && vis_nil(vcdr(vcdr(args))))
        return SF_CALL_WITH_VALUES;
    if (head == S_PARAMETERIZE) return SF_PARAMETERIZE;
    if (head == S_GUARD) return SF_GUARD;
    if (head == S_WITH_EXCEPTION_HANDLER) return SF_WITH_EXCEPTION_HANDLER;
    if (head == S_RECEIVE && vis_pair(args) && vis_pair(vcdr(args)))
        return SF_RECEIVE;
    if (head == S_DEFINE_RECORD_TYPE) return SF_DEFINE_RECORD_TYPE;
    if (head == S_DEFINE_SYNTAX) return SF_DEFINE_SYNTAX;
    if (head == S_LET_SYNTAX || head == S_LETREC_SYNTAX) return SF_LET_SYNTAX;
    if (head == S_SYMBOLIC) return SF_SYMBOLIC;
    if (head == S_WITH_ASSUMPTIONS) return SF_WITH_ASSUMPTIONS;
    if (head == S_DEFINE_RULE) return SF_DEFINE_RULE;
    if (head == S_DEFINE_RULESET) return SF_DEFINE_RULESET;
    if (head == S_DEFINE_ALGEBRA) return SF_DEFINE_ALGEBRA;
    if (head == S_IMPORT || head == S_DEFINE_LIBRARY || head == S_LIBRARY)
        return SF_TREE_EVAL;
    if (vis_symbol(head)) {
        val_t transformer;
        bool is_macro = resolve_syntax_local(c, head, &transformer);
        if (!is_macro && c->chunk->target_env != V_VOID) {
            val_t macro = env_lookup_or_false(c->chunk->target_env, head);
            is_macro = vis_syntax(macro);
            if (is_macro) transformer = as_syntax(macro)->transformer;
        }
        if (!is_macro) {
            val_t macro = env_lookup_or_false(GLOBAL_ENV, head);
            is_macro = vis_syntax(macro);
            if (is_macro) transformer = as_syntax(macro)->transformer;
        }
        if (is_macro) { *transformer_out = transformer; return SF_MACRO; }
    }
    return SF_NONE;
}

/* Tries the Tier 2.1/2.2 IR pipeline (ir_lower -> ir_optimize -> ir_emit)
 * for the current `expr`/`tail`/`line` (all in scope wherever this is
 * used, inside compile()'s own switch below); if ir_lower can't actually
 * lower this specific case (falls back to IR_FALLBACK -- only possible
 * for SF_DEFINE today, whose malformed-target shape compile_define
 * itself handles gracefully rather than crashing, so ir_lower can't
 * safely claim unconditional coverage the way SF_IF/SF_LAMBDA/etc. do;
 * see ir_lower's own S_LAMBDA/S_LET comments), falls back to
 * `classic_stmt` instead of calling ir_emit -- calling ir_emit on an
 * IR_FALLBACK node would call compile() again on this SAME expr, which
 * re-classifies to this SAME case and re-enters this SAME macro: an
 * infinite loop on malformed input (found during design review, before
 * compile() ever called ir_lower live -- no existing test exercised
 * malformed input through the live dispatch to catch it). Applied
 * uniformly to every case below, including ones where ir_lower is
 * already known to have unconditional coverage (SF_IF, SF_SET, SF_AND,
 * SF_OR, SF_LAMBDA, SF_LET, SF_LET_STAR, SF_LETREC, and the SF_NONE/
 * ordinary-call fallthrough) -- redundant but cheap for those, and
 * removes any need to keep a second, driftable list of "which cases are
 * actually safe" in sync with ir_lower's own guards.
 *
 * Also checks g_force_classic_compile first -- see its own comment --
 * so compile_classic can force a genuinely IR-free compile of the WHOLE
 * recursive tree for compiler_ir_self_check/compiler_ir_optimize_check's
 * "old" side, which needs one now that this macro means compile() no
 * longer IS classic-only by default. */
#define IR_OR_CLASSIC(classic_stmt) do {                              \
        if (g_force_classic_compile) { classic_stmt; return; }        \
        IRNode *_ir = ir_lower(c, expr, tail, line);                  \
        if (_ir->kind == IR_FALLBACK) { classic_stmt; return; }       \
        ir_emit(c, ir_optimize(_ir));                                 \
        return;                                                       \
    } while (0)

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
    val_t head = lang_translate(vcar(expr));
    val_t args = vcdr(expr);

    val_t transformer = V_FALSE;
    switch (classify_head(c, head, args, &transformer)) {

    /* quote */
    case SF_QUOTE:
        emit_const(c, vis_pair(args) ? vcar(args) : V_NIL, line);
        return;

    /* quasiquote — expand to list-construction code, then compile that */
    case SF_QUASIQUOTE: {
        val_t expanded = expand_qq(vis_pair(args) ? vcar(args) : V_NIL, GLOBAL_ENV, 0);
        compile(c, expanded, tail, line);
        return;
    }

    /* if -- routed through the Tier 2.1 IR (see IR_OR_CLASSIC's comment) */
    case SF_IF: IR_OR_CLASSIC(compile_if(c, args, tail, line));

    /* begin */
    case SF_BEGIN: IR_OR_CLASSIC(compile_begin(c, args, tail, line));

    /* cond-expand — resolved entirely at compile time: pick the first
     * satisfied clause's body (see features.c) and compile it in place,
     * exactly as if it had been written as (begin body...) here. */
    case SF_COND_EXPAND: {
        bool matched;
        val_t body = cond_expand_choose(args, &matched);
        if (!matched) scm_raise(V_FALSE, "cond-expand: no matching clause");
        compile_begin(c, body, tail, line);
        return;
    }

    /* define -- the one case where IR_OR_CLASSIC's fallback path is
     * actually reachable (a malformed target); see its own comment. */
    case SF_DEFINE: IR_OR_CLASSIC(compile_define(c, args, line));

    /* define-values */
    case SF_DEFINE_VALUES: compile_define_values(c, args, line); return;

    /* defined? */
    case SF_DEFINED_P: compile_defined_p(c, args, line); return;

    /* set! */
    case SF_SET: IR_OR_CLASSIC(compile_set(c, args, line));

    /* lambda */
    case SF_LAMBDA: IR_OR_CLASSIC({
        require_min_args(args, 1, "lambda");
        val_t params = vcar(args);
        val_t body   = vcdr(args);
        compile_lambda(c, params, body, NULL, line);
    });

    /* let */
    case SF_LET:      IR_OR_CLASSIC(compile_let(c, args, tail, line));
    case SF_LET_STAR: IR_OR_CLASSIC(compile_let_star(c, args, tail, line));
    case SF_LETREC:   IR_OR_CLASSIC(compile_letrec(c, args, tail, line));
    case SF_LET_VALUES:      compile_let_values(c, args, tail, line);      return;
    case SF_LET_STAR_VALUES: compile_let_star_values(c, args, tail, line); return;

    /* and / or */
    case SF_AND: IR_OR_CLASSIC(compile_and(c, args, tail, line));
    case SF_OR:  IR_OR_CLASSIC(compile_or(c, args, tail, line));

    /* cond / case */
    case SF_COND: compile_cond(c, args, tail, line); return;
    case SF_CASE: compile_case(c, args, tail, line); return;

    /* when / unless */
    case SF_WHEN:   compile_when(c, args, tail, line);   return;
    case SF_UNLESS: compile_unless(c, args, tail, line); return;

    /* delay / delay-force */
    case SF_DELAY:       compile_delay(c, args, false, tail, line); return;
    case SF_DELAY_FORCE: compile_delay(c, args, true,  tail, line); return;

    /* do */
    case SF_DO: compile_do(c, args, tail, line); return;

    /* values */
    case SF_VALUES: {
        /* Tier 2.4 fix (found via a real, confirmed miscompilation:
         * `(values 1 (let ((x 5)) x) 2)` returned (1 1 2) instead of
         * (1 5 2)): this loop predates Tier 2.3's reserve_pending_slot
         * convention and was never updated for it -- compile_call's own
         * identical argument loop already has this bracketing (see its
         * own "Tier 2.3" comment), but SF_VALUES/SF_APPLY/
         * SF_CALL_WITH_VALUES below are classic special forms with no IR-
         * native form of their own, so they never got the same pass. Any
         * one of these several already-pushed-but-untracked pending
         * values (e.g. `1` above) is exactly the "still-pending sibling
         * argument" reserve_pending_slot's own comment warns about: a
         * LATER item that happens to be a let / let* / letrec / letrec* /
         * named-let now splices real locals directly into `c` (Tier 2.4), and
         * without this bracketing it computes its own new slot indices
         * against a `c->local_count` that doesn't yet account for the
         * earlier items' own already-pushed values -- aliasing physical
         * stack positions that are still very much in use. */
        int n = 0;
        val_t a = args;
        int saved = c->local_count;
        while (vis_pair(a)) {
            compile(c, vcar(a), false, line);
            reserve_pending_slot(c);
            n++; a = vcdr(a);
        }
        release_pending_slots(c, saved);
        emit_ab(c, OP_VALUES, (uint8_t)n, line);
        return;
    }

    /* apply — (apply f arg1 ... rest-list) */
    case SF_APPLY: {
        /* Tier 2.4 fix -- see SF_VALUES's own comment just above for the
         * full rationale; same bug, same fix, this is the actual site
         * where it was originally found: `(apply + (list 1 (let ((x 5))
         * x) 2))` returned 4 instead of 8. */
        int n = 0;
        val_t a = args;
        int saved = c->local_count;
        while (vis_pair(a)) {
            compile(c, vcar(a), false, line);
            reserve_pending_slot(c);
            n++; a = vcdr(a);
        }
        release_pending_slots(c, saved);
        emit_ab(c, OP_APPLY, (uint8_t)n, line); /* n = fn + intermediates + last-list */
        return;
    }

    /* call-with-values — (call-with-values producer consumer), literal
     * 2-argument syntactic position only (same unconditional special-
     * casing convention as `apply`/`values` above, not checking for local
     * shadowing of the name; still callable indirectly as an ordinary
     * value through a rebound identifier, just without this fast path).
     * OP_TAIL_CALL_WITH_VALUES vs OP_CALL_WITH_VALUES exists for exactly
     * the same reason OP_TAIL_CALL vs OP_CALL does: prim_call_with_values
     * (builtins.c) invokes its consumer via a real nested C call, so a
     * receive/let-values/let*-values/with-values in tail position of a
     * self-recursive loop would otherwise accumulate one nested call
     * frame per iteration instead of looping forever. Shape already
     * verified by classify_head. */
    case SF_CALL_WITH_VALUES: {
        /* Tier 2.4 fix -- see SF_VALUES's own comment above for the full
         * rationale; same bug (the producer thunk is a still-pending
         * value while the consumer is compiled). */
        int saved = c->local_count;
        compile(c, vcar(args), false, line);
        reserve_pending_slot(c);
        compile(c, vcar(vcdr(args)), false, line);
        reserve_pending_slot(c);
        release_pending_slots(c, saved);
        emit(c, tail ? OP_TAIL_CALL_WITH_VALUES : OP_CALL_WITH_VALUES, line);
        return;
    }

    /* parameterize — desugar to let + dynamic-wind in the compiler so that
       local variables in the body are captured as upvalues, not looked up
       in GLOBAL_ENV (which would fail for BcClosure-local bindings). */
    case SF_PARAMETERIZE:
        compile_parameterize(c, args, tail, line);
        return;

    case SF_GUARD:
        compile_guard(c, args, tail, line);
        return;

    case SF_WITH_EXCEPTION_HANDLER:
        compile_with_exception_handler(c, args, tail, line);
        return;

    /* `receive` is ambiguous: it's both the R7RS special form (requires at
     * least 2 forms after it: formals and a producer expression, body...
     * optional) and — pre-existing in this codebase — the actor-mailbox
     * primitive (arity 0-1: optional timeout). The two are unambiguous by
     * argument count alone, so only treat it as the special form when the
     * shape can't be the primitive (already verified by classify_head);
     * otherwise it classifies as SF_NONE, falling through to an ordinary
     * call so (receive) and (receive timeout) keep working. */
    case SF_RECEIVE:
        compile_receive(c, args, tail, line);
        return;

    case SF_DEFINE_RECORD_TYPE:
        compile_define_record_type(c, args, line);
        return;

    case SF_DEFINE_SYNTAX:
        compile_define_syntax(c, args, line);
        return;

    case SF_LET_SYNTAX:
        compile_let_syntax(c, args, tail, line);
        return;

    case SF_SYMBOLIC:
        compile_symbolic(c, args, line);
        return;

    case SF_WITH_ASSUMPTIONS:
        compile_with_assumptions(c, args, tail, line);
        return;

    case SF_DEFINE_RULE:
        compile_define_rule(c, args, tail, line);
        return;

    case SF_DEFINE_RULESET:
        compile_define_ruleset(c, args, tail, line);
        return;

    case SF_DEFINE_ALGEBRA:
        compile_define_algebra(c, args, tail, line);
        return;

    /* Note: syntax-rules itself is deliberately NOT special-cased here.
     * syntax_rules_register() binds the symbol `syntax-rules` to a T_SYNTAX
     * in GLOBAL_ENV whose transformer (sr_compile_fn) takes the raw
     * (syntax-rules literals rule...) form and returns a self-evaluating
     * T_PRIMITIVE transformer — so a (syntax-rules ...) expression, e.g. as
     * define-syntax's transformer-expr, already gets handled by the
     * ordinary "macro expansion" case below like any other macro use,
     * with no eval()/tree-eval dependency at all. */

    /* Forms the compiler delegates to the tree-walker at runtime:
       import, define-library, library.
       Emitted as OP_TREE_EVAL_CACHED, not a (tree-eval '<form>) call --
       see chunk.h's Chunk::tree_eval_cache and opcode.h's own comment.
       Memoizes per call SITE (this exact bytecode instruction, keyed by
       constant-pool index), not globally: re-executing the SAME chunk
       (e.g. an import nested inside a function called from a loop) only
       actually runs the form through eval() once. No tail/non-tail
       distinction needed -- these forms are side-effecting declarations
       that always return normally, never altering control flow. */
    case SF_TREE_EVAL: {
        int ci = chunk_add_const(c->chunk, expr);
        emit_ab(c, OP_TREE_EVAL_CACHED, (uint8_t)ci, line);
        return;
    }

    /* Macro expansion: head is a symbol bound to a macro (classify_head
       already resolved which one -- local macro via c->syntax_locals,
       this chunk's own target_env, or GLOBAL_ENV, in that shadowing
       order, with `transformer` set accordingly); expand and recompile.
       Expansion errors are deferred to runtime via (raise ...) so that
       guard forms inside lambdas can catch zero-clause or no-match errors. */
    case SF_MACRO: {
        ExnHandler h;
        val_t expanded = V_FALSE;
        val_t exn_val  = V_FALSE;
        uint64_t _expand_t0 = curry_timings_enabled ? profiling_now_ns() : 0;
        /* sr_current_env (syntax_rules.c) is how sr_compile_fn learns
         * a (syntax-rules ...) expression's defining environment, to
         * capture as the resulting transformer's def_env -- eval.c's
         * tree-walker save/set/restores this around every macro
         * apply() (see its own comment at the analogous site), but
         * this compiler.c macro-expansion call had no equivalent,
         * leaving sr_current_env at its V_FALSE default (mapped to
         * GLOBAL_ENV by sr_compile_fn) for every macro compiled here
         * -- including a library's own (define-syntax ... (syntax-
         * rules ...)) compiled against its target_env (chunk.h). That
         * made sr_is_protected's def_env check miss any library-local
         * helper procedure/macro a template referenced (only visible
         * via target_env, never GLOBAL_ENV), incorrectly renaming it
         * to a fresh gensym and breaking self-recursive macros that
         * call a sibling from their own library body (found via
         * (curry schematic extract)'s %match-case calling %match).
         * Save/restore, not a bare set, so a nested macro expansion
         * triggered from within this apply() doesn't leak this env
         * into an unrelated later expansion. */
        val_t saved_sr_env = sr_get_current_env();
        sr_set_current_env(c->chunk->target_env != V_VOID ? c->chunk->target_env : GLOBAL_ENV);
        SCM_PROTECT(h,
            expanded = apply(transformer, scm_cons(expr, V_NIL)),
            exn_val  = h.exn);
        sr_set_current_env(saved_sr_env);
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

    case SF_NONE:
        break;
    }

    /* Fallthrough: function call -- routed through the IR too. */
    IR_OR_CLASSIC(compile_call(c, head, args, tail, line));
}

/* Compiles `expr` via compile()'s ORIGINAL, pre-IR dispatch -- guaranteed
 * to never touch ir_lower/ir_emit anywhere in the resulting tree, however
 * deep. Needed because compile_if/compile_let/etc. call compile() itself
 * (not a pluggable alternative) for their own subexpressions -- once
 * compile()'s own dispatch routes through the IR (IR_OR_CLASSIC, above),
 * merely calling a classic per-form function once at the top no longer
 * guarantees the WHOLE recursive tree stays IR-free, since that
 * function's own nested compile() calls would immediately route back
 * through the IR again. Setting g_force_classic_compile before calling
 * compile() sidesteps that: it's a thread-local IR_OR_CLASSIC itself
 * checks, so it stays in effect for every nested compile() call in the
 * same dynamic extent, not just this one.
 *
 * This exists for exactly one reason: compiler_ir_self_check and
 * compiler_ir_optimize_check need a genuine, independent classic
 * implementation to compare the IR pipeline against. Before compile()
 * routed through the IR, compile() itself WAS that independent
 * implementation; now it isn't, so this recovers it. Without this,
 * "old_c" and "new_c" in both those functions would silently collapse
 * into the exact same code path, and their comparison would trivially
 * pass regardless of any future bug in ir_lower/ir_emit -- a tautology,
 * not a test (found during design review, before compile() ever called
 * ir_lower live).
 *
 * SCM_PROTECT'd so a raise mid-compile still restores the flag -- a
 * naive save/set/compile/restore would leave it stuck at `true` for the
 * rest of the process on any compile-time error, silently disabling the
 * IR pipeline for every later compile on this thread. */
static void compile_classic(Compiler *c, val_t expr, bool tail, int line) {
    bool saved = g_force_classic_compile;
    g_force_classic_compile = true;

    ExnHandler h;
    bool raised = false;
    SCM_PROTECT(h, {
        compile(c, expr, tail, line);
    }, {
        raised = true;
    });

    g_force_classic_compile = saved;
    if (raised) scm_raise_val(h.exn);
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

/* ── Tier 2.1 IR: lowering + codegen (src/ir.h) ──────────────────────────
 *
 * ir_lower walks the same raw S-expressions compile() does, natively
 * recognizing: self-evaluating literals, #:keyword symbols, ordinary
 * variable references (local/upvalue/global -- resolution decided at
 * ir_emit time, exactly as emit_load does), quote, if, begin, set!, and,
 * or, `(define sym expr)` and `(define (f params...) body...)`
 * lambda-sugar, ordinary/fused-global/self-tail calls, lambda/closures,
 * and let / let-star / letrec / letrec-star / named-let. Everything else
 * compile()
 * would otherwise handle further down its dispatch chain (cond, case,
 * define-record-type, define-syntax, the CAS forms, import/
 * define-library/library, macro uses, ...) is wrapped whole as an
 * IR_FALLBACK leaf, whose ir_emit case is simply `compile(c, expr, tail,
 * line)`: byte-for-byte whatever would have happened had ir_lower never
 * run for that subform.
 *
 * ir_lower is guaranteed to never return NULL: every input either matches
 * one of the native cases above or becomes an IR_FALLBACK leaf.
 *
 * As of the seventh landing, ir_lower/ir_emit/ir_optimize are ACTUALLY
 * LIVE: compile()'s own dispatch (via the IR_OR_CLASSIC macro, defined
 * just above compile() itself) routes every case this section covers
 * through them, for every Scheme program curry compiles -- the REPL,
 * -e, script files, everything. They are no longer test-only; see
 * IR_OR_CLASSIC's own comment for the malformed-input infinite-
 * recursion hazard that had to be closed before this was safe, and
 * compile_classic's comment for why compiler_ir_self_check/
 * compiler_ir_optimize_check needed a new, genuinely IR-free reference
 * implementation once compile() itself stopped being one. */

static IRNode *ir_lower(Compiler *c, val_t expr, bool tail, int line);
static void    ir_emit(Compiler *c, IRNode *n);

static IRNode *ir_lower_if(Compiler *c, val_t args, bool tail, int line) {
    require_min_args(args, 2, "if");
    val_t test = vcar(args);  args = vcdr(args);
    val_t then = vcar(args);  args = vcdr(args);
    val_t els  = vis_pair(args) ? vcar(args) : V_VOID;

    IRNode *n = ir_node_new(c->ir_arena, IR_IF, tail, line);
    n->as.iff.test = ir_lower(c, test, false, line);
    n->as.iff.then = ir_lower(c, then, tail, line);
    n->as.iff.els  = ir_lower(c, els,  tail, line);
    return n;
}

/* Mirrors compile_seq's exact line-tracking (a mutable `line` threaded
 * through the loop, updated from each cons cell's own hdr.flags when
 * present) so IR_SEQ's pop_lines[] carries the same per-item line
 * compile_seq would have used for that item's OP_POP -- see IR_SEQ's own
 * field comment in ir.h for why this must be tracked SEPARATELY from
 * items[i]->line (a real, differential-self-check-invisible bug, found
 * by independent code review, when the two were conflated). */
static IRNode *ir_lower_seq(Compiler *c, val_t list, bool tail, int line) {
    if (vis_nil(list)) {
        IRNode *n = ir_node_new(c->ir_arena, IR_CONST, tail, line);
        n->as.konst.value = V_VOID;
        return n;
    }
    /* `body` is the raw list, stored unprocessed -- see ir.h's comment on
     * IRNode::as.seq for why per-item lowering has to wait for ir_emit's
     * own interleaved walk now, same reasoning as IR_LAMBDA's body. */
    IRNode *n = ir_node_new(c->ir_arena, IR_SEQ, tail, line);
    n->as.seq.body = list;
    return n;
}

/* Mirrors compile_set (compiler.c) exactly: value compiled non-tail,
 * name resolution deferred to ir_emit via emit_store (see IRNode::
 * as.set's comment in ir.h). */
static IRNode *ir_lower_set(Compiler *c, val_t args, int line) {
    require_min_args(args, 2, "set!");
    val_t name = vcar(args);
    val_t expr = vcar(vcdr(args));
    IRNode *n = ir_node_new(c->ir_arena, IR_SET, false, line);
    n->as.set.name  = name;
    n->as.set.value = ir_lower(c, expr, false, line);
    return n;
}

/* Mirrors compile_and exactly, including which per-item tail value each
 * item gets: only the last item inherits `tail`, matching `compile(c,
 * vcar(args), last && tail, line)`. */
static IRNode *ir_lower_and(Compiler *c, val_t args, bool tail, int line) {
    if (vis_nil(args)) {
        IRNode *n = ir_node_new(c->ir_arena, IR_AND, tail, line);
        n->as.andor.items = NULL;
        n->as.andor.count = 0;
        return n;
    }
    size_t count = 0;
    for (val_t p = args; vis_pair(p); p = vcdr(p)) count++;

    IRNode *n = ir_node_new(c->ir_arena, IR_AND, tail, line);
    n->as.andor.items = (IRNode **)ir_arena_alloc(c->ir_arena,
                                                   sizeof(IRNode *) * count);
    n->as.andor.count = (int)count;

    size_t i = 0;
    while (vis_pair(args)) {
        val_t next = vcdr(args);
        bool  last = vis_nil(next);
        n->as.andor.items[i++] = ir_lower(c, vcar(args), last && tail, line);
        args = next;
    }
    return n;
}

/* Mirrors compile_or exactly -- deliberately, NOT symmetric with
 * ir_lower_and: compile_or hardcodes `false` for every item's tail
 * argument, including the last, unlike compile_and. Preserved as-is,
 * see this landing's plan for why "fixing" this asymmetry is explicitly
 * out of scope here. */
static IRNode *ir_lower_or(Compiler *c, val_t args, bool tail, int line) {
    if (vis_nil(args)) {
        IRNode *n = ir_node_new(c->ir_arena, IR_OR, tail, line);
        n->as.andor.items = NULL;
        n->as.andor.count = 0;
        return n;
    }
    size_t count = 0;
    for (val_t p = args; vis_pair(p); p = vcdr(p)) count++;

    IRNode *n = ir_node_new(c->ir_arena, IR_OR, tail, line);
    n->as.andor.items = (IRNode **)ir_arena_alloc(c->ir_arena,
                                                   sizeof(IRNode *) * count);
    n->as.andor.count = (int)count;

    size_t i = 0;
    while (vis_pair(args)) {
        val_t next = vcdr(args);
        n->as.andor.items[i++] = ir_lower(c, vcar(args), false, line);
        args = next;
    }
    return n;
}

/* Mirrors compile_define's `(define sym expr)` case exactly. */
static IRNode *ir_lower_define(Compiler *c, val_t args, int line) {
    val_t name  = vcar(args);
    val_t rest  = vcdr(args);
    val_t value = vis_pair(rest) ? vcar(rest) : V_VOID;
    IRNode *n = ir_node_new(c->ir_arena, IR_DEFINE, false, line);
    n->as.def.name  = name;
    n->as.def.value = ir_lower(c, value, false, line);
    return n;
}

/* Mirrors compile_lambda's own signature. Deliberately does NOT lower the
 * body here -- see ir.h's comment on IRNode::as.lambda for why the body
 * has to wait for ir_emit to create the real child Compiler it belongs
 * to (a scope that doesn't exist, and can't be faked, until then). */
static IRNode *ir_lower_lambda(Compiler *c, val_t params, val_t body,
                                const char *name, int line) {
    IRNode *n = ir_node_new(c->ir_arena, IR_LAMBDA, false, line);
    n->as.lambda.params = params;
    n->as.lambda.body   = body;
    n->as.lambda.name   = name;
    return n;
}

/* Mirrors compile_define's `(define (f params...) body...)` lambda-sugar
 * case exactly (name = vcar(target), params = vcdr(target), rest = the
 * lambda body) -- builds IR_DEFINE{value = IR_LAMBDA} now that IR_LAMBDA
 * exists, instead of falling the whole form back to IR_FALLBACK the way
 * the third landing had to. */
static IRNode *ir_lower_define_lambda_sugar(Compiler *c, val_t args, int line) {
    val_t target = vcar(args);
    val_t rest   = vcdr(args);
    val_t name   = vcar(target);
    val_t params = vcdr(target);
    IRNode *n = ir_node_new(c->ir_arena, IR_DEFINE, false, line);
    n->as.def.name  = name;
    n->as.def.value = ir_lower_lambda(c, params, rest, as_sym(name)->data, line);
    return n;
}

/* Mirrors compile_call, but builds a single IR_CALL node rather than
 * deciding self-tail/fused-global/generic here -- see ir.h's comment on
 * IRNode::as.call for why that classification has to wait for ir_emit.
 * `callee` is lowered eagerly (cheap, side-effect-free, like any other
 * subtree); only used by ir_emit's generic-call fallback branch. */
static IRNode *ir_lower_call(Compiler *c, val_t head, val_t args, bool tail, int line) {
    int argc = 0;
    for (val_t a = args; vis_pair(a); a = vcdr(a)) argc++;

    IRNode *n = ir_node_new(c->ir_arena, IR_CALL, tail, line);
    n->as.call.head   = head;
    n->as.call.callee = ir_lower(c, head, false, line);
    n->as.call.argc   = argc;
    n->as.call.args   = argc > 0
        ? (IRNode **)ir_arena_alloc(c->ir_arena, sizeof(IRNode *) * (size_t)argc)
        : NULL;

    int i = 0;
    for (val_t a = args; vis_pair(a); a = vcdr(a))
        n->as.call.args[i++] = ir_lower(c, vcar(a), false, line);
    return n;
}

/* Builds an IR_CALL{callee=IR_LAMBDA{...}} node directly (not via
 * ir_lower_call, which expects a raw val_t callee to lower itself --
 * here the callee is ALREADY the IR_LAMBDA ir_lower_lambda just built,
 * no S-expression synthesis needed for it). `head` is V_FALSE: never a
 * symbol, so IR_CALL's own ir_emit classification always falls through
 * to its generic (non-self-tail, non-fused-global) branch -- exactly
 * matching compile_let/compile_let_star/compile_letrec, which always
 * emit a plain OP_CALL/OP_TAIL_CALL for these desugared forms, never
 * OP_CALL_GLOBAL. Shared by ir_lower_let/ir_lower_let_star/
 * ir_lower_letrec below. */
static IRNode *ir_lower_lambda_call(Compiler *c, IRNode *callee,
                                     IRNode **args, int argc,
                                     bool tail, int line) {
    IRNode *n = ir_node_new(c->ir_arena, IR_CALL, tail, line);
    n->as.call.head   = V_FALSE;
    n->as.call.callee = callee;
    n->as.call.argc   = argc;
    n->as.call.args   = args;
    return n;
}

/* Mirrors compile_let's own PLAIN (non-named) branch exactly: `(let
 * ((x v) ...) body...)` desugars to `((lambda (x ...) body...) v ...)`.
 * No new IR node kind needed -- IR_LAMBDA already handles everything a
 * let's body needs (its own child scope, internal defines, internal
 * macros -- see IR_LAMBDA's own comment in ir.h), so this is pure
 * ir_lower-time construction reusing it unchanged. The named-let branch
 * is NOT handled here -- see ir_lower_named_let, called separately from
 * ir_lower's own dispatch (it needs ir_emit-time work this can't do). */
static IRNode *ir_lower_let(Compiler *c, val_t args, bool tail, int line) {
    val_t bindings = vcar(args);
    val_t body     = vcdr(args);

    /* Forward-order params list -- same double-reverse compile_let itself
     * uses (bindings is walked once to reverse, then reversed back). */
    val_t params = V_NIL;
    int argc = 0;
    for (val_t b = bindings; vis_pair(b); b = vcdr(b)) {
        params = scm_cons(vcar(vcar(b)), params);
        argc++;
    }
    val_t fwd = V_NIL;
    while (vis_pair(params)) { fwd = scm_cons(vcar(params), fwd); params = vcdr(params); }

    IRNode *callee = ir_lower_lambda(c, fwd, body, c->name, line);
    IRNode **argv = argc > 0
        ? (IRNode **)ir_arena_alloc(c->ir_arena, sizeof(IRNode *) * (size_t)argc)
        : NULL;
    /* Tier 2.4: one KnownParam slot per binding, populated for whichever
     * ones are eligible known-lambda candidates -- see IRKnownParam's own
     * comment (ir.h) for why this has to be decided HERE, at ir_lower
     * time, rather than post-hoc the way IR_DEFINE's registration works. */
    IRKnownParam *kp = argc > 0
        ? (IRKnownParam *)ir_arena_alloc(c->ir_arena, sizeof(IRKnownParam) * (size_t)argc)
        : NULL;
    int i = 0;
    for (val_t b = bindings; vis_pair(b); b = vcdr(b)) {
        argv[i] = ir_lower(c, vcar(vcdr(vcar(b))), false, line);
        kp[i].closed = false;
        /* Only a bare (lambda ...) literal is ever eligible -- matches
         * IR_DEFINE's own `value->kind == IR_LAMBDA` gate exactly: an
         * expression that merely EVALUATES to a lambda isn't statically
         * inspectable. */
        if (argv[i]->kind == IR_LAMBDA) {
            val_t cand_params = argv[i]->as.lambda.params;
            val_t cand_body   = argv[i]->as.lambda.body;
            int cand_argc;
            int budget = INLINE_MAX_BODY_NODES;
            /* Tier 2.4: reject if the candidate's body references ANY of
             * THIS let's own binding names (including its own) -- see
             * body_contains_any_symbol's own comment for why: those
             * names aren't yet resolvable in `c` (so lambda_is_closed's
             * existing enclosing-scope check can't see them), but ARE
             * real locals once the wrapper is entered -- exactly the
             * gap a confirmed miscompilation was found through before
             * this check existed. */
            if (params_proper_arity(cand_params, &cand_argc) &&
                ir_count_ast_nodes(cand_body, INLINE_MAX_BODY_NODES) <= INLINE_MAX_BODY_NODES &&
                !body_contains_any_symbol(cand_body, fwd) &&
                lambda_is_closed(c, cand_params, cand_body, &budget)) {
                kp[i].closed = true;
                kp[i].params = cand_params;
                kp[i].body   = cand_body;
                kp[i].argc   = cand_argc;
            }
        }
        i++;
    }
    callee->as.lambda.known_params = kp;
    return ir_lower_lambda_call(c, callee, argv, argc, tail, line);
}

/* Mirrors compile_let_star exactly, including its base case (empty
 * bindings compiles the body directly via ir_lower_seq, no lambda
 * wrapper) and its single-binding-at-a-time nesting: `(let* ((x v)
 * rest...) body)` -> `((lambda (x) (let* rest... body)) v)`. The
 * synthesized inner `(let* rest body)` form is embedded as the lambda's
 * OWN body (via ir_lower_lambda, same as ir_lower_let above) -- it gets
 * lowered lazily, later, when ir_emit's IR_LAMBDA case walks that body,
 * re-entering this same function via ir_lower's own S_LET_STAR dispatch
 * hook. No manual recursion needed here -- IR_LAMBDA's deferred-body
 * design already gives this the same per-level laziness compile_let_star
 * itself relies on (each inner let* is only compiled when ITS enclosing
 * lambda's body is). */
static IRNode *ir_lower_let_star(Compiler *c, val_t args, bool tail, int line) {
    val_t bindings = vcar(args);
    val_t body     = vcdr(args);

    if (vis_nil(bindings))
        return ir_lower_seq(c, body, tail, line);

    val_t binding = vcar(bindings);
    val_t name    = vcar(binding);
    val_t init    = vcar(vcdr(binding));
    val_t rest    = vcdr(bindings);

    val_t inner_body;
    if (vis_nil(rest)) {
        inner_body = body;
    } else {
        val_t let_star_sym = sym_intern_cstr("let*");
        val_t inner_form   = scm_cons(let_star_sym, scm_cons(rest, body));
        inner_body = scm_cons(inner_form, V_NIL);
    }
    val_t params = scm_cons(name, V_NIL);

    IRNode *callee = ir_lower_lambda(c, params, inner_body, c->name, line);
    IRNode **argv  = (IRNode **)ir_arena_alloc(c->ir_arena, sizeof(IRNode *));
    argv[0] = ir_lower(c, init, false, line);

    /* Tier 2.4: same single-slot registration as ir_lower_let above,
     * just for this one binding. `c` here is exactly the right enclosing
     * scope for THIS binding's own closedness check -- the "rest" of a
     * let* (any FURTHER bindings) doesn't exist as a real scope yet at
     * all; it's still raw, embedded body, only re-lowered later against
     * the real child Compiler this binding's own IR_LAMBDA eventually
     * gets (see this function's own header comment on why that's fine:
     * the same laziness that makes let*'s per-level nesting free also
     * gives each later binding's OWN lambda_is_closed check the correct,
     * progressively narrower `c` for free, the next time ir_lower_let_star
     * is re-entered). */
    IRKnownParam *kp = (IRKnownParam *)ir_arena_alloc(c->ir_arena, sizeof(IRKnownParam));
    kp[0].closed = false;
    if (argv[0]->kind == IR_LAMBDA) {
        val_t cand_params = argv[0]->as.lambda.params;
        val_t cand_body   = argv[0]->as.lambda.body;
        int cand_argc;
        int budget = INLINE_MAX_BODY_NODES;
        /* Tier 2.4: reject if the candidate body references its OWN
         * binding name -- see body_contains_any_symbol's comment
         * (compiler.c) for the full rationale; only one name to check
         * here (unlike plain let's possibly-several siblings), since
         * let*'s later bindings aren't visible to earlier ones at all
         * (sequential scoping), so there's no sibling-name hazard beyond
         * self-reference for this one binding. */
        if (params_proper_arity(cand_params, &cand_argc) &&
            ir_count_ast_nodes(cand_body, INLINE_MAX_BODY_NODES) <= INLINE_MAX_BODY_NODES &&
            !body_contains_symbol(cand_body, name) &&
            lambda_is_closed(c, cand_params, cand_body, &budget)) {
            kp[0].closed = true;
            kp[0].params = cand_params;
            kp[0].body   = cand_body;
            kp[0].argc   = cand_argc;
        }
    }
    callee->as.lambda.known_params = kp;

    return ir_lower_lambda_call(c, callee, argv, 1, tail, line);
}

/* Mirrors compile_letrec exactly (shared by S_LETREC and S_LETREC_STAR,
 * same as compile_letrec itself -- curry draws no stricter distinction
 * between them at the compiler level). Rather than replicate
 * compile_letrec's own bespoke "pre-declare all locals with void
 * placeholders, then compile+store each init in order" bytecode
 * sequence, this desugars to `((lambda () (define n1 v1) (define n2 v2)
 * ... body...)))`  -- lambda_prescan (compiler.c) ALREADY gives ordinary
 * internal defines this exact same "every name visible from the first
 * define onward, initialized in sequence" treatment for letrec*
 * semantics, which is what compile_letrec itself provides for both
 * letrec and letrec* today, so this produces identical observable
 * behavior through already-verified machinery instead of a parallel
 * bespoke implementation. */
static IRNode *ir_lower_letrec(Compiler *c, val_t args, bool tail, int line) {
    val_t bindings = vcar(args);
    val_t body     = vcdr(args);

    /* Build (define name init) forms in reverse, then prepend each (in
     * that reverse order) onto body -- restores original binding order
     * at the front, ahead of the original body forms. */
    val_t rev = V_NIL;
    for (val_t b = bindings; vis_pair(b); b = vcdr(b)) {
        val_t name = vcar(vcar(b));
        val_t init = vcar(vcdr(vcar(b)));
        val_t def_form = scm_cons(S_DEFINE, scm_cons(name, scm_cons(init, V_NIL)));
        rev = scm_cons(def_form, rev);
    }
    val_t new_body = body;
    for (val_t p = rev; vis_pair(p); p = vcdr(p))
        new_body = scm_cons(vcar(p), new_body);

    IRNode *callee = ir_lower_lambda(c, V_NIL, new_body, "<letrec>", line);
    return ir_lower_lambda_call(c, callee, NULL, 0, tail, line);
}

/* Named let: `(let loop ((x v) ...) body)` -- see compile_let's own
 * comment for the full "zero-arg outer wrapper" shape this mirrors.
 * Unlike every other let / let-star / letrec form above, this ISN'T
 * pure ir_lower-time desugaring -- see ir.h's comment on IRNode::as.named_let
 * for why the self-tail-call thread-local arming forces the whole
 * construction into ir_emit instead. This function only stores the raw
 * pieces ir_emit's IR_NAMED_LET case needs. */
static IRNode *ir_lower_named_let(Compiler *c, val_t loop_name, val_t bindings,
                                   val_t body, bool tail, int line) {
    IRNode *n = ir_node_new(c->ir_arena, IR_NAMED_LET, tail, line);
    n->as.named_let.loop_name = loop_name;
    n->as.named_let.bindings  = bindings;
    n->as.named_let.body      = body;
    return n;
}

static IRNode *ir_lower(Compiler *c, val_t expr, bool tail, int line) {
    if (vis_pair(expr) && as_pair(expr)->hdr.flags)
        line = (int)as_pair(expr)->hdr.flags;

    /* ── Self-evaluating atoms (mirrors compile()'s own checks exactly) ── */
    if (vis_fixnum(expr) || vis_flonum(expr) || vis_bignum(expr) ||
        vis_rational(expr) || vis_complex(expr) || vis_string(expr) ||
        vis_char(expr)) {
        IRNode *n = ir_node_new(c->ir_arena, IR_CONST, tail, line);
        n->as.konst.value = expr;
        return n;
    }
    if (expr == V_TRUE || expr == V_FALSE || expr == V_NIL || expr == V_VOID) {
        IRNode *n = ir_node_new(c->ir_arena, IR_CONST, tail, line);
        n->as.konst.value = expr;
        return n;
    }

    /* ── Symbol → variable reference ── */
    if (vis_symbol(expr)) {
        Symbol *ksym = as_sym(expr);
        if (ksym->len >= 2 && ksym->data[0] == '#' && ksym->data[1] == ':') {
            IRNode *n = ir_node_new(c->ir_arena, IR_CONST, tail, line);
            n->as.konst.value = expr;
            return n;
        }
        /* Resolution (local vs upvalue vs global) is deliberately NOT
         * decided here -- see IRNode::as.var_ref's comment in ir.h for
         * why: resolve_upvalue/chunk_add_const have ordering-sensitive
         * side effects that must happen in the same left-to-right order
         * ir_emit walks the tree, not the order ir_lower happens to
         * build it in. */
        IRNode *n = ir_node_new(c->ir_arena, IR_VAR_REF, tail, line);
        n->as.var_ref.name = expr;
        return n;
    }

    /* ── Non-pair non-symbol: quote it ── */
    if (!vis_pair(expr)) {
        IRNode *n = ir_node_new(c->ir_arena, IR_CONST, tail, line);
        n->as.konst.value = expr;
        return n;
    }

    /* ── Compound form ── */
    val_t head = lang_translate(vcar(expr));
    val_t args = vcdr(expr);

    if (head == S_QUOTE) {
        IRNode *n = ir_node_new(c->ir_arena, IR_CONST, tail, line);
        n->as.konst.value  = vis_pair(args) ? vcar(args) : V_NIL;
        n->as.konst.quoted = true;
        return n;
    }
    if (head == S_IF)    return ir_lower_if(c, args, tail, line);
    if (head == S_BEGIN) return ir_lower_seq(c, args, tail, line);
    if (head == S_SET)   return ir_lower_set(c, args, line);
    if (head == S_AND)   return ir_lower_and(c, args, tail, line);
    if (head == S_OR)    return ir_lower_or(c, args, tail, line);
    /* No IR_FALLBACK path here (or on S_LET just below) for malformed
     * args -- this makes ir_lower NEVER produce IR_FALLBACK for
     * head==S_LAMBDA at all. Originally deliberate for a specific reason:
     * if it COULD fall back to IR_FALLBACK here, ir_emit's IR_FALLBACK
     * case would call compile() again on the same expr, which
     * re-classifies to this same SF_LAMBDA case and (once wired into
     * compile()'s live dispatch) recurses into this exact code path
     * again -- an infinite loop on malformed input, found during design
     * review before compile() ever called ir_lower live. The ORIGINAL fix
     * for that was accepting SIGSEGV-parity with classic's own equally
     * unchecked `vcar(args)`/`vcdr(args)` (a bare `(lambda)` crashed the
     * whole process, confirmed present on `main`) -- require_min_args
     * below is a real improvement on that, not a reintroduction of the
     * infinite-loop risk: raising via scm_raise_code is a longjmp, not a
     * return, so it never produces an IR_FALLBACK node for ir_emit to
     * reprocess at all -- it unwinds straight out of this whole compile
     * attempt instead. S_DEFINE can't use the ORIGINAL crash-parity trick
     * (compile_define's malformed-target case degrades gracefully with an
     * error message, not a crash -- silently mis-lowering it instead of
     * replicating that behavior would be a correctness bug, not crash-
     * parity), so compile()'s own SF_DEFINE case instead checks for
     * IR_FALLBACK and falls back to compile_define directly -- see
     * compile()'s dispatch for the full reasoning; unaffected by this
     * change either way, since S_DEFINE never reaches this file's own
     * IR_FALLBACK-avoidance concern in the first place. */
    if (head == S_LAMBDA) {
        require_min_args(args, 1, "lambda");
        return ir_lower_lambda(c, vcar(args), vcdr(args), NULL, line);
    }
    /* let -- named vs plain distinguished the same way compile_let
     * itself does (a leading symbol instead of a bindings list). Named
     * let routes to ir_lower_named_let (IR_NAMED_LET); everything else
     * routes to ir_lower_let (pure desugaring, see its own comment). */
    if (head == S_LET) {
        /* Same require_min_args + no-IR_FALLBACK reasoning as S_LAMBDA
         * just above -- a bare `(let)` used to SIGSEGV here identically
         * to `vcar(V_NIL)` crashing classic's own compile_let. */
        require_min_args(args, 1, "let");
        val_t bindings = vcar(args);
        if (vis_symbol(bindings)) {
            val_t let_body = vcdr(args);
            require_min_args(let_body, 1, "let");  /* named-let's own bindings list */
            return ir_lower_named_let(c, bindings, vcar(let_body), vcdr(let_body), tail, line);
        }
        return ir_lower_let(c, args, tail, line);
    }
    if (head == S_LET_STAR) {
        require_min_args(args, 1, "let*");
        return ir_lower_let_star(c, args, tail, line);
    }
    if (head == S_LETREC || head == S_LETREC_STAR) {
        require_min_args(args, 1, head == S_LETREC ? "letrec" : "letrec*");
        return ir_lower_letrec(c, args, tail, line);
    }
    /* Both `(define sym expr)` and `(define (f params...) body...)`
     * lambda-sugar are natively lowered (the latter via IR_LAMBDA, now
     * that it exists -- see ir_lower_define_lambda_sugar's comment);
     * anything else (malformed args) falls through to the generic
     * IR_FALLBACK wrap below, matching compile_define's own error path. */
    if (head == S_DEFINE && vis_pair(args)) {
        val_t target = vcar(args);
        if (vis_symbol(target)) return ir_lower_define(c, args, line);
        if (vis_pair(target))   return ir_lower_define_lambda_sugar(c, args, line);
    }

    /* Anything else compile() would treat as a special form or macro use
     * (classify_head -- the same function compile() itself switches on,
     * see its own comment) still falls back whole; only a genuine
     * ordinary call (SF_NONE) gets natively lowered here. */
    {
        val_t transformer = V_FALSE;
        if (classify_head(c, head, args, &transformer) == SF_NONE)
            return ir_lower_call(c, head, args, tail, line);
    }

    /* Not (yet) natively lowered -- delegate the whole subform. */
    IRNode *n = ir_node_new(c->ir_arena, IR_FALLBACK, tail, line);
    n->as.fallback.expr = expr;
    return n;
}

/* Tier 2.3 local inliner: splices a known-closed candidate's raw
 * `params`/`body` directly into the CALLING Compiler `c`'s own
 * instruction stream -- no new child Compiler, no OP_CLOSURE, no
 * OP_CALL/OP_RETURN at all. `args` are `call_node`'s own already-lowered
 * (via ir_lower, at ir_lower_call time) argument IRNodes; `call_node` is
 * the original IR_CALL node being replaced, whose ->tail this inlined
 * body's own last form inherits (so an inlined tail call composes for
 * free with whatever self-tail-call/fused-global classification that
 * last form's own ir_emit ends up choosing).
 *
 * This is deliberately IR_LAMBDA's own case (above) with init_compiler/
 * end_compiler/OP_CLOSURE removed and begin_scope/end_scope substituted
 * for "new Compiler": begin_scope/end_scope (see end_scope's own comment)
 * already correctly (a) gives resolve_local's innermost-first walk
 * correct shadowing for the newly-spliced params/internal-defines against
 * c's own pre-existing locals, (b) emits OP_CLOSE_UP for a nested closure
 * inside the inlined body capturing one of the spliced-in locals, before
 * (c) OP_SLIDE compacts every spliced local away, leaving only the
 * inlined body's final value on the stack -- exactly where a real call's
 * return value would have ended up. No new opcode needed. */
/* `known_params` (Tier 2.4, wrapper elision): NULL for the existing
 * Tier 2.3 named-candidate call site, or the let/let*-desugared wrapper's
 * own per-param closedness analysis (ir_lower_let/ir_lower_let_star) when
 * this splice IS that wrapper's callee -- see IRKnownParam's own comment
 * (ir.h) for what it records. Transferred into c->known[] here, the same
 * way IR_LAMBDA's own ir_emit case already does for a REAL (non-spliced)
 * closure, so splicing a let's own wrapper away doesn't silently regress
 * the already-shipped optimization for a let-bound lambda literal called
 * inside that same let's own body (caught by independent design review
 * before this landing: routing straight to this function bypasses
 * IR_LAMBDA's case entirely, which is the only other place that transfer
 * happens).
 *
 * `track_cycle`: true for the Tier 2.3 named-candidate path (this body
 * IS looked up by name at other call sites, so currently_inlining's
 * g_inlining_bodies stack must track it to catch mutual recursion via
 * splicing); false for wrapper elision, where a body is never looked up
 * by name a second time (nothing registers it in any known[] table) --
 * cycle detection is structurally meaningless for that path. Passing
 * false there is required, not just an optimization: g_inlining_bodies
 * is a small, FIXED-size, SHARED array (MAX_INLINE_DEPTH) that unconditional
 * per-let-level tracking would exhaust for an entirely ordinary, non-
 * adversarial long let* chain (each binding is its own nested wrapper),
 * silently degrading cycle detection for genuine named-candidate mutual
 * recursion nested elsewhere in the same compile -- caught by independent
 * design review before this landing. */
static void ir_emit_inline_call(Compiler *c, val_t params, val_t body,
                                 IRNode **args, int argc, IRNode *call_node,
                                 IRKnownParam *known_params, bool track_cycle) {
    begin_scope(c);

    /* Every argument must be evaluated -- and, in particular, every
     * IR_VAR_REF inside it resolved -- against the CALLER's own scope,
     * completely untouched by this call's own params, before any of
     * those params are bound. Binding param i as a real, resolvable
     * local BEFORE emitting argument i+1 was a real, confirmed
     * miscompilation: (let ((x 100)) (define (add x y) (+ x y)) (add 1
     * x)) returned 2 instead of 101, because by the time the second
     * argument's reference to the OUTER x was resolved (IR_VAR_REF
     * resolution is deliberately deferred to ir_emit time -- see ir.h),
     * the first param (also named x, from `add`'s own param list) had
     * already been bound and shadowed it. This is exactly the ordering
     * hazard reserve_pending_slot/release_pending_slots already exists
     * to prevent for an OUTER call's own sibling arguments (see that
     * function's own comment) -- the same fix applies here: reserve
     * every argument's value as an anonymous (name = V_FALSE, never
     * resolvable) placeholder first, and only claim them as the real,
     * shadowing-capable param names in a second pass once every argument
     * has already been evaluated. */
    int base = c->local_count;
    for (int i = 0; i < argc; i++) {
        ir_emit(c, args[i]);
        reserve_pending_slot(c);
    }
    /* Claim each reserved placeholder as its real param name -- no
     * bytecode, the values are already correctly positioned; depth is
     * already correct too (reserve_pending_slot set it to the current
     * scope_depth, exactly what add_local+mark_initialised would have
     * set here). dbg_idx is NOT already correct, though: reserve_pending_
     * slot never calls chunk_local_debug_add (it's an anonymous
     * placeholder, not a real named local, at the point it's reserved),
     * so it retains whatever stale index the physical array slot last
     * held. end_scope unconditionally calls chunk_local_debug_end on
     * every popped local's dbg_idx when this splice's own scope closes
     * -- left stale, that would silently close some UNRELATED, already-
     * finalized local's live-range entry instead of this param's own
     * (caught by independent code review), corrupting the interactive
     * debugger's reported variable lifetimes. Give each renamed param a
     * genuine, fresh entry, exactly as add_local does. */
    val_t p = params;
    for (int i = 0; i < argc; i++) {
        val_t name = vcar(p);
        c->locals[base + i].name    = name;
        c->locals[base + i].dbg_idx = chunk_local_debug_add(
            c->chunk, name, base + i, chunk_pos(c->chunk));
        p = vcdr(p);
    }

    /* Tier 2.4: transfer any per-param known-lambda registration the
     * caller already computed (let/let*'s own closedness analysis --
     * see this function's own header comment). Same shape as IR_LAMBDA's
     * own ir_emit case, just against `c` directly (base + i) instead of
     * a child Compiler's own resolve_local. */
    if (known_params) {
        for (int i = 0; i < argc; i++) {
            if (known_params[i].closed) {
                c->known[base + i].valid  = true;
                c->known[base + i].params = known_params[i].params;
                c->known[base + i].body   = known_params[i].body;
                c->known[base + i].argc   = known_params[i].argc;
            }
        }
    }

    lambda_prescan(c, body, call_node->line);

    /* Mark this candidate's body as "currently being inlined" for the
     * duration of walking it -- see currently_inlining's own comment.
     * Bounded by MAX_INLINE_DEPTH, itself well above the inline-effort
     * budget (32), so this can't overflow in practice for the Tier 2.3
     * named-candidate path; fails safe (silently stops tracking, at
     * worst re-allowing a budget-bounded expansion) rather than crashing
     * if it somehow did. Skipped entirely when !track_cycle (Tier 2.4
     * wrapper elision -- see this function's own header comment for why
     * that's required, not just an optimization: unconditional per-let-
     * level tracking would let an entirely ordinary long let* chain
     * exhaust this small, shared, fixed-size array on its own). */
    if (track_cycle && g_inlining_depth < MAX_INLINE_DEPTH)
        g_inlining_bodies[g_inlining_depth++] = body;

    /* Interleaved lower-then-emit walk, one body form at a time, against
     * `c` directly -- identical in shape to IR_LAMBDA's own body walk
     * above, just targeting the caller's own Compiler instead of a
     * freshly created child. */
    val_t list = body;
    if (vis_nil(list)) {
        emit(c, OP_VOID, call_node->line);
    } else {
        int seq_line = call_node->line;
        while (vis_pair(list)) {
            val_t expr = vcar(list);
            val_t rest = vcdr(list);
            bool  last = vis_nil(rest);
            if (as_pair(list)->hdr.flags)
                seq_line = (int)as_pair(list)->hdr.flags;
            IRNode *form_ir = ir_lower(c, expr, call_node->tail && last, seq_line);
            ir_emit(c, form_ir);
            if (!last) emit(c, OP_POP, seq_line);
            list = rest;
        }
    }

    if (track_cycle && g_inlining_depth > 0 &&
        g_inlining_bodies[g_inlining_depth - 1] == body)
        g_inlining_depth--;

    end_scope(c, call_node->line);
}

static void ir_emit(Compiler *c, IRNode *n) {
    switch (n->kind) {
    case IR_CONST: {
        val_t v = n->as.konst.value;
        /* quote's own codegen (compile()'s S_QUOTE case) always uses
         * emit_const, never the special-cased immediate opcodes below --
         * see this struct field's comment in ir.h. */
        if (!n->as.konst.quoted) {
            if (v == V_TRUE)  { emit(c, OP_TRUE,  n->line); return; }
            if (v == V_FALSE) { emit(c, OP_FALSE, n->line); return; }
            if (v == V_NIL)   { emit(c, OP_NIL,   n->line); return; }
            if (v == V_VOID)  { emit(c, OP_VOID,  n->line); return; }
        }
        emit_const(c, v, n->line);
        return;
    }
    case IR_VAR_REF:
        /* Resolution happens here, at emit time -- see ir.h's comment on
         * IRNode::as.var_ref for why. emit_load is the exact function
         * compile()'s own symbol-handling calls. */
        emit_load(c, n->as.var_ref.name, n->line);
        return;
    case IR_FALLBACK:
        compile(c, n->as.fallback.expr, n->tail, n->line);
        return;
    case IR_IF: {
        ir_emit(c, n->as.iff.test);
        int else_jmp = emit_jump(c, OP_JUMP_FALSE, n->line);
        ir_emit(c, n->as.iff.then);
        int end_jmp  = emit_jump(c, OP_JUMP, n->line);
        patch_jump(c, else_jmp);
        ir_emit(c, n->as.iff.els);
        patch_jump(c, end_jmp);
        return;
    }
    case IR_SEQ: {
        /* Interleaved lower-then-emit walk, one item at a time -- see
         * ir.h's comment on IRNode::as.seq for why (an internal
         * define-syntax's registration only exists once IT has been
         * emitted, and a LATER item's own classify_head-based
         * classification needs to see it). Mirrors compile_seq exactly,
         * including its own per-spine-cell `hdr.flags` line tracking. */
        val_t list = n->as.seq.body;
        int seq_line = n->line;
        while (vis_pair(list)) {
            val_t expr = vcar(list);
            val_t rest = vcdr(list);
            bool  last = vis_nil(rest);
            if (as_pair(list)->hdr.flags)
                seq_line = (int)as_pair(list)->hdr.flags;
            IRNode *item_ir = ir_lower(c, expr, n->tail && last, seq_line);
            ir_emit(c, item_ir);
            if (!last) emit(c, OP_POP, seq_line);
            list = rest;
        }
        return;
    }
    case IR_SET: {
        ir_emit(c, n->as.set.value);
        /* Resolution happens here, at emit time -- see ir.h's comment on
         * IRNode::as.set for why. */
        emit_store(c, n->as.set.name, n->line);
        /* Tier 2.3: a set! anywhere reachable permanently invalidates any
         * known-lambda registration for this name -- see poison_known's
         * own comment for why this must walk ->enclosing, not just c. */
        poison_known(c, n->as.set.name);
        emit(c, OP_VOID, n->line);
        return;
    }
    case IR_AND: {
        /* Direct translation of compile_and's patch-list loop -- see
         * ir_lower_and's comment for the exact tail-propagation this
         * mirrors. Arena-allocated patch array sized to `count`, not
         * compile_and's fixed MAX_LOCALS C-stack array -- no reason to
         * inherit that specific bound here. */
        int count = n->as.andor.count;
        if (count == 0) { emit(c, OP_TRUE, n->line); return; }
        int *patches = (int *)ir_arena_alloc(c->ir_arena, sizeof(int) * (size_t)count);
        int np = 0;
        for (int i = 0; i < count; i++) {
            ir_emit(c, n->as.andor.items[i]);
            if (i != count - 1)
                patches[np++] = emit_jump(c, OP_JUMP_FALSE, n->line);
        }
        int end = emit_jump(c, OP_JUMP, n->line);
        int false_pos = chunk_pos(c->chunk);
        emit(c, OP_FALSE, n->line);
        patch_jump(c, end);
        for (int i = 0; i < np; i++)
            chunk_patch16(c->chunk, patches[i], (uint16_t)false_pos);
        return;
    }
    case IR_OR: {
        /* Direct translation of compile_or's patch-list loop. */
        int count = n->as.andor.count;
        if (count == 0) { emit(c, OP_FALSE, n->line); return; }
        int *patches = (int *)ir_arena_alloc(c->ir_arena, sizeof(int) * (size_t)count);
        int np = 0;
        for (int i = 0; i < count; i++) {
            ir_emit(c, n->as.andor.items[i]);
            if (i != count - 1) {
                emit(c, OP_DUP, n->line);
                patches[np++] = emit_jump(c, OP_JUMP_TRUE, n->line);
                emit(c, OP_POP, n->line);
            }
        }
        for (int i = 0; i < np; i++)
            patch_jump(c, patches[i]);
        return;
    }
    case IR_DEFINE: {
        ir_emit(c, n->as.def.value);
        /* Declaration/resolution happens here, at emit time -- see ir.h's
         * comment on IRNode::as.def for why. */
        emit_define_store(c, n->as.def.name, n->line);

        /* Tier 2.3 local inliner: register `name` as a known,
         * safely-re-lowerable lambda iff ALL of: internal (not
         * top-level) binding, the value just emitted above was
         * syntactically (lambda ...) (covers both `(define name (lambda
         * ...))` and `(define (f params...) body...)` sugar -- both
         * reach here as IR_DEFINE{value=IR_LAMBDA}, see
         * ir_lower_define/ir_lower_define_lambda_sugar), that lambda
         * compiled fully closed (g_last_lambda_upval_count == 0 -- the
         * soundness condition, see its own comment), a proper
         * (non-rest) param list, non-self-recursive, and within the
         * size budget. A candidate failing any gate simply isn't
         * registered -- calls to it still compile correctly, just
         * without inlining. */
        if (c->scope_depth > 0 && n->as.def.value->kind == IR_LAMBDA &&
            g_last_lambda_upval_count == 0) {
            val_t params = n->as.def.value->as.lambda.params;
            val_t body   = n->as.def.value->as.lambda.body;
            int argc;
            /* Ordered cheapest-first: params_proper_arity and
             * ir_count_ast_nodes are simple bounded walks; both run
             * before body_contains_symbol's unbounded recursive scan, so
             * a large candidate gets rejected by the size budget without
             * ever paying for the full self-reference walk (or risking
             * deep C-stack recursion compiling adversarially large
             * source in the first place). */
            if (params_proper_arity(params, &argc) &&
                ir_count_ast_nodes(body, INLINE_MAX_BODY_NODES) <= INLINE_MAX_BODY_NODES &&
                !body_contains_symbol(body, n->as.def.name)) {
                int slot = resolve_local(c, n->as.def.name);
                if (slot >= 0) {
                    c->known[slot].valid  = true;
                    c->known[slot].params = params;
                    c->known[slot].body   = body;
                    c->known[slot].argc   = argc;
                }
            }
        }
        return;
    }
    case IR_CALL: {
        /* Direct translation of compile_call's own three-branch
         * classification -- see ir.h's comment on IRNode::as.call for
         * why this decision (not just the resolution it depends on)
         * happens here, at emit time, rather than during ir_lower.
         *
         * The self-tail-call sub-branch just below now has real coverage
         * (as of the sixth landing, named-let/IR_NAMED_LET below): a
         * named-let loop's inner lambda body is walked through
         * ir_lower_lambda+ir_emit (see IR_NAMED_LET's own case), with
         * c->self_tail_name armed there exactly as compile_let arms it
         * for the classic path -- so a tail-position self-call inside
         * that body reaches this branch for real, exercised by
         * tests/test_core.c's named-let self-tail-call cases. (Earlier
         * landings, before named-let was IR-lowered, had this branch
         * correct by inspection only; an earlier version of the test
         * file wrongly claimed coverage that didn't exist yet -- caught
         * by independent code review at the time.) */
        val_t head  = n->as.call.head;
        int   argc  = n->as.call.argc;
        bool  ctail = n->tail;

        /* Tier 2.4: closure elision for let / let-star / letrec / letrec-star's own
         * compiler-synthesized entry wrapper. ir_lower_lambda_call is the
         * ONLY producer, anywhere in this file, of an IR_CALL with
         * head==V_FALSE and a literal IR_LAMBDA callee (grep confirms
         * exactly 3 call sites: ir_lower_let/ir_lower_let_star/
         * ir_lower_letrec, each returning the node as the WHOLE lowered
         * form for that let/letrec expression) -- so this callee's
         * closure is provably always non-escaping and always single-
         * call-site BY CONSTRUCTION, not by analysis: nothing else ever
         * holds a reference to this specific node's callee field except
         * this same emission, which immediately consumes it with a call.
         * Unlike the named-candidate inliner below, this needs no
         * closedness check, no self-recursion guard, and no size/effort
         * budget -- there being exactly one call site means splicing
         * costs exactly the same total code size as not splicing; the
         * body is compiled exactly once either way. Checked first since
         * head==V_FALSE trivially fails every vis_symbol(head) guard
         * every other branch below requires, making this unconditionally
         * mutually exclusive with all of them -- placement is purely for
         * readability, not correctness. See ir_emit_inline_call's own
         * header comment for known_params/track_cycle's meaning here
         * (NB: named-let's own entry wrapper does NOT go through this
         * path -- IR_NAMED_LET hand-builds its own separate Compiler
         * rather than routing through ir_lower_lambda_call/IR_CALL at
         * all; eliding it is deliberately deferred, real separate work).
         *
         * `c->local_count + argc < MAX_LOCALS` (flagged missing by
         * independent code review): the Tier 2.3 named-candidate site
         * below guards its own ir_emit_inline_call call with this exact
         * check; this site originally didn't, and unlike Tier 2.3 that
         * omission is NOT merely quantitative here. Before wrapper
         * elision, every let / let* / letrec / letrec* got its own fresh child
         * Compiler with local_count reset to 0 -- MAX_LOCALS(256) was a
         * per-form limit. Splicing removes that reset, so a long chain of
         * lets/letrec sharing one enclosing frame (sequential lets in one
         * function body, or a single let* with 256+ bindings) now
         * accumulates locals against ONE Compiler's fixed-size
         * `locals[MAX_LOCALS]`/`known[MAX_LOCALS]` arrays. Without this
         * guard, ir_emit_inline_call's param-claiming loop writes
         * `c->locals[base + i]` / `c->known[base + i]` for i in
         * [0, argc) unconditionally -- reserve_pending_slot's own -1-on-
         * overflow return is never consulted there, so base + i walks
         * straight past index 255 and out of bounds of both arrays,
         * corrupting adjacent Compiler fields on the stack. Falling
         * through on failure (rather than returning) reaches the
         * ordinary, always-safe call path further below, which emits the
         * callee as a real closure and calls it -- exactly what happened
         * before this landing existed. */
        if (head == V_FALSE && n->as.call.callee->kind == IR_LAMBDA &&
            c->local_count + argc < MAX_LOCALS) {
            ir_emit_inline_call(c, n->as.call.callee->as.lambda.params,
                                 n->as.call.callee->as.lambda.body,
                                 n->as.call.args, argc, n,
                                 n->as.call.callee->as.lambda.known_params,
                                 false);
            return;
        }

        if (ctail && vis_symbol(head) && c->self_tail_name != V_FALSE &&
            head == c->self_tail_name && !c->self_tail_mutated &&
            resolve_local(c, head) < 0) {
            resolve_upvalue(c, head);
            /* Tier 2.3: reserve/release brackets this loop so a nested
             * inline call in a later argument (see reserve_pending_slot's
             * own comment) numbers its own locals correctly relative to
             * earlier, still-pending sibling arguments. */
            int saved = c->local_count;
            for (int i = 0; i < argc; i++) {
                ir_emit(c, n->as.call.args[i]);
                reserve_pending_slot(c);
            }
            release_pending_slots(c, saved);
            emit_ab(c, OP_SELF_TAIL_CALL, (uint8_t)argc, n->line);
            return;
        }

        /* Tier 2.3 local inliner. Mutually exclusive with both the
         * self-tail-call branch above and the fused-global branch below
         * by construction: both require resolve_local(c, head) < 0, which
         * a registered known-lambda local can never satisfy (it IS a
         * resolvable local). MAX_LOCALS overflow from the params alone is
         * checked here; overflow from the inlined body's own internal
         * defines relies on add_local's existing guard, same as any other
         * deeply-nested closure in this file today -- not a new risk
         * category, just a modest quantitative increase in worst-case
         * slot pressure, and bounded in practice by the effort/size
         * budgets already limiting how much inlining can compound. */
        if (vis_symbol(head)) {
            int slot = resolve_local(c, head);
            if (slot >= 0 && c->known[slot].valid &&
                c->known[slot].argc == argc &&
                c->local_count + argc < MAX_LOCALS &&
                /* Mutual/indirect recursion guard -- checked BEFORE
                 * claiming inline budget below, so a candidate rejected
                 * for this reason doesn't burn a unit of the shared
                 * budget on a splice that was never going to happen; see
                 * currently_inlining's own comment. */
                !currently_inlining(c->known[slot].body) &&
                ir_arena_take_inline_budget(c->ir_arena)) {
                ir_emit_inline_call(c, c->known[slot].params,
                                     c->known[slot].body,
                                     n->as.call.args, argc, n,
                                     NULL, true);
                return;
            }
        }

        if (vis_symbol(head) && !is_keyword_symbol(head) &&
            resolve_local(c, head) < 0 && resolve_upvalue(c, head) < 0) {
            /* Tier 2.5 step 1 (docs/thoughts/performance-chez-kaappi.md §5,
             * item 2.5): open-code car/cdr instead of the ordinary fused-
             * global-call path below, when the call shape matches their
             * own fixed 1-arg arity exactly -- see OP_CAR/OP_CDR's own
             * comment (opcode.h) for the full redefinition-safety design.
             * car/cdr are ordinary, user-redefinable globals, not sealed
             * VM primitives, so the opcode re-verifies at EVERY execution
             * (comparing against prim_car/prim_cdr's actual function
             * pointer, builtins.h) rather than trusting anything captured
             * here at compile time -- this emission site only needs to
             * decide WHETHER to try the fast opcode at all, never what to
             * compare against. An earlier version of this landing tried
             * to also bake in "the value car currently resolves to" as a
             * second, compile-time-captured constant to compare against
             * at runtime -- a real, confirmed bug: if `car` had ALREADY
             * been redefined before this call site was compiled (a
             * perfectly ordinary sequence: `(define (car x) ...)` followed
             * by a later `(car ...)` call in the same or a later top-level
             * form), that captured "expected" value WAS the redefinition,
             * so the runtime guard matched it forever and the opcode kept
             * open-coding raw pair access instead of ever calling the
             * redefined procedure. Comparing against the actual, immutable
             * C function pointer instead is correct regardless of
             * compile-time timing, and needs no such capture at all --
             * `head` only needs to be unshadowed (already confirmed above)
             * and its symbol name checked. No compile-time env lookup
             * either: if car/cdr are actually unbound in this chunk's
             * target env, the opcode's own load_global_cached raises the
             * correct unbound-variable error at runtime, same as the
             * ordinary OP_CALL_GLOBAL path below would. Wrong arity
             * (argc != 1) falls through on purpose: that's headed for a
             * genuine wrong-number-of-arguments error either way, and the
             * ordinary call path below already raises it correctly. */
            /* Extended to cons/pair?/null? (same design, same
             * redefinition-safety guard -- see opcode.h's own comment on
             * this whole group of opcodes) once car/cdr proved out, then
             * to +, -, *, =, <, <=, >, >= (Tier 2.5 step 2) once THAT
             * proved out -- OP_ADD/OP_SUB/OP_MUL/OP_NUMEQ/OP_LT/OP_LE/
             * OP_GT/OP_GE are all strictly binary opcodes (pop exactly
             * two, push one), so a 1-arg or 3+-arg call to any of these
             * names still correctly falls through to the ordinary
             * OP_CALL_GLOBAL/OP_TAIL_CALL_GLOBAL path below (which is what
             * R7RS's own variadic +, -, * and comparison chains' own
             * reduction over ac != 2 needs anyway -- not replicated here).
             * On a match, every entry below calls the REAL primitive
             * function directly (see builtins.h's own comment on
             * prim_add/prim_num_lt/etc.) rather than any separate inline
             * fast-path body -- required for correctness, not just style:
             * prim_num_lt/le/gt/ge's own 2-arg case dispatches to
             * sx_lt/sx_le/sx_gt/sx_ge for a symbolic (CAS) operand, which
             * a hand-rolled fixnum-or-num_lt fast path would not
             * replicate, silently doing the wrong thing for e.g. an
             * open-coded `(< x 5)` where `x` is a sym-var.
             *
             * Table-driven rather than three separate growing if/else
             * chains (one per arity group) as of this landing: the third
             * such chain (the eight arithmetic/comparison ops just above)
             * was near-identical copy-paste of the first two's own
             * "compute sym_ci, emit args, bracket pending slots if arity
             * 2, emit_ab" shape, an accumulating-duplication smell flagged
             * on review. Adding a future open-coded primitive is now one
             * table row, not another ~15-line branch. */
            static const struct { const char *name; uint8_t op; uint8_t arity; } open_code_table[] = {
                { "car",   OP_CAR,   1 },
                { "cdr",   OP_CDR,   1 },
                { "pair?", OP_PAIRP, 1 },
                { "null?", OP_NULLP, 1 },
                { "cons",  OP_CONS,  2 },
                { "+",     OP_ADD,   2 },
                { "-",     OP_SUB,   2 },
                { "*",     OP_MUL,   2 },
                { "=",     OP_NUMEQ, 2 },
                { "<",     OP_LT,    2 },
                { "<=",    OP_LE,    2 },
                { ">",     OP_GT,    2 },
                { ">=",    OP_GE,    2 },
            };
            enum { OPEN_CODE_N = sizeof(open_code_table) / sizeof(open_code_table[0]) };
            /* Interned once, lazily, rather than re-interning every table
             * entry's name on every 2-arg call node ir_emit ever visits
             * (flagged on review: an ordinary, non-open-coded 2-arg call
             * like `(f 1 2)` used to pay for 9 wasted sym_intern_cstr
             * calls -- cons plus all eight arithmetic/comparison entries
             * -- before falling through). A benign race on first use
             * across concurrent compiles (if that ever happens) just
             * re-interns the same idempotent symbols into the same slots
             * twice; sym_intern_cstr itself is the actual synchronization
             * point, so no lock is needed here. */
            static val_t open_code_syms[OPEN_CODE_N];
            static bool open_code_syms_ready = false;
            if (!open_code_syms_ready) {
                for (size_t i = 0; i < OPEN_CODE_N; i++)
                    open_code_syms[i] = sym_intern_cstr(open_code_table[i].name);
                open_code_syms_ready = true;
            }
            /* `!ctail` (found by independent code review, confirmed by
             * direct reproduction -- a real TCO regression, not just a
             * documented trade-off): none of these opcodes' own VM
             * handlers reuse the current CallFrame the way OP_TAIL_CALL/
             * OP_TAIL_CALL_GLOBAL do on their fallback (redefined-to-a-
             * BcClosure) path -- they always go through call_foreign,
             * which recurses via apply_arr -> a nested vm_run(). A
             * self-tail-recursive redefinition -- `(define (cdr n) (if (=
             * n 0) 'done (cdr (- n 1)))) (cdr 2000000)` -- overflowed the
             * call stack on this branch (256-frame limit hit almost
             * immediately) where it ran to completion on main, silently
             * breaking R7RS's proper-tail-call guarantee for any program
             * that redefines one of these names and calls it recursively
             * in tail position. Simplest correct fix: never open-code a
             * call already known to be in tail position at all -- fall
             * through to the ordinary OP_TAIL_CALL_GLOBAL path below
             * instead, which already handles both the common (unredefined
             * primitive) case efficiently via the existing fused-global-
             * call opcode AND the redefined-BcClosure case with correct,
             * O(1)-C-stack tail-call reuse. Giving up the open-coding win
             * specifically in tail position is an acceptable trade: these
             * names are rarely the tail expression of a recursive loop
             * themselves (far more commonly an ARGUMENT to the actual
             * recursive call, e.g. `(loop (cdr x) ...)`, a shape this
             * restriction doesn't touch at all, since `cdr` there is not
             * itself in tail position). */
            bool opencoded = false;
            if (!ctail && (argc == 1 || argc == 2)) {
                for (size_t oci = 0; oci < OPEN_CODE_N; oci++) {
                    if ((uint8_t)argc != open_code_table[oci].arity) continue;
                    if (head != open_code_syms[oci]) continue;
                    int sym_ci = chunk_add_const(c->chunk, head);
                    if (argc == 1) {
                        ir_emit(c, n->as.call.args[0]);
                    } else {
                        /* Both args are, briefly, pending values on the
                         * real stack while the second one compiles --
                         * bracketed exactly like any other multi-argument
                         * call site in this file (see e.g. compile_do's
                         * own "Tier 2.4 fix" comment for the bug class
                         * this prevents: a splicing second argument
                         * computing its own new locals' slot indices
                         * without accounting for the first argument's
                         * still-pending value). */
                        int saved = c->local_count;
                        ir_emit(c, n->as.call.args[0]);
                        reserve_pending_slot(c);
                        ir_emit(c, n->as.call.args[1]);
                        reserve_pending_slot(c);
                        release_pending_slots(c, saved);
                    }
                    emit_ab(c, open_code_table[oci].op, (uint8_t)sym_ci, n->line);
                    opencoded = true;
                    break;
                }
            }
            if (opencoded) return;

            int ci = chunk_add_const(c->chunk, head);
            /* Tier 2.3: see the self-tail-call branch's own comment above. */
            int saved = c->local_count;
            for (int i = 0; i < argc; i++) {
                ir_emit(c, n->as.call.args[i]);
                reserve_pending_slot(c);
            }
            release_pending_slots(c, saved);
            emit_abc(c, ctail ? OP_TAIL_CALL_GLOBAL : OP_CALL_GLOBAL,
                     (uint8_t)ci, (uint8_t)argc, n->line);
            return;
        }

        /* Tier 2.3: the callee itself (evaluated first here) is ALSO a
         * pending value while the args below are compiled -- reserve it
         * too, not just the args. */
        {
            int saved = c->local_count;
            ir_emit(c, n->as.call.callee);
            reserve_pending_slot(c);
            for (int i = 0; i < argc; i++) {
                ir_emit(c, n->as.call.args[i]);
                reserve_pending_slot(c);
            }
            release_pending_slots(c, saved);
        }
        emit_ab(c, ctail ? OP_TAIL_CALL : OP_CALL, (uint8_t)argc, n->line);
        return;
    }
    case IR_LAMBDA: {
        /* Direct translation of compile_lambda -- see ir.h's comment on
         * IRNode::as.lambda for why the body is walked (lowered AND
         * emitted, one form at a time) here rather than during ir_lower:
         * this is the point where the real child Compiler this body
         * belongs to finally exists. */
        Compiler child;
        init_compiler(&child, c, n->as.lambda.name);

        int arity = compile_params(&child, n->as.lambda.params);
        child.chunk->arity = arity;

        /* Tier 2.4: populate known[] for any let/let*-desugared param
         * ir_lower_let/ir_lower_let_star already proved closed (see
         * IRKnownParam's own comment, ir.h, for why this can't be done
         * the IR_DEFINE way, post-hoc from g_last_lambda_upval_count).
         * resolve_local(&child, ...) right after compile_params is
         * guaranteed to find each param at its correct physical slot --
         * params were just add_local'd, in the same forward order
         * known_params was built in, by compile_params itself. */
        if (n->as.lambda.known_params) {
            int pi = 0;
            for (val_t p = n->as.lambda.params; vis_pair(p); p = vcdr(p), pi++) {
                IRKnownParam *kp = &n->as.lambda.known_params[pi];
                if (kp->closed) {
                    int slot = resolve_local(&child, vcar(p));
                    if (slot >= 0) {
                        child.known[slot].valid  = true;
                        child.known[slot].params = kp->params;
                        child.known[slot].body   = kp->body;
                        child.known[slot].argc   = kp->argc;
                    }
                }
            }
        }

        begin_scope(&child);  /* body scope: depth 1+, matches compile_lambda */

        lambda_prescan(&child, n->as.lambda.body, n->line);

        /* Interleaved lower-then-emit walk, one body form at a time,
         * against the now-real child compiler -- mirrors compile_seq(
         * &child, body, true, line) exactly (including its own per-spine
         * -cell line tracking), just with ir_lower+ir_emit standing in
         * for compile()'s direct call per form. */
        val_t list = n->as.lambda.body;
        if (vis_nil(list)) {
            emit(&child, OP_VOID, n->line);
        } else {
            int seq_line = n->line;
            while (vis_pair(list)) {
                val_t expr = vcar(list);
                val_t rest = vcdr(list);
                bool  last = vis_nil(rest);
                if (as_pair(list)->hdr.flags)
                    seq_line = (int)as_pair(list)->hdr.flags;
                IRNode *form_ir = ir_lower(&child, expr, last, seq_line);
                ir_emit(&child, form_ir);
                if (!last) emit(&child, OP_POP, seq_line);
                list = rest;
            }
        }

        Chunk *ch = end_compiler(&child);

        /* Preserve source AST and upvalue names for tiered JIT hot-swap --
         * matches compile_lambda exactly. */
        ch->src_lambda = scm_cons(S_LAMBDA,
                              scm_cons(n->as.lambda.params, n->as.lambda.body));
        if (child.upval_count > 0) {
            ch->upval_names = (val_t *)gc_alloc_raw_pinned(
                                  (size_t)child.upval_count * sizeof(val_t));
            for (int i = 0; i < child.upval_count; i++)
                ch->upval_names[i] = child.upvals[i].name;
        }

        /* In parent: emit OP_CLOSURE followed by upvalue descriptors --
         * matches compile_lambda exactly. */
        int ci = chunk_add_const(c->chunk, (val_t)(uintptr_t)ch);
        emit_ab(c, OP_CLOSURE, (uint8_t)ci, n->line);
        for (int i = 0; i < child.upval_count; i++) {
            chunk_emit(c->chunk, child.upvals[i].is_local ? 1 : 0, n->line);
            chunk_emit(c->chunk, (uint8_t)child.upvals[i].index,   n->line);
        }
        /* Tier 2.3: see g_last_lambda_upval_count's own comment -- last
         * statement in this case, after `child` is otherwise done with. */
        g_last_lambda_upval_count = child.upval_count;
        return;
    }
    case IR_NAMED_LET: {
        /* Tier 2.4: wrapper elision for named-let's own "zero-arg outer
         * wrapper" -- see ir.h's comment on IRNode::as.named_let for the
         * shape this used to build as a REAL, separate closure (`outer`,
         * a Compiler of its own, OP_CLOSURE'd and immediately OP_CALL'd/
         * OP_TAIL_CALL'd from here). Exactly the same "provably non-
         * escaping, single call site BY CONSTRUCTION" argument already
         * used to justify eliding let / let* / letrec / letrec*'s own wrapper
         * (see this switch's head==V_FALSE branch above) applies here:
         * `outer` was created, closed over, and consumed by the very next
         * instruction, every time, with no other reference ever taken to
         * it. Splicing it away needed no closedness check and no size/
         * effort budget -- but DOES need the same MAX_LOCALS guard the
         * let / let* / letrec / letrec* branch above needed (flagged missing
         * here by independent code review, same bug class as that
         * branch's own now-fixed one): add_local/reserve_pending_slot
         * fail safe on overflow in the sense of never writing out of
         * bounds, but "safe" there means silently returning slot 0 and
         * printing a diagnostic, not silently doing the RIGHT thing --
         * loop_name would alias whatever real local already lives in
         * slot 0, and the OP_STORE_LOCAL below would then overwrite that
         * unrelated local's actual runtime value. Splicing needs room for
         * loop_name (1), the reloaded callee copy (1), and each binding
         * (argc) simultaneously live at once; below that, fall back to
         * building a real, separate closure for the wrapper exactly the
         * way this case always did before this landing -- correctness
         * over the optimization when the frame is already nearly full,
         * matching the let-branch's own fallback philosophy exactly. */
        val_t loop_name0 = n->as.named_let.loop_name;
        val_t bindings0  = n->as.named_let.bindings;
        int   argc0 = 0;
        for (val_t b = bindings0; vis_pair(b); b = vcdr(b)) argc0++;
        if (c->local_count + 2 + argc0 >= MAX_LOCALS) {
            val_t body0 = n->as.named_let.body;
            val_t params0 = V_NIL;
            for (val_t b = bindings0; vis_pair(b); b = vcdr(b))
                params0 = scm_cons(vcar(vcar(b)), params0);
            val_t fwd0 = V_NIL;
            while (vis_pair(params0)) { fwd0 = scm_cons(vcar(params0), fwd0); params0 = vcdr(params0); }

            Compiler outer;
            init_compiler(&outer, c, as_sym(loop_name0)->data);
            outer.chunk->arity = 0;
            add_local(&outer, loop_name0);
            mark_initialised(&outer);
            emit(&outer, OP_VOID, n->line);
            g_compile_self_tail_name    = loop_name0;
            g_compile_self_tail_mutated = body_mentions_set_target(c, body0, loop_name0);
            IRNode *loop_lambda0 = ir_lower_lambda(&outer, fwd0, body0,
                                                    as_sym(loop_name0)->data, n->line);
            ir_emit(&outer, loop_lambda0);
            emit_ab(&outer, OP_STORE_LOCAL, 0, n->line);
            emit_ab(&outer, OP_LOAD_LOCAL,  0, n->line);
            {
                int osaved = outer.local_count;
                reserve_pending_slot(&outer);
                for (val_t b = bindings0; vis_pair(b); b = vcdr(b)) {
                    IRNode *arg = ir_lower(&outer, vcar(vcdr(vcar(b))), false, n->line);
                    ir_emit(&outer, arg);
                    reserve_pending_slot(&outer);
                }
                release_pending_slots(&outer, osaved);
            }
            emit_ab(&outer, OP_TAIL_CALL, (uint8_t)argc0, n->line);
            Chunk *och = end_compiler(&outer);

            int ci = chunk_add_const(c->chunk, (val_t)(uintptr_t)och);
            emit_ab(c, OP_CLOSURE, (uint8_t)ci, n->line);
            for (int i = 0; i < outer.upval_count; i++) {
                chunk_emit(c->chunk, outer.upvals[i].is_local ? 1 : 0, n->line);
                chunk_emit(c->chunk, (uint8_t)outer.upvals[i].index,   n->line);
            }
            emit_ab(c, n->tail ? OP_TAIL_CALL : OP_CALL, 0, n->line);
            return;
        }

        /* The one real semantic difference from `outer`'s own bytecode:
         * `outer` was ALWAYS entered via a tail call from here (any
         * function's own body computing its return value via a fresh,
         * dedicated closure can always tail-call out of it) -- but once
         * spliced directly into `c`, this expression's own value is only
         * in a tail position relative to `c` when n->tail actually says
         * so. Emitting OP_TAIL_CALL unconditionally here, as `outer`
         * always safely could, would incorrectly let the loop's call
         * reuse/discard `c`'s own frame even when this named-let is just
         * a subexpression (e.g. a let-binding init) that `c` still needs
         * to keep computing after the loop returns -- so this uses
         * `n->tail ? OP_TAIL_CALL : OP_CALL`, exactly like every other
         * call site in this switch already does.
         *
         * No begin_scope/end_scope (and so no OP_SLIDE) needed either,
         * unlike the let / let* / letrec wrapper-elision case: THAT case's
         * body ends with an ordinary value left on the stack above its
         * own now-dead param locals, which nothing but an explicit
         * OP_SLIDE would otherwise reclaim. Here, the loop_name local
         * and every binding-init value are the callee and arguments of a
         * REAL call (OP_CALL/OP_TAIL_CALL) at the end -- the call
         * instruction itself already consumes all of them off the
         * runtime stack and leaves only the result, the same way it does
         * for any other call in this file; `release_pending_slots` only
         * needs to fix up `c`'s own COMPILE-TIME local_count bookkeeping
         * to match, not emit any bytecode of its own. */
        val_t loop_name = loop_name0;
        val_t bindings  = bindings0;
        val_t body      = n->as.named_let.body;
        int   argc      = argc0;  /* already counted for the guard above */

        /* Forward-order params list -- matches compile_let exactly. */
        val_t params = V_NIL;
        for (val_t b = bindings; vis_pair(b); b = vcdr(b))
            params = scm_cons(vcar(vcar(b)), params);
        val_t fwd = V_NIL;
        while (vis_pair(params)) { fwd = scm_cons(vcar(params), fwd); params = vcdr(params); }

        int saved     = c->local_count;
        int loop_slot = add_local(c, loop_name);
        mark_initialised(c);
        emit(c, OP_VOID, n->line);

        /* Arm the self-tail-call thread-local, consumed by init_compiler
         * inside ir_lower_lambda+ir_emit's own IR_LAMBDA case below --
         * body_mentions_set_target scans against `c` (the ENCLOSING
         * compiler), matching compile_let's identical call exactly. */
        g_compile_self_tail_name    = loop_name;
        g_compile_self_tail_mutated = body_mentions_set_target(c, body, loop_name);
        IRNode *loop_lambda = ir_lower_lambda(c, fwd, body,
                                               as_sym(loop_name)->data, n->line);
        ir_emit(c, loop_lambda);
        emit_ab(c, OP_STORE_LOCAL, (uint8_t)loop_slot, n->line);  /* store closure */
        emit_ab(c, OP_LOAD_LOCAL,  (uint8_t)loop_slot, n->line);  /* callee */
        reserve_pending_slot(c);  /* the callee just loaded, a pending value too */
        for (val_t b = bindings; vis_pair(b); b = vcdr(b)) {
            IRNode *arg = ir_lower(c, vcar(vcdr(vcar(b))), false, n->line);
            ir_emit(c, arg);
            reserve_pending_slot(c);
        }
        release_pending_slots(c, saved);
        /* The call above consumes exactly its own callee+args (the
         * reloaded copy at OP_LOAD_LOCAL and the argc binding values) --
         * NOT loop_slot's own underlying stack position, which is a
         * SEPARATE physical slot the reload merely copied from (found by
         * independent testing: a named-let nested as a non-first,
         * non-tail argument of an enclosing call -- e.g.
         * `(+ 1 (let loop ((i 0)) i))` -- corrupted the outer call's own
         * slot numbering, since the enclosing call's own reserve_pending_
         * slot bookkeeping assumed this expression's fresh, uncommitted
         * result would land at physical index `saved` the way every other
         * expression's result does, but the call above actually leaves it
         * one slot higher, at `saved + 1`, with the stale, now-redundant
         * closure copy still sitting at index `saved` since nothing ever
         * consumes THAT slot). In the OP_TAIL_CALL case this doesn't
         * matter -- by definition nothing else compiles into `c` after a
         * true tail position, so the stale slot is never observed -- but
         * the OP_CALL case needs an explicit OP_SLIDE(1) to reclaim it,
         * exactly the same operation (and reason) end_scope already uses
         * to remove a scope's own dead locals out from under its result:
         * move the fresh result down over the one stale slot below it. */
        if (n->tail) {
            emit_ab(c, OP_TAIL_CALL, (uint8_t)argc, n->line);
        } else {
            emit_ab(c, OP_CALL, (uint8_t)argc, n->line);
            emit_ab(c, OP_SLIDE, 1, n->line);
        }
        return;
    }
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

bool compiler_ir_self_check(val_t expr) {
    gc_inhibit_minor();

    /* init_compiler allocates a fresh ir_arena for every root Compiler
     * now (see its own comment) -- no need to set one up explicitly here
     * anymore; doing so would leak the one init_compiler already made. */
    Compiler old_c;
    init_compiler(&old_c, NULL, "<ir-check-old>");

    Compiler new_c;
    init_compiler(&new_c, NULL, "<ir-check-new>");

    int  line   = g_reader_last_line;
    bool same   = false;
    bool raised = false;

    /* compile()/ir_lower/ir_emit can scm_raise on malformed input
     * (unbound variable, bad special-form syntax, ...); without this,
     * such a longjmp would skip both arena frees below and leave
     * gc_inhibit_count unbalanced for the rest of the process -- exactly
     * the hazard compile_time_eval's own SCM_PROTECT usage documents and
     * guards against (independent security review). */
    ExnHandler h;
    SCM_PROTECT(h, {
        compile_classic(&old_c, expr, false, line);
        chunk_emit(old_c.chunk, OP_RETURN, line);

        IRNode *ir = ir_lower(&new_c, expr, false, line);
        ir_emit(&new_c, ir);
        chunk_emit(new_c.chunk, OP_RETURN, line);

        same = old_c.chunk->code_len == new_c.chunk->code_len &&
               memcmp(old_c.chunk->code, new_c.chunk->code,
                      (size_t)old_c.chunk->code_len) == 0;
        /* Also compare per-byte source lines: code-identical but
         * lines[]-divergent output is a real, silent bug class a
         * code-only comparison would miss entirely (this is exactly how
         * the IR_SEQ pop_lines bug -- see ir.h's field comment --
         * initially got past this same self-check; found by independent
         * code review). */
        if (same)
            same = memcmp(old_c.chunk->lines, new_c.chunk->lines,
                           (size_t)old_c.chunk->code_len * sizeof(int)) == 0;

        if (!same) {
            fprintf(stderr, "ir self-check: MISMATCH\n--- old (direct compile) ---\n");
            chunk_disasm(old_c.chunk, "old");
            fprintf(stderr, "--- new (ir_lower + ir_emit) ---\n");
            chunk_disasm(new_c.chunk, "new");
        }
    }, {
        raised = true;
    });

    ir_arena_free(old_c.ir_arena);
    ir_arena_free(new_c.ir_arena);
    gc_resume_minor();

    if (raised) scm_raise_val(h.exn);
    return same;
}

/* ── Tier 2.2: cheap IR optimizations (docs/thoughts/
 * performance-chez-kaappi.md §5, item 2.2) ─────────────────────────────
 *
 * ir_optimize_andor (below) recurses via ir_optimize before ir_optimize
 * itself is defined -- forward-declared here. */
static IRNode *ir_optimize(IRNode *n);

/* ir_optimize runs on an already-lowered tree, between ir_lower and
 * ir_emit, and rewrites it in place (no arena allocation -- it only ever
 * splices existing subtrees into a parent's slot or leaves a node
 * untouched, never builds a new one). Two transforms land here:
 *   - dead-branch elimination on IR_IF: `(if <const> then else)` folds
 *     to whichever branch the constant's truthiness picks, discarding
 *     the OTHER branch's bytecode and the test's own load+jump
 *     instructions entirely.
 *   - boolean simplification on IR_AND/IR_OR (ir_optimize_andor, below):
 *     a non-last constant item that already decides the short-circuit
 *     outcome truncates the list there (everything after is dead); a
 *     non-last constant item that doesn't is spliced out (side-effect-
 *     free, contributes nothing).
 *
 * This is the first Tier 2.1/2.2 transform in this codebase that
 * deliberately produces DIFFERENT bytecode than compile()'s own
 * unoptimized output for affected inputs -- every prior landing
 * (IR_SET/AND/OR/DEFINE/CALL/LAMBDA) was required to be byte-identical,
 * verified by compiler_ir_self_check's memcmp. That comparison does NOT
 * apply here (an optimizer whose output always byte-matches unoptimized
 * code isn't optimizing anything); compiler_ir_optimize_check (below)
 * verifies this pass by actually RUNNING both compiled forms and
 * comparing their RESULTS instead.
 *
 * A folded branch's own `->tail` field is already correct without
 * adjustment: ir_lower_if lowers `then`/`els` with the SAME `tail` value
 * the IR_IF node itself received, so splicing the taken branch directly
 * into the parent's slot preserves tail-position correctness for free.
 *
 * IR_LAMBDA and IR_SEQ are deliberately NOT recursed into: their bodies
 * are raw val_t at this point in the pipeline (see IRNode::as.lambda/
 * as.seq's own comments in ir.h), not yet an IR tree -- there is nothing
 * for this pass to walk until their own ir_emit cases do their
 * interleaved lower+emit walks, a separate concern from this pass
 * (IR_SEQ's own deferred design exists for the same internal-define-
 * syntax-scoping reason IR_LAMBDA's already did -- found as a real
 * ctest-only regression, invisible to compiler_ir_self_check, once
 * compile() started routing through the IR live). IR_FALLBACK is opaque
 * for the same reason (raw val_t, not a tree) and IR_VAR_REF/IR_CONST
 * are leaves -- none of these five kinds get a case below; they fall
 * through to the default `return n` unchanged. */

/* Shared by IR_AND/IR_OR: compacts the items list in place, applying two
 * safe transforms to each NON-LAST item (after recursively optimizing
 * it, so an item that itself folds down to a constant -- e.g. a nested
 * `(if #t 1 2)` -- is caught by the same pass):
 *
 *   - a constant whose truthiness already decides the whole expression's
 *     short-circuit outcome (falsy for AND, truthy for OR) truncates the
 *     list right there: it becomes the new last item, and every item
 *     after it is unreachable dead code, discarded.
 *   - a constant whose truthiness does NOT decide the outcome (truthy
 *     for AND, falsy for OR) contributes nothing but its own position in
 *     the sequence, and -- being a literal -- has no side effect either:
 *     spliced out entirely.
 *
 * The last item is never touched by either rule (guarded by `!is_last`):
 * its VALUE is what the whole expression evaluates to when reached, not
 * just a truth test, so it's kept (and still recursively optimized)
 * regardless of its own truthiness. A dropped or truncation-point item
 * is, by construction, always an IR_CONST -- literals ignore `tail`
 * entirely in their own codegen, so this never disturbs the one item
 * (the ORIGINAL last item, when no truncation happens) whose `->tail`
 * ir_lower_and/or actually set meaningfully for tail-call purposes.
 * `short_circuits_on_falsy` selects AND's vs OR's roles. */
static void ir_optimize_andor(IRNode *n, bool short_circuits_on_falsy) {
    int in_count  = n->as.andor.count;
    int out_count = 0;
    for (int i = 0; i < in_count; i++) {
        bool     is_last = (i == in_count - 1);
        IRNode  *item    = ir_optimize(n->as.andor.items[i]);
        if (!is_last && item->kind == IR_CONST) {
            bool truthy  = item->as.konst.value != V_FALSE;
            bool ends_it = short_circuits_on_falsy ? !truthy : truthy;
            if (ends_it) {
                n->as.andor.items[out_count++] = item;
                break;  /* truncate: this becomes the new last item */
            }
            continue;   /* provably doesn't end it, no side effect: drop */
        }
        n->as.andor.items[out_count++] = item;
    }
    n->as.andor.count = out_count;
}

static IRNode *ir_optimize(IRNode *n) {
    switch (n->kind) {
    case IR_IF: {
        IRNode *test = ir_optimize(n->as.iff.test);
        if (test->kind == IR_CONST) {
            bool truthy = test->as.konst.value != V_FALSE;
            return ir_optimize(truthy ? n->as.iff.then : n->as.iff.els);
        }
        n->as.iff.test = test;
        n->as.iff.then = ir_optimize(n->as.iff.then);
        n->as.iff.els  = ir_optimize(n->as.iff.els);
        return n;
    }
    /* IR_SEQ is NOT recursed into, same reasoning as IR_LAMBDA just
     * below: its body is raw val_t at this point in the pipeline (see
     * IRNode::as.seq's own comment in ir.h) -- there's no pre-lowered
     * tree here to optimize until ir_emit's own interleaved walk builds
     * one, item by item. Falls to the default `return n` unchanged. */
    case IR_SET:
        n->as.set.value = ir_optimize(n->as.set.value);
        return n;
    case IR_AND: ir_optimize_andor(n, true);  return n;
    case IR_OR:  ir_optimize_andor(n, false); return n;
    case IR_DEFINE:
        n->as.def.value = ir_optimize(n->as.def.value);
        return n;
    case IR_CALL:
        n->as.call.callee = ir_optimize(n->as.call.callee);
        for (int i = 0; i < n->as.call.argc; i++)
            n->as.call.args[i] = ir_optimize(n->as.call.args[i]);
        return n;
    default:
        return n;
    }
}

bool compiler_ir_optimize_check(val_t expr) {
    gc_inhibit_minor();

    /* init_compiler allocates a fresh ir_arena for every root Compiler
     * now (see its own comment) -- no need to set one up explicitly. */
    Compiler old_c;
    init_compiler(&old_c, NULL, "<ir-opt-check-old>");

    Compiler new_c;
    init_compiler(&new_c, NULL, "<ir-opt-check-new>");

    int  line   = g_reader_last_line;
    bool equal  = false;
    bool raised = false;
    val_t exn   = V_FALSE;

    ExnHandler h;
    SCM_PROTECT(h, {
        compile_classic(&old_c, expr, false, line);
        chunk_emit(old_c.chunk, OP_RETURN, line);
        old_c.chunk->upval_count = 0;
        BcClosure *old_cl = vm_make_closure(old_c.chunk, 0);

        IRNode *ir = ir_lower(&new_c, expr, false, line);
        ir = ir_optimize(ir);
        ir_emit(&new_c, ir);
        chunk_emit(new_c.chunk, OP_RETURN, line);
        new_c.chunk->upval_count = 0;
        BcClosure *new_cl = vm_make_closure(new_c.chunk, 0);

        vm_push(vptr(old_cl));
        val_t old_result = vm_run(old_cl, 0);
        vm_push(vptr(new_cl));
        val_t new_result = vm_run(new_cl, 0);

        equal = scm_equal(old_result, new_result);
    }, {
        raised = true;
        exn    = h.exn;
    });

    ir_arena_free(old_c.ir_arena);
    ir_arena_free(new_c.ir_arena);
    gc_resume_minor();

    if (raised) scm_raise_val(exn);
    return equal;
}

/* Tier 2.3 local-inliner positive-firing check: compiler_ir_optimize_check
 * above already differentially verifies inlining's CORRECTNESS for free
 * (inlining's decision logic lives entirely inside ir_emit's IR_CALL case,
 * not in ir_optimize, so the live IR path it already runs exercises it
 * whenever an input expression happens to trigger it) -- but a pure
 * result-comparison check can pass even with inlining silently disabled
 * entirely (a no-op fallback to an ordinary call is still "correct"). This
 * is the proof the feature actually exists: for an expr whose eligible
 * candidate is called `call_count` times, a genuinely-firing inliner
 * duplicates the candidate's body at each call site instead of a compact
 * OP_LOAD_LOCAL+OP_CALL sequence, so the live-IR chunk's compiled bytecode
 * length must come out LARGER than classic compilation's -- a simple,
 * robust, opcode-format-agnostic signal that doesn't require decoding
 * individual instructions. Same compile-both-sides shape as
 * compiler_ir_optimize_check, minus the result-equality assertion (this
 * function's caller is expected to also call compiler_ir_optimize_check
 * on the same expr for that). */
/* Tier 2.4: sums code_len recursively across `c` and every chunk nested
 * (transitively) in its constant pool -- the TOTAL compiled bytecode size
 * of the whole unit, regardless of which particular chunk boundary any of
 * it happens to live behind. Wrapper elision (this same landing) merely
 * RELOCATES a let / let-star / letrec wrapper's own body bytecode from a nested
 * child chunk into its parent chunk directly -- it does not duplicate
 * anything, so it leaves this total essentially unchanged (a few bytes
 * either way from the closure-creation opcodes it removes). Genuine
 * named-candidate duplication (the thing this check actually exists to
 * prove) does grow this total, because the candidate's body bytecode then
 * appears more than once somewhere in the whole tree. This replaces an
 * earlier single-chunk-comparison approach (see git history) that assumed
 * a let-shaped expr's interesting body always lived at a fixed, predictable
 * nesting depth -- an assumption wrapper elision broke, since the IR side
 * of that comparison could end up anchored on an unrelated inner closure's
 * own chunk instead once the previously-reliable outer wrapper chunk
 * stopped being created at all. */
static int chunk_total_code_len(Chunk *c) {
    int total = c->code_len;
    for (int i = 0; i < c->const_len; i++)
        if (vis_type(c->constants[i], T_CHUNK))
            total += chunk_total_code_len(vunptr(Chunk, c->constants[i]));
    return total;
}

bool compiler_ir_inline_fired_check(val_t expr) {
    gc_inhibit_minor();

    Compiler old_c;
    init_compiler(&old_c, NULL, "<inline-fired-check-old>");
    Compiler new_c;
    init_compiler(&new_c, NULL, "<inline-fired-check-new>");

    int  line = g_reader_last_line;
    bool grew = false;

    ExnHandler h;
    SCM_PROTECT(h, {
        compile_classic(&old_c, expr, false, line);
        IRNode *ir = ir_lower(&new_c, expr, false, line);
        ir_emit(&new_c, ir_optimize(ir));
        /* Tier 2.4: compare TOTAL compiled bytecode size across the whole
         * unit on each side (see chunk_total_code_len's own comment) --
         * robust to wrapper elision relocating a let/letrec wrapper's own
         * body bytecode up into its parent chunk, which a single fixed-
         * nesting-depth chunk comparison is not. */
        int old_len = chunk_total_code_len(old_c.chunk);
        int new_len = chunk_total_code_len(new_c.chunk);
        grew = new_len > old_len;
    }, {
        ir_arena_free(old_c.ir_arena);
        ir_arena_free(new_c.ir_arena);
        gc_resume_minor();
        scm_raise_val(h.exn);
    });

    ir_arena_free(old_c.ir_arena);
    ir_arena_free(new_c.ir_arena);
    gc_resume_minor();
    return grew;
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
