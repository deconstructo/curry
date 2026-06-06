/*
 * Number theory module (src/numtheory.c) — always compiled, GMP-only.
 *
 * Provides primality, factoring, divisor functions, modular arithmetic,
 * combinatorics (Fibonacci, Lucas, Bernoulli, Stirling, Bell, partitions),
 * Jacobi/Kronecker/Legendre symbols, Chinese Remainder, and continued
 * fractions / best rational approximation.
 *
 * No MPFR dependency.  Result types are exact (fixnum / bignum / rational)
 * everywhere they can be.
 */

#include "numtheory.h"
#include "object.h"
#include "env.h"
#include "symbol.h"
#include "numeric.h"
#include "builtins.h"
#include "eval.h"
#include "gc.h"
#include <gmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define DEF(name, fn, min, max) defprim(env, name, fn, min, max)

/* ---------------------------------------------------------------------- */
/* Internal: load a non-negative exact integer into mpz_t                  */
/* ---------------------------------------------------------------------- */
static void load_z(mpz_t out, val_t v, const char *ctx) {
    if (vis_fixnum(v))      mpz_set_si(out, (long)vunfix(v));
    else if (vis_bignum(v)) mpz_set   (out, as_big(v)->z);
    else scm_raise(V_FALSE, "%s: not an exact integer", ctx);
}

static val_t z_to_val(mpz_t z) { return make_big_from_mpz(z); }

/* ---------------------------------------------------------------------- */
/* Primality                                                              */
/* ---------------------------------------------------------------------- */

static val_t prim_prime_p(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    mpz_t z; mpz_init(z); load_z(z, av[0], "prime?");
    /* Miller-Rabin with 25 rounds — same rigor as GMP's own next-prime. */
    int r = mpz_probab_prime_p(z, 25);
    mpz_clear(z);
    /* r==2 → definitely prime, r==1 → probably prime, r==0 → composite. */
    return vbool(r > 0);
}

static val_t prim_next_prime(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    mpz_t z, r; mpz_init(z); mpz_init(r);
    load_z(z, av[0], "next-prime");
    mpz_nextprime(r, z);
    val_t out = z_to_val(r);
    mpz_clear(z); mpz_clear(r);
    return out;
}

static val_t prim_prev_prime(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    mpz_t z; mpz_init(z); load_z(z, av[0], "prev-prime");
    if (mpz_cmp_ui(z, 3) <= 0) {
        mpz_clear(z);
        scm_raise(V_FALSE, "prev-prime: no prime less than %s",
                  mpz_cmp_ui(z, 3) == 0 ? "3" : "n");
    }
    /* Decrement by 1 then step down by 2, testing each candidate. */
    if (mpz_even_p(z)) mpz_sub_ui(z, z, 1);
    else               mpz_sub_ui(z, z, 2);
    while (mpz_probab_prime_p(z, 25) == 0) mpz_sub_ui(z, z, 2);
    val_t out = z_to_val(z);
    mpz_clear(z);
    return out;
}

/* ---------------------------------------------------------------------- */
/* Factoring — trial division up to TRIAL_LIMIT, then Brent-Pollard ρ.    */
/* ---------------------------------------------------------------------- */

#define TRIAL_LIMIT 100000UL

/* Brent's variant of Pollard rho.  Returns true on success and writes the
 * factor into `factor`.  Returns false if no factor was found (caller may
 * retry with a different `c`). */
static bool brent_pollard(mpz_t factor, const mpz_t n, unsigned long c_init) {
    /* If n is even, 2 is a factor. */
    if (mpz_even_p(n)) { mpz_set_ui(factor, 2); return true; }
    if (mpz_cmp_ui(n, 1) <= 0) return false;

    mpz_t x, y, ys, q, c, d, tmp;
    mpz_inits(x, y, ys, q, c, d, tmp, NULL);
    mpz_set_ui(y, 2);
    mpz_set_ui(c, c_init);
    mpz_set_ui(q, 1);

    unsigned long r_step = 1;
    const unsigned long M = 128;
    bool found = false;

    /* Outer loop: scale r_step by 2 each iteration. */
    for (int iter = 0; iter < 40 && !found; iter++) {
        mpz_set(x, y);
        for (unsigned long i = 0; i < r_step; i++) {
            /* y = (y*y + c) mod n */
            mpz_mul(tmp, y, y); mpz_add(tmp, tmp, c); mpz_mod(y, tmp, n);
        }
        unsigned long k = 0;
        while (k < r_step && !found) {
            mpz_set(ys, y);
            unsigned long step = (r_step - k < M) ? (r_step - k) : M;
            for (unsigned long i = 0; i < step; i++) {
                mpz_mul(tmp, y, y); mpz_add(tmp, tmp, c); mpz_mod(y, tmp, n);
                mpz_sub(tmp, x, y); mpz_abs(tmp, tmp);
                mpz_mul(q, q, tmp); mpz_mod(q, q, n);
            }
            mpz_gcd(d, q, n);
            if (mpz_cmp_ui(d, 1) > 0) {
                if (mpz_cmp(d, n) == 0) {
                    /* Backtrack: replay with single-step gcd. */
                    do {
                        mpz_mul(tmp, ys, ys); mpz_add(tmp, tmp, c); mpz_mod(ys, tmp, n);
                        mpz_sub(tmp, x, ys); mpz_abs(tmp, tmp);
                        mpz_gcd(d, tmp, n);
                    } while (mpz_cmp_ui(d, 1) <= 0);
                    if (mpz_cmp(d, n) == 0) break;  /* failure */
                }
                mpz_set(factor, d);
                found = true;
                break;
            }
            k += step;
        }
        r_step *= 2;
    }

    mpz_clears(x, y, ys, q, c, d, tmp, NULL);
    return found;
}

/* Append prime factors of n into the list (in ascending order).
 * `head` is updated in place; `n` is reduced in place. */
