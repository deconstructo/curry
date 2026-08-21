#include "gc.h"
#include "symbol.h"
#include "numeric.h"
#include "port.h"
#include "env.h"
#include "eval.h"
#include "reader.h"
#include "actors.h"
#include "modules.h"
#include "set.h"
#include "object.h"
#include "compiler.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

static int pass = 0, fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("PASS: %s\n", msg); pass++; } \
    else { printf("FAIL: %s\n", msg); fail++; } \
    fflush(stdout); \
} while (0)

static void init(void) {
    fprintf(stderr, "gc_init\n"); gc_init();
    fprintf(stderr, "sym_init\n"); sym_init();
    fprintf(stderr, "num_init\n"); num_init();
    fprintf(stderr, "port_init\n"); port_init();
    fprintf(stderr, "env_init\n"); env_init();
    fprintf(stderr, "eval_init\n"); eval_init();
    fprintf(stderr, "actors_init\n"); actors_init();
    fprintf(stderr, "modules_init\n"); modules_init();
    fprintf(stderr, "init done\n");
}

static val_t run(const char *src) {
    /* Evaluate all forms in src; return last value */
    val_t port = port_open_input_string(src, (uint32_t)strlen(src));
    val_t result = V_VOID;
    val_t datum;
    while (!vis_eof(datum = scm_read(port)))
        result = eval(datum, GLOBAL_ENV);
    return result;
}

static void test_fixnums(void) {
    CHECK(vis_fixnum(vfix(0)),   "vfix(0) is fixnum");
    CHECK(vunfix(vfix(42)) == 42,"vfix/vunfix round-trip");
    CHECK(vis_true(run("(= (+ 1 2) 3)")),         "1+2=3");
    CHECK(vis_true(run("(= (* 6 7) 42)")),         "6*7=42");
    CHECK(vis_true(run("(exact? 5)")),             "5 is exact");
    CHECK(vis_false(run("(inexact? 5)")),           "5 is not inexact");
}

static void test_bignums(void) {
    /* 2^100 */
    val_t r = run("(expt 2 100)");
    CHECK(vis_bignum(r) || vis_fixnum(r),  "2^100 is integer");
    CHECK(vis_true(run("(= (+ (expt 2 64) 1) (+ (expt 2 64) 1))")),
          "bignum equality");
}

static void test_rationals(void) {
    CHECK(vis_true(run("(= 1/3 (/ 1 3))")),    "rational 1/3");
    CHECK(vis_true(run("(= 1/2 (+ 1/4 1/4))")), "rational arithmetic");
}

static void test_floats(void) {
    CHECK(vis_true(run("(inexact? 1.5)")),      "1.5 is inexact");
    CHECK(vis_true(run("(< (abs (- (sin 0.0) 0.0)) 1e-15)")), "sin(0)=0");
}

/* num_flonum_to_shortest_cstr — regression coverage for a bug found in a
 * full-codebase audit: num_to_string's flonum branch (and scm_write's own
 * direct flonum case in port.c, and the matrix/tensor/multivector/f64vector
 * element printers) all used a bare "%g" (6 significant digits), losing
 * precision on anything needing more -- (display 3.14159265358979) printed
 * "3.14159". Fixed with a shared helper that finds the shortest decimal
 * string that round-trips back to the exact same double via strtod, with a
 * safety check before trying to avoid needless scientific notation for
 * "round" values (100.0 -> "100", not "1e+02") that verifies the extra
 * digits revealed are genuine trailing zeros and not real precision noise
 * from a value that isn't exactly decimal-clean in binary (1e300 -> the
 * clean "1e+300", not 17 digits of binary-conversion noise). */
