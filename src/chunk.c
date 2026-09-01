/*
 * chunk.c — Bytecode chunk: allocation, constant pool, disassembler.
 *
 * PURPOSE
 *   A Chunk is the compiled unit of execution — it holds:
 *     code[]      byte stream of OpCode instructions and their operands
 *     constants[] pool of val_t values referenced by index in the code
 *     lines[]     source line number for each byte (parallel to code[])
 *
 *   chunk_new()      allocates an empty Chunk on the GC heap.
 *   chunk_emit()     appends a single byte with its source line.
 *   chunk_emit16()   appends a 2-byte little-endian value.
 *   chunk_patch16()  back-patches a 2-byte field (used by jump fixup).
 *   chunk_add_const() adds a constant to the pool; deduplicates where
 *                    possible (same pointer / numeric equality / string
 *                    content) to keep the pool compact.
 *   chunk_disasm()   pretty-prints bytecode to stderr for debugging.
 *
 * The Chunk* itself is stored as a raw val_t (cast to uintptr_t) in the
 * parent chunk's constant pool when OP_CLOSURE is emitted; the VM
 * extracts it back with vunptr(Chunk, v).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chunk.h"
#include "opcode.h"
#include "gc.h"
#include "numeric.h"
#include "symbol.h"
#include "object.h"
#include "set.h"
#include "eval.h"

const char *opcode_name[OP_COUNT] = {
    [OP_CONST]          = "CONST",
    [OP_CONST_W]        = "CONST_W",
    [OP_TRUE]           = "TRUE",
    [OP_FALSE]          = "FALSE",
    [OP_NIL]            = "NIL",
    [OP_VOID]           = "VOID",
    [OP_LOAD_LOCAL]     = "LOAD_LOCAL",
    [OP_STORE_LOCAL]    = "STORE_LOCAL",
    [OP_LOAD_GLOBAL]    = "LOAD_GLOBAL",
    [OP_STORE_GLOBAL]   = "STORE_GLOBAL",
    [OP_DEF_GLOBAL]     = "DEF_GLOBAL",
    [OP_DEFINED_GLOBAL] = "DEFINED_GLOBAL",
    [OP_LOAD_UP]        = "LOAD_UP",
    [OP_STORE_UP]       = "STORE_UP",
    [OP_POP]            = "POP",
    [OP_DUP]            = "DUP",
    [OP_SWAP]           = "SWAP",
    [OP_SLIDE]          = "SLIDE",
    [OP_ADD]            = "ADD",
    [OP_SUB]            = "SUB",
    [OP_MUL]            = "MUL",
    [OP_DIV]            = "DIV",
    [OP_NEG]            = "NEG",
    [OP_ABS]            = "ABS",
    [OP_EXPT]           = "EXPT",
    [OP_EQ]             = "EQ",
    [OP_LT]             = "LT",
    [OP_LE]             = "LE",
    [OP_GT]             = "GT",
    [OP_GE]             = "GE",
    [OP_NUMEQ]          = "NUMEQ",
    [OP_EQV]            = "EQV",
    [OP_EQUAL]          = "EQUAL",
    [OP_NOT]            = "NOT",
    [OP_CONS]           = "CONS",
    [OP_CAR]            = "CAR",
    [OP_CDR]            = "CDR",
    [OP_SETCAR]         = "SETCAR",
    [OP_SETCDR]         = "SETCDR",
    [OP_NULLP]          = "NULLP",
    [OP_PAIRP]          = "PAIRP",
    [OP_STRINGLEN]      = "STRINGLEN",
    [OP_STRINGREF]      = "STRINGREF",
    [OP_CHARTOFIX]      = "CHARTOFIX",
    [OP_FIXTOCHAR]      = "FIXTOCHAR",
    [OP_NUMBERP]        = "NUMBERP",
    [OP_STRINGP]        = "STRINGP",
    [OP_SYMBOLP]        = "SYMBOLP",
    [OP_CHARP]          = "CHARP",
    [OP_BOOLP]          = "BOOLP",
    [OP_PROCP]          = "PROCP",
    [OP_VECTORP]        = "VECTORP",
    [OP_MAKEVEC]        = "MAKEVEC",
    [OP_VECREF]         = "VECREF",
    [OP_VECSET]         = "VECSET",
    [OP_VECLEN]         = "VECLEN",
    [OP_JUMP]           = "JUMP",
    [OP_JUMP_FALSE]     = "JUMP_FALSE",
    [OP_JUMP_TRUE]      = "JUMP_TRUE",
    [OP_CALL]           = "CALL",
    [OP_TAIL_CALL]      = "TAIL_CALL",
    [OP_RETURN]         = "RETURN",
    [OP_SELF_TAIL_CALL]   = "SELF_TAIL_CALL",
    [OP_CALL_GLOBAL]      = "CALL_GLOBAL",
    [OP_TAIL_CALL_GLOBAL] = "TAIL_CALL_GLOBAL",
    [OP_TREE_EVAL_CACHED] = "TREE_EVAL_CACHED",
    [OP_CLOSURE]        = "CLOSURE",
    [OP_CLOSE_UP]       = "CLOSE_UP",
    [OP_APPLY]          = "APPLY",
    [OP_TAIL_APPLY]     = "TAIL_APPLY",
    [OP_VALUES]         = "VALUES",
    [OP_VALUES_REF]     = "VALUES_REF",
    [OP_CALL_WITH_VALUES] = "CALL_WITH_VALUES",
    [OP_TAIL_CALL_WITH_VALUES] = "TAIL_CALL_WITH_VALUES",
    [OP_PUSH_HANDLER]   = "PUSH_HANDLER",
    [OP_POP_HANDLER]    = "POP_HANDLER",
    [OP_RAISE]          = "RAISE",
    [OP_DISPLAY]        = "DISPLAY",
    [OP_WRITE]          = "WRITE",
    [OP_NEWLINE]        = "NEWLINE",
    [OP_NOP]            = "NOP",
};

/* ── Chunk allocation ────────────────────────────────────────────────── */