static void factor_into(mpz_t n, val_t *factor_list, bool *sorted) {
    if (mpz_cmp_ui(n, 1) <= 0) return;

    /* Trial division. */
    mpz_t tmp; mpz_init(tmp);
    for (unsigned long p = 2; p <= TRIAL_LIMIT; ) {
        if (mpz_cmp_ui(n, 1) == 0) break;
        mpz_mod_ui(tmp, n, p);
        if (mpz_cmp_ui(tmp, 0) == 0) {
            *factor_list = scm_cons(num_make_bignum_i((long)p), *factor_list);
            mpz_divexact_ui(n, n, p);
        } else {
            /* If p*p > n, then n itself is prime (or 1) */
            mpz_t pp; mpz_init(pp); mpz_set_ui(pp, p);
            mpz_mul(pp, pp, pp);
            int gt = mpz_cmp(pp, n) > 0;
            mpz_clear(pp);
            if (gt) {
                if (mpz_cmp_ui(n, 1) > 0) {
                    *factor_list = scm_cons(z_to_val(n), *factor_list);
                    mpz_set_ui(n, 1);
                    *sorted = false;
                }
                mpz_clear(tmp); return;
            }
            p = (p == 2) ? 3 : p + 2;
        }
    }
    mpz_clear(tmp);

    /* Remaining is composite or prime > TRIAL_LIMIT.  Use Pollard ρ. */
    while (mpz_cmp_ui(n, 1) > 0) {
        if (mpz_probab_prime_p(n, 25) > 0) {
            *factor_list = scm_cons(z_to_val(n), *factor_list);
            mpz_set_ui(n, 1);
            *sorted = false;
            break;
        }
        mpz_t f; mpz_init(f);
        bool ok = false;
        for (unsigned long c = 1; c < 32 && !ok; c++) ok = brent_pollard(f, n, c);
        if (!ok) {
            /* Should not happen for moderate inputs.  Give up. */
            *factor_list = scm_cons(z_to_val(n), *factor_list);
            mpz_set_ui(n, 1);
            *sorted = false;
            mpz_clear(f);
            break;
        }
        /* Recurse on the found factor. */
        factor_into(f, factor_list, sorted);
        /* And on the cofactor. */
        mpz_divexact(n, n, f);
        mpz_clear(f);
    }
}

/* Sort a list of small numbers ascending.  All elements are exact integers. */
static int int_cmp(const void *a, const void *b) {
    val_t va = *(const val_t *)a;
    val_t vb = *(const val_t *)b;
    return num_cmp(va, vb);
}

static val_t sort_int_list(val_t lst) {
    int n = scm_list_length(lst);
    if (n <= 1) return lst;
    val_t *buf = (val_t *)malloc((size_t)n * sizeof(val_t));
    int i = 0;
    for (val_t p = lst; vis_pair(p); p = vcdr(p)) buf[i++] = vcar(p);
    qsort(buf, (size_t)n, sizeof(val_t), int_cmp);
    val_t r = V_NIL;
    for (i = n - 1; i >= 0; i--) r = scm_cons(buf[i], r);
    free(buf);
    return r;
}

static val_t prim_factor(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    mpz_t n; mpz_init(n); load_z(n, av[0], "factor");
    if (mpz_sgn(n) <= 0) { mpz_clear(n); return V_NIL; }
    val_t list = V_NIL;
    bool sorted = true;
    factor_into(n, &list, &sorted);
    mpz_clear(n);
    /* Trial division produces factors in ascending order — but they were
     * cons'd onto the head, so the result is descending.  Reverse to ascending.
     * If Pollard ρ produced anything, run a stable sort. */
    list = scm_reverse(list);
    if (!sorted) list = sort_int_list(list);
    return list;
}

static val_t dedup_sorted(val_t lst) {
    if (!vis_pair(lst)) return lst;
    val_t out = V_NIL, prev = V_FALSE;
    for (val_t p = lst; vis_pair(p); p = vcdr(p)) {
        val_t cur = vcar(p);
        if (vis_false(prev) || !num_eq(cur, prev)) {
            out = scm_cons(cur, out);
            prev = cur;
        }
    }
    return scm_reverse(out);
}

static val_t prim_prime_factors(int ac, val_t *av, void *ud) {
    val_t f = prim_factor(ac, av, ud);
    return dedup_sorted(f);
}

/* ---------------------------------------------------------------------- */
/* Arithmetic functions                                                    */
/* ---------------------------------------------------------------------- */

static val_t prim_totient(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    mpz_t n; mpz_init(n); load_z(n, av[0], "totient");
    if (mpz_cmp_ui(n, 1) <= 0) {
        val_t r = (mpz_cmp_ui(n, 1) == 0) ? vfix(1) : vfix(0);
        mpz_clear(n); return r;
    }
    /* φ(n) = n * ∏ (1 - 1/p) over distinct primes p|n */
    val_t primes = prim_prime_factors(ac, av, ud);
    mpz_t result, p_z, tmp;
    mpz_inits(result, p_z, tmp, NULL);
    mpz_set(result, n);
    for (val_t l = primes; vis_pair(l); l = vcdr(l)) {
        load_z(p_z, vcar(l), "totient");
        /* result = result / p * (p-1) */
        mpz_divexact(result, result, p_z);
        mpz_sub_ui(tmp, p_z, 1);
        mpz_mul(result, result, tmp);
    }
    val_t out = z_to_val(result);
    mpz_clears(n, result, p_z, tmp, NULL);
    return out;
}

/* Run-length-encode a sorted list of prime factors into a list of
 * ((p . e) ...) where e is the multiplicity. */
static val_t rle_factors(val_t sorted_factors) {
    val_t out = V_NIL;
    val_t cur_p = V_FALSE; long cur_e = 0;
    for (val_t p = sorted_factors; vis_pair(p); p = vcdr(p)) {
        val_t v = vcar(p);
        if (vis_false(cur_p) || !num_eq(v, cur_p)) {
            if (!vis_false(cur_p))
                out = scm_cons(scm_cons(cur_p, vfix(cur_e)), out);
            cur_p = v; cur_e = 1;
        } else {
            cur_e++;
        }
    }
    if (!vis_false(cur_p))
        out = scm_cons(scm_cons(cur_p, vfix(cur_e)), out);
    return scm_reverse(out);
}

static val_t prim_mobius(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    mpz_t n; mpz_init(n); load_z(n, av[0], "mobius");
    if (mpz_cmp_ui(n, 1) == 0) { mpz_clear(n); return vfix(1); }
    if (mpz_cmp_ui(n, 1) < 0)  { mpz_clear(n); return vfix(0); }
    val_t factors = prim_factor(ac, av, ud);
    val_t rle = rle_factors(factors);
    int omega = 0;
    for (val_t p = rle; vis_pair(p); p = vcdr(p)) {
        val_t pair = vcar(p);
        long e = (long)vunfix(vcdr(pair));
        if (e > 1) { mpz_clear(n); return vfix(0); }
        omega++;
    }
    mpz_clear(n);
    return vfix((omega & 1) ? -1 : 1);
}