static void test_flonum_shortest_string(void) {
    char buf[64];
    int n;

    n = num_flonum_to_shortest_cstr(129.985001, buf, sizeof(buf));
    CHECK(n == (int)strlen("129.985001") && strcmp(buf, "129.985001") == 0,
          "flonum shortest-string: 129.985001 round-trips losslessly");
    CHECK(strtod(buf, NULL) == 129.985001, "flonum shortest-string: round-trips via strtod");

    n = num_flonum_to_shortest_cstr(3.14159265358979, buf, sizeof(buf));
    CHECK(strcmp(buf, "3.14159265358979") == 0, "flonum shortest-string: pi-like value keeps all digits");

    n = num_flonum_to_shortest_cstr(100.0, buf, sizeof(buf));
    CHECK(strcmp(buf, "100") == 0, "flonum shortest-string: round value avoids needless scientific notation");
    (void)n;

    n = num_flonum_to_shortest_cstr(1e300, buf, sizeof(buf));
    CHECK(strcmp(buf, "1e+300") == 0,
          "flonum shortest-string: large non-decimal-clean value stays scientific, no noise digits");
    (void)n;

    n = num_flonum_to_shortest_cstr(0.0/0.0, buf, sizeof(buf));
    CHECK(strcmp(buf, "nan") == 0, "flonum shortest-string: NaN");
    (void)n;

    n = num_flonum_to_shortest_cstr(1.0/0.0, buf, sizeof(buf));
    CHECK(strcmp(buf, "inf") == 0, "flonum shortest-string: +inf");
    (void)n;

    n = num_flonum_to_shortest_cstr(-129.985001, buf, sizeof(buf));
    CHECK(strcmp(buf, "-129.985001") == 0, "flonum shortest-string: negative value keeps sign and all digits");
    (void)n;

    n = num_flonum_to_shortest_cstr(-100.0, buf, sizeof(buf));
    CHECK(strcmp(buf, "-100") == 0, "flonum shortest-string: negative round value, no scientific notation");
    (void)n;

    CHECK(vis_true(run("(= (string->number (number->string 129.985001)) 129.985001)")),
          "number->string round-trips 129.985001 (Scheme level)");
    CHECK(vis_true(run("(string=? (number->string 100.0) \"100\")")),
          "number->string of a round value has no scientific notation");
}

static void test_complex(void) {
    CHECK(vis_true(run("(complex? (make-rectangular 3 4))")), "make-rectangular");
    CHECK(vis_true(run("(= (magnitude (make-rectangular 3.0 4.0)) 5.0)")), "magnitude 3+4i=5");
}

static void test_quaternion(void) {
    val_t q = run("(make-quaternion 1.0 2.0 3.0 4.0)");
    CHECK(vis_quat(q), "make-quaternion");
    CHECK(vis_true(run("(quaternion? (make-quaternion 1.0 0.0 0.0 0.0))")), "quaternion?");
}

static void test_octonion(void) {
    val_t o = run("(make-octonion 1.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0)");
    CHECK(vis_oct(o), "make-octonion");
}

static void test_lists(void) {
    CHECK(vis_true(run("(equal? '(1 2 3) (list 1 2 3))")),  "list equality");
    CHECK(vunfix(run("(length '(a b c))")) == 3,             "length");
    CHECK(vis_true(run("(equal? (reverse '(1 2 3)) '(3 2 1))")), "reverse");
    CHECK(vis_true(run("(equal? (map (lambda (x) (* x x)) '(1 2 3)) '(1 4 9))")), "map");
    CHECK(vis_true(run("(equal? (filter odd? '(1 2 3 4 5)) '(1 3 5))")), "filter");
}

static void test_strings(void) {
    CHECK(vis_true(run("(string? \"hello\")")), "string?");
    CHECK(vis_true(run("(string=? \"abc\" \"abc\")")), "string=?");
    CHECK(vis_true(run("(equal? (string-append \"foo\" \"bar\") \"foobar\")")), "string-append");
}

static void test_tail_calls(void) {
    /* Deep recursion - should not stack overflow with TCO */
    val_t r = run("(let loop ((n 100000) (acc 0)) (if (= n 0) acc (loop (- n 1) (+ acc 1))))");
    CHECK(vis_fixnum(r) && vunfix(r) == 100000, "tail call - 100k iterations");
}

static void test_closures(void) {
    run("(define (make-counter) (let ((n 0)) (lambda () (set! n (+ n 1)) n)))");
    run("(define c (make-counter))");
    CHECK(vunfix(run("(c)")) == 1, "closure counter 1");
    CHECK(vunfix(run("(c)")) == 2, "closure counter 2");
}

static void test_continuations(void) {
    val_t r = run("(call/cc (lambda (k) (k 42) 99))");
    CHECK(vis_fixnum(r) && vunfix(r) == 42, "call/cc escape");
}

static void test_sets(void) {
    run("(define s (make-set))");
    run("(set-add! s 1) (set-add! s 2) (set-add! s 3)");
    CHECK(vis_true(run("(set-member? s 2)")),      "set-member?");
    CHECK(vis_false(run("(set-member? s 99)")),    "set non-member");
    CHECK(vunfix(run("(set-size s)")) == 3,        "set-size");
}

