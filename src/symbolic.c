#include "symbolic.h"
#include "sx_rules.h"
#include "sx_algebra.h"
#include "sx_poly.h"
#include "object.h"
#include "set.h"    /* scm_equal */
#include "eval.h"   /* apply_arr */
#include "gc.h"
#include "symbol.h"
#include "numeric.h"
#include "port.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

extern void scm_raise(val_t kind, const char *fmt, ...) __attribute__((noreturn));

/* Interned operator symbols */
val_t SX_ADD, SX_SUB, SX_MUL, SX_DIV, SX_NEG;
val_t SX_NCMUL;
val_t SX_EXPT, SX_SQRT, SX_SIN, SX_COS, SX_TAN, SX_EXP, SX_LOG, SX_ABS;
val_t SX_INTEGRATE, SX_CONJ, SX_REAL, SX_IMAG;
val_t SX_FRACDIFF, SX_FRACINT;
val_t SX_SINH, SX_COSH, SX_TANH;
val_t SX_ASIN, SX_ACOS, SX_ATAN;
val_t SX_ASINH, SX_ACOSH, SX_ATANH;
val_t SX_COT, SX_SEC, SX_CSC;
val_t SX_LIMIT;
val_t SX_SIGN;
val_t SX_LT, SX_LE, SX_GT, SX_GE;
val_t SX_APPLY;
val_t SX_LAPLACE;
val_t SX_FOURIER;

void symbolic_init(void) {
    SX_ADD       = sym_intern_cstr("+");
    SX_SUB       = sym_intern_cstr("-");
    SX_MUL       = sym_intern_cstr("*");
    SX_NCMUL     = sym_intern_cstr("nc*");
    SX_DIV       = sym_intern_cstr("/");
    SX_NEG       = sym_intern_cstr("neg");
    SX_EXPT      = sym_intern_cstr("expt");
    SX_SQRT      = sym_intern_cstr("sqrt");
    SX_SIN       = sym_intern_cstr("sin");
    SX_COS       = sym_intern_cstr("cos");
    SX_TAN       = sym_intern_cstr("tan");
    SX_EXP       = sym_intern_cstr("exp");
    SX_LOG       = sym_intern_cstr("log");
    SX_ABS       = sym_intern_cstr("abs");
    SX_INTEGRATE = sym_intern_cstr("∫");
    SX_CONJ      = sym_intern_cstr("conj");
    SX_REAL      = sym_intern_cstr("real-part");
    SX_IMAG      = sym_intern_cstr("imag-part");
    SX_FRACDIFF  = sym_intern_cstr("frac-diff");
    SX_FRACINT   = sym_intern_cstr("frac-int");
    SX_SINH      = sym_intern_cstr("sinh");
    SX_COSH      = sym_intern_cstr("cosh");
    SX_TANH      = sym_intern_cstr("tanh");
    SX_ASIN      = sym_intern_cstr("asin");
    SX_ACOS      = sym_intern_cstr("acos");
    SX_ATAN      = sym_intern_cstr("atan");
    SX_ASINH     = sym_intern_cstr("asinh");
    SX_ACOSH     = sym_intern_cstr("acosh");
    SX_ATANH     = sym_intern_cstr("atanh");
    SX_COT       = sym_intern_cstr("cot");
    SX_SEC       = sym_intern_cstr("sec");
    SX_CSC       = sym_intern_cstr("csc");
    SX_LIMIT     = sym_intern_cstr("limit");
    SX_SIGN      = sym_intern_cstr("sign");
    SX_LT        = sym_intern_cstr("<");
    SX_LE        = sym_intern_cstr("<=");
    SX_GT        = sym_intern_cstr(">");
    SX_GE        = sym_intern_cstr(">=");
    SX_APPLY     = sym_intern_cstr("apply");
    SX_LAPLACE   = sym_intern_cstr("laplace");
    SX_FOURIER   = sym_intern_cstr("fourier");
}

/* ---- Constructors ---- */

val_t sx_make_var(val_t name) {
    SymVar *v = CURRY_NEW_PINNED(SymVar);
    v->hdr.type  = T_SYMVAR;
    v->hdr.flags = 0;
    v->name      = name;
    return vptr(v);
}

val_t sx_make_var_flags(val_t name, uint32_t flags) {
    SymVar *v = CURRY_NEW_PINNED(SymVar);
    v->hdr.type  = T_SYMVAR;
    v->hdr.flags = flags;
    v->name      = name;
    return vptr(v);
}

val_t sx_make_fn(val_t name, val_t params) {
    SymFn *f = CURRY_NEW_PINNED(SymFn);
    f->hdr.type  = T_SYMFN;
    f->hdr.flags = 0;
    f->name      = name;
    f->params    = params;
    f->parent    = V_FALSE;
    f->d_param   = V_FALSE;
    return vptr(f);
}

val_t sx_fn_name(val_t fn)   { return as_symfn(fn)->name; }
val_t sx_fn_params(val_t fn) { return as_symfn(fn)->params; }

val_t sx_make_apply(val_t fn, int nargs, val_t *args) {
    /* SX_APPLY: args[0]=T_SYMFN, args[1..nargs]=applied arguments */
    int total = nargs + 1;
    val_t *all = (val_t *)gc_alloc_raw_pinned((size_t)total * sizeof(val_t));
    all[0] = fn;
    for (int i = 0; i < nargs; i++) all[i + 1] = args[i];
    return sx_make_expr(SX_APPLY, total, all);
}

val_t sx_make_expr(val_t op, int nargs, val_t *args) {
    /* Pinned (Boehm-managed, non-moving): C built-ins hold val_t locals pointing
     * to symbolic nodes across nursery allocations; pinning ensures they stay valid
     * if a minor GC fires mid-computation. */
    SymExpr *e = (SymExpr *)gc_alloc_pinned(sizeof(SymExpr) + (size_t)nargs * sizeof(val_t));
    e->hdr.type  = T_SYMEXPR;
    e->hdr.flags = 0;
    e->op        = op;
    e->nargs     = (uint32_t)nargs;
    /* zero-init (op_deps/var_deps/max_gen all 0, "never cached") is
     * automatic via GC too; explicit for clarity -- see object.h. */
    memset(e->op_deps, 0, sizeof(e->op_deps));
    memset(e->var_deps, 0, sizeof(e->var_deps));
    e->max_gen = 0;
    for (int i = 0; i < nargs; i++) e->args[i] = args[i];
    return vptr(e);
}

static val_t sx_expr1(val_t op, val_t a) {
    return sx_make_expr(op, 1, &a);
}
static val_t sx_expr2(val_t op, val_t a, val_t b) {
    val_t args[2] = {a, b}; return sx_make_expr(op, 2, args);
}

/* ---- Accessors ---- */

val_t sx_var_name(val_t v)        { return as_symvar(v)->name; }
val_t sx_expr_op(val_t e)         { return as_symexpr(e)->op; }
int   sx_expr_nargs(val_t e)      { return (int)as_symexpr(e)->nargs; }
val_t sx_expr_arg(val_t e, int i) { return as_symexpr(e)->args[i]; }

/* ---- Structural equality ---- */

bool sx_equal(val_t a, val_t b) {
    /* Same unbounded-recursion class as sx_simplify (issue #134). */
    check_c_stack_depth("symbolic");
    if (a == b) return true;
    if (vis_symvar(a) && vis_symvar(b))
        return as_symvar(a)->name == as_symvar(b)->name;
    if (vis_symexpr(a) && vis_symexpr(b)) {
        SymExpr *ea = as_symexpr(a), *eb = as_symexpr(b);
        if (ea->op != eb->op || ea->nargs != eb->nargs) return false;
        for (uint32_t i = 0; i < ea->nargs; i++)
            if (!sx_equal(ea->args[i], eb->args[i])) return false;
        return true;
    }
    if (vis_number(a) && vis_number(b)) return num_eq(a, b);
    if (vis_symfn(a) && vis_symfn(b))
        return as_symfn(a)->name == as_symfn(b)->name;
    return false;
}

/* ---- Simplification ---- */

/* forward declaration */
val_t sx_simplify(val_t expr);

static bool is_zero(val_t v) { return vis_number(v) && num_is_zero(v); }
static bool is_one(val_t v)  { return vis_number(v) && !vis_symbolic(v) && num_is_one(v); }
static bool is_two(val_t v)  { return vis_fixnum(v) && vunfix(v) == 2; }

/* Returns true if v is (expt (trig_op f) 2); sets *f_out to the argument f */
static bool is_trig_sq(val_t v, val_t trig_op, val_t *f_out) {
    if (!vis_symexpr(v)) return false;
    SymExpr *se = as_symexpr(v);
    if (se->op != SX_EXPT || se->nargs != 2) return false;
    if (!is_two(se->args[1])) return false;
    if (!vis_symexpr(se->args[0])) return false;
    SymExpr *inner = as_symexpr(se->args[0]);
    if (inner->op != trig_op || inner->nargs != 1) return false;
    if (f_out) *f_out = inner->args[0];
    return true;
}

/* Decompose an ADD term as coeff * trig_op(f)^2.
   Handles: sin²(f), cos²(f), neg(sin²(f)), neg(cos²(f)), c*sin²(f), c*cos²(f). */
static bool decompose_trig_sq(val_t term,
                               val_t *coeff_out, val_t *trig_op_out, val_t *f_out) {
    val_t f;
    if (is_trig_sq(term, SX_SIN, &f)) {
        *coeff_out = vfix(1); *trig_op_out = SX_SIN; *f_out = f; return true;
    }
    if (is_trig_sq(term, SX_COS, &f)) {
        *coeff_out = vfix(1); *trig_op_out = SX_COS; *f_out = f; return true;
    }
    if (vis_symexpr(term) && as_symexpr(term)->op == SX_NEG && as_symexpr(term)->nargs == 1) {
        val_t inner = as_symexpr(term)->args[0];
        if (is_trig_sq(inner, SX_SIN, &f)) {
            *coeff_out = vfix(-1); *trig_op_out = SX_SIN; *f_out = f; return true;
        }
        if (is_trig_sq(inner, SX_COS, &f)) {
            *coeff_out = vfix(-1); *trig_op_out = SX_COS; *f_out = f; return true;
        }
    }
    if (vis_symexpr(term) && as_symexpr(term)->op == SX_MUL && as_symexpr(term)->nargs >= 2) {
        SymExpr *se = as_symexpr(term);
        if (vis_number(se->args[0]) && !vis_symbolic(se->args[0])) {
            /* Build the rest of the product as the potential trig-sq term */
            val_t sq = se->nargs == 2 ? se->args[1]
                     : sx_make_expr(SX_MUL, (int)se->nargs - 1, se->args + 1);
            if (is_trig_sq(sq, SX_SIN, &f)) {
                *coeff_out = se->args[0]; *trig_op_out = SX_SIN; *f_out = f; return true;
            }
            if (is_trig_sq(sq, SX_COS, &f)) {
                *coeff_out = se->args[0]; *trig_op_out = SX_COS; *f_out = f; return true;
            }
        }
    }
    return false;
}


/* Issue #137: sx_simplify_impl (below) re-walks and re-simplifies its
 * ENTIRE argument tree from scratch on every call, even subexpressions
 * a prior call already fully simplified -- building an expression by
 * repeatedly wrapping an already-simplified result one level at a time
 * (e.g. `(sin e)` called N times in a loop, each call passing in the
 * previous iteration's own result) costs O(depth^2) total work, not
 * O(depth), since each of the N calls re-simplifies the whole current
 * tree. This is a genuine CPU-exhaustion DoS distinct from #134's
 * stack-depth fix -- it fires at sizes well below that guard's own
 * threshold, confirmed during #134's own testing to burn 20-30+ seconds
 * of CPU under a generous stack ulimit before the guard ever engages.
 *
 * Fixed with a generation-tagged memoization cache, originally (per
 * #137/#140) a single global counter -- simple, but #140 found it let
 * one cheap define-rule/define-algebra call interleaved per step of an
 * otherwise-cheap deep-expression build defeat memoization ENTIRELY
 * (invalidating the whole cache, not just nodes touching the operator
 * that actually changed), reintroducing the O(depth^2) DoS this cache
 * exists to close.
 *
 * Issue #195 replaces the single global counter with PER-OPERATOR and
 * PER-VARIABLE scoped generation tracking:
 *
 *   - op_generation_table / var_generation_table (below): fixed-size,
 *     open-addressed tables mirroring sx_rules.c's rtab / sx_algebra.c's
 *     atab -- slot never reassigned once claimed, rwlock guards
 *     claiming only, reads are lock-free atomic loads once a slot
 *     exists. Keyed by operator symbol / SymVar NAME symbol (not SymVar
 *     object identity -- multiple distinct SymVars can share a name and
 *     are the same logical variable elsewhere in this file, e.g.
 *     sx_equal's vis_symvar case), both permanently-interned symbols
 *     (GC_MALLOC_UNCOLLECTABLE, never moved -- see symbol.c), so unlike
 *     rtab/atab these tables need no GC scanner.
 *   - Each SymExpr node records which slots its OWN simplification
 *     actually depended on (op_deps/var_deps bitmasks, object.h) and the
 *     max generation those slots had AT CACHE TIME (max_gen). A cache
 *     hit re-checks: is the CURRENT generation of every tracked slot
 *     still <= max_gen? Bounded by table capacity, not tree depth --
 *     the key property that closes the O(depth^2) DoS, since a deep but
 *     narrow expression touches only a handful of distinct operators
 *     regardless of its depth.
 *   - Soundness (this is the fix for a gap #195's own design review
 *     caught before any code was written): a node's operator dependency
 *     must be the operator sx_simplify() actually QUERIED for that
 *     node -- se->op, read and locked in via op_gen_touch() BEFORE
 *     sx_simplify_impl runs any rule/algebra lookup for it -- never
 *     derived from the shape of whatever the lookup rewrites it to.
 *     E.g. `(- a 0)` rewrites to a `neg(a)` node; if the dependency were
 *     derived from the RESULT's own top-level op (neg) instead of the
 *     op actually queried (sub), a later `define-rule` for `-` would
 *     never invalidate this cached rewrite -- exactly the silent-
 *     staleness class #140 exists to prevent. sx_simplify implements
 *     this via a thread-local stack of per-call-frame dependency
 *     accumulators (g_sx_deps_stack, below): every nested sx_simplify()
 *     call made WHILE processing node N's own frame -- whether a plain
 *     child recursion or a rule/algebra rewrite's own recursive
 *     re-simplify -- merges its contribution into N's frame, with no
 *     change needed to sx_simplify_impl's own (large, rewrite-heavy)
 *     body: it already calls sx_simplify(), not sx_simplify_impl(),
 *     for every recursive step, including every rewrite continuation.
 *   - Structural limitation (not a bug -- cannot be closed by any
 *     design in this category, per #195's own design-validation pass):
 *     sx_rule_add/sx_algebra_define register arbitrary Scheme closures
 *     (guard_fn/action_fn/relations_fn). A closure that closes over a
 *     SymVar NOT among the expression's own args, and branches on that
 *     variable's assumption flags, creates a dependency invisible to
 *     any args-tree-based tracking scheme, this one included. Accepted,
 *     documented limitation -- do not attempt to "fix" this without a
 *     fundamentally different (non-tree-based) design.
 *   - Capacity (#195 finding 3): op_generation_table's real domain is
 *     every operator symbol ever SEEN during simplification, not just
 *     ones with a registered rule -- confirmed much larger than rtab's
 *     own domain (rtab only grows via actual sx_rule_add calls). Sized
 *     generously (256 op slots / 128 var slots) rather than made
 *     growable, with the LAST slot of each table permanently reserved
 *     as a safe overflow/catch-all: an operator or variable that can't
 *     claim a real slot (table genuinely exhausted -- implausible under
 *     256/128 slots outside a deliberately adversarial workload) is
 *     tracked via the overflow slot instead, which every mutation of
 *     that kind (op or var) bumps unconditionally -- an always-correct,
 *     if coarser, fallback for just the overflowing operator/variable,
 *     never for the whole cache. A fixed table was chosen over a
 *     growable one for simplicity: growing rtab/atab-style tables under
 *     their own rwlock is well-precedented in this codebase, but doing
 *     it for tables consulted on literally every sx_simplify call (not
 *     just rule-bearing operators) adds real complexity for a case 256/
 *     128 slots already covers comfortably.
 *
 * Generation counters keep #140's already-fixed conventions: uint64_t
 * (not _Atomic-qualified -- __atomic_load_n/compare_exchange require a
 * plain type, matching runtime.c's g_jit_arith_tainted convention),
 * skip-0-on-wraparound via a CAS retry loop (0 is reserved for "slot
 * never bumped" / max_gen==0 is reserved for "node never cached"),
 * acquire/release ordering (establishes happens-before between an
 * ordinary, non-atomic mutation like prim_assume's `hdr.flags |= flag`
 * and another actor thread observing the resulting generation bump and
 * therefore knowing to recompute -- relaxed ordering would let a
 * weakly-ordered CPU, e.g. arm64, reorder that plain store past the
 * atomic bump). See #140's original PR for why each of these choices
 * matters on its own (32-bit wraparound reached in ~3 minutes under
 * tight invalidation; a plain fetch-add-then-corrective-add left a
 * window where a concurrent load could observe an intermediate 0). */

#define SX_OP_GEN_TABLE_SIZE   256
#define SX_OP_GEN_OVERFLOW_IDX (SX_OP_GEN_TABLE_SIZE - 1)
#define SX_OP_GEN_USABLE       (SX_OP_GEN_TABLE_SIZE - 1)
#define SX_OP_MASK_WORDS       4   /* 256 bits: 255 usable slots + 1 overflow */

#define SX_VAR_GEN_TABLE_SIZE   128
#define SX_VAR_GEN_OVERFLOW_IDX (SX_VAR_GEN_TABLE_SIZE - 1)
#define SX_VAR_GEN_USABLE       (SX_VAR_GEN_TABLE_SIZE - 1)
#define SX_VAR_MASK_WORDS       2  /* 128 bits: 127 usable slots + 1 overflow */

typedef struct { val_t key; uint64_t gen; } SxGenSlot;

static SxGenSlot sx_op_gen_table[SX_OP_GEN_TABLE_SIZE];
static SxGenSlot sx_var_gen_table[SX_VAR_GEN_TABLE_SIZE];
static pthread_rwlock_t sx_op_gen_lock  = PTHREAD_RWLOCK_INITIALIZER;
static pthread_rwlock_t sx_var_gen_lock = PTHREAD_RWLOCK_INITIALIZER;
static pthread_once_t sx_gen_tables_once = PTHREAD_ONCE_INIT;

static void sx_gen_tables_init_once(void) {
    for (int i = 0; i < SX_OP_GEN_TABLE_SIZE; i++) {
        sx_op_gen_table[i].key = V_VOID;
        sx_op_gen_table[i].gen = 0;
    }
    for (int i = 0; i < SX_VAR_GEN_TABLE_SIZE; i++) {
        sx_var_gen_table[i].key = V_VOID;
        sx_var_gen_table[i].gen = 0;
    }
    /* Code-review finding: unlike every normal slot (which gets a real,
     * clock-drawn nonzero gen the MOMENT it's first claimed -- see
     * sx_op_gen_index/sx_var_gen_index's own "Issue #195 correctness fix"
     * comments), the reserved overflow slot is never "claimed" at all --
     * it's always index SX_OP_GEN_OVERFLOW_IDX/SX_VAR_GEN_OVERFLOW_IDX,
     * fixed from the start. Left at the 0 init value, a node whose entire
     * dependency set resolves only to the overflow slot (i.e., every
     * operator/variable it touches happens to have overflowed its table)
     * would get max_gen==0 the first time it's cached -- indistinguishable
     * from object.h's own "never cached" sentinel, silently defeating
     * memoization for that node specifically until the overflow slot
     * happens to be bumped by some unrelated invalidation. Benign in
     * effect (the node is just never served from cache, always
     * recomputed -- never a WRONG answer), but it contradicts this file's
     * own stated invariant for every other slot. Giving it a real,
     * hardcoded nonzero gen here closes the gap the same way a claim
     * would, without needing sx_gen_clock_next() (not yet declared at this
     * point in the file) -- 1 is guaranteed distinct from and older than
     * every value sx_gen_clock_next() itself can ever return (its first
     * call returns 2: g_sx_gen_clock starts at 1, is incremented, then
     * returned), so any later real invalidation of the overflow slot
     * still correctly compares as newer. */
    sx_op_gen_table[SX_OP_GEN_OVERFLOW_IDX].gen  = 1;
    sx_var_gen_table[SX_VAR_GEN_OVERFLOW_IDX].gen = 1;
}

/* Both independent code-review and security-review passes flagged this as a
 * genuine (if benign-in-practice) data race: a plain, unlocked int flag
 * guarding first-use initialization of tables that every actor thread reads
 * and writes, in a file otherwise meticulous about atomics/rwlocks for
 * cross-actor visibility (see g_sx_gen_clock's own comment). Every racing
 * writer stored the same values, so no divergence was ever observable on
 * common platforms -- but it's still UB under C11 and would be flagged by
 * ThreadSanitizer. pthread_once gives the same "run exactly once, visible
 * to every thread before any of them proceeds past this call" guarantee
 * `rtab_init`/`atab_init` already provide their own tables via a distinct
 * mechanism (a one-time init call from modules_init(), not first-use lazy
 * init) -- pthread_once is used here instead since this table's first use
 * can genuinely come from any actor thread, not just startup. */
static void sx_gen_tables_init(void) {
    pthread_once(&sx_gen_tables_once, sx_gen_tables_init_once);
}

/* Issue #195 correctness fix (found while validating this exact design
 * against sx_algebra_tests.scm's own pre-existing #137 regression test --
 * "a rule registered AFTER caching still fires on the cached node"): op
 * and var slots CANNOT each keep independent, self-relative generation
 * counters (e.g. each doing its own skip-0 CAS increment starting from
 * a shared initial value) if a node's single max_gen scalar is compared
 * against the CURRENT max over several DIFFERENT tracked slots. Concrete
 * failure: a node depends on op slot A (gen 1) and var slot B (gen 6,
 * already bumped several times by earlier unrelated assume!/with-
 * assumptions calls) -- max_gen recorded at cache time is max(1,6)=6.
 * Later, ONLY slot A is bumped, 1 -> 2. The revalidation check computes
 * current max(2,6)=6, which still equals the stored 6 -- a false HIT,
 * silently serving a result that predates a real, relevant rule change.
 * The stored single-scalar max is only a sound proxy for "did anything
 * tracked change" when every slot's generation values are drawn from
 * ONE shared, globally monotonic sequence -- so a slot being bumped
 * ALWAYS receives a value strictly greater than every generation number
 * issued anywhere before it, regardless of which specific slot (op or
 * var, and regardless of that slot's own prior value) receives it. This
 * restores the property the design relies on ("all counters are
 * monotonic" only actually implies what's needed here if it's the SAME
 * counter sequence backing every tracked slot). g_sx_gen_clock is that
 * shared source; sx_gen_bump draws the next value from it and stores it
 * (a plain assignment of a fresh, already-unique value, not a
 * read-modify-write on the destination slot itself -- no CAS needed
 * there). Table slots initialise to 0 (below the clock's own starting
 * value of 1), which doubles as a harmless edge case: a node whose
 * ENTIRE dependency set has never been bumped even once ends up with
 * max_gen==0, indistinguishable from "never cached" -- always a safe,
 * if slightly wasteful (one missed optimization, not a wrong answer),
 * outcome; see max_gen's own field comment in object.h. */
static uint64_t g_sx_gen_clock = 1;

static uint64_t sx_gen_clock_next(void) {
    uint64_t cur = __atomic_load_n(&g_sx_gen_clock, __ATOMIC_RELAXED);
    uint64_t next;
    do {
        next = cur + 1;
        if (next == 0) next = 1;
    } while (!__atomic_compare_exchange_n(&g_sx_gen_clock, &cur, next,
                                          0 /* not weak */, __ATOMIC_RELEASE, __ATOMIC_RELAXED));
    return next;
}

static void sx_gen_bump(uint64_t *genp) {
    __atomic_store_n(genp, sx_gen_clock_next(), __ATOMIC_RELEASE);
}

/* Find-or-claim a table slot for `key`, mirroring rtab/atab's open-
 * addressing find_chain/atab_slot pattern exactly: fast read-locked scan
 * first (the hot path -- every sx_simplify call on a SymExpr touches
 * this for its own operator), falling back to a write-locked claim only
 * the first time a given op/var is ever seen. Returns the reserved
 * overflow index if the table is genuinely full. */
