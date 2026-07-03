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
    [OP_CLOSURE]        = "CLOSURE",
    [OP_CLOSE_UP]       = "CLOSE_UP",
    [OP_APPLY]          = "APPLY",
    [OP_VALUES]         = "VALUES",
    [OP_CALL_WITH_VALUES] = "CALL_WITH_VALUES",
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
    c->glob_cache = NULL;
    c->src_lambda  = V_VOID;
    c->upval_names = NULL;
    return c;
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
    }
    int idx = c->const_len++;
    c->constants[idx] = v;
    return idx;
}

/* ── Disassembler ────────────────────────────────────────────────────── */

static int disasm_one(const Chunk *c, int off) {
    fprintf(stderr, "%04d ", off);
    if (off > 0 && c->lines[off] == c->lines[off-1])
        fprintf(stderr, "   | ");
    else
        fprintf(stderr, "%4d ", c->lines[off]);

    uint8_t op = c->code[off++];
    const char *name = (op < OP_COUNT) ? opcode_name[op] : "???";

    switch ((OpCode)op) {
    case OP_CONST:
    case OP_LOAD_LOCAL: case OP_STORE_LOCAL:
    case OP_LOAD_GLOBAL: case OP_STORE_GLOBAL: case OP_DEF_GLOBAL:
    case OP_LOAD_UP: case OP_STORE_UP:
    case OP_CALL: case OP_TAIL_CALL:
    case OP_CLOSURE: case OP_CLOSE_UP:
    case OP_VALUES: case OP_MAKEVEC: case OP_APPLY:
    case OP_SLIDE: {
        uint8_t a = c->code[off++];
        fprintf(stderr, "%-16s %4d", name, a);
        if ((OpCode)op == OP_CONST || (OpCode)op == OP_LOAD_GLOBAL ||
            (OpCode)op == OP_STORE_GLOBAL || (OpCode)op == OP_DEF_GLOBAL) {
            /* print constant value */
            if (a < c->const_len) {
                val_t v = c->constants[a];
                if (vis_fixnum(v))  fprintf(stderr, "  ; %ld", (long)vunfix(v));
                else if (vis_symbol(v)) fprintf(stderr, "  ; %s", as_sym(v)->data);
            }
        }
        fprintf(stderr, "\n");
        break;
    }
    case OP_CONST_W:
    case OP_JUMP: case OP_JUMP_FALSE: case OP_JUMP_TRUE:
    case OP_PUSH_HANDLER: {
        uint16_t b = (uint16_t)(c->code[off] | (c->code[off+1] << 8));
        off += 2;
        fprintf(stderr, "%-16s %4d\n", name, (int)b);
        break;
    }
    default:
        fprintf(stderr, "%s\n", name);
        break;
    }
    return off;
}

void chunk_disasm(const Chunk *c, const char *label) {
    fprintf(stderr, "=== %s (%d bytes, %d consts) ===\n",
            label ? label : "<anon>", c->code_len, c->const_len);
    int off = 0;
    while (off < c->code_len)
        off = disasm_one(c, off);
}
