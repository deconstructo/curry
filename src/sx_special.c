/*
 * sx_special.c — Special functions and extended series (Phase 4f/4g)
 */

#include "sx_special.h"
#include "symbolic.h"
#include "symbol.h"
#include "object.h"
#include "numeric.h"
#include "gc.h"
#include "eval.h"
#include "builtins.h"
#include <math.h>
#include <string.h>
#include <gmp.h>

extern void scm_raise(val_t kind, const char *fmt, ...) __attribute__((noreturn));

static val_t Q(long n)          { return vfix(n); }
static val_t Qr(long n, long d) { return num_make_rational(vfix(n), vfix(d)); }

/* Make a symbolic 0-arg operator (used to name unevaluated special forms) */
static val_t make_sym_expr(const char *name, int nargs, val_t *args) {
    return sx_make_expr(sym_intern_cstr(name), nargs, args);
}

/* ========================================================================== */
/* PART 1: Orthogonal polynomials via three-term recurrence                   */
/* ========================================================================== */

val_t sx_legendre(val_t n_v, val_t x) {
    if (!vis_fixnum(n_v) || vunfix(n_v) < 0)
        scm_raise(V_FALSE, "legendre: order must be a non-negative integer");
    int N = (int)vunfix(n_v);

    if (vis_number(x)) {
        double xd = num_to_double(x);
        if (N == 0) return num_make_float(1.0);
        double p0 = 1.0, p1 = xd;
        for (int k = 2; k <= N; k++) {
            double pk = ((2*k-1)*xd*p1 - (k-1)*p0) / k;
            p0 = p1; p1 = pk;
        }
        return num_make_float(p1);
    }
    /* Symbolic recurrence: (2n-1)*x*P_{n-1}/n - (n-1)*P_{n-2}/n */
    if (N == 0) return Q(1);
    if (N == 1) return x;
    val_t p0 = Q(1), p1 = x;
    for (int n = 2; n <= N; n++) {
        val_t cur = sx_simplify(sx_sub(
            sx_div(sx_mul(Q(2*n-1), sx_mul(x, p1)), Q(n)),
            sx_div(sx_mul(Q(n-1), p0), Q(n))));
        p0 = p1; p1 = cur;
    }
    return p1;
}

val_t sx_assoc_legendre(val_t l_v, val_t m_v, val_t x) {
    if (!vis_fixnum(l_v) || !vis_fixnum(m_v))
        scm_raise(V_FALSE, "assoc-legendre: l and m must be integers");
    int l = (int)vunfix(l_v), m = (int)vunfix(m_v);
    int abm = m < 0 ? -m : m;
    if (abm > l) return Q(0);
    /* P_l^m = (-1)^m (1-x²)^(m/2) * d^m/dx^m P_l(x) */
    val_t pl = sx_legendre(l_v, x);
    val_t dml = pl;
    for (int k = 0; k < abm; k++)
        dml = sx_simplify(sx_diff(dml, x));
    val_t sign        = (abm % 2 == 0) ? Q(1) : Q(-1);
    val_t sqrt_factor = sx_expt(sx_sub(Q(1), sx_expt(x, Q(2))), Qr(abm, 2));
    return sx_simplify(sx_mul(sign, sx_mul(sqrt_factor, dml)));
}

val_t sx_hermite(val_t n_v, val_t x) {
    if (!vis_fixnum(n_v) || vunfix(n_v) < 0)
        scm_raise(V_FALSE, "hermite: order must be a non-negative integer");
    int N = (int)vunfix(n_v);
    if (vis_number(x)) {
        if (N == 0) return num_make_float(1.0);
        double xd = num_to_double(x), h0 = 1.0, h1 = 2.0*xd;
        for (int k = 2; k <= N; k++) {
            double hk = 2.0*xd*h1 - 2.0*(k-1)*h0;
            h0 = h1; h1 = hk;
        }
        return num_make_float(h1);
    }
    if (N == 0) return Q(1);
    val_t h0 = Q(1), h1 = sx_mul(Q(2), x);
    if (N == 1) return h1;
    for (int n = 2; n <= N; n++) {
        val_t cur = sx_simplify(sx_sub(sx_mul(sx_mul(Q(2), x), h1),
                                       sx_mul(Q(2*(n-1)), h0)));
        h0 = h1; h1 = cur;
    }
    return h1;
}