static int sx_op_gen_index(val_t op) {
    unsigned h = (unsigned)((uintptr_t)op >> 3);
    int start = (int)(h % (unsigned)SX_OP_GEN_USABLE);

    pthread_rwlock_rdlock(&sx_op_gen_lock);
    for (int i = 0; i < SX_OP_GEN_USABLE; i++) {
        int idx = (start + i) % SX_OP_GEN_USABLE;
        val_t cur = sx_op_gen_table[idx].key;
        if (cur == op) { pthread_rwlock_unlock(&sx_op_gen_lock); return idx; }
        if (cur == V_VOID) break;
    }
    pthread_rwlock_unlock(&sx_op_gen_lock);

    pthread_rwlock_wrlock(&sx_op_gen_lock);
    int result = SX_OP_GEN_OVERFLOW_IDX;
    for (int i = 0; i < SX_OP_GEN_USABLE; i++) {
        int idx = (start + i) % SX_OP_GEN_USABLE;
        if (sx_op_gen_table[idx].key == op) { result = idx; break; }
        if (sx_op_gen_table[idx].key == V_VOID) {
            /* Issue #195 correctness fix: a slot's gen must become a real,
             * nonzero, clock-ordered value the MOMENT it's first claimed,
             * not stay at its 0 init placeholder until some later explicit
             * invalidation. Otherwise a node whose entire dependency set
             * (op AND every var it touches) has never been invalidated
             * even once ends up with max_gen==0 -- indistinguishable from
             * "never cached" (object.h's own sentinel), silently defeating
             * memoization for the common case of a program that never
             * calls define-rule/define-algebra/assume! at all. See
             * op_deps's field comment in object.h and this file's
             * g_sx_gen_clock comment for the full reasoning. */
            sx_op_gen_table[idx].key = op;
            sx_op_gen_table[idx].gen = sx_gen_clock_next();
            result = idx;
            break;
        }
    }
    pthread_rwlock_unlock(&sx_op_gen_lock);
    return result;
}

static int sx_var_gen_index(val_t name) {
    unsigned h = (unsigned)((uintptr_t)name >> 3);
    int start = (int)(h % (unsigned)SX_VAR_GEN_USABLE);

    pthread_rwlock_rdlock(&sx_var_gen_lock);
    for (int i = 0; i < SX_VAR_GEN_USABLE; i++) {
        int idx = (start + i) % SX_VAR_GEN_USABLE;
        val_t cur = sx_var_gen_table[idx].key;
        if (cur == name) { pthread_rwlock_unlock(&sx_var_gen_lock); return idx; }
        if (cur == V_VOID) break;
    }
    pthread_rwlock_unlock(&sx_var_gen_lock);

    pthread_rwlock_wrlock(&sx_var_gen_lock);
    int result = SX_VAR_GEN_OVERFLOW_IDX;
    for (int i = 0; i < SX_VAR_GEN_USABLE; i++) {
        int idx = (start + i) % SX_VAR_GEN_USABLE;
        if (sx_var_gen_table[idx].key == name) { result = idx; break; }
        if (sx_var_gen_table[idx].key == V_VOID) {
            /* See sx_op_gen_index's identical fix for why a freshly-
             * claimed slot needs a real clock-drawn gen immediately,
             * not the 0 init placeholder. */
            sx_var_gen_table[idx].key = name;
            sx_var_gen_table[idx].gen = sx_gen_clock_next();
            result = idx;
            break;
        }
    }
    pthread_rwlock_unlock(&sx_var_gen_lock);
    return result;
}

static inline void sx_bit_set(uint64_t *mask, int idx) {
    mask[idx >> 6] |= ((uint64_t)1 << (idx & 63));
}

/* Claims (if needed) the slot for `op`, sets its bit in `mask_out`, and
 * returns its CURRENT generation -- called by sx_simplify() BEFORE
 * sx_simplify_impl runs any rule/algebra lookup for this exact op (see
 * the soundness note in the long comment above), so the returned value
 * can never be newer than whatever state that lookup actually saw. */
static uint64_t sx_op_gen_touch(val_t op, uint64_t mask_out[SX_OP_MASK_WORDS]) {
    sx_gen_tables_init();
    int idx = sx_op_gen_index(op);
    sx_bit_set(mask_out, idx);
    return __atomic_load_n(&sx_op_gen_table[idx].gen, __ATOMIC_ACQUIRE);
}
static uint64_t sx_var_gen_touch(val_t name, uint64_t mask_out[SX_VAR_MASK_WORDS]) {
    sx_gen_tables_init();
    int idx = sx_var_gen_index(name);
    sx_bit_set(mask_out, idx);
    return __atomic_load_n(&sx_var_gen_table[idx].gen, __ATOMIC_ACQUIRE);
}

/* Cache-hit validity check: current generation of every tracked slot,
 * lock-free (slots are never reassigned once claimed, so reading .gen
 * without the rwlock is safe -- only claiming a NEW slot needs it). */
static uint64_t sx_op_gen_current_max(const uint64_t mask[SX_OP_MASK_WORDS]) {
    uint64_t m = 0;
    for (int w = 0; w < SX_OP_MASK_WORDS; w++) {
        uint64_t bits = mask[w];
        while (bits) {
            int b = __builtin_ctzll(bits);
            bits &= bits - 1;
            uint64_t g = __atomic_load_n(&sx_op_gen_table[w * 64 + b].gen, __ATOMIC_ACQUIRE);
            if (g > m) m = g;
        }
    }
    return m;
}
static uint64_t sx_var_gen_current_max(const uint64_t mask[SX_VAR_MASK_WORDS]) {
    uint64_t m = 0;
    for (int w = 0; w < SX_VAR_MASK_WORDS; w++) {
        uint64_t bits = mask[w];
        while (bits) {
            int b = __builtin_ctzll(bits);
            bits &= bits - 1;
            uint64_t g = __atomic_load_n(&sx_var_gen_table[w * 64 + b].gen, __ATOMIC_ACQUIRE);
            if (g > m) m = g;
        }
    }
    return m;
}

void sx_invalidate_simplify_cache_op(val_t op) {
    sx_gen_tables_init();
    int idx = sx_op_gen_index(op);
    sx_gen_bump(&sx_op_gen_table[idx].gen);
    if (idx != SX_OP_GEN_OVERFLOW_IDX)
        sx_gen_bump(&sx_op_gen_table[SX_OP_GEN_OVERFLOW_IDX].gen);
}

void sx_invalidate_simplify_cache_var(val_t var_name) {
    sx_gen_tables_init();
    int idx = sx_var_gen_index(var_name);
    sx_gen_bump(&sx_var_gen_table[idx].gen);
    if (idx != SX_VAR_GEN_OVERFLOW_IDX)
        sx_gen_bump(&sx_var_gen_table[SX_VAR_GEN_OVERFLOW_IDX].gen);
}

void sx_invalidate_simplify_cache(void) {
    /* True global invalidation -- every claimed slot in both tables,
     * plus both overflow slots. O(table size), not O(cache size); kept
     * for callers that don't know (or don't want to compute) exactly
     * which operator/variable changed. Nothing in this codebase calls
     * this anymore as of #195 (sx_rule_add/sx_algebra_define/
     * sx_rules_clear/assume!/with-assumptions all now use the scoped
     * variants above), but it's kept available and genuinely global,
     * not repurposed to mean something narrower, so it can never become
     * a silent-unsoundness trap for a future caller that assumes the
     * old global-invalidation contract. */
    sx_gen_tables_init();
    pthread_rwlock_wrlock(&sx_op_gen_lock);
    for (int i = 0; i < SX_OP_GEN_TABLE_SIZE; i++) sx_gen_bump(&sx_op_gen_table[i].gen);
    pthread_rwlock_unlock(&sx_op_gen_lock);
    pthread_rwlock_wrlock(&sx_var_gen_lock);
    for (int i = 0; i < SX_VAR_GEN_TABLE_SIZE; i++) sx_gen_bump(&sx_var_gen_table[i].gen);
    pthread_rwlock_unlock(&sx_var_gen_lock);
}

/* ---- Per-call-frame dependency accumulator stack (see the soundness
 * note in the long comment above) ----
 *
 * Heap-backed (not a fixed on-stack/TLS array of raw pointers), grown
 * on demand, per-thread: most actor threads never touch symbolic code
 * at all, so a lazily-allocated pointer costs nothing for them, and a
 * growable buffer avoids picking one fixed depth that's either wasteful
 * for the common case or too shallow for a legitimately deep (if
 * unusual) expression tree.
 *
 * Indexed by depth rather than holding raw pointers into this
 * function's own C stack frame is a deliberate exception-safety choice:
 * a rule's guard_fn/action_fn or an algebra's relations_fn is arbitrary
 * Scheme, free to call `error`/raise and unwind via longjmp through
 * sx_simplify's own C stack frame without running its cleanup code. If
 * the accumulator lived in THAT frame, an unwind would leave
 * g_sx_deps_depth (thread-local, so it retains its value across the
 * jump) pointing at a slot that's still perfectly valid storage --
 * because it's in this static/heap buffer, not on the collapsed C
 * stack -- but was never popped back down. The only consequence is a
 * bounded, self-limiting "leak" of unused depth accounting for the rest
 * of that thread's life (capped by SX_DEPS_STACK_HARD_MAX, at which
 * point sx_simplify falls back to not caching at all rather than ever
 * indexing out of bounds); a later, unrelated top-level call simply
 * starts pushing frames at a higher starting depth than 0, which is
 * internally self-consistent (each push/pop pair within one call still
 * nests correctly relative to ITSELF) and never corrupts another
 * thread's or another call's state, since g_sx_deps_stack is per-thread
 * and each active call only ever reads/writes the slot(s) it pushed. */
typedef struct {
    uint64_t op[SX_OP_MASK_WORDS];
    uint64_t var[SX_VAR_MASK_WORDS];
    uint64_t max_gen;
} SxDeps;

#define SX_DEPS_STACK_HARD_MAX 65536

static CURRY_THREAD_LOCAL SxDeps *g_sx_deps_stack = NULL;
static CURRY_THREAD_LOCAL int     g_sx_deps_cap   = 0;
static CURRY_THREAD_LOCAL int     g_sx_deps_depth = 0;

/* Ensures g_sx_deps_stack has room for index `depth`; returns false (and
 * leaves the stack untouched) if that would require growing past
 * SX_DEPS_STACK_HARD_MAX -- the caller's job is to fall back to the
 * always-safe "don't tag this node" path in that case. */
static bool sx_deps_stack_reserve(int depth) {
    if (depth < g_sx_deps_cap) return true;
    if (depth >= SX_DEPS_STACK_HARD_MAX) return false;
    int new_cap = g_sx_deps_cap ? g_sx_deps_cap * 2 : 64;
    if (new_cap <= depth) new_cap = depth + 1;
    if (new_cap > SX_DEPS_STACK_HARD_MAX) new_cap = SX_DEPS_STACK_HARD_MAX;
    SxDeps *grown = (SxDeps *)realloc(g_sx_deps_stack, (size_t)new_cap * sizeof(SxDeps));
    if (!grown) return false; /* OOM: safe fallback, same as hitting the hard cap */
    g_sx_deps_stack = grown;
    g_sx_deps_cap = new_cap;
    return true;
}

static void sx_deps_merge_into_current(const uint64_t op_mask[SX_OP_MASK_WORDS],
                                        const uint64_t var_mask[SX_VAR_MASK_WORDS],
                                        uint64_t max_gen) {
    if (g_sx_deps_depth <= 0) return;
    SxDeps *cur = &g_sx_deps_stack[g_sx_deps_depth - 1];
    for (int w = 0; w < SX_OP_MASK_WORDS; w++)  cur->op[w]  |= op_mask[w];
    for (int w = 0; w < SX_VAR_MASK_WORDS; w++) cur->var[w] |= var_mask[w];
    if (max_gen > cur->max_gen) cur->max_gen = max_gen;
}

