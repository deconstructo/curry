#ifndef CURRY_NUMERIC_H
#define CURRY_NUMERIC_H

/*
 * Numeric tower for Curry Scheme.
 *
 * Hierarchy (from narrowest / most exact to broadest):
 *
 *   fixnum  (62-bit C integer, immediate)
 *   bignum  (arbitrary precision integer, GMP mpz)
 *   rational (exact ratio p/q, GMP mpq, always normalized)
 *   flonum  (IEEE 754 double, inexact)
 *   complex  (real + imaginary, each any tower level)
 *   quaternion  (a + bi + cj + dk, all inexact double)
 *   octonion    (e0..e7, all inexact double)
 *
 * Arithmetic promotes automatically:  fixnum op fixnum -> fixnum (or bignum
 * on overflow), etc.  Mixing exact and inexact promotes to inexact.
 *
 * Quaternion multiplication is non-commutative.
 * Octonion multiplication is non-commutative and non-associative.
 */

#include "value.h"
#include <stdbool.h>

void num_init(void);

/* ---- Internal helpers (used by builtins) ---- */
#include <gmp.h>
val_t make_big_from_mpz(mpz_t z);

/* ---- Constructors ---- */
val_t num_make_bignum_i(long n);
val_t num_make_bignum_str(const char *s, int base);
val_t num_make_rational(val_t num, val_t den); /* auto-reduces; may return fixnum */
val_t num_make_float(double d);
val_t num_make_complex(val_t real, val_t imag);
val_t num_make_quat(double a, double b, double c, double d);
val_t num_make_oct(const double e[8]);
val_t num_make_tuple(int type, uint32_t n, val_t *data); /* T_UP or T_DOWN */

/* ---- Coercions ---- */
double     num_to_double(val_t v);   /* any number -> double (may lose precision) */
long       num_to_long(val_t v);     /* exact integer -> long (asserts) */
bool       num_to_bool(val_t v);     /* non-zero? */

val_t      num_exact(val_t v);       /* inexact -> exact */
val_t      num_inexact(val_t v);     /* exact -> inexact (flonum) */

/* ---- Predicates ---- */
bool num_is_zero(val_t v);
bool num_is_one(val_t v);
bool num_is_positive(val_t v);
bool num_is_negative(val_t v);
bool num_is_nan(val_t v);
bool num_is_infinite(val_t v);
bool num_is_finite(val_t v);
bool num_is_integer(val_t v);   /* exact or inexact integer value */

/* ---- Arithmetic ---- */
val_t num_add(val_t a, val_t b);
val_t num_sub(val_t a, val_t b);
val_t num_mul(val_t a, val_t b);
val_t num_div(val_t a, val_t b);
val_t num_neg(val_t a);
val_t num_abs(val_t a);

val_t num_quotient(val_t a, val_t b);
val_t num_remainder(val_t a, val_t b);
val_t num_modulo(val_t a, val_t b);
val_t num_gcd(val_t a, val_t b);
val_t num_lcm(val_t a, val_t b);

val_t num_floor(val_t v);
val_t num_ceiling(val_t v);
val_t num_truncate(val_t v);
val_t num_round(val_t v);    /* round half to even (banker's rounding) */

val_t num_expt(val_t base, val_t exp);
val_t num_sqrt(val_t v);
val_t num_exp(val_t v);
val_t num_log(val_t v);
val_t num_sin(val_t v);
val_t num_cos(val_t v);
val_t num_tan(val_t v);
val_t num_asin(val_t v);
val_t num_acos(val_t v);
val_t num_atan(val_t v);
val_t num_atan2(val_t y, val_t x);
val_t num_sinh(val_t v);
val_t num_cosh(val_t v);
val_t num_tanh(val_t v);
val_t num_asinh(val_t v);
val_t num_acosh(val_t v);
val_t num_atanh(val_t v);
val_t num_cot(val_t v);
val_t num_sec(val_t v);
val_t num_csc(val_t v);