static val_t prim_divisors(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    mpz_t n; mpz_init(n); load_z(n, av[0], "divisors");
    if (mpz_sgn(n) <= 0) { mpz_clear(n); return V_NIL; }
    val_t factors = prim_factor(ac, av, ud);
    val_t rle = rle_factors(factors);

    /* Build divisor list by combining each prime power. */
    val_t out = scm_cons(vfix(1), V_NIL);
    for (val_t p = rle; vis_pair(p); p = vcdr(p)) {
        val_t pair = vcar(p);
        val_t prime = vcar(pair);
        long e = (long)vunfix(vcdr(pair));
        val_t additions = V_NIL;
        val_t p_power = vfix(1);
        for (long k = 1; k <= e; k++) {
            p_power = num_mul(p_power, prime);
            for (val_t d = out; vis_pair(d); d = vcdr(d))
                additions = scm_cons(num_mul(vcar(d), p_power), additions);
        }
        for (val_t a = additions; vis_pair(a); a = vcdr(a))
            out = scm_cons(vcar(a), out);
    }
    mpz_clear(n);
    return sort_int_list(out);
}

static val_t prim_divisor_count(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t factors = prim_factor(ac, av, ud);
    val_t rle = rle_factors(factors);
    long count = 1;
    for (val_t p = rle; vis_pair(p); p = vcdr(p)) {
        long e = (long)vunfix(vcdr(vcar(p)));
        count *= (e + 1);
    }
    return num_make_bignum_i(count);
}

static val_t prim_divisor_sum(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    mpz_t n; mpz_init(n); load_z(n, av[0], "divisor-sum");
    if (mpz_sgn(n) <= 0) { mpz_clear(n); return vfix(0); }
    if (mpz_cmp_ui(n, 1) == 0) { mpz_clear(n); return vfix(1); }
    val_t factors = prim_factor(ac, av, ud);
    val_t rle = rle_factors(factors);
    /* σ(n) = ∏ (p^(e+1) - 1) / (p - 1) */
    val_t sum = vfix(1);
    for (val_t p = rle; vis_pair(p); p = vcdr(p)) {
        val_t pair = vcar(p);
        val_t prime = vcar(pair);
        long e = (long)vunfix(vcdr(pair));
        val_t num = num_sub(num_expt(prime, vfix(e + 1)), vfix(1));
        val_t den = num_sub(prime, vfix(1));
        val_t part = num_div(num, den);
        sum = num_mul(sum, part);
    }
    mpz_clear(n);
    return sum;
}

static val_t prim_perfect_p(int ac, val_t *av, void *ud) {
    val_t s = prim_divisor_sum(ac, av, ud);
    val_t two_n = num_mul(vfix(2), av[0]);
    return vbool(num_eq(s, two_n));
}
static val_t prim_abundant_p(int ac, val_t *av, void *ud) {
    val_t s = prim_divisor_sum(ac, av, ud);
    val_t two_n = num_mul(vfix(2), av[0]);
    return vbool(num_gt(s, two_n));
}
static val_t prim_deficient_p(int ac, val_t *av, void *ud) {
    val_t s = prim_divisor_sum(ac, av, ud);
    val_t two_n = num_mul(vfix(2), av[0]);
    return vbool(num_lt(s, two_n));
}

static val_t prim_omega(int ac, val_t *av, void *ud) {
    val_t primes = prim_prime_factors(ac, av, ud);
    return num_make_bignum_i((long)scm_list_length(primes));
}
static val_t prim_big_omega(int ac, val_t *av, void *ud) {
    val_t factors = prim_factor(ac, av, ud);
    return num_make_bignum_i((long)scm_list_length(factors));
}

