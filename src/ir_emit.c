/*
 * ir_emit.c — Tier 2.1 IR bytecode emission + Tier 2.3 local inliner.
 *
 * Split out of the old single-file compiler.c (pure code motion, no
 * behavior change -- see compiler.c's own header comment for the full
 * five-way split). Holds ir_emit (the ~700-line function that walks an
 * IR tree produced by ir_lower.c and emits VM bytecode, including the
 * open-coding table for car/cdr/cons/pair?/null?/arithmetic/comparison
 * ops and this IS the point where variable references finally resolve to
 * local/upvalue/global slots) and ir_emit_inline_call (the Tier 2.3 local
 * inliner: splices a known-closed candidate lambda's body directly into
 * the calling Compiler's own instruction stream instead of a real call).
 *
 * Shared symbols (the real `Compiler` struct, foundation helpers defined in
 * compiler.c, classify_head/compile()/emit_define_store defined in
 * compiler_classic.c, ir_lower/ir_optimize defined in ir_lower.c) are
 * declared in compiler_internal.h.
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

/* Tier 2.3 local inliner. IR_LAMBDA's ir_emit case (below) creates a real
 * child Compiler, compiles the lambda, and tears the child down
 * (end_compiler) before returning -- so child.upval_count is gone by the
 * time a caller like IR_DEFINE's case, which just called ir_emit(c,
 * n->as.def.value), would want to inspect it. Set as literally the last
 * statement in IR_LAMBDA's case before it returns, mirroring
 * g_compile_self_tail_name/g_compile_self_tail_mutated (compiler.c) -- the
 * same "child Compiler learned something only the parent's next statement
 * needs" hand-off shape, at the same granularity (read immediately after
 * the one ir_emit call that produced it, before any other ir_emit call
 * could overwrite it). child.upval_count == 0 means the lambda captured no
 * free variables from its defining scope -- the soundness condition that
 * makes it safe to re-lower its raw params/body unchanged at a different
 * call site later (see KnownLambda's comment): with no free variables,
 * there is nothing for re-lowering elsewhere to mis-resolve, since every
 * non-param name it references either fails to resolve locally at every
 * level (falls through to global lookup, which is scope-independent) or is
 * a global to begin with. Deliberately NOT reset to some sentinel after
 * IR_LAMBDA's case -- every caller that cares reads it immediately after
 * its own single ir_emit(..., IR_LAMBDA) call, never after any other node
 * kind, so a stale value from an unrelated earlier IR_LAMBDA can never be
 * misread as this one's. Entirely local to this file: both the one write
 * (IR_LAMBDA's case) and the two reads (IR_DEFINE's case, ir_emit_
 * inline_call's own eligibility check) live here. */
static CURRY_THREAD_LOCAL int g_last_lambda_upval_count = -1;

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

