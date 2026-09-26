/* Tiny shared library, built and loaded ONLY by tests/ffi_tests.scm's
 * struct-by-value-argument coverage. Real, independently-compiled C ABI
 * to marshal against -- standard libc has essentially no by-value
 * struct-ARGUMENT functions (div_t/ldiv_t cover struct-by-value RETURN
 * well, but everything that takes a struct takes it by pointer), so
 * there is no equivalent real system-library function to test against
 * for the argument direction. This shim exists purely to give
 * (curry ffi)'s struct-by-value marshaling a genuine external C compiler
 * to agree (or disagree) with -- curry's own code calling curry's own
 * code would prove nothing about real struct-passing ABI correctness.
 */

#include <stdint.h>

typedef struct { double x; double y; } Point;
typedef struct { int32_t a; double b; int32_t c; } Mixed;

/* struct passed BY VALUE as an argument -- exercises real by-value
 * struct-argument ABI (register-packing on most platforms for a
 * two-double struct like this). */
double point_dot(Point p, Point q) {
    return p.x * q.x + p.y * q.y;
}

/* struct-by-value argument AND struct-by-value return together, with a
 * mixed int/double/int layout to exercise real struct padding/alignment
 * (a plain {int32,double,int32} struct has real padding on most ABIs:
 * bytes before the double to align it to 8, and trailing padding after
 * the second int32 to round the whole struct up to a multiple of the
 * double's own alignment). */
Mixed mixed_double_a_and_c(Mixed m) {
    Mixed out;
    out.a = m.a * 2;
    out.b = m.b;
    out.c = m.c * 2;
    return out;
}
