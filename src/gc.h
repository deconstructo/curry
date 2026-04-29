#ifndef CURRY_GC_H
#define CURRY_GC_H

/*
 * Thin wrapper around the Boehm-Demers-Weiser garbage collector.
 *
 * Boehm GC is conservative, thread-safe (with GC_allow_register_threads),
 * and requires no explicit rooting for stack-allocated pointers.  Heap
 * objects may contain val_t fields freely.
 *
 * Allocation conventions:
 *   gc_alloc        - object may contain GC pointers
 *   gc_alloc_atomic - object contains no GC pointers (strings, bignums, ...)
 *
 * GC_NEW / GC_NEW_FLEX are the primary allocation macros used throughout the
 * interpreter.
 */

#define GC_THREADS
#include <gc/gc.h>
#include <stddef.h>

void gc_init(void);
void gc_register_thread(void);
void gc_finalizer(void *obj, void (*fn)(void *, void *), void *cd);

static inline void *gc_alloc(size_t n)        { return GC_MALLOC(n);          }
static inline void *gc_alloc_atomic(size_t n) { return GC_MALLOC_ATOMIC(n);   }
static inline void  gc_collect(void)           { GC_gcollect();                }

/* Allocate a fixed-size struct that may contain pointers */
#define CURRY_NEW(T)           ((T *)gc_alloc(sizeof(T)))

/* Allocate a struct with a flexible array member 'data' of element type E */
#define CURRY_NEW_FLEX(T, n)   ((T *)gc_alloc(sizeof(T) + (n) * sizeof(((T *)0)->data[0])))

/* Allocate a fixed-size struct with no interior GC pointers */
#define CURRY_NEW_ATOM(T)      ((T *)gc_alloc_atomic(sizeof(T)))

/* Allocate an atomic flexible-array struct */
#define CURRY_NEW_FLEX_ATOM(T, n) \
    ((T *)gc_alloc_atomic(sizeof(T) + (n) * sizeof(((T *)0)->data[0])))

#endif /* CURRY_GC_H */