void ir_emit(Compiler *c, IRNode *n) {
    /* ir_emit and ir_emit_inline_call (both call sites of the latter are
     * inside this function, so guarding ir_emit's own entry covers the
     * whole recursive tree) recurse into each other once per binding of
     * a flat let* / letrec* / do chain -- unlike eval()'s own goto-tail
     * trampoline, nothing here reuses a C frame, so a few hundred
     * sequential bindings SIGSEGVs the whole process once the real C
     * stack is exhausted (issue #125). check_c_stack_depth (runtime.c)
     * is the same guard eval() already has for its own unbounded
     * recursion -- shared rather than duplicated, since both consume
     * the same physical C stack on the same thread. */
    check_c_stack_depth("compile");
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
         * loop_name (1), the reloaded callee copy (1), each binding's
         * ALREADY-computed init value (argc, held in a pending slot until
         * the reload below re-pushes it -- see issue #120's own fix,
         * which introduced this second `argc`-sized reservation to
         * correctly compile each init in the caller's own enclosing
         * scope), and each binding's RELOADED copy (argc again)
         * simultaneously live at once: 2*argc + 2, not argc + 2. Below
         * that, fall back to building a real, separate closure for the
         * wrapper exactly the way this case always did before this
         * landing -- correctness over the optimization when the frame is
         * already nearly full, matching the let-branch's own fallback
         * philosophy exactly. (Independent security review, #120's own
         * follow-up: reserve_pending_slot's own bounds check keeps this
         * currently inert -- nothing is compiled between the two
         * reservation loops and release_pending_slots for anything to
         * observe a wrong local_count through -- but the guard's
         * arithmetic should still describe the peak it actually gates,
         * not the pre-#120 shape's smaller one, since any future change
         * compiling something in between would turn a latent
         * miscounting into a live one.) */
        val_t loop_name0 = n->as.named_let.loop_name;
        val_t bindings0  = n->as.named_let.bindings;
        int   argc0 = 0;
        for (val_t b = bindings0; vis_pair(b); b = vcdr(b)) argc0++;
        /* Validates every binding before either branch below destructures
         * it -- see ir_lower_let's identical comment (issue #124):
         * (let loop ((a)) 1) previously SIGSEGV'd here (and in the
         * splice fast path below) on a binding with no init. */
        for (val_t b = bindings0; vis_pair(b); b = vcdr(b))
            require_min_args(vcar(b), 2, "let");
        if (c->local_count + 2 * argc0 + 2 >= MAX_LOCALS) {
            val_t body0 = n->as.named_let.body;
            val_t params0 = V_NIL;
            for (val_t b = bindings0; vis_pair(b); b = vcdr(b))
                params0 = scm_cons(vcar(vcar(b)), params0);
            val_t fwd0 = V_NIL;
            while (vis_pair(params0)) { fwd0 = scm_cons(vcar(params0), fwd0); params0 = vcdr(params0); }

            /* Outer wrapper, arity = argc0 instead of 0: issue #120,
             * same fix and same reasoning as compile_let's own outer-
             * wrapper (compiler_classic.c) -- inits must be compiled by
             * `c` (the true enclosing scope), not by `outer` itself,
             * so a reference to loop_name0 within an init expression
             * can never resolve to this named-let's own about-to-be-
             * created self-reference. */
            Compiler outer;
            init_compiler(&outer, c, as_sym(loop_name0)->data);
            outer.chunk->arity = compile_params(&outer, fwd0);
            add_local(&outer, loop_name0);
            mark_initialised(&outer);
            emit(&outer, OP_VOID, n->line);
            g_compile_self_tail_name    = loop_name0;
            g_compile_self_tail_mutated = body_mentions_set_target(c, body0, loop_name0);
            IRNode *loop_lambda0 = ir_lower_lambda(&outer, fwd0, body0,
                                                    as_sym(loop_name0)->data, n->line);
            ir_emit(&outer, loop_lambda0);
            emit_ab(&outer, OP_STORE_LOCAL, (uint8_t)argc0, n->line);
            emit_ab(&outer, OP_LOAD_LOCAL,  (uint8_t)argc0, n->line);
            for (int i = 0; i < argc0; i++)
                emit_ab(&outer, OP_LOAD_LOCAL, (uint8_t)i, n->line);
            emit_ab(&outer, OP_TAIL_CALL, (uint8_t)argc0, n->line);
            Chunk *och = end_compiler(&outer);

            int ci = chunk_add_const(c->chunk, (val_t)(uintptr_t)och);
            emit_ab(c, OP_CLOSURE, (uint8_t)ci, n->line);
            for (int i = 0; i < outer.upval_count; i++) {
                chunk_emit(c->chunk, outer.upvals[i].is_local ? 1 : 0, n->line);
                chunk_emit(c->chunk, (uint8_t)outer.upvals[i].index,   n->line);
            }
            /* Compile each init value in c's OWN scope, pushed after
             * the callee -- the true enclosing environment R7RS
             * mandates. See issue #120. */
            {
                int osaved = c->local_count;
                reserve_pending_slot(c);  /* the callee just pushed via OP_CLOSURE above */
                for (val_t b = bindings0; vis_pair(b); b = vcdr(b)) {
                    IRNode *arg = ir_lower(c, vcar(vcdr(vcar(b))), false, n->line);
                    ir_emit(c, arg);
                    reserve_pending_slot(c);
                }
                release_pending_slots(c, osaved);
            }
            emit_ab(c, n->tail ? OP_TAIL_CALL : OP_CALL, (uint8_t)argc0, n->line);
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

        int saved = c->local_count;

        /* Compile each init value FIRST, into temporary pending slots,
         * BEFORE loop_name exists anywhere in c's own locals table --
         * issue #120: the previous shape add_local'd loop_name (below)
         * BEFORE compiling the inits, so a reference to loop_name within
         * an init expression incorrectly resolved to this named-let's
         * own about-to-be-created self-reference (and, worse, by the
         * time the inits actually RAN at runtime -- after OP_STORE_LOCAL
         * had already written the real closure into that slot -- such a
         * reference saw the fully-constructed closure) instead of R7RS's
         * mandated "loop_name is not yet bound while evaluating its own
         * inits, which run in the ENCLOSING scope". Each value lands at
         * a known physical position (arg_base + i) that nothing above
         * this point could yet alias, exactly like any other pending
         * call argument. */
        int arg_base = c->local_count;
        for (val_t b = bindings; vis_pair(b); b = vcdr(b)) {
            IRNode *arg = ir_lower(c, vcar(vcdr(vcar(b))), false, n->line);
            ir_emit(c, arg);
            reserve_pending_slot(c);
        }

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
        /* Re-push a fresh copy of each already-computed init value from
         * its arg_base+i position -- these have no discoverable NAME
         * (reserve_pending_slot's placeholders never match a real
         * lookup), but OP_LOAD_LOCAL addresses a physical slot by plain
         * numeric index, same trick compile_let's own outer-wrapper fix
         * uses to forward its own params. This assembles the calling
         * convention's required contiguous [callee, arg1, ..., argN]
         * layout even though the args were computed earlier, at lower
         * physical positions, than the callee. */
        for (int i = 0; i < argc; i++) {
            emit_ab(c, OP_LOAD_LOCAL, (uint8_t)(arg_base + i), n->line);
            reserve_pending_slot(c);
        }
        release_pending_slots(c, saved);
        /* Exactly like the previous shape, the call above consumes only
         * its own reloaded callee+args copies -- NOT the argc original
         * arg-value slots or loop_slot's own underlying stack position,
         * all of which are SEPARATE physical slots the reloads merely
         * copied from. Where the previous shape left exactly ONE stale
         * slot below the call's own footprint (the original closure
         * copy), this leaves argc+1 (the original arg values AND the
         * original closure) -- so the OP_CALL case's cleanup needs
         * OP_SLIDE(argc+1) here, not the fixed OP_SLIDE(1) that was
         * correct only when there was nothing computed ahead of the
         * closure itself. In the OP_TAIL_CALL case this doesn't matter,
         * as before: nothing else compiles into `c` after a true tail
         * position, so stale slots below are never observed. */
        if (n->tail) {
            emit_ab(c, OP_TAIL_CALL, (uint8_t)argc, n->line);
        } else {
            emit_ab(c, OP_CALL, (uint8_t)argc, n->line);
            /* loop_slot (an open upvalue captured by the inner recursive
             * lambda above) is one of the stale slots OP_SLIDE is about
             * to discard from the runtime stack -- but discarding a
             * stack slot is not the same as closing the upvalue that
             * still points at it. Without this, an escaped closure
             * holding the loop's own name as an upvalue keeps pointing
             * at that now-stale, soon-to-be-reused stack address: a
             * LATER local in this same enclosing frame ends up sharing
             * storage with it, so reading/writing through the escaped
             * closure's upvalue silently aliases an unrelated variable
             * instead. Pre-existing gap (this fast path never closed
             * loop_slot on this exit path), but widening OP_SLIDE's
             * operand from a fixed 1 to argc+1 (above) turned it from
             * "clobber the named-let's own about-to-be-discarded result"
             * into "clobber whichever enclosing local happens to sit
             * argc+1 slots up, chosen by how many loop variables the
             * source declares" -- independent security review. */
            emit_ab(c, OP_CLOSE_UP, (uint8_t)arg_base, n->line);
            emit_ab(c, OP_SLIDE, (uint8_t)(argc + 1), n->line);
        }
        return;
    }
    }
}
