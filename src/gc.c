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