val_t sx_hermite_prob(val_t n_v, val_t x) {
    if (!vis_fixnum(n_v) || vunfix(n_v) < 0)
        scm_raise(V_FALSE, "hermite-prob: order must be a non-negative integer");
    int N = (int)vunfix(n_v);
    if (N == 0) return Q(1);
    val_t h0 = Q(1), h1 = x;
    if (N == 1) return h1;
    for (int n = 2; n <= N; n++) {
        val_t cur = sx_simplify(sx_sub(sx_mul(x, h1), sx_mul(Q(n-1), h0)));
        h0 = h1; h1 = cur;
    }
    return h1;
}

val_t sx_chebyshev_t(val_t n_v, val_t x) {
    if (!vis_fixnum(n_v) || vunfix(n_v) < 0)
        scm_raise(V_FALSE, "chebyshev-t: order must be a non-negative integer");
    int N = (int)vunfix(n_v);
    if (N == 0) return Q(1);
    if (vis_number(x)) {
        double xd = num_to_double(x), t0 = 1.0, t1 = xd;
        for (int k = 2; k <= N; k++) { double tk = 2*xd*t1-t0; t0=t1; t1=tk; }
        return num_make_float(t1);
    }
    val_t t0 = Q(1), t1 = x;
    for (int n = 2; n <= N; n++) {
        val_t cur = sx_simplify(sx_sub(sx_mul(sx_mul(Q(2), x), t1), t0));
        t0 = t1; t1 = cur;
    }
    return t1;
}

val_t sx_chebyshev_u(val_t n_v, val_t x) {
    if (!vis_fixnum(n_v) || vunfix(n_v) < 0)
        scm_raise(V_FALSE, "chebyshev-u: order must be a non-negative integer");
    int N = (int)vunfix(n_v);
    if (N == 0) return Q(1);
    val_t u0 = Q(1), u1 = sx_mul(Q(2), x);
    if (N == 1) return u1;
    for (int n = 2; n <= N; n++) {
        val_t cur = sx_simplify(sx_sub(sx_mul(sx_mul(Q(2), x), u1), u0));
        u0 = u1; u1 = cur;
    }
    return u1;
}

val_t sx_laguerre(val_t n_v, val_t x) {
    if (!vis_fixnum(n_v) || vunfix(n_v) < 0)
        scm_raise(V_FALSE, "laguerre: order must be a non-negative integer");
    int N = (int)vunfix(n_v);
    if (N == 0) return Q(1);
    val_t l0 = Q(1), l1 = sx_simplify(sx_sub(Q(1), x));
    if (N == 1) return l1;
    for (int n = 2; n <= N; n++) {
        val_t cur = sx_simplify(sx_div(
            sx_sub(sx_mul(sx_sub(Q(2*n-1), x), l1), sx_mul(Q(n-1), l0)),
            Q(n)));
        l0 = l1; l1 = cur;
    }
    return l1;
}

val_t sx_assoc_laguerre(val_t n_v, val_t k_v, val_t x) {
    if (!vis_fixnum(n_v) || !vis_fixnum(k_v) || vunfix(n_v) < 0 || vunfix(k_v) < 0)
        scm_raise(V_FALSE, "assoc-laguerre: n and k must be non-negative integers");
    int N = (int)vunfix(n_v), K = (int)vunfix(k_v);
    if (N == 0) return Q(1);
    val_t l0 = Q(1), l1 = sx_simplify(sx_sub(Q(1+K), x));
    if (N == 1) return l1;
    for (int n = 2; n <= N; n++) {
        val_t cur = sx_simplify(sx_div(
            sx_sub(sx_mul(sx_sub(Q(2*n+K-1), x), l1), sx_mul(Q(n+K-1), l0)),
            Q(n)));
        l0 = l1; l1 = cur;
    }
    return l1;
}

