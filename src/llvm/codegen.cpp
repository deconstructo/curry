/*
 * Curry → LLVM IR code generator.
 *
 * Compilation pipeline:
 *
 *   Scheme S-expression
 *     └─► emit_expr()   — recursive AST → SSA
 *           ├─ literals, quote           → i64 immediates
 *           ├─ symbol                    → load local slot or curry_jit_global_lookup
 *           ├─ (if t c a)               → branch + phi
 *           ├─ (cond ...)               → nested branches
 *           ├─ (case key ...)           → cond with eqv? tests
 *           ├─ (and ...) / (or ...)     → short-circuit branches
 *           ├─ (when c e...) / (unless) → if + begin
 *           ├─ (begin e...)             → sequential, last value survives
 *           ├─ (lambda params body...)  → emit_lambda → JIT closure alloc
 *           ├─ (define ...)             → local binding or curry_jit_global_define
 *           ├─ (let ...)               → emit_let (regular and named)
 *           ├─ (let* ...)              → sequential let
 *           ├─ (letrec / letrec* ...)  → pre-bind then fill
 *           ├─ (set! v e)              → store to slot or error
 *           ├─ (do ...)               → loop with step vars
 *           └─ (f args...)             → gc.statepoint call sequence
 *
 * gc.statepoint invariant (must hold from day 1):
 *   Every call that may allocate is emitted as an llvm.experimental.gc.statepoint.
 *   GC root enumeration (gc_args) is empty until L3 (generational GC).
 *   All emitted functions carry  gc "curry-generational"  and  nounwind.
 */

#include "codegen.h"
#include "gc_strategy.h"
#include <optional>

extern "C" {
#include "../object.h"
#include "../value.h"
#include "../symbol.h"
#include "../env.h"
#include "../builtins.h"
}

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/GCStrategy.h>
#include <llvm/IR/Statepoint.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>

#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include <atomic>

using namespace llvm;

namespace curry {

/* ---- Interned special-form symbols (cached once per process) ---- */

static val_t S(const char *name) { return sym_intern_cstr(name); }

/* ---- Type helpers ---- */

static Type *val_type(LLVMContext &ctx) { return Type::getInt64Ty(ctx); }

static FunctionType *scheme_fn_type(LLVMContext &ctx) {
    return FunctionType::get(
        val_type(ctx),
        { Type::getInt32Ty(ctx), PointerType::getUnqual(ctx) },
        false);
}

static Function *declare_helper(llvm::Module &M, const char *name, FunctionType *ft) {
    if (auto *f = M.getFunction(name)) return f;
    auto *f = Function::Create(ft, Function::ExternalLinkage, name, M);
    f->setCallingConv(CallingConv::C);
    return f;
}

/* ---- Compile context ---- */

struct Binding {
    AllocaInst *slot;
    bool        is_mutable;
    bool        is_cell;   /* slot holds a GC cell address (i64), not a direct value */
};

/* Tracks an active named-let loop so self-calls emit backedges instead of
 * recursive function calls. */
struct NamedLetCtx {
    std::string             name;
    std::vector<AllocaInst*> slots;     /* one per loop variable */
    BasicBlock             *loop_bb;    /* branch target for self-call */
    BasicBlock             *exit_bb;    /* branch target when loop exits */
    AllocaInst             *result_slot; /* result written by exit paths */
};

struct CompileCtx {
    LLVMContext    &llvm_ctx;
    llvm::Module   &M;
    IRBuilder<>   B;
    Function     *fn;
    bool          top_level;   /* true in file/expr context, false inside lambda */

    std::vector<std::unordered_map<std::string, Binding>> scopes;
    std::vector<AllocaInst *> gc_roots;
    std::vector<NamedLetCtx>  named_lets;   /* innermost-last stack */

    Type     *i64_t;
    Type     *i32_t;
    Type     *ptr_t;
    Constant *V_VOID_c;
    Constant *V_FALSE_c;
    Constant *V_TRUE_c;
    Constant *V_NIL_c;

    CompileCtx(LLVMContext &ctx, llvm::Module &mod, Function *f, bool tl = false)
        : llvm_ctx(ctx), M(mod), B(ctx), fn(f), top_level(tl)
        , i64_t(Type::getInt64Ty(ctx))
        , i32_t(Type::getInt32Ty(ctx))
        , ptr_t(PointerType::getUnqual(ctx))
        , V_VOID_c(ConstantInt::get(Type::getInt64Ty(ctx), 0x0F))
        , V_FALSE_c(ConstantInt::get(Type::getInt64Ty(ctx), 0x03))
        , V_TRUE_c(ConstantInt::get(Type::getInt64Ty(ctx), 0x07))
        , V_NIL_c(ConstantInt::get(Type::getInt64Ty(ctx), 0x0B))
    {
        B.SetInsertPoint(&f->getEntryBlock());
    }

    void push_scope() { scopes.emplace_back(); }
    void pop_scope()  { scopes.pop_back(); }

    Binding *lookup(const std::string &name) {
        for (int i = (int)scopes.size() - 1; i >= 0; --i) {
            auto it = scopes[i].find(name);
            if (it != scopes[i].end()) return &it->second;
        }
        return nullptr;
    }

    void bind(const std::string &name, AllocaInst *slot, bool mut = true, bool cell = false) {
        if (scopes.empty()) scopes.emplace_back();
        scopes.back()[name] = Binding{slot, mut, cell};
    }

    AllocaInst *alloc_root(const std::string &name = "") {
        IRBuilder<> eb(&fn->getEntryBlock(), fn->getEntryBlock().begin());
        auto *a = eb.CreateAlloca(i64_t, nullptr, name);
        eb.CreateStore(V_FALSE_c, a);
        gc_roots.push_back(a);
        return a;
    }

    bool terminated() const {
        return B.GetInsertBlock()->getTerminator() != nullptr;
    }