/* Carmichael λ.  λ(p^k) is well-defined; combine with LCM. */
static val_t prim_carmichael(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    mpz_t n; mpz_init(n); load_z(n, av[0], "carmichael");
    if (mpz_cmp_ui(n, 1) <= 0) { mpz_clear(n); return vfix(1); }
    val_t factors = prim_factor(ac, av, ud);
    val_t rle = rle_factors(factors);
    val_t result = vfix(1);
    for (val_t p = rle; vis_pair(p); p = vcdr(p)) {
        val_t pair = vcar(p);
        val_t prime = vcar(pair);
        long e = (long)vunfix(vcdr(pair));
        val_t lam;
        /* λ(2)=1, λ(4)=2, λ(2^k)=2^(k-2) for k≥3. */
        if (vis_fixnum(prime) && vunfix(prime) == 2) {
            if (e == 1)      lam = vfix(1);
            else if (e == 2) lam = vfix(2);
            else             lam = num_expt(vfix(2), vfix(e - 2));
        } else {
            /* λ(p^k) = p^(k-1) · (p-1) */
            val_t pkminus1 = num_expt(prime, vfix(e - 1));
            lam = num_mul(pkminus1, num_sub(prime, vfix(1)));
        }
        result = num_lcm(result, lam);
    }
    mpz_clear(n);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Modular arithmetic                                                     */
/* ---------------------------------------------------------------------- */

static val_t prim_mod_expt(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    mpz_t b, e, m, r; mpz_inits(b, e, m, r, NULL);
    load_z(b, av[0], "mod-expt");
    load_z(e, av[1], "mod-expt");
    load_z(m, av[2], "mod-expt");
    if (mpz_sgn(m) == 0) { mpz_clears(b,e,m,r,NULL); scm_raise(V_FALSE, "mod-expt: modulus is zero"); }
    mpz_powm(r, b, e, m);
    val_t out = z_to_val(r);
    mpz_clears(b, e, m, r, NULL);
    return out;
}

static val_t prim_mod_inverse(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    mpz_t a, m, r; mpz_inits(a, m, r, NULL);
    load_z(a, av[0], "mod-inverse");
    load_z(m, av[1], "mod-inverse");
    if (mpz_invert(r, a, m) == 0) {
        mpz_clears(a, m, r, NULL);
        scm_raise(V_FALSE, "mod-inverse: not invertible");
    }
    val_t out = z_to_val(r);
    mpz_clears(a, m, r, NULL);
    return out;
}

static val_t prim_jacobi(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    mpz_t a, n; mpz_inits(a, n, NULL);
    load_z(a, av[0], "jacobi-symbol");
    load_z(n, av[1], "jacobi-symbol");
    int j = mpz_jacobi(a, n);
    mpz_clears(a, n, NULL);
    return vfix(j);
}

static val_t prim_kronecker(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    mpz_t a, n; mpz_inits(a, n, NULL);
    load_z(a, av[0], "kronecker-symbol");
    load_z(n, av[1], "kronecker-symbol");
    int k = mpz_kronecker(a, n);
    mpz_clears(a, n, NULL);
    return vfix(k);
}

static val_t prim_legendre(int ac, val_t *av, void *ud) {
    /* Legendre is Jacobi for odd primes; GMP's mpz_jacobi already handles it. */
    return prim_jacobi(ac, av, ud);
}

/* (extended-gcd a b) → (values gcd s t)  with a·s + b·t = gcd. */
static val_t prim_extended_gcd(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    mpz_t a, b, g, s, t; mpz_inits(a, b, g, s, t, NULL);
    load_z(a, av[0], "extended-gcd");
    load_z(b, av[1], "extended-gcd");
    mpz_gcdext(g, s, t, a, b);
    val_t triple[3] = { z_to_val(g), z_to_val(s), z_to_val(t) };
    mpz_clears(a, b, g, s, t, NULL);
    Values *vv = (Values *)gc_alloc(sizeof(Values) + 3*sizeof(val_t));
    vv->hdr.type = T_VALUES; vv->hdr.flags = 0; vv->count = 3;
    vv->vals[0] = triple[0]; vv->vals[1] = triple[1]; vv->vals[2] = triple[2];
    return vptr(vv);
}

/* (chinese-remainder rems mods) — pairwise-coprime mods. */
static val_t prim_crt(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t rems = av[0], mods = av[1];
    int nr = scm_list_length(rems), nm = scm_list_length(mods);
    if (nr < 0 || nm < 0 || nr != nm)
        scm_raise(V_FALSE, "chinese-remainder: lists must be the same length");
    if (nr == 0) return vfix(0);
    mpz_t cur_r, cur_m, ri, mi, g, s, t, tmp, new_m;
    mpz_inits(cur_r, cur_m, ri, mi, g, s, t, tmp, new_m, NULL);
    load_z(cur_r, vcar(rems), "chinese-remainder");
    load_z(cur_m, vcar(mods), "chinese-remainder");
    val_t r_rest = vcdr(rems), m_rest = vcdr(mods);
    while (vis_pair(r_rest)) {
        load_z(ri, vcar(r_rest), "chinese-remainder");
        load_z(mi, vcar(m_rest), "chinese-remainder");
        /* Solve: cur_r + cur_m * k ≡ ri (mod mi) for k.
         * (cur_m * k) ≡ (ri - cur_r) (mod mi).  Requires gcd(cur_m, mi) | (ri - cur_r). */
        mpz_gcdext(g, s, t, cur_m, mi);
        mpz_sub(tmp, ri, cur_r);
        mpz_t q, rem; mpz_inits(q, rem, NULL);
        mpz_fdiv_qr(q, rem, tmp, g);
        if (mpz_sgn(rem) != 0) {
            mpz_clears(cur_r, cur_m, ri, mi, g, s, t, tmp, new_m, q, rem, NULL);
            scm_raise(V_FALSE, "chinese-remainder: incompatible residues");
        }
        /* k = q * s mod (mi/g) */
        mpz_t mi_over_g; mpz_init(mi_over_g);
        mpz_divexact(mi_over_g, mi, g);
        mpz_mul(q, q, s);
        mpz_mod(q, q, mi_over_g);
        /* new_r = cur_r + cur_m * k */
        mpz_mul(tmp, cur_m, q);
        mpz_add(cur_r, cur_r, tmp);
        /* new_m = cur_m * mi / g */
        mpz_mul(new_m, cur_m, mi_over_g);
        mpz_set(cur_m, new_m);
        mpz_mod(cur_r, cur_r, cur_m);
        mpz_clear(mi_over_g); mpz_clears(q, rem, NULL);
        r_rest = vcdr(r_rest); m_rest = vcdr(m_rest);
    }
    val_t out = z_to_val(cur_r);
    mpz_clears(cur_r, cur_m, ri, mi, g, s, t, tmp, new_m, NULL);
    return out;
}

/* ---------------------------------------------------------------------- */
/* Sequences                                                              */
/* ---------------------------------------------------------------------- */

/* Fibonacci via fast doubling.  Returns fib(n). */
static void fib_doubling(unsigned long n, mpz_t f_n, mpz_t f_n1) {
    if (n == 0) { mpz_set_ui(f_n, 0); mpz_set_ui(f_n1, 1); return; }
    mpz_t a, b, c, d, tmp;
    mpz_inits(a, b, c, d, tmp, NULL);
    fib_doubling(n >> 1, a, b);
    /* c = a*(2b - a), d = a² + b² */
    mpz_mul_2exp(tmp, b, 1);
    mpz_sub(tmp, tmp, a);
    mpz_mul(c, a, tmp);
    mpz_mul(d, a, a);
    mpz_mul(tmp, b, b);
    mpz_add(d, d, tmp);
    if (n & 1) {
        mpz_set(f_n, d);
        mpz_add(f_n1, c, d);
    } else {
        mpz_set(f_n, c);
        mpz_set(f_n1, d);
    }
    mpz_clears(a, b, c, d, tmp, NULL);
}

static val_t prim_fibonacci(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_fixnum(av[0]) && !vis_bignum(av[0]))
        scm_raise(V_FALSE, "fibonacci: not an exact integer");
    long n;
    if (vis_fixnum(av[0])) n = (long)vunfix(av[0]);
    else { if (!mpz_fits_slong_p(as_big(av[0])->z)) scm_raise(V_FALSE, "fibonacci: index too large");
           n = mpz_get_si(as_big(av[0])->z); }
    if (n < 0) scm_raise(V_FALSE, "fibonacci: negative index");
    mpz_t fn, fn1; mpz_inits(fn, fn1, NULL);
    fib_doubling((unsigned long)n, fn, fn1);
    val_t out = z_to_val(fn);
    mpz_clears(fn, fn1, NULL);
    return out;
}