static val_t sx_simplify_impl(val_t expr) {
    /* Issue #134: sx_simplify recurses into itself once per argument of
     * every nested SymExpr node (just below, and again for the tuple
     * case) with no bound at all -- a deeply nested expression tree
     * built via repeated unary/binary applications (e.g. `(sin (sin
     * (sin ... x)))` a few thousand levels deep, or any user script
     * that recursively wraps a sym-var) SIGSEGVs during construction,
     * before the value even exists for #133's printer guard to ever
     * see. Shares check_c_stack_depth (runtime.c) with eval()/ir_emit()/
     * the reader/the printer -- same real C stack, same guard. */
    check_c_stack_depth("symbolic");
    if (!vis_symexpr(expr)) {
        /* Tuple: simplify each component */
        if (vis_tuple(expr)) {
            Tuple *t = as_tuple(expr);
            val_t *simp = (val_t *)gc_alloc_raw_pinned((size_t)t->len * sizeof(val_t));
            for (uint32_t i = 0; i < t->len; i++) simp[i] = sx_simplify(t->data[i]);
            return num_make_tuple((int)t->hdr.type, t->len, simp);
        }
        return expr;
    }

    SymExpr *se = as_symexpr(expr);
    val_t op = se->op;
    int n = (int)se->nargs;

    /* Recursively simplify all arguments */
    val_t *sa = (val_t *)gc_alloc_raw_pinned((size_t)n * sizeof(val_t));
    for (int i = 0; i < n; i++) sa[i] = sx_simplify(se->args[i]);

    /* User-defined rules — consulted before built-in dispatch so that custom
       algebras (e.g. Dirac γ-matrix anti-commutation) take precedence. */
    {
        val_t user_expr   = sx_make_expr(op, n, sa);
        val_t user_result = sx_rule_try(user_expr);
        if (user_result != V_VOID) return sx_simplify(user_result);
    }

    /* Declared algebra properties for user-defined operators.
       Built-in operators (+, *, etc.) are not in the algebra table and fall
       through to the hard-coded dispatch below. */
    {
        AlgebraInfo alg_info;
        AlgebraInfo *alg = sx_algebra_lookup(op, &alg_info) ? &alg_info : NULL;
        if (alg) {
            /* Absorbing element: (op ... abs ...) → abs */
            if (alg->absorbing != V_VOID) {
                for (int i = 0; i < n; i++)
                    if (scm_equal(sa[i], alg->absorbing)) return alg->absorbing;
            }
            /* Identity elimination: remove all identity elements from args */
            if (alg->identity != V_VOID) {
                int new_n = 0;
                val_t *new_sa = (val_t *)gc_alloc_raw_pinned((size_t)n * sizeof(val_t));
                for (int i = 0; i < n; i++)
                    if (!scm_equal(sa[i], alg->identity)) new_sa[new_n++] = sa[i];
                if (new_n == 0) return alg->identity;
                if (new_n == 1) return new_sa[0];
                if (new_n < n)  return sx_simplify(sx_make_expr(op, new_n, new_sa));
            }
            /* Associative flattening: (op (op a b) c) → (op a b c) */
            if (alg->associative) {
                int total = 0;
                for (int i = 0; i < n; i++) {
                    if (vis_symexpr(sa[i]) && as_symexpr(sa[i])->op == op)
                        total += (int)as_symexpr(sa[i])->nargs;
                    else total++;
                }
                if (total > n) {
                    val_t *flat = (val_t *)gc_alloc_raw_pinned((size_t)total * sizeof(val_t));
                    int k = 0;
                    for (int i = 0; i < n; i++) {
                        if (vis_symexpr(sa[i]) && as_symexpr(sa[i])->op == op) {
                            SymExpr *sub = as_symexpr(sa[i]);
                            for (uint32_t j = 0; j < sub->nargs; j++) flat[k++] = sub->args[j];
                        } else flat[k++] = sa[i];
                    }
                    sa = flat; n = total;
                }
            }
            /* Custom relations function */
            if (alg->relations_fn != V_FALSE) {
                val_t rel_expr   = sx_make_expr(op, n, sa);
                val_t rel_result = apply_arr(alg->relations_fn, 1, &rel_expr);
                if (rel_result != V_VOID && rel_result != rel_expr)
                    return sx_simplify(rel_result);
            }
            /* No built-in dispatch for user operators — return simplified form */
            return sx_make_expr(op, n, sa);
        }
    }

    /* Flatten nested ADD or MUL into one level */
    if (op == SX_ADD || op == SX_MUL || op == SX_NCMUL) {
        /* Count total terms after flattening */
        int total = 0;
        for (int i = 0; i < n; i++) {
            if (vis_symexpr(sa[i]) && as_symexpr(sa[i])->op == op)
                total += (int)as_symexpr(sa[i])->nargs;
            else total += 1;
        }
        if (total > n) {
            val_t *flat = (val_t *)gc_alloc_raw_pinned((size_t)total * sizeof(val_t));
            int k = 0;
            for (int i = 0; i < n; i++) {
                if (vis_symexpr(sa[i]) && as_symexpr(sa[i])->op == op) {
                    SymExpr *sub = as_symexpr(sa[i]);
                    for (uint32_t j = 0; j < sub->nargs; j++) flat[k++] = sub->args[j];
                } else flat[k++] = sa[i];
            }
            sa = flat; n = total;
        }
    }

    /* Count purely numeric args */
    int num_count = 0;
    for (int i = 0; i < n; i++) if (vis_number(sa[i])) num_count++;

    /* ---- ADD: fold numerics and strip zeros ---- */
    if (op == SX_ADD) {
        if (num_count == n) {
            val_t acc = vfix(0);
            for (int i = 0; i < n; i++) acc = num_add(acc, sa[i]);
            return acc;
        }
        /* Remove zero terms, fold remaining numerics */
        val_t num_acc = vfix(0);
        int nsym = 0;
        for (int i = 0; i < n; i++) if (!vis_number(sa[i])) nsym++;
        val_t *syms = (val_t *)gc_alloc_raw_pinned((size_t)nsym * sizeof(val_t));
        int j = 0;
        for (int i = 0; i < n; i++) {
            if (vis_number(sa[i])) num_acc = num_add(num_acc, sa[i]);
            else syms[j++] = sa[i];
        }
        if (nsym == 0) return num_acc;

        /* Like-term collection: combine terms with equal bases.
           Each symbolic term is decomposed as coeff * base where base is the
           "structural" part (a sym-var, an nc* product, or other expr).
           Equal bases accumulate their coefficients; coeff=0 terms are dropped. */
        {
            /* Extract (coeff, base) pairs.  Convention:
               - plain sym-var or sym-expr (not neg, not coeff*base): coeff=1, base=self
               - neg(e):                                               coeff=-1, base=e
               - (* n e) or (nc* n e) where n is numeric, e is non-numeric: coeff=n, base=e */
            val_t *bases  = (val_t *)gc_alloc_raw_pinned((size_t)nsym * sizeof(val_t));
            val_t *coeffs = (val_t *)gc_alloc_raw_pinned((size_t)nsym * sizeof(val_t));
            for (int i = 0; i < nsym; i++) {
                val_t s = syms[i];
                bases[i] = s; coeffs[i] = vfix(1);
                if (vis_symexpr(s)) {
                    SymExpr *se = as_symexpr(s);
                    if (se->op == SX_NEG && se->nargs == 1) {
                        bases[i] = se->args[0]; coeffs[i] = vfix(-1);
                    } else if ((se->op == SX_MUL || se->op == SX_NCMUL) && se->nargs >= 2
                               && vis_number(se->args[0]) && !vis_symbolic(se->args[0])) {
                        coeffs[i] = se->args[0];
                        bases[i] = se->nargs == 2 ? se->args[1]
                                 : sx_make_expr(se->op, se->nargs - 1, se->args + 1);
                    }
                }
            }
            bool any_combined = false;
            for (int i = 0; i < nsym; i++) {
                if (is_zero(coeffs[i])) continue;
                for (int k2 = i + 1; k2 < nsym; k2++) {
                    if (is_zero(coeffs[k2])) continue;
                    if (sx_equal(bases[i], bases[k2])) {
                        coeffs[i] = num_add(coeffs[i], coeffs[k2]);
                        coeffs[k2] = vfix(0);
                        any_combined = true;
                    }
                }
            }
            if (any_combined) {
                int new_nsym = 0;
                for (int i = 0; i < nsym; i++) {
                    if (is_zero(coeffs[i])) continue;
                    if (is_one(coeffs[i])) syms[new_nsym++] = bases[i];
                    else syms[new_nsym++] = sx_mul(coeffs[i], bases[i]);
                }
                nsym = new_nsym;
                if (nsym == 0) return num_acc;
            }
        }

        /* ---- Pythagorean: c*sin²(f) + c*cos²(f) → c ---- */
        {
            bool reduced = true;
            while (reduced && nsym >= 2) {
                reduced = false;
                for (int i = 0; i < nsym && !reduced; i++) {
                    val_t ci, op_i, fi;
                    if (!decompose_trig_sq(syms[i], &ci, &op_i, &fi)) continue;
                    val_t match_op = (op_i == SX_SIN) ? SX_COS : SX_SIN;
                    for (int k2 = i + 1; k2 < nsym; k2++) {
                        val_t ck, op_k, fk;
                        if (!decompose_trig_sq(syms[k2], &ck, &op_k, &fk)) continue;
                        if (op_k != match_op) continue;
                        if (!sx_equal(fi, fk)) continue;
                        if (!num_eq(ci, ck)) continue;
                        /* c*sin²(f) + c*cos²(f) → c */
                        num_acc = num_add(num_acc, ci);
                        size_t new_n = (nsym >= 2) ? (size_t)(nsym - 2) : 0;
                        val_t *new_syms = new_n ? (val_t *)gc_alloc_raw_pinned(new_n * sizeof(val_t))
                                                : (val_t *)gc_alloc_raw_pinned(sizeof(val_t));
                        int m = 0;
                        for (int j = 0; j < nsym; j++)
                            if (j != i && j != k2) new_syms[m++] = syms[j];
                        syms = new_syms; nsym -= 2;
                        reduced = true;
                        break;
                    }
                }
            }
            if (nsym == 0) return num_acc;
        }

        if (is_zero(num_acc)) {
            if (nsym == 1) return syms[0];
            return sx_make_expr(SX_ADD, nsym, syms);
        }
        /* prepend numeric sum */
        val_t *all = (val_t *)gc_alloc_raw_pinned((size_t)(nsym + 1) * sizeof(val_t));
        all[0] = num_acc;
        for (int i = 0; i < nsym; i++) all[i+1] = syms[i];
        if (nsym + 1 == 1) return all[0];
        return sx_make_expr(SX_ADD, nsym + 1, all);
    }

    /* ---- SUB ---- */
    if (op == SX_SUB && n == 2) {
        val_t a = sa[0], b = sa[1];
        if (num_count == 2) return num_sub(a, b);
        if (is_zero(b)) return a;
        if (is_zero(a)) return sx_neg(b);
        if (sx_equal(a, b)) return vfix(0);
    }

    /* ---- NEG ---- */
    if (op == SX_NEG && n == 1) {
        val_t a = sa[0];
        if (vis_number(a)) return num_neg(a);
        if (vis_symexpr(a) && as_symexpr(a)->op == SX_NEG)
            return as_symexpr(a)->args[0];
        /* neg(c * rest) → (-c) * rest — fold into leading numeric */
        if (vis_symexpr(a) && as_symexpr(a)->op == SX_MUL
            && as_symexpr(a)->nargs >= 2 && vis_number(as_symexpr(a)->args[0])
            && !vis_symbolic(as_symexpr(a)->args[0])) {
            SymExpr *m = as_symexpr(a);
            val_t neg_c = num_neg(m->args[0]);
            val_t *na = (val_t *)gc_alloc_raw_pinned((size_t)m->nargs * sizeof(val_t));
            na[0] = neg_c;
            for (uint32_t k = 1; k < m->nargs; k++) na[k] = m->args[k];
            return sx_simplify(sx_make_expr(SX_MUL, (int)m->nargs, na));
        }
    }

    /* ---- MUL: fold numerics, strip ones, detect zero ---- */
    if (op == SX_MUL) {
        if (num_count == n) {
            val_t acc = vfix(1);
            for (int i = 0; i < n; i++) acc = num_mul(acc, sa[i]);
            return acc;
        }
        /* Distribute over tuple: scalar * tuple → component-wise product */
        {
            int ti = -1;
            for (int i = 0; i < n; i++) if (vis_tuple(sa[i])) { ti = i; break; }
            if (ti >= 0) {
                Tuple *t = as_tuple(sa[ti]);
                val_t buf[256]; uint32_t tlen = t->len < 256 ? t->len : 256;
                val_t scalar_acc = vfix(1);
                for (int i = 0; i < n; i++)
                    if (i != ti) scalar_acc = sx_simplify(sx_expr2(SX_MUL, scalar_acc, sa[i]));
                for (uint32_t k = 0; k < tlen; k++)
                    buf[k] = sx_simplify(sx_expr2(SX_MUL, scalar_acc, t->data[k]));
                return num_make_tuple((int)t->hdr.type, tlen, buf);
            }
        }
        /* Any zero factor? */
        for (int i = 0; i < n; i++) if (is_zero(sa[i])) return vfix(0);
        /* Fold numeric factors and strip ones; unwrap NEG(X) as -1*X */
        val_t coeff = vfix(1);
        int nsym = 0;
        for (int i = 0; i < n; i++) if (!vis_number(sa[i])) nsym++;
        val_t *nsyms = (val_t *)gc_alloc_raw_pinned((size_t)nsym * sizeof(val_t));
        int j = 0;
        for (int i = 0; i < n; i++) {
            if (vis_number(sa[i])) {
                coeff = num_mul(coeff, sa[i]);
            } else if (vis_symexpr(sa[i]) && as_symexpr(sa[i])->op == SX_NEG
                       && as_symexpr(sa[i])->nargs == 1
                       && !vis_number(as_symexpr(sa[i])->args[0])) {
                /* NEG(sym) → fold -1 into coefficient, keep inner sym */
                coeff = num_mul(coeff, vfix(-1));
                nsyms[j++] = as_symexpr(sa[i])->args[0];
            } else {
                nsyms[j++] = sa[i];
            }
        }
        if (is_zero(coeff)) return vfix(0);

        /* ---- Collect repeated factors: a*a→a², a²*a³→a⁵ ---- */
        if (nsym >= 2) {
            val_t *bases = (val_t *)gc_alloc_raw_pinned((size_t)nsym * sizeof(val_t));
            val_t *exps  = (val_t *)gc_alloc_raw_pinned((size_t)nsym * sizeof(val_t));
            for (int i = 0; i < nsym; i++) {
                val_t s = nsyms[i];
                if (vis_symexpr(s) && as_symexpr(s)->op == SX_EXPT
                    && as_symexpr(s)->nargs == 2) {
                    bases[i] = as_symexpr(s)->args[0];
                    exps[i]  = as_symexpr(s)->args[1];
                } else {
                    bases[i] = s;
                    exps[i]  = vfix(1);
                }
            }
            bool any_merged = false;
            for (int i = 0; i < nsym; i++) {
                if (vis_false(bases[i])) continue;
                for (int k = i + 1; k < nsym; k++) {
                    if (vis_false(bases[k])) continue;
                    if (!sx_equal(bases[i], bases[k])) continue;
                    exps[i]  = sx_add(exps[i], exps[k]);
                    bases[k] = V_FALSE;
                    any_merged = true;
                }
            }
            if (any_merged) {
                int new_n = 0;
                for (int i = 0; i < nsym; i++) {
                    if (vis_false(bases[i])) continue;
                    val_t e = exps[i];
                    if (is_zero(e)) continue;            /* base^0 = 1 */
                    if (is_one(e)) nsyms[new_n++] = bases[i];
                    else nsyms[new_n++] = sx_simplify(sx_expt(bases[i], e));
                }
                nsym = new_n;
                if (nsym == 0) return coeff;
            }
        }

        /* ---- Canonical factor order: sym-vars before exprs, lex within ----
         * This ensures a*b and b*a produce the same node, enabling ADD
         * like-term detection to work across different creation orders. */
        if (nsym >= 2) {
            /* Compute a string sort-key for each factor */
            char **keys = (char **)gc_alloc_raw_pinned((size_t)nsym * sizeof(char *));
            for (int i = 0; i < nsym; i++) {
                char kbuf[128];
                val_t v = nsyms[i];
                /* Unwrap expt for sorting by base */
                val_t base = v;
                if (vis_symexpr(v) && as_symexpr(v)->op == SX_EXPT
                    && as_symexpr(v)->nargs == 2) base = as_symexpr(v)->args[0];
                /* Assign sort prefix: 0=symvar, 1=symfn-apply, 2=other */
                if (vis_symvar(base)) {
                    Symbol *s = as_sym(as_symvar(base)->name);
                    snprintf(kbuf, sizeof(kbuf), "0:%.*s", (int)s->len, s->data);
                } else if (vis_symexpr(base) && as_symexpr(base)->op == SX_APPLY
                           && as_symexpr(base)->nargs >= 1
                           && vis_symfn(as_symexpr(base)->args[0])) {
                    Symbol *fn = as_sym(as_symfn(as_symexpr(base)->args[0])->name);
                    snprintf(kbuf, sizeof(kbuf), "1:%.*s", (int)fn->len, fn->data);
                } else if (vis_symexpr(base)) {
                    Symbol *op = as_sym(as_symexpr(base)->op);
                    snprintf(kbuf, sizeof(kbuf), "2:%.*s", (int)op->len, op->data);
                } else {
                    snprintf(kbuf, sizeof(kbuf), "3:");
                }
                keys[i] = gc_alloc_raw_pinned_atomic(strlen(kbuf) + 1);
                memcpy(keys[i], kbuf, strlen(kbuf) + 1);
            }
            /* Insertion sort (nsym is small in practice) */
            for (int i = 1; i < nsym; i++) {
                val_t tv = nsyms[i]; char *tk = keys[i]; int k = i;
                while (k > 0 && strcmp(keys[k-1], tk) > 0) {
                    nsyms[k] = nsyms[k-1]; keys[k] = keys[k-1]; k--;
                }
                nsyms[k] = tv; keys[k] = tk;
            }
        }

        if (is_one(coeff)) {
            if (nsym == 1) return nsyms[0];
            /* (a/b) * c → (a*c)/b */
            if (nsym == 2) {
                for (int i = 0; i < 2; i++) {
                    int jj = 1 - i;
                    if (!vis_symexpr(nsyms[i]) || as_symexpr(nsyms[i])->op != SX_DIV ||
                        as_symexpr(nsyms[i])->nargs != 2) continue;
                    val_t dn = as_symexpr(nsyms[i])->args[0];
                    val_t dd = as_symexpr(nsyms[i])->args[1];
                    val_t new_num = sx_mul(dn, nsyms[jj]);
                    return sx_simplify(sx_div(new_num, dd));
                }
            }
            return sx_make_expr(SX_MUL, nsym, nsyms);
        }
        /* coeff == -1: return neg (also detect quaternion -1+0i+0j+0k) */
        if (vis_number(coeff) && !vis_complex(coeff) && num_eq(coeff, vfix(-1))) {
            val_t inner = nsym == 1 ? nsyms[0] : sx_make_expr(SX_MUL, nsym, nsyms);
            return sx_neg(inner);
        }
        val_t *all = (val_t *)gc_alloc_raw_pinned((size_t)(nsym + 1) * sizeof(val_t));
        all[0] = coeff;
        for (int i = 0; i < nsym; i++) all[i+1] = nsyms[i];
        if (nsym + 1 == 1) return all[0];
        return sx_make_expr(SX_MUL, nsym + 1, all);
    }

    /* ---- DIV ---- */
    if (op == SX_DIV && n == 2) {
        val_t a = sa[0], b = sa[1];
        if (num_count == 2) {
            if (num_is_zero(b)) {
                /* Float zero: let IEEE 754 produce ±inf (needed by limit/series) */
                if (vis_flonum(b)) return num_div(a, b);
                /* Exact zero denominator */
                if (!num_is_zero(a))
                    scm_raise(V_FALSE, "/: division by zero");
                /* 0/0: return unevaluated to let limit/series handle it */
                return sx_make_expr(SX_DIV, 2, sa);
            } else {
                return num_div(a, b);
            }
        }
        if (is_zero(a)) return vfix(0);
        if (is_one(b)) return a;
        if (sx_equal(a, b)) return vfix(1);  /* f/f = 1 */
        /* (/ (* c . syms) d) → fold numeric factors: (* (c/d) . syms) */
        if (vis_number(b) && vis_symexpr(a) && as_symexpr(a)->op == SX_MUL) {
            SymExpr *mul = as_symexpr(a);
            int mn = (int)mul->nargs, mnum = 0;
            for (int i = 0; i < mn; i++) if (vis_number(mul->args[i])) mnum++;
            if (mnum > 0) {
                val_t coeff = vfix(1);
                int msym = mn - mnum;
                val_t *msyms = (val_t *)gc_alloc_raw_pinned((size_t)msym * sizeof(val_t));
                int j = 0;
                for (int i = 0; i < mn; i++) {
                    if (vis_number(mul->args[i])) coeff = num_mul(coeff, mul->args[i]);
                    else msyms[j++] = mul->args[i];
                }
                val_t nc = num_div(coeff, b);
                if (msym == 0) return nc;
                if (is_one(nc)) return msym == 1 ? msyms[0] : sx_make_expr(SX_MUL, msym, msyms);
                val_t *all = (val_t *)gc_alloc_raw_pinned((size_t)(msym + 1) * sizeof(val_t));
                all[0] = nc;
                for (int i = 0; i < msym; i++) all[i+1] = msyms[i];
                return sx_make_expr(SX_MUL, msym + 1, all);
            }
        }
        /* (/ (* c1 . syms_n) (* c2 . syms_d)) — cancel leading numeric coefficients */
        if (vis_symexpr(b) && as_symexpr(b)->op == SX_MUL) {
            SymExpr *dmul = as_symexpr(b);
            /* Extract numeric coefficient from denominator */
            val_t dcoeff = vfix(1); bool d_has_num = false;
            int dn = (int)dmul->nargs, dsym = 0;
            for (int i = 0; i < dn; i++) if (vis_number(dmul->args[i])) { d_has_num = true; break; }
            if (d_has_num) {
                val_t *dsyms = (val_t *)gc_alloc_raw_pinned((size_t)dn * sizeof(val_t));
                for (int i = 0; i < dn; i++) {
                    if (vis_number(dmul->args[i])) dcoeff = num_mul(dcoeff, dmul->args[i]);
                    else dsyms[dsym++] = dmul->args[i];
                }
                /* Extract numeric coefficient from numerator */
                val_t ncoeff = vfix(1); bool n_has_num = false;
                val_t new_num = a;
                if (vis_symexpr(a) && as_symexpr(a)->op == SX_MUL) {
                    SymExpr *nmul = as_symexpr(a);
                    int nn = (int)nmul->nargs, nsym = 0;
                    for (int i = 0; i < nn; i++) if (vis_number(nmul->args[i])) { n_has_num = true; break; }
                    if (n_has_num) {
                        val_t *nsyms = (val_t *)gc_alloc_raw_pinned((size_t)nn * sizeof(val_t));
                        for (int i = 0; i < nn; i++) {
                            if (vis_number(nmul->args[i])) ncoeff = num_mul(ncoeff, nmul->args[i]);
                            else nsyms[nsym++] = nmul->args[i];
                        }
                        val_t nc = num_div(ncoeff, dcoeff);
                        val_t new_den = dsym == 0 ? vfix(1) :
                                        (dsym == 1 ? dsyms[0] : sx_make_expr(SX_MUL, dsym, dsyms));
                        if (nsym == 0) {
                            new_num = nc;
                        } else if (nsym == 1 && is_one(nc)) {
                            new_num = nsyms[0];
                        } else {
                            val_t *all = gc_alloc_raw_pinned((size_t)(nsym + (is_one(nc)?0:1)) * sizeof(val_t));
                            int k = 0;
                            if (!is_one(nc)) all[k++] = nc;
                            for (int i = 0; i < nsym; i++) all[k++] = nsyms[i];
                            new_num = sx_make_expr(SX_MUL, k, all);
                        }
                        if (is_one(new_den)) return sx_simplify(new_num);
                        return sx_simplify(sx_expr2(SX_DIV, new_num, new_den));
                    }
                } else if (vis_number(a)) {
                    /* numerator is a plain number */
                    val_t nc = num_div(a, dcoeff);
                    val_t new_den = dsym == 0 ? vfix(1) :
                                    (dsym == 1 ? dsyms[0] : sx_make_expr(SX_MUL, dsym, dsyms));
                    if (is_one(new_den)) return nc;
                    return sx_simplify(sx_expr2(SX_DIV, nc, new_den));
                }
            }
        }
    }
    /* ---- NCMUL: ordered (non-commutative) product ---- */
    if (op == SX_NCMUL) {
        if (n == 1) return sa[0];
        /* All concrete: fold left-to-right */
        if (num_count == n) {
            val_t acc = sa[0];
            for (int i = 1; i < n; i++) acc = num_mul(acc, sa[i]);
            return acc;
        }
        /* Separate real scalars (commute freely with quaternions) from ordered
           non-commutative factors.  Adjacent NC concretes are folded in place. */
        val_t scalar = vfix(1); bool has_scalar = false;
        val_t *nc = (val_t *)gc_alloc_raw_pinned((size_t)n * sizeof(val_t)); int nc_n = 0;
        for (int i = 0; i < n; i++) {
            val_t v = sa[i];
            if (is_zero(v)) return vfix(0);
            if (vis_fixnum(v) || vis_flonum(v) || vis_bignum(v) || vis_rational(v)) {
                scalar = has_scalar ? num_mul(scalar, v) : v; has_scalar = true;
            } else if (vis_number(v)) {
                /* Quaternion/complex/octonion with zero imaginary parts is a real
                   scalar and commutes freely; otherwise maintain order. */
                bool is_real_embed = false;
                if (vis_quat(v)) {
                    Quaternion *q = as_quat(v);
                    is_real_embed = (q->b == 0.0 && q->c == 0.0 && q->d == 0.0);
                }
                if (is_real_embed) {
                    val_t rv = num_make_float(as_quat(v)->a);
                    scalar = has_scalar ? num_mul(scalar, rv) : rv; has_scalar = true;
                } else {
                    /* NC concrete: fold with adjacent NC concrete */
                    if (nc_n > 0 && vis_number(nc[nc_n-1])) nc[nc_n-1] = num_mul(nc[nc_n-1], v);
                    else nc[nc_n++] = v;
                }
            } else {
                nc[nc_n++] = v;
            }
        }
        /* scalar == -1 with NC factors: emit (neg (nc* ...)) */
        if (has_scalar && num_eq(scalar, vfix(-1)) && nc_n > 0) {
            val_t inner = nc_n == 1 ? nc[0] : sx_make_expr(SX_NCMUL, nc_n, nc);
            return sx_neg(inner);
        }
        /* Rebuild: leading real scalar then ordered NC factors */
        val_t *res = (val_t *)gc_alloc_raw_pinned((size_t)(nc_n + 1) * sizeof(val_t)); int rn = 0;
        if (has_scalar && !is_one(scalar)) res[rn++] = scalar;
        for (int i = 0; i < nc_n; i++) if (!is_one(nc[i])) res[rn++] = nc[i];
        if (rn == 0) return vfix(1);
        if (rn == 1) return res[0];
        return sx_make_expr(SX_NCMUL, rn, res);
    }

    /* ---- EXPT ---- */
    if (op == SX_EXPT && n == 2) {
        val_t base = sa[0], exp = sa[1];
        if (num_count == 2) return num_expt(base, exp);
        if (is_zero(exp)) return vfix(1);
        if (is_one(exp)) return base;
        if (vis_number(base) && num_is_zero(base)) return vfix(0);
        if (vis_number(base) && is_one(base)) return vfix(1);
    }

    /* ---- SQRT ---- */
    if (op == SX_SQRT && n == 1 && vis_number(sa[0]))
        return num_sqrt(sa[0]);
    /* sqrt(x^2) — simplify with assumption knowledge */
    if (op == SX_SQRT && n == 1 && vis_symexpr(sa[0])) {
        SymExpr *inner = as_symexpr(sa[0]);
        if (inner->op == SX_EXPT && inner->nargs == 2 && is_two(inner->args[1])) {
            val_t base = inner->args[0];
            if (sym_is_positive(base)) return base;         /* sqrt(x^2) = x  (x > 0) */
            return sx_simplify(sx_expr1(SX_ABS, base));     /* sqrt(x^2) = |x| */
        }
    }

    /* ---- Transcendentals on constants ---- */
    if (n == 1 && num_count == 1) {
        if (op == SX_SIN)  return num_sin(sa[0]);
        if (op == SX_COS)  return num_cos(sa[0]);
        if (op == SX_TAN)  return num_tan(sa[0]);
        if (op == SX_EXP)  return num_exp(sa[0]);
        if (op == SX_LOG)  return num_log(sa[0]);
        if (op == SX_ABS)  return num_abs(sa[0]);
        if (op == SX_SINH)  return num_sinh(sa[0]);
        if (op == SX_COSH)  return num_cosh(sa[0]);
        if (op == SX_TANH)  return num_tanh(sa[0]);
        if (op == SX_ASIN)  return num_asin(sa[0]);
        if (op == SX_ACOS)  return num_acos(sa[0]);
        if (op == SX_ATAN)  return num_atan(sa[0]);
        if (op == SX_ASINH) return num_asinh(sa[0]);
        if (op == SX_ACOSH) return num_acosh(sa[0]);
        if (op == SX_ATANH) return num_atanh(sa[0]);
        if (op == SX_COT)   return num_cot(sa[0]);
        if (op == SX_SEC)   return num_sec(sa[0]);
        if (op == SX_CSC)   return num_csc(sa[0]);
        if (op == SX_SIGN) {
            if (num_is_zero(sa[0]))     return vfix(0);
            if (num_is_negative(sa[0])) return vfix(-1);
            return vfix(1);
        }
    }

    /* ---- ABS — assumption-based simplification ---- */
    if (op == SX_ABS && n == 1) {
        val_t a = sa[0];
        if (sym_is_positive(a)) return a;              /* |x| = x  (x > 0) */
        if (sym_is_negative(a)) return sx_neg(a);      /* |x| = -x  (x < 0) */
        /* |(-x)| = |x| */
        if (vis_symexpr(a) && as_symexpr(a)->op == SX_NEG && as_symexpr(a)->nargs == 1)
            return sx_simplify(sx_expr1(SX_ABS, as_symexpr(a)->args[0]));
    }

    /* ---- LOG — pull exponent when base is positive ---- */
    if (op == SX_LOG && n == 1 && vis_symexpr(sa[0])) {
        SymExpr *inner = as_symexpr(sa[0]);
        if (inner->op == SX_EXPT && inner->nargs == 2 && sym_is_positive(inner->args[0]))
            return sx_simplify(sx_mul(inner->args[1], sx_log(inner->args[0]))); /* log(x^n) = n*log(x) */
    }

    /* ---- SIGN — assumption-based simplification ---- */
    if (op == SX_SIGN && n == 1) {
        val_t a = sa[0];
        if (sym_is_positive(a)) return vfix(1);
        if (sym_is_negative(a)) return vfix(-1);
    }

    /* ---- Comparisons (<, <=, >, >=) — decide where possible, else stay
     * symbolic. See docs/reference/symbolic.md "Symbolic inequalities"
     * for the exact scope: no general expression-level sign inference,
     * no bound assumptions beyond sign. Motivated by
     * https://fredrikj.net/blog/2022/04/things-i-would-like-to-see-in-a-computer-algebra-system/
     * ("Analysis-oriented CASes are generally good at manipulating
     * equalities and limits, but strangely poor at manipulating
     * inequalities."). ---- */
    if ((op == SX_LT || op == SX_LE || op == SX_GT || op == SX_GE) && n == 2) {
        val_t a = sa[0], b = sa[1];
        if (num_count == 2) {
            bool r = (op == SX_LT) ? num_lt(a, b) : (op == SX_LE) ? num_le(a, b)
                    : (op == SX_GT) ? num_gt(a, b) : num_ge(a, b);
            return vbool(r);
        }
        /* Reflexive: identical expressions on both sides. */
        if (sx_equal(a, b)) return vbool(op == SX_LE || op == SX_GE);
        /* One side a sign-flagged sym-var, other side a plain number. */
        if (sym_is_positive(a) && vis_number(b) && !num_is_positive(b))
            return vbool(op == SX_GT || op == SX_GE);
        if (sym_is_negative(a) && vis_number(b) && !num_is_negative(b))
            return vbool(op == SX_LT || op == SX_LE);
        if (sym_is_positive(b) && vis_number(a) && !num_is_positive(a))
            return vbool(op == SX_LT || op == SX_LE);
        if (sym_is_negative(b) && vis_number(a) && !num_is_negative(a))
            return vbool(op == SX_GT || op == SX_GE);
    }

    /* ---- CONJ ---- */
    if (op == SX_CONJ && n == 1) {
        val_t a = sa[0];
        if (vis_number(a)) return num_conjugate(a);
        if (vis_symexpr(a)) {
            val_t iop = as_symexpr(a)->op;
            /* conj(conj(f)) = f */
            if (iop == SX_CONJ) return as_symexpr(a)->args[0];
            /* conj(real(f)) = real(f)  — real-part is always real */
            if (iop == SX_REAL) return a;
            /* conj(imag(f)) = imag(f)  — imag-part is always real */
            if (iop == SX_IMAG) return a;
        }
    }

    /* ---- REAL ---- */
    if (op == SX_REAL && n == 1) {
        val_t a = sa[0];
        if (vis_number(a)) return num_real_part(a);
        if (vis_symexpr(a)) {
            val_t iop = as_symexpr(a)->op;
            /* real(conj(f)) = real(f) */
            if (iop == SX_CONJ) return sx_real(as_symexpr(a)->args[0]);
            /* real(real(f)) = real(f) */
            if (iop == SX_REAL) return a;
            /* real(imag(f)) = imag(f)  — imag-part is real */
            if (iop == SX_IMAG) return a;
        }
    }

    /* ---- IMAG ---- */
    if (op == SX_IMAG && n == 1) {
        val_t a = sa[0];
        if (vis_number(a)) return num_imag_part(a);
        if (vis_symexpr(a)) {
            val_t iop = as_symexpr(a)->op;
            /* imag(conj(f)) = -imag(f) */
            if (iop == SX_CONJ) return sx_neg(sx_imag(as_symexpr(a)->args[0]));
            /* imag(real(f)) = 0 */
            if (iop == SX_REAL) return vfix(0);
            /* imag(imag(f)) = 0 */
            if (iop == SX_IMAG) return vfix(0);
        }
    }

    return sx_make_expr(op, n, sa);
}

/* Public entry point: the memoization fast-path for issue #137, scoped
 * per-operator/per-variable as of #195 (see the long comment above
 * sx_simplify_impl for the full rationale). Every recursive call INSIDE
 * sx_simplify_impl's own body calls this function (not
 * sx_simplify_impl directly) -- both plain child recursion and every
 * rule/algebra rewrite's own re-simplify -- so a subexpression already
 * cached and still valid is returned unchanged with no further
 * recursion at any depth, not just at the top level, and every such
 * call correctly contributes its own tracked dependencies to whichever
 * node's frame is currently being computed (see
 * sx_deps_merge_into_current / g_sx_deps_stack above). */
val_t sx_simplify(val_t expr) {
    if (vis_symvar(expr)) {
        /* Bare variables are always already "simplified"; still record
         * a dependency on this var's own name into whatever frame is
         * currently active, since the ENCLOSING node's own dispatch
         * (sym_is_positive/sym_is_negative etc., a few lines further
         * into sx_simplify_impl) may branch on its assumption flags. */
        uint64_t var_mask[SX_VAR_MASK_WORDS] = {0};
        uint64_t g = sx_var_gen_touch(as_symvar(expr)->name, var_mask);
        static const uint64_t no_op_bits[SX_OP_MASK_WORDS] = {0};
        sx_deps_merge_into_current(no_op_bits, var_mask, g);
        return expr;
    }
    if (!vis_symexpr(expr)) {
        if (vis_tuple(expr)) return sx_simplify_impl(expr);
        return expr; /* numbers, etc: nothing to track */
    }

    SymExpr *se0 = as_symexpr(expr);

    /* Cache-hit check: is every slot this node's OWN prior computation
     * depended on still at (or below) the generation recorded then?
     *
     * A SymExpr node returned unchanged by a rewrite elsewhere (e.g.
     * `(- a 0)` simplifying straight to the pre-existing node `a`) can
     * be a SHARED object multiple concurrent sx_simplify() calls (on
     * different actor threads, or nested within the same thread) tag at
     * once -- see the cache-store side below. max_gen is read with
     * ACQUIRE and op_deps/var_deps with RELAXED, in that order: the
     * store side always publishes mask updates (RELAXED fetch_or)
     * strictly before its own max_gen update (RELEASE CAS), so an
     * ACQUIRE load of max_gen that observes a given bump makes every
     * mask bit set before that bump visible here too (release-sequence
     * happens-before), without needing every individual word access to
     * be a full acquire itself. */
    uint64_t stored_max = __atomic_load_n(&se0->max_gen, __ATOMIC_ACQUIRE);
    if (stored_max != 0) {
        uint64_t op_mask[SX_OP_MASK_WORDS], var_mask[SX_VAR_MASK_WORDS];
        for (int w = 0; w < SX_OP_MASK_WORDS; w++)
            op_mask[w] = __atomic_load_n(&se0->op_deps[w], __ATOMIC_RELAXED);
        for (int w = 0; w < SX_VAR_MASK_WORDS; w++)
            var_mask[w] = __atomic_load_n(&se0->var_deps[w], __ATOMIC_RELAXED);
        uint64_t cur_max = sx_op_gen_current_max(op_mask);
        uint64_t var_max  = sx_var_gen_current_max(var_mask);
        if (var_max > cur_max) cur_max = var_max;
        if (cur_max <= stored_max) {
            /* HIT: this node's own already-recorded deps become part of
             * whatever the caller's frame is (if any) -- the caller's
             * own eventual result transitively depends on everything
             * this cached subtree depends on. */
            sx_deps_merge_into_current(op_mask, var_mask, stored_max);
            return expr;
        }
    }

    /* MISS (or never cached): push a fresh frame for THIS node, seeded
     * with the operator sx_simplify_impl is ABOUT to query (soundness:
     * read before, never derived from the rewritten result's shape). */
    int depth = g_sx_deps_depth;
    if (!sx_deps_stack_reserve(depth)) {
        /* Capacity fallback (see g_sx_deps_stack's comment): skip
         * precise tracking for this node. Nested sx_simplify() calls
         * made while computing it still merge into whatever frame WAS
         * active (if any), which is conservative-safe; this node itself
         * is simply never tagged/cached (max_gen stays 0 on the fresh
         * node sx_make_expr allocates), so it's always recomputed. */
        return sx_simplify_impl(expr);
    }
    {
        SxDeps *frame0 = &g_sx_deps_stack[depth];
        memset(frame0, 0, sizeof *frame0);
        uint64_t g = sx_op_gen_touch(se0->op, frame0->op);
        frame0->max_gen = g;
    }
    g_sx_deps_depth = depth + 1;

    val_t result = sx_simplify_impl(expr);

    g_sx_deps_depth = depth; /* pop (frame's contents are still valid) */

    /* Re-derive the frame pointer AFTER sx_simplify_impl returns, not
     * before: a nested sx_simplify() call made WHILE computing `expr`
     * (any child, or a rule/algebra rewrite's own recursive re-simplify)
     * can itself need a deeper frame than g_sx_deps_stack currently has
     * room for, triggering sx_deps_stack_reserve()'s realloc -- which
     * can MOVE the whole array. A `frame` pointer captured before that
     * call would then dangle; re-indexing by `depth` here always finds
     * this call's own slot at its current (possibly relocated) address,
     * since `depth` itself is a stable index, not a pointer. */
    SxDeps *frame = &g_sx_deps_stack[depth];

    if (vis_symexpr(result)) {
        /* result may be a freshly-allocated node (the common case, not
         * yet visible to any other thread -- these writes could be
         * plain stores) OR a pre-existing SHARED node a rewrite path
         * returned unchanged (e.g. `(- a 0)` -> `a`, see the cache-hit
         * comment above) -- always use atomic RMW so a concurrent
         * tagging of the SAME shared node from another call never loses
         * an update. Masks (RELAXED fetch_or) are published strictly
         * before max_gen (RELEASE CAS): see the cache-hit check's
         * comment for why that specific order is what makes an ACQUIRE
         * read of max_gen there see these bits too. */
        SymExpr *rs = as_symexpr(result);
        for (int w = 0; w < SX_OP_MASK_WORDS; w++)
            if (frame->op[w]) __atomic_fetch_or(&rs->op_deps[w], frame->op[w], __ATOMIC_RELAXED);
        for (int w = 0; w < SX_VAR_MASK_WORDS; w++)
            if (frame->var[w]) __atomic_fetch_or(&rs->var_deps[w], frame->var[w], __ATOMIC_RELAXED);
        uint64_t cur = __atomic_load_n(&rs->max_gen, __ATOMIC_RELAXED);
        while (frame->max_gen > cur) {
            if (__atomic_compare_exchange_n(&rs->max_gen, &cur, frame->max_gen,
                                            1 /* weak */, __ATOMIC_RELEASE, __ATOMIC_RELAXED))
                break;
        }
    }
    /* Propagate this node's own contribution up into the (now-restored)
     * enclosing frame too, if this call was itself made while an outer
     * node's own frame was active. */
    sx_deps_merge_into_current(frame->op, frame->var, frame->max_gen);

    return result;
}

