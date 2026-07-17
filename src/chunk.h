#ifndef CURRY_CHUNK_H
#define CURRY_CHUNK_H

#include <stdint.h>
#include "value.h"
#include "opcode.h"
#include "object.h"

/*
 * Chunk — a compiled unit of bytecode.
 *
 * Produced by the compiler for each lambda (top-level or nested).
 * The VM executes chunks; closures wrap a chunk with captured upvalues.
 *
 * Memory: chunks are GC-managed (T_CHUNK).  The code and constants
 * arrays are resized via realloc during compilation then frozen.
 */

/* Monomorphic inline cache entry for OP_LOAD_GLOBAL / OP_STORE_GLOBAL.
 * slot points directly into the EnvFrame vals array; version mirrors
 * the root frame's version so we can detect if it reallocated. */
typedef struct { val_t *slot; uint32_t version; } GlobCacheEntry;

/* Debug info: one local variable's name and the bytecode range where its
 * stack slot holds it.  Slots are reused across scopes, so the same slot
 * may appear in several entries with disjoint [start_pc, end_pc) ranges.
 * Not persisted in .scc files (matching name/source_name). */
typedef struct {
    val_t    name;      /* interned symbol                              */
    uint16_t slot;      /* frame slot index                             */
    int      start_pc;  /* first bytecode offset where the local exists */
    int      end_pc;    /* one past the last offset; -1 until closed    */
} LocalDebugEntry;

typedef struct {
    Hdr       hdr;        /* type = T_CHUNK; must be first               */
    uint8_t  *code;       /* bytecode stream                             */
    int       code_len;   /* used bytes                                  */
    int       code_cap;   /* allocated bytes                             */

    val_t    *constants;  /* constant pool (numbers, strings, symbols,
                             nested Chunk* wrapped as val_t)             */
    int       const_len;
    int       const_cap;

    int      *lines;      /* source line number per bytecode byte        */
    int       line_cap;

    int       arity;      /* >=0: exact required arg count.
                             <0: variadic; fixed (required) count is
                             -arity-1, plus one rest-list parameter.       */
    int       local_count;/* number of local variable slots              */
    int       upval_count;/* number of captured upvalues                 */
    const char *name;     /* function name for error messages (or NULL)  */
    const char *source_name; /* source file/unit for backtraces (or NULL);
                                 caller-owned, must outlive the chunk     */

    GlobCacheEntry *glob_cache; /* parallel to constants[], filled lazily; GC-traced */

    LocalDebugEntry *local_debug; /* debugger's slot→name map; may be NULL */
    int       local_debug_len;
    int       local_debug_cap;

    /* Tiered JIT: source AST preserved for hot-swap to native code.
     * src_lambda = (lambda params body...) form; V_VOID if unavailable.
     * upval_names[i] = interned symbol for upvalue i; NULL if unavailable. */
    val_t  src_lambda;
    val_t *upval_names;
} Chunk;

/* Allocate a fresh empty chunk */
Chunk *chunk_new(void);

/* Stamp source_name on this chunk and every nested chunk reachable through
 * its constant pool (lambdas compiled inside it). Used to attach a source
 * file to chunks loaded from .scc, which does not persist source_name. */
void chunk_set_source_name_recursive(Chunk *c, const char *name);

/* Emit one byte; returns byte offset of the emitted byte */
int chunk_emit(Chunk *c, uint8_t byte, int line);

/* Emit a wide 16-bit operand (little-endian) */
void chunk_emit16(Chunk *c, uint16_t val, int line);

/* Add a constant to the pool; returns its index */
int chunk_add_const(Chunk *c, val_t v);

/* Patch a 16-bit jump target at offset `at` */
void chunk_patch16(Chunk *c, int at, uint16_t target);

/* Record a local variable for the debugger; returns the entry index.
 * end_pc starts at -1; close it with chunk_local_debug_end when the local
 * leaves scope, or leave it for chunk_local_debug_finalize. */
int  chunk_local_debug_add(Chunk *c, val_t name, int slot, int start_pc);
void chunk_local_debug_end(Chunk *c, int idx, int end_pc);
/* Stamp code_len as end_pc on every still-open entry (params, top-levels). */
void chunk_local_debug_finalize(Chunk *c);

/* Current write position (for computing jump offsets) */
static inline int chunk_pos(const Chunk *c) { return c->code_len; }

/* Disassemble to stderr (debug) */
void chunk_disasm(const Chunk *c, const char *label);

#endif /* CURRY_CHUNK_H */