/* ========================================================================== */
/* PART 2: Spherical harmonics                                                 */
/* ========================================================================== */

val_t sx_spherical_harmonic(val_t l_v, val_t m_v, val_t theta, val_t phi) {
    if (!vis_fixnum(l_v) || !vis_fixnum(m_v))
        scm_raise(V_FALSE, "spherical-harmonic: l and m must be integers");
    int l = (int)vunfix(l_v), m = (int)vunfix(m_v);
    if (l < 0 || m < -l || m > l)
        scm_raise(V_FALSE, "spherical-harmonic: require l>=0, -l<=m<=l");

    int abm = m < 0 ? -m : m;
    /* Compute (l - |m|)! / (l + |m|)! as exact rational */
    val_t fac_ratio = Q(1);
    for (int k = l - abm + 1; k <= l + abm; k++)
        fac_ratio = num_div(fac_ratio, Q(k));

    /* N² = (2l+1)/(4π) * (l-|m|)!/(l+|m|)! */
    val_t pi_sym = sx_make_var(sym_intern_cstr("π"));
    val_t norm2  = sx_simplify(sx_mul(
        sx_div(Q(2*l+1), sx_mul(Q(4), pi_sym)), fac_ratio));
    val_t N_lm   = sx_sqrt(norm2);

    /* P_l^|m|(cos θ) */
    val_t cos_theta = sx_cos(theta);
    val_t Plm = sx_assoc_legendre(l_v, vfix(abm), cos_theta);

    /* Phase factor e^(i*m*φ) */
    val_t i_sym = sx_make_var(sym_intern_cstr("i"));
    val_t phase = sx_exp(sx_mul(sx_mul(i_sym, Q(m)), phi));

    return sx_simplify(sx_mul(N_lm, sx_mul(Plm, phase)));
}

/* ========================================================================== */
/* PART 3: Gamma and related                                                   */
/* ========================================================================== */

static val_t exact_factorial(long n) {
    val_t r = Q(1);
    for (long k = 2; k <= n; k++) r = num_mul(r, Q(k));
    return r;
}

static double lanczos_gamma(double z) {
    static const double g   = 7.0;
    static const double c[] = {
        0.99999999999980993, 676.5203681218851,  -1259.1392167224028,
        771.32342877765313,  -176.61502916214059, 12.507343278686905,
        -0.13857109526572012, 9.9843695780195716e-6, 1.5056327351493116e-7
    };
    if (z < 0.5) return M_PI / (sin(M_PI * z) * lanczos_gamma(1.0 - z));
    z -= 1.0;
    double x = c[0];
    for (int i = 1; i < 9; i++) x += c[i] / (z + i);
    double t = z + g + 0.5;
    return sqrt(2.0 * M_PI) * pow(t, z + 0.5) * exp(-t) * x;
}

