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
#include <stdbool.h>

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

/* ---- Pipeline stage timings (-- timings CLI flag, main.c) ----
 * Coarse accumulators for the read/expand/compile/execute stages the
 * top-level driver goes through for every form it compiles and runs —
 * distinct from the closure profiler above, which times USER code, not
 * the compiler's own work. Guarded by curry_timings_enabled so the
 * profiling_now_ns() calls at each call site are skipped entirely when
 * off (single branch, same discipline as curry_profiling_level).
 *
 * "compile" time as accumulated in curry_timing_compile_ns includes
 * "expand" time internally (macro transformers run via apply() inside
 * compiler_compile — see compile()'s macro-expansion check in
 * compiler.c), so curry_timings_report() subtracts expand out of compile
 * before printing, so the four printed lines sum to the total instead of
 * double-counting the nested time. */
extern bool     curry_timings_enabled;
extern uint64_t curry_timing_read_ns;
extern uint64_t curry_timing_expand_ns;
extern uint64_t curry_timing_compile_ns;
extern uint64_t curry_timing_execute_ns;

/* Print the accumulated report to stderr. No-op if disabled. Safe to call
 * at any point (e.g. process exit, or ,quit in the REPL) — does not reset
 * the accumulators, so a REPL session's report grows across calls. */
void curry_timings_report(void);

#endif /* CURRY_PROFILING_H */