static void test_hash_tables(void) {
    run("(define h (make-hash-table))");
    run("(hash-table-set! h \"key\" 42)");
    CHECK(vunfix(run("(hash-table-ref h \"key\" #f)")) == 42, "hash-table-ref");
    CHECK(vis_false(run("(hash-table-ref h \"missing\" #f)")), "hash-table miss");
}

static void test_records(void) {
    run("(define-record-type point (make-point x y) point? (x point-x) (y point-y set-point-y!))");
    run("(define p (make-point 3 4))");
    CHECK(vis_true(run("(point? p)")),              "record predicate");
    CHECK(vunfix(run("(point-x p)")) == 3,          "record accessor");
    run("(set-point-y! p 10)");
    CHECK(vunfix(run("(point-y p)")) == 10,         "record mutator");
}

static void test_guard(void) {
    val_t r = run("(guard (e (#t 'caught)) (error \"test error\"))");
    CHECK(r == sym_intern_cstr("caught"), "guard catches error");
}

/* Tier 2.1 IR (src/ir.h, docs/thoughts/performance-chez-kaappi.md §5):
 * compiler_ir_self_check(expr) compiles expr both via the existing direct
 * compile() path and via the new ir_lower+ir_emit path and asserts the
 * resulting bytecode is byte-for-byte identical. Covers the bounded
 * subset ir_lower recognizes natively (literals, quote, symbol refs at
 * every scope depth, if, begin) plus IR_FALLBACK's delegation for
 * everything else (calls, lambda, let, and/or, ...) nested inside those
 * forms -- exercising both the native cases and the fallback composition
 * they need to work with real code. */