static val_t prim_lucas(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_fixnum(av[0]) && !vis_bignum(av[0]))
        scm_raise(V_FALSE, "lucas: not an exact integer");
    long n;
    if (vis_fixnum(av[0])) n = (long)vunfix(av[0]);
    else { if (!mpz_fits_slong_p(as_big(av[0])->z)) scm_raise(V_FALSE, "lucas: index too large");
           n = mpz_get_si(as_big(av[0])->z); }
    if (n < 0) scm_raise(V_FALSE, "lucas: negative index");
    /* L(n) = F(n-1) + F(n+1), and L(0)=2 L(1)=1. */
    if (n == 0) return vfix(2);
    if (n == 1) return vfix(1);
    mpz_t fn, fn1, L; mpz_inits(fn, fn1, L, NULL);
    fib_doubling((unsigned long)n, fn, fn1);
    /* L(n) = 2*F(n+1) - F(n) */
    mpz_mul_2exp(L, fn1, 1);
    mpz_sub(L, L, fn);
    val_t out = z_to_val(L);
    mpz_clears(fn, fn1, L, NULL);
    return out;
}

static val_t prim_binomial(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_fixnum(av[0]) && !vis_bignum(av[0]))
        scm_raise(V_FALSE, "binomial: n must be an exact integer");
    if (!vis_fixnum(av[1]))
        scm_raise(V_FALSE, "binomial: k must be a fixnum");
    long k = (long)vunfix(av[1]);
    if (k < 0) return vfix(0);
    mpz_t n, r; mpz_inits(n, r, NULL);
    load_z(n, av[0], "binomial");
    mpz_bin_ui(r, n, (unsigned long)k);
    val_t out = z_to_val(r);
    mpz_clears(n, r, NULL);
    return out;
}

static val_t prim_multinomial(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    /* (multinomial n (k1 k2 ...)) = n! / (k1! * k2! * ...) where Σ kᵢ = n. */
    if (!vis_fixnum(av[0])) scm_raise(V_FALSE, "multinomial: n must be a fixnum");
    long n = (long)vunfix(av[0]);
    val_t ks = av[1];
    mpz_t num, denom, fac, tmp;
    mpz_inits(num, denom, fac, tmp, NULL);
    mpz_fac_ui(num, (unsigned long)n);
    mpz_set_ui(denom, 1);
    long sum = 0;
    for (val_t p = ks; vis_pair(p); p = vcdr(p)) {
        if (!vis_fixnum(vcar(p))) { mpz_clears(num,denom,fac,tmp,NULL); scm_raise(V_FALSE, "multinomial: ks must be fixnums"); }
        long k = (long)vunfix(vcar(p));
        if (k < 0) { mpz_clears(num,denom,fac,tmp,NULL); scm_raise(V_FALSE, "multinomial: negative k"); }
        sum += k;
        mpz_fac_ui(fac, (unsigned long)k);
        mpz_mul(denom, denom, fac);
    }
    if (sum != n) { mpz_clears(num,denom,fac,tmp,NULL); scm_raise(V_FALSE, "multinomial: ks must sum to n"); }
    mpz_divexact(num, num, denom);
    val_t out = z_to_val(num);
    mpz_clears(num, denom, fac, tmp, NULL);
    return out;
}

static val_t prim_catalan(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_fixnum(av[0])) scm_raise(V_FALSE, "catalan: not a fixnum");
    long n = (long)vunfix(av[0]);
    if (n < 0) return vfix(0);
    mpz_t bin, r; mpz_inits(bin, r, NULL);
    mpz_set_ui(bin, 1);
    /* C(n) = C(2n, n) / (n+1) */
    mpz_t two_n; mpz_init(two_n); mpz_set_si(two_n, 2*n);
    mpz_bin_ui(bin, two_n, (unsigned long)n);
    mpz_fdiv_q_ui(r, bin, (unsigned long)(n + 1));
    val_t out = z_to_val(r);
    mpz_clears(bin, r, two_n, NULL);
    return out;
}

/* ---- Bernoulli numbers (exact rationals).  Cached statically. ---- */

#define BERNOULLI_CACHE_SIZE 256
static mpq_t  bernoulli_cache[BERNOULLI_CACHE_SIZE];
static int    bernoulli_cached[BERNOULLI_CACHE_SIZE];

static void compute_bernoulli(int m) {
    /* B(0) = 1; for m ≥ 1: B(m) = -1/(m+1) Σ_{k=0..m-1} C(m+1,k) * B(k). */
    if (bernoulli_cached[m]) return;
    if (m == 0) { mpq_init(bernoulli_cache[0]); mpq_set_ui(bernoulli_cache[0], 1, 1); bernoulli_cached[0]=1; return; }
    /* Pre-compute lower indices recursively. */
    for (int k = 0; k < m; k++) compute_bernoulli(k);

    mpq_t sum, term; mpq_init(sum); mpq_init(term);
    mpz_t bin; mpz_init(bin);
    mpz_t mp1; mpz_init_set_si(mp1, m + 1);
    for (int k = 0; k < m; k++) {
        mpz_bin_ui(bin, mp1, (unsigned long)k);
        mpq_set_z(term, bin);
        mpq_mul(term, term, bernoulli_cache[k]);
        mpq_add(sum, sum, term);
    }
    /* B(m) = -sum / (m+1) */
    mpq_t neg_recip; mpq_init(neg_recip);
    mpq_set_si(neg_recip, -1, 1);
    mpq_t mp1q; mpq_init(mp1q);
    mpq_set_z(mp1q, mp1);
    mpq_div(neg_recip, neg_recip, mp1q);
    mpq_mul(sum, sum, neg_recip);
    mpq_canonicalize(sum);
    mpq_init(bernoulli_cache[m]); mpq_set(bernoulli_cache[m], sum);
    bernoulli_cached[m] = 1;
    mpq_clear(sum); mpq_clear(term); mpq_clear(neg_recip); mpq_clear(mp1q);
    mpz_clear(bin); mpz_clear(mp1);
}