Chunk *chunk_new(void) {
    Chunk *c = (Chunk *)gc_alloc_pinned(sizeof(Chunk));
    c->hdr.type   = T_CHUNK; c->hdr.flags = 0; c->hdr.fwd = 0;
    c->code       = NULL; c->code_len = 0; c->code_cap = 0;
    c->constants  = NULL; c->const_len = 0; c->const_cap = 0;
    c->lines      = NULL; c->line_cap = 0;
    c->arity      = 0;
    c->local_count = 0;
    c->upval_count = 0;
    c->name       = NULL;
    c->source_name = NULL;
    c->glob_cache = NULL;
    c->tree_eval_cache = NULL;
    c->local_debug = NULL;
    c->local_debug_len = 0;
    c->local_debug_cap = 0;
    c->src_lambda  = V_VOID;
    c->upval_names = NULL;
    c->target_env  = V_VOID;
    c->uses_local_macro = false;
    return c;
}

void chunk_set_source_name_recursive(Chunk *c, const char *name) {
    if (!c || c->source_name == name) return; /* already stamped: avoid re-walking */
    c->source_name = name;
    for (int i = 0; i < c->const_len; i++) {
        val_t v = c->constants[i];
        if (vis_type(v, T_CHUNK))
            chunk_set_source_name_recursive(vunptr(Chunk, v), name);
    }
}

/* ── Emit ────────────────────────────────────────────────────────────── */

static void ensure_code(Chunk *c, int need) {
    if (c->code_len + need <= c->code_cap) return;
    int cap = c->code_cap < 8 ? 8 : c->code_cap * 2;
    while (cap < c->code_len + need) cap *= 2;
    c->code  = GC_REALLOC(c->code,  (size_t)cap);
    c->lines = GC_REALLOC(c->lines, (size_t)cap * sizeof(int));
    c->code_cap = cap; c->line_cap = cap;
}

int chunk_emit(Chunk *c, uint8_t byte, int line) {
    ensure_code(c, 1);
    int off = c->code_len;
    c->code[off]  = byte;
    c->lines[off] = line;
    c->code_len++;
    return off;
}