val_t sx_gamma(val_t z) {
    /* Positive integer: exact */
    if (vis_fixnum(z) && vunfix(z) > 0)
        return exact_factorial(vunfix(z) - 1);
    if (vis_fixnum(z) && vunfix(z) <= 0)
        scm_raise(V_FALSE, "gamma: pole at non-positive integer");

    /* Half-integer n/2 where n is odd positive: Γ(n/2) = (n-2)!!/(2^((n-1)/2)) * √π */
    if (vis_rational(z)) {
        mpq_t *q = &as_rat(z)->q;
        long num_v = (long)mpz_get_si(mpq_numref(*q));
        long den_v = (long)mpz_get_si(mpq_denref(*q));
        if (den_v == 2 && num_v > 0 && num_v % 2 == 1) {
            /* Γ((2k+1)/2) = (2k-1)!!/(2^k) * √π, where k = (num_v-1)/2 */
            long k = (num_v - 1) / 2;
            val_t doublefac = Q(1);
            for (long j = 2*k - 1; j >= 1; j -= 2) doublefac = num_mul(doublefac, Q(j));
            val_t coeff    = num_div(doublefac, num_expt(Q(2), Q(k)));
            val_t sqrt_pi  = sx_sqrt(sx_make_var(sym_intern_cstr("π")));
            return sx_simplify(sx_mul(coeff, sqrt_pi));
        }
    }

    /* Numerical float */
    if (vis_flonum(z)) return num_make_float(lanczos_gamma(num_to_double(z)));

    /* Symbolic */
    val_t args[1] = {z};
    return make_sym_expr("Γ", 1, args);
}

val_t sx_log_gamma(val_t z) {
    if (vis_flonum(z)) return num_make_float(lgamma(num_to_double(z)));
    if (vis_fixnum(z) && vunfix(z) > 0) return sx_log(sx_gamma(z));
    val_t args[1] = {z};
    return make_sym_expr("log-Γ", 1, args);
}

val_t sx_digamma(val_t z) {
    if (vis_flonum(z)) {
        double x = num_to_double(z), r = 0.0;
        while (x < 8.0) { r -= 1.0/x; x += 1.0; }
        double s = log(x) - 0.5/x, x2 = x*x;
        s -= 1.0/(12.0*x2) - 1.0/(120.0*x2*x2);
        return num_make_float(r + s);
    }
    val_t args[1] = {z};
    return make_sym_expr("ψ", 1, args);
}

val_t sx_beta(val_t a, val_t b) {
    if (vis_flonum(a) || vis_flonum(b)) {
        double da = num_to_double(a), db = num_to_double(b);
        return num_make_float(exp(lgamma(da) + lgamma(db) - lgamma(da+db)));
    }
    val_t ga = sx_gamma(a), gb = sx_gamma(b), gab = sx_gamma(num_add(a,b));
    /* If all exact, compute */
    if (!vis_symexpr(ga) && !vis_symexpr(gb) && !vis_symexpr(gab))
        return sx_simplify(sx_div(sx_mul(ga, gb), gab));
    val_t args[2] = {a, b};
    return make_sym_expr("B", 2, args);
}

/* ========================================================================== */
/* PART 4: Bessel functions                                                    */
/* ========================================================================== */

val_t sx_bessel_j(val_t n_v, val_t x) {
    if (vis_fixnum(n_v) && vis_number(x))
        return num_make_float(jn((int)vunfix(n_v), num_to_double(x)));
    val_t args[2] = {n_v, x};
    return make_sym_expr("J", 2, args);
}

val_t sx_bessel_y(val_t n_v, val_t x) {
    if (vis_fixnum(n_v) && vis_number(x))
        return num_make_float(yn((int)vunfix(n_v), num_to_double(x)));
    val_t args[2] = {n_v, x};
    return make_sym_expr("Y", 2, args);
}

val_t sx_bessel_i(val_t n_v, val_t x) {
    if (vis_fixnum(n_v) && vis_number(x)) {
        int n = (int)vunfix(n_v); if (n < 0) n = -n;
        double sum = 0.0, term = 1.0, half_x = num_to_double(x) / 2.0;
        for (int k = 1; k <= n; k++) term *= half_x / k;
        for (int k = 0; k <= 60; k++) {
            sum += term;
            term *= half_x * half_x / ((k+1.0) * (k+1.0+n));
            if (fabs(term) < 1e-15 * fabs(sum) + 1e-300) break;
        }
        return num_make_float(sum);
    }
    val_t args[2] = {n_v, x};
    return make_sym_expr("I", 2, args);
}

