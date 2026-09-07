/*
 * curry_checked_args.h — shared checked-argument accessors for C/C++
 * modules.
 *
 * curry_define_fn (src/api.c) only enforces argument ARITY, not type --
 * nothing upstream validates that av[N] is actually the type a handler
 * assumes before curry_string/curry_float/curry_fixnum/curry_symbol
 * (all unchecked casts in src/api.c: curry_string is
 * str_data(as_str(v)), curry_float falls to a raw as_flo(v)->value
 * cast for non-fixnum values, curry_fixnum is vunfix(v), curry_symbol
 * is sym_cstr(v)) touch it directly. A wrong-type argument therefore
 * wild-casts instead of raising a Scheme error -- confirmed
 * reproducible SIGSEGV via e.g. (sqlite-open 42) and
 * (redis-connect 42 6379) (issue #189), the same bug class as #158
 * through #187 (handle-arguments, vector-arguments, list-traversal,
 * and qt6's own direct scalar arguments), just not yet swept outside
 * modules/qt6/qt6.cpp.
 *
 * These thin wrappers are drop-in replacements for the four unchecked
 * accessors above: same return type/value on success, but curry_error()
 * first if the type doesn't match, naming the offending argument
 * position (1-based, matching how a Scheme user counts a procedure's
 * own arguments) and the calling procedure's own Scheme-visible name.
 * `static inline` is safe here since every module is compiled as an
 * independent shared-object translation unit -- no ODR/multiple-
 * definition concern across modules including this header, unlike a
 * header shared within a single statically-linked binary.
 *
 * Originally added ad hoc inside modules/qt6/qt6.cpp (issue #187) with
 * an identical implementation; promoted here so every other module
 * shares one implementation instead of reinventing it. qt6.cpp itself
 * was left as-is (not switched to include this header) to avoid
 * touching its already-reviewed, already-merged code for a pure
 * refactor with no behavior change.
 */

#ifndef CURRY_CHECKED_ARGS_H
#define CURRY_CHECKED_ARGS_H

#include <curry.h>

static inline const char *checked_string(curry_val v, int argpos, const char *who) {
    if (!curry_is_string(v)) curry_error("%s: argument %d must be a string", who, argpos);
    return curry_string(v);
}

static inline double checked_float(curry_val v, int argpos, const char *who) {
    if (!curry_is_fixnum(v) && !curry_is_float(v))
        curry_error("%s: argument %d must be a number", who, argpos);
    return curry_float(v);
}

static inline const char *checked_symbol(curry_val v, int argpos, const char *who) {
    if (!curry_is_symbol(v)) curry_error("%s: argument %d must be a symbol", who, argpos);
    return curry_symbol(v);
}

static inline intptr_t checked_fixnum(curry_val v, int argpos, const char *who) {
    if (!curry_is_fixnum(v)) curry_error("%s: argument %d must be an exact integer", who, argpos);
    return curry_fixnum(v);
}

#endif /* CURRY_CHECKED_ARGS_H */