void chunk_emit16(Chunk *c, uint16_t val, int line) {
    chunk_emit(c, (uint8_t)(val & 0xFF), line);
    chunk_emit(c, (uint8_t)(val >> 8),   line);
}

void chunk_patch16(Chunk *c, int at, uint16_t target) {
    c->code[at]     = (uint8_t)(target & 0xFF);
    c->code[at + 1] = (uint8_t)(target >> 8);
}

/* ── Local-variable debug info ───────────────────────────────────────── */

int chunk_local_debug_add(Chunk *c, val_t name, int slot, int start_pc) {
    if (c->local_debug_len == c->local_debug_cap) {
        int cap = c->local_debug_cap < 8 ? 8 : c->local_debug_cap * 2;
        /* holds val_t names — allocate traced+pinned like glob_cache */
        LocalDebugEntry *ne =
            (LocalDebugEntry *)gc_alloc_raw_pinned((size_t)cap * sizeof(LocalDebugEntry));
        if (c->local_debug)
            memcpy(ne, c->local_debug,
                   (size_t)c->local_debug_len * sizeof(LocalDebugEntry));
        c->local_debug = ne;
        c->local_debug_cap = cap;
    }
    int idx = c->local_debug_len++;
    c->local_debug[idx].name     = name;
    c->local_debug[idx].slot     = (uint16_t)slot;
    c->local_debug[idx].start_pc = start_pc;
    c->local_debug[idx].end_pc   = -1;
    return idx;
}

void chunk_local_debug_end(Chunk *c, int idx, int end_pc) {
    if (idx >= 0 && idx < c->local_debug_len)
        c->local_debug[idx].end_pc = end_pc;
}

void chunk_local_debug_finalize(Chunk *c) {
    for (int i = 0; i < c->local_debug_len; i++)
        if (c->local_debug[i].end_pc < 0)
            c->local_debug[i].end_pc = c->code_len;
}

/* ── Constants ───────────────────────────────────────────────────────── */

int chunk_add_const(Chunk *c, val_t v) {
    /* Reuse existing constant if possible */
    for (int i = 0; i < c->const_len; i++) {
        val_t e = c->constants[i];
        if (e == v) return i;                       /* same pointer / immediate */
        if (vis_number(e) && vis_number(v) && scm_eqv(e, v)) return i;
        if (vis_symbol(e) && vis_symbol(v) && e == v)  return i; /* interned */
        if (vis_string(e) && vis_string(v) &&
            strcmp(str_data(as_str(e)), str_data(as_str(v))) == 0) return i;
    }
    /* Every bytecode operand indexing into constants[] is a single uint8_t
     * (OP_LOAD_GLOBAL/OP_STORE_GLOBAL/OP_CALL_GLOBAL/OP_TAIL_CALL_GLOBAL/
     * OP_TREE_EVAL_CACHED/emit_const, ...) -- a chunk that accumulates a
     * 257th DISTINCT constant would silently wrap the returned index back
     * into uint8_t range and every later use of that index would load or
     * call the WRONG constant instead of failing loudly. This has always
     * been possible (emit_load/emit_store hit this same path for every
     * distinct global reference, pre-dating this check), but is worth a
     * hard, clear compile-time stop rather than silent misbehavior --
     * mirrors the existing "cond: too many clauses (compiler limit)"
     * precedent below for the same category of fixed-width-operand
     * limit. */
    if (c->const_len >= 256)
        scm_raise(V_FALSE, "compiler limit: too many distinct constants in one chunk (max 256)");
    if (c->const_len == c->const_cap) {
        int cap = c->const_cap < 8 ? 8 : c->const_cap * 2;
        c->constants = GC_REALLOC(c->constants, (size_t)cap * sizeof(val_t));
        c->const_cap = cap;
        /* grow glob_cache parallel to constants — use gc_alloc (not atomic) so
         * Boehm GC traces the interior val_t* slot pointers */
        GlobCacheEntry *new_gc = (GlobCacheEntry *)gc_alloc_raw_pinned((size_t)cap * sizeof(GlobCacheEntry));
        if (c->glob_cache)
            memcpy(new_gc, c->glob_cache, (size_t)c->const_len * sizeof(GlobCacheEntry));
        memset(new_gc + c->const_len, 0,
               (size_t)(cap - c->const_len) * sizeof(GlobCacheEntry));
        c->glob_cache = new_gc;
        /* Grow tree_eval_cache the same way, parallel to constants --
         * see chunk.h's Chunk::tree_eval_cache comment. Unlike
         * glob_cache this holds real val_t values (not raw pointers), so
         * it needs gc_alloc_raw_pinned for the same "Boehm traces this"
         * reason but ALSO needs a gc_gen.c evacuation case (added
         * alongside T_CHUNK's existing one) for correctness under
         * --gc generational. */
        val_t *new_tc = (val_t *)gc_alloc_raw_pinned((size_t)cap * sizeof(val_t));
        if (c->tree_eval_cache)
            memcpy(new_tc, c->tree_eval_cache, (size_t)c->const_len * sizeof(val_t));
        memset(new_tc + c->const_len, 0, (size_t)(cap - c->const_len) * sizeof(val_t));
        c->tree_eval_cache = new_tc;
    }
    int idx = c->const_len++;
    c->constants[idx] = v;
    return idx;
}