/* K_0(x): Maclaurin series for x≤8, asymptotic for x>8 */
static double bessel_k0(double x) {
    static const double EULER = 0.5772156649015328606;
    if (x > 8.0) {
        double t = 1.0 / (8.0 * x);
        double p = 1.0 + t*(-1.0 + t*(4.5 + t*(-37.5)));
        return sqrt(M_PI / (2.0 * x)) * exp(-x) * p;
    }
    /* K_0(x) = Σ_{k≥0} H_k (x/2)^{2k}/(k!)² − (γ+ln(x/2)) I_0(x), H_0=0 */
    double y = x * 0.5, y2 = y * y, term = 1.0, i0 = 1.0, s = 0.0, hk = 0.0;
    for (int k = 1; k <= 50; k++) {
        term *= y2 / ((double)k * k);
        hk += 1.0 / k;
        s += hk * term;
        i0 += term;
        if (fabs(term) < 1e-15 * (1.0 + i0)) break;
    }
    return s - (EULER + log(y)) * i0;
}

/* K_1(x): Maclaurin for x≤8, asymptotic for x>8 */
static double bessel_k1(double x) {
    static const double EULER = 0.5772156649015328606;
    if (x > 8.0) {
        double t = 1.0 / (8.0 * x);
        double p = 1.0 + t*(3.0 + t*(-7.5 + t*(52.5)));
        return sqrt(M_PI / (2.0 * x)) * exp(-x) * p;
    }
    /* K_1(x) = I_0(x)/x + (γ+ln(x/2)) I_1(x) − Σ_{j≥0} H_{j+1} (x/2)^{2j+1}/(j!(j+1)!) */
    double y = x * 0.5, y2 = y * y;
    double te = 1.0, i0 = 1.0, to = y, i1 = y, s = y, hk = 1.0;
    for (int j = 1; j <= 50; j++) {
        te *= y2 / ((double)j * j);          i0 += te;
        to *= y2 / ((double)j * (j + 1));    i1 += to;
        hk += 1.0 / (j + 1);
        s  += hk * to;
        if (fabs(to) < 1e-15 * (1.0 + i1)) break;
    }
    return i0 / x + (EULER + log(y)) * i1 - s;
}

static double bessel_k_double(int n, double x) {
    if (x <= 0.0) return 1.0 / 0.0;
    if (n < 0) n = -n;
    if (n == 0) return bessel_k0(x);
    double km2 = bessel_k0(x), km1 = bessel_k1(x);
    if (n == 1) return km1;
    for (int ord = 1; ord < n; ord++) {
        double kk = 2.0 * ord / x * km1 + km2;
        km2 = km1; km1 = kk;
    }
    return km1;
}

val_t sx_bessel_k(val_t n_v, val_t x) {
    if (vis_fixnum(n_v) && vis_number(x))
        return num_make_float(bessel_k_double((int)vunfix(n_v), num_to_double(x)));
    val_t args[2] = {n_v, x};
    return make_sym_expr("K", 2, args);
}

/* ========================================================================== */
/* PART 5: Elliptic integrals (AGM)                                           */
/* ========================================================================== */

static double elliptic_k_agm(double k) {
    if (fabs(k) >= 1.0) return 1.0 / 0.0;
    double a = 1.0, b = sqrt(1.0 - k*k);
    for (int i = 0; i < 50; i++) {
        double a1 = (a+b)/2.0, b1 = sqrt(a*b);
        a = a1; b = b1;
        if (fabs(a-b) < 1e-15) break;
    }
    return M_PI / (2.0 * a);
}

static double elliptic_e_agm(double k) {
    if (k == 0.0) return M_PI / 2.0;
    double a = 1.0, b = sqrt(1.0 - k*k), pow2 = 2.0, sum = 1.0;
    for (int i = 0; i < 50; i++) {
        double c1 = (a - b) / 2.0;
        sum -= pow2 * c1 * c1;
        pow2 *= 2.0;
        double a1 = (a+b)/2.0, b1 = sqrt(a*b);
        a = a1; b = b1;
        if (fabs(a-b) < 1e-15) break;
    }
    return M_PI / 2.0 * sum;
}