static val_t prim_bernoulli(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_fixnum(av[0])) scm_raise(V_FALSE, "bernoulli: not a fixnum");
    long n = (long)vunfix(av[0]);
    if (n < 0)  scm_raise(V_FALSE, "bernoulli: negative index");
    if (n >= BERNOULLI_CACHE_SIZE) scm_raise(V_FALSE, "bernoulli: index too large");
    compute_bernoulli((int)n);
    /* Convert mpq to val_t (rational, or integer if denom = 1). */
    return num_make_rational(make_big_from_mpz(mpq_numref(bernoulli_cache[n])),
                             make_big_from_mpz(mpq_denref(bernoulli_cache[n])));
}

/* Euler numbers — even-indexed nonzero, odd-indexed zero.  All exact integers. */
#define EULER_CACHE_SIZE 256
static mpz_t  euler_cache[EULER_CACHE_SIZE];
static int    euler_cached[EULER_CACHE_SIZE];

static void compute_euler(int n) {
    if (euler_cached[n]) return;
    if (n == 0) { mpz_init_set_si(euler_cache[0], 1); euler_cached[0]=1; return; }
    if (n & 1)  { mpz_init_set_si(euler_cache[n], 0); euler_cached[n]=1; return; }
    /* E(2n) = -Σ_{k=0..n-1} C(2n, 2k) E(2k). */
    int half = n / 2;
    for (int k = 0; k < half; k++) compute_euler(2*k);
    mpz_t sum, bin, prod; mpz_inits(sum, bin, prod, NULL);
    mpz_set_ui(sum, 0);
    mpz_t two_n; mpz_init_set_si(two_n, n);
    for (int k = 0; k < half; k++) {
        mpz_bin_ui(bin, two_n, (unsigned long)(2*k));
        mpz_mul(prod, bin, euler_cache[2*k]);
        mpz_add(sum, sum, prod);
    }
    mpz_neg(sum, sum);
    mpz_init(euler_cache[n]); mpz_set(euler_cache[n], sum);
    euler_cached[n] = 1;
    mpz_clears(sum, bin, prod, two_n, NULL);
}

static val_t prim_euler_number(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_fixnum(av[0])) scm_raise(V_FALSE, "euler-number: not a fixnum");
    long n = (long)vunfix(av[0]);
    if (n < 0) scm_raise(V_FALSE, "euler-number: negative index");
    if (n >= EULER_CACHE_SIZE) scm_raise(V_FALSE, "euler-number: index too large");
    compute_euler((int)n);
    return z_to_val(euler_cache[n]);
}

/* Stirling numbers — recurrence tables, O(n*k).  Returned as bignums. */
static val_t prim_stirling1(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_fixnum(av[0]) || !vis_fixnum(av[1]))
        scm_raise(V_FALSE, "stirling1: arguments must be fixnums");
    long n = (long)vunfix(av[0]), k = (long)vunfix(av[1]);
    if (n < 0 || k < 0) scm_raise(V_FALSE, "stirling1: negative index");
    if (k > n) return vfix(0);
    if (n == 0 && k == 0) return vfix(1);
    /* Row-by-row.  s[n][k] = (n-1)*s[n-1][k] + s[n-1][k-1]. */
    mpz_t *prev = (mpz_t *)calloc((size_t)(n + 1), sizeof(mpz_t));
    mpz_t *cur  = (mpz_t *)calloc((size_t)(n + 1), sizeof(mpz_t));
    for (long i = 0; i <= n; i++) { mpz_init(prev[i]); mpz_init(cur[i]); }
    mpz_set_ui(prev[0], 1);
    for (long i = 1; i <= n; i++) {
        mpz_set_ui(cur[0], 0);
        for (long j = 1; j <= i; j++) {
            /* cur[j] = (i-1)*prev[j] + prev[j-1] */
            mpz_t tmp; mpz_init(tmp);
            mpz_mul_si(tmp, prev[j], i - 1);
            mpz_add(cur[j], tmp, prev[j-1]);
            mpz_clear(tmp);
        }
        for (long j = 0; j <= n; j++) mpz_swap(prev[j], cur[j]);
    }
    val_t out = z_to_val(prev[k]);
    for (long i = 0; i <= n; i++) { mpz_clear(prev[i]); mpz_clear(cur[i]); }
    free(prev); free(cur);
    return out;
}

static val_t prim_stirling2(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_fixnum(av[0]) || !vis_fixnum(av[1]))
        scm_raise(V_FALSE, "stirling2: arguments must be fixnums");
    long n = (long)vunfix(av[0]), k = (long)vunfix(av[1]);
    if (n < 0 || k < 0) scm_raise(V_FALSE, "stirling2: negative index");
    if (k > n) return vfix(0);
    if (n == 0 && k == 0) return vfix(1);
    mpz_t *prev = (mpz_t *)calloc((size_t)(n + 1), sizeof(mpz_t));
    mpz_t *cur  = (mpz_t *)calloc((size_t)(n + 1), sizeof(mpz_t));
    for (long i = 0; i <= n; i++) { mpz_init(prev[i]); mpz_init(cur[i]); }
    mpz_set_ui(prev[0], 1);
    for (long i = 1; i <= n; i++) {
        mpz_set_ui(cur[0], 0);
        for (long j = 1; j <= i; j++) {
            /* cur[j] = j * prev[j] + prev[j-1] */
            mpz_t tmp; mpz_init(tmp);
            mpz_mul_si(tmp, prev[j], j);
            mpz_add(cur[j], tmp, prev[j-1]);
            mpz_clear(tmp);
        }
        for (long j = 0; j <= n; j++) mpz_swap(prev[j], cur[j]);
    }
    val_t out = z_to_val(prev[k]);
    for (long i = 0; i <= n; i++) { mpz_clear(prev[i]); mpz_clear(cur[i]); }
    free(prev); free(cur);
    return out;
}

