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
#include "vm.h"
#include "builtins.h"
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
    /* Needed by anything that runs compiled bytecode on the VM rather
     * than tree-walking via eval() -- e.g. compile_time_eval (compiler.c),
     * which define-syntax uses to construct a macro's transformer. Every
     * test in this binary happened to go through eval()/run() until the
     * Tier 2.1 IR's lambda-widening tests added an internal define-syntax
     * case, which segfaulted on a NULL _Thread_local `vm` (vm.c) -- this
     * gap was always latent, just never previously exercised. */
    fprintf(stderr, "vm_init\n"); vm_init();
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

/* Like run(), but through the REAL compiled path (compiler_compile +
 * vm_run) instead of eval()'s tree-walker -- needed for anything that
 * only exists in classic compile()'s own special-form handling with no
 * tree-walker equivalent to fall back to (apply/values/call-with-values
 * are compiled directly by compile(), never interpreted by eval() at
 * all). compiler_ir_optimize_check can't stand in for this either: it
 * only catches divergence BETWEEN classic and IR compilation, and apply/
 * values/call-with-values have no IR-native form, so both its "old" and
 * "new" sides run the exact SAME classic code -- a bug shared by both
 * sides compares equal to itself and is invisible to that check (exactly
 * how the regression below escaped it, only caught by tests/sicm_tests.scm
 * actually running the compiled bytecode). Only evaluates ONE expression
 * (unlike run()) since compiler_compile takes a single expr, not a
 * sequence. */
static val_t run_vm(const char *src) {
    val_t port = port_open_input_string(src, (uint32_t)strlen(src));
    val_t expr = scm_read(port);
    val_t cl_val = compiler_compile(expr);
    BcClosure *cl = as_bcclosure(cl_val);
    vm_push(cl_val);
    return vm_run(cl, 0);
}

/* Like run_vm(), but for a whole sequence of top-level forms compiled as
 * ONE chunk (compiler_compile_script), matching how a real .scm script
 * file is compiled -- needed for anything that depends on top-level
 * forms sharing compile-time state across the sequence (e.g. a `car`
 * redefinition in an earlier form affecting a later form's own
 * compile-time decisions). run_vm can't stand in for this: it only ever
 * reads and compiles a single expression. */
