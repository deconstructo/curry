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

    /* Reserved for a future widening pass (see Tier 2.1 plan's deferred
     * list) -- not constructed by ir_lower in this landing. Listed here
     * so the schema doesn't need another revision when calls/lambda get
     * lowered natively. */
    IR_LAMBDA,
    IR_CALL,
    IR_SELF_TAIL_CALL,
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
        /* pop_lines[i] is the line compile_seq would stamp on the
         * OP_POP emitted after items[i] (i.e. the SEQ SPINE cons cell's
         * own hdr.flags at that position) -- deliberately NOT the same
         * as items[i]->line, which is whatever line ir_lower recursively
         * refined the ITEM's own subtree to internally (e.g. from the
         * item pair's own hdr.flags, which can differ from the spine
         * cell's when the item came from a macro-rebuilt cons whose
         * spine cell has hdr.flags == 0 but whose captured/spliced
         * subexpression retains its original non-zero flags).
         * compile_seq's own `line` variable is never affected by
         * whatever compile() does internally to ITS copy of `line`
         * (plain C pass-by-value) -- collapsing this into a single
         * per-item line and using it for OP_POP produced a real,
         * differential-self-check-invisible divergence (code-identical,
         * lines[]-divergent) for macro-synthesized begin spines, found
         * by independent code review during Tier 2.1 development. */
        struct { IRNode **items; int *pop_lines; int count; } seq;  /* IR_SEQ */
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
    } as;
};

/* ── Arena ────────────────────────────────────────────────────────────── */

typedef struct IRArena IRArena;

IRArena *ir_arena_new(void);
void     ir_arena_free(IRArena *a);
void    *ir_arena_alloc(IRArena *a, size_t size);

/* Convenience: allocate and zero-init one IRNode from the arena. */
IRNode  *ir_node_new(IRArena *a, IRKind kind, bool tail, int line);

#endif /* CURRY_IR_H */