/* ---- Trigonometric simplification (trigsimp) ----
 *
 * Applies trig identities beyond what sx_simplify catches.
 * Called via (trigsimp expr) from Scheme.
 *
 * Identities applied:
 *   sin²(f) + cos²(f)  →  1          (Pythagorean — also in sx_simplify)
 *   1 − sin²(f)        →  cos²(f)
 *   1 − cos²(f)        →  sin²(f)
 *   n − sin²(f)        →  (n−1) + cos²(f)    (n exact integer ≥ 1)
 *   n − cos²(f)        →  (n−1) + sin²(f)
 *   2·sin(f)·cos(f)    →  sin(2·f)   (double-angle contraction)
 *   cos²(f) − sin²(f)  →  cos(2·f)
 */
val_t sx_trigsimp(val_t expr) {
    /* Same unbounded-recursion class as sx_simplify above (issue #134). */
    check_c_stack_depth("symbolic");
    if (!vis_symexpr(expr)) return expr;

    SymExpr *se = as_symexpr(expr);
    val_t op = se->op;
    int n = (int)se->nargs;

    /* Recursively apply trigsimp to children */
    val_t *sa = (val_t *)gc_alloc_raw_pinned((size_t)n * sizeof(val_t));
    for (int i = 0; i < n; i++) sa[i] = sx_trigsimp(se->args[i]);

    /* Rebuild and run standard simplification (which includes Pythagorean in ADD) */
    val_t e = sx_simplify(sx_make_expr(op, n, sa));

    /* Re-examine the simplified expression */
    if (!vis_symexpr(e)) return e;
    SymExpr *es = as_symexpr(e);
    val_t eop = es->op;
    int en = (int)es->nargs;

    /* ---- SUB: n − sin²(f) → (n−1) + cos²(f), etc. ---- */
    if (eop == SX_SUB && en == 2) {
        val_t a = es->args[0], b = es->args[1];
        val_t f, g;
        /* n − sin²(f) → (n−1) + cos²(f)  (integer n ≥ 1) */
        if (vis_fixnum(a) && vunfix(a) >= 1 && is_trig_sq(b, SX_SIN, &f))
            return sx_trigsimp(sx_add(num_add(a, vfix(-1)),
                                     sx_expt(sx_cos(f), vfix(2))));
        if (vis_fixnum(a) && vunfix(a) >= 1 && is_trig_sq(b, SX_COS, &f))
            return sx_trigsimp(sx_add(num_add(a, vfix(-1)),
                                     sx_expt(sx_sin(f), vfix(2))));
        /* cos²(f) − sin²(f) → cos(2·f) */
        if (is_trig_sq(a, SX_COS, &f) && is_trig_sq(b, SX_SIN, &g) && sx_equal(f, g))
            return sx_cos(sx_mul(vfix(2), f));
        /* sin²(f) − cos²(f) → −cos(2·f) */
        if (is_trig_sq(a, SX_SIN, &f) && is_trig_sq(b, SX_COS, &g) && sx_equal(f, g))
            return sx_neg(sx_cos(sx_mul(vfix(2), f)));
    }

    /* ---- ADD: borrow from integer part to complete Pythagorean ----
     * If num_acc is an integer k ≥ 1 and there's a (neg sin²(f)) term,
     * rewrite k + (-1)*sin²(f) → (k-1) + cos²(f), and similarly for cos². */
    if (eop == SX_ADD) {
        /* Collect numeric and symbolic terms */
        val_t num_part = vfix(0);
        int nsym_e = 0;
        for (int i = 0; i < en; i++)
            if (vis_number(es->args[i])) num_part = num_add(num_part, es->args[i]);
            else nsym_e++;
        val_t *syms_e = (val_t *)gc_alloc_raw_pinned((size_t)nsym_e * sizeof(val_t));
        int j = 0;
        for (int i = 0; i < en; i++)
            if (!vis_number(es->args[i])) syms_e[j++] = es->args[i];

        /* Look for neg(trig²(f)) term that can be turned into (comp²(f) - 1) */
        if (vis_fixnum(num_part) && vunfix(num_part) >= 1) {
            for (int i = 0; i < nsym_e; i++) {
                val_t ci, op_i, fi;
                if (!decompose_trig_sq(syms_e[i], &ci, &op_i, &fi)) continue;
                if (!num_eq(ci, vfix(-1))) continue;
                /* k + (-1)*sin²(f)  → (k-1) + cos²(f) */
                val_t comp_op = (op_i == SX_SIN) ? SX_COS : SX_SIN;
                val_t comp_sq = sx_expt(sx_make_expr(comp_op, 1, &fi), vfix(2));
                syms_e[i] = comp_sq;
                num_part = num_add(num_part, vfix(-1));
                /* Rebuild and re-simplify (catches further Pythagorean reductions) */
                val_t *all2 = (val_t *)gc_alloc_raw_pinned((size_t)(nsym_e + 1) * sizeof(val_t));
                all2[0] = num_part;
                for (int k2 = 0; k2 < nsym_e; k2++) all2[k2 + 1] = syms_e[k2];
                return sx_trigsimp(sx_simplify(
                    sx_make_expr(SX_ADD, nsym_e + 1, all2)));
            }
        }
    }

    /* ---- MUL: c·sin(f)·cos(f) → (c/2)·sin(2·f), with optional sign flips ----
     * Matches both ±2·sin·cos (traditional) and ±2·MUL→NEG-free form produced
     * after MUL canonicalisation absorbs any NEG factors into a numeric coeff. */
    if (eop == SX_MUL) {
        /* Extract leading numeric coefficient (may be ±2 after NEG-absorption) */
        val_t lead_num = V_FALSE;
        int lead_idx   = -1;
        val_t sin_f = V_FALSE, cos_f = V_FALSE;
        bool neg_sin = false, neg_cos = false;
        for (int i = 0; i < en; i++) {
            val_t v = es->args[i];
            if (vis_number(v) && vis_false(lead_num)) {
                lead_num = v; lead_idx = i; continue;
            }
            if (!vis_symexpr(v)) continue;
            SymExpr *fi = as_symexpr(v);
            /* direct sin/cos */
            if (fi->op == SX_SIN && fi->nargs == 1 && vis_false(sin_f)) {
                sin_f = fi->args[0]; continue;
            }
            if (fi->op == SX_COS && fi->nargs == 1 && vis_false(cos_f)) {
                cos_f = fi->args[0]; continue;
            }
            /* neg(sin(f)) or neg(cos(f)) — still emitted by trigsimp sub-exprs */
            if (fi->op == SX_NEG && fi->nargs == 1 && vis_symexpr(fi->args[0])) {
                SymExpr *inner = as_symexpr(fi->args[0]);
                if (inner->op == SX_SIN && inner->nargs == 1 && vis_false(sin_f)) {
                    sin_f = inner->args[0]; neg_sin = true; continue;
                }
                if (inner->op == SX_COS && inner->nargs == 1 && vis_false(cos_f)) {
                    cos_f = inner->args[0]; neg_cos = true; continue;
                }
            }
        }
        /* Need |coeff| == 2, plus matching sin and cos of the same argument */
        if (!vis_false(lead_num) && !vis_false(sin_f) && !vis_false(cos_f)
                && sx_equal(sin_f, cos_f)) {
            double cv = num_to_double(lead_num);
            if (cv == 2.0 || cv == -2.0) {
                int sign = (cv < 0 ? -1 : 1) * (neg_sin ? -1 : 1) * (neg_cos ? -1 : 1);
                val_t *rest = (val_t *)gc_alloc_raw_pinned((size_t)en * sizeof(val_t));
                int rn = 0;
                bool used_num = false, used_sin = false, used_cos = false;
                for (int i = 0; i < en; i++) {
                    val_t v = es->args[i];
                    if (!used_num && i == lead_idx) { used_num = true; continue; }
                    if (!used_sin && vis_symexpr(v)) {
                        SymExpr *fi = as_symexpr(v);
                        if (fi->op == SX_SIN) { used_sin = true; continue; }
                        if (fi->op == SX_NEG && fi->nargs == 1 && vis_symexpr(fi->args[0]) &&
                            as_symexpr(fi->args[0])->op == SX_SIN) { used_sin = true; continue; }
                    }
                    if (!used_cos && vis_symexpr(v)) {
                        SymExpr *fi = as_symexpr(v);
                        if (fi->op == SX_COS) { used_cos = true; continue; }
                        if (fi->op == SX_NEG && fi->nargs == 1 && vis_symexpr(fi->args[0]) &&
                            as_symexpr(fi->args[0])->op == SX_COS) { used_cos = true; continue; }
                    }
                    rest[rn++] = v;
                }
                val_t two_f = sx_mul(vfix(2), sin_f);
                val_t sin2f = sx_sin(two_f);
                val_t result = (sign < 0) ? sx_neg(sin2f) : sin2f;
                if (rn == 0) return result;
                rest[rn++] = result;
                return sx_simplify(sx_make_expr(SX_MUL, rn, rest));
            }
        }
    }

    /* ---- ADD: cos²(f) − sin²(f) → cos(2f) ---- */
    if (eop == SX_ADD && en >= 2) {
        for (int i = 0; i < en; i++) {
            val_t f1;
            if (!is_trig_sq(es->args[i], SX_COS, &f1)) continue;
            for (int k2 = 0; k2 < en; k2++) {
                if (k2 == i) continue;
                val_t ck2, op_k2, f2;
                if (!decompose_trig_sq(es->args[k2], &ck2, &op_k2, &f2)) continue;
                if (op_k2 != SX_SIN) continue;
                if (!num_eq(ck2, vfix(-1))) continue;
                if (!sx_equal(f1, f2)) continue;
                /* cos²(f) + (-1)*sin²(f) → cos(2f) */
                val_t cos2f = sx_cos(sx_mul(vfix(2), f1));
                val_t *rest = (val_t *)gc_alloc_raw_pinned((size_t)en * sizeof(val_t));
                int rn = 0;
                for (int j = 0; j < en; j++)
                    if (j != i && j != k2) rest[rn++] = es->args[j];
                if (rn == 0) return cos2f;
                rest[rn++] = cos2f;
                return sx_trigsimp(sx_simplify(sx_make_expr(SX_ADD, rn, rest)));
            }
        }
    }

    return e;
}

/* ---- Symbolic arithmetic (these return simplified expressions) ---- */

/* Returns true if v is or contains a quaternion/octonion value or a
   quaternion-typed sym-var — signalling that products involving v are
   non-commutative and must use SX_NCMUL. */
static bool sx_is_nc(val_t v) {
    /* Same unbounded-recursion class as sx_simplify above (issue #134). */
    check_c_stack_depth("symbolic");
    if (vis_quat(v) || vis_oct(v)) return true;
    if (vis_symvar(v)) return (sym_var_flags(v) & SYM_ASSUME_QUATERNION) != 0;
    if (!vis_symexpr(v)) return false;
    SymExpr *e = as_symexpr(v);
    if (e->op == SX_NCMUL) return true;
    for (uint32_t i = 0; i < e->nargs; i++)
        if (sx_is_nc(e->args[i])) return true;
    return false;
}

val_t sx_neg(val_t a) {
    if (vis_number(a)) return num_neg(a);
    if (vis_tuple(a))  return num_neg(a);   /* distribute negation over tuple components */
    return sx_simplify(sx_expr1(SX_NEG, a));
}
val_t sx_abs(val_t a) {
    if (vis_number(a)) return num_abs(a);
    return sx_simplify(sx_expr1(SX_ABS, a));
}
val_t sx_add(val_t a, val_t b) {
    /* tuple + tuple (with symbolic elements) distributes via num_add */
    if (vis_tuple(a) || vis_tuple(b)) return num_add(a, b);
    return sx_simplify(sx_expr2(SX_ADD, a, b));
}
val_t sx_sub(val_t a, val_t b) {
    if (vis_tuple(a) || vis_tuple(b)) return num_sub(a, b);
    return sx_simplify(sx_expr2(SX_SUB, a, b));
}
val_t sx_mul(val_t a, val_t b) {
    /* scalar (including symbolic) × tuple distributes component-wise */
    if (vis_tuple(a) || vis_tuple(b)) return num_mul(a, b);
    if (sx_is_nc(a) || sx_is_nc(b)) return sx_ncmul(a, b);
    return sx_simplify(sx_expr2(SX_MUL, a, b));
}
val_t sx_ncmul(val_t a, val_t b) { return sx_simplify(sx_expr2(SX_NCMUL, a, b)); }
val_t sx_div(val_t a, val_t b) { return sx_simplify(sx_expr2(SX_DIV, a, b)); }
val_t sx_expt(val_t base, val_t exp) { return sx_simplify(sx_expr2(SX_EXPT, base, exp)); }

/* Unary transcendentals: symbolic when arg is symbolic, numeric otherwise.
 * SX_UNARY: sx_simplify handles numeric evaluation via its operator table.
 * SX_UNARY_NUM: explicit numeric early-exit for ops not in sx_simplify's table. */
#define SX_UNARY(name, op) \
    val_t sx_##name(val_t a) { return sx_simplify(sx_expr1(op, a)); }
#define SX_UNARY_NUM(name, op, fn) \
    val_t sx_##name(val_t a) { if (vis_number(a)) return fn(a); return sx_simplify(sx_expr1(op, a)); }

SX_UNARY(sqrt,  SX_SQRT)
SX_UNARY(sin,   SX_SIN)
SX_UNARY(cos,   SX_COS)
SX_UNARY(tan,   SX_TAN)
SX_UNARY(exp,   SX_EXP)
SX_UNARY(log,   SX_LOG)
SX_UNARY_NUM(sinh,  SX_SINH,  num_sinh)
SX_UNARY_NUM(cosh,  SX_COSH,  num_cosh)
SX_UNARY_NUM(tanh,  SX_TANH,  num_tanh)
SX_UNARY_NUM(asin,  SX_ASIN,  num_asin)
SX_UNARY_NUM(acos,  SX_ACOS,  num_acos)
SX_UNARY_NUM(atan,  SX_ATAN,  num_atan)
SX_UNARY_NUM(asinh, SX_ASINH, num_asinh)
SX_UNARY_NUM(acosh, SX_ACOSH, num_acosh)
SX_UNARY_NUM(atanh, SX_ATANH, num_atanh)
SX_UNARY_NUM(cot,   SX_COT,   num_cot)
SX_UNARY_NUM(sec,   SX_SEC,   num_sec)
SX_UNARY_NUM(csc,   SX_CSC,   num_csc)

#undef SX_UNARY
#undef SX_UNARY_NUM

val_t sx_sign(val_t a) {
    if (vis_number(a)) {
        if (num_is_zero(a))     return vfix(0);
        if (num_is_negative(a)) return vfix(-1);
        return vfix(1);
    }
    return sx_simplify(sx_expr1(SX_SIGN, a));
}

/* Decision logic (assumption-based, structural-equality reflexive case)
 * lives inside sx_simplify's dispatch, mirroring SX_SIGN/SX_ABS above --
 * these constructors just build the node and simplify it, so a later
 * re-simplify (e.g. after substitute swaps a number in for a variable)
 * decides it too, not just at construction time. */
val_t sx_lt(val_t a, val_t b) { return sx_simplify(sx_expr2(SX_LT, a, b)); }
val_t sx_le(val_t a, val_t b) { return sx_simplify(sx_expr2(SX_LE, a, b)); }
val_t sx_gt(val_t a, val_t b) { return sx_simplify(sx_expr2(SX_GT, a, b)); }
val_t sx_ge(val_t a, val_t b) { return sx_simplify(sx_expr2(SX_GE, a, b)); }

val_t sx_conj(val_t a) {
    if (vis_number(a)) return num_conjugate(a);
    return sx_simplify(sx_expr1(SX_CONJ, a));
}
val_t sx_real(val_t a) {
    if (vis_number(a)) return num_real_part(a);
    return sx_simplify(sx_expr1(SX_REAL, a));
}
val_t sx_imag(val_t a) {
    if (vis_number(a)) return num_imag_part(a);
    return sx_simplify(sx_expr1(SX_IMAG, a));
}

/* ---- Symbolic function (T_SYMFN) derivative helpers ---- */

/* Create the partial derivative of fn w.r.t. param_var (a sym-var from fn's params).
 * Name: "<fn_name>_<param_name>" (e.g. u → u_x).  Inherits fn's params and records lineage. */
static val_t sx_diff_symfn(val_t fn, val_t param_var) {
    SymFn   *sf      = as_symfn(fn);
    Symbol  *fn_sym  = as_sym(sf->name);
    Symbol  *par_sym = as_sym(as_symvar(param_var)->name);
    /* Build "<fn_name>_<param_name>" */
    size_t  total    = fn_sym->len + 1 + par_sym->len;
    char   *buf      = (char *)gc_alloc_raw_pinned_atomic(total + 1);
    memcpy(buf, fn_sym->data, fn_sym->len);
    buf[fn_sym->len] = '_';
    memcpy(buf + fn_sym->len + 1, par_sym->data, par_sym->len);
    buf[total] = '\0';
    val_t   dname = sym_intern_cstr(buf);
    SymFn  *d     = CURRY_NEW(SymFn);
    d->hdr.type   = T_SYMFN;
    d->hdr.flags  = 0;
    d->name       = dname;
    d->params     = sf->params;
    d->parent     = fn;
    d->d_param    = param_var;
    return vptr(d);
}

/* Like sx_diff_symfn but using an integer index when params are unavailable.
 * Name: "<fn_name>_<index>" (e.g. f_0, f_1). */
static val_t sx_diff_symfn_idx(val_t fn, int idx) {
    SymFn  *sf     = as_symfn(fn);
    Symbol *fn_sym = as_sym(sf->name);
    char    buf[128];
    snprintf(buf, sizeof(buf), "%.*s_%d", (int)fn_sym->len, fn_sym->data, idx);
    SymFn  *d    = CURRY_NEW(SymFn);
    d->hdr.type  = T_SYMFN;
    d->hdr.flags = 0;
    d->name      = sym_intern_cstr(buf);
    d->params    = sf->params;
    d->parent    = fn;
    d->d_param   = V_FALSE;
    return vptr(d);
}

/* ---- Symbolic differentiation ---- */

val_t sx_diff(val_t expr, val_t var) {
    /* Same unbounded-recursion class as sx_simplify/sx_substitute above
     * (issue #134): its own separate self-recursive tree-walk (product/
     * chain-rule cases below recurse into sx_diff on their own
     * subexpressions), not merely a caller of either. */
    check_c_stack_depth("symbolic");
    /* var must be a T_SYMVAR */
    if (!vis_symvar(var))
        scm_raise(V_FALSE, "∂: second argument must be a symbolic variable");

    /* Numbers differentiate to 0 */
    if (vis_number(expr)) return vfix(0);

    /* Symbolic variable */
    if (vis_symvar(expr)) {
        return (as_symvar(expr)->name == as_symvar(var)->name) ? vfix(1) : vfix(0);
    }

    /* Tuple (up/down): differentiate componentwise */
    if (vis_tuple(expr)) {
        Tuple *t = as_tuple(expr);
        val_t *diffs = (val_t *)gc_alloc_raw_pinned((size_t)t->len * sizeof(val_t));
        for (uint32_t i = 0; i < t->len; i++) diffs[i] = sx_diff(t->data[i], var);
        return num_make_tuple((int)t->hdr.type, t->len, diffs);
    }

    if (!vis_symexpr(expr)) return vfix(0);

    SymExpr *se = as_symexpr(expr);
    val_t op = se->op;
    int n = (int)se->nargs;
    val_t *args = se->args;

    /* ---- d/dx (a + b + ...) = da/dx + db/dx + ... ---- */
    if (op == SX_ADD) {
        val_t *dargs = (val_t *)gc_alloc_raw_pinned((size_t)n * sizeof(val_t));
        for (int i = 0; i < n; i++) dargs[i] = sx_diff(args[i], var);
        return sx_simplify(sx_make_expr(SX_ADD, n, dargs));
    }

    /* ---- d/dx (a - b) = da/dx - db/dx ---- */
    if (op == SX_SUB && n == 2)
        return sx_sub(sx_diff(args[0], var), sx_diff(args[1], var));

    /* ---- d/dx (-a) = -(da/dx) ---- */
    if (op == SX_NEG && n == 1)
        return sx_neg(sx_diff(args[0], var));

    /* ---- Product rule: d/dx (f*g*h*...) = Σᵢ (product with fᵢ replaced by dfᵢ/dx) ---- */
    if (op == SX_MUL) {
        val_t *sum_terms = (val_t *)gc_alloc_raw_pinned((size_t)n * sizeof(val_t));
        for (int i = 0; i < n; i++) {
            val_t *factors = (val_t *)gc_alloc_raw_pinned((size_t)n * sizeof(val_t));
            for (int j = 0; j < n; j++)
                factors[j] = (j == i) ? sx_diff(args[j], var) : args[j];
            sum_terms[i] = sx_simplify(sx_make_expr(SX_MUL, n, factors));
        }
        return sx_simplify(sx_make_expr(SX_ADD, n, sum_terms));
    }

    /* ---- NC product rule: order of remaining factors must be preserved ---- */
    if (op == SX_NCMUL) {
        val_t *sum_terms = (val_t *)gc_alloc_raw_pinned((size_t)n * sizeof(val_t));
        int nterms = 0;
        for (int i = 0; i < n; i++) {
            val_t di = sx_diff(args[i], var);
            if (is_zero(di)) continue;
            val_t *factors = (val_t *)gc_alloc_raw_pinned((size_t)n * sizeof(val_t));
            for (int j = 0; j < n; j++)
                factors[j] = (j == i) ? di : args[j];
            sum_terms[nterms++] = sx_simplify(sx_make_expr(SX_NCMUL, n, factors));
        }
        if (nterms == 0) return vfix(0);
        val_t result = sum_terms[0];
        for (int i = 1; i < nterms; i++) result = sx_add(result, sum_terms[i]);
        return result;
    }

    /* ---- Quotient rule: d/dx (f/g) = (f'g - fg') / g² ---- */
    if (op == SX_DIV && n == 2) {
        val_t f = args[0], g = args[1];
        val_t df = sx_diff(f, var), dg = sx_diff(g, var);
        if (is_zero(dg))
            return sx_div(df, g);   /* constant denominator: simpler form */
        return sx_div(sx_sub(sx_mul(df, g), sx_mul(f, dg)),
                      sx_mul(g, g));
    }

    /* ---- Power rule: d/dx (f^n) = n * f^(n-1) * f' ---- */
    if (op == SX_EXPT && n == 2) {
        val_t base = args[0], exp = args[1];
        if (vis_number(exp)) {
            val_t n1 = num_sub(exp, vfix(1));
            return sx_mul(sx_mul(exp, sx_expt(base, n1)), sx_diff(base, var));
        }
        /* General: d/dx[f^g] = f^g * (g' ln f + g f'/f) */
        val_t df = sx_diff(base, var), dg = sx_diff(exp, var);
        val_t term1 = sx_mul(dg, sx_log(base));
        val_t term2 = sx_mul(exp, sx_div(df, base));
        return sx_mul(expr, sx_add(term1, term2));
    }

    /* ---- Transcendentals (chain rule) ---- */
    if (op == SX_SIN && n == 1)
        return sx_mul(sx_cos(args[0]), sx_diff(args[0], var));

    if (op == SX_COS && n == 1)
        return sx_neg(sx_mul(sx_sin(args[0]), sx_diff(args[0], var)));

    if (op == SX_TAN && n == 1) {
        /* d/dx tan(f) = f' / cos²(f) */
        val_t cos2 = sx_expt(sx_cos(args[0]), vfix(2));
        return sx_div(sx_diff(args[0], var), cos2);
    }

    if (op == SX_EXP && n == 1)
        return sx_mul(expr, sx_diff(args[0], var));

    if (op == SX_LOG && n == 1)
        return sx_div(sx_diff(args[0], var), args[0]);

    if (op == SX_SQRT && n == 1) {
        /* d/dx √f = f' / (2√f) */
        return sx_div(sx_diff(args[0], var),
                      sx_mul(vfix(2), sx_sqrt(args[0])));
    }

    if (op == SX_ABS && n == 1) {
        /* d/dx |f| = f * f' / |f|  (undefined at 0) */
        return sx_div(sx_mul(args[0], sx_diff(args[0], var)), expr);
    }

    /* ---- Hyperbolic (chain rule) ---- */
    if (op == SX_SINH && n == 1)
        return sx_mul(sx_cosh(args[0]), sx_diff(args[0], var));

    if (op == SX_COSH && n == 1)
        return sx_mul(sx_sinh(args[0]), sx_diff(args[0], var));

    if (op == SX_TANH && n == 1)
        return sx_div(sx_diff(args[0], var), sx_expt(sx_cosh(args[0]), vfix(2)));

    /* ---- Inverse trig (chain rule) ---- */
    if (op == SX_ASIN && n == 1)
        return sx_div(sx_diff(args[0], var),
                      sx_sqrt(sx_sub(vfix(1), sx_expt(args[0], vfix(2)))));

    if (op == SX_ACOS && n == 1)
        return sx_neg(sx_div(sx_diff(args[0], var),
                             sx_sqrt(sx_sub(vfix(1), sx_expt(args[0], vfix(2))))));

    if (op == SX_ATAN && n == 1)
        return sx_div(sx_diff(args[0], var),
                      sx_add(vfix(1), sx_expt(args[0], vfix(2))));

    if (op == SX_ASINH && n == 1)
        return sx_div(sx_diff(args[0], var),
                      sx_sqrt(sx_add(sx_expt(args[0], vfix(2)), vfix(1))));

    if (op == SX_ACOSH && n == 1)
        return sx_div(sx_diff(args[0], var),
                      sx_sqrt(sx_sub(sx_expt(args[0], vfix(2)), vfix(1))));

    if (op == SX_ATANH && n == 1)
        return sx_div(sx_diff(args[0], var),
                      sx_sub(vfix(1), sx_expt(args[0], vfix(2))));

    /* ---- Reciprocal trig (chain rule) ---- */
    if (op == SX_COT && n == 1)
        return sx_neg(sx_div(sx_diff(args[0], var),
                             sx_expt(sx_sin(args[0]), vfix(2))));

    if (op == SX_SEC && n == 1)
        return sx_mul(sx_mul(sx_sec(args[0]), sx_tan(args[0])),
                      sx_diff(args[0], var));

    if (op == SX_CSC && n == 1)
        return sx_neg(sx_mul(sx_mul(sx_csc(args[0]), sx_cot(args[0])),
                             sx_diff(args[0], var)));

    /* ---- Complex operators (x is a real variable) ---- */
    /* ∂conj(f)/∂x = conj(∂f/∂x) */
    if (op == SX_CONJ && n == 1)
        return sx_conj(sx_diff(args[0], var));

    /* ∂real(f)/∂x = real(∂f/∂x) */
    if (op == SX_REAL && n == 1)
        return sx_real(sx_diff(args[0], var));

    /* ∂imag(f)/∂x = imag(∂f/∂x) */
    if (op == SX_IMAG && n == 1)
        return sx_imag(sx_diff(args[0], var));

    /* ---- SX_APPLY: chain rule over function arguments ---- */
    if (op == SX_APPLY && n >= 1) {
        val_t fn      = args[0];  /* T_SYMFN */
        int   nf      = n - 1;   /* number of applied arguments */
        val_t *fargs  = args + 1;
        val_t params  = as_symfn(fn)->params;
        val_t sum     = vfix(0);
        for (int i = 0; i < nf; i++) {
            val_t darg = sx_diff(fargs[i], var);
            if (is_zero(darg)) continue;
            /* Find i-th param sym-var for derivative naming */
            val_t param_i = V_FALSE;
            val_t pl = params;
            for (int j = 0; vis_pair(pl); j++, pl = vcdr(pl)) {
                if (j == i) { param_i = vcar(pl); break; }
            }
            val_t d_fn = vis_symvar(param_i)
                       ? sx_diff_symfn(fn, param_i)
                       : sx_diff_symfn_idx(fn, i);
            val_t d_apply = sx_make_apply(d_fn, nf, fargs);
            val_t term    = sx_simplify(sx_mul(d_apply, darg));
            sum           = sx_simplify(sx_add(sum, term));
        }
        return sum;
    }

    /* Unknown op — return unevaluated ∂ notation */
    val_t diff_sym = sym_intern_cstr("∂");
    val_t d_args[2] = {expr, var};
    return sx_make_expr(diff_sym, 2, d_args);
}