static void test_ir_self_check(void) {
    static const char *cases[] = {
        "42",
        "3.14",
        "\"hello\"",
        "#\\a",
        "#t",
        "#f",
        "'()",
        "#:foo",
        "(quote (1 2 3))",
        "(quote ())",
        "(quote #t)",
        "(quote #f)",
        "(if #t (quote ()) (quote ()))",
        "(if #t 1 2)",
        "(if #f 1)",
        "(begin)",
        "(begin 1)",
        "(begin 1 2 3)",
        "(if (> 3 2) (begin (+ 1 2) 'yes) 'no)",
        "((lambda (x) (if x (+ x 1) 0)) 5)",
        "(begin (define x 10) (if (> x 5) (* x 2) x))",
        "(let ((a 1) (b 2)) (if (< a b) (begin a (+ a b)) b))",
        "(if (if #t #f #t) 'a (begin 'b 'c))",
        /* set! -- widened in the second landing */
        "(set! some-global 5)",
        "(begin (set! another-global (+ 1 2)) 'done)",
        "(if #t (set! g1 1) (set! g2 2))",
        /* and/or -- widened in the second landing */
        "(and)",
        "(and 1)",
        "(and 1 2 3)",
        "(and 1 #f 3)",
        "(or)",
        "(or #f)",
        "(or #f 1 2)",
        "(or 1 2)",
        "(and (> 3 2) (< 1 2))",
        "(or (> 1 2) (< 1 2))",
        /* and/or nested inside if/begin, mixed with IR_FALLBACK-only
         * subexpressions (calls) -- the exact shape that caught the two
         * ordering bugs during the first landing. */
        "(if (and (f 1) (g 2)) 'yes 'no)",
        "(begin (or (h 1) (k 2)) 'tail)",
        "(and (begin 1 2) (if #t 3 4))",
        "(or (and 1 2) (and 3 #f))",
        /* define -- (define sym expr) only, widened in the third landing.
         * Lambda-sugar `(define (f ...) ...)` deliberately stays
         * IR_FALLBACK (see ir_lower_define's comment) and is exercised by
         * the existing "(begin (define x 10) ...)" case above and by
         * ordinary compiler tests elsewhere -- not re-tested here. */
        "(define some-new-global 42)",
        "(define another-new-global)",
        "(begin (define p 1) (define q 2) (+ p q))",
        "(if #t (define ifdef-a 1) (define ifdef-b 2))",
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        val_t port = port_open_input_string(cases[i], (uint32_t)strlen(cases[i]));
        val_t expr = scm_read(port);
        char msg[256];
        snprintf(msg, sizeof(msg), "ir self-check: %s", cases[i]);
        CHECK(compiler_ir_self_check(expr), msg);
    }

    /* Regression case for a real bug independent code review found: a
     * macro-rebuilt `begin` spine (scm_cons always stamps hdr.flags = 0
     * on the cells IT conses, unlike the reader) whose spliced-in items
     * retain their own original, non-zero reader-stamped lines. This is
     * exactly the shape that let items[i]->line silently diverge from
     * the seq-spine line compile_seq itself would use for OP_POP -- a
     * divergence invisible to a code-only bytecode comparison (only
     * chunk->lines[] differs), which is also why compiler_ir_self_check
     * itself now compares lines[] too, not just code. */
    run("(define-syntax my-begin2 (syntax-rules () ((_ a b) (begin a b))))");
    {
        const char *src = "(my-begin2 1 2)";
        val_t port = port_open_input_string(src, (uint32_t)strlen(src));
        val_t expr = scm_read(port);
        CHECK(compiler_ir_self_check(expr), "ir self-check: macro-expanded begin spine");
    }

    /* Exception-safety regression: compiler_ir_self_check must not crash
     * or corrupt gc_inhibit_count when compile() raises mid-way
     * (independent security review -- see this function's own
     * SCM_PROTECT comment in compiler.c). A begin with >256 distinct
     * symbols overflows chunk_add_const's compiler-limit check, a
     * reliable, clean compile-time scm_raise. Wrap the call in our own
     * SCM_PROTECT since compiler_ir_self_check correctly RE-raises (it
     * is a verification tool, not an error-swallowing one) -- catching
     * it here just confirms the process is still in a sane, unbalanced-
     * nothing state afterward. */
    {
        char src[8192];
        /* snprintf's return value is how many bytes WOULD have been
         * written, not how many actually were -- accumulating it
         * unchecked into `off` and using `sizeof(src) - off` for the
         * next call's size argument underflows (size_t is unsigned) the
         * moment off exceeds sizeof(src), turning "buffer full" into
         * "here, take this huge size and write past the end" (flagged by
         * CodeQL). 300 short symbol names comfortably fit in 8192 bytes
         * in practice, but the pattern itself is the bug, independent of
         * whether today's fixed inputs happen to avoid it -- clamp `off`
         * to the buffer size so a future change to the loop bound or
         * symbol-name length can't silently reintroduce an overflow. */
        size_t off = (size_t)snprintf(src, sizeof(src), "(begin");
        if (off > sizeof(src)) off = sizeof(src);
        for (int i = 0; i < 300 && off < sizeof(src); i++) {
            int n = snprintf(src + off, sizeof(src) - off, " sym%d", i);
            off += (size_t)n;
            if (off > sizeof(src)) off = sizeof(src);
        }
        if (off < sizeof(src)) snprintf(src + off, sizeof(src) - off, ")");
        val_t port = port_open_input_string(src, (uint32_t)strlen(src));
        val_t expr = scm_read(port);

        int before = gc_inhibit_save();
        ExnHandler h;
        bool raised = false;
        SCM_PROTECT(h, {
            compiler_ir_self_check(expr);
        }, {
            raised = true;
        });
        int after = gc_inhibit_save();
        CHECK(raised, "ir self-check: >256-constant overflow raises");
        CHECK(before == after, "ir self-check: gc_inhibit_count balanced after raise");
    }
}

#define RUN_TEST(fn) do { fprintf(stderr, ">> " #fn "\n"); fn(); fflush(stdout); } while(0)

int main(void) {
    init();

    RUN_TEST(test_fixnums);
    RUN_TEST(test_bignums);
    RUN_TEST(test_rationals);
    RUN_TEST(test_floats);
    RUN_TEST(test_flonum_shortest_string);
    RUN_TEST(test_complex);
    RUN_TEST(test_quaternion);
    RUN_TEST(test_octonion);
    RUN_TEST(test_lists);
    RUN_TEST(test_strings);
    RUN_TEST(test_tail_calls);
    RUN_TEST(test_closures);
    RUN_TEST(test_continuations);
    RUN_TEST(test_sets);
    RUN_TEST(test_hash_tables);
    RUN_TEST(test_records);
    RUN_TEST(test_guard);
    RUN_TEST(test_ir_self_check);

    printf("\n%d passed, %d failed\n", pass, fail);
    return fail > 0 ? 1 : 0;
}
