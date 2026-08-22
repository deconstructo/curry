#ifndef CURRY_IR_H
#define CURRY_IR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "value.h"

/*
 * ir.h — the Tier 2.1 tree IR (docs/thoughts/performance-chez-kaappi.md
 * §5, item 2.1) and its arena allocator.
 *
 * This header intentionally holds ONLY the node representation and the
 * arena -- data structures with no dependency on the compiler's internal
 * state. The two passes that actually produce/consume IRNode trees
 * (ir_lower, ir_emit) live in compiler.c itself, not here, because they
 * need direct access to Compiler's private helpers (resolve_local,
 * resolve_upvalue, chunk_add_const, emit/emit_jump/patch_jump, and
 * compile() itself for IR_FALLBACK) which are static to that translation
 * unit. Keeping this split -- types here, passes in compiler.c -- avoids
 * exposing Compiler's internals through a second header just to satisfy
 * a file-layout preference from the original plan.
 *
 * Lifetime: one IRArena per top-level compiled form, shared by every
 * nested Compiler in that form's compile tree (root Compiler creates it;
 * every child Compiler inherits the same pointer -- see compiler.c's
 * init_compiler). Freed once that top-level form's Chunk is fully
 * emitted. IRNode trees are never reachable from Scheme code and are
 * never GC-traced -- purely compiler-transient scratch memory.
 */

typedef enum {
    /* Leaves */
    IR_CONST,       /* a self-evaluating value, or a quoted datum */
    IR_VAR_REF,     /* a variable reference -- see this kind's own comment
                     * on IRNode::as.var_ref below for why resolution is
                     * deferred to ir_emit rather than decided here */
    IR_FALLBACK,    /* not-yet-lowered subtree: ir_emit calls compile()
                     * on the raw val_t directly, byte-for-byte identical
                     * to what would have happened had ir_lower never run
                     * at all. This is what lets IR_IF/IR_SEQ cover forms
                     * whose subexpressions include calls, lambdas, etc.
                     * that this landing doesn't lower natively yet -- see
                     * ir.h's header comment and the Tier 2.1 plan's
                     * "explicitly deferred" list. */

    /* Combinators */
    IR_IF,
    IR_SEQ,         /* begin; only the last element inherits `tail` */
    IR_SET,         /* set! -- widened in the second landing */
    IR_AND,         /* widened in the second landing */
    IR_OR,          /* widened in the second landing */
    IR_DEFINE,      /* (define sym expr) only -- widened in the third
                     * landing; the (define (f params...) body...) lambda-
                     * sugar case still falls back whole, see
                     * ir_lower_define's comment */
    IR_CALL,        /* ordinary call, fused-global call, AND self-tail-
                     * call -- widened in the fourth landing. All three
                     * are ONE node kind, not three: which of them a given
                     * call site actually is depends on resolve_local/
                     * resolve_upvalue(head), which (like every other
                     * ordering-sensitive resolution in this IR) can only
                     * be decided at ir_emit time -- see this kind's own
                     * `call` field comment below. There is deliberately
                     * no separate IR_SELF_TAIL_CALL kind: it is a
                     * classification IR_CALL's own ir_emit case makes
                     * internally, exactly mirroring how compile_call
                     * itself decides between OP_SELF_TAIL_CALL/
                     * OP_CALL_GLOBAL/OP_CALL at one call site, not a
                     * decision ir_lower could make ahead of time. */
    IR_LAMBDA,      /* widened in the fifth landing -- see this kind's own
                     * `lambda` field comment below for why its body is
                     * stored RAW, not pre-lowered into an IR tree. */
    IR_NAMED_LET,   /* widened in the sixth landing, ALONGSIDE plain
                     * let / let-star / letrec / letrec-star -- but those
                     * four need no new node kind at all: they're pure
                     * ir_lower-time desugaring into
                     * IR_CALL{callee=IR_LAMBDA{...}}
                     * (see ir_lower_let/ir_lower_let_star/
                     * ir_lower_letrec, compiler.c), reusing IR_LAMBDA's
                     * already-verified machinery unchanged. Named let is
                     * the one exception: it needs the self-tail-call
                     * thread-local side-channel (g_compile_self_tail_
                     * name/mutated, compiler.c) armed at exactly the
                     * right moment, an ordering-sensitive side effect
                     * that -- like every other one in this IR -- can
                     * only happen at ir_emit time. See this kind's own
                     * `named_let` field comment below. */
} IRKind;

typedef struct IRNode IRNode;