val_t sx_elliptic_k(val_t k) {
    if (vis_number(k)) return num_make_float(elliptic_k_agm(num_to_double(k)));
    val_t args[1] = {k}; return make_sym_expr("K", 1, args);
}
val_t sx_elliptic_e(val_t k) {
    if (vis_number(k)) return num_make_float(elliptic_e_agm(num_to_double(k)));
    val_t args[1] = {k}; return make_sym_expr("E", 1, args);
}
val_t sx_elliptic_f(val_t phi, val_t k) {
    if (vis_number(phi) && vis_number(k)) {
        double ph = num_to_double(phi), kd = num_to_double(k);
        double a = 1.0, b = sqrt(1.0 - kd*kd);
        for (int i = 0; i < 30; i++) {
            double a1=(a+b)/2.0, b1=sqrt(a*b);
            ph = (ph + atan2(b*tan(ph), a))/2.0 + M_PI*floor(ph/M_PI + 0.5);
            a=a1; b=b1;
            if (fabs(a-b) < 1e-14) break;
        }
        return num_make_float(ph / a);
    }
    val_t args[2] = {phi, k}; return make_sym_expr("F", 2, args);
}
val_t sx_elliptic_pi(val_t n, val_t k) {
    if (vis_number(n) && vis_number(k)) {
        static const double gl_t[] = {
            0.0950125098,0.2816035508,0.4580167777,0.6178762444,
            0.7554044084,0.8656312024,0.9445750231,0.9894009350};
        static const double gl_w[] = {
            0.1894506105,0.1826034150,0.1691565194,0.1495959889,
            0.1246289863,0.0951585117,0.0622535239,0.0271524594};
        double nd=num_to_double(n), kd=num_to_double(k), k2=kd*kd, sum=0.0;
        for (int i=0; i<8; i++) {
            double s=sin(M_PI/2.0*gl_t[i]), s2=s*s;
            sum += gl_w[i]*2.0/((1.0-nd*s2)*sqrt(1.0-k2*s2));
        }
        return num_make_float(sum * M_PI / 4.0);
    }
    val_t args[2] = {n, k}; return make_sym_expr("Π", 2, args);
}

/* ========================================================================== */
/* PART 6: Laurent and Puiseux series (Phase 4g)                              */
/* ========================================================================== */

/* Helper: eval expr at point, returning false (setting *out = V_VOID) on exception,
   or if the result is infinite/NaN.  Returns true and sets *out on success. */
static bool try_subst(val_t expr, val_t var, val_t point, val_t *out) {
    ExnHandler _h;
    bool ok = false;
    SCM_PROTECT(_h, {
        val_t v = sx_simplify(sx_substitute(expr, var, point));
        if (vis_number(v) &&
            !(vis_flonum(v) && (isinf(num_to_double(v)) || isnan(num_to_double(v))))) {
            *out = v; ok = true;
        }
    }, { /* exception — ok stays false */ });
    return ok;
}