/* ---- Wirtinger derivatives  ∂/∂z  and  ∂/∂z̄ ---- */

val_t sx_wirtinger(val_t expr, val_t var, bool is_dbar) {
    /* Same unbounded-recursion class as sx_diff above (issue #134). */
    check_c_stack_depth("symbolic");
    if (!vis_symvar(var))
        scm_raise(V_FALSE, "wirtinger: second argument must be a symbolic variable");

    if (vis_number(expr)) return vfix(0);

    /* Variable: ∂z/∂z=1, ∂z/∂z̄=0 */
    if (vis_symvar(expr)) {
        return (as_symvar(expr)->name == as_symvar(var)->name)
               ? (is_dbar ? vfix(0) : vfix(1))
               : vfix(0);
    }

    if (!vis_symexpr(expr)) return vfix(0);

    SymExpr *se = as_symexpr(expr);
    val_t op = se->op;
    int n = (int)se->nargs;
    val_t *args = se->args;

    /* KEY WIRTINGER RULES for conj ---------------------------------------- */
    if (op == SX_CONJ && n == 1) {
        /* conj(var): ∂conj(z)/∂z = 0,  ∂conj(z)/∂z̄ = 1 */
        if (vis_symvar(args[0]) &&
            as_symvar(args[0])->name == as_symvar(var)->name)
            return is_dbar ? vfix(1) : vfix(0);
        /* conj(f): ∂/∂z = conj(∂f/∂z̄),  ∂/∂z̄ = conj(∂f/∂z) */
        return sx_conj(sx_wirtinger(args[0], var, !is_dbar));
    }

    /* real(f) = ½(f + conj(f)):
       ∂real(f)/∂z  = ½(∂f/∂z  + conj(∂f/∂z̄))
       ∂real(f)/∂z̄ = ½(∂f/∂z̄ + conj(∂f/∂z))   */
    if (op == SX_REAL && n == 1) {
        val_t df   = sx_wirtinger(args[0], var, is_dbar);
        val_t dfb  = sx_wirtinger(args[0], var, !is_dbar);
        return sx_div(sx_add(df, sx_conj(dfb)), vfix(2));
    }

    /* imag(f) = (f - conj(f))/(2i):
       ∂imag(f)/∂z  = (∂f/∂z  - conj(∂f/∂z̄)) / (2i)
       ∂imag(f)/∂z̄ = (∂f/∂z̄ - conj(∂f/∂z))  / (2i)  */
    if (op == SX_IMAG && n == 1) {
        val_t df   = sx_wirtinger(args[0], var, is_dbar);
        val_t dfb  = sx_wirtinger(args[0], var, !is_dbar);
        val_t two_i = num_make_complex(vfix(0), vfix(2));
        return sx_div(sx_sub(df, sx_conj(dfb)), two_i);
    }

    /* Linearity -----------------------------------------------------------  */
    if (op == SX_ADD) {
        val_t *dargs = (val_t *)gc_alloc_raw_pinned((size_t)n * sizeof(val_t));
        for (int i = 0; i < n; i++) dargs[i] = sx_wirtinger(args[i], var, is_dbar);
        return sx_simplify(sx_make_expr(SX_ADD, n, dargs));
    }
    if (op == SX_SUB && n == 2)
        return sx_sub(sx_wirtinger(args[0], var, is_dbar),
                      sx_wirtinger(args[1], var, is_dbar));
    if (op == SX_NEG && n == 1)
        return sx_neg(sx_wirtinger(args[0], var, is_dbar));

    /* Product rule --------------------------------------------------------- */
    if (op == SX_MUL) {
        val_t *terms = (val_t *)gc_alloc_raw_pinned((size_t)n * sizeof(val_t));
        for (int i = 0; i < n; i++) {
            val_t *factors = (val_t *)gc_alloc_raw_pinned((size_t)n * sizeof(val_t));
            for (int j = 0; j < n; j++)
                factors[j] = (j == i) ? sx_wirtinger(args[j], var, is_dbar) : args[j];
            terms[i] = sx_simplify(sx_make_expr(SX_MUL, n, factors));
        }
        return sx_simplify(sx_make_expr(SX_ADD, n, terms));
    }
    if (op == SX_NCMUL) {
        val_t *terms = (val_t *)gc_alloc_raw_pinned((size_t)n * sizeof(val_t));
        int nterms = 0;
        for (int i = 0; i < n; i++) {
            val_t di = sx_wirtinger(args[i], var, is_dbar);
            if (is_zero(di)) continue;
            val_t *factors = (val_t *)gc_alloc_raw_pinned((size_t)n * sizeof(val_t));
            for (int j = 0; j < n; j++)
                factors[j] = (j == i) ? di : args[j];
            terms[nterms++] = sx_simplify(sx_make_expr(SX_NCMUL, n, factors));
        }
        if (nterms == 0) return vfix(0);
        val_t result = terms[0];
        for (int i = 1; i < nterms; i++) result = sx_add(result, terms[i]);
        return result;
    }

    /* Quotient rule -------------------------------------------------------- */
    if (op == SX_DIV && n == 2) {
        val_t f = args[0], g = args[1];
        val_t df = sx_wirtinger(f, var, is_dbar);
        val_t dg = sx_wirtinger(g, var, is_dbar);
        if (is_zero(dg)) return sx_div(df, g);
        return sx_div(sx_sub(sx_mul(df, g), sx_mul(f, dg)), sx_mul(g, g));
    }

    /* Power rule ----------------------------------------------------------- */
    if (op == SX_EXPT && n == 2) {
        val_t base = args[0], exp_v = args[1];
        if (vis_number(exp_v)) {
            val_t n1 = num_sub(exp_v, vfix(1));
            return sx_mul(sx_mul(exp_v, sx_expt(base, n1)),
                          sx_wirtinger(base, var, is_dbar));
        }
        val_t df = sx_wirtinger(base, var, is_dbar);
        val_t dg = sx_wirtinger(exp_v, var, is_dbar);
        return sx_mul(expr, sx_add(sx_mul(dg, sx_log(base)),
                                   sx_mul(exp_v, sx_div(df, base))));
    }

    /* Holomorphic transcendentals — chain rule (same for ∂/∂z and ∂/∂z̄) -- */
    if (op == SX_SIN && n == 1)
        return sx_mul(sx_cos(args[0]), sx_wirtinger(args[0], var, is_dbar));
    if (op == SX_COS && n == 1)
        return sx_neg(sx_mul(sx_sin(args[0]), sx_wirtinger(args[0], var, is_dbar)));
    if (op == SX_TAN && n == 1)
        return sx_div(sx_wirtinger(args[0], var, is_dbar),
                      sx_expt(sx_cos(args[0]), vfix(2)));
    if (op == SX_EXP && n == 1)
        return sx_mul(expr, sx_wirtinger(args[0], var, is_dbar));
    if (op == SX_LOG && n == 1)
        return sx_div(sx_wirtinger(args[0], var, is_dbar), args[0]);
    if (op == SX_SQRT && n == 1)
        return sx_div(sx_wirtinger(args[0], var, is_dbar),
                      sx_mul(vfix(2), sx_sqrt(args[0])));

    /* Holomorphic transcendentals — Phase 1 extensions */
    if (op == SX_SINH && n == 1)
        return sx_mul(sx_cosh(args[0]), sx_wirtinger(args[0], var, is_dbar));
    if (op == SX_COSH && n == 1)
        return sx_mul(sx_sinh(args[0]), sx_wirtinger(args[0], var, is_dbar));
    if (op == SX_TANH && n == 1)
        return sx_div(sx_wirtinger(args[0], var, is_dbar),
                      sx_expt(sx_cosh(args[0]), vfix(2)));
    if (op == SX_ASIN && n == 1)
        return sx_div(sx_wirtinger(args[0], var, is_dbar),
                      sx_sqrt(sx_sub(vfix(1), sx_expt(args[0], vfix(2)))));
    if (op == SX_ACOS && n == 1)
        return sx_neg(sx_div(sx_wirtinger(args[0], var, is_dbar),
                             sx_sqrt(sx_sub(vfix(1), sx_expt(args[0], vfix(2))))));
    if (op == SX_ATAN && n == 1)
        return sx_div(sx_wirtinger(args[0], var, is_dbar),
                      sx_add(vfix(1), sx_expt(args[0], vfix(2))));
    if (op == SX_ASINH && n == 1)
        return sx_div(sx_wirtinger(args[0], var, is_dbar),
                      sx_sqrt(sx_add(sx_expt(args[0], vfix(2)), vfix(1))));
    if (op == SX_ACOSH && n == 1)
        return sx_div(sx_wirtinger(args[0], var, is_dbar),
                      sx_sqrt(sx_sub(sx_expt(args[0], vfix(2)), vfix(1))));
    if (op == SX_ATANH && n == 1)
        return sx_div(sx_wirtinger(args[0], var, is_dbar),
                      sx_sub(vfix(1), sx_expt(args[0], vfix(2))));
    if (op == SX_COT && n == 1)
        return sx_neg(sx_div(sx_wirtinger(args[0], var, is_dbar),
                             sx_expt(sx_sin(args[0]), vfix(2))));
    if (op == SX_SEC && n == 1)
        return sx_mul(sx_mul(sx_sec(args[0]), sx_tan(args[0])),
                      sx_wirtinger(args[0], var, is_dbar));
    if (op == SX_CSC && n == 1)
        return sx_neg(sx_mul(sx_mul(sx_csc(args[0]), sx_cot(args[0])),
                             sx_wirtinger(args[0], var, is_dbar)));

    /* Fallback: unevaluated */
    val_t wsym = sym_intern_cstr(is_dbar ? "∂z̄" : "∂z");
    val_t w_args[2] = {expr, var};
    return sx_make_expr(wsym, 2, w_args);
}

/* ---- Substitution ---- */

val_t sx_substitute(val_t expr, val_t var, val_t val) {
    /* Same unbounded-recursion class as sx_simplify above (issue #134):
     * this is its own separate self-recursive tree-walk, not merely a
     * caller of sx_simplify (which only gets consulted at the very end
     * of each level, on the way back up), so it needs its own guard. */
    check_c_stack_depth("symbolic");
    if (vis_number(expr)) return expr;
    if (vis_symvar(expr)) {
        if (as_symvar(expr)->name == as_symvar(var)->name) return val;
        return expr;
    }
    /* Tuple: substitute componentwise */
    if (vis_tuple(expr)) {
        Tuple *t = as_tuple(expr);
        val_t *subs = (val_t *)gc_alloc_raw_pinned((size_t)t->len * sizeof(val_t));
        for (uint32_t i = 0; i < t->len; i++) subs[i] = sx_substitute(t->data[i], var, val);
        return num_make_tuple((int)t->hdr.type, t->len, subs);
    }
    if (vis_symexpr(expr)) {
        SymExpr *se = as_symexpr(expr);
        val_t *sargs = (val_t *)gc_alloc_raw_pinned((size_t)se->nargs * sizeof(val_t));
        for (uint32_t i = 0; i < se->nargs; i++)
            sargs[i] = sx_substitute(se->args[i], var, val);
        return sx_simplify(sx_make_expr(se->op, (int)se->nargs, sargs));
    }
    return expr;
}

/* ---- Dependency test ---- */

bool sx_depends_on(val_t expr, val_t var) {
    /* Same unbounded-recursion class as sx_simplify above (issue #134).
     * Found by independent security review to be reachable at
     * unbounded depth despite sx_integrate/sx_limit/sx_series/
     * sx_laplace/sx_ilaplace/sx_fourier/sx_ifourier's own guards
     * already having run: this is called as their first structural
     * check, on a `up`/`down` tuple expression, which builds nesting
     * in O(1) per level with no simplification pass -- unlike ordinary
     * symexpr construction (capped by sx_simplify's own guard), a
     * tuple's depth is not bounded before it ever reaches here. */
    check_c_stack_depth("symbolic");
    if (vis_number(expr)) return false;
    if (vis_symvar(expr))
        return as_symvar(expr)->name == as_symvar(var)->name;
    if (vis_symfn(expr)) return false;  /* sym-fn is a function object, not a variable */
    if (vis_tuple(expr)) {
        Tuple *t = as_tuple(expr);
        for (uint32_t i = 0; i < t->len; i++)
            if (sx_depends_on(t->data[i], var)) return true;
        return false;
    }
    if (vis_symexpr(expr)) {
        SymExpr *se = as_symexpr(expr);
        for (uint32_t i = 0; i < se->nargs; i++)
            if (sx_depends_on(se->args[i], var)) return true;
    }
    return false;
}

/* ---- Polynomial / structural operations ---- */

/* Distribute a*b when either operand is a sum. */
static val_t expand_mul2(val_t a, val_t b) {
    if (vis_symexpr(a) && as_symexpr(a)->op == SX_ADD) {
        SymExpr *aa = as_symexpr(a);
        int m = (int)aa->nargs;
        val_t *terms = (val_t *)gc_alloc_raw_pinned((size_t)m * sizeof(val_t));
        for (int i = 0; i < m; i++) terms[i] = expand_mul2(aa->args[i], b);
        return sx_simplify(sx_make_expr(SX_ADD, m, terms));
    }
    if (vis_symexpr(b) && as_symexpr(b)->op == SX_ADD) {
        SymExpr *ab = as_symexpr(b);
        int m = (int)ab->nargs;
        val_t *terms = (val_t *)gc_alloc_raw_pinned((size_t)m * sizeof(val_t));
        for (int i = 0; i < m; i++) terms[i] = expand_mul2(a, ab->args[i]);
        return sx_simplify(sx_make_expr(SX_ADD, m, terms));
    }
    return sx_mul(a, b);
}

/* Non-commutative analogue of expand_mul2: distribute a⊗b maintaining order. */
static val_t expand_ncmul2(val_t a, val_t b) {
    if (vis_symexpr(a) && as_symexpr(a)->op == SX_ADD) {
        SymExpr *aa = as_symexpr(a);
        int m = (int)aa->nargs;
        val_t *terms = (val_t *)gc_alloc_raw_pinned((size_t)m * sizeof(val_t));
        for (int i = 0; i < m; i++) terms[i] = expand_ncmul2(aa->args[i], b);
        return sx_simplify(sx_make_expr(SX_ADD, m, terms));
    }
    if (vis_symexpr(b) && as_symexpr(b)->op == SX_ADD) {
        SymExpr *ab = as_symexpr(b);
        int m = (int)ab->nargs;
        val_t *terms = (val_t *)gc_alloc_raw_pinned((size_t)m * sizeof(val_t));
        for (int i = 0; i < m; i++) terms[i] = expand_ncmul2(a, ab->args[i]);
        return sx_simplify(sx_make_expr(SX_ADD, m, terms));
    }
    return sx_ncmul(a, b);
}

val_t sx_expand(val_t expr) {
    /* Same unbounded-recursion class as sx_simplify above (issue #134). */
    check_c_stack_depth("symbolic");
    if (!vis_symbolic(expr)) return expr;
    if (vis_symvar(expr)) return expr;

    SymExpr *se = as_symexpr(expr);
    val_t op = se->op;
    int n = (int)se->nargs;

    val_t *ea = (val_t *)gc_alloc_raw_pinned((size_t)n * sizeof(val_t));
    for (int i = 0; i < n; i++) ea[i] = sx_expand(se->args[i]);

    if (op == SX_ADD)
        return sx_simplify(sx_make_expr(SX_ADD, n, ea));

    if (op == SX_SUB && n == 2) {
        /* Distribute: A − B  →  A + (−B), pushing neg into sums */
        val_t neg_b = sx_expand(sx_neg(ea[1]));
        return sx_simplify(sx_add(ea[0], neg_b));
    }

    /* Push NEG into ADD */
    if (op == SX_NEG && n == 1) {
        val_t inner = ea[0];
        if (vis_symexpr(inner) && as_symexpr(inner)->op == SX_ADD) {
            SymExpr *ia = as_symexpr(inner);
            int m = (int)ia->nargs;
            val_t *neg_terms = (val_t *)gc_alloc_raw_pinned((size_t)m * sizeof(val_t));
            for (int i = 0; i < m; i++) neg_terms[i] = sx_neg(ia->args[i]);
            return sx_simplify(sx_make_expr(SX_ADD, m, neg_terms));
        }
        return sx_neg(inner);
    }

    /* Distribute MUL over ADD, folding left */
    if (op == SX_MUL) {
        val_t acc = ea[0];
        for (int i = 1; i < n; i++) acc = expand_mul2(acc, ea[i]);
        return acc;
    }

    /* Distribute NCMUL over ADD preserving order */
    if (op == SX_NCMUL) {
        val_t acc = ea[0];
        for (int i = 1; i < n; i++) acc = expand_ncmul2(acc, ea[i]);
        return acc;
    }

    /* Expand integer exponents 2..16 by repeated multiplication */
    if (op == SX_EXPT && n == 2) {
        val_t base = ea[0], exp_v = ea[1];
        if (vis_fixnum(exp_v)) {
            long e = vunfix(exp_v);
            if (e == 0) return vfix(1);
            if (e == 1) return base;
            if (e >= 2 && e <= 16) {
                val_t acc = base;
                for (long i = 1; i < e; i++)
                    acc = sx_is_nc(acc) ? expand_ncmul2(acc, base) : expand_mul2(acc, base);
                return acc;
            }
        }
    }

    return sx_simplify(sx_make_expr(op, n, ea));
}

/* Internal: degree as a C long (−1 for transcendentals of var, 0 for constants). */
long sx_degree_long(val_t expr, val_t var) {
    /* Same unbounded-recursion class as sx_diff above (issue #134). */
    check_c_stack_depth("symbolic");
    if (vis_number(expr)) return 0;
    if (vis_symvar(expr))
        return (as_symvar(expr)->name == as_symvar(var)->name) ? 1 : 0;
    if (!vis_symexpr(expr)) return 0;

    SymExpr *se = as_symexpr(expr);
    val_t op = se->op;
    int n = (int)se->nargs;
    val_t *args = se->args;

    if (op == SX_ADD) {
        long mx = 0;
        for (int i = 0; i < n; i++) {
            long d = sx_degree_long(args[i], var);
            if (d > mx) mx = d;
        }
        return mx;
    }
    if (op == SX_SUB && n == 2) {
        long d0 = sx_degree_long(args[0], var);
        long d1 = sx_degree_long(args[1], var);
        return d0 > d1 ? d0 : d1;
    }
    if (op == SX_NEG && n == 1)
        return sx_degree_long(args[0], var);
    if (op == SX_MUL || op == SX_NCMUL) {
        long s = 0;
        for (int i = 0; i < n; i++) s += sx_degree_long(args[i], var);
        return s;
    }
    if (op == SX_EXPT && n == 2) {
        val_t base = args[0], exp_v = args[1];
        if (vis_fixnum(exp_v) && vunfix(exp_v) >= 0)
            return sx_degree_long(base, var) * vunfix(exp_v);
    }
    return 0;
}

val_t sx_degree(val_t expr, val_t var) {
    if (!vis_symvar(var))
        scm_raise(V_FALSE, "degree: second argument must be a symbolic variable");
    return vfix(sx_degree_long(expr, var));
}

/*
 * Decompose a single monomial term into (coefficient × var^degree).
 * Returns true and fills *coeff / *deg on success.
 * Returns false if the term is not a recognizable monomial in var.
 */
static bool decomp_monomial(val_t term, val_t var, val_t *coeff, long *deg) {
    if (vis_number(term)) { *coeff = term; *deg = 0; return true; }
    if (vis_symvar(term)) {
        if (as_symvar(term)->name == as_symvar(var)->name)
            { *coeff = vfix(1); *deg = 1; }
        else
            { *coeff = term; *deg = 0; }
        return true;
    }
    if (!vis_symexpr(term)) { *coeff = term; *deg = 0; return true; }

    SymExpr *se = as_symexpr(term);
    val_t op = se->op;
    int n = (int)se->nargs;
    val_t *args = se->args;

    if (op == SX_NEG && n == 1) {
        if (decomp_monomial(args[0], var, coeff, deg)) {
            *coeff = sx_neg(*coeff);
            return true;
        }
        return false;
    }

    /* (expt var k) */
    if (op == SX_EXPT && n == 2 &&
        sx_equal(args[0], var) && vis_fixnum(args[1])) {
        *coeff = vfix(1); *deg = vunfix(args[1]); return true;
    }

    if (op == SX_MUL) {
        long degree = 0;
        val_t coeff_acc = vfix(1);
        bool ok = true;
        for (int i = 0; i < n && ok; i++) {
            val_t f = args[i];
            if (!sx_depends_on(f, var)) {
                coeff_acc = sx_mul(coeff_acc, f);
            } else if (vis_symvar(f) &&
                       as_symvar(f)->name == as_symvar(var)->name) {
                degree += 1;
            } else if (vis_symexpr(f) && as_symexpr(f)->op == SX_EXPT &&
                       (int)as_symexpr(f)->nargs == 2 &&
                       sx_equal(as_symexpr(f)->args[0], var) &&
                       vis_fixnum(as_symexpr(f)->args[1])) {
                degree += vunfix(as_symexpr(f)->args[1]);
            } else {
                ok = false;
            }
        }
        if (ok) { *coeff = coeff_acc; *deg = degree; return true; }
    }

    if (!sx_depends_on(term, var)) { *coeff = term; *deg = 0; return true; }
    return false;
}

