#include "gc.h"
#include <gc/gc.h>

void gc_init(void) {
    GC_INIT();
    GC_allow_register_threads();
}

void gc_register_thread(void) {
    struct GC_stack_base sb;
    GC_get_stack_base(&sb);
    GC_register_my_thread(&sb);
}

void gc_finalizer(void *obj, void (*fn)(void *, void *), void *cd) {
    GC_register_finalizer(obj, (GC_finalization_proc)fn, cd, NULL, NULL);
}

void   gc_set_max_heap(size_t bytes)    { GC_set_max_heap_size((GC_word)bytes); }
void   gc_set_free_space_divisor(int n) { GC_set_free_space_divisor((GC_word)n); }
void   gc_enable_incremental(void)      { GC_enable_incremental(); }
size_t gc_heap_size(void)               { return (size_t)GC_get_heap_size(); }
size_t gc_free_bytes(void)              { return (size_t)GC_get_free_bytes(); }
size_t gc_total_bytes(void)             { return (size_t)GC_get_total_bytes(); }
