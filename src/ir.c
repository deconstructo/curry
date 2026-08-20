#include "ir.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Simple growable bump allocator. Blocks double in size (starting at 4K)
 * and are never individually freed until ir_arena_free tears down the
 * whole chain -- IRNode trees are short-lived (one top-level form's
 * compile) and small, so per-node free() bookkeeping would be pure
 * overhead. Not thread-shared: each Compiler tree gets its own arena
 * (see ir.h's lifetime comment), and compilation of one top-level form
 * runs on a single thread. */

#define IR_ARENA_INITIAL_BLOCK 4096

typedef struct IRArenaBlock {
    struct IRArenaBlock *next;
    size_t used;
    size_t cap;
    char   data[];
} IRArenaBlock;

struct IRArena {
    IRArenaBlock *head;
};

static IRArenaBlock *ir_arena_block_new(size_t min_cap) {
    size_t cap = IR_ARENA_INITIAL_BLOCK;
    while (cap < min_cap) cap *= 2;
    IRArenaBlock *b = (IRArenaBlock *)malloc(sizeof(IRArenaBlock) + cap);
    if (!b) { fprintf(stderr, "ir: out of memory\n"); abort(); }
    b->next = NULL;
    b->used = 0;
    b->cap  = cap;
    return b;
}

IRArena *ir_arena_new(void) {
    IRArena *a = (IRArena *)malloc(sizeof(IRArena));
    if (!a) { fprintf(stderr, "ir: out of memory\n"); abort(); }
    a->head = ir_arena_block_new(IR_ARENA_INITIAL_BLOCK);
    return a;
}

void ir_arena_free(IRArena *a) {
    if (!a) return;
    IRArenaBlock *b = a->head;
    while (b) {
        IRArenaBlock *next = b->next;
        free(b);
        b = next;
    }
    free(a);
}

/* Round up to 8-byte alignment -- every IRNode member (val_t, pointers,
 * int/bool) is at most 8-byte aligned on every platform curry targets. */
static size_t align8(size_t n) { return (n + 7u) & ~(size_t)7u; }

void *ir_arena_alloc(IRArena *a, size_t size) {
    size = align8(size);
    IRArenaBlock *b = a->head;
    if (b->used + size > b->cap) {
        IRArenaBlock *nb = ir_arena_block_new(size);
        nb->next = b;
        a->head = nb;
        b = nb;
    }
    void *p = b->data + b->used;
    b->used += size;
    return p;
}

IRNode *ir_node_new(IRArena *a, IRKind kind, bool tail, int line) {
    IRNode *n = (IRNode *)ir_arena_alloc(a, sizeof(IRNode));
    memset(n, 0, sizeof(IRNode));
    n->kind = kind;
    n->tail = tail;
    n->line = line;
    return n;
}