val_t sx_collect(val_t expr, val_t var) {
    if (!vis_symvar(var))
        scm_raise(V_FALSE, "collect: second argument must be a symbolic variable");

    val_t expanded = sx_expand(expr);

    /* Flatten additive terms */
    int nterms;
    val_t *terms;
    val_t single[1];
    if (vis_symexpr(expanded) && as_symexpr(expanded)->op == SX_ADD) {
        SymExpr *se = as_symexpr(expanded);
        nterms = (int)se->nargs;
        terms  = se->args;
    } else {
        single[0] = expanded; nterms = 1; terms = single;
    }

    /* degree → accumulated coefficient table */
    long  *degs    = (long *)gc_alloc_raw_pinned_atomic((size_t)nterms * sizeof(long));
    val_t *coeffs  = (val_t *)gc_alloc_raw_pinned((size_t)nterms * sizeof(val_t));
    val_t *unc     = (val_t *)gc_alloc_raw_pinned((size_t)nterms * sizeof(val_t));
    int ndeg = 0, nunc = 0;

    for (int i = 0; i < nterms; i++) {
        val_t c; long d;
        if (decomp_monomial(terms[i], var, &c, &d)) {
            int found = -1;
            for (int j = 0; j < ndeg; j++) if (degs[j] == d) { found = j; break; }
            if (found >= 0) coeffs[found] = sx_add(coeffs[found], c);
            else { degs[ndeg] = d; coeffs[ndeg] = c; ndeg++; }
        } else {
            unc[nunc++] = terms[i];
        }
    }

    /* Sort by descending degree */
    for (int i = 0; i < ndeg - 1; i++) {
        for (int j = 0; j < ndeg - i - 1; j++) {
            if (degs[j] < degs[j+1]) {
                long td = degs[j]; degs[j] = degs[j+1]; degs[j+1] = td;
                val_t tc = coeffs[j]; coeffs[j] = coeffs[j+1]; coeffs[j+1] = tc;
            }
        }
    }

    /* Build result terms */
    val_t *result = (val_t *)gc_alloc_raw_pinned((size_t)(ndeg + nunc) * sizeof(val_t));
    int ri = 0;
    for (int i = 0; i < ndeg; i++) {
        val_t c = coeffs[i];
        long d  = degs[i];
        if (vis_number(c) && num_is_zero(c)) continue;
        if (d == 0)      result[ri++] = c;
        else if (d == 1) result[ri++] = is_one(c) ? var : sx_mul(c, var);
        else {
            val_t vp = sx_expt(var, vfix(d));
            result[ri++] = is_one(c) ? vp : sx_mul(c, vp);
        }
    }
    for (int i = 0; i < nunc; i++) result[ri++] = unc[i];

    if (ri == 0) return vfix(0);
    if (ri == 1) return result[0];
    return sx_simplify(sx_make_expr(SX_ADD, ri, result));
}

val_t sx_leading_coeff(val_t expr, val_t var) {
    if (!vis_symvar(var))
        scm_raise(V_FALSE, "leading-coeff: second argument must be a symbolic variable");

    long target = sx_degree_long(expr, var);
    val_t expanded = sx_expand(expr);

    int nterms;
    val_t *terms;
    val_t single[1];
    if (vis_symexpr(expanded) && as_symexpr(expanded)->op == SX_ADD) {
        SymExpr *se = as_symexpr(expanded);
        nterms = (int)se->nargs;
        terms  = se->args;
    } else {
        single[0] = expanded; nterms = 1; terms = single;
    }

    val_t acc = vfix(0);
    for (int i = 0; i < nterms; i++) {
        val_t c; long d;
        if (decomp_monomial(terms[i], var, &c, &d) && d == target)
            acc = sx_add(acc, c);
    }
    return acc;
}

/* ---- Symbolic integration ---- */

val_t sx_integrate(val_t expr, val_t var) {
    /* Same unbounded-recursion class as sx_diff above (issue #134). */
    check_c_stack_depth("symbolic");
    if (!vis_symvar(var))
        scm_raise(V_FALSE, "∫: second argument must be a symbolic variable");

    /* ∫c dx = c*x */
    if (vis_number(expr))
        return sx_mul(expr, var);

    /* ∫x dx = x²/2,   ∫y dx = y*x  (y is constant wrt x) */
    if (vis_symvar(expr)) {
        if (as_symvar(expr)->name == as_symvar(var)->name)
            return sx_div(sx_expt(var, vfix(2)), vfix(2));
        return sx_mul(expr, var);
    }

    if (!vis_symexpr(expr)) return sx_mul(expr, var);

    SymExpr *se = as_symexpr(expr);
    val_t op = se->op;
    int n = (int)se->nargs;
    val_t *args = se->args;

    /* Linearity: ∫(f+g+...) = ∫f + ∫g + ... */
    if (op == SX_ADD) {
        val_t *iargs = (val_t *)gc_alloc_raw_pinned((size_t)n * sizeof(val_t));
        for (int i = 0; i < n; i++) iargs[i] = sx_integrate(args[i], var);
        return sx_simplify(sx_make_expr(SX_ADD, n, iargs));
    }

    /* ∫(a - b) = ∫a - ∫b */
    if (op == SX_SUB && n == 2)
        return sx_sub(sx_integrate(args[0], var), sx_integrate(args[1], var));

    /* ∫(-f) = -(∫f) */
    if (op == SX_NEG && n == 1)
        return sx_neg(sx_integrate(args[0], var));

    /* Constant multiple: pull out factors that don't depend on var */
    if (op == SX_MUL) {
        int ndep = 0, nconst = 0;
        for (int i = 0; i < n; i++) {
            if (sx_depends_on(args[i], var)) ndep++;
            else nconst++;
        }
        if (nconst == n)
            return sx_mul(expr, var);
        if (nconst > 0) {
            val_t *consts = (val_t *)gc_alloc_raw_pinned((size_t)nconst * sizeof(val_t));
            val_t *deps   = (val_t *)gc_alloc_raw_pinned((size_t)ndep   * sizeof(val_t));
            int ci = 0, di = 0;
            for (int i = 0; i < n; i++) {
                if (sx_depends_on(args[i], var)) deps[di++] = args[i];
                else consts[ci++] = args[i];
            }
            val_t cfactor = (nconst == 1) ? consts[0] : sx_make_expr(SX_MUL, nconst, consts);
            val_t dpart   = (ndep   == 1) ? deps[0]   : sx_make_expr(SX_MUL, ndep, deps);
            return sx_mul(cfactor, sx_integrate(dpart, var));
        }
        /* All factors depend on var: try IBP.
         * Priority: log factors (LIATE rule — differentiate log, integrate rest);
         * then polynomial (x^n) factors (differentiate poly, integrate rest). */
        {
            int log_idx = -1;
            int poly_idx = -1;
            for (int i = 0; i < n; i++) {
                val_t a = args[i];
                /* LOG factor: use LIATE — differentiate log, integrate algebraic */
                if (log_idx < 0 && vis_symexpr(a) &&
                    as_symexpr(a)->op == SX_LOG && as_symexpr(a)->nargs == 1)
                    log_idx = i;
                /* Polynomial factor: var itself or var^k for positive integer k */
                if (poly_idx < 0) {
                    if (sx_equal(a, var)) {
                        poly_idx = i;
                    } else if (vis_symexpr(a) &&
                               as_symexpr(a)->op == SX_EXPT &&
                               as_symexpr(a)->nargs == 2 &&
                               sx_equal(as_symexpr(a)->args[0], var) &&
                               vis_fixnum(as_symexpr(a)->args[1]) &&
                               vunfix(as_symexpr(a)->args[1]) > 0) {
                        poly_idx = i;
                    }

                }
            }
            /* Choose which factor to differentiate (u) and which to integrate (f) */
            int u_idx = -1;
            if (log_idx >= 0)  u_idx = log_idx;
            else if (poly_idx >= 0) u_idx = poly_idx;

            /* Direct formula for log × var^k: avoids unsimplified intermediate fractions.
             * ∫ var^k · ln(f) dx = var^(k+1)·ln(f)/(k+1) - ∫ var^(k+1)/(k+1) · f'/f dx */
            if (log_idx >= 0 && poly_idx >= 0 && n == 2) {
                val_t log_expr = args[log_idx];
                val_t log_arg  = as_symexpr(log_expr)->args[0];
                val_t poly_f   = args[poly_idx];
                /* Determine k: var → 1, expt(var, k) → k */
                long k;
                if (sx_equal(poly_f, var)) {
                    k = 1;
                } else {
                    k = vunfix(as_symexpr(poly_f)->args[1]);
                }
                val_t kp1    = vfix(k + 1);
                val_t kp1_sq = num_mul(kp1, kp1);
                val_t vpow   = sx_expt(var, kp1);
                /* ∫ var^(k+1) * f'/(f*(k+1)) dx — try integration */
                val_t df_log = sx_simplify(sx_diff(log_arg, var));
                if (vis_number(df_log) && !num_is_zero(df_log)) {
                    /* inner integrand = var^(k+1) * df_log / (log_arg * (k+1)) */
                    val_t tail_expr = sx_div(sx_mul(vpow, df_log),
                                            sx_mul(log_arg, kp1));
                    val_t tail = sx_integrate(tail_expr, var);
                    if (!(vis_symexpr(tail) && as_symexpr(tail)->op == SX_INTEGRATE)) {
                        val_t head = sx_div(sx_mul(vpow, log_expr), kp1);
                        return sx_simplify(sx_sub(head, tail));
                    }
                    /* Fallback for log(x): direct closed form */
                    if (sx_equal(log_arg, var)) {
                        /* ∫ x^k · ln(x) dx = x^(k+1)·ln(x)/(k+1) - x^(k+1)/(k+1)^2 */
                        val_t head2 = sx_div(sx_mul(vpow, log_expr), kp1);
                        val_t tail2 = sx_div(vpow, kp1_sq);
                        return sx_simplify(sx_sub(head2, tail2));
                    }
                }
            }

            if (u_idx >= 0) {
                val_t u = args[u_idx];
                /* f_part = product of all factors except u */
                val_t f_part;
                if (n == 2) {
                    f_part = args[1 - u_idx];
                } else {
                    val_t *rargs = (val_t *)gc_alloc_raw_pinned((size_t)(n - 1) * sizeof(val_t));
                    int ri = 0;
                    for (int i = 0; i < n; i++)
                        if (i != u_idx) rargs[ri++] = args[i];
                    f_part = sx_make_expr(SX_MUL, n - 1, rargs);
                }
                /* v = ∫f_part dx */
                val_t v = sx_integrate(f_part, var);
                /* Guard: if v is still unevaluated, give up */
                if (!(vis_symexpr(v) && as_symexpr(v)->op == SX_INTEGRATE)) {
                    /* IBP: ∫u·f dx = u·v - ∫v·du dx
                     * Multiply fractions explicitly to avoid unsimplified (a/b)*(c/d) */
                    val_t du = sx_simplify(sx_diff(u, var));
                    if (vis_number(du) && num_is_zero(du))
                        return sx_mul(u, v);
                    val_t inner;
                    if (vis_symexpr(v) && as_symexpr(v)->op == SX_DIV &&
                        as_symexpr(v)->nargs == 2 &&
                        vis_symexpr(du) && as_symexpr(du)->op == SX_DIV &&
                        as_symexpr(du)->nargs == 2) {
                        /* (a/b) · (c/d) → (a*c)/(b*d) */
                        val_t vn = as_symexpr(v)->args[0], vd = as_symexpr(v)->args[1];
                        val_t dn = as_symexpr(du)->args[0], dd = as_symexpr(du)->args[1];
                        inner = sx_simplify(sx_div(sx_simplify(sx_mul(vn, dn)),
                                                   sx_simplify(sx_mul(vd, dd))));
                    } else {
                        inner = sx_simplify(sx_mul(v, du));
                    }
                    val_t tail  = sx_integrate(inner, var);
                    return sx_simplify(sx_sub(sx_mul(u, v), tail));
                }
            }
        }
    }

    /* NCMUL: factor out a leading real-scalar constant, leave quaternion-prefixed
       products unevaluated (quaternion constants don't commute past the integrand) */
    if (op == SX_NCMUL) {
        /* Partition factors into a leading constant block, a variable block, and
           a trailing constant block.  If the variable block integrates cleanly,
           return  leading ⊗ ∫(var_block) ⊗ trailing. */
        int first_var = n, last_var = -1;
        for (int i = 0; i < n; i++) {
            if (sx_depends_on(args[i], var)) {
                if (i < first_var) first_var = i;
                last_var = i;
            }
        }
        if (first_var == n) {
            /* No factors depend on var: whole product is constant */
            val_t whole = n == 1 ? args[0] : sx_make_expr(SX_NCMUL, n, args);
            return sx_ncmul(whole, var);
        }
        if (first_var > 0 || last_var < n - 1) {
            /* Build the var-dependent sub-product and integrate it */
            int vn = last_var - first_var + 1;
            val_t var_part = vn == 1 ? args[first_var]
                           : sx_make_expr(SX_NCMUL, vn, args + first_var);
            val_t ivar = sx_integrate(var_part, var);
            if (!vis_symexpr(ivar) || as_symexpr(ivar)->op != SX_INTEGRATE) {
                /* leading ⊗ ivar ⊗ trailing */
                val_t result = ivar;
                if (last_var < n - 1) {
                    val_t *trail = args + last_var + 1;
                    int tn = n - last_var - 1;
                    val_t tail = tn == 1 ? trail[0] : sx_make_expr(SX_NCMUL, tn, trail);
                    result = sx_ncmul(result, tail);
                }
                if (first_var > 0) {
                    val_t head = first_var == 1 ? args[0]
                               : sx_make_expr(SX_NCMUL, first_var, args);
                    result = sx_ncmul(head, result);
                }
                return result;
            }
        }
    }

    /* Power rule: ∫f^n dx where n is numeric */
    if (op == SX_EXPT && n == 2) {
        val_t base = args[0], exp_v = args[1];
        if (!sx_depends_on(exp_v, var) && vis_number(exp_v)) {
            /* ∫sec²(f) dx = tan(f)/f',  ∫csc²(f) dx = -cot(f)/f'
             * ∫sin²(f) dx = x/2 - sin(2f)/(4f'),  ∫cos²(f) dx = x/2 + sin(2f)/(4f') */
            if (vis_symexpr(base) && num_eq(exp_v, vfix(2))) {
                val_t bop = as_symexpr(base)->op;
                if (as_symexpr(base)->nargs == 1) {
                    val_t f  = as_symexpr(base)->args[0];
                    val_t df = sx_diff(f, var);
                    if (vis_number(df) && !num_is_zero(df)) {
                        if (bop == SX_SEC)
                            return sx_div(sx_tan(f), df);
                        if (bop == SX_CSC)
                            return sx_div(sx_neg(sx_cot(f)), df);
                        if (bop == SX_SIN) {
                            val_t sin2f = sx_sin(sx_mul(vfix(2), f));
                            return sx_sub(sx_div(var, vfix(2)),
                                          sx_div(sin2f, sx_mul(vfix(4), df)));
                        }
                        if (bop == SX_COS) {
                            val_t sin2f = sx_sin(sx_mul(vfix(2), f));
                            return sx_add(sx_div(var, vfix(2)),
                                          sx_div(sin2f, sx_mul(vfix(4), df)));
                        }
                    }
                }
            }
            if (sx_equal(base, var)) {
                /* ∫x^n dx */
                if (num_eq(exp_v, vfix(-1)))
                    return sx_log(sx_abs(var));
                val_t np1 = num_add(exp_v, vfix(1));
                return sx_div(sx_expt(var, np1), np1);
            }
            /* ∫(ax+b)^n dx via linear substitution */
            val_t df = sx_diff(base, var);
            if (vis_number(df) && !num_is_zero(df)) {
                if (num_eq(exp_v, vfix(-1)))
                    return sx_div(sx_log(sx_abs(base)), df);
                val_t np1 = num_add(exp_v, vfix(1));
                return sx_div(sx_expt(base, np1), sx_mul(df, np1));
            }
        }
    }

    /* ∫ num/den dx — linear and quadratic denominator cases */
    if (op == SX_DIV && n == 2) {
        val_t num_v = args[0], den = args[1];
        if (!sx_depends_on(num_v, var)) {
            /* Linear: f' is constant → ln|f|/f' */
            val_t df = sx_diff(den, var);
            if (vis_number(df) && !num_is_zero(df))
                return sx_mul(sx_div(num_v, df), sx_log(sx_abs(den)));

            /* Quadratic: extract a, b, c from ax²+bx+c via point evaluation */
            if (sx_depends_on(den, var)) {
                val_t deg = sx_degree(den, var);
                if (vis_fixnum(deg) && vunfix(deg) == 2) {
                    val_t at0  = sx_simplify(sx_substitute(den, var, vfix(0)));
                    val_t at1  = sx_simplify(sx_substitute(den, var, vfix(1)));
                    val_t atn1 = sx_simplify(sx_substitute(den, var, vfix(-1)));
                    if (vis_number(at0) && vis_number(at1) && vis_number(atn1)) {
                        val_t c_v = at0;
                        val_t b_v = num_div(num_sub(at1, atn1), vfix(2));
                        val_t a_v = num_sub(num_sub(at1, b_v), c_v);
                        /* disc = 4ac - b² */
                        val_t disc = num_sub(num_mul(num_mul(vfix(4), a_v), c_v),
                                             num_mul(b_v, b_v));
                        if (vis_number(disc)) {
                            double disc_d = num_to_double(disc);
                            if (disc_d > 0.0) {
                                /* ∫ num/(ax²+bx+c) dx = 2·num/√disc · atan((2ax+b)/√disc) */
                                val_t sq = sx_sqrt(disc);
                                val_t inner = sx_div(sx_add(sx_mul(num_mul(vfix(2), a_v), var), b_v), sq);
                                return sx_mul(sx_div(num_mul(vfix(2), num_v), sq),
                                              sx_atan(inner));
                            }
                        }
                    }
                }
            }
        } else if (!sx_depends_on(den, var)) {
            /* ∫ f(x)/c dx = (1/c) * ∫ f(x) dx  (constant denominator) */
            val_t inner = sx_integrate(num_v, var);
            if (!(vis_symexpr(inner) && as_symexpr(inner)->op == SX_INTEGRATE))
                return sx_div(inner, den);
        }
    }

    /* ∫sin(f) dx = -cos(f) / f'  (linear f) */
    if (op == SX_SIN && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df))
            return sx_div(sx_neg(sx_cos(args[0])), df);
    }

    /* ∫cos(f) dx = sin(f) / f'  (linear f) */
    if (op == SX_COS && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df))
            return sx_div(sx_sin(args[0]), df);
    }

    /* ∫tan(f) dx = -ln|cos(f)| / f'  (linear f) */
    if (op == SX_TAN && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df))
            return sx_div(sx_neg(sx_log(sx_abs(sx_cos(args[0])))), df);
    }

    /* ∫exp(f) dx = exp(f) / f'  (linear f) */
    if (op == SX_EXP && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df))
            return sx_div(expr, df);
    }

    /* ∫ln(f) dx = (f*ln(f) - f) / f'  (linear f, from integration by parts) */
    if (op == SX_LOG && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df)) {
            val_t f = args[0];
            return sx_div(sx_sub(sx_mul(f, sx_log(f)), f), df);
        }
    }

    /* ∫sqrt(f) dx = 2*f^(3/2) / (3*f')  (linear f) */
    if (op == SX_SQRT && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df)) {
            val_t three_halves = num_div(vfix(3), vfix(2));
            return sx_div(sx_mul(vfix(2), sx_expt(args[0], three_halves)),
                          sx_mul(vfix(3), df));
        }
    }

    /* ---- Complex operators (x is a real variable) ---- */
    /* ∫conj(f) dx = conj(∫f dx) */
    if (op == SX_CONJ && n == 1)
        return sx_conj(sx_integrate(args[0], var));

    /* ∫real(f) dx = real(∫f dx) */
    if (op == SX_REAL && n == 1)
        return sx_real(sx_integrate(args[0], var));

    /* ∫imag(f) dx = imag(∫f dx) */
    if (op == SX_IMAG && n == 1)
        return sx_imag(sx_integrate(args[0], var));

    /* ---- Hyperbolic integrals (linear argument) ---- */
    if (op == SX_SINH && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df))
            return sx_div(sx_cosh(args[0]), df);
    }
    if (op == SX_COSH && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df))
            return sx_div(sx_sinh(args[0]), df);
    }
    if (op == SX_TANH && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df))
            return sx_div(sx_log(sx_cosh(args[0])), df);
    }

    /* ---- Reciprocal trig integrals (linear argument) ---- */
    if (op == SX_COT && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df))
            return sx_div(sx_log(sx_abs(sx_sin(args[0]))), df);
    }
    if (op == SX_SEC && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df))
            return sx_div(sx_log(sx_abs(sx_add(sx_sec(args[0]), sx_tan(args[0])))), df);
    }
    if (op == SX_CSC && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df))
            return sx_div(sx_neg(sx_log(sx_abs(sx_add(sx_csc(args[0]), sx_cot(args[0]))))), df);
    }

    /* ---- Inverse trig integrals — IBP results (linear argument) ---- */
    if (op == SX_ASIN && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df)) {
            val_t f   = args[0];
            val_t res = sx_add(sx_mul(f, sx_asin(f)),
                               sx_sqrt(sx_sub(vfix(1), sx_expt(f, vfix(2)))));
            return sx_div(res, df);
        }
    }
    if (op == SX_ACOS && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df)) {
            val_t f   = args[0];
            val_t res = sx_sub(sx_mul(f, sx_acos(f)),
                               sx_sqrt(sx_sub(vfix(1), sx_expt(f, vfix(2)))));
            return sx_div(res, df);
        }
    }
    if (op == SX_ATAN && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df)) {
            val_t f   = args[0];
            val_t res = sx_sub(sx_mul(f, sx_atan(f)),
                               sx_div(sx_log(sx_add(vfix(1), sx_expt(f, vfix(2)))), vfix(2)));
            return sx_div(res, df);
        }
    }
    if (op == SX_ASINH && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df)) {
            val_t f   = args[0];
            val_t res = sx_sub(sx_mul(f, sx_asinh(f)),
                               sx_sqrt(sx_add(sx_expt(f, vfix(2)), vfix(1))));
            return sx_div(res, df);
        }
    }
    if (op == SX_ACOSH && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df)) {
            val_t f   = args[0];
            val_t res = sx_sub(sx_mul(f, sx_acosh(f)),
                               sx_sqrt(sx_sub(sx_expt(f, vfix(2)), vfix(1))));
            return sx_div(res, df);
        }
    }
    if (op == SX_ATANH && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df)) {
            val_t f   = args[0];
            val_t res = sx_add(sx_mul(f, sx_atanh(f)),
                               sx_div(sx_log(sx_sub(vfix(1), sx_expt(f, vfix(2)))), vfix(2)));
            return sx_div(res, df);
        }
    }

    /* ---- Risch: rational function integration ---- */
    if (op == SX_DIV) {
        val_t rat = sx_integrate_rational(expr, var);
        if (rat != V_VOID) return rat;
    }

    /* ---- Risch: log of a polynomial ---- */
    if (op == SX_LOG && n == 1) {
        val_t logp = sx_integrate_log_poly(args[0], var);
        if (logp != V_VOID) return logp;
    }

    /* Fallback: unevaluated (∫ expr var) */
    val_t i_args[2] = {expr, var};
    return sx_make_expr(SX_INTEGRATE, 2, i_args);
}

/* ---- Fractional calculus ---- */

/*
 * sx_fracdiff: Caputo fractional derivative D^α[expr] w.r.t. var.
 *
 * Rules (α numeric):
 *   D^0 f       = f                            (identity)
 *   D^1 f       = df/dx                        (ordinary derivative)
 *   D^n f       = d^n f / dx^n  (positive integer n, iterated)
 *   D^α c       = 0                             (constants vanish, Caputo)
 *   D^α x^n     = Γ(n+1)/Γ(n−α+1) · x^(n−α)  (power rule, n ≥ 0)
 *   D^α e^(λx)  = λ^α · e^(λx)               (eigenfunction, linear argument)
 *   D^α (f+g)   = D^α f + D^α g              (linearity)
 *   D^α (c·f)   = c · D^α f                  (constant factor)
 *   D^α (D^β f) = D^(α+β) f                  (composition)
 */
