#ifndef CURRY_PROFILING_H
#define CURRY_PROFILING_H

/*
 * Runtime profiler for Curry Scheme.
 *
 * Controlled by two well-known Scheme symbols:
 *
 *   (set! **eval-profiler** N)   — N = 0..3, default 0
 *   (set! **gc-profiler**   N)   — N = 0..1, default 0
 *
 * Levels:
 *   0  off — single not-taken branch in eval, effectively zero overhead
 *   1  named closure call counts
 *   2  wall-clock timing for named closure calls through apply()
 *   3  also count primitives
 *
 * The C globals curry_profiling_level / curry_gc_profiling_level are written
 * by the set! intercept in eval.c and read on the hot path.  The check
 * `if (curry_profiling_level)` is a single integer compare; the branch
 * predictor will predict not-taken in steady state when profiling is off.
 */

#include "value.h"
#include <stdint.h>

/* Profiling level — mirrors **eval-profiler** */
extern int curry_profiling_level;
extern int curry_gc_profiling_level;

/* Called from the set! intercept in eval.c */
void profiling_set_level(int level);
void gc_profiling_set_level(int level);

/* Hook: named closure entry, called from apply() and apply_arr().
 * At level 1, just increments the call count.
 * At level 2+, start_ns should be profiling_now_ns() captured before the
 * call; the function records elapsed time on exit. */
void profiling_record_call(val_t name_sym);
void profiling_record_timed(val_t name_sym, uint64_t start_ns);

/* Hook: named closure call count from the TCO path in eval() — counts only,
 * no timing because goto tail has no natural exit point. */
void profiling_record_call_tco(val_t name_sym);

/* Hook: primitive call (level >= 3), keyed by interned name symbol */
void profiling_record_prim(val_t name_sym);

/* Current monotonic timestamp in nanoseconds */
uint64_t profiling_now_ns(void);

/* Reset all accumulated data */
void profiling_reset(void);

/* Build a Scheme alist of the form:
 *   ((name . (calls . ns)) ...)
 * sorted by call count, descending. */
val_t profiling_report(void);

/* Called from main() after env_init() to pre-bind **eval-profiler** and
 * **gc-profiler** in env so that set! can find them. */
void profiling_init(val_t env);

#endif /* CURRY_PROFILING_H */
