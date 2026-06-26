/*
 * curry_profiling — profiling consumer module for Curry Scheme.
 *
 * The instrumentation lives in src/profiling.c (always compiled into the
 * main binary).  This module exposes the controls and report as Scheme
 * procedures and keeps the Scheme binding **eval-profiler** in sync.
 *
 * Scheme API:
 *   (profiler-start [level])          — enable at level 1..3 (default 1)
 *   (profiler-stop)                   — set level to 0
 *   (profiler-reset)                  — clear all accumulated data
 *   (profiler-level)                  — current level as fixnum
 *   (profiler-report)                 — alist: ((name . (calls . ns)) ...)
 *   (profiler-report/top n)           — top N entries by call count
 *
 * ns values are wall-clock nanoseconds (non-zero only for apply() paths
 * at level >= 2; TCO calls are counted but not timed).
 */

#include <curry.h>
#include "profiling.h"
#include "env.h"
#include "symbol.h"
#include "builtins.h"

static void sync_scheme_binding(int level) {
    extern val_t GLOBAL_ENV;
    env_set(GLOBAL_ENV, S_EVAL_PROFILER, vfix(level));
}

static val_t prim_profiler_start(int argc, val_t *argv, void *ud) {
    (void)ud;
    int level = (argc >= 1 && vis_fixnum(argv[0])) ? (int)vunfix(argv[0]) : 1;
    profiling_set_level(level);
    sync_scheme_binding(curry_profiling_level);
    return V_VOID;
}

static val_t prim_profiler_stop(int argc, val_t *argv, void *ud) {
    (void)argc; (void)argv; (void)ud;
    profiling_set_level(0);
    sync_scheme_binding(0);
    return V_VOID;
}

static val_t prim_profiler_reset(int argc, val_t *argv, void *ud) {
    (void)argc; (void)argv; (void)ud;
    profiling_reset();
    return V_VOID;
}

static val_t prim_profiler_level(int argc, val_t *argv, void *ud) {
    (void)argc; (void)argv; (void)ud;
    return vfix(curry_profiling_level);
}

static val_t prim_profiler_report(int argc, val_t *argv, void *ud) {
    (void)argc; (void)argv; (void)ud;
    return profiling_report();
}

/* Return the top N entries by call count as an alist.
 * If n <= 0, return all entries (same as profiler-report). */
static val_t prim_profiler_report_top(int argc, val_t *argv, void *ud) {
    (void)ud;
    int n = (argc >= 1 && vis_fixnum(argv[0])) ? (int)vunfix(argv[0]) : 0;
    val_t full = profiling_report();
    if (n <= 0) return full;
    /* full is already sorted descending by call count; take first n */
    val_t result = V_NIL;
    val_t tail   = V_NIL;
    int   count  = 0;
    for (val_t p = full; vis_pair(p) && count < n; p = as_pair(p)->cdr) {
        val_t entry = scm_cons(as_pair(p)->car, V_NIL);
        if (vis_nil(result)) {
            result = tail = entry;
        } else {
            as_pair(tail)->cdr = entry;
            tail = entry;
        }
        count++;
    }
    return result;
}

void curry_module_init(CurryVM *vm) {
    curry_define_fn(vm, "profiler-start",      prim_profiler_start,      0, 1, NULL);
    curry_define_fn(vm, "profiler-stop",       prim_profiler_stop,       0, 0, NULL);
    curry_define_fn(vm, "profiler-reset",      prim_profiler_reset,      0, 0, NULL);
    curry_define_fn(vm, "profiler-level",      prim_profiler_level,      0, 0, NULL);
    curry_define_fn(vm, "profiler-report",     prim_profiler_report,     0, 0, NULL);
    curry_define_fn(vm, "profiler-report/top", prim_profiler_report_top, 0, 1, NULL);
}