val_t sx_fracdiff(val_t expr, val_t alpha, val_t var) {
    /* Same unbounded-recursion class as sx_diff above (issue #134). */
    check_c_stack_depth("symbolic");
    if (!vis_symvar(var))
        scm_raise(V_FALSE, "frac-diff: third argument must be a symbolic variable");

    /* α=0: identity */
    if (vis_number(alpha) && num_is_zero(alpha)) return expr;

    /* α=1: ordinary derivative */
    if (vis_number(alpha) && num_is_one(alpha)) return sx_diff(expr, var);

    /* positive integer α: iterate */
    if (vis_fixnum(alpha)) {
        long n = vunfix(alpha);
        if (n > 1 && n <= 20) {
            val_t r = expr;
            for (long i = 0; i < n; i++) r = sx_diff(r, var);
            return r;
        }
    }

    /* Numbers → 0 (Caputo: constants have zero fractional derivative) */
    if (vis_number(expr)) return vfix(0);

    /* Symbolic variable */
    if (vis_symvar(expr)) {
        /* D^α[x] = power rule with n=1 */
        if (as_symvar(expr)->name == as_symvar(var)->name && vis_number(alpha)) {
            double a = num_to_double(alpha);
            double coeff = tgamma(2.0) / tgamma(2.0 - a);
            val_t new_exp = num_sub(vfix(1), alpha);
            return sx_mul(num_make_float(coeff), sx_expt(var, new_exp));
        }
        /* D^α[y] = 0 for y independent of var */
        if (as_symvar(expr)->name != as_symvar(var)->name) return vfix(0);
    }

    if (!vis_symexpr(expr)) goto unevaluated;

    {
        SymExpr *se = as_symexpr(expr);
        val_t op = se->op;
        int n = (int)se->nargs;
        val_t *args = se->args;

        /* Linearity: D^α(f+g+...) */
        if (op == SX_ADD) {
            val_t *dargs = (val_t *)gc_alloc_raw_pinned((size_t)n * sizeof(val_t));
            for (int i = 0; i < n; i++) dargs[i] = sx_fracdiff(args[i], alpha, var);
            return sx_simplify(sx_make_expr(SX_ADD, n, dargs));
        }
        if (op == SX_SUB && n == 2)
            return sx_sub(sx_fracdiff(args[0], alpha, var),
                          sx_fracdiff(args[1], alpha, var));
        if (op == SX_NEG && n == 1)
            return sx_neg(sx_fracdiff(args[0], alpha, var));

        /* Constant multiple: pull out factors independent of var */
        if (op == SX_MUL) {
            int nconst = 0;
            for (int i = 0; i < n; i++)
                if (!sx_depends_on(args[i], var)) nconst++;
            if (nconst == n) return vfix(0);  /* whole product is constant */
            if (nconst > 0) {
                val_t *consts = (val_t *)gc_alloc_raw_pinned((size_t)nconst * sizeof(val_t));
                val_t *deps   = (val_t *)gc_alloc_raw_pinned((size_t)(n - nconst) * sizeof(val_t));
                int ci = 0, di = 0;
                for (int i = 0; i < n; i++) {
                    if (sx_depends_on(args[i], var)) deps[di++] = args[i];
                    else consts[ci++] = args[i];
                }
                val_t cfactor = (nconst == 1) ? consts[0]
                                              : sx_make_expr(SX_MUL, nconst, consts);
                val_t dpart   = (di == 1) ? deps[0] : sx_make_expr(SX_MUL, di, deps);
                return sx_mul(cfactor, sx_fracdiff(dpart, alpha, var));
            }
        }

        /* Power rule: D^α[x^n] = Γ(n+1)/Γ(n−α+1) · x^(n−α) */
        if (op == SX_EXPT && n == 2) {
            val_t base = args[0], exp_v = args[1];
            if (sx_equal(base, var) && vis_number(exp_v) && vis_number(alpha)) {
                double pn = num_to_double(exp_v);
                double a  = num_to_double(alpha);
                if (pn >= 0.0) {
                    double coeff = tgamma(pn + 1.0) / tgamma(pn - a + 1.0);
                    val_t new_exp = num_sub(exp_v, alpha);
                    return sx_mul(num_make_float(coeff), sx_expt(var, new_exp));
                }
            }
        }

        /* Exponential eigenfunction: D^α[e^(λx)] = λ^α · e^(λx)  (linear arg) */
        if (op == SX_EXP && n == 1 && vis_number(alpha)) {
            val_t df = sx_diff(args[0], var);  /* extract λ */
            if (vis_number(df) && !num_is_zero(df)) {
                double lambda = num_to_double(df);
                double a = num_to_double(alpha);
                return sx_mul(num_make_float(pow(lambda, a)), expr);
            }
        }

        /* Composition: D^α[D^β[f, β, x], α, x] = D^(α+β)[f, x] */
        if (op == SX_FRACDIFF && n == 3 && sx_equal(args[2], var)) {
            val_t new_alpha = sx_simplify(sx_add(alpha, args[1]));
            return sx_fracdiff(args[0], new_alpha, var);
        }
    }

unevaluated:;
    val_t fargs[3] = {expr, alpha, var};
    return sx_make_expr(SX_FRACDIFF, 3, fargs);
}

/*
 * sx_fracint: Riemann-Liouville fractional integral I^α[expr] w.r.t. var.
 *
 * Rules (α numeric):
 *   I^0 f      = f                             (identity)
 *   I^1 f      = ∫f dx                         (ordinary integral)
 *   I^α c      = c · x^α / Γ(α+1)             (constant)
 *   I^α x^n    = Γ(n+1)/Γ(n+α+1) · x^(n+α)  (power rule, n ≥ 0)
 *   I^α e^(λx) = λ^(−α) · e^(λx)             (eigenfunction, linear arg)
 *   I^α (f+g)  = I^α f + I^α g               (linearity)
 *   I^α (c·f)  = c · I^α f                   (constant factor)
 */
val_t sx_fracint(val_t expr, val_t alpha, val_t var) {
    /* Same unbounded-recursion class as sx_diff above (issue #134). */
    check_c_stack_depth("symbolic");
    if (!vis_symvar(var))
        scm_raise(V_FALSE, "frac-int: third argument must be a symbolic variable");

    /* α=0: identity */
    if (vis_number(alpha) && num_is_zero(alpha)) return expr;

    /* α=1: ordinary integral */
    if (vis_number(alpha) && num_is_one(alpha)) return sx_integrate(expr, var);

    /* positive integer α: iterate */
    if (vis_fixnum(alpha)) {
        long n = vunfix(alpha);
        if (n > 1 && n <= 20) {
            val_t r = expr;
            for (long i = 0; i < n; i++) r = sx_integrate(r, var);
            return r;
        }
    }

    /* Constant: I^α[c] = c · x^α / Γ(α+1) */
    if (vis_number(expr) && vis_number(alpha)) {
        double a = num_to_double(alpha);
        double denom = tgamma(a + 1.0);
        val_t coeff = (num_is_one(expr))
            ? num_make_float(1.0 / denom)
            : sx_div(expr, num_make_float(denom));
        return sx_mul(coeff, sx_expt(var, alpha));
    }

    /* Symbolic variable */
    if (vis_symvar(expr)) {
        if (as_symvar(expr)->name == as_symvar(var)->name && vis_number(alpha)) {
            /* I^α[x] = power rule with n=1 */
            double a  = num_to_double(alpha);
            double coeff = tgamma(2.0) / tgamma(2.0 + a);
            val_t new_exp = num_add(vfix(1), alpha);
            return sx_mul(num_make_float(coeff), sx_expt(var, new_exp));
        }
        if (as_symvar(expr)->name != as_symvar(var)->name && vis_number(alpha)) {
            /* I^α[c] where c is a different variable (treated as constant) */
            double a = num_to_double(alpha);
            double denom = tgamma(a + 1.0);
            return sx_mul(sx_div(expr, num_make_float(denom)), sx_expt(var, alpha));
        }
    }

    if (!vis_symexpr(expr)) goto unevaluated_i;

    {
        SymExpr *se = as_symexpr(expr);
        val_t op = se->op;
        int n = (int)se->nargs;
        val_t *args = se->args;

        /* Linearity: I^α(f+g+...) */
        if (op == SX_ADD) {
            val_t *iargs = (val_t *)gc_alloc_raw_pinned((size_t)n * sizeof(val_t));
            for (int i = 0; i < n; i++) iargs[i] = sx_fracint(args[i], alpha, var);
            return sx_simplify(sx_make_expr(SX_ADD, n, iargs));
        }
        if (op == SX_SUB && n == 2)
            return sx_sub(sx_fracint(args[0], alpha, var),
                          sx_fracint(args[1], alpha, var));
        if (op == SX_NEG && n == 1)
            return sx_neg(sx_fracint(args[0], alpha, var));

        /* Constant multiple */
        if (op == SX_MUL) {
            int nconst = 0;
            for (int i = 0; i < n; i++)
                if (!sx_depends_on(args[i], var)) nconst++;
            if (nconst > 0 && nconst < n) {
                val_t *consts = (val_t *)gc_alloc_raw_pinned((size_t)nconst * sizeof(val_t));
                val_t *deps   = (val_t *)gc_alloc_raw_pinned((size_t)(n - nconst) * sizeof(val_t));
                int ci = 0, di = 0;
                for (int i = 0; i < n; i++) {
                    if (sx_depends_on(args[i], var)) deps[di++] = args[i];
                    else consts[ci++] = args[i];
                }
                val_t cfactor = (nconst == 1) ? consts[0]
                                              : sx_make_expr(SX_MUL, nconst, consts);
                val_t dpart   = (di == 1) ? deps[0] : sx_make_expr(SX_MUL, di, deps);
                return sx_mul(cfactor, sx_fracint(dpart, alpha, var));
            }
            if (nconst == n && vis_number(alpha)) {
                /* whole product is constant */
                double a = num_to_double(alpha);
                return sx_mul(sx_div(expr, num_make_float(tgamma(a + 1.0))),
                              sx_expt(var, alpha));
            }
        }

        /* Power rule: I^α[x^n] = Γ(n+1)/Γ(n+α+1) · x^(n+α) */
        if (op == SX_EXPT && n == 2) {
            val_t base = args[0], exp_v = args[1];
            if (sx_equal(base, var) && vis_number(exp_v) && vis_number(alpha)) {
                double pn = num_to_double(exp_v);
                double a  = num_to_double(alpha);
                if (pn >= 0.0) {
                    double coeff = tgamma(pn + 1.0) / tgamma(pn + a + 1.0);
                    val_t new_exp = num_add(exp_v, alpha);
                    return sx_mul(num_make_float(coeff), sx_expt(var, new_exp));
                }
            }
        }

        /* Exponential eigenfunction: I^α[e^(λx)] = λ^(−α) · e^(λx) */
        if (op == SX_EXP && n == 1 && vis_number(alpha)) {
            val_t df = sx_diff(args[0], var);
            if (vis_number(df) && !num_is_zero(df)) {
                double lambda = num_to_double(df);
                double a = num_to_double(alpha);
                return sx_mul(num_make_float(pow(lambda, -a)), expr);
            }
        }

        /* Composition: I^α[I^β[f, β, x], α, x] = I^(α+β)[f, x] */
        if (op == SX_FRACINT && n == 3 && sx_equal(args[2], var)) {
            val_t new_alpha = sx_simplify(sx_add(alpha, args[1]));
            return sx_fracint(args[0], new_alpha, var);
        }
    }

unevaluated_i:;
    val_t iargs[3] = {expr, alpha, var};
    return sx_make_expr(SX_FRACINT, 3, iargs);
}

/* ---- Limits ---- */

/*
 * sx_ratio_simplify / sx_mul_for_ratio — aggressive ratio algebra used exclusively
 * by L'Hôpital to cancel the derivative quotient dp/dq.  NOT called from sx_simplify
 * to avoid rewrite loops (inverting a denominator would undo the 0·∞ rewrite).
 */
static val_t sx_ratio_simplify(val_t num, val_t den);

static val_t sx_mul_for_ratio(val_t a, val_t b) {
    /* Same unbounded-recursion class as sx_simplify above (issue #134):
     * mutually recursive with sx_ratio_simplify below, driven by nested
     * DIV/NEG structure. */
    check_c_stack_depth("symbolic");
    /* (p/q)*b → ratio_simplify(p*b, q) */
    if (vis_symexpr(a) && as_symexpr(a)->op == SX_DIV && as_symexpr(a)->nargs == 2)
        return sx_ratio_simplify(sx_mul_for_ratio(as_symexpr(a)->args[0], b),
                                 as_symexpr(a)->args[1]);
    if (vis_symexpr(a) && as_symexpr(a)->op == SX_NEG && as_symexpr(a)->nargs == 1)
        return sx_neg(sx_mul_for_ratio(as_symexpr(a)->args[0], b));
    if (vis_symexpr(b) && as_symexpr(b)->op == SX_DIV && as_symexpr(b)->nargs == 2)
        return sx_ratio_simplify(sx_mul_for_ratio(a, as_symexpr(b)->args[0]),
                                 as_symexpr(b)->args[1]);
    if (vis_symexpr(b) && as_symexpr(b)->op == SX_NEG && as_symexpr(b)->nargs == 1)
        return sx_neg(sx_mul_for_ratio(a, as_symexpr(b)->args[0]));
    return sx_simplify(sx_expr2(SX_MUL, a, b));
}

static val_t sx_ratio_simplify(val_t num, val_t den) {
    /* Same unbounded-recursion class as sx_mul_for_ratio above (issue #134). */
    check_c_stack_depth("symbolic");
    /* Pull negation from denominator */
    if (vis_symexpr(den) && as_symexpr(den)->op == SX_NEG && as_symexpr(den)->nargs == 1)
        return sx_neg(sx_ratio_simplify(num, as_symexpr(den)->args[0]));
    if (vis_number(den) && num_is_negative(den))
        return sx_neg(sx_ratio_simplify(num, num_neg(den)));
    /* Pull negation from numerator */
    if (vis_symexpr(num) && as_symexpr(num)->op == SX_NEG && as_symexpr(num)->nargs == 1)
        return sx_neg(sx_ratio_simplify(as_symexpr(num)->args[0], den));
    if (vis_number(num) && num_is_negative(num))
        return sx_neg(sx_ratio_simplify(num_neg(num), den));
    /* num/(p/q) → (num*q)/p */
    if (vis_symexpr(den) && as_symexpr(den)->op == SX_DIV && as_symexpr(den)->nargs == 2) {
        val_t p = as_symexpr(den)->args[0], q = as_symexpr(den)->args[1];
        return sx_ratio_simplify(sx_mul_for_ratio(num, q), p);
    }
    /* (p/q)/den → p/(q*den) */
    if (vis_symexpr(num) && as_symexpr(num)->op == SX_DIV && as_symexpr(num)->nargs == 2) {
        val_t p = as_symexpr(num)->args[0], q = as_symexpr(num)->args[1];
        return sx_ratio_simplify(p, sx_mul_for_ratio(q, den));
    }
    /* Cancel equal numerator and denominator */
    if (sx_equal(num, den)) return vfix(1);
    /* Factor cancellation: den appears as a factor inside a MUL numerator */
    if (vis_symexpr(num) && as_symexpr(num)->op == SX_MUL) {
        SymExpr *mul = as_symexpr(num);
        for (uint32_t i = 0; i < mul->nargs; i++) {
            if (sx_equal(mul->args[i], den)) {
                int nn = (int)mul->nargs - 1;
                if (nn == 0) return vfix(1);
                if (nn == 1) return sx_simplify((i == 0) ? mul->args[1] : mul->args[0]);
                val_t *nf = (val_t *)gc_alloc_raw_pinned((size_t)nn * sizeof(val_t));
                int k = 0;
                for (uint32_t j = 0; j < mul->nargs; j++)
                    if (j != i) nf[k++] = mul->args[j];
                return sx_simplify(sx_make_expr(SX_MUL, nn, nf));
            }
        }
    }
    /* Power cancellations */
    /* x^n / x → x^(n-1) */
    if (vis_symexpr(num) && as_symexpr(num)->op == SX_EXPT && as_symexpr(num)->nargs == 2 &&
        sx_equal(as_symexpr(num)->args[0], den))
        return sx_simplify(sx_expt(den, num_sub(as_symexpr(num)->args[1], vfix(1))));
    /* x / x^n → x^(1-n) */
    if (vis_symexpr(den) && as_symexpr(den)->op == SX_EXPT && as_symexpr(den)->nargs == 2 &&
        sx_equal(num, as_symexpr(den)->args[0]))
        return sx_simplify(sx_expt(num, num_sub(vfix(1), as_symexpr(den)->args[1])));
    /* x^n / x^m → x^(n-m) */
    if (vis_symexpr(num) && as_symexpr(num)->op == SX_EXPT && as_symexpr(num)->nargs == 2 &&
        vis_symexpr(den) && as_symexpr(den)->op == SX_EXPT && as_symexpr(den)->nargs == 2 &&
        sx_equal(as_symexpr(num)->args[0], as_symexpr(den)->args[0]))
        return sx_simplify(sx_expt(as_symexpr(num)->args[0],
                                   num_sub(as_symexpr(num)->args[1], as_symexpr(den)->args[1])));
    return sx_simplify(sx_expr2(SX_DIV, num, den));
}

/*
 * sx_limit_inner: recursive worker.  depth guards against infinite L'Hôpital loops.
 *
 * Strategy:
 *  1. Direct substitution — if result is a finite number, return it.
 *  2. For expr = p/q (SX_DIV):
 *       evaluate p and q at point separately (avoiding the 0/0→0 shortcut in simplify).
 *       - p=0, q≠0           → 0
 *       - p≠0, q=0            → ±∞  (unevaluated if sign is ambiguous)
 *       - finite p, infinite q → 0
 *       - 0/0 or ∞/∞          → L'Hôpital: retry limit(p'/q', x, point)
 *  3. Fallback: unevaluated (limit expr var point).
 */

#define LHOPITAL_MAX 5

static val_t sx_limit_inner(val_t expr, val_t var, val_t point, int dir, int depth);

static val_t sx_limit_unevaluated(val_t expr, val_t var, val_t point) {
    val_t args[3] = {expr, var, point};
    return sx_make_expr(SX_LIMIT, 3, args);
}

static val_t sx_limit_inner(val_t expr, val_t var, val_t point, int dir, int depth) {
    /* LHOPITAL_MAX below only bounds the L'Hopital-iteration count
     * (depth+1 calls); the separate tree-structural recursion into each
     * subexpression (la[i] = sx_limit_inner(args[i], ...), same depth)
     * is bounded only by expression NESTING, which is unbounded --
     * issue #134's same class as sx_diff/sx_simplify above. */
    check_c_stack_depth("symbolic");
    if (depth > LHOPITAL_MAX)
        return sx_limit_unevaluated(expr, var, point);

    /* Constant w.r.t. var */
    if (!sx_depends_on(expr, var))
        return sx_simplify(expr);

    /* Dispatch on form */
    if (vis_symexpr(expr)) {
        SymExpr *se = as_symexpr(expr);
        val_t op = se->op;
        int n = (int)se->nargs;
        val_t *args = se->args;

        /* p/q — handle 0/0 and ∞/∞ via L'Hôpital */
        if (op == SX_DIV && n == 2) {
            val_t p = args[0], q = args[1];
            val_t pv = sx_simplify(sx_substitute(p, var, point));
            val_t qv = sx_simplify(sx_substitute(q, var, point));

            bool p_zero  = vis_number(pv) && num_is_zero(pv);
            bool q_zero  = vis_number(qv) && num_is_zero(qv);
            bool p_inf   = vis_flonum(pv) && isinf(num_to_double(pv));
            bool q_inf   = vis_flonum(qv) && isinf(num_to_double(qv));
            bool p_fin   = vis_number(pv) && !p_inf;
            bool q_fin   = vis_number(qv) && !q_inf;

            if (p_zero && q_zero) {
                /* 0/0: L'Hôpital */
                val_t dp = sx_simplify(sx_diff(p, var));
                val_t dq = sx_simplify(sx_diff(q, var));
                return sx_limit_inner(sx_ratio_simplify(dp, dq), var, point, dir, depth + 1);
            }
            if (p_inf && q_inf) {
                /* ∞/∞: L'Hôpital */
                val_t dp = sx_simplify(sx_diff(p, var));
                val_t dq = sx_simplify(sx_diff(q, var));
                return sx_limit_inner(sx_ratio_simplify(dp, dq), var, point, dir, depth + 1);
            }
            if (p_fin && q_inf)
                return vfix(0);  /* finite/∞ = 0 */
            if (p_zero && q_fin && !q_zero)
                return vfix(0);
            if (p_fin && q_fin && !q_zero)
                return sx_simplify(sx_div(pv, qv));
        }

        /* EXPT: handle indeterminate power forms 1^∞, 0^0, ∞^0 */
        if (op == SX_EXPT && n == 2) {
            val_t base = args[0], expo = args[1];
            val_t bv = sx_simplify(sx_substitute(base, var, point));
            val_t ev = sx_simplify(sx_substitute(expo, var, point));
            bool b_one  = is_one(bv);
            bool b_zero = vis_number(bv) && num_is_zero(bv);
            bool b_inf  = vis_flonum(bv) && isinf(num_to_double(bv));
            bool e_zero = vis_number(ev) && num_is_zero(ev);
            bool e_inf  = vis_flonum(ev) && isinf(num_to_double(ev));
            /* Indeterminate power: rewrite f^g = exp(g * log(f)), take limit of exponent */
            if ((b_one && e_inf) || (b_zero && e_zero) || (b_inf && e_zero)) {
                val_t exponent = sx_mul(expo, sx_log(base));
                val_t lim_exp = sx_limit_inner(exponent, var, point, dir, depth + 1);
                if (vis_number(lim_exp) && !(vis_flonum(lim_exp) && isnan(num_to_double(lim_exp)))) {
                    val_t res = sx_simplify(sx_exp(lim_exp));
                    /* Coerce flonum integer results (e.g. 1.0 → 1) for exact equality */
                    if (vis_flonum(res)) {
                        double d = num_to_double(res), fl = floor(d);
                        if (d == fl && d >= -9.0e18 && d <= 9.0e18)
                            res = vfix((long long)d);
                    }
                    return res;
                }
            }
            /* Non-indeterminate: both limits are determinate numbers */
            if (vis_number(bv) && vis_number(ev) &&
                !(vis_flonum(bv) && isnan(num_to_double(bv))) &&
                !(vis_flonum(ev) && isnan(num_to_double(ev))))
                return sx_simplify(num_expt(bv, ev));
        }

        /* ADD/SUB/MUL/NEG: substitute into each subterm and reassemble */
        if (op == SX_ADD) {
            val_t *la = (val_t *)gc_alloc_raw_pinned((size_t)n * sizeof(val_t));
            for (int i = 0; i < n; i++)
                la[i] = sx_limit_inner(args[i], var, point, dir, depth);
            return sx_simplify(sx_make_expr(SX_ADD, n, la));
        }
        if (op == SX_SUB && n == 2)
            return sx_simplify(sx_sub(sx_limit_inner(args[0], var, point, dir, depth),
                                      sx_limit_inner(args[1], var, point, dir, depth)));
        if (op == SX_NEG && n == 1)
            return sx_simplify(sx_neg(sx_limit_inner(args[0], var, point, dir, depth)));
        if (op == SX_MUL) {
            val_t *la = (val_t *)gc_alloc_raw_pinned((size_t)n * sizeof(val_t));
            for (int i = 0; i < n; i++)
                la[i] = sx_limit_inner(args[i], var, point, dir, depth);
            /* 0·∞: detect after taking individual limits and rewrite as ratio.
             * Heuristic: put the simpler (sym-var) factor in the denominator.
             * If the ∞ factor is a sym-var → use f/(1/g) = 0/0 form (faster convergence).
             * Otherwise → use g/(1/f) = ∞/∞ form. */
            for (int i = 0; i < n && n == 2; i++) {
                int j = 1 - i;
                bool li_zero = vis_number(la[i]) && num_is_zero(la[i]);
                bool lj_inf  = vis_flonum(la[j]) && isinf(num_to_double(la[j]));
                if (li_zero && lj_inf) {
                    val_t rewritten;
                    if (vis_symvar(args[j]))
                        /* ∞ factor is a sym-var: f/(1/g) = 0/0 form */
                        rewritten = sx_div(args[i], sx_div(vfix(1), args[j]));
                    else
                        /* default: g/(1/f) = ∞/∞ form */
                        rewritten = sx_div(args[j], sx_div(vfix(1), args[i]));
                    return sx_limit_inner(rewritten, var, point, dir, depth + 1);
                }
            }
            return sx_simplify(sx_make_expr(SX_MUL, n, la));
        }
    }

    /* Direct substitution fallback */
    val_t sub = sx_simplify(sx_substitute(expr, var, point));
    if (vis_number(sub)) {
        if (vis_flonum(sub) && isnan(num_to_double(sub)))
            return sx_limit_unevaluated(expr, var, point);
        return sub;
    }
    /* Still symbolic after substitution — unevaluated */
    if (!sx_depends_on(sub, var))
        return sub;
    return sx_limit_unevaluated(expr, var, point);
}

val_t sx_limit(val_t expr, val_t var, val_t point, int dir) {
    if (!vis_symvar(var))
        scm_raise(V_FALSE, "limit: second argument must be a symbolic variable");
    return sx_limit_inner(expr, var, point, dir, 0);
}

/* ---- Taylor series ---- */

