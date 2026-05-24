#ifndef CURRY_CHUNK_H
#define CURRY_CHUNK_H

#include <stdint.h>
#include "value.h"
#include "opcode.h"

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

typedef struct {
    uint8_t  *code;       /* bytecode stream                             */
    int       code_len;   /* used bytes                                  */
    int       code_cap;   /* allocated bytes                             */

    val_t    *constants;  /* constant pool (numbers, strings, symbols,
                             nested Chunk* wrapped as val_t)             */
    int       const_len;
    int       const_cap;

    int      *lines;      /* source line number per bytecode byte        */
    int       line_cap;

    int       arity;      /* expected argument count (-1 = variadic)     */
    int       local_count;/* number of local variable slots              */
    int       upval_count;/* number of captured upvalues                 */
    const char *name;     /* function name for error messages (or NULL)  */

    GlobCacheEntry *glob_cache; /* parallel to constants[], filled lazily; GC-traced */
} Chunk;

/* Allocate a fresh empty chunk */
Chunk *chunk_new(void);

/* Emit one byte; returns byte offset of the emitted byte */
int chunk_emit(Chunk *c, uint8_t byte, int line);

/* Emit a wide 16-bit operand (little-endian) */
void chunk_emit16(Chunk *c, uint16_t val, int line);

/* Add a constant to the pool; returns its index */
int chunk_add_const(Chunk *c, val_t v);

/* Patch a 16-bit jump target at offset `at` */
void chunk_patch16(Chunk *c, int at, uint16_t target);

/* Current write position (for computing jump offsets) */
static inline int chunk_pos(const Chunk *c) { return c->code_len; }

/* Disassemble to stderr (debug) */
void chunk_disasm(const Chunk *c, const char *label);

#endif /* CURRY_CHUNK_H */
