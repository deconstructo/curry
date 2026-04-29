/*
 * curry_symengine — Symbolic mathematics module for Curry Scheme.
 *
 * Wraps the SymEngine C++ library (https://github.com/symengine/symengine).
 *
 * Scheme API:
 *   Symbols and expressions:
 *     (sym-symbol "x")                  -> sym-expr
 *     (sym-integer n)                   -> sym-expr
 *     (sym-rational p q)                -> sym-expr
 *     (sym-float x)                     -> sym-expr
 *
 *   Arithmetic (returns sym-expr):
 *     (sym-add a b)    (sym-sub a b)
 *     (sym-mul a b)    (sym-div a b)
 *     (sym-pow a b)    (sym-neg a)
 *
 *   Calculus:
 *     (sym-diff expr sym)              -> sym-expr   ; differentiate
 *     (sym-integrate expr sym)         -> sym-expr   ; indefinite integral
 *     (sym-expand expr)                -> sym-expr
 *     (sym-simplify expr)              -> sym-expr
 *     (sym-factor expr)                -> sym-expr
 *     (sym-subs expr old new)          -> sym-expr   ; substitute
 *
 *   Evaluation:
 *     (sym->string expr)               -> string
 *     (sym->number expr)               -> number (if ground term)
 *     (sym-eval expr bindings)         -> sym-expr   ; bindings = alist (sym . val)
 *
 *   Linear algebra:
 *     (sym-matrix rows cols init-list) -> sym-matrix
 *     (sym-matmul a b)                 -> sym-matrix
 *     (sym-det mat)                    -> sym-expr
 *     (sym-inv mat)                    -> sym-matrix
 *
 *   Solvers:
 *     (sym-solve expr sym)             -> list of solutions
 *     (sym-solve-system eqs syms)      -> alist of solutions
 *
 *   Limits and series:
 *     (sym-limit expr sym point [dir]) -> sym-expr
 *     (sym-series expr sym point n)    -> sym-expr
 */

#include <curry.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>

/* NOTE: This stub compiles without SymEngine.
 * Enable with: cmake -DBUILD_MODULE_SYMENGINE=ON
 * when SymEngine is installed (vcpkg/conan/homebrew).
 *
 * With SymEngine available, replace the stub bodies with:
 *   #include <symengine/expression.h>
 *   #include <symengine/parser.h>
 *   #include <symengine/diff.h>
 *   etc.
 */

#ifdef HAVE_SYMENGINE
#include <symengine/expression.h>
#include <symengine/parser.h>
#include <symengine/diff.h>
#include <symengine/simplify.h>
#include <symengine/solve.h>
using namespace SymEngine;
#endif

/* Wrap a SymEngine expression string as a Scheme string value for now.
 * In the real implementation, wrap RCP<const Basic> in a bytevector handle. */

static curry_val stub_sym(const char *label) {
    return curry_make_pair(curry_make_symbol("sym-expr"),
                           curry_make_string(label));
}

static curry_val fn_sym_symbol(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    return stub_sym(curry_string(av[0]));
}

static curry_val fn_sym_integer(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    char buf[32]; snprintf(buf, sizeof(buf), "%ld", (long)curry_fixnum(av[0]));
    return stub_sym(buf);
}

static curry_val fn_sym_add(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    const char *a = curry_string(curry_cdr(av[0]));
    const char *b = curry_string(curry_cdr(av[1]));
    std::string s = std::string("(") + a + "+" + b + ")";
    return stub_sym(s.c_str());
}

static curry_val fn_sym_mul(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    const char *a = curry_string(curry_cdr(av[0]));
    const char *b = curry_string(curry_cdr(av[1]));
    std::string s = std::string("(") + a + "*" + b + ")";
    return stub_sym(s.c_str());
}

static curry_val fn_sym_sub(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    const char *a = curry_string(curry_cdr(av[0]));
    const char *b = curry_string(curry_cdr(av[1]));
    std::string s = std::string("(") + a + "-" + b + ")";
    return stub_sym(s.c_str());
}

static curry_val fn_sym_div(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    const char *a = curry_string(curry_cdr(av[0]));
    const char *b = curry_string(curry_cdr(av[1]));
    std::string s = std::string("(") + a + "/" + b + ")";
    return stub_sym(s.c_str());
}

static curry_val fn_sym_pow(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    const char *a = curry_string(curry_cdr(av[0]));
    const char *b = curry_string(curry_cdr(av[1]));
    std::string s = std::string("(") + a + "^" + b + ")";
    return stub_sym(s.c_str());
}

static curry_val fn_sym_diff(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    const char *expr = curry_string(curry_cdr(av[0]));
    const char *sym  = curry_string(curry_cdr(av[1]));
    std::string s = std::string("diff(") + expr + "," + sym + ")";
    return stub_sym(s.c_str());
}

static curry_val fn_sym_expand(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    const char *expr = curry_string(curry_cdr(av[0]));
    std::string s = std::string("expand(") + expr + ")";
    return stub_sym(s.c_str());
}

static curry_val fn_sym_to_string(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    return curry_cdr(av[0]);  /* already a string */
}

static curry_val fn_sym_subs(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    const char *expr = curry_string(curry_cdr(av[0]));
    const char *old  = curry_string(curry_cdr(av[1]));
    const char *newv = curry_string(curry_cdr(av[2]));
    std::string s = std::string("subs(") + expr + "," + old + "->" + newv + ")";
    return stub_sym(s.c_str());
}

extern "C" void curry_module_init(CurryVM *vm) {
    curry_define_fn(vm, "sym-symbol",    fn_sym_symbol,    1, 1, NULL);
    curry_define_fn(vm, "sym-integer",   fn_sym_integer,   1, 1, NULL);
    curry_define_fn(vm, "sym-add",       fn_sym_add,       2, 2, NULL);
    curry_define_fn(vm, "sym-sub",       fn_sym_sub,       2, 2, NULL);
    curry_define_fn(vm, "sym-mul",       fn_sym_mul,       2, 2, NULL);
    curry_define_fn(vm, "sym-div",       fn_sym_div,       2, 2, NULL);
    curry_define_fn(vm, "sym-pow",       fn_sym_pow,       2, 2, NULL);
    curry_define_fn(vm, "sym-diff",      fn_sym_diff,      2, 2, NULL);
    curry_define_fn(vm, "sym-expand",    fn_sym_expand,    1, 1, NULL);
    curry_define_fn(vm, "sym-subs",      fn_sym_subs,      3, 3, NULL);
    curry_define_fn(vm, "sym->string",   fn_sym_to_string, 1, 1, NULL);
}