val_t sx_laurent(val_t expr, val_t var, val_t point, int n) {
    if (!vis_symvar(var))
        scm_raise(V_FALSE, "laurent: second argument must be a symbolic variable");

    /* Find pole order k_min by trying direct substitution at the expansion point.
       If that succeeds (finite number, possibly zero), the function is analytic → k_min=0.
       If it raises an exception or gives ∞, search for the smallest k≥1 such that
       f(x)*(x-a)^k is finite and non-zero at a. */
    int k_min = 0;
    val_t xma = sx_sub(var, point);
    {
        val_t at_pt;
        if (!try_subst(expr, var, point, &at_pt)) {
            /* Direct substitution fails (pole or essential singularity) */
            for (int k = 1; k <= 8; k++) {
                val_t probe = sx_simplify(sx_mul(expr, sx_expt(xma, Q(k))));
                val_t u;
                if (try_subst(probe, var, point, &u) && !num_is_zero(u)) {
                    k_min = k; break;
                }
            }
        }
        /* else: function is analytic at point → k_min stays 0 */
    }

    /* g(x) = f(x) * (x-a)^k_min — Taylor-expandable */
    val_t g = k_min > 0
        ? sx_simplify(sx_mul(expr, sx_expt(xma, Q(k_min))))
        : expr;

    /* For the regular case (k_min=0), use sx_series which handles most cases.
       For the pole case (k_min>0), g should now simplify cleanly (e.g. x*(1/x)=1)
       and sx_series will work via sx_substitute.  Fall back to sx_limit per term
       only when the coefficient evaluation fails. */
    int taylor_n = n + k_min;
    if (taylor_n < 0) taylor_n = 0;

    /* Try to use sx_series directly first */
    val_t series_result = sx_series(g, var, point, taylor_n);

    /* Collect terms from series_result and adjust powers by -k_min */
    val_t *raw_terms;
    int    nraw;
    val_t  single_arr[1];
    if (vis_symexpr(series_result) && as_symexpr(series_result)->op == SX_ADD) {
        nraw      = (int)as_symexpr(series_result)->nargs;
        raw_terms = as_symexpr(series_result)->args;
    } else {
        single_arr[0] = series_result; nraw = 1; raw_terms = single_arr;
    }

    val_t *terms = (val_t *)gc_alloc_raw_pinned(
        (size_t)(nraw) * sizeof(val_t));
    int nterms = 0;

    if (k_min == 0) {
        /* No adjustment needed */
        for (int i = 0; i < nraw; i++) terms[nterms++] = raw_terms[i];
    } else {
        /* Divide each term by (x-a)^k_min to shift powers */
        for (int i = 0; i < nraw; i++) {
            val_t t = sx_simplify(sx_div(raw_terms[i], sx_expt(xma, Q(k_min))));
            if (!num_is_zero(t)) terms[nterms++] = t;
        }
    }

    if (nterms == 0) return Q(0);
    if (nterms == 1) return terms[0];
    return sx_simplify(sx_make_expr(SX_ADD, nterms, terms));
}

val_t sx_puiseux(val_t expr, val_t var, val_t point, int n, int denom) {
    if (!vis_symvar(var))
        scm_raise(V_FALSE, "puiseux: second argument must be a symbolic variable");
    if (denom <= 0) denom = 1;

    /* t-variable for substitution x = point + t^denom */
    val_t t   = sx_make_var(sym_intern_cstr("__puiseux_t__"));
    val_t sub = sx_add(point, sx_expt(t, Q(denom)));
    val_t g   = sx_simplify(sx_substitute(expr, var, sub));

    int taylor_n = n * denom;
    val_t *terms = (val_t *)gc_alloc_raw_pinned(
        (size_t)(taylor_n + 1) * sizeof(val_t));
    int nterms = 0;
    val_t fk = g, fact = Q(1);

    for (int k = 0; k <= taylor_n; k++) {
        if (k > 0) { fk = sx_simplify(sx_diff(fk, t)); fact = num_mul(fact, Q(k)); }
        val_t ck = sx_simplify(sx_substitute(fk, t, Q(0)));
        if (!vis_number(ck) || num_is_zero(ck)) continue;
        val_t coeff = num_div(ck, fact);
        if (num_is_zero(coeff)) continue;
        val_t xma = sx_sub(var, point);
        val_t pw  = k == 0 ? Q(1) : sx_expt(xma, Qr(k, denom));
        terms[nterms++] = k == 0 ? coeff : sx_simplify(sx_mul(coeff, pw));
    }

    if (nterms == 0) return Q(0);
    if (nterms == 1) return terms[0];
    return sx_simplify(sx_make_expr(SX_ADD, nterms, terms));
}