/* Bell numbers via Bell triangle. */
static val_t prim_bell(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_fixnum(av[0])) scm_raise(V_FALSE, "bell: not a fixnum");
    long n = (long)vunfix(av[0]);
    if (n < 0) scm_raise(V_FALSE, "bell: negative index");
    if (n == 0) return vfix(1);
    /* B(n) = Σ S(n,k) for k=0..n. */
    mpz_t *prev = (mpz_t *)calloc((size_t)(n + 1), sizeof(mpz_t));
    mpz_t *cur  = (mpz_t *)calloc((size_t)(n + 1), sizeof(mpz_t));
    for (long i = 0; i <= n; i++) { mpz_init(prev[i]); mpz_init(cur[i]); }
    mpz_set_ui(prev[0], 1);
    for (long i = 1; i <= n; i++) {
        mpz_set_ui(cur[0], 0);
        for (long j = 1; j <= i; j++) {
            mpz_t tmp; mpz_init(tmp);
            mpz_mul_si(tmp, prev[j], j);
            mpz_add(cur[j], tmp, prev[j-1]);
            mpz_clear(tmp);
        }
        for (long j = 0; j <= n; j++) mpz_swap(prev[j], cur[j]);
    }
    mpz_t sum; mpz_init_set_ui(sum, 0);
    for (long k = 0; k <= n; k++) mpz_add(sum, sum, prev[k]);
    val_t out = z_to_val(sum);
    for (long i = 0; i <= n; i++) { mpz_clear(prev[i]); mpz_clear(cur[i]); }
    free(prev); free(cur); mpz_clear(sum);
    return out;
}

/* Partition numbers via Euler's pentagonal recurrence. */
static val_t prim_partition_count(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_fixnum(av[0])) scm_raise(V_FALSE, "partition-count: not a fixnum");
    long n = (long)vunfix(av[0]);
    if (n < 0) return vfix(0);
    if (n == 0) return vfix(1);
    mpz_t *p = (mpz_t *)calloc((size_t)(n + 1), sizeof(mpz_t));
    for (long i = 0; i <= n; i++) mpz_init(p[i]);
    mpz_set_ui(p[0], 1);
    for (long m = 1; m <= n; m++) {
        /* p(m) = Σ_{k=1..} (-1)^(k+1) ( p(m - k(3k-1)/2) + p(m - k(3k+1)/2) ) */
        mpz_set_ui(p[m], 0);
        for (long k = 1; ; k++) {
            long p1 = k * (3*k - 1) / 2;
            long p2 = k * (3*k + 1) / 2;
            if (p1 > m && p2 > m) break;
            int sign = (k & 1) ? 1 : -1;
            if (p1 <= m) {
                if (sign > 0) mpz_add(p[m], p[m], p[m - p1]);
                else          mpz_sub(p[m], p[m], p[m - p1]);
            }
            if (p2 <= m) {
                if (sign > 0) mpz_add(p[m], p[m], p[m - p2]);
                else          mpz_sub(p[m], p[m], p[m - p2]);
            }
        }
    }
    val_t out = z_to_val(p[n]);
    for (long i = 0; i <= n; i++) mpz_clear(p[i]);
    free(p);
    return out;
}

/* ---------------------------------------------------------------------- */
/* Continued fractions                                                    */
/* ---------------------------------------------------------------------- */

/* For exact rational x, compute the (always terminating) CF expansion. */
static val_t cf_exact(val_t x) {
    val_t out = V_NIL;
    /* Repeatedly: a = floor(x); x = 1 / (x - a). */
    for (int i = 0; i < 1000; i++) {
        val_t fl = num_floor(x);
        out = scm_cons(fl, out);
        val_t frac = num_sub(x, fl);
        if (num_is_zero(frac)) break;
        x = num_div(vfix(1), frac);
    }
    return scm_reverse(out);
}

/* For inexact (flonum), limit to N terms or until 1/(x - floor(x)) overflows. */
static val_t cf_flonum(double x, int terms) {
    val_t out = V_NIL;
    for (int i = 0; i < terms; i++) {
        double fl = floor(x);
        out = scm_cons(num_make_bignum_i((long)fl), out);
        double frac = x - fl;
        if (frac < 1e-14) break;
        x = 1.0 / frac;
        if (!isfinite(x)) break;
    }
    return scm_reverse(out);
}

static val_t prim_continued_fraction(int ac, val_t *av, void *ud) {
    (void)ud;
    int terms = 50;
    if (ac >= 2) {
        if (!vis_fixnum(av[1])) scm_raise(V_FALSE, "continued-fraction: terms must be a fixnum");
        terms = (int)vunfix(av[1]);
    }
    val_t x = av[0];
    if (vis_fixnum(x) || vis_bignum(x) || vis_rational(x))
        return cf_exact(x);
    if (vis_flonum(x))
        return cf_flonum(vfloat(x), terms);
    scm_raise(V_FALSE, "continued-fraction: not a real number");
}

static val_t prim_convergents(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    /* p_-1 = 1, p_0 = a0;  q_-1 = 0, q_0 = 1.
     * p_n = a_n * p_{n-1} + p_{n-2};  q_n = a_n * q_{n-1} + q_{n-2}. */
    val_t lst = av[0];
    if (vis_nil(lst)) return V_NIL;
    val_t p_prev2 = vfix(1), p_prev1 = vcar(lst);
    val_t q_prev2 = vfix(0), q_prev1 = vfix(1);
    val_t out = scm_cons(num_div(p_prev1, q_prev1), V_NIL);
    for (val_t p = vcdr(lst); vis_pair(p); p = vcdr(p)) {
        val_t a = vcar(p);
        val_t pn = num_add(num_mul(a, p_prev1), p_prev2);
        val_t qn = num_add(num_mul(a, q_prev1), q_prev2);
        out = scm_cons(num_div(pn, qn), out);
        p_prev2 = p_prev1; p_prev1 = pn;
        q_prev2 = q_prev1; q_prev1 = qn;
    }
    return scm_reverse(out);
}

