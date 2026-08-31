/*
 * compiler_ir_checks.c — Tier 2.1/2.2/2.3 differential self-check
 * infrastructure.
 *
 * Split out of the old single-file compiler.c (pure code motion, no
 * behavior change -- see compiler.c's own header comment for the full
 * five-way split). Holds compiler_ir_self_check (byte-for-byte bytecode
 * comparison between compile_classic() and ir_lower+ir_emit),
 * compiler_ir_optimize_check (result comparison between the two,
 * exercising ir_optimize), and compiler_ir_inline_fired_check (proves the
 * Tier 2.3 local inliner actually fired for a given expression, via total
 * compiled-bytecode-size growth -- chunk_total_code_len is its own private
 * helper). These are test-only tools, not part of any live compile path.
 *
 * Shared symbols (the real `Compiler` struct, compile_classic defined in
 * compiler_classic.c, ir_lower/ir_optimize defined in ir_lower.c, ir_emit
 * defined in ir_emit.c) are declared in compiler_internal.h.
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
            chunk_disasm(old_c.chunk, "old", stderr);
            fprintf(stderr, "--- new (ir_lower + ir_emit) ---\n");
            chunk_disasm(new_c.chunk, "new", stderr);
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
