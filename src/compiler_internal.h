#ifndef CURRY_COMPILER_INTERNAL_H
#define CURRY_COMPILER_INTERNAL_H

/*
 * Private structures and helpers shared between the files that together
 * implement compiler.c's old, single-file compiler:
 *
 *   compiler.c         -- foundation: Compiler lifecycle, emit/scope/local/
 *                          upvalue helpers, and the public API entry points
 *                          (compiler_compile, compiler_compile_script, ...).
 *   compiler_classic.c -- the classic (pre-IR) compile_ family, compile(),
 *                          and classify_head.
 *   ir_lower.c          -- Tier 2.1 IR lowering (ir_lower/ir_lower_*) and
 *                          Tier 2.2 optimization (ir_optimize).
 *   ir_emit.c           -- Tier 2.1 IR bytecode emission (ir_emit) plus the
 *                          Tier 2.3 local inliner (ir_emit_inline_call).
 *   compiler_ir_checks.c -- the differential self-check test infrastructure
 *                          (compiler_ir_self_check/compiler_ir_optimize_check/
 *                          compiler_ir_inline_fired_check).
 *
 * This is the same pattern src/runtime_internal.h uses for eval.c/runtime.c:
 * what used to be file-scope `static` symbols in one big compiler.c are
 * declared here as ordinary extern symbols so they can cross the new file
 * boundaries. Not part of the public API -- do not include outside these
 * five files. The real `Compiler` struct body lives here; compiler.h only
 * ever exposes it as an opaque `typedef struct Compiler Compiler;` to
 * external callers (e.g. the LLVM codegen retargeting work).
 */

#include "compiler.h"
#include "chunk.h"
#include "ir.h"
#include "value.h"

/* ── Compiler scope structures ───────────────────────────────────────── */

#define MAX_LOCALS  256
#define MAX_UPVALS  256
#define MAX_SYNTAX_LOCALS 64

/* Tier 2.3 local inliner: guards against mutual/indirect recursion
 * inlining itself arbitrarily deep -- see currently_inlining's own
 * comment in compiler.c. */
#define MAX_INLINE_DEPTH 64

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

struct Compiler {
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
};

/* Every compound-form classification compile()'s own dispatch can reach,
 * in the same order compile() tests for them. Shared with ir_lower's
 * dispatch (classify_head is the single source of truth for "is this an
 * ordinary call or a special form/macro use"). */
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

/* ── Thread-locals shared across file boundaries ─────────────────────────
 * (defined in compiler.c; see that file for the full comments on each). */

/* Set by compile_let's named-let branch right before compiling the loop
 * lambda's own body, consumed-and-cleared by init_compiler. */
extern CURRY_THREAD_LOCAL val_t g_compile_self_tail_name;
extern CURRY_THREAD_LOCAL bool  g_compile_self_tail_mutated;

/* Tier 2.3: guards against mutual/indirect recursion inlining itself
 * arbitrarily deep -- see currently_inlining's own comment in compiler.c. */
extern CURRY_THREAD_LOCAL val_t g_inlining_bodies[MAX_INLINE_DEPTH];
extern CURRY_THREAD_LOCAL int   g_inlining_depth;

/* ── compiler.c: lifecycle / emit / scope / locals / upvalues ──────────── */

void   init_compiler(Compiler *c, Compiler *enc, const char *name);
Chunk *end_compiler(Compiler *c);

void emit(Compiler *c, uint8_t op, int line);
void emit_const(Compiler *c, val_t v, int line);
void emit_ab(Compiler *c, uint8_t op, uint8_t a, int line);
void emit_abc(Compiler *c, uint8_t op, uint8_t a, uint8_t b, int line);
int  emit_jump(Compiler *c, uint8_t op, int line);
void patch_jump(Compiler *c, int placeholder);

void begin_scope(Compiler *c);
void end_scope(Compiler *c, int line);

int  add_syntax_local(Compiler *c, val_t name, val_t transformer);
bool resolve_syntax_local(Compiler *c, val_t name, val_t *out_transformer);

int  add_local(Compiler *c, val_t name);
int  reserve_pending_slot(Compiler *c);
void release_pending_slots(Compiler *c, int saved_local_count);
void mark_initialised(Compiler *c);

int  add_upvalue(Compiler *c, int index, bool is_local, val_t name);
int  resolve_local(Compiler *c, val_t name);
void poison_known(Compiler *c, val_t name);
int  resolve_upvalue(Compiler *c, val_t name);
bool is_upvalue_reachable(Compiler *c, val_t name);

void emit_load(Compiler *c, val_t name, int line);
void emit_store(Compiler *c, val_t name, int line);

int  compile_params(Compiler *c, val_t params);
void lambda_prescan(Compiler *c, val_t body, int line);

void require_min_args(val_t args, int min, const char *form_name);
bool currently_inlining(val_t body);

/* ── compiler_classic.c: classic (pre-IR) dispatcher ────────────────────── */

void compile(Compiler *c, val_t expr, bool tail, int line);
void compile_seq(Compiler *c, val_t list, bool tail, int line);
void compile_classic(Compiler *c, val_t expr, bool tail, int line);

SpecialForm classify_head(Compiler *c, val_t head, val_t args,
                           val_t *transformer_out);

bool is_quoted_symbol(val_t expr, val_t *out_sym);
void emit_define_store_novoid(Compiler *c, val_t name, int line);
void emit_define_store(Compiler *c, val_t name, int line);
bool is_keyword_symbol(val_t v);
bool body_mentions_set_target(Compiler *c, val_t body, val_t name);

/* ── ir_lower.c: Tier 2.1 lowering + Tier 2.2 optimization ──────────────── */

IRNode *ir_lower(Compiler *c, val_t expr, bool tail, int line);
IRNode *ir_optimize(IRNode *n);
IRNode *ir_lower_lambda(Compiler *c, val_t params, val_t body,
                         const char *name, int line);

int  ir_count_ast_nodes(val_t expr, int budget);
bool params_proper_arity(val_t params, int *argc_out);
bool expr_contains_symbol(val_t expr, val_t name);
bool body_contains_symbol(val_t body, val_t name);

/* ── ir_emit.c: Tier 2.1 bytecode emission ──────────────────────────────── */

void ir_emit(Compiler *c, IRNode *n);

#endif /* CURRY_COMPILER_INTERNAL_H */