static val_t prim_best_rational_approx(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    /* Walk the continued-fraction expansion of x; keep the latest convergent
     * whose denominator is ≤ max_denom.  Then consider semi-convergents on the
     * final partial quotient. */
    val_t x = av[0], max_den = av[1];
    val_t cf;
    if (vis_flonum(x))      cf = cf_flonum(vfloat(x), 100);
    else                    cf = cf_exact(x);

    /* Standard three-term recurrence: h_{-2}=0, h_{-1}=1, k_{-2}=1, k_{-1}=0.
     * h_n = a_n * h_{n-1} + h_{n-2};  k_n = a_n * k_{n-1} + k_{n-2}.
     * Convergent_n = h_n / k_n. */
    val_t p_prev2 = vfix(0), p_prev1 = vfix(1);
    val_t q_prev2 = vfix(1), q_prev1 = vfix(0);
    val_t best = vfix(0);
    for (val_t p = cf; vis_pair(p); p = vcdr(p)) {
        val_t a = vcar(p);
        val_t pn = num_add(num_mul(a, p_prev1), p_prev2);
        val_t qn = num_add(num_mul(a, q_prev1), q_prev2);
        if (num_gt(qn, max_den)) {
            /* Try semi-convergents: m = floor((max_den - q_prev2) / q_prev1).
             * Valid only when q_prev1 > 0 (i.e., we're past the first step). */
            if (num_gt(q_prev1, vfix(0))) {
                val_t m = num_quotient(num_sub(max_den, q_prev2), q_prev1);
                if (num_gt(m, vfix(0))) {
                    val_t pm = num_add(num_mul(m, p_prev1), p_prev2);
                    val_t qm = num_add(num_mul(m, q_prev1), q_prev2);
                    best = num_div(pm, qm);
                }
            }
            break;
        }
        best = num_div(pn, qn);
        p_prev2 = p_prev1; p_prev1 = pn;
        q_prev2 = q_prev1; q_prev1 = qn;
    }
    return best;
}

/* ---------------------------------------------------------------------- */
/* Number predicates                                                      */
/* ---------------------------------------------------------------------- */

static val_t prim_squarefree_p(int ac, val_t *av, void *ud) {
    val_t mu = prim_mobius(ac, av, ud);
    return vbool(!num_is_zero(mu));
}

/* Returns #f or (values base exp) if n = base^exp with exp ≥ 2. */
static val_t prim_perfect_power_p(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    mpz_t n; mpz_init(n); load_z(n, av[0], "perfect-power?");
    if (mpz_cmp_ui(n, 1) <= 0) { mpz_clear(n); return V_FALSE; }
    if (!mpz_perfect_power_p(n)) { mpz_clear(n); return V_FALSE; }
    /* Find the smallest base. */
    mpz_t root, pow; mpz_inits(root, pow, NULL);
    for (unsigned long e = (unsigned long)mpz_sizeinbase(n, 2); e >= 2; e--) {
        if (mpz_root(root, n, e) != 0) {
            /* Verify: root^e == n. */
            mpz_pow_ui(pow, root, e);
            if (mpz_cmp(pow, n) == 0) {
                val_t base = z_to_val(root);
                val_t exp  = num_make_bignum_i((long)e);
                mpz_clears(n, root, pow, NULL);
                Values *vv = (Values *)gc_alloc(sizeof(Values) + 2*sizeof(val_t));
                vv->hdr.type = T_VALUES; vv->hdr.flags = 0; vv->count = 2;
                vv->vals[0] = base; vv->vals[1] = exp;
                return vptr(vv);
            }
        }
    }
    mpz_clears(n, root, pow, NULL);
    return V_FALSE;
}

static val_t prim_smooth_p(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t factors = prim_factor(1, av, ud);  /* uses av[0] as n */
    val_t bound = av[1];
    for (val_t p = factors; vis_pair(p); p = vcdr(p))
        if (num_gt(vcar(p), bound)) return V_FALSE;
    return V_TRUE;
}

/* ---------------------------------------------------------------------- */
/* Registration                                                           */
/* ---------------------------------------------------------------------- */

void builtins_numtheory_register(val_t env) {
    /* Primality */
    DEF("prime?",        prim_prime_p,        1, 1);
    DEF("next-prime",    prim_next_prime,     1, 1);
    DEF("prev-prime",    prim_prev_prime,     1, 1);

    /* Factoring */
    DEF("factor",        prim_factor,         1, 1);
    DEF("prime-factors", prim_prime_factors,  1, 1);

    /* Arithmetic functions */
    DEF("totient",       prim_totient,        1, 1);
    DEF("carmichael",    prim_carmichael,     1, 1);
    DEF("mobius",        prim_mobius,         1, 1);
    DEF("divisors",      prim_divisors,       1, 1);
    DEF("divisor-count", prim_divisor_count,  1, 1);
    DEF("num-divisors",  prim_divisor_count,  1, 1);
    DEF("divisor-sum",   prim_divisor_sum,    1, 1);
    DEF("sum-divisors",  prim_divisor_sum,    1, 1);
    DEF("perfect?",      prim_perfect_p,      1, 1);
    DEF("abundant?",     prim_abundant_p,     1, 1);
    DEF("deficient?",    prim_deficient_p,    1, 1);
    DEF("omega",         prim_omega,          1, 1);
    DEF("big-omega",     prim_big_omega,      1, 1);

    /* Modular arithmetic */
    DEF("mod-expt",          prim_mod_expt,    3, 3);
    DEF("mod-inverse",       prim_mod_inverse, 2, 2);
    DEF("jacobi-symbol",     prim_jacobi,      2, 2);
    DEF("kronecker-symbol",  prim_kronecker,   2, 2);
    DEF("legendre-symbol",   prim_legendre,    2, 2);
    DEF("extended-gcd",      prim_extended_gcd, 2, 2);
    DEF("chinese-remainder", prim_crt,         2, 2);

    /* Sequences */
    DEF("fibonacci",         prim_fibonacci,   1, 1);
    DEF("lucas",             prim_lucas,       1, 1);
    DEF("binomial",          prim_binomial,    2, 2);
    DEF("multinomial",       prim_multinomial, 2, 2);
    DEF("catalan",           prim_catalan,     1, 1);
    DEF("bernoulli",         prim_bernoulli,   1, 1);
    DEF("euler-number",      prim_euler_number, 1, 1);
    DEF("stirling1",         prim_stirling1,   2, 2);
    DEF("stirling2",         prim_stirling2,   2, 2);
    DEF("bell",              prim_bell,        1, 1);
    DEF("partition-count",   prim_partition_count, 1, 1);

    /* Continued fractions */
    DEF("continued-fraction",   prim_continued_fraction, 1, 2);
    DEF("convergents",          prim_convergents,        1, 1);
    DEF("best-rational-approx", prim_best_rational_approx, 2, 2);

    /* Number predicates */
    DEF("squarefree?",          prim_squarefree_p,       1, 1);
    DEF("perfect-power?",       prim_perfect_power_p,    1, 1);
    DEF("smooth?",              prim_smooth_p,           2, 2);
}