static val_t run_vm_script(const char *src) {
    val_t port = port_open_input_string(src, (uint32_t)strlen(src));
    val_t head = V_NIL, *tail = &head;
    val_t datum;
    while (!vis_eof(datum = scm_read(port))) {
        *tail = scm_cons(datum, V_NIL);
        tail = &as_pair(*tail)->cdr;
    }
    val_t cl_val = compiler_compile_script(head);
    BcClosure *cl = as_bcclosure(cl_val);
    vm_push(cl_val);
    return vm_run(cl, 0);
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
        "((lambda (x) (if x (+ x 1) 0)) 5)",
        "(if (if #t #f #t) 'a (begin 'b 'c))",
        /* set! -- widened in the second landing */
        "(set! some-global 5)",
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
         * ordinary compiler tests elsewhere -- not re-tested here. */
        "(define some-new-global 42)",
        "(define another-new-global)",
        "(if #t (define ifdef-a 1) (define ifdef-b 2))",
        /* calls -- ordinary/fused-global/self-tail, widened in the fourth
         * landing. classify_head is what decides "not a special form or
         * macro, so a call" here -- see IR_CALL in ir.h and
         * classify_head's own comment. Exact-2-arg calls to
         * +, -, *, =, <, <=, >, >= deliberately excluded from this list as of
         * Tier 2.5 step 2 (arithmetic/comparison open-coding): like the
         * Tier 2.3/2.4 cases below, they no longer compile byte-identical
         * to classic by design (ir_emit's IR_CALL case open-codes them,
         * compile_classic's own SF_CALL path doesn't) -- see
         * test_ir_optimize_check's own Tier 2.5 section for their
         * result-equality replacements. */
        "(f)",
        "(f 1)",
        "(f 1 2 3)",
        "((lambda (x) x) 5)",
        /* let / let-star / letrec / letrec-star / named-let -- widened
         * in the sixth landing. Plain let/let-star/letrec are pure
         * ir_lower-time desugaring into IR_CALL{callee=IR_LAMBDA{...}}
         * (see ir_lower_let/ir_lower_let_star/ir_lower_letrec's own
         * comments); named-let is the one genuinely new IR_NAMED_LET
         * node kind, and the first thing to give IR_CALL's self-tail-
         * call branch REAL coverage (previously correct-by-inspection
         * but unreachable -- an earlier version of this test wrongly
         * claimed a named-let case tested it, when named-let itself
         * wasn't IR-lowered yet; caught by independent code review).
         *
         * Plain let/let-star/letrec/letrec-star cases with real bindings
         * moved OUT of this list as of Tier 2.4 (wrapper elision): they
         * no longer compile byte-identical to classic, by design -- see
         * test_ir_optimize_check's own Tier 2.4 section below for their
         * result-equality replacements. A zero-binding let-star is kept
         * here since it still happens to compile identically either way
         * (no params/args means no OP_SLIDE-vs-real-call-return
         * difference for ir_emit_inline_call's splice to introduce). */
        "(let* () 'empty)",
        /* named-let cases with real bindings moved OUT of this list as of
         * Tier 2.4 (named-let wrapper elision): like plain let / let* /
         * letrec / letrec* above, they no longer compile byte-identical to
         * classic (no more a real outer closure being allocated and
         * called) -- see test_ir_optimize_check's own Tier 2.4 section
         * below for their result-equality replacements. */
        /* receive/call-with-values with the "wrong" shape for the special
         * form fall through to an ordinary call (classify_head's shaped
         * checks) -- see compile()'s own SF_RECEIVE/SF_CALL_WITH_VALUES
         * comments. */
        "(receive 5)",
        "(call-with-values f)",
        /* calls nested inside already-lowered forms, mixing IR_CALL with
         * IR_IF/IR_SEQ/IR_AND/IR_OR/IR_DEFINE siblings -- the exact shape
         * that caught real ordering bugs in earlier landings. */
        "(if (f 1) (g 2) (h 3))",
        "(begin (f 1) (g 2) (h 3))",
        "(and (f 1) (g 2))",
        "(or (f 1) (g 2))",
        "(define computed (f (g 1) (h 2)))",
        /* lambda -- widened in the fifth landing. The body is walked
         * (lowered AND emitted) against a real child Compiler created at
         * ir_emit time -- see IR_LAMBDA in ir.h. */
        "(lambda (x) x)",
        "((lambda (x) x) 5)",
        "((lambda (x y) (+ x y)) 3 4)",
        "((lambda () 42))",
        "((lambda () ))",
        /* dotted (rest) params -- compile_params' negative-arity path. */
        "((lambda (a . rest) rest) 1 2 3)",
        /* multi-form body: non-last forms need the OP_POP glue compile_seq
         * emits, at the SPINE cell's own line, not the item's -- the same
         * class of bug an earlier landing's IR_SEQ caught (see ir.h's
         * pop_lines comment). */
        "((lambda (x) (+ x 1) (+ x 2) (+ x 3)) 5)",
        /* nested lambda closing over the outer's parameter -- upvalue
         * capture across the child/parent Compiler boundary. */
        "((lambda (n) (lambda (x) (+ x n))) 3)",
        "(((lambda (n) (lambda (x) (+ x n))) 3) 4)",
        /* internal define -- letrec* self-recursion: `fact` (itself a
         * nested lambda, sugar-defined) calling itself from within its
         * OWN body, which is a DIFFERENT child Compiler one level deeper
         * than the internal define's own scope. */
        "((lambda (n) (define (fact k acc) (if (= k 0) acc (fact (- k 1) (* k acc)))) (fact n 1)) 5)",
        /* internal define-syntax registered mid-body, used by a LATER
         * form in the SAME body -- the exact scoping hazard IR_LAMBDA's
         * own design comment (ir.h) exists to avoid: a naively bulk-
         * lowered body would classify the later use as an ordinary call
         * instead of expanding it, since a proxy Compiler would never see
         * the registration in the right order. */
        "((lambda () (define-syntax my-const (syntax-rules () ((_) 42))) (my-const)))",
        /* the SAME hazard, but for a plain `begin` (IR_SEQ) rather than a
         * lambda body -- IR_SEQ's own body is ALSO raw val_t, walked via
         * the same interleaved lower-then-emit design as IR_LAMBDA's, for
         * exactly this reason. This exact case was a real regression:
         * IR_SEQ originally pre-lowered every item in one eager pass
         * (before any of them were emitted), which compiler_ir_self_check
         * never caught (it doesn't execute anything, and `my-const2`
         * misclassified as an ordinary call still LOWERED to *something*,
         * a byte-comparable-but-wrong IR_CALL) -- only surfaced once
         * compile() started routing through the IR live and ctest's
         * syntax_rules suite actually ran the miscompiled form
         * (`not-a-procedure` on `my-const2`). Fixed by giving IR_SEQ the
         * same deferred-body treatment as IR_LAMBDA; this case is here so
         * it can't silently regress again. */
        "(begin (define-syntax my-const2 (syntax-rules () ((_) 43))) (my-const2))",
        /* internal define-record-type -- one of lambda_prescan's
         * multi-binding cases (constructor + predicate + accessor). */
        "((lambda () (define-record-type point (make-point x y) point? (x point-x) (y point-y)) (point-x (make-point 1 2))))",
        /* internal (symbolic ...) -- reserves a local slot without R7RS's
         * definitions-must-precede-expressions restriction. */
        "((lambda () (symbolic x) x))",
        /* define lambda-sugar -- now natively lowered as IR_DEFINE{value
         * = IR_LAMBDA} instead of falling back whole (see
         * ir_lower_define_lambda_sugar's comment). */
        "(define (add1 x) (+ x 1))",
        "(begin (define (add1 x) (+ x 1)) (add1 5))",
        /* self-recursive top-level define lambda-sugar -- `fact` resolves
         * as a fused-global call from within its own IR_LAMBDA body. */
        "(define (fact n) (if (= n 0) 1 (* n (fact (- n 1)))))",
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

/* Tier 2.2 (docs/thoughts/performance-chez-kaappi.md §5, item 2.2):
 * compiler_ir_optimize_check(expr) actually RUNS both the classic and the
 * ir_lower+ir_optimize+ir_emit compiled forms and compares RESULTS, not
 * bytecode -- dead-branch elimination is only worth having if it produces
 * DIFFERENT (shorter) bytecode for these inputs, so byte-identical
 * comparison (compiler_ir_self_check's contract) doesn't apply. Every
 * case here must be self-contained (no free/unbound variables, no
 * observable side effects) since both sides are genuinely executed. */
static void test_ir_optimize_check(void) {
    static const char *cases[] = {
        "(if #t 1 2)",
        "(if #f 1 2)",
        /* only #f is falsy in Scheme -- 0 and '() are both truthy,
         * unlike some other Lisps. */
        "(if 0 1 2)",
        "(if '() 1 2)",
        "(if #t (+ 1 2) (* 3 4))",
        "(if #f (+ 1 2) (* 3 4))",
        /* nested: the outer fold must recurse into the taken branch,
         * which is itself a foldable IR_IF. */
        "(if #t (if #f 10 20) 30)",
        "(if #f (if #t 10 20) 30)",
        /* the dead branch is never compiled at all once eliminated --
         * (car '()) would raise at runtime if it ever executed, proving
         * this isn't just a runtime-skipped jump. */
        "(if #t 42 (car '()))",
        "(if #f (car '()) 42)",
        /* test itself is a nested foldable expression, not a bare
         * literal -- ir_optimize must recurse into iff.test too. */
        "(if (if #t #t #f) 1 2)",
        /* IR_IF nested inside other already-lowered forms -- the fold
         * must still fire when it's not the top-level node. */
        "(begin 0 (if #t 'a 'b))",
        "(and #t (if #f 1 2))",
        "(or #f (if #t 1 2))",
        "(define x (if #t 10 20))",
        /* and/or boolean simplification (ir_optimize_andor). */
        "(and 1 2 3)",
        "(and #f 1 2)",
        "(and 1 #f 2)",
        "(and 1 2 #f)",
        "(and 1 2 3 'last)",
        "(or 1 2 3)",
        "(or #f 1 2)",
        "(or #f #f 1)",
        "(or 1 2 3 'last)",
        "(and #t #t #t)",
        "(or #f #f #f)",
        /* a non-last item that only becomes constant AFTER its own
         * nested fold -- ir_optimize_andor must re-check post-recursion,
         * not just the item's shape before optimizing it. */
        "(and (if #t 1 2) 3)",
        "(or (if #f 1 2) 3)",
        /* mixed with calls -- the dropped/truncated items are always
         * literals, but a surviving non-constant item (a call) must
         * still be reachable and correctly emitted. */
        "(and 1 (+ 2 3))",
        "(or #f (+ 2 3))",
        /* nested and/or -- the outer pass must recurse into an inner
         * and/or that itself simplifies. */
        "(and (and 1 2) 3)",
        "(or (or #f #f) 3)",

        /* Tier 2.5 step 2: exact-2-arg calls to +, -, *, =, <, <=, >, >= are
         * open-coded by ir_emit's IR_CALL case (compiler.c) but not by
         * compile_classic's own SF_CALL path -- like the Tier 2.3/2.4
         * cases below, they compile to genuinely different bytecode by
         * design, so this result-equality check (not
         * compiler_ir_self_check's byte-identical one) is the right home
         * for them. Every case is self-contained (no free variables), as
         * this function's own header comment requires, since both sides
         * are actually executed. */
        "(if (> 3 2) (begin (+ 1 2) 'yes) 'no)",
        "(let ((x 10)) (if (> x 5) (* x 2) x))",
        "(and (> 3 2) (< 1 2))",
        "(or (> 1 2) (< 1 2))",
        "(let ((p 1) (q 2)) (+ p q))",
        "(+ 1 (* 2 3))",
        "(- 10 3)",
        "(<= 3 3)",
        "(>= 4 3)",
        "(= 5 5)",

        /* Tier 2.3 local inliner. Its decision logic lives entirely
         * inside ir_emit's IR_CALL case (see compiler.c), not in
         * ir_optimize -- so this same classic-vs-live-IR result-equality
         * check already exercises it for free whenever a case below
         * happens to trigger it; no separate differential-check function
         * needed. Every candidate is wrapped in `(let () ...)` so its
         * internal define runs at scope_depth > 0 (a top-level define
         * doesn't register as a known-lambda candidate at all). */

        /* Basic closed helper, called twice with a fused-global (+)
         * consuming both results -- the exact shape that first exposed
         * a real miscompilation during development: `add_local`'s
         * physical-slot numbering silently aliased the first call's
         * still-pending result with the second call's own parameter
         * (25 came out as 90) until IR_CALL's argument-evaluation loops
         * were fixed to reserve a placeholder for every pending sibling
         * value before a nested inline call can claim any locals. */
        "(let () (define (sq x) (* x x)) (+ (sq 3) (sq 4)))",
        /* Three+ pending arguments to the same fused-global call. */
        "(let () (define (sq x) (* x x)) (+ (sq 1) (sq 2) (sq 3) (sq 4)))",
        /* Nested inlining: an inlined call's own argument is itself
         * another inlined call. */
        "(let () (define (sq x) (* x x)) (define (dbl x) (+ x x)) (sq (dbl 3)))",
        /* A nested closure inside the inlined body captures one of the
         * spliced-in params -- exercises end_scope's OP_CLOSE_UP firing
         * correctly for a same-frame splice, not just a real closure's
         * own frame teardown. */
        "(let () (define (adder x) (lambda (y) (+ x y))) (define add5 (adder 5)) (add5 10))",
        /* Self-recursive candidate: must be correctly rejected (not
         * inlined -- named-let/self-tail-call already owns recursive
         * loops), and still compile correctly either way. */
        "(let () (define (fact k) (if (= k 0) 1 (* k (fact (- k 1))))) (fact 5))",
        /* set! before the only call site -- must poison the registration
         * so the call sees the REASSIGNED procedure, not the original
         * inlined body. */
        "(let () (define (sq x) (* x x)) (set! sq (lambda (x) 999)) (sq 3))",
        /* Cross-Compiler poisoning: a NESTED closure mutates the outer
         * known-local via an upvalue-resolved set!, not a same-Compiler
         * one -- exercises poison_known's ->enclosing chain walk
         * specifically (a same-Compiler-only check would miss this). */
        "(let () (define (sq x) (* x x)) (define mut (lambda () (set! sq (lambda (x) 777)))) (mut) (sq 3))",
        /* Rest-param candidate: must be rejected (proper-param-list gate)
         * and still compile/run correctly via a real call. */
        "(let () (define (sumall . xs) (apply + xs)) (sumall 1 2 3))",
        /* A call to a known local via the GENERIC (non-fused-global,
         * non-self-tail) branch -- (car ops) as the callee is never a
         * bare symbol, so this exercises the ordinary call path, not the
         * inliner (confirms the two coexist correctly). */
        "(let () (define (sq x) (* x x)) (define ops (list sq sq)) ((car ops) 7))",
        /* An inlined call inside a named-let loop body, composing with
         * self-tail-call classification each iteration. */
        "(let () (define (sq x) (* x x)) (let loop ((i 0) (acc 0)) (if (= i 5) acc (loop (+ i 1) (+ acc (sq i))))))",
        /* Oversized body (over INLINE_MAX_BODY_NODES): must fall back to
         * a real call, not attempt to inline, and still be correct. */
        "(let () (define (sum10 x) (+ x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x)) (sum10 1))",
        /* A later argument's free variable shares a name with an EARLIER
         * parameter of the candidate being inlined -- a real, confirmed
         * miscompilation caught by independent code review: binding each
         * param immediately after emitting its own argument let the first
         * param (also named x) shadow the second argument's reference to
         * the OUTER x before it was ever evaluated. */
        "(let ((x 100)) (define (add x y) (+ x y)) (add 1 x))",
        "(let ((x 100)) (define (add x y) (+ x y)) (add x 1))",
        "(let ((a 1) (b 2) (c 3)) (define (f a b c) (+ a b c)) (f b c a))",

        /* Tier 2.4: let/let*-bound candidates (letrec/letrec* already
         * covered above -- they desugar to internal defines, reusing
         * the IR_DEFINE registration unchanged). */
        "(let ((f (lambda (x) (* x x)))) (+ (f 3) (f 4)))",
        /* let*: capturing (must NOT fire) vs. not (must fire) -- proves
         * the distinction is load-bearing, not accidental. */
        "(let* ((n 5) (f (lambda (x) (+ x n)))) (f 1))",
        "(let* ((f (lambda (x) (+ x 1)))) (f 1))",
        /* Sibling-shadow-by-name: f must capture the OUTER x (100),
         * never the sibling x (5) bound in the SAME let -- the sharpest
         * test that a plain let's bindings are genuinely not visible to
         * each other's inits. */
        "(let ((x 100)) (let ((x 5) (f (lambda (a) (+ a x)))) (f 1)))",
        /* Self-reference via plain let (NOT letrec) -- f is unbound
         * inside its own body under real let semantics; classic and IR
         * paths must still agree on whatever that produces. */
        /* Wrapped in guard, not left to raise past this expression:
         * compiler_ir_optimize_check runs BOTH the classic and IR
         * closures inside one shared SCM_PROTECT, so an uncaught raise
         * from the FIRST run would skip the second entirely and
         * re-propagate out of the check itself, crashing this test
         * binary rather than reporting a clean pass/fail. */
        "(let ((f (lambda (x) (if (= x 0) 1 (* x (f (- x 1))))))) (guard (e (#t 'correctly-unbound)) (f 5)))",
        /* Rest-param and oversized-body analogues, bound via let instead
         * of internal define. */
        "(let ((f (lambda args (apply + args)))) (f 1 2 3))",
        "(let ((f (lambda (x) (+ x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x)))) (f 1))",
        /* guard's asymmetric scoping inside a candidate body: e resolves
         * within the clause, x throughout. */
        "(let ((f (lambda (x) (guard (e (#t (+ e x))) (raise x))))) (f 5))",
        /* Adversarial macro-in-body: an outer-defined macro (three-tier
         * lookup) must bail out correctly. */
        "(let () (define-syntax dbl (syntax-rules () ((_ x) (* 2 x)))) (let ((f (lambda (x) (dbl x)))) (+ (f 3) (f 4))))",
        /* Adversarial internal-macro-in-body: the candidate defines its
         * own local macro via define-syntax -- must also bail out. */
        "(let ((f (lambda () (define-syntax m (syntax-rules () ((_) 42))) (m)))) (f))",
        /* A real, confirmed miscompilation caught by independent code
         * review: a let-bound candidate referencing its own binding
         * name (or a sibling's) was wrongly treated as "closed" --
         * that name isn't yet resolvable against the enclosing scope at
         * the point closedness gets checked (correctly not a capture),
         * but also isn't visible to the candidate's own init under real
         * let semantics (unlike letrec) -- so a reference to it should
         * raise unbound-variable, not silently resolve once splicing
         * makes the name a real local at the call site. Wrapped in
         * guard, not left to raise, for the same reason noted above. */
        "(let ((f (lambda (x) f))) (guard (e (#t 'correctly-unbound)) (procedure? (f 1))))",
        "(let ((y 10) (f (lambda (x) (+ x y)))) (guard (e (#t 'correctly-unbound)) (f 1)))",
        "(let* ((f (lambda (x) (+ x f)))) (guard (e (#t 'correctly-unbound)) (f 1)))",

        /* Tier 2.4: wrapper elision -- let/let-star/letrec/letrec-star's
         * OWN compiler-synthesized entry closure is now spliced directly
         * into the caller's frame (ir_lower_lambda_call's only 3 producer
         * sites; see ir_emit's IR_CALL head==V_FALSE branch). These moved
         * here from test_ir_self_check since this landing deliberately
         * makes their bytecode diverge from classic compilation -- only
         * RESULTS need to still agree. */
        "(let ((a 1) (b 2)) (+ a b))",
        "(let () 42)",
        "(let* ((a 1) (b (+ a 1)) (c (+ b 1))) c)",
        "(letrec ((even? (lambda (n) (if (= n 0) #t (odd? (- n 1))))) (odd?  (lambda (n) (if (= n 0) #f (even? (- n 1)))))) (even? 10))",
        "(letrec* ((a 1) (b (+ a 1))) b)",
        /* A nested closure inside the wrapper's own body capturing one of
         * the wrapper's OWN bindings (not a let-bound lambda CANDIDATE --
         * an ordinary escaping closure) -- proves end_scope's existing
         * OP_CLOSE_UP logic still fires correctly with no separate
         * wrapper closure frame in between it and the caller. */
        "(let ((n 7)) (define get (lambda () n)) (get))",
        "(letrec ((n 7) (get (lambda () n))) (get))",
        /* let/letrec used as an argument to an outer call, with a
         * binding name that shadows an outer variable -- composition with
         * reserve_pending_slot/release_pending_slots, the exact
         * miscompilation shape ir_emit_inline_call's own header comment
         * documents, now one level further out (the let ITSELF is the
         * pending sibling argument, not just a call inside it). */
        "(let ((x 100)) (+ x (let ((x 1) (y 2)) (+ x y))))",
        "(let ((x 100)) (+ x (letrec ((x 1) (y 2)) (+ x y))))",
        /* letrec/letrec* with 2+ mutually-recursive internal bindings --
         * proves splicing the wrapper doesn't disturb lambda_prescan's
         * existing internal-define mutual-visibility handling. */
        "(letrec ((e? (lambda (n) (if (= n 0) #t (o? (- n 1))))) (o? (lambda (n) (if (= n 0) #f (e? (- n 1)))))) (list (e? 8) (o? 8)))",
        /* A real, confirmed miscompilation caught while landing this
         * feature: a named-let's OWN init-argument-evaluation loop
         * (IR_NAMED_LET's ir_emit case) never got the same
         * reserve_pending_slot/release_pending_slots bracketing IR_CALL's
         * argument loops already have -- before wrapper elision, no named-
         * let init argument could ever splice new locals into `outer`
         * mid-loop (a nested let always built a real, separate closure),
         * so the gap was latent. Once a plain nested let could splice
         * directly into whatever compiler was active when it was emitted,
         * an EARLIER sibling init's still-pending stack value got aliased
         * by the LATER let-init's own spliced locals -- reproduced
         * directly against (srfi s128 comparators)'s real %default-hash
         * helper (a named-let whose middle binding's init is itself a
         * nested let), which segfaulted the whole process under ctest
         * before this fix. */
        "(let loop ((i 0) (acc 17) (s (let ((out (+ 1 2))) (+ out 3)))) (if (= i 1) (+ acc s) (loop (+ i 1) (+ acc 1) s)))",
        /* Same shape, deliberately 3+ siblings around the splicing init
         * (not just before it) to also cover a LATER sibling referencing
         * an outer name the spliced let's own params happen to shadow. */
        "(let ((x 100)) (let loop ((a (let ((x 1)) (+ x 1))) (b x) (c (let ((x 2)) (+ x 1)))) (+ a b c)))",

        /* Tier 2.4: named-let wrapper elision -- named-let's own "zero-arg
         * outer wrapper" (see IR_NAMED_LET's own ir_emit case) is now
         * spliced directly into the caller's frame too, the same way the
         * plain let / let* / letrec / letrec* wrapper already is; the loop's
         * OWN lambda (holding the real, still-genuinely-recursive closure
         * self-tail-call semantics) is untouched -- only the "bind
         * loop_name, evaluate the initial bindings, call it" wrapper
         * around that closure is elided. Moved here from
         * test_ir_self_check for the same reason the plain-let cases were:
         * no more real "outer" closure being allocated and called, so the
         * bytecode legitimately diverges from classic. */
        "(let loop ((i 0) (acc 0)) (if (= i 5) acc (loop (+ i 1) (+ acc i))))",
        /* a non-tail self-reference must NOT take the self-tail-call
         * path (matches compile_call's own `tail &&` guard) -- also now
         * the case that proves this wrapper's own final call correctly
         * uses OP_CALL, not OP_TAIL_CALL, when the named-let expression
         * itself isn't in tail position relative to its enclosing scope. */
        "(let loop ((i 0)) (+ 1 (if (< i 3) (loop (+ i 1)) i)))",
        /* named-let nested inside a call/if -- mixed with other already-
         * lowered forms, the exact shape that caught real ordering bugs
         * in earlier landings. */
        "(+ 1 (let loop ((i 0)) (if (= i 3) i (loop (+ i 1)))))",
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        val_t port = port_open_input_string(cases[i], (uint32_t)strlen(cases[i]));
        val_t expr = scm_read(port);
        char msg[256];
        snprintf(msg, sizeof(msg), "ir optimize-check: %s", cases[i]);
        CHECK(compiler_ir_optimize_check(expr), msg);
    }
}

/* Tier 2.3: positive proof the local inliner actually fires, not just
 * that it's harmless when disabled -- see compiler_ir_inline_fired_check's
 * own comment (compiler.c/compiler.h) for why compiler_ir_optimize_check's
 * result-equality check alone can't tell "inlined correctly" apart from
 * "silently never inlined, fell back to an ordinary call correctly" (both
 * produce the same RESULT). Split into two groups: cases that SHOULD grow
 * the live-IR bytecode (an eligible candidate genuinely gets duplicated at
 * its call site) and cases that must NOT (self-recursive, rest-param,
 * oversized-body, or non-symbol-callee candidates correctly falling back
 * to a real call) -- both directions matter equally: a false positive
 * here would mean the eligibility gates aren't actually excluding what
 * they claim to. */
static void test_ir_inline_fires(void) {
    static const char *should_fire[] = {
        "(let () (define (sq x) (* x x)) (+ (sq 3) (sq 4)))",
        "(let () (define (sq x) (* x x)) (+ (sq 1) (sq 2) (sq 3) (sq 4)))",
        "(let () (define (sq x) (* x x)) (define (dbl x) (+ x x)) (sq (dbl 3)))",
        "(let () (define (adder x) (lambda (y) (+ x y))) (define add5 (adder 5)) (add5 10))",
        /* Tier 2.4: let/let*-bound candidates. */
        "(let ((f (lambda (x) (* x x)))) (+ (f 3) (f 4)))",
        "(let* ((f (lambda (x) (+ x 1)))) (f 1))",
    };
    static const char *should_not_fire[] = {
        "(let () (define (fact k) (if (= k 0) 1 (* k (fact (- k 1))))) (fact 5))",
        "(let () (define (sumall . xs) (apply + xs)) (sumall 1 2 3))",
        "(let () (define (sq x) (* x x)) (define ops (list sq sq)) ((car ops) 7))",
        "(let () (define (sum10 x) (+ x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x)) (sum10 1))",
        /* Tier 2.4: let/let*-bound candidates that must correctly NOT
         * fire -- a real capture, a rest-param, and macro use in body. */
        "(let* ((n 5) (f (lambda (x) (+ x n)))) (f 1))",
        "(let ((f (lambda args (apply + args)))) (f 1 2 3))",
        "(let () (define-syntax dbl (syntax-rules () ((_ x) (* 2 x)))) (let ((f (lambda (x) (dbl x)))) (+ (f 3) (f 4))))",
        /* Self/sibling-reference regression -- see the matching
         * test_ir_optimize_check cases' own comment for the full
         * rationale. */
        "(let ((f (lambda (x) f))) (guard (e (#t 'correctly-unbound)) (procedure? (f 1))))",
        "(let ((y 10) (f (lambda (x) (+ x y)))) (guard (e (#t 'correctly-unbound)) (f 1)))",
        "(let* ((f (lambda (x) (+ x f)))) (guard (e (#t 'correctly-unbound)) (f 1)))",
    };
    for (size_t i = 0; i < sizeof(should_fire) / sizeof(should_fire[0]); i++) {
        val_t port = port_open_input_string(should_fire[i], (uint32_t)strlen(should_fire[i]));
        val_t expr = scm_read(port);
        char msg[256];
        snprintf(msg, sizeof(msg), "ir inline fires: %s", should_fire[i]);
        CHECK(compiler_ir_inline_fired_check(expr), msg);
    }
    for (size_t i = 0; i < sizeof(should_not_fire) / sizeof(should_not_fire[0]); i++) {
        val_t port = port_open_input_string(should_not_fire[i], (uint32_t)strlen(should_not_fire[i]));
        val_t expr = scm_read(port);
        char msg[256];
        snprintf(msg, sizeof(msg), "ir inline correctly withheld: %s", should_not_fire[i]);
        CHECK(!compiler_ir_inline_fired_check(expr), msg);
    }
}

/* Tier 2.4 stress cases, built programmatically since their whole point is
 * SIZE: a plain `let*` desugars to one nested wrapper per binding, and
 * with wrapper elision every level of that chain now splices via
 * ir_emit_inline_call with track_cycle=false -- this is the direct
 * regression test for the cycle-detection-budget-exhaustion gap the
 * design review caught (see ir_emit_inline_call's own header comment):
 * without track_cycle=false, a ~150-binding let* chain would push
 * g_inlining_bodies past MAX_INLINE_DEPTH=64 on every compile, silently
 * degrading cycle detection for genuine named-candidate mutual recursion
 * compiled anywhere else in the SAME top-level expression. A deeply
 * nested plain `let` chain is a separate smoke test for C-stack depth in
 * the compiler itself (ir_emit_inline_call recurses one C frame per
 * splice level, same as the pre-existing IR_LAMBDA path it replaces --
 * this proves that didn't get WORSE, not that recursion was newly
 * introduced). */
static void test_ir_wrapper_elision_stress(void) {
    /* (let* ((a0 0) (a1 (+ a0 1)) ... (a149 (+ a148 1))) a149) == 149 */
    {
        char buf[16384];
        /* size_t + clamp-after-every-call, matching test_ir_self_check's
         * own "snprintf's return value is how many bytes WOULD have been
         * written" fix above -- see that block's own comment for the full
         * rationale (flagged again here by CodeQL on this file's own
         * newer, unfixed copies of the identical pattern). */
        size_t off = (size_t)snprintf(buf, sizeof(buf), "(let* ((a0 0)");
        if (off > sizeof(buf)) off = sizeof(buf);
        for (int i = 1; i < 150; i++) {
            off += (size_t)snprintf(buf + off, sizeof(buf) - off,
                             " (a%d (+ a%d 1))", i, i - 1);
            if (off > sizeof(buf)) off = sizeof(buf);
        }
        snprintf(buf + off, sizeof(buf) - off, ") a149)");
        val_t port = port_open_input_string(buf, (uint32_t)strlen(buf));
        val_t expr = scm_read(port);
        CHECK(compiler_ir_optimize_check(expr),
              "ir optimize-check: long let* chain (149 bindings)");
    }

    /* Same long let* chain, but with genuine named-candidate mutual
     * recursion (even?/odd?) nested inside its own body -- proves
     * track_cycle=false on the chain's own splices doesn't disturb
     * track_cycle=true cycle detection for the mutual recursion, which
     * must still correctly decline to inline either candidate. */
    {
        char buf[16384];
        size_t off = (size_t)snprintf(buf, sizeof(buf), "(let* ((a0 0)");
        if (off > sizeof(buf)) off = sizeof(buf);
        for (int i = 1; i < 100; i++) {
            off += (size_t)snprintf(buf + off, sizeof(buf) - off,
                             " (a%d (+ a%d 1))", i, i - 1);
            if (off > sizeof(buf)) off = sizeof(buf);
        }
        off += (size_t)snprintf(buf + off, sizeof(buf) - off,
            ") (letrec ((e? (lambda (n) (if (= n 0) #t (o? (- n 1)))))"
            "           (o? (lambda (n) (if (= n 0) #f (e? (- n 1))))))"
            "  (list a99 (e? 20) (o? 20))))");
        if (off > sizeof(buf)) off = sizeof(buf);
        val_t port = port_open_input_string(buf, (uint32_t)strlen(buf));
        val_t expr = scm_read(port);
        CHECK(compiler_ir_optimize_check(expr),
              "ir optimize-check: long let* chain + nested mutual recursion");
    }

    /* (let ((x0 1)) (let ((x1 (+ x0 1))) ... N levels ... )) -- C-stack
     * depth smoke test, no result correctness subtlety (each level just
     * adds 1). N deliberately conservative (120, not e.g. 200): this
     * process's C-stack limit for this kind of recursive-descent IR
     * lowering isn't a fixed constant -- it's however many levels fit
     * given ir_emit's OWN current per-call stack-frame size, which grows
     * whenever a new case in its switch adds more local variables (an
     * unoptimized/Debug build typically doesn't shrink a function's frame
     * back down between sibling switch cases). Confirmed directly: this
     * exact test at N=200 started segfaulting purely from the Tier 2.4
     * named-let-wrapper-elision landing adding several new locals to
     * ir_emit's IR_NAMED_LET case, with no logic bug involved -- and the
     * same threshold-sensitivity was already independently confirmed
     * present on `main`, unrelated to any Tier 2.4 work at all (see the
     * MAX_LOCALS-guard commit's own message). 120 leaves comfortable
     * headroom against future, similarly innocuous frame-size growth. */
    {
        char buf[16384];
        size_t off = (size_t)snprintf(buf, sizeof(buf), "(let ((x0 1))");
        if (off > sizeof(buf)) off = sizeof(buf);
        for (int i = 1; i < 120; i++) {
            off += (size_t)snprintf(buf + off, sizeof(buf) - off,
                             " (let ((x%d (+ x%d 1)))", i, i - 1);
            if (off > sizeof(buf)) off = sizeof(buf);
        }
        off += (size_t)snprintf(buf + off, sizeof(buf) - off, " x119");
        if (off > sizeof(buf)) off = sizeof(buf);
        for (int i = 0; i < 120; i++) {
            off += (size_t)snprintf(buf + off, sizeof(buf) - off, ")");
            if (off > sizeof(buf)) off = sizeof(buf);
        }
        val_t port = port_open_input_string(buf, (uint32_t)strlen(buf));
        val_t expr = scm_read(port);
        CHECK(compiler_ir_optimize_check(expr),
              "ir optimize-check: deeply nested let chain (120 levels)");
    }
}

/* Regression test for a real, confirmed memory-corruption bug found by
 * independent code review of this same landing: a single FLAT `let` with
 * more bindings than MAX_LOCALS (256) -- unlike the let*-chain/deep-
 * nesting cases in test_ir_wrapper_elision_stress above, this hits the
 * guard in ONE ir_emit_inline_call call (argc=300 by itself), not via
 * cross-splice accumulation, and needs no deep C-stack recursion to reach
 * (deep nesting/let* accumulation toward 256 actually hits a PRE-
 * EXISTING, already-present-on-main C-stack-depth segfault around ~200
 * levels first -- confirmed by testing this same expression shape against
 * `main`, unrelated to this landing). ir_emit's new Tier 2.4 wrapper-
 * elision branch originally called ir_emit_inline_call unconditionally
 * whenever head==V_FALSE and the callee was a literal IR_LAMBDA, with no
 * MAX_LOCALS bound check -- unlike the Tier 2.3 named-candidate call site,
 * which already guards with `c->local_count + argc < MAX_LOCALS`.
 * ir_emit_inline_call's own param-claiming loop writes `c->locals[base +
 * i]` / `c->known[base + i]` for i in [0, argc) with no bounds check of
 * its own -- for argc=300, base+i walks straight past index 255 and out
 * of bounds of both fixed-size 256-entry arrays, corrupting adjacent
 * Compiler struct fields on the C stack. Confirmed as a real, not just
 * theoretical, crash: reverting just the added guard and running this
 * same expression through the CLI reliably produced a Bus error (SIGBUS),
 * not a clean Scheme-level error.
 *
 * With the guard in place, this now falls back to the ordinary "emit
 * callee as a real closure, call it" path -- which hits the SAME
 * MAX_LOCALS(256) limit compile_params/add_local already enforce for any
 * ordinary closure, on BOTH the classic and IR sides, so
 * compiler_ir_optimize_check itself would re-raise rather than return a
 * bool; this uses its own SCM_PROTECT instead, following the exact same
 * pattern as the `>256-constant overflow raises` case in
 * test_ir_self_check above -- the assertion is that this raises a
 * catchable, well-formed error (and leaves gc_inhibit_count balanced)
 * rather than corrupting memory or crashing the process.
 *
 * Deliberately its OWN function, not folded into
 * test_ir_wrapper_elision_stress above: that function's 200-level nested-
 * let case is already close to this process's C-stack limit (a pre-
 * existing, on-`main` fragility, unrelated to this landing), and a sibling
 * lexical block's own large on-stack `char buf[]` still contributes to
 * that SAME function's total stack-frame size in an unoptimized build even
 * though it's used later -- confirmed by reproducing a spurious crash in
 * the unrelated 200-level case purely from adding this buffer to that
 * function, before splitting it out. */
static void test_ir_wrapper_elision_max_locals_guard(void) {
    char buf[8192];
    /* size_t + clamp-after-every-call -- see test_ir_self_check's own
     * "snprintf's return value is how many bytes WOULD have been written"
     * comment for the full rationale. The loop's own `off < sizeof(buf)`
     * guard alone isn't enough: a single iteration can still push `off`
     * past sizeof(buf) before the guard is rechecked, and the trailing
     * snprintf call below runs unconditionally either way. */
    size_t off = (size_t)snprintf(buf, sizeof(buf), "(define x 1) (let (");
    if (off > sizeof(buf)) off = sizeof(buf);
    for (int i = 0; i < 300 && off < sizeof(buf); i++) {
        off += (size_t)snprintf(buf + off, sizeof(buf) - off, "(a%d x) ", i);
        if (off > sizeof(buf)) off = sizeof(buf);
    }
    snprintf(buf + off, sizeof(buf) - off, ") a299)");
    val_t port = port_open_input_string(buf, (uint32_t)strlen(buf));
    val_t def_expr = scm_read(port);
    val_t let_expr = scm_read(port);

    eval(def_expr, GLOBAL_ENV);  /* curry's own tree-walking Scheme evaluator (src/eval.c), not JS eval */

    int before = gc_inhibit_save();
    ExnHandler h;
    bool raised = false;
    SCM_PROTECT(h, {
        compiler_ir_optimize_check(let_expr);
    }, {
        raised = true;
    });
    int after = gc_inhibit_save();
    CHECK(raised,
          "ir optimize-check: 300-binding flat let raises cleanly (no MAX_LOCALS overflow)");
    CHECK(before == after,
          "ir optimize-check: gc_inhibit_count balanced after 300-binding let raise");
}

/* Regression test for a real, confirmed miscompilation found while
 * testing named-let wrapper elision above, but NOT specific to named-let
 * or even to this landing at all: apply/values/call-with-values are
 * classic special forms (compile()'s SF_APPLY/SF_VALUES/
 * SF_CALL_WITH_VALUES, compiler.c) with no IR-native form of their own --
 * each compiles its own multiple sub-expressions with a plain sequential
 * loop that PREDATES Tier 2.3's reserve_pending_slot convention and was
 * never updated for it (compile_call's own, otherwise-identical argument
 * loop already has this bracketing, added by Tier 2.3 -- these three just
 * never got the same pass, since nothing about landing the LOCAL inliner
 * itself touched them).
 *
 * The bug: with only Tier 2.4's let / let* / letrec / letrec* / named-let wrapper
 * elision in place (this whole branch, not just the named-let work), a
 * LATER argument to apply/values/call-with-values that happens to be one
 * of those forms now splices real locals directly into the SAME Compiler
 * compiling the earlier arguments -- but since the earlier arguments'
 * own already-pushed values were never reserve_pending_slot'd, the
 * splice computes its new locals' slot indices against a local_count
 * that doesn't account for them, aliasing physical stack positions the
 * earlier arguments are still using. Confirmed as a real, silent wrong-
 * VALUE bug (not a crash): `(apply + (list 1 (let ((x 5)) x) 2))`
 * returned 4 instead of 8 before the fix -- originally surfaced by
 * tests/sicm_tests.scm's `Hamilton-equations`/`Lagrangian->Hamiltonian`
 * failing deep in real physics code, not by anything in this file, since
 * compiler_ir_optimize_check's differential classic-vs-IR comparison
 * cannot catch it: apply/values/call-with-values have no IR-native form,
 * so its "old" and "new" sides run the EXACT SAME classic code and the
 * shared bug compares equal to itself. Needs run_vm (real compiled
 * execution), not compiler_ir_optimize_check, to catch at all. */
static void test_apply_values_pending_slot_fix(void) {
    CHECK(vunfix(run_vm("(apply + (list 1 (let ((x 5)) x) 2))")) == 8,
          "apply: let argument doesn't alias an earlier pending sibling argument");
    CHECK(vunfix(run_vm("(apply + (list 1 (let loop ((i 0)) (if (= i 1) 5 (loop (+ i 1)))) 2))")) == 8,
          "apply: named-let argument doesn't alias an earlier pending sibling argument");
    CHECK(vunfix(run_vm(
        "(call-with-values (lambda () (values 1 (let ((x 5)) x) 2)) (lambda (a b c) (+ a b c)))")) == 8,
        "call-with-values: let-producing value doesn't alias an earlier pending sibling value");
    CHECK(vunfix(run_vm(
        "(let* ((a 1) (b (apply + (list a (let ((x 5)) x) 2)))) b)")) == 8,
        "apply nested inside let*'s own wrapper-elided body still sees correct pending-slot bookkeeping");
}

/* Regression test for a real, confirmed miscompilation found by
 * independent code review: compile_do's step-expression loop (compiler.c,
 * `(do ((var init step) ...) (test result) body...)`) has the exact same
 * bug class, and needed the exact same fix, as SF_VALUES/SF_APPLY/
 * SF_CALL_WITH_VALUES above -- its own step-compilation loop also
 * predates reserve_pending_slot and was never bracketed with it. Before
 * the fix, a step expression that spliced real locals (any Tier 2.4
 * wrapper-elided form) corrupted `base`'s own computation (`d->local_count
 * - nv`, evaluated AFTER the splice-disturbed loop instead of captured
 * before it), which is doubly dangerous for a `do` loop specifically:
 * `base` here doesn't just mislocate a transient result the way apply/
 * values did, it selects which PERSISTENT loop-variable slots the
 * OP_STORE_LOCAL loop overwrites -- get it wrong and the loop variables
 * never actually update, so the termination test never becomes true and
 * the loop runs forever. Confirmed directly: `(do ((i 0 (+ i 1)) (acc 0
 * (+ acc (let ((x 1)) x)))) ((= i 3) acc))` hung indefinitely before the
 * fix (not merely a wrong value) -- caught interactively, not by this
 * suite, since nothing here previously exercised a splicing step
 * expression at all. */
static void test_do_step_pending_slot_fix(void) {
    CHECK(vunfix(run_vm(
        "(do ((i 0 (+ i 1)) (acc 0 (+ acc (let ((x 1)) x)))) ((= i 3) acc))")) == 3,
        "do: let step expression doesn't alias an earlier pending sibling step value");
    CHECK(vunfix(run_vm(
        "(do ((i 0 (+ i 1)) (acc 0 (+ acc (let loop ((j 0)) (if (= j 1) 1 (loop (+ j 1))))))) ((= i 3) acc))")) == 3,
        "do: named-let step expression doesn't alias an earlier pending sibling step value");
}

/* Regression test for a real, confirmed memory-corruption bug found by
 * independent code review: IR_NAMED_LET's own wrapper-elision splice
 * (ir_emit's IR_NAMED_LET case, compiler.c) called add_local(c,
 * loop_name) directly against the enclosing Compiler with NO MAX_LOCALS
 * guard, unlike the sibling let / let* / letrec / letrec* elision branch, which
 * already added `c->local_count + argc < MAX_LOCALS` after an earlier
 * round of this same review found the identical risk there. On overflow,
 * add_local silently returns slot 0 (a stale, already-live, unrelated
 * local's own slot) instead of a fresh one, and the OP_STORE_LOCAL that
 * follows then overwrites THAT local's real runtime value out from under
 * it -- not a crash, a silent corruption of unrelated program state.
 * Confirmed directly: a 256-param function (filling MAX_LOCALS exactly)
 * with a named-let anywhere in its body, called and returning its first
 * parameter afterward, returned a corrupted internal object instead of
 * that parameter's real value before the fix. The fix falls back to
 * building a real, separate closure for the wrapper (the pre-Tier-2.4
 * behavior) whenever there isn't room to splice safely -- this is the
 * direct regression test for that fallback actually firing and producing
 * the CORRECT value, not just avoiding a crash. Its own function, not
 * folded into another test, for the same on-stack-buffer-size reason
 * test_ir_wrapper_elision_max_locals_guard already documents. */
static void test_named_let_max_locals_guard(void) {
    char buf[8192];
    /* size_t + clamp-after-every-call -- see test_ir_self_check's own
     * "snprintf's return value is how many bytes WOULD have been written"
     * comment for the full rationale. */
    size_t off = (size_t)snprintf(buf, sizeof(buf), "(let ()");
    if (off > sizeof(buf)) off = sizeof(buf);
    off += (size_t)snprintf(buf + off, sizeof(buf) - off, " (define (f");
    if (off > sizeof(buf)) off = sizeof(buf);
    for (int i = 0; i < 256 && off < sizeof(buf); i++) {
        off += (size_t)snprintf(buf + off, sizeof(buf) - off, " p%d", i);
        if (off > sizeof(buf)) off = sizeof(buf);
    }
    off += (size_t)snprintf(buf + off, sizeof(buf) - off,
        ") (let loop ((i 0)) (if (= i 1) i (loop (+ i 1)))) p0)");
    if (off > sizeof(buf)) off = sizeof(buf);
    off += (size_t)snprintf(buf + off, sizeof(buf) - off,
        " (apply f (make-list 256 42)))");
    if (off > sizeof(buf)) off = sizeof(buf);
    CHECK(off < sizeof(buf), "named-let MAX_LOCALS guard source fits in buffer");
    CHECK(vunfix(run_vm(buf)) == 42,
          "named-let: MAX_LOCALS overflow falls back to a real closure instead of corrupting a local");
}

/* Regression test for a real, confirmed miscompilation found by
 * independent code review: compile_with_exception_handler's own
 * `(with-exception-handler handler thunk)` codegen (compiler.c) has the
 * same pending-slot bug class as SF_VALUES/SF_APPLY/SF_CALL_WITH_VALUES/
 * compile_do/compile_do's own fixes elsewhere in this file -- handler's
 * own fresh result is a still-pending value on the physical stack while
 * thunk compiles below it, and thunk splicing real locals (any Tier 2.4
 * wrapper-elided form) without accounting for handler's pending slot
 * aliases it. Confirmed as a real bug: `(with-exception-handler (lambda
 * (e) 'handled) (let ((f (lambda () 42))) f))` returned 'handled (as if
 * an exception were raised and caught) instead of calling the thunk and
 * returning 42 -- silently wrong control flow, not merely a wrong value. */
static void test_with_exception_handler_pending_slot_fix(void) {
    CHECK(run_vm(
        "(with-exception-handler (lambda (e) 'handled) (let ((f (lambda () 'ok))) f))")
        == sym_intern_cstr("ok"),
        "with-exception-handler: let-producing thunk doesn't alias handler's pending slot");
}

/* Tier 2.5 step 1 (open-coding car/cdr, compiler.c's ir_emit IR_CALL
 * case + OP_CAR/OP_CDR, vm.c). Basic correctness, plus a real regression
 * caught while landing this: the first version captured "whatever car
 * currently resolves to" as a compile-time constant to compare against
 * at runtime, instead of the actual, immutable prim_car function
 * pointer -- if car had ALREADY been redefined before a later call site
 * in the SAME script was compiled (an entirely ordinary sequence:
 * `(define (car x) ...)` followed by a later `(car ...)` call, both
 * compiled together as one chunk by compiler_compile_script, exactly
 * how a real .scm file compiles), that captured "expected" value WAS
 * the redefinition, so the runtime guard matched it forever and kept
 * open-coding raw pair access instead of ever calling the redefined
 * procedure. `(define (car x) (list 'redefined x)) (car (list 1 2 3))`
 * returned `1` (silently ignoring the redefinition) before this fix. */
static void test_car_cdr_open_coding(void) {
    CHECK(vunfix(run_vm("(car (list 1 2 3))")) == 1,
          "open-coded car: basic correctness");
    {
        val_t cdr_result = run_vm("(cdr (list 1 2 3))");
        CHECK(vis_pair(cdr_result) && vunfix(vcar(cdr_result)) == 2,
              "open-coded cdr: basic correctness");
    }

    int before = gc_inhibit_save();
    ExnHandler h;
    bool raised = false;
    SCM_PROTECT(h, { run_vm("(car 5)"); }, { raised = true; });
    int after = gc_inhibit_save();
    CHECK(raised, "open-coded car: wrong-type argument still raises");
    CHECK(before == after, "open-coded car: gc_inhibit_count balanced after raise");

    raised = false;
    SCM_PROTECT(h, { run_vm("(car 1 2)"); }, { raised = true; });
    CHECK(raised, "open-coded car: wrong arity still raises");

    /* The actual regression: car redefined, then called, in the SAME
     * compiled chunk (run_vm_script, not run_vm -- see its own comment). */
    val_t redefined = run_vm_script(
        "(define (car x) (list 'redefined x)) (car (list 1 2 3))");
    CHECK(vis_pair(redefined) && vcar(redefined) == sym_intern_cstr("redefined"),
          "open-coded car: redefinition in the same script is honored, not silently skipped");

    /* Shadowing by a local named car/cdr must never open-code at all --
     * resolve_local(c, head) >= 0 makes this branch unreachable for a
     * shadowed reference, same guard the ordinary fused-global-call path
     * already relies on. */
    CHECK(vunfix(run_vm("(let ((car (lambda (x) (+ x 100)))) (car 5))")) == 105,
          "open-coded car: local shadow is never open-coded");
}

/* Tier 2.5 step 1, extended to cons/pair?/null? -- same design as
 * test_car_cdr_open_coding above, same class of redefinition/shadow/
 * arity coverage, plus the reserve_pending_slot bracketing cons's own
 * two-argument emission needed (the same bug class as compile_do/
 * SF_APPLY/etc.'s own "Tier 2.4 fix"es elsewhere in this file: a second
 * argument that splices real locals must not treat the first argument's
 * still-pending value as absent). */
static void test_cons_pairp_nullp_open_coding(void) {
    {
        val_t p = run_vm("(cons 1 2)");
        CHECK(vis_pair(p) && vunfix(vcar(p)) == 1 && vunfix(vcdr(p)) == 2,
              "open-coded cons: basic correctness");
    }
    CHECK(run_vm("(pair? (cons 1 2))") == V_TRUE,
          "open-coded pair?: #t on a real pair");
    CHECK(run_vm("(pair? 5)") == V_FALSE,
          "open-coded pair?: #f on a non-pair");
    CHECK(run_vm("(null? '())") == V_TRUE,
          "open-coded null?: #t on the empty list");
    CHECK(run_vm("(null? 5)") == V_FALSE,
          "open-coded null?: #f on a non-nil value");

    /* cons's own pending-slot regression: a let that splices real locals
     * as cons's SECOND argument, while the first argument's own value is
     * still pending. */
    {
        val_t p = run_vm("(cons 1 (let ((x 2)) x))");
        CHECK(vis_pair(p) && vunfix(vcar(p)) == 1 && vunfix(vcdr(p)) == 2,
              "open-coded cons: let as second argument doesn't alias the first, still-pending argument");
    }

    /* Redefinition in the same compiled chunk, same shape as car's own
     * regression test above. */
    {
        val_t redefined = run_vm_script(
            "(define (cons a b) (list 'redefined a b)) (cons 1 2)");
        CHECK(vis_pair(redefined) && vcar(redefined) == sym_intern_cstr("redefined"),
              "open-coded cons: redefinition in the same script is honored, not silently skipped");
    }
    {
        val_t redefined = run_vm_script(
            "(define (pair? x) 'redefined) (pair? 5)");
        CHECK(redefined == sym_intern_cstr("redefined"),
              "open-coded pair?: redefinition in the same script is honored, not silently skipped");
    }

    /* Local shadow must never open-code. */
    CHECK(vunfix(run_vm("(let ((cons (lambda (a b) (+ a b)))) (cons 3 4))")) == 7,
          "open-coded cons: local shadow is never open-coded");
}

/* Regression test for a real, confirmed TCO (proper tail call) violation
 * found by independent code review: the open-coding landing above
 * originally never checked whether a car/cdr/cons/pair?/null? call was
 * in TAIL position before open-coding it. Every one of these opcodes'
 * fallback path (taken when the name has been redefined) goes through
 * call_foreign, which recurses via apply_arr -> a NESTED vm_run() --
 * unlike OP_TAIL_CALL/OP_TAIL_CALL_GLOBAL's own fallback, which reuses
 * the current CallFrame for a BcClosure callee. A self-tail-recursive
 * redefinition compiled to the open-coded opcode therefore grew the C
 * stack by one frame per call instead of looping in O(1) C-stack space --
 * `(define (cdr n) (if (= n 0) 'done (cdr (- n 1)))) (cdr 2000000)`
 * overflowed the VM's own 256-frame call-stack limit almost immediately
 * on the buggy version, where an identically-shaped loop using an
 * ordinary (non-open-coded) name completed normally. Fixed by never
 * open-coding a call already known to be in tail position at all (see
 * ir_emit's own "Tier 2.5" comment, compiler.c) -- this is the direct
 * regression test for that fix actually holding: a redefinition of each
 * of the five open-coded names, self-tail-recursing enough times that
 * anything short of genuine O(1)-C-stack tail-call reuse would exhaust
 * the 256-frame limit and raise a catchable stack-overflow condition
 * instead of completing. */
static void test_open_coding_preserves_tco(void) {
    CHECK(run_vm_script(
        "(define (cdr n) (if (= n 0) 'done (cdr (- n 1)))) (cdr 5000)")
        == sym_intern_cstr("done"),
        "open-coded cdr: redefinition called in tail position still gets proper TCO");
    CHECK(run_vm_script(
        "(define (car n) (if (= n 0) 'done (car (- n 1)))) (car 5000)")
        == sym_intern_cstr("done"),
        "open-coded car: redefinition called in tail position still gets proper TCO");
    CHECK(run_vm_script(
        "(define (cons a b) (if (= a 0) 'done (cons (- a 1) b))) (cons 5000 1)")
        == sym_intern_cstr("done"),
        "open-coded cons: redefinition called in tail position still gets proper TCO");
    CHECK(run_vm_script(
        "(define (pair? n) (if (= n 0) 'done (pair? (- n 1)))) (pair? 5000)")
        == sym_intern_cstr("done"),
        "open-coded pair?: redefinition called in tail position still gets proper TCO");
    CHECK(run_vm_script(
        "(define (null? n) (if (= n 0) 'done (null? (- n 1)))) (null? 5000)")
        == sym_intern_cstr("done"),
        "open-coded null?: redefinition called in tail position still gets proper TCO");
}

/* Tier 2.5 step 2: open-coding extended from car/cdr/cons/pair?/null? to
 * +, -, *, =, <, <=, >, >= -- same design throughout (see opcode.h's own
 * comment on this whole opcode group, and builtins.h's comment on
 * prim_add/prim_num_lt/etc. for why the fast path calls the real
 * primitive directly instead of reusing OP_ADD/OP_LT/etc.'s own separate
 * pre-existing inline fixnum fast-path bodies). Covers basic correctness
 * for all eight, the same redefinition-in-the-same-script and local-shadow
 * regressions already established for car/cdr/cons/pair?/null?, a non-2-
 * arg call falling through correctly (R7RS variadic +/-/*'s own reduction,
 * not replicated by the open-coded 2-arg-only opcode), and -- the actual
 * reason this step's design changed mid-implementation -- a symbolic (CAS
 * sym-var) operand to < still returning a genuine symbolic comparison node
 * rather than silently doing the wrong thing, which a naive reuse of
 * OP_LT's own fixnum-or-num_lt fast path would NOT have done correctly. */
static void test_arithmetic_open_coding(void) {
    CHECK(vunfix(run_vm("(+ 2 3)")) == 5, "open-coded +: basic correctness");
    CHECK(vunfix(run_vm("(- 5 3)")) == 2, "open-coded -: basic correctness");
    CHECK(vunfix(run_vm("(* 4 3)")) == 12, "open-coded *: basic correctness");
    CHECK(run_vm("(= 3 3)") == V_TRUE, "open-coded =: #t on equal");
    CHECK(run_vm("(= 3 4)") == V_FALSE, "open-coded =: #f on unequal");
    CHECK(run_vm("(< 3 4)") == V_TRUE, "open-coded <: #t");
    CHECK(run_vm("(< 4 3)") == V_FALSE, "open-coded <: #f");
    CHECK(run_vm("(<= 3 3)") == V_TRUE, "open-coded <=: #t on equal");
    CHECK(run_vm("(> 4 3)") == V_TRUE, "open-coded >: #t");
    CHECK(run_vm("(>= 3 3)") == V_TRUE, "open-coded >=: #t on equal");

    /* Non-2-arg calls must fall through to the ordinary variadic path,
     * not be silently mishandled by the strictly-binary opcode. */
    CHECK(vunfix(run_vm("(+ 1 2 3)")) == 6,
          "open-coded +: 3-arg call falls through to variadic primitive");
    CHECK(vunfix(run_vm("(+ 5)")) == 5,
          "open-coded +: 1-arg call falls through to variadic primitive");
    CHECK(vunfix(run_vm("(+)")) == 0,
          "open-coded +: 0-arg call falls through to variadic primitive");

    /* Local shadow must never open-code. */
    CHECK(vunfix(run_vm("(let ((+ (lambda (a b) 999))) (+ 1 2))")) == 999,
          "open-coded +: local shadow is never open-coded");

    /* The symbolic-dispatch gap this step's design was corrected for: a
     * sym-var operand to < must still produce a genuine symbolic
     * comparison node (via prim_num_lt's own sx_lt dispatch), not a raw
     * #t/#f or a raise -- confirming the open-coded opcode really does
     * call the real primitive rather than any hand-rolled fixnum-only
     * fast path. MUST run before the redefinition tests below: those
     * redefine +/< via run_vm_script, which -- like every other top-level
     * define in this codebase -- writes into the real, process-wide
     * GLOBAL_ENV (not some scoped/reset test fixture), so a later call in
     * this SAME process would otherwise see the poisoned binding instead
     * of the real primitive (this exact ordering hazard is also documented
     * project-wide, see feedback_scheme_base_global_env_aliasing_test_
     * gotcha in this repo's memory notes). */
    {
        val_t r = run_vm("(< (sym-var 'x) 5)");
        CHECK(vis_symbolic(r),
              "open-coded <: symbolic operand still dispatches to a symbolic comparison node");
    }
    {
        val_t r = run_vm("(+ (sym-var 'x) 5)");
        CHECK(vis_symbolic(r),
              "open-coded +: symbolic operand still dispatches to a symbolic result");
    }

    /* Redefinition in the same compiled chunk, same shape as car's own
     * regression test above. From here on, +/< are permanently redefined
     * for the rest of THIS TEST FUNCTION (see the comment just above) --
     * nothing after this point may assume the real primitive. */
    {
        val_t redefined = run_vm_script(
            "(define (+ a b) (list 'redefined a b)) (+ 1 2)");
        CHECK(vis_pair(redefined) && vcar(redefined) == sym_intern_cstr("redefined"),
              "open-coded +: redefinition in the same script is honored, not silently skipped");
    }
    {
        val_t redefined = run_vm_script(
            "(define (< a b) 'redefined) (< 1 2)");
        CHECK(redefined == sym_intern_cstr("redefined"),
              "open-coded <: redefinition in the same script is honored, not silently skipped");
    }

    /* TCO preserved in tail position, same regression class as the other
     * five open-coded opcodes. Each of these ITSELF redefines +/< again
     * (to the self-tail-recursive version) before calling it, so the
     * already-poisoned binding from just above doesn't matter here. */
    CHECK(run_vm_script(
        "(define (+ a b) (if (= a 0) 'done (+ (- a 1) b))) (+ 5000 1)")
        == sym_intern_cstr("done"),
        "open-coded +: redefinition called in tail position still gets proper TCO");
    CHECK(run_vm_script(
        "(define (< a b) (if (= a 0) 'done (< (- a 1) b))) (< 5000 1)")
        == sym_intern_cstr("done"),
        "open-coded <: redefinition called in tail position still gets proper TCO");
}

/* Tier 2.6 step 1: smoke test for compiler_ir_lower_for_jit -- see its
 * own comment (compiler.c/compiler.h) for the full contract. Just checks
 * this new entry point produces a real tree (and a matching arena) for
 * ordinary input, since nothing calls it live yet (the codegen.cpp
 * rewrite that would actually consume this tree is deliberately out of
 * scope for this landing). */
static void test_compiler_ir_lower_for_jit(void) {
    {
        val_t port = port_open_input_string("(+ 1 2)", 7);
        val_t expr = scm_read(port);
        IRArena *arena = NULL;
        IRNode *ir = compiler_ir_lower_for_jit(expr, &arena);
        CHECK(ir != NULL, "compiler_ir_lower_for_jit: returns a tree for ordinary input");
        CHECK(arena != NULL, "compiler_ir_lower_for_jit: returns an arena for ordinary input");
        if (arena) ir_arena_free(arena);
    }
    /* Malformed input: `(if)` -- exercises require_min_args, added to
     * ir_lower_if (and every other special-form compile_* / ir_lower_*
     * function in this file, a separate, wider fix -- see
     * test_special_form_arity_crashes below for the full story). Before
     * that fix, ir_lower/ir_optimize never actually raised for ANY
     * currently-reachable top-level input at all (everything either
     * became an unresolved IR_VAR_REF, deferred to ir_emit time, or fell
     * back whole to IR_FALLBACK) -- `(if)` specifically used to SIGSEGV
     * the whole process instead, so this is also this new entry point's
     * own regression test for that fix actually reaching ir_lower's own
     * dispatch, not just compile()'s classic one. */
    {
        val_t port = port_open_input_string("(if)", 4);
        val_t expr = scm_read(port);
        int before = gc_inhibit_save();
        IRArena *arena = (IRArena *)(void *)1;  /* poison: must be overwritten */
        IRNode *ir = compiler_ir_lower_for_jit(expr, &arena);
        int after = gc_inhibit_save();
        CHECK(ir == NULL, "compiler_ir_lower_for_jit: malformed input returns NULL, not a longjmp");
        CHECK(arena == NULL, "compiler_ir_lower_for_jit: malformed input clears *out_arena");
        CHECK(before == after, "compiler_ir_lower_for_jit: gc_inhibit_count balanced after raise");
    }
}

/* Regression test for a real, confirmed, WIDESPREAD bug found by
 * independent code review while landing Tier 2.6 step 1 (not something
 * that landing introduced -- confirmed present on `main` too): every
 * special-form compile_* / ir_lower_* function in this file originally read
 * its own operands with bare vcar/vcdr and no arity/shape check at all,
 * so a malformed-but-syntactically-readable source form -- too few parts
 * for the special form it names -- SIGSEGV'd the whole process instead of
 * raising a catchable condition. Found via `(if)`; confirmed to affect at
 * least 19 special forms by direct testing before the fix (require_min_args,
 * compiler.c). This is the regression test for all of them at once: each
 * must now raise a catchable EC_WRONG_NUMBER_OF_ARGUMENTS condition
 * (with gc_inhibit_count left balanced) instead of crashing. Uses run_vm
 * (the real compiled path) rather than compiler_ir_lower_for_jit, since
 * several of these forms (case/when/unless/do/guard/let-values/
 * parameterize/define-record-type) have no IR-native lowering at all and
 * are only reachable through compile()'s own classic dispatch. */
static void test_special_form_arity_crashes(void) {
    static const char *const forms[] = {
        "(if)", "(if 1)", "(let)", "(let loop)", "(let*)", "(letrec)",
        "(letrec*)", "(lambda)", "(define)", "(set!)", "(case)", "(when)",
        "(unless)", "(do)", "(guard)", "(let-values)", "(let*-values)",
        "(parameterize)", "(define-record-type)",
        "(define-syntax)", "(let-syntax)", "(letrec-syntax)", "(syntax-rules)",
        /* with-exception-handler: missed by the original sweep (found by
         * independent security review of the Tier 2.5 step 2 commit) --
         * compile_with_exception_handler read vcar(args)/vcar(vcdr(args))
         * unconditionally, with no require_min_args guard and no shape
         * check in classify_head (unlike compile_receive's own guard at
         * that same call site) to keep a too-short arg list from ever
         * reaching it. `(with-exception-handler)` and
         * `(with-exception-handler (lambda (e) e))` both SIGSEGV'd the
         * whole process before this was added to require_min_args. */
        "(with-exception-handler)", "(with-exception-handler (lambda (e) e))",
    };
    for (size_t i = 0; i < sizeof(forms) / sizeof(forms[0]); i++) {
        int before = gc_inhibit_save();
        ExnHandler h;
        bool raised = false;
        SCM_PROTECT(h, { run_vm(forms[i]); }, { raised = true; });
        int after = gc_inhibit_save();
        char msg[128];
        snprintf(msg, sizeof(msg), "%s: raises cleanly instead of crashing", forms[i]);
        CHECK(raised, msg);
        snprintf(msg, sizeof(msg), "%s: gc_inhibit_count balanced after raise", forms[i]);
        CHECK(before == after, msg);
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
    RUN_TEST(test_ir_optimize_check);
    RUN_TEST(test_ir_inline_fires);
    RUN_TEST(test_ir_wrapper_elision_stress);
    RUN_TEST(test_ir_wrapper_elision_max_locals_guard);
    RUN_TEST(test_apply_values_pending_slot_fix);
    RUN_TEST(test_do_step_pending_slot_fix);
    RUN_TEST(test_named_let_max_locals_guard);
    RUN_TEST(test_with_exception_handler_pending_slot_fix);
    RUN_TEST(test_car_cdr_open_coding);
    RUN_TEST(test_cons_pairp_nullp_open_coding);
    RUN_TEST(test_open_coding_preserves_tco);
    RUN_TEST(test_arithmetic_open_coding);
    RUN_TEST(test_compiler_ir_lower_for_jit);
    RUN_TEST(test_special_form_arity_crashes);

    printf("\n%d passed, %d failed\n", pass, fail);
    return fail > 0 ? 1 : 0;
}