/* ── Disassembler ────────────────────────────────────────────────────── */

static int disasm_one(const Chunk *c, int off, FILE *out) {
    fprintf(out, "%04d ", off);
    if (off > 0 && c->lines[off] == c->lines[off-1])
        fprintf(out, "   | ");
    else
        fprintf(out, "%4d ", c->lines[off]);

    uint8_t op = c->code[off++];
    const char *name = (op < OP_COUNT) ? opcode_name[op] : "???";

    switch ((OpCode)op) {
    /* OP_CLOSURE has its own case, separate from the plain single-byte-
     * operand group below: its encoding is ci (1 byte) followed by
     * upval_count*2 more bytes (an (is_local, idx) pair per captured
     * upvalue -- see vm.c's OP_CLOSURE handler), a variable-length tail
     * this disassembler must skip or every subsequent instruction in the
     * chunk decodes from the wrong offset. Previously grouped with the
     * fixed single-operand cases below, which only consumed ci and never
     * skipped the upvalue table -- a real desync bug for any closure that
     * captures at least one upvalue (the common case), found while wiring
     * this up to a `disassemble` builtin/`,asm` REPL command. Also a
     * latent OOB-read risk: the resulting misaligned `off` could walk the
     * next fake "instruction" past code_len when this function's own
     * multi-byte-operand cases read code[off+1] without their own bounds
     * check, relying on this loop having decoded prior instructions
     * correctly to stay in range. */
    case OP_CLOSURE: {
        uint8_t ci = c->code[off++];
        fprintf(out, "%-16s %4d", name, ci);
        /* If the constant at ci isn't actually a chunk (out of range, or
         * present but the wrong type -- a corrupt/hostile .scc file could
         * shape one this way; read_chunk in scc.c does not validate
         * upval_count against anything), there is no reliable way to know
         * how many trailing upvalue-table bytes this instruction actually
         * has. Silently treating that as "0 upvalues" and continuing --
         * an earlier version of this fix did exactly that -- resumes
         * decoding at exactly the wrong offset, i.e. the desync bug this
         * whole case exists to fix, just made silent instead of loud
         * (found by independent review). Print a marker and stop
         * disassembling the rest of the chunk instead: a truncated-but-
         * honest listing beats a longer one that's silently wrong from
         * this point on. */
        if (ci >= c->const_len || !vis_type(c->constants[ci], T_CHUNK)) {
            fprintf(out, "  <bad chunk constant %d -- disassembly aborted>\n", ci);
            return c->code_len;
        }
        int n_upvals = vunptr(Chunk, c->constants[ci])->upval_count;
        if (n_upvals > 0) fprintf(out, "  upvals:");
        for (int i = 0; i < n_upvals; i++) {
            /* Bounds-check every pair read, not just the loop count: a
             * corrupt/hostile .scc's upval_count could claim more
             * upvalues than actually fit in the remaining code[] bytes.
             * Same "stop rather than silently misdecode" policy as above. */
            if (off + 1 >= c->code_len) {
                fprintf(out, "  <upvalue table runs past end of code -- disassembly aborted>\n");
                return c->code_len;
            }
            uint8_t is_local = c->code[off++];
            uint8_t idx      = c->code[off++];
            fprintf(out, " (%s %d)", is_local ? "local" : "up", idx);
        }
        fprintf(out, "\n");
        break;
    }
    case OP_CONST:
    case OP_LOAD_LOCAL: case OP_STORE_LOCAL:
    case OP_LOAD_GLOBAL: case OP_STORE_GLOBAL: case OP_DEF_GLOBAL:
    case OP_DEFINED_GLOBAL:
    case OP_LOAD_UP: case OP_STORE_UP:
    case OP_CALL: case OP_TAIL_CALL: case OP_SELF_TAIL_CALL:
    case OP_CLOSE_UP:
    case OP_VALUES: case OP_VALUES_REF: case OP_MAKEVEC: case OP_APPLY: case OP_TAIL_APPLY:
    case OP_SLIDE: case OP_TREE_EVAL_CACHED:
    case OP_CAR: case OP_CDR: case OP_CONS: case OP_NULLP: case OP_PAIRP:
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_NUMEQ:
    case OP_LT: case OP_LE: case OP_GT: case OP_GE: {
        uint8_t a = c->code[off++];
        fprintf(out, "%-16s %4d", name, a);
        if ((OpCode)op == OP_CONST || (OpCode)op == OP_LOAD_GLOBAL ||
            (OpCode)op == OP_STORE_GLOBAL || (OpCode)op == OP_DEF_GLOBAL ||
            (OpCode)op == OP_DEFINED_GLOBAL ||
            (OpCode)op == OP_CAR || (OpCode)op == OP_CDR ||
            (OpCode)op == OP_CONS || (OpCode)op == OP_NULLP ||
            (OpCode)op == OP_PAIRP ||
            (OpCode)op == OP_ADD || (OpCode)op == OP_SUB ||
            (OpCode)op == OP_MUL || (OpCode)op == OP_NUMEQ ||
            (OpCode)op == OP_LT || (OpCode)op == OP_LE ||
            (OpCode)op == OP_GT || (OpCode)op == OP_GE) {
            /* print constant value */
            if (a < c->const_len) {
                val_t v = c->constants[a];
                if (vis_fixnum(v))  fprintf(out, "  ; %ld", (long)vunfix(v));
                else if (vis_symbol(v)) fprintf(out, "  ; %s", as_sym(v)->data);
            }
        }
        fprintf(out, "\n");
        break;
    }
    case OP_CONST_W:
    case OP_JUMP: case OP_JUMP_FALSE: case OP_JUMP_TRUE:
    case OP_PUSH_HANDLER: {
        uint16_t b = (uint16_t)(c->code[off] | (c->code[off+1] << 8));
        off += 2;
        fprintf(out, "%-16s %4d\n", name, (int)b);
        break;
    }
    case OP_CALL_GLOBAL: case OP_TAIL_CALL_GLOBAL: {
        uint8_t a = c->code[off++];
        uint8_t b = c->code[off++];
        fprintf(out, "%-16s %4d %4d", name, a, b);
        if (a < c->const_len) {
            val_t v = c->constants[a];
            if (vis_symbol(v)) fprintf(out, "  ; %s", as_sym(v)->data);
        }
        fprintf(out, "\n");
        break;
    }
    default:
        fprintf(out, "%s\n", name);
        break;
    }
    return off;
}

void chunk_disasm(const Chunk *c, const char *label, FILE *out) {
    fprintf(out, "=== %s (%d bytes, %d consts) ===\n",
            label ? label : "<anon>", c->code_len, c->const_len);
    int off = 0;
    while (off < c->code_len)
        off = disasm_one(c, off, out);
}