/* ---- Comparison (exact and inexact) ---- */
int num_cmp(val_t a, val_t b);    /* -1 / 0 / +1 */
bool num_eq(val_t a, val_t b);
bool num_lt(val_t a, val_t b);
bool num_le(val_t a, val_t b);
bool num_gt(val_t a, val_t b);
bool num_ge(val_t a, val_t b);

val_t num_min(val_t a, val_t b);
val_t num_max(val_t a, val_t b);

/* ---- Bitwise (exact integers only) ---- */
val_t num_bitand(val_t a, val_t b);
val_t num_bitor(val_t a, val_t b);
val_t num_bitxor(val_t a, val_t b);
val_t num_bitnot(val_t a);
val_t num_shl(val_t a, int n);
val_t num_shr(val_t a, int n);
val_t num_bitlen(val_t a);  /* bit-length (floor(log2(|a|)) + 1) */

/* ---- Complex accessors ---- */
val_t num_real_part(val_t v);
val_t num_imag_part(val_t v);
val_t num_magnitude(val_t v);
val_t num_angle(val_t v);
val_t num_conjugate(val_t v);

/* ---- Quaternion accessors ---- */
val_t num_quat_a(val_t v);   /* scalar part */
val_t num_quat_b(val_t v);
val_t num_quat_c(val_t v);
val_t num_quat_d(val_t v);
val_t num_quat_norm(val_t v);
val_t num_quat_normalize(val_t v);
val_t num_quat_conjugate(val_t v);
val_t num_quat_inverse(val_t v);
/* rotate a 3D vector (as quaternion with zero scalar) */
val_t num_quat_rotate(val_t q, val_t v3);

/* ---- Octonion accessors ---- */
val_t num_oct_ref(val_t v, int i);   /* e[i], i in 0..7 */
val_t num_oct_norm(val_t v);
val_t num_oct_conjugate(val_t v);
val_t num_oct_inverse(val_t v);

/* ---- Number -> string ---- */
val_t num_to_string(val_t v, int radix);  /* radix 2/8/10/16 */
val_t num_from_string(val_t s, int radix);

/* Writes the shortest decimal string for `d` that round-trips back to the
 * exact same double via strtod (R7RS's "read the same number back" writer
 * convention), into buf (bufsize should be at least 32). Returns the
 * string length. Shared by num_to_string's flonum branch (numeric.c) and
 * scm_write's direct flonum case (port.c) -- both previously used a bare
 * "%g" (6 significant digits, nowhere near enough for a double) and lost
 * precision on anything with more than 6 significant digits, e.g.
 * (display 3.14159265358979) printed "3.14159". */
int num_flonum_to_shortest_cstr(double d, char *buf, size_t bufsize);

/* ---- Promotion helpers (internal use) ---- */
val_t num_normalize(val_t v);  /* reduce e.g. bignum that fits in fixnum */

/* ---- Sexagesimal (Babylonian/Neugebauer) I/O — v1.2.5 ---- */
/* Check whether a codepoint is in the Cuneiform Unicode block */
#define SEX_IS_CUNEIFORM(cp) ((uint32_t)(cp) >= 0x12000u && (uint32_t)(cp) <= 0x1247Fu)

int   sex_decode_cp(const char **s);          /* decode one UTF-8 codepoint, advance *s; -1 at end */
val_t sex_parse_neugebauer(const char *s);   /* "1;30" → 3/2, "1,0,0" → 3600 */
val_t sex_parse_cuneiform(const char *s);    /* "𒁹 𒌋𒁹" → 71 */
val_t sex_to_neugebauer(val_t v, int max_frac); /* < 0 = auto */
val_t sex_to_cuneiform(val_t v);

/* Current number notation global — 0 = decimal, else a symbol */
extern val_t g_number_notation;

#endif /* CURRY_NUMERIC_H */