struct IRNode {
    IRKind kind;
    bool   tail;
    int    line;
    union {
        /* quoted: true for a (quote ...) datum, compiled via emit_const
         * unconditionally (matching compile()'s S_QUOTE case, which never
         * special-cases V_NIL/V_TRUE/V_FALSE/V_VOID); false for a bare
         * self-evaluating atom, compiled via compile()'s own top-of-
         * dispatch special-casing of those four immediates into dedicated
         * opcodes before ever considering emit_const. Same underlying
         * value, genuinely different codegen -- collapsing this
         * distinction produced a real, caught-by-test bytecode mismatch
         * for (quote ()) during Tier 2.1 development (emits OP_CONST in
         * compile() today, would have wrongly emitted OP_NIL). */
        struct { val_t value; bool quoted; } konst;          /* IR_CONST */
        /* Deliberately just the raw symbol, NOT a pre-resolved local
         * slot / upvalue index / constant-pool index. An earlier version
         * resolved eagerly here (matching the original Tier 2.1 plan's
         * "resolution already decided during lowering" framing) -- caught
         * by the differential self-check as a real bug: resolve_upvalue
         * and chunk_add_const both have ordering-sensitive side effects
         * (they assign indices in first-seen order), and ir_lower builds
         * the WHOLE tree before ir_emit walks any of it, so a natively-
         * lowered var-ref sibling that resolves eagerly during lowering
         * can register its upvalue/constant-pool slot BEFORE an earlier
         * IR_FALLBACK sibling gets a chance to (fallback nodes only touch
         * the chunk when ir_emit finally calls compile() on them) --
         * silently reordering the constant pool / upvalue table relative
         * to the original interleaved compile() and breaking the byte-
         * identical guarantee. Fix: resolve_local/resolve_upvalue/
         * chunk_add_const only ever run at ir_emit time now (via
         * emit_load), in the same left-to-right tree-walk order ir_emit
         * itself proceeds in -- which matches original compile()'s
         * interleaved order exactly, natively-lowered and fallback nodes
         * alike. Eager resolution at lower time can only be safely
         * reintroduced once no IR_FALLBACK escape hatch remains for
         * anything that could appear as a sibling. */
        struct { val_t name; } var_ref;                       /* IR_VAR_REF */
        struct { val_t expr; } fallback;                      /* IR_FALLBACK */
        struct { IRNode *test, *then, *els; } iff;             /* IR_IF */
        /* `body` is deliberately the RAW, unprocessed val_t list -- NOT
         * pre-lowered into an array of IRNodes the way this field
         * originally worked. Reason (found via a real, ctest-only
         * regression -- invisible to compiler_ir_self_check's hand-
         * picked cases -- once compile() actually started routing
         * through the IR live): an internal `(define-syntax ...)` inside
         * a `(begin ...)` registers into the enclosing Compiler's
         * syntax_locals table only when it's EMITTED (IR_FALLBACK's own
         * ir_emit calls compile(), which calls compile_define_syntax) --
         * never when it's merely LOWERED. Eagerly lowering every item in
         * one pass, before any of them are emitted, meant a LATER item
         * using that macro was classified (via classify_head) before the
         * registration existed, misreading the macro use as an ordinary
         * call. ir_emit's own IR_SEQ case now does the same interleaved
         * lower-then-emit walk per item that IR_LAMBDA's body walk
         * already used for exactly this reason (see IR_LAMBDA's own
         * `lambda` field comment) -- including the same per-spine-cell
         * `hdr.flags` line tracking a differential-self-check-invisible
         * bug (macro-synthesized begin spines) previously required here.
         * Consequence: ir_optimize can no longer reach INSIDE a `begin`
         * the way it once could (there's no pre-lowered IRNode tree to
         * walk until ir_emit builds one, item by item) -- the same
         * accepted tradeoff IR_LAMBDA's own body already has. */
        struct { val_t body; } seq;  /* IR_SEQ */
        /* Deliberately just the raw symbol, same reasoning as
         * IR_VAR_REF's own `name` field above: emit_store (the store-
         * side counterpart of emit_load) has the identical ordering-
         * sensitive resolve_local/resolve_upvalue side effects, so
         * resolution happens at ir_emit time via emit_store itself, not
         * here. */
        struct { val_t name; IRNode *value; } set;             /* IR_SET */
        /* Shared by IR_AND and IR_OR -- both are "a list of tested
         * expressions with early-exit," structurally identical to
         * IR_SEQ's items array, but codegen is jump-based (short-circuit)
         * rather than OP_POP-separated sequencing, so pop_lines has no
         * meaning here; a separate struct avoids a meaningless field on
         * every AND/OR node. Each items[i]'s own ->tail was set during
         * lowering to match compile_and/compile_or's own per-item tail
         * argument exactly -- see ir_lower_and/ir_lower_or's comments for
         * the (deliberately preserved, not "fixed") asymmetry between the
         * two: `and` propagates tail to its last item, `or` never does. */
        struct { IRNode **items; int count; } andor;      /* IR_AND, IR_OR */
        /* Deliberately just the raw symbol, same reasoning as IR_SET's own
         * `name` field above: emit_define_store has the same ordering-
         * sensitive add_local/resolve_local side effects (it also mutates
         * c->locals/c->local_count for a brand-new internal define), so
         * resolution/declaration happens at ir_emit time via
         * emit_define_store itself, not here. Only covers `(define sym
         * expr)`; ir_lower_define falls the whole form back to
         * IR_FALLBACK when the target is a pair (lambda sugar), since
         * that requires compile_lambda -- not yet IR-lowered (see
         * IR_LAMBDA above). */
        struct { val_t name; IRNode *value; } def;             /* IR_DEFINE */
        /* `head` is deliberately the raw, unresolved val_t (a symbol, or
         * any other expression when the callee position isn't a bare
         * name) -- classify_head/compile_call's own self-tail/fused-
         * global/generic classification runs entirely on resolve_local/
         * resolve_upvalue(head), which must happen at ir_emit time, in
         * ir_emit's own left-to-right walk order, for the same reason
         * IR_VAR_REF/IR_SET/IR_DEFINE defer their own resolution there
         * (see IR_VAR_REF's comment above) -- so ir_lower_call must NOT
         * pre-decide the classification, only hand ir_emit everything it
         * needs to decide it itself. `callee` is `head` already run
         * through ir_lower (cheap and side-effect-free at lower time,
         * same as any other subtree) -- used only by the generic-call
         * branch, when classification falls through past both the
         * self-tail and fused-global fast paths; built eagerly anyway so
         * ir_emit never needs to call ir_lower mid-emit. `args` are
         * always lowered non-tail, matching every one of compile_call's
         * three branches. */
        struct { val_t head; IRNode *callee; IRNode **args; int argc; } call; /* IR_CALL */
        /* `params`/`body` are deliberately the RAW, unprocessed val_t
         * forms compile_lambda itself takes -- NOT pre-lowered into an IR
         * tree during ir_lower, unlike every other node's subexpressions.
         * Reason: a lambda body is compiled against a brand-new child
         * Compiler (fresh locals[]/upvals[]/syntax_locals[], its own
         * Chunk) that doesn't exist yet at ir_lower time, and doesn't
         * meaningfully exist as a *concept* until ir_emit is ready to
         * create it -- resolve_upvalue's own capture-chain walk
         * (compiler.c) needs `child.enclosing` to be the REAL, live
         * parent Compiler stack frame, not something reconstructible
         * from an arena snapshot. Worse, an internal (define-syntax ...)
         * inside the body registers into that child's OWN syntax_locals
         * table (compile_define_syntax, mid-walk) for LATER forms in the
         * same body to see via classify_head's macro lookup -- if the
         * whole body were pre-lowered in one pass against some
         * proxy/placeholder Compiler, a later form using an earlier
         * form's local macro would classify wrong (misread as an
         * ordinary call instead of expanding it), since the proxy would
         * never see that registration in the right order. ir_emit's
         * IR_LAMBDA case creates the real child Compiler, runs the same
         * prescan compile_lambda uses (lambda_prescan, compiler.c), then
         * walks body forms one at a time -- ir_lower(&child, form, ...)
         * immediately followed by ir_emit(&child, that_node) -- exactly
         * interleaved the way compile_seq's own per-form compile() call
         * is, so a macro registered by form i is visible by the time
         * form i+1 is classified. `name` is the JIT/debug label
         * (compile_lambda's own `name` parameter) -- NULL for an
         * anonymous (lambda ...), the bound symbol's C string for
         * `(define (f ...) ...)` sugar. */
        struct { val_t params; val_t body; const char *name; } lambda; /* IR_LAMBDA */
        /* All three fields raw/unprocessed, same reasoning as
         * IR_LAMBDA's own `lambda` field above -- ir_emit's IR_NAMED_LET
         * case builds the whole "zero-arg outer wrapper" structure
         * (compile_let's own comment on its named-let branch,
         * compiler.c, has the full shape) itself, arming
         * g_compile_self_tail_name/g_compile_self_tail_mutated
         * immediately before lowering+emitting the inner loop lambda --
         * nothing here can be pre-built during ir_lower. `bindings` is
         * the raw `((x v) ...)` list (not yet split into params/inits);
         * `body` is the loop's own raw body forms. */
        struct { val_t loop_name; val_t bindings; val_t body; } named_let; /* IR_NAMED_LET */
    } as;
};

/* ── Arena ────────────────────────────────────────────────────────────── */

typedef struct IRArena IRArena;

IRArena *ir_arena_new(void);
void     ir_arena_free(IRArena *a);
void    *ir_arena_alloc(IRArena *a, size_t size);

/* Tier 2.3 local inliner: claims one unit of this compile tree's shared
 * inline-effort budget. Returns false (no claim made) once exhausted, so
 * callers fall back to a normal, correct, just-not-inlined call -- see
 * struct IRArena's own comment in ir.c for why this lives on the arena
 * rather than on Compiler. */
bool ir_arena_take_inline_budget(IRArena *a);

/* Convenience: allocate and zero-init one IRNode from the arena. */
IRNode  *ir_node_new(IRArena *a, IRKind kind, bool tail, int line);

#endif /* CURRY_IR_H */