/* Coerce a flonum with an integer value to a fixnum so that subsequent
 * num_div produces exact rationals (e.g. exp(0)=1.0 → 1 → 1/6 not 0.1667). */
static val_t series_coerce_exact(val_t v) {
    if (!vis_flonum(v)) return v;
    double d = num_to_double(v);
    double fl = floor(d);
    if (d == fl && d >= (double)INTPTR_MIN && d <= (double)INTPTR_MAX)
        return vfix((intptr_t)fl);
    return v;
}

val_t sx_series(val_t expr, val_t var, val_t point, int n) {
    if (!vis_symvar(var))
        scm_raise(V_FALSE, "series: second argument must be a symbolic variable");
    if (n < 0) n = 0;

    val_t *terms = (val_t *)gc_alloc_raw_pinned((size_t)(n + 1) * sizeof(val_t));
    int nterms = 0;
    val_t fk = expr;
    val_t fact = vfix(1);  /* k! */

    for (int k = 0; k <= n; k++) {
        if (k > 0) {
            fk = sx_simplify(sx_diff(fk, var));
            fact = num_mul(fact, vfix(k));
        }

        /* Evaluate f^(k)(point), falling back to limit on exception or 0/0 */
        val_t ck;
        {
            ExnHandler _sh;
            bool _subst_ok = false;
            SCM_PROTECT(_sh, {
                ck = sx_simplify(sx_substitute(fk, var, point));
                _subst_ok = true;
            }, { (void)0; });
            if (!_subst_ok) {
                ck = sx_limit(fk, var, point, 0);
            } else if (vis_symexpr(ck) && as_symexpr(ck)->op == SX_DIV) {
                /* 0/0: check if denominator is zero at point, guarding against poles */
                bool _denom_zero = false;
                ExnHandler _sh2;
                SCM_PROTECT(_sh2, {
                    _denom_zero = num_is_zero(sx_simplify(
                        sx_substitute(as_symexpr(ck)->args[1], var, point)));
                }, { (void)0; });
                if (_denom_zero) ck = sx_limit(fk, var, point, 0);
            }
        }
        if (vis_number(ck) && num_is_zero(ck)) continue;

        /* Promote integer-valued flonums to fixnum for exact rational output */
        ck = series_coerce_exact(ck);

        val_t coeff = vis_number(ck) ? num_div(ck, fact) : sx_div(ck, fact);
        if (vis_number(coeff) && num_is_zero(coeff)) continue;

        val_t term;
        if (k == 0) {
            term = coeff;
        } else {
            /* sx_sub(var, 0) simplifies to var automatically */
            val_t xma = sx_sub(var, point);
            val_t pw = (k == 1) ? xma : sx_expt(xma, vfix(k));
            term = sx_mul(coeff, pw);
        }
        terms[nterms++] = term;
    }

    if (nterms == 0) return vfix(0);
    if (nterms == 1) return terms[0];
    return sx_simplify(sx_make_expr(SX_ADD, nterms, terms));
}

/* ---- Integral transforms ---- */

/* Helper: test if two sym-vars have the same name */
static bool sx_same_var(val_t a, val_t b) {
    return vis_symvar(a) && vis_symvar(b) &&
           as_symvar(a)->name == as_symvar(b)->name;
}

/* Create the Laplace-transform function of fn: name "L_<fn_name>", same params
 * but with t replaced by s in the params list. */
static val_t sx_laplace_fn(val_t fn, val_t t_var, val_t s_var) {
    SymFn  *sf     = as_symfn(fn);
    Symbol *fn_sym = as_sym(sf->name);
    char    buf[256];
    snprintf(buf, sizeof(buf), "L_%.*s", (int)fn_sym->len, fn_sym->data);
    /* Build new params: replace t with s */
    val_t old_params = sf->params, new_params = V_NIL;
    val_t *tail = &new_params;
    while (vis_pair(old_params)) {
        val_t p = vcar(old_params);
        Pair *cell = CURRY_NEW(Pair);
        cell->hdr.type = T_PAIR; cell->hdr.flags = 0;
        cell->car = sx_same_var(p, t_var) ? s_var : p;
        cell->cdr = V_NIL;
        *tail = vptr(cell); tail = &cell->cdr;
        old_params = vcdr(old_params);
    }
    return sx_make_fn(sym_intern_cstr(buf), new_params);
}

/* Replace t_var with s_var in the arg list of a SX_APPLY node */
static void sx_replace_var_in_args(val_t *dst, val_t *src, int n,
                                   val_t t_var, val_t s_var) {
    for (int i = 0; i < n; i++)
        dst[i] = sx_same_var(src[i], t_var) ? s_var : src[i];
}

/* Extract the linear coefficient of var in expr, i.e. return a such that
 * expr == a*var + b, or return V_FALSE if not of that form. */
static val_t sx_linear_coeff(val_t expr, val_t var) {
    if (sx_same_var(expr, var)) return vfix(1);
    if (!vis_symexpr(expr)) return V_FALSE;
    SymExpr *se = as_symexpr(expr);
    if (se->op == SX_MUL && se->nargs == 2) {
        if (sx_same_var(se->args[0], var) && !sx_depends_on(se->args[1], var))
            return se->args[1];
        if (sx_same_var(se->args[1], var) && !sx_depends_on(se->args[0], var))
            return se->args[0];
    }
    return V_FALSE;
}

val_t sx_laplace(val_t expr, val_t t_var, val_t s_var) {
    /* Same unbounded-recursion class as sx_diff above (issue #134). */
    check_c_stack_depth("symbolic");
    if (!vis_symvar(t_var))
        scm_raise(V_FALSE, "laplace: t argument must be a symbolic variable");
    if (!vis_symvar(s_var))
        scm_raise(V_FALSE, "laplace: s argument must be a symbolic variable");

    /* Constant (doesn't depend on t): L{c} = c/s */
    if (!sx_depends_on(expr, t_var))
        return sx_div(expr, s_var);

    /* The t variable itself: L{t} = 1/s^2 */
    if (sx_same_var(expr, t_var))
        return sx_div(vfix(1), sx_expt(s_var, vfix(2)));

    if (vis_symexpr(expr)) {
        SymExpr *se = as_symexpr(expr);
        val_t    op = se->op;
        int      n  = (int)se->nargs;
        val_t   *a  = se->args;

        /* Linearity: L{f+g} = L{f}+L{g} */
        if (op == SX_ADD) {
            val_t acc = vfix(0);
            for (int i = 0; i < n; i++)
                acc = sx_add(acc, sx_laplace(a[i], t_var, s_var));
            return acc;
        }
        if (op == SX_SUB && n == 2)
            return sx_sub(sx_laplace(a[0], t_var, s_var),
                          sx_laplace(a[1], t_var, s_var));
        if (op == SX_NEG && n == 1)
            return sx_neg(sx_laplace(a[0], t_var, s_var));

        /* Constant multiple: L{c*f} = c*L{f} */
        if ((op == SX_MUL || op == SX_NCMUL) && n == 2) {
            if (!sx_depends_on(a[0], t_var))
                return sx_mul(a[0], sx_laplace(a[1], t_var, s_var));
            if (!sx_depends_on(a[1], t_var))
                return sx_mul(a[1], sx_laplace(a[0], t_var, s_var));
        }

        /* t^n: L{t^n} = n!/s^{n+1} */
        if (op == SX_EXPT && n == 2 && sx_same_var(a[0], t_var) &&
            vis_fixnum(a[1]) && vunfix(a[1]) >= 0) {
            long nv = vunfix(a[1]);
            /* compute n! as a fixnum (safe for small n) */
            val_t fact = vfix(1);
            for (long k = 2; k <= nv; k++) fact = num_mul(fact, vfix(k));
            return sx_div(fact, sx_expt(s_var, vfix(nv + 1)));
        }

        /* e^{a*t}: L{exp(a*t)} = 1/(s-a) */
        if (op == SX_EXP && n == 1) {
            val_t coeff = sx_linear_coeff(a[0], t_var);
            if (!vis_false(coeff))
                return sx_div(vfix(1), sx_sub(s_var, coeff));
        }

        /* sin(omega*t): L{sin} = omega/(s^2+omega^2) */
        if (op == SX_SIN && n == 1) {
            val_t coeff = sx_linear_coeff(a[0], t_var);
            if (!vis_false(coeff)) {
                val_t w2 = sx_expt(coeff, vfix(2));
                return sx_div(coeff, sx_add(sx_expt(s_var, vfix(2)), w2));
            }
        }

        /* cos(omega*t): L{cos} = s/(s^2+omega^2) */
        if (op == SX_COS && n == 1) {
            val_t coeff = sx_linear_coeff(a[0], t_var);
            if (!vis_false(coeff)) {
                val_t w2 = sx_expt(coeff, vfix(2));
                return sx_div(s_var, sx_add(sx_expt(s_var, vfix(2)), w2));
            }
        }

        /* sinh(omega*t): L{sinh} = omega/(s^2-omega^2) */
        if (op == SX_SINH && n == 1) {
            val_t coeff = sx_linear_coeff(a[0], t_var);
            if (!vis_false(coeff)) {
                val_t w2 = sx_expt(coeff, vfix(2));
                return sx_div(coeff, sx_sub(sx_expt(s_var, vfix(2)), w2));
            }
        }

        /* cosh(omega*t): L{cosh} = s/(s^2-omega^2) */
        if (op == SX_COSH && n == 1) {
            val_t coeff = sx_linear_coeff(a[0], t_var);
            if (!vis_false(coeff)) {
                val_t w2 = sx_expt(coeff, vfix(2));
                return sx_div(s_var, sx_sub(sx_expt(s_var, vfix(2)), w2));
            }
        }

        /* SX_APPLY: symbolic function application */
        if (op == SX_APPLY && n >= 1 && vis_symfn(a[0])) {
            val_t fn  = a[0];
            int   nf  = n - 1;
            val_t *fa = a + 1;
            SymFn *sf = as_symfn(fn);

            /* Derivative property: L{u_t(x,t)} = s*L{u(x,t)} - u(x,0) */
            if (!vis_false(sf->parent) && vis_symvar(sf->d_param) &&
                as_symvar(sf->d_param)->name == as_symvar(t_var)->name) {
                /* Replace t with s in args for L{parent} call */
                val_t *la = (val_t *)gc_alloc_raw_pinned((size_t)nf * sizeof(val_t));
                val_t *za = (val_t *)gc_alloc_raw_pinned((size_t)nf * sizeof(val_t));
                sx_replace_var_in_args(la, fa, nf, t_var, s_var);
                sx_replace_var_in_args(za, fa, nf, t_var, vfix(0));
                val_t L_fn       = sx_laplace_fn(sf->parent, t_var, s_var);
                val_t L_of_par   = sx_make_apply(L_fn, nf, la);
                val_t par_at_0   = sx_make_apply(sf->parent, nf, za);
                /* s * L{parent}(x,s) - parent(x,0) */
                return sx_sub(sx_mul(s_var, L_of_par), par_at_0);
            }

            /* Simple case: u(x,t) -> L_u(x,s) */
            val_t *na = (val_t *)gc_alloc_raw_pinned((size_t)nf * sizeof(val_t));
            sx_replace_var_in_args(na, fa, nf, t_var, s_var);
            val_t L_fn = sx_laplace_fn(fn, t_var, s_var);
            return sx_make_apply(L_fn, nf, na);
        }
    }

    /* Fallback: unevaluated node (laplace expr t s) */
    val_t largs[3] = {expr, t_var, s_var};
    return sx_make_expr(SX_LAPLACE, 3, largs);
}

/* ---- Inverse Laplace (table-based) ---- */

val_t sx_ilaplace(val_t expr, val_t s_var, val_t t_var) {
    /* Same unbounded-recursion class as sx_diff above (issue #134). */
    check_c_stack_depth("symbolic");
    if (!vis_symvar(s_var))
        scm_raise(V_FALSE, "ilaplace: s argument must be a symbolic variable");
    if (!vis_symvar(t_var))
        scm_raise(V_FALSE, "ilaplace: t argument must be a symbolic variable");

    /* Constant (doesn't depend on s): L^{-1}{c} -> unevaluated */
    if (!sx_depends_on(expr, s_var)) {
        val_t largs[3] = {expr, s_var, t_var};
        return sx_make_expr(SX_LAPLACE, 3, largs);
    }

    if (vis_symexpr(expr)) {
        SymExpr *se = as_symexpr(expr);
        val_t    op = se->op;
        int      n  = (int)se->nargs;
        val_t   *a  = se->args;

        /* Linearity */
        if (op == SX_ADD) {
            val_t acc = vfix(0);
            for (int i = 0; i < n; i++)
                acc = sx_add(acc, sx_ilaplace(a[i], s_var, t_var));
            return acc;
        }
        if (op == SX_SUB && n == 2)
            return sx_sub(sx_ilaplace(a[0], s_var, t_var),
                          sx_ilaplace(a[1], s_var, t_var));
        if (op == SX_NEG && n == 1)
            return sx_neg(sx_ilaplace(a[0], s_var, t_var));

        /* Constant multiple */
        if ((op == SX_MUL || op == SX_NCMUL) && n == 2) {
            if (!sx_depends_on(a[0], s_var))
                return sx_mul(a[0], sx_ilaplace(a[1], s_var, t_var));
            if (!sx_depends_on(a[1], s_var))
                return sx_mul(a[1], sx_ilaplace(a[0], s_var, t_var));
        }

        /* c/s -> c (step function * constant) */
        if (op == SX_DIV && n == 2 && vis_number(a[0]) && sx_same_var(a[1], s_var))
            return a[0];

        /* c/s^n -> c * t^{n-1}/(n-1)! */
        if (op == SX_DIV && n == 2 && vis_number(a[0]) && !sx_depends_on(a[0], s_var)) {
            val_t den = a[1];
            if (vis_symexpr(den) && as_symexpr(den)->op == SX_EXPT &&
                sx_same_var(as_symexpr(den)->args[0], s_var) &&
                vis_fixnum(as_symexpr(den)->args[1])) {
                long nv = vunfix(as_symexpr(den)->args[1]);
                if (nv >= 1) {
                    /* c * t^{n-1}/(n-1)! — try to reduce c/(n-1)! exactly */
                    val_t fact = vfix(1);
                    for (long k = 2; k <= nv - 1; k++) fact = num_mul(fact, vfix(k));
                    val_t coeff = vis_number(a[0]) ? num_div(a[0], fact) : sx_div(a[0], fact);
                    if (num_is_one(coeff))
                        return sx_expt(t_var, vfix(nv - 1));
                    return sx_mul(coeff, sx_expt(t_var, vfix(nv - 1)));
                }
            }
        }

        /* c/(s-a) -> c*e^{at} */
        if (op == SX_DIV && n == 2 && vis_number(a[0]) && !sx_depends_on(a[0], s_var)) {
            val_t den = a[1];
            if (vis_symexpr(den) && as_symexpr(den)->op == SX_SUB &&
                (int)as_symexpr(den)->nargs == 2 &&
                sx_same_var(as_symexpr(den)->args[0], s_var) &&
                !sx_depends_on(as_symexpr(den)->args[1], s_var)) {
                val_t pole = as_symexpr(den)->args[1];
                val_t base_e = sx_exp(sx_mul(pole, t_var));
                return num_is_one(a[0]) ? base_e : sx_mul(a[0], base_e);
            }
        }

        /* omega/(s^2+omega^2) -> sin(omega*t)
         * omega/(s^2-omega^2) -> sinh(omega*t)
         * s/(s^2+omega^2)     -> cos(omega*t)
         * s/(s^2-omega^2)     -> cosh(omega*t)
         * Also handles simplifier-reordered forms: (+ c s^2) or (+ s^2 c)
         * num_v may be s itself (for cos/cosh) or a constant (for sin/sinh) */
        if (op == SX_DIV && n == 2 &&
            (!sx_depends_on(a[0], s_var) || sx_same_var(a[0], s_var))) {
            val_t num_v = a[0], den = a[1];
            if (vis_symexpr(den)) {
                val_t dop = as_symexpr(den)->op;
                int   dn  = (int)as_symexpr(den)->nargs;
                if ((dop == SX_ADD || dop == SX_SUB) && dn == 2) {
                    val_t d0 = as_symexpr(den)->args[0];
                    val_t d1 = as_symexpr(den)->args[1];
                    /* Find which of d0, d1 is s^2 and which is the constant part.
                     * The simplifier places numeric constants first, so s^2 may be d1. */
                    val_t s2_term = V_FALSE, c_term = V_FALSE;
                    if (vis_symexpr(d0) && as_symexpr(d0)->op == SX_EXPT &&
                        (int)as_symexpr(d0)->nargs == 2 &&
                        sx_same_var(as_symexpr(d0)->args[0], s_var) &&
                        vis_fixnum(as_symexpr(d0)->args[1]) &&
                        vunfix(as_symexpr(d0)->args[1]) == 2 &&
                        !sx_depends_on(d1, s_var)) {
                        s2_term = d0; c_term = d1;
                    } else if (dop == SX_ADD &&
                               vis_symexpr(d1) && as_symexpr(d1)->op == SX_EXPT &&
                               (int)as_symexpr(d1)->nargs == 2 &&
                               sx_same_var(as_symexpr(d1)->args[0], s_var) &&
                               vis_fixnum(as_symexpr(d1)->args[1]) &&
                               vunfix(as_symexpr(d1)->args[1]) == 2 &&
                               !sx_depends_on(d0, s_var)) {
                        s2_term = d1; c_term = d0;
                    }
                    if (!vis_false(s2_term)) {
                        /* c_term is omega^2 or a constant; extract omega */
                        val_t w = V_FALSE;
                        if (vis_symexpr(c_term) && as_symexpr(c_term)->op == SX_EXPT &&
                            (int)as_symexpr(c_term)->nargs == 2 &&
                            vis_fixnum(as_symexpr(c_term)->args[1]) &&
                            vunfix(as_symexpr(c_term)->args[1]) == 2)
                            w = as_symexpr(c_term)->args[0];  /* omega from omega^2 */
                        if (vis_false(w)) w = sx_simplify(sx_sqrt(c_term)); /* omega = sqrt(c_term) */
                        if (dop == SX_ADD) {
                            if (sx_equal(num_v, w))
                                return sx_sin(sx_mul(w, t_var));
                            if (sx_same_var(num_v, s_var))
                                return sx_cos(sx_mul(w, t_var));
                            /* c/(s^2+w^2) where c != w: return c/w * sin(w*t) */
                            if (vis_number(num_v) && !vis_false(w) && !vis_symbolic(w))
                                return sx_mul(sx_div(num_v, w), sx_sin(sx_mul(w, t_var)));
                        } else { /* SX_SUB: hyperbolic */
                            if (sx_equal(num_v, w))
                                return sx_sinh(sx_mul(w, t_var));
                            if (sx_same_var(num_v, s_var))
                                return sx_cosh(sx_mul(w, t_var));
                            /* c/(s^2-w^2) where c != w: return c/w * sinh(w*t) */
                            if (vis_number(num_v) && !vis_false(w) && !vis_symbolic(w))
                                return sx_mul(sx_div(num_v, w), sx_sinh(sx_mul(w, t_var)));
                        }
                    }
                }
            }
        }
    }

    /* Fallback: unevaluated */
    val_t largs[3] = {expr, s_var, t_var};
    return sx_make_expr(SX_LAPLACE, 3, largs);
}

/* ---- Fourier transform ---- */

/* Create the Fourier-transform function of fn: name "F_<fn_name>" */
static val_t sx_fourier_fn(val_t fn, val_t t_var, val_t w_var) {
    SymFn  *sf     = as_symfn(fn);
    Symbol *fn_sym = as_sym(sf->name);
    char    buf[256];
    snprintf(buf, sizeof(buf), "F_%.*s", (int)fn_sym->len, fn_sym->data);
    /* Replace t with omega in params */
    val_t old_params = sf->params, new_params = V_NIL;
    val_t *tail = &new_params;
    while (vis_pair(old_params)) {
        val_t p = vcar(old_params);
        Pair *cell = CURRY_NEW(Pair);
        cell->hdr.type = T_PAIR; cell->hdr.flags = 0;
        cell->car = sx_same_var(p, t_var) ? w_var : p;
        cell->cdr = V_NIL;
        *tail = vptr(cell); tail = &cell->cdr;
        old_params = vcdr(old_params);
    }
    return sx_make_fn(sym_intern_cstr(buf), new_params);
}

val_t sx_fourier(val_t expr, val_t t_var, val_t w_var) {
    /* Same unbounded-recursion class as sx_diff above (issue #134). */
    check_c_stack_depth("symbolic");
    if (!vis_symvar(t_var))
        scm_raise(V_FALSE, "fourier: t argument must be a symbolic variable");
    if (!vis_symvar(w_var))
        scm_raise(V_FALSE, "fourier: omega argument must be a symbolic variable");

    /* Constant: F{c} -> unevaluated (needs Dirac delta) */
    if (!sx_depends_on(expr, t_var)) {
        val_t fargs[3] = {expr, t_var, w_var};
        return sx_make_expr(SX_FOURIER, 3, fargs);
    }

    if (vis_symexpr(expr)) {
        SymExpr *se = as_symexpr(expr);
        val_t    op = se->op;
        int      n  = (int)se->nargs;
        val_t   *a  = se->args;

        /* Linearity */
        if (op == SX_ADD) {
            val_t acc = vfix(0);
            for (int i = 0; i < n; i++)
                acc = sx_add(acc, sx_fourier(a[i], t_var, w_var));
            return acc;
        }
        if (op == SX_SUB && n == 2)
            return sx_sub(sx_fourier(a[0], t_var, w_var),
                          sx_fourier(a[1], t_var, w_var));
        if (op == SX_NEG && n == 1)
            return sx_neg(sx_fourier(a[0], t_var, w_var));

        /* Constant multiple */
        if ((op == SX_MUL || op == SX_NCMUL) && n == 2) {
            if (!sx_depends_on(a[0], t_var))
                return sx_mul(a[0], sx_fourier(a[1], t_var, w_var));
            if (!sx_depends_on(a[1], t_var))
                return sx_mul(a[1], sx_fourier(a[0], t_var, w_var));
        }

        /* SX_APPLY: symbolic function application */
        if (op == SX_APPLY && n >= 1 && vis_symfn(a[0])) {
            val_t fn  = a[0];
            int   nf  = n - 1;
            val_t *fa = a + 1;
            SymFn *sf = as_symfn(fn);

            /* Derivative property: F{u_t(x,t)} = i*omega * F{u(x,t)} */
            if (!vis_false(sf->parent) && vis_symvar(sf->d_param) &&
                as_symvar(sf->d_param)->name == as_symvar(t_var)->name) {
                val_t *wa = (val_t *)gc_alloc_raw_pinned((size_t)nf * sizeof(val_t));
                sx_replace_var_in_args(wa, fa, nf, t_var, w_var);
                val_t F_fn     = sx_fourier_fn(sf->parent, t_var, w_var);
                val_t F_of_par = sx_make_apply(F_fn, nf, wa);
                /* i*omega * F{parent} — use complex i */
                val_t i_val = num_make_complex(vfix(0), vfix(1));  /* 0+1i */
                return sx_mul(sx_mul(i_val, w_var), F_of_par);
            }

            /* Simple case: u(x,t) -> F_u(x,omega) */
            val_t *wa = (val_t *)gc_alloc_raw_pinned((size_t)nf * sizeof(val_t));
            sx_replace_var_in_args(wa, fa, nf, t_var, w_var);
            val_t F_fn = sx_fourier_fn(fn, t_var, w_var);
            return sx_make_apply(F_fn, nf, wa);
        }
    }

    /* Fallback: unevaluated */
    val_t fargs[3] = {expr, t_var, w_var};
    return sx_make_expr(SX_FOURIER, 3, fargs);
}

val_t sx_ifourier(val_t expr, val_t w_var, val_t t_var) {
    /* Same unbounded-recursion class as sx_diff above (issue #134). */
    check_c_stack_depth("symbolic");
    /* Minimal inverse: linearity only, fallback unevaluated */
    if (!vis_symvar(w_var))
        scm_raise(V_FALSE, "ifourier: omega argument must be a symbolic variable");
    if (!vis_symvar(t_var))
        scm_raise(V_FALSE, "ifourier: t argument must be a symbolic variable");

    if (!sx_depends_on(expr, w_var)) {
        val_t fa[3] = {expr, w_var, t_var};
        return sx_make_expr(SX_FOURIER, 3, fa);
    }

    if (vis_symexpr(expr)) {
        SymExpr *se = as_symexpr(expr);
        val_t op = se->op;
        int n = (int)se->nargs;
        val_t *a = se->args;
        if (op == SX_ADD) {
            val_t acc = vfix(0);
            for (int i = 0; i < n; i++) acc = sx_add(acc, sx_ifourier(a[i], w_var, t_var));
            return acc;
        }
        if (op == SX_NEG && n == 1) return sx_neg(sx_ifourier(a[0], w_var, t_var));
        if ((op == SX_MUL || op == SX_NCMUL) && n == 2) {
            if (!sx_depends_on(a[0], w_var)) return sx_mul(a[0], sx_ifourier(a[1], w_var, t_var));
            if (!sx_depends_on(a[1], w_var)) return sx_mul(a[1], sx_ifourier(a[0], w_var, t_var));
        }
    }
    val_t fa[3] = {expr, w_var, t_var};
    return sx_make_expr(SX_FOURIER, 3, fa);
}