    void branch_if_open(BasicBlock *target) {
        if (!terminated()) B.CreateBr(target);
    }
};

/* ---- Forward declarations ---- */
static Value *emit_expr(CompileCtx &cc, val_t expr);
static Value *emit_body(CompileCtx &cc, val_t forms);

/* ---- Statepoint call emission ---- */
/*
 * Wraps every potentially-allocating call in an llvm.experimental.gc.statepoint.
 * GCArgs is empty: Boehm conservatively scans the C stack for pointers in
 * old-gen; the generational GC is inhibited across every statepoint so nursery
 * pointers in JIT alloca slots are never stale (Phase 1 correctness fix).
 *
 * Phase 3 will populate GCArgs from cc.gc_roots once we have precise old-gen
 * and can emit gc.relocate projections.
 *
 * Minor GC inhibit protocol:
 *   gc_inhibit_minor_fn() is called before the statepoint; gc_resume_minor_fn()
 *   after.  This prevents the generational minor GC from running while live
 *   nursery pointers reside in JIT alloca slots that the GC cannot enumerate.
 *   If a Scheme exception longjmps through a JIT frame, SCM_PROTECT restores
 *   gc_inhibit_count to its pre-JIT value via gc_inhibit_restore().
 */
static Value *emit_statepoint_call(CompileCtx &cc, Value *callee,
                                   FunctionType *ftype,
                                   ArrayRef<Value *> call_args) {
    /* Inhibit minor GC before the call. */
    auto *void_ft = FunctionType::get(Type::getVoidTy(cc.llvm_ctx), {}, false);
    cc.B.CreateCall(void_ft,
        declare_helper(cc.M, "curry_gc_inhibit_minor_jit", void_ft), {});

#if LLVM_VERSION_MAJOR >= 17
    std::optional<ArrayRef<Value *>> deopt_args;
#else
    llvm::Optional<ArrayRef<Value *>> deopt_args;
#endif
    auto *sp = cc.B.CreateGCStatepointCall(
        /*ID=*/0, /*NumPatchBytes=*/0,
        FunctionCallee(ftype, callee),
        SmallVector<Value *, 8>(call_args.begin(), call_args.end()),
        deopt_args,
        /*GCArgs=*/ArrayRef<Value *>{});

    Value *result;
    if (ftype->getReturnType()->isVoidTy())
        result = cc.V_VOID_c;
    else
        result = cc.B.CreateGCResult(sp, ftype->getReturnType());

    /* Resume minor GC after the call. */
    cc.B.CreateCall(void_ft,
        declare_helper(cc.M, "curry_gc_resume_minor_jit", void_ft), {});

    return result;
}

/* ---- Literal emission ---- */

static Value *emit_literal(CompileCtx &cc, val_t v) {
    if (vis_fixnum(v)) return ConstantInt::get(cc.i64_t, (uint64_t)v);
    if (vis_nil(v))    return cc.V_NIL_c;
    if (vis_false(v))  return cc.V_FALSE_c;
    if (v == V_TRUE)   return cc.V_TRUE_c;
    if (vis_void(v))   return cc.V_VOID_c;
    /* Heap objects: embed the pointer.  Boehm scans conservatively, so the
     * object stays alive.  When we move to a compacting GC, use gc.root. */
    return ConstantInt::get(cc.i64_t, (uint64_t)v);
}

/* ---- Global helpers ---- */

static Value *emit_global_lookup(CompileCtx &cc, val_t sym) {
    auto *ft = FunctionType::get(cc.i64_t, { cc.i64_t }, false);
    auto *fn = declare_helper(cc.M, "curry_jit_global_lookup", ft);
    return emit_statepoint_call(cc, fn, ft,
        { ConstantInt::get(cc.i64_t, (uint64_t)sym) });
}

static Value *emit_global_define(CompileCtx &cc, val_t sym, Value *val_v) {
    auto *ft = FunctionType::get(Type::getVoidTy(cc.llvm_ctx),
                                  { cc.i64_t, cc.i64_t }, false);
    auto *fn = declare_helper(cc.M, "curry_jit_global_define", ft);
    emit_statepoint_call(cc, fn, ft,
        { ConstantInt::get(cc.i64_t, (uint64_t)sym), val_v });
    return cc.V_VOID_c;
}

/* ---- Procedure call ---- */

static Value *emit_apply(CompileCtx &cc, Value *callee_v,
                         const SmallVector<Value *, 8> &arg_vals) {
    uint32_t argc = (uint32_t)arg_vals.size();
    AllocaInst *argv_alloca = cc.B.CreateAlloca(
        cc.i64_t, ConstantInt::get(cc.i32_t, argc > 0 ? argc : 1), "argv");
    for (uint32_t i = 0; i < argc; ++i) {
        auto *gep = cc.B.CreateGEP(cc.i64_t, argv_alloca,
                                    ConstantInt::get(cc.i32_t, i));
        cc.B.CreateStore(arg_vals[i], gep);
    }
    auto *apply_ft = FunctionType::get(
        cc.i64_t, { cc.i64_t, cc.i32_t, cc.ptr_t }, false);
    auto *apply_fn = declare_helper(cc.M, "curry_jit_apply_arr", apply_ft);
    return emit_statepoint_call(cc, apply_fn, apply_ft,
        { callee_v,
          ConstantInt::get(cc.i32_t, argc),
          cc.B.CreateBitCast(argv_alloca, cc.ptr_t) });
}

/* Known 2-argument arithmetic operators → direct C helper (skip global lookup
 * + apply dispatch).  Comparison wrappers are defined in jit.cpp.           */
static const std::unordered_map<std::string, const char *> ARITH2 = {
    {"+",  "num_add"}, {"-",  "num_sub"}, {"*",  "num_mul"}, {"/",  "num_div"},
    {"<",  "curry_jit_lt"},  {">",  "curry_jit_gt"},
    {"<=", "curry_jit_le"},  {">=", "curry_jit_ge"},
    {"=",  "curry_jit_num_eq"},
};

/* Known 1-argument numeric functions. */
static const std::unordered_map<std::string, const char *> ARITH1 = {
    {"inexact",          "num_inexact"},
    {"exact->inexact",   "num_inexact"},
    {"log",   "num_log"},  {"exp",  "num_exp"},  {"sqrt", "num_sqrt"},
    {"sin",   "num_sin"},  {"cos",  "num_cos"},  {"abs",  "num_abs"},
    {"floor", "num_floor"},{"ceiling","num_ceiling"},{"truncate","num_truncate"},
    {"round", "num_round"},
};

static Value *emit_call(CompileCtx &cc, val_t fn_expr, val_t args) {

    /* ---- Named-let self-call → loop backedge ---- */
    if (vis_symbol(fn_expr) && !cc.named_lets.empty()) {
        std::string nm = as_sym(fn_expr)->data;
        for (int i = (int)cc.named_lets.size() - 1; i >= 0; --i) {
            if (cc.named_lets[i].name == nm) {
                NamedLetCtx &nlet = cc.named_lets[i];
                /* Evaluate all new argument values BEFORE storing (avoid
                 * reading a slot after it may have been updated). */
                SmallVector<Value *, 8> new_vals;
                for (val_t a = args; vis_pair(a); a = as_pair(a)->cdr)
                    new_vals.push_back(emit_expr(cc, as_pair(a)->car));
                for (int j = 0; j < (int)new_vals.size() && j < (int)nlet.slots.size(); ++j)
                    cc.B.CreateStore(new_vals[j], nlet.slots[j]);
                cc.B.CreateBr(nlet.loop_bb);
                return cc.V_VOID_c;   /* unreachable, but emit_body needs a value */
            }
        }
    }

    /* ---- Direct arithmetic (skip global lookup + apply dispatch) ---- */
    if (vis_symbol(fn_expr)) {
        std::string op_nm = as_sym(fn_expr)->data;

        /* Variadic fold: (+), (*) → identity; (- x) → negate; (op a b c…) → fold */
        {
            auto it = ARITH2.find(op_nm);
            if (it != ARITH2.end()) {
                /* Count arguments */
                int argc = 0;
                for (val_t a = args; vis_pair(a); a = as_pair(a)->cdr) ++argc;

                if (argc == 1) {
                    /* (+ v) → v;  (* v) → v;  (- v) → num_sub(0,v);  (/ v) → num_div(1,v) */
                    Value *v = emit_expr(cc, as_pair(args)->car);
                    if (op_nm == "-") {
                        auto *ft = FunctionType::get(cc.i64_t, {cc.i64_t, cc.i64_t}, false);
                        auto *fn = declare_helper(cc.M, "num_sub", ft);
                        Value *zero = ConstantInt::get(cc.i64_t,
                            (uint64_t)vfix(0));
                        return emit_statepoint_call(cc, fn, ft, {zero, v});
                    }
                    if (op_nm == "/") {
                        auto *ft = FunctionType::get(cc.i64_t, {cc.i64_t, cc.i64_t}, false);
                        auto *fn = declare_helper(cc.M, "num_div", ft);
                        Value *one = ConstantInt::get(cc.i64_t,
                            (uint64_t)vfix(1));
                        return emit_statepoint_call(cc, fn, ft, {one, v});
                    }
                    return v; /* (+) and (*) identity */
                }

                if (argc >= 2) {
                    /* Fold left: (op a b c) → op(op(a,b),c) */
                    auto *ft = FunctionType::get(cc.i64_t, {cc.i64_t, cc.i64_t}, false);
                    auto *fn = declare_helper(cc.M, it->second, ft);
                    val_t cur = args;
                    Value *acc = emit_expr(cc, as_pair(cur)->car);
                    cur = as_pair(cur)->cdr;
                    while (vis_pair(cur)) {
                        Value *rhs = emit_expr(cc, as_pair(cur)->car);
                        acc = emit_statepoint_call(cc, fn, ft, {acc, rhs});
                        cur = as_pair(cur)->cdr;
                    }
                    return acc;
                }
                /* argc == 0: (+) → 0, (*) → 1 */
                if (op_nm == "*")
                    return ConstantInt::get(cc.i64_t, (uint64_t)vfix(1));
                return ConstantInt::get(cc.i64_t, (uint64_t)vfix(0));
            }
        }

        /* ---- Direct 1-arg numeric (single arg) ---- */
        if (vis_pair(args) && vis_nil(as_pair(args)->cdr)) {
            auto it = ARITH1.find(op_nm);
            if (it != ARITH1.end()) {
                Value *a = emit_expr(cc, as_pair(args)->car);
                auto *ft = FunctionType::get(cc.i64_t, {cc.i64_t}, false);
                auto *fn = declare_helper(cc.M, it->second, ft);
                return emit_statepoint_call(cc, fn, ft, {a});
            }
        }
    }

    /* ---- Generic call through apply_arr ---- */
    Value *callee_v = emit_expr(cc, fn_expr);
    SmallVector<Value *, 8> arg_vals;
    for (val_t a = args; vis_pair(a); a = as_pair(a)->cdr)
        arg_vals.push_back(emit_expr(cc, as_pair(a)->car));
    return emit_apply(cc, callee_v, arg_vals);
}

/* ---- Free variable analysis ---- */

static void collect_free_vars(val_t expr,
                               const std::unordered_set<std::string> &bound,
                               std::unordered_set<std::string> &free) {
    if (vis_symbol(expr)) {
        std::string n = as_sym(expr)->data;
        if (!bound.count(n)) free.insert(n);
        return;
    }
    if (!vis_pair(expr)) return;

    val_t op  = as_pair(expr)->car;
    val_t rst = as_pair(expr)->cdr;

    if (vis_symbol(op)) {
        val_t s = op;
        if (s == S("quote")) return;

        if (s == S("lambda")) {
            val_t params = as_pair(rst)->car;
            val_t body   = as_pair(rst)->cdr;
            auto nb = bound;
            val_t p = params;
            while (vis_pair(p)) {
                if (vis_symbol(as_pair(p)->car))
                    nb.insert(as_sym(as_pair(p)->car)->data);
                p = as_pair(p)->cdr;
            }
            if (vis_symbol(p)) nb.insert(as_sym(p)->data);
            else if (vis_symbol(params)) nb.insert(as_sym(params)->data);
            for (val_t e = body; vis_pair(e); e = as_pair(e)->cdr)
                collect_free_vars(as_pair(e)->car, nb, free);
            return;
        }

        if (s == S("let")) {
            val_t bindings = as_pair(rst)->car;
            val_t body     = as_pair(rst)->cdr;
            if (vis_symbol(bindings)) {
                /* named let */
                std::string loop_nm = as_sym(bindings)->data;
                val_t real_b = vis_pair(body) ? as_pair(body)->car : V_NIL;
                val_t real_body = vis_pair(body) ? as_pair(body)->cdr : V_NIL;
                for (val_t b = real_b; vis_pair(b); b = as_pair(b)->cdr)
                    collect_free_vars(as_pair(as_pair(as_pair(b)->car)->cdr)->car, bound, free);
                auto nb = bound;
                nb.insert(loop_nm);
                for (val_t b = real_b; vis_pair(b); b = as_pair(b)->cdr)
                    if (vis_symbol(as_pair(as_pair(b)->car)->car))
                        nb.insert(as_sym(as_pair(as_pair(b)->car)->car)->data);
                for (val_t e = real_body; vis_pair(e); e = as_pair(e)->cdr)
                    collect_free_vars(as_pair(e)->car, nb, free);
                return;
            }
            auto nb = bound;
            for (val_t b = bindings; vis_pair(b); b = as_pair(b)->cdr) {
                val_t bnd = as_pair(b)->car;
                collect_free_vars(as_pair(as_pair(bnd)->cdr)->car, bound, free);
                if (vis_symbol(as_pair(bnd)->car))
                    nb.insert(as_sym(as_pair(bnd)->car)->data);
            }
            for (val_t e = body; vis_pair(e); e = as_pair(e)->cdr)
                collect_free_vars(as_pair(e)->car, nb, free);
            return;
        }

        if (s == S("let*") || s == S("letrec") || s == S("letrec*")) {
            val_t bindings = as_pair(rst)->car;
            val_t body     = as_pair(rst)->cdr;
            auto nb = bound;
            for (val_t b = bindings; vis_pair(b); b = as_pair(b)->cdr) {
                val_t bnd = as_pair(b)->car;
                if (vis_symbol(as_pair(bnd)->car))
                    nb.insert(as_sym(as_pair(bnd)->car)->data);
                collect_free_vars(as_pair(as_pair(bnd)->cdr)->car, nb, free);
            }
            for (val_t e = body; vis_pair(e); e = as_pair(e)->cdr)
                collect_free_vars(as_pair(e)->car, nb, free);
            return;
        }

        if (s == S("define")) {
            val_t first = as_pair(rst)->car;
            if (vis_symbol(first)) {
                collect_free_vars(as_pair(as_pair(rst)->cdr)->car, bound, free);
            } else if (vis_pair(first)) {
                auto nb = bound;
                for (val_t p = as_pair(first)->cdr; vis_pair(p); p = as_pair(p)->cdr)
                    if (vis_symbol(as_pair(p)->car))
                        nb.insert(as_sym(as_pair(p)->car)->data);
                for (val_t e = as_pair(rst)->cdr; vis_pair(e); e = as_pair(e)->cdr)
                    collect_free_vars(as_pair(e)->car, nb, free);
            }
            return;
        }

        if (s == S("set!")) {
            std::string n = as_sym(as_pair(rst)->car)->data;
            if (!bound.count(n)) free.insert(n);
            collect_free_vars(as_pair(as_pair(rst)->cdr)->car, bound, free);
            return;
        }
    }

    /* Default: recurse on every subform. */
    collect_free_vars(op, bound, free);
    for (val_t e = rst; vis_pair(e); e = as_pair(e)->cdr)
        collect_free_vars(as_pair(e)->car, bound, free);
}

/* ---- Cell allocation ---- */
/*
 * Allocates a GC-managed heap cell initialised to init_val and returns its
 * address as an i64.  Used by letrec / named let so that lambdas capture the
 * cell address rather than the stale #f present at lambda-creation time.
 */
static Value *emit_alloc_cell(CompileCtx &cc, Value *init_val) {
    auto *ft = FunctionType::get(cc.i64_t, { cc.i64_t }, false);
    auto *fn = declare_helper(cc.M, "curry_jit_alloc_cell", ft);
    return emit_statepoint_call(cc, fn, ft, { init_val });
}

/* ---- Lambda emitter ---- */

static std::atomic<uint64_t> g_lambda_seq{0};

static Value *emit_lambda(CompileCtx &cc, val_t params_val, val_t body_val) {
    /* --- Parse parameter list --- */
    std::vector<std::string> fixed_params;
    std::string rest_param;
    bool has_rest = false;

    if (vis_symbol(params_val)) {
        rest_param = as_sym(params_val)->data;
        has_rest   = true;
    } else {
        val_t p = params_val;
        while (vis_pair(p)) {
            val_t pn = as_pair(p)->car;
            if (!vis_symbol(pn))
                throw std::runtime_error("lambda: non-symbol parameter");
            fixed_params.push_back(as_sym(pn)->data);
            p = as_pair(p)->cdr;
        }
        if (vis_symbol(p)) {
            rest_param = as_sym(p)->data;
            has_rest   = true;
        }
    }

    /* --- Free variable analysis --- */
    std::unordered_set<std::string> bound_by(fixed_params.begin(),
                                               fixed_params.end());
    if (has_rest) bound_by.insert(rest_param);

    std::unordered_set<std::string> free_set;
    for (val_t e = body_val; vis_pair(e); e = as_pair(e)->cdr)
        collect_free_vars(as_pair(e)->car, bound_by, free_set);

    std::vector<std::string> captured;
    std::vector<bool>        captured_is_cell;
    for (const auto &nm : free_set) {
        if (Binding *b = cc.lookup(nm)) {
            captured.push_back(nm);
            captured_is_cell.push_back(b->is_cell);
        }
    }

    /* --- Emit the lambda as a new LLVM function in the same module ---
     * ABI: i64 fn(i32 argc, ptr argv, ptr caps) */
    std::string fn_name = "lambda_" + std::to_string(g_lambda_seq.fetch_add(1));

    auto *lambda_ft = FunctionType::get(
        cc.i64_t, { cc.i32_t, cc.ptr_t, cc.ptr_t }, false);
    auto *lambda_fn = Function::Create(
        lambda_ft, Function::ExternalLinkage, fn_name, cc.M);
    lambda_fn->setGC("curry-generational");
    lambda_fn->addFnAttr(Attribute::NoUnwind);

    /* Save caller's insertion point. */
    BasicBlock *saved_bb = cc.B.GetInsertBlock();
    BasicBlock::iterator saved_ip = cc.B.GetInsertPoint();

    BasicBlock::Create(cc.llvm_ctx, "entry", lambda_fn);
    CompileCtx lcc(cc.llvm_ctx, cc.M, lambda_fn, /*top_level=*/false);

    auto *argc_arg = lambda_fn->getArg(0); argc_arg->setName("argc");
    auto *argv_arg = lambda_fn->getArg(1); argv_arg->setName("argv");
    auto *caps_arg = lambda_fn->getArg(2); caps_arg->setName("caps");

    lcc.push_scope();

    /* Load captured values from the caps array.
     * Cell-typed caps carry a cell address; the binding is marked is_cell=true
     * so that reads/writes go through the cell indirection. */
    for (int i = 0; i < (int)captured.size(); ++i) {
        auto *gep = lcc.B.CreateGEP(lcc.i64_t, caps_arg,
                                     ConstantInt::get(lcc.i32_t, i));
        auto *cell_or_val = lcc.B.CreateLoad(lcc.i64_t, gep, captured[i]);
        auto *slot = lcc.alloc_root(captured[i]);
        lcc.B.CreateStore(cell_or_val, slot);
        lcc.bind(captured[i], slot, true, captured_is_cell[i]);
    }

    /* Load fixed parameters from argv. */
    for (int i = 0; i < (int)fixed_params.size(); ++i) {
        const auto &pn = fixed_params[i];
        auto *gep = lcc.B.CreateGEP(lcc.i64_t, argv_arg,
                                     ConstantInt::get(lcc.i32_t, i));
        auto *val = lcc.B.CreateLoad(lcc.i64_t, gep, pn);
        auto *slot = lcc.alloc_root(pn);
        lcc.B.CreateStore(val, slot);
        lcc.bind(pn, slot);
    }

    /* Rest parameter: collect remaining args into a list. */
    if (has_rest) {
        int n_fixed = (int)fixed_params.size();
        auto *list_ft = FunctionType::get(lcc.i64_t,
            { lcc.ptr_t, lcc.i32_t }, false);
        auto *list_fn = declare_helper(lcc.M, "curry_jit_list_tail", list_ft);
        auto *rest_argv = lcc.B.CreateGEP(lcc.i64_t, argv_arg,
                                            ConstantInt::get(lcc.i32_t, n_fixed));
        auto *rest_n   = lcc.B.CreateSub(argc_arg,
                                          ConstantInt::get(lcc.i32_t, n_fixed));
        auto *rest_val = emit_statepoint_call(lcc, list_fn, list_ft,
                                               { rest_argv, rest_n });
        auto *slot = lcc.alloc_root(rest_param);
        lcc.B.CreateStore(rest_val, slot);
        lcc.bind(rest_param, slot);
    }

    /* Emit body (handles internal defines via emit_body). */
    Value *result = emit_body(lcc, body_val);
    if (!lcc.terminated()) lcc.B.CreateRet(result);
    lcc.pop_scope();

    /* Restore enclosing function's insertion point. */
    cc.B.SetInsertPoint(saved_bb, saved_ip);

    /* --- At call site: build capture array and create a JIT closure --- */
    Value *caps_ptr;
    if (!captured.empty()) {
        caps_ptr = cc.B.CreateAlloca(cc.i64_t,
                                      ConstantInt::get(cc.i32_t, (int)captured.size()),
                                      "caps");
        for (int i = 0; i < (int)captured.size(); ++i) {
            auto *cv = cc.B.CreateLoad(cc.i64_t, cc.lookup(captured[i])->slot,
                                        captured[i]);
            cc.B.CreateStore(cv, cc.B.CreateGEP(cc.i64_t, caps_ptr,
                                                  ConstantInt::get(cc.i32_t, i)));
        }
    } else {
        caps_ptr = ConstantPointerNull::get(cast<PointerType>(cc.ptr_t));
    }

    auto *make_ft = FunctionType::get(cc.i64_t,
        { cc.ptr_t, cc.i32_t, cc.ptr_t }, false);
    auto *make_fn = declare_helper(cc.M, "curry_jit_make_closure", make_ft);
    return emit_statepoint_call(cc, make_fn, make_ft,
        { lambda_fn,
          ConstantInt::get(cc.i32_t, (int)captured.size()),
          caps_ptr });
}

/* ---- Body emitter (handles internal defines as letrec*) ---- */

static Value *emit_body(CompileCtx &cc, val_t forms) {
    /* Pre-scan leading defines: collect (name, init-expr) pairs.
     * Skipped at top level — defines there go to global env via emit_expr. */
    struct DefSpec { std::string name; val_t init; };
    std::vector<DefSpec> defs;
    val_t rest = forms;

    if (!cc.top_level) {
        while (vis_pair(rest)) {
            val_t form = as_pair(rest)->car;
            if (!vis_pair(form)) break;
            val_t op = as_pair(form)->car;
            if (!vis_symbol(op) || op != S("define")) break;

            val_t first = as_pair(as_pair(form)->cdr)->car;
            if (vis_symbol(first)) {
                defs.push_back({ as_sym(first)->data,
                                 as_pair(as_pair(as_pair(form)->cdr)->cdr)->car });
            } else if (vis_pair(first)) {
                val_t name_sym = as_pair(first)->car;
                val_t params   = as_pair(first)->cdr;
                val_t body2    = as_pair(as_pair(form)->cdr)->cdr;
                val_t lam_form = scm_cons(S("lambda"), scm_cons(params, body2));
                defs.push_back({ as_sym(name_sym)->data, lam_form });
            } else {
                break;
            }
            rest = as_pair(rest)->cdr;
        }
    }

    if (!defs.empty()) {
        /* Internal defines: letrec* semantics with GC cells so that
         * mutually-recursive lambdas capture cell addresses, not stale #f. */
        for (auto &d : defs) {
            auto *slot = cc.alloc_root(d.name);
            Value *cell = emit_alloc_cell(cc, cc.V_FALSE_c);
            cc.B.CreateStore(cell, slot);
            cc.bind(d.name, slot, true, /*is_cell=*/true);
        }
        for (auto &d : defs) {
            Value *v = emit_expr(cc, d.init);
            Value *cell_addr = cc.B.CreateLoad(cc.i64_t, cc.lookup(d.name)->slot);
            Value *cell_ptr  = cc.B.CreateIntToPtr(cell_addr, cc.ptr_t);
            cc.B.CreateStore(v, cell_ptr);
        }
    }

    Value *result = cc.V_VOID_c;
    for (val_t e = rest; vis_pair(e); e = as_pair(e)->cdr)
        result = emit_expr(cc, as_pair(e)->car);
    return result;
}

/* ---- and / or ---- */

static Value *emit_and(CompileCtx &cc, val_t args) {
    if (vis_nil(args)) return cc.V_TRUE_c;

    auto *slot    = cc.alloc_root("and_r");
    auto *done_bb = BasicBlock::Create(cc.llvm_ctx, "and_done", cc.fn);

    val_t a = args;
    while (vis_pair(a)) {
        Value *v = emit_expr(cc, as_pair(a)->car);
        if (!cc.terminated()) cc.B.CreateStore(v, slot);
        a = as_pair(a)->cdr;
        if (vis_nil(a)) break;
        auto *next = BasicBlock::Create(cc.llvm_ctx, "and_next", cc.fn);
        if (!cc.terminated())
            cc.B.CreateCondBr(cc.B.CreateICmpEQ(v, cc.V_FALSE_c), done_bb, next);
        cc.B.SetInsertPoint(next);
    }
    cc.branch_if_open(done_bb);
    cc.B.SetInsertPoint(done_bb);
    return cc.B.CreateLoad(cc.i64_t, slot, "and_result");
}

static Value *emit_or(CompileCtx &cc, val_t args) {
    if (vis_nil(args)) return cc.V_FALSE_c;

    auto *slot    = cc.alloc_root("or_r");
    cc.B.CreateStore(cc.V_FALSE_c, slot);
    auto *done_bb = BasicBlock::Create(cc.llvm_ctx, "or_done", cc.fn);

    val_t a = args;
    while (vis_pair(a)) {
        Value *v = emit_expr(cc, as_pair(a)->car);
        if (!cc.terminated()) cc.B.CreateStore(v, slot);
        a = as_pair(a)->cdr;
        if (vis_nil(a)) break;
        auto *next = BasicBlock::Create(cc.llvm_ctx, "or_next", cc.fn);
        if (!cc.terminated())
            cc.B.CreateCondBr(cc.B.CreateICmpNE(v, cc.V_FALSE_c), done_bb, next);
        cc.B.SetInsertPoint(next);
    }
    cc.branch_if_open(done_bb);
    cc.B.SetInsertPoint(done_bb);
    return cc.B.CreateLoad(cc.i64_t, slot, "or_result");
}

/* ---- cond ---- */
/*
 * Uses a result-slot (alloca + store/load) instead of phi nodes so that
 * when a branch is terminated by a named-let backedge we can simply skip
 * the store/branch without corrupting the phi's incoming-value list.
 * LLVM mem2reg converts the slot back to SSA phi nodes at -O1+.
 */

static Value *emit_cond(CompileCtx &cc, val_t clauses);

static Value *emit_cond(CompileCtx &cc, val_t clauses) {
    if (vis_nil(clauses)) return cc.V_VOID_c;

    val_t clause = as_pair(clauses)->car;
    val_t rest   = as_pair(clauses)->cdr;
    val_t test   = as_pair(clause)->car;
    val_t body   = as_pair(clause)->cdr;

    /* (else ...) */
    if (vis_symbol(test) && test == S("else")) {
        Value *r = cc.V_VOID_c;
        for (val_t e = body; vis_pair(e); e = as_pair(e)->cdr)
            r = emit_expr(cc, as_pair(e)->car);
        return r;
    }

    Value *test_v = emit_expr(cc, test);
    auto *result_slot = cc.alloc_root("cond_r");

    /* (test => proc) */
    if (vis_pair(body) && vis_symbol(as_pair(body)->car) &&
        as_pair(body)->car == S("=>")) {
        val_t proc_expr = as_pair(as_pair(body)->cdr)->car;
        auto *test_slot = cc.alloc_root("cond_v");
        cc.B.CreateStore(test_v, test_slot);

        auto *then_bb  = BasicBlock::Create(cc.llvm_ctx, "cond_then", cc.fn);
        auto *else_bb  = BasicBlock::Create(cc.llvm_ctx, "cond_else", cc.fn);
        auto *merge_bb = BasicBlock::Create(cc.llvm_ctx, "cond_mrg",  cc.fn);

        cc.B.CreateCondBr(cc.B.CreateICmpNE(test_v, cc.V_FALSE_c), then_bb, else_bb);

        cc.B.SetInsertPoint(then_bb);
        Value *tv    = cc.B.CreateLoad(cc.i64_t, test_slot);
        Value *proc  = emit_expr(cc, proc_expr);
        Value *then_v = emit_apply(cc, proc, { tv });
        if (!cc.terminated()) { cc.B.CreateStore(then_v, result_slot); cc.B.CreateBr(merge_bb); }

        cc.B.SetInsertPoint(else_bb);
        Value *else_v = emit_cond(cc, rest);
        if (!cc.terminated()) { cc.B.CreateStore(else_v, result_slot); cc.B.CreateBr(merge_bb); }

        cc.B.SetInsertPoint(merge_bb);
        return cc.B.CreateLoad(cc.i64_t, result_slot, "cond_val");
    }

    /* Normal (test expr...) clause */
    auto *then_bb  = BasicBlock::Create(cc.llvm_ctx, "cond_then", cc.fn);
    auto *else_bb  = BasicBlock::Create(cc.llvm_ctx, "cond_else", cc.fn);
    auto *merge_bb = BasicBlock::Create(cc.llvm_ctx, "cond_mrg",  cc.fn);

    cc.B.CreateCondBr(cc.B.CreateICmpNE(test_v, cc.V_FALSE_c), then_bb, else_bb);

    cc.B.SetInsertPoint(then_bb);
    Value *then_v;
    if (vis_nil(body)) {
        then_v = test_v;   /* (cond (test)) → return test value */
    } else {
        then_v = cc.V_VOID_c;
        for (val_t e = body; vis_pair(e); e = as_pair(e)->cdr)
            then_v = emit_expr(cc, as_pair(e)->car);
    }
    if (!cc.terminated()) { cc.B.CreateStore(then_v, result_slot); cc.B.CreateBr(merge_bb); }

    cc.B.SetInsertPoint(else_bb);
    Value *else_v = emit_cond(cc, rest);
    if (!cc.terminated()) { cc.B.CreateStore(else_v, result_slot); cc.B.CreateBr(merge_bb); }

    cc.B.SetInsertPoint(merge_bb);
    return cc.B.CreateLoad(cc.i64_t, result_slot, "cond_val");
}

/* ---- do ---- */

static Value *emit_do(CompileCtx &cc, val_t rst) {
    val_t var_specs   = as_pair(rst)->car;
    val_t test_clause = as_pair(as_pair(rst)->cdr)->car;
    val_t body        = as_pair(as_pair(rst)->cdr)->cdr;

    val_t test_expr  = as_pair(test_clause)->car;
    val_t result_seq = as_pair(test_clause)->cdr;

    struct VarSpec { std::string name; val_t init; val_t step; bool has_step; };
    std::vector<VarSpec> vars;
    for (val_t vs = var_specs; vis_pair(vs); vs = as_pair(vs)->cdr) {
        val_t spec      = as_pair(vs)->car;
        std::string nm  = as_sym(as_pair(spec)->car)->data;
        val_t init      = as_pair(as_pair(spec)->cdr)->car;
        val_t step_cell = as_pair(as_pair(spec)->cdr)->cdr;
        bool  has_step  = vis_pair(step_cell);
        val_t step      = has_step ? as_pair(step_cell)->car : as_pair(spec)->car;
        vars.push_back({ nm, init, step, has_step });
    }

    auto *loop_bb = BasicBlock::Create(cc.llvm_ctx, "do_loop", cc.fn);
    auto *body_bb = BasicBlock::Create(cc.llvm_ctx, "do_body", cc.fn);
    auto *done_bb = BasicBlock::Create(cc.llvm_ctx, "do_done", cc.fn);

    cc.push_scope();
    std::vector<AllocaInst *> slots;
    for (auto &vs : vars) {
        Value *iv   = emit_expr(cc, vs.init);
        auto  *slot = cc.alloc_root(vs.name);
        cc.B.CreateStore(iv, slot);
        cc.bind(vs.name, slot);
        slots.push_back(slot);
    }
    cc.B.CreateBr(loop_bb);

    cc.B.SetInsertPoint(loop_bb);
    Value *tv   = emit_expr(cc, test_expr);
    cc.B.CreateCondBr(cc.B.CreateICmpNE(tv, cc.V_FALSE_c), done_bb, body_bb);

    cc.B.SetInsertPoint(body_bb);
    for (val_t e = body; vis_pair(e); e = as_pair(e)->cdr)
        emit_expr(cc, as_pair(e)->car);
    /* Compute all step values before storing any (parallel step semantics). */
    std::vector<Value *> new_vals;
    for (int i = 0; i < (int)vars.size(); ++i)
        new_vals.push_back(emit_expr(cc, vars[i].step));
    for (int i = 0; i < (int)vars.size(); ++i)
        cc.B.CreateStore(new_vals[i], slots[i]);
    cc.B.CreateBr(loop_bb);

    cc.B.SetInsertPoint(done_bb);
    Value *result = cc.V_VOID_c;
    for (val_t e = result_seq; vis_pair(e); e = as_pair(e)->cdr)
        result = emit_expr(cc, as_pair(e)->car);
    cc.pop_scope();
    return result;
}

/* ---- Main expression emitter ---- */

static Value *emit_expr(CompileCtx &cc, val_t expr) {

    /* ---- Symbol — variable reference ---- */
    if (vis_symbol(expr)) {
        const char *name = as_sym(expr)->data;
        if (Binding *b = cc.lookup(name)) {
            if (b->is_cell) {
                Value *cell_addr = cc.B.CreateLoad(cc.i64_t, b->slot,
                                                   std::string(name) + "_cell");
                Value *cell_ptr  = cc.B.CreateIntToPtr(cell_addr, cc.ptr_t);
                return cc.B.CreateLoad(cc.i64_t, cell_ptr, name);
            }
            return cc.B.CreateLoad(cc.i64_t, b->slot, name);
        }
        return emit_global_lookup(cc, expr);
    }

    /* ---- Self-evaluating atoms ---- */
    if (!vis_pair(expr))
        return emit_literal(cc, expr);

    val_t op  = as_pair(expr)->car;
    val_t rst = as_pair(expr)->cdr;

    /* ---- (quote datum) ---- */
    if (vis_symbol(op) && op == S("quote"))
        return emit_literal(cc, vis_pair(rst) ? as_pair(rst)->car : V_NIL);

    /* ---- (if test consequent [alternate]) ---- */
    if (vis_symbol(op) && op == S("if")) {
        val_t test = as_pair(rst)->car;
        val_t cons = as_pair(as_pair(rst)->cdr)->car;
        val_t alt  = vis_pair(as_pair(as_pair(rst)->cdr)->cdr)
                       ? as_pair(as_pair(as_pair(rst)->cdr)->cdr)->car
                       : V_VOID;

        Value *tv = emit_expr(cc, test);
        auto *then_bb  = BasicBlock::Create(cc.llvm_ctx, "then", cc.fn);
        auto *else_bb  = BasicBlock::Create(cc.llvm_ctx, "else", cc.fn);
        auto *merge_bb = BasicBlock::Create(cc.llvm_ctx, "merge", cc.fn);
        auto *result_slot = cc.alloc_root("if_r");

        cc.B.CreateCondBr(cc.B.CreateICmpNE(tv, cc.V_FALSE_c), then_bb, else_bb);

        cc.B.SetInsertPoint(then_bb);
        Value *then_v = emit_expr(cc, cons);
        if (!cc.terminated()) { cc.B.CreateStore(then_v, result_slot); cc.B.CreateBr(merge_bb); }

        cc.B.SetInsertPoint(else_bb);
        Value *else_v = emit_expr(cc, alt);
        if (!cc.terminated()) { cc.B.CreateStore(else_v, result_slot); cc.B.CreateBr(merge_bb); }

        cc.B.SetInsertPoint(merge_bb);
        return cc.B.CreateLoad(cc.i64_t, result_slot, "if_result");
    }

    /* ---- (cond clause...) ---- */
    if (vis_symbol(op) && op == S("cond"))
        return emit_cond(cc, rst);

    /* ---- (case key clause...) ---- */
    if (vis_symbol(op) && op == S("case")) {
        val_t key_expr = as_pair(rst)->car;
        val_t clauses  = as_pair(rst)->cdr;
        Value *key_v  = emit_expr(cc, key_expr);
        auto  *key_slot = cc.alloc_root("case_key");
        cc.B.CreateStore(key_v, key_slot);

        auto *lookup_ft  = FunctionType::get(cc.i64_t, { cc.i64_t }, false);
        auto *lookup_fn  = declare_helper(cc.M, "curry_jit_global_lookup", lookup_ft);
        val_t  eqv_sym_v = S("eqv?");
        Value *eqv_proc  = emit_statepoint_call(cc, lookup_fn, lookup_ft,
            { ConstantInt::get(cc.i64_t, (uint64_t)eqv_sym_v) });

        std::function<Value*(val_t)> emit_case_clauses = [&](val_t cls) -> Value * {
            if (vis_nil(cls)) return cc.V_VOID_c;
            val_t clause = as_pair(cls)->car;
            val_t datums = as_pair(clause)->car;
            val_t cbody  = as_pair(clause)->cdr;

            if (vis_symbol(datums) && datums == S("else")) {
                Value *r = cc.V_VOID_c;
                for (val_t e = cbody; vis_pair(e); e = as_pair(e)->cdr)
                    r = emit_expr(cc, as_pair(e)->car);
                return r;
            }

            Value *match_v = cc.V_FALSE_c;
            for (val_t d = datums; vis_pair(d); d = as_pair(d)->cdr) {
                val_t datum = as_pair(d)->car;
                Value *kv = cc.B.CreateLoad(cc.i64_t, key_slot);
                Value *r  = emit_apply(cc, eqv_proc,
                    { kv, emit_literal(cc, datum) });
                auto *slot2 = cc.alloc_root("case_m");
                cc.B.CreateStore(match_v, slot2);
                Value *cur  = cc.B.CreateLoad(cc.i64_t, slot2);
                auto *merged = cc.B.CreateSelect(
                    cc.B.CreateICmpNE(r, cc.V_FALSE_c), cc.V_TRUE_c, cur);
                match_v = merged;
            }

            auto *then_bb  = BasicBlock::Create(cc.llvm_ctx, "case_t", cc.fn);
            auto *else_bb  = BasicBlock::Create(cc.llvm_ctx, "case_e", cc.fn);
            auto *mrg_bb   = BasicBlock::Create(cc.llvm_ctx, "case_m", cc.fn);

            cc.B.CreateCondBr(cc.B.CreateICmpNE(match_v, cc.V_FALSE_c),
                               then_bb, else_bb);

            cc.B.SetInsertPoint(then_bb);
            Value *tv = cc.V_VOID_c;
            for (val_t e = cbody; vis_pair(e); e = as_pair(e)->cdr)
                tv = emit_expr(cc, as_pair(e)->car);
            cc.B.CreateBr(mrg_bb);
            BasicBlock *te = cc.B.GetInsertBlock();

            cc.B.SetInsertPoint(else_bb);
            Value *ev = emit_case_clauses(as_pair(cls)->cdr);
            cc.B.CreateBr(mrg_bb);
            BasicBlock *ee = cc.B.GetInsertBlock();

            cc.B.SetInsertPoint(mrg_bb);
            auto *phi = cc.B.CreatePHI(cc.i64_t, 2);
            phi->addIncoming(tv, te);
            phi->addIncoming(ev, ee);
            return phi;
        };
        return emit_case_clauses(clauses);
    }

    /* ---- (and e...) ---- */
    if (vis_symbol(op) && op == S("and")) return emit_and(cc, rst);

    /* ---- (or e...) ---- */
    if (vis_symbol(op) && op == S("or"))  return emit_or(cc, rst);

    /* ---- (when test e...) ---- */
    if (vis_symbol(op) && op == S("when")) {
        Value *tv = emit_expr(cc, as_pair(rst)->car);
        auto *then_bb = BasicBlock::Create(cc.llvm_ctx, "when_t", cc.fn);
        auto *done_bb = BasicBlock::Create(cc.llvm_ctx, "when_d", cc.fn);
        cc.B.CreateCondBr(cc.B.CreateICmpNE(tv, cc.V_FALSE_c), then_bb, done_bb);
        cc.B.SetInsertPoint(then_bb);
        for (val_t e = as_pair(rst)->cdr; vis_pair(e); e = as_pair(e)->cdr)
            emit_expr(cc, as_pair(e)->car);
        cc.B.CreateBr(done_bb);
        cc.B.SetInsertPoint(done_bb);
        return cc.V_VOID_c;
    }

    /* ---- (unless test e...) ---- */
    if (vis_symbol(op) && op == S("unless")) {
        Value *tv = emit_expr(cc, as_pair(rst)->car);
        auto *then_bb = BasicBlock::Create(cc.llvm_ctx, "unls_t", cc.fn);
        auto *done_bb = BasicBlock::Create(cc.llvm_ctx, "unls_d", cc.fn);
        cc.B.CreateCondBr(cc.B.CreateICmpEQ(tv, cc.V_FALSE_c), then_bb, done_bb);
        cc.B.SetInsertPoint(then_bb);
        for (val_t e = as_pair(rst)->cdr; vis_pair(e); e = as_pair(e)->cdr)
            emit_expr(cc, as_pair(e)->car);
        cc.B.CreateBr(done_bb);
        cc.B.SetInsertPoint(done_bb);
        return cc.V_VOID_c;
    }

    /* ---- (begin e...) ---- */
    if (vis_symbol(op) && op == S("begin")) {
        Value *result = cc.V_VOID_c;
        for (val_t es = rst; vis_pair(es); es = as_pair(es)->cdr)
            result = emit_expr(cc, as_pair(es)->car);
        return result;
    }

    /* ---- (lambda params body...) ---- */
    if (vis_symbol(op) && op == S("lambda")) {
        val_t params = as_pair(rst)->car;
        val_t body   = as_pair(rst)->cdr;
        return emit_lambda(cc, params, body);
    }

    /* ---- (define ...) ---- */
    if (vis_symbol(op) && op == S("define")) {
        val_t first = as_pair(rst)->car;
        std::string nm;
        Value *val_v;

        if (vis_symbol(first)) {
            nm    = as_sym(first)->data;
            val_v = emit_expr(cc, as_pair(as_pair(rst)->cdr)->car);
        } else if (vis_pair(first)) {
            val_t name_sym = as_pair(first)->car;
            val_t params   = as_pair(first)->cdr;
            val_t body2    = as_pair(rst)->cdr;
            nm    = as_sym(name_sym)->data;
            val_v = emit_lambda(cc, params, body2);
        } else {
            throw std::runtime_error("define: invalid form");
        }

        if (cc.top_level) {
            return emit_global_define(cc, S(nm.c_str()), val_v);
        } else {
            if (Binding *b = cc.lookup(nm)) {
                if (b->is_cell) {
                    Value *cell_addr = cc.B.CreateLoad(cc.i64_t, b->slot);
                    Value *cell_ptr  = cc.B.CreateIntToPtr(cell_addr, cc.ptr_t);
                    cc.B.CreateStore(val_v, cell_ptr);
                } else {
                    cc.B.CreateStore(val_v, b->slot);
                }
            } else {
                auto *slot = cc.alloc_root(nm);
                cc.B.CreateStore(val_v, slot);
                cc.bind(nm, slot);
            }
            return cc.V_VOID_c;
        }
    }

    /* ---- (let bindings body...) and (let name bindings body...) ---- */
    if (vis_symbol(op) && op == S("let")) {
        val_t first = as_pair(rst)->car;

        if (vis_symbol(first)) {
            /* Named let: (let name ((v e)...) body...)
             *
             * Compiled as an LLVM loop with a backedge rather than a recursive
             * lambda, so self-calls become branches (no C-stack growth).
             * When emit_call sees a call to `name` inside the body it stores
             * the new argument values to the variable slots and branches to
             * loop_bb.  Any other tail value stores to result_slot and falls
             * through to exit_bb.
             */
            std::string loop_nm = as_sym(first)->data;
            val_t bindings = as_pair(as_pair(rst)->cdr)->car;
            val_t body     = as_pair(as_pair(rst)->cdr)->cdr;

            SmallVector<std::string, 8> var_names;
            SmallVector<val_t, 8>       init_exprs;
            for (val_t b = bindings; vis_pair(b); b = as_pair(b)->cdr) {
                val_t bnd = as_pair(b)->car;
                var_names.push_back(as_sym(as_pair(bnd)->car)->data);
                init_exprs.push_back(as_pair(as_pair(bnd)->cdr)->car);
            }

            auto *loop_bb    = BasicBlock::Create(cc.llvm_ctx, "nlet_loop",   cc.fn);
            auto *exit_bb    = BasicBlock::Create(cc.llvm_ctx, "nlet_exit",   cc.fn);
            auto *result_slot = cc.alloc_root("nlet_result");

            cc.push_scope();
            std::vector<AllocaInst *> var_slots;
            for (int i = 0; i < (int)var_names.size(); ++i) {
                Value *iv   = emit_expr(cc, init_exprs[i]);
                auto  *slot = cc.alloc_root(var_names[i]);
                cc.B.CreateStore(iv, slot);
                cc.bind(var_names[i], slot);
                var_slots.push_back(slot);
            }

            cc.B.CreateBr(loop_bb);
            cc.B.SetInsertPoint(loop_bb);

            cc.named_lets.push_back(NamedLetCtx{loop_nm, var_slots, loop_bb, exit_bb, result_slot});

            Value *body_result = emit_body(cc, body);

            cc.named_lets.pop_back();

            if (!cc.terminated()) {
                cc.B.CreateStore(body_result, result_slot);
                cc.B.CreateBr(exit_bb);
            }

            cc.pop_scope();
            cc.B.SetInsertPoint(exit_bb);
            return cc.B.CreateLoad(cc.i64_t, result_slot, "nlet_val");
        }

        /* Regular let. */
        val_t bindings = first;
        val_t body     = as_pair(rst)->cdr;
        cc.push_scope();
        for (val_t b = bindings; vis_pair(b); b = as_pair(b)->cdr) {
            val_t bnd = as_pair(b)->car;
            const char *vn = as_sym(as_pair(bnd)->car)->data;
            Value *iv      = emit_expr(cc, as_pair(as_pair(bnd)->cdr)->car);
            auto  *slot    = cc.alloc_root(vn);
            cc.B.CreateStore(iv, slot);
            cc.bind(vn, slot);
        }
        Value *result = emit_body(cc, body);
        cc.pop_scope();
        return result;
    }

    /* ---- (let* bindings body...) ---- */
    if (vis_symbol(op) && op == S("let*")) {
        val_t bindings = as_pair(rst)->car;
        val_t body     = as_pair(rst)->cdr;
        cc.push_scope();
        for (val_t b = bindings; vis_pair(b); b = as_pair(b)->cdr) {
            val_t bnd  = as_pair(b)->car;
            const char *vn = as_sym(as_pair(bnd)->car)->data;
            Value *iv  = emit_expr(cc, as_pair(as_pair(bnd)->cdr)->car);
            auto  *slot = cc.alloc_root(vn);
            cc.B.CreateStore(iv, slot);
            cc.bind(vn, slot);
        }
        Value *result = emit_body(cc, body);
        cc.pop_scope();
        return result;
    }

    /* ---- (letrec / letrec* bindings body...) ---- */
    if (vis_symbol(op) && (op == S("letrec") || op == S("letrec*"))) {
        val_t bindings = as_pair(rst)->car;
        val_t body     = as_pair(rst)->cdr;
        cc.push_scope();
        struct BSpec { std::string name; val_t init; AllocaInst *slot; };
        std::vector<BSpec> specs;
        for (val_t b = bindings; vis_pair(b); b = as_pair(b)->cdr) {
            val_t bnd  = as_pair(b)->car;
            std::string nm = as_sym(as_pair(bnd)->car)->data;
            auto *slot = cc.alloc_root(nm);
            Value *cell = emit_alloc_cell(cc, cc.V_FALSE_c);
            cc.B.CreateStore(cell, slot);
            cc.bind(nm, slot, true, /*is_cell=*/true);
            specs.push_back({ nm, as_pair(as_pair(bnd)->cdr)->car, slot });
        }
        for (auto &sp : specs) {
            Value *v = emit_expr(cc, sp.init);
            Value *cell_addr = cc.B.CreateLoad(cc.i64_t, sp.slot);
            Value *cell_ptr  = cc.B.CreateIntToPtr(cell_addr, cc.ptr_t);
            cc.B.CreateStore(v, cell_ptr);
        }
        Value *result = emit_body(cc, body);
        cc.pop_scope();
        return result;
    }

    /* ---- (set! var val) ---- */
    if (vis_symbol(op) && op == S("set!")) {
        const char *vn   = as_sym(as_pair(rst)->car)->data;
        Value     *val_v = emit_expr(cc, as_pair(as_pair(rst)->cdr)->car);
        if (Binding *b = cc.lookup(vn)) {
            if (b->is_cell) {
                Value *cell_addr = cc.B.CreateLoad(cc.i64_t, b->slot);
                Value *cell_ptr  = cc.B.CreateIntToPtr(cell_addr, cc.ptr_t);
                cc.B.CreateStore(val_v, cell_ptr);
            } else {
                cc.B.CreateStore(val_v, b->slot);
            }
            return cc.V_VOID_c;
        }
        return emit_global_define(cc, S(vn), val_v);
    }

    /* ---- (do var-specs (test result...) body...) ---- */
    if (vis_symbol(op) && op == S("do"))
        return emit_do(cc, rst);

    /* ---- procedure call ---- */
    return emit_call(cc, op, rst);
}

/* ---- Module entry points ---- */

static std::unique_ptr<llvm::Module> make_module(LLVMContext &llvm_ctx,
                                                   const std::string &name) {
    register_gc_strategy();
    auto M = std::make_unique<llvm::Module>(name, llvm_ctx);
    M->setDataLayout("");
    return M;
}

static Function *make_fn(llvm::Module &M, LLVMContext &ctx,
                          const std::string &name,
                          FunctionType *ft) {
    auto *f = Function::Create(ft, Function::ExternalLinkage, name, M);
    f->setGC("curry-generational");
    f->addFnAttr(Attribute::NoUnwind);
    BasicBlock::Create(ctx, "entry", f);
    return f;
}

std::unique_ptr<llvm::Module> codegen_thunk(LLVMContext &llvm_ctx,
                                       val_t thunk,
                                       const std::string &name) {
    if (!vis_closure(thunk))
        throw std::runtime_error("codegen_thunk: not a T_CLOSURE");

    auto M  = make_module(llvm_ctx, name);
    auto *ft = FunctionType::get(Type::getInt64Ty(llvm_ctx), {}, false);
    auto *fn = make_fn(*M, llvm_ctx, name, ft);

    CompileCtx cc(llvm_ctx, *M, fn, /*top_level=*/false);
    cc.push_scope();
    Closure *cl = as_clos(thunk);
    Value *result = emit_body(cc, cl->body);
    cc.pop_scope();
    if (!cc.terminated()) cc.B.CreateRet(result);

    std::string err;
    llvm::raw_string_ostream es(err);
    if (verifyModule(*M, &es))
        throw std::runtime_error("codegen_thunk: " + err);
    return M;
}

std::unique_ptr<llvm::Module> codegen_procedure(LLVMContext &llvm_ctx,
                                           val_t proc,
                                           const std::string &name) {
    if (!vis_closure(proc))
        throw std::runtime_error("codegen_procedure: not a T_CLOSURE");

    auto M  = make_module(llvm_ctx, name);
    auto *ft = scheme_fn_type(llvm_ctx);
    auto *fn = make_fn(*M, llvm_ctx, name, ft);

    CompileCtx cc(llvm_ctx, *M, fn, /*top_level=*/false);
    auto *argc_arg = fn->getArg(0); argc_arg->setName("argc");
    auto *argv_arg = fn->getArg(1); argv_arg->setName("argv");

    Closure *cl = as_clos(proc);
    cc.push_scope();
    int idx = 0;
    for (val_t ps = cl->params; vis_pair(ps); ps = as_pair(ps)->cdr, ++idx) {
        val_t pn = as_pair(ps)->car;
        if (!vis_symbol(pn))
            throw std::runtime_error("codegen_procedure: non-symbol param");
        const char *pstr = as_sym(pn)->data;
        auto *gep  = cc.B.CreateGEP(cc.i64_t, argv_arg,
                                     ConstantInt::get(cc.i32_t, idx));
        auto *pval = cc.B.CreateLoad(cc.i64_t, gep, pstr);
        auto *slot = cc.alloc_root(pstr);
        cc.B.CreateStore(pval, slot);
        cc.bind(pstr, slot);
    }
    Value *result = emit_body(cc, cl->body);
    cc.pop_scope();
    if (!cc.terminated()) cc.B.CreateRet(result);

    std::string err;
    llvm::raw_string_ostream es(err);
    if (verifyModule(*M, &es))
        throw std::runtime_error("codegen_procedure: " + err);
    return M;
}

std::unique_ptr<llvm::Module> codegen_file(LLVMContext &llvm_ctx,
                                            val_t forms,
                                            const std::string &module_name) {
    auto M  = make_module(llvm_ctx, module_name);
    auto *ft = FunctionType::get(Type::getInt64Ty(llvm_ctx), {}, false);
    auto *fn = make_fn(*M, llvm_ctx, "_curry_toplevel", ft);

    CompileCtx cc(llvm_ctx, *M, fn, /*top_level=*/true);
    cc.push_scope();
    Value *result = emit_body(cc, forms);
    cc.pop_scope();
    if (!cc.terminated()) cc.B.CreateRet(result);

    std::string err;
    llvm::raw_string_ostream es(err);
    if (verifyModule(*M, &es))
        throw std::runtime_error("codegen_file: " + err);
    return M;
}

std::unique_ptr<llvm::Module> codegen_expr(LLVMContext &llvm_ctx,
                                            val_t expr,
                                            const std::string &name) {
    auto M  = make_module(llvm_ctx, name);
    auto *ft = FunctionType::get(Type::getInt64Ty(llvm_ctx), {}, false);
    auto *fn = make_fn(*M, llvm_ctx, name, ft);

    CompileCtx cc(llvm_ctx, *M, fn, /*top_level=*/true);
    cc.push_scope();
    Value *result = emit_expr(cc, expr);
    cc.pop_scope();
    if (!cc.terminated()) cc.B.CreateRet(result);

    std::string err;
    llvm::raw_string_ostream es(err);
    if (verifyModule(*M, &es))
        throw std::runtime_error("codegen_expr: " + err);
    return M;
}

bool codegen_emit_ir(llvm::Module &M, const std::string &path) {
    std::error_code ec;
    llvm::raw_fd_ostream out(path, ec, llvm::sys::fs::OF_Text);
    if (ec) return false;
    M.print(out, nullptr);
    return true;
}

bool codegen_emit_bc(llvm::Module &M, const std::string &path) {
    std::error_code ec;
    llvm::raw_fd_ostream out(path, ec, llvm::sys::fs::OF_None);
    if (ec) return false;
    WriteBitcodeToFile(M, out);
    return true;
}

} /* namespace curry */
