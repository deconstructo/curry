/*
 * Curry ORC JIT v2 session implementation.
 *
 * Lifecycle:
 *   1. curry_llvm_init() → JITSession::instance() → JITSession ctor
 *   2. JITSession::register_runtime_symbols() → exposes all C helper
 *      functions to JIT'd code via an absolute-symbol JITDylib.
 *   3. Each hot BcClosure:
 *        codegen_procedure()       → LLVM Module
 *        JITSession::add_module() → compiled and linked
 *        JITSession::lookup()  → function pointer
 *        hot-swap via jit_val → result val_t
 *
 * Runtime symbol table
 * --------------------
 * JIT'd code calls helpers registered below.  These are thin wrappers around
 * the interpreter's own C functions, keeping the JIT-runtime ABI stable.
 *
 * Stack-map reader
 * ----------------
 * Every object file produced by LLJIT that contains a .llvm_stackmaps section
 * is processed by curry_gc_register_stackmap() which records the safe-point
 * table for the GC's root enumeration walk (future: generational GC).
 */

#include "jit.h"
#include "codegen.h"
#include "gc_strategy.h"

extern "C" {
#include "../eval.h"
/* jit_depth_push/pop are declared in eval.h (C-linkage wrappers for the TLS counter) */
#include "../value.h"
#include "../object.h"
#include "../gc.h"
#include "../numeric.h"
}

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/Orc/ExecutionUtils.h>
#include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/JITSymbol.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Transforms/Scalar/Reassociate.h>
#include <llvm/Transforms/Scalar/SimplifyCFG.h>
#include <llvm/Passes/PassBuilder.h>

#include <stdexcept>
#include <cassert>

using namespace llvm;
using namespace llvm::orc;

/* ---- C runtime helpers exposed to JIT'd code ---- */
/* Declared extern "C" so they have stable mangled names. */

extern "C" {

/* Apply a Scheme procedure to argc/argv.  Depth-guarded: when
 * g_jit_call_depth reaches JIT_CALL_DEPTH_LIMIT the JIT redirect in
 * apply_arr is bypassed and the bytecode interpreter runs instead, giving
 * proper O(1) C-stack tail-call semantics for deep recursion. */
static uint64_t curry_jit_apply_arr(uint64_t proc_val, int argc, uint64_t *argv) {
    jit_depth_push();
    uint64_t r = (uint64_t)apply_arr((val_t)proc_val, argc, (val_t *)argv);
    jit_depth_pop();
    return r;
}

/* ---- Numeric comparison wrappers (bool → val_t) ---- */
static uint64_t curry_jit_lt(uint64_t a, uint64_t b)
    { return num_lt((val_t)a,(val_t)b) ? V_TRUE : V_FALSE; }
static uint64_t curry_jit_gt(uint64_t a, uint64_t b)
    { return num_gt((val_t)a,(val_t)b) ? V_TRUE : V_FALSE; }
static uint64_t curry_jit_le(uint64_t a, uint64_t b)
    { return num_le((val_t)a,(val_t)b) ? V_TRUE : V_FALSE; }
static uint64_t curry_jit_ge(uint64_t a, uint64_t b)
    { return num_ge((val_t)a,(val_t)b) ? V_TRUE : V_FALSE; }
static uint64_t curry_jit_num_eq(uint64_t a, uint64_t b)
    { return num_eq((val_t)a,(val_t)b) ? V_TRUE : V_FALSE; }

/* Global variable lookup by symbol val_t.  Returns the binding's value. */
static uint64_t curry_jit_global_lookup(uint64_t sym_val) {
    val_t *slot = env_lookup_slot(GLOBAL_ENV, (val_t)sym_val);
    if (!slot)
        scm_raise(V_FALSE, "unbound variable");
    return (uint64_t)*slot;
}

/* Define or redefine a global variable. */
static void curry_jit_global_define(uint64_t sym_val, uint64_t val) {
    env_define(GLOBAL_ENV, (val_t)sym_val, (val_t)val);
}

/* Allocate a mutable cell holding one i64 for cell-based variable capture.
 *
 * The cell lives in Boehm (GC_MALLOC) rather than the nursery because:
 *  a) it has no Hdr type tag, so the nursery evacuator cannot size it;
 *  b) under the generational backend the cell's VALUE (a val_t) may point
 *     into the nursery — gc_register_root ensures minor GC updates it.
 *
 * Returns the cell address as a raw integer captured in JitClosure.caps[]. */
static uint64_t curry_jit_alloc_cell(uint64_t v) {
    uint64_t *cell = (uint64_t *)GC_MALLOC(sizeof(uint64_t));
    *cell = v;
    gc_register_root(cell);  /* keep cell VALUE updated after nursery promotion */
    return (uint64_t)cell;
}

/* Allocate a JIT closure wrapping a native function with a captured environment.
 * fn:     i64 (*)(i32 argc, i64 *argv, i64 *caps)
 * n_caps: number of captured values
 * caps:   pointer to array of captured val_t values (copied into the closure)
 *
 * JitClosure is GC:PIN — it must never enter the nursery because:
 *  a) the native function pointer (fn) must not move;
 *  b) BcClosure.jit_val holds a raw JitClosure* — if the closure moved,
 *     all BcClosures with that raw pointer would dangle.
 * Use gc_alloc_pinned so the generational backend keeps it in Boehm. */
static uint64_t curry_jit_make_closure(void *fn, int32_t n_caps, uint64_t *caps) {
    JitClosure *jc = (JitClosure *)gc_alloc_pinned(
        sizeof(JitClosure) + (size_t)n_caps * sizeof(val_t));
    jc->hdr.type  = T_JITCLOSURE;
    jc->hdr.flags = 0;
    jc->hdr.fwd   = 0;
    jc->fn        = (void *)GC_HIDE_POINTER(fn);
    jc->n_caps    = (uint32_t)n_caps;
    for (int32_t i = 0; i < n_caps; i++)
        jc->caps[i] = (val_t)caps[i];
    return (uint64_t)vptr(jc);
}

/* Build a Scheme list from argv[0..n-1]. */
static uint64_t curry_jit_list_tail(uint64_t *argv, int32_t n) {
    val_t lst = V_NIL;
    for (int32_t i = n - 1; i >= 0; i--) {
        Pair *p = CURRY_NEW(Pair);
        p->hdr.type = T_PAIR; p->hdr.flags = 0; p->hdr.fwd = 0;
        p->car = (val_t)argv[i]; p->cdr = lst;
        lst = vptr(p);
    }
    return (uint64_t)lst;
}

/* ---- GC inhibit wrappers (callable from JIT-compiled LLVM IR) ---- */
/*
 * gc_inhibit_minor() / gc_resume_minor() are no-ops in C++ (TLS gc_inhibit_count
 * is not visible from __cplusplus).  These plain-C-linkage wrappers delegate to
 * the C implementations in gc.c so JIT code can inhibit minor GC around every
 * statepoint call (preventing minor GC from firing while live nursery pointers
 * reside in JIT alloca slots that the GC cannot yet enumerate).
 */
static void curry_gc_inhibit_minor_jit(void) { gc_inhibit_minor_fn(); }
static void curry_gc_resume_minor_jit(void)  { gc_resume_minor_fn(); }

} /* extern "C" */

namespace curry {

/* ---- Unwrap LLVM Expected<T>, throw on error ---- */

template<typename T>
static T unwrap(Expected<T> e) {
    if (!e) {
        std::string msg;
        raw_string_ostream s(msg);
        s << e.takeError();
        throw std::runtime_error("LLVM JIT error: " + msg);
    }
    return std::move(*e);
}

static void check(Error e) {
    if (e) {
        std::string msg;
        raw_string_ostream s(msg);
        s << std::move(e);
        throw std::runtime_error("LLVM JIT error: " + msg);
    }
}

/* ---- JITSession ---- */

JITSession &JITSession::instance() {
    static JITSession s;
    return s;
}

JITSession::JITSession() {
    /* Initialise native target + asm printer + asm parser. */
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();

    register_gc_strategy();

    /* Build LLJIT. */
    lljit_ = unwrap(LLJITBuilder().create());

    /* Attach a simple -O1 optimisation transform. */
    lljit_->getIRTransformLayer().setTransform(
        [](orc::ThreadSafeModule TSM,
           const orc::MaterializationResponsibility &) -> Expected<orc::ThreadSafeModule> {
            TSM.withModuleDo([](llvm::Module &M) {
                PassBuilder PB;
                LoopAnalysisManager LAM;
                FunctionAnalysisManager FAM;
                CGSCCAnalysisManager CGAM;
                ModuleAnalysisManager MAM;
                PB.registerModuleAnalyses(MAM);
                PB.registerCGSCCAnalyses(CGAM);
                PB.registerFunctionAnalyses(FAM);
                PB.registerLoopAnalyses(LAM);
                PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);
                ModulePassManager MPM =
                    PB.buildPerModuleDefaultPipeline(OptimizationLevel::O1);
                MPM.run(M, MAM);
            });
            return TSM;
        });

    /* Add DynamicLibrarySearchGenerator so JIT'd code can call libc etc. */
    auto &main_jd = lljit_->getMainJITDylib();
    main_jd.addGenerator(
        unwrap(DynamicLibrarySearchGenerator::GetForCurrentProcess(
            lljit_->getDataLayout().getGlobalPrefix())));

    register_runtime_symbols();
}

void JITSession::register_runtime_symbols() {
    /* Expose our C helpers to JIT'd code as absolute symbols. */
    auto &jd = lljit_->getMainJITDylib();
    auto &es = lljit_->getExecutionSession();

    /* On macOS the object format prepends '_' to C symbol names.
     * Prefix all registered names with the platform's global prefix so the
     * JIT can find them when resolving IR calls. */
    char pfx = lljit_->getDataLayout().getGlobalPrefix();
    auto mangle = [&](const char *name) -> std::string {
        return pfx ? std::string(1, pfx) + name : std::string(name);
    };

    SymbolMap syms;
    auto add = [&](const char *name, void *ptr) {
#if LLVM_VERSION_MAJOR >= 17
        syms[es.intern(mangle(name))] = {
            ExecutorAddr::fromPtr(ptr),
            JITSymbolFlags::Exported | JITSymbolFlags::Callable
        };
#else
        syms[es.intern(mangle(name))] = JITEvaluatedSymbol(
            ExecutorAddr::fromPtr(ptr).getValue(),
            JITSymbolFlags::Exported | JITSymbolFlags::Callable);
#endif
    };

    add("curry_jit_apply_arr",          (void *)curry_jit_apply_arr);
    add("curry_jit_global_lookup",      (void *)curry_jit_global_lookup);
    add("curry_jit_global_define",      (void *)curry_jit_global_define);
    add("curry_jit_alloc_cell",         (void *)curry_jit_alloc_cell);
    add("curry_jit_make_closure",       (void *)curry_jit_make_closure);
    add("curry_jit_list_tail",          (void *)curry_jit_list_tail);
    add("curry_gc_inhibit_minor_jit",   (void *)curry_gc_inhibit_minor_jit);
    add("curry_gc_resume_minor_jit",    (void *)curry_gc_resume_minor_jit);

    /* Comparison wrappers (bool → val_t). */
    add("curry_jit_lt",            (void *)curry_jit_lt);
    add("curry_jit_gt",            (void *)curry_jit_gt);
    add("curry_jit_le",            (void *)curry_jit_le);
    add("curry_jit_ge",            (void *)curry_jit_ge);
    add("curry_jit_num_eq",        (void *)curry_jit_num_eq);

    /* Direct arithmetic (val_t → val_t). */
    add("num_add",      (void *)num_add);
    add("num_sub",      (void *)num_sub);
    add("num_mul",      (void *)num_mul);
    add("num_div",      (void *)num_div);
    add("num_log",      (void *)num_log);
    add("num_inexact",  (void *)num_inexact);
    add("num_exp",      (void *)num_exp);
    add("num_sqrt",     (void *)num_sqrt);
    add("num_sin",      (void *)num_sin);
    add("num_cos",      (void *)num_cos);
    add("num_abs",      (void *)num_abs);
    add("num_floor",    (void *)num_floor);
    add("num_ceiling",  (void *)num_ceiling);
    add("num_truncate", (void *)num_truncate);
    add("num_round",    (void *)num_round);

    check(jd.define(absoluteSymbols(std::move(syms))));
}

void JITSession::add_module(std::unique_ptr<llvm::Module> M,
                             std::unique_ptr<LLVMContext> ctx) {
    /* Stamp the data layout so codegen matches the JIT target. */
    M->setDataLayout(lljit_->getDataLayout());

    check(lljit_->addIRModule(
        ThreadSafeModule(std::move(M), std::move(ctx))));
}

uint64_t JITSession::lookup(const std::string &name) {
    auto sym = unwrap(lljit_->lookup(name));
    return sym.getValue();
}

std::unique_ptr<LLVMContext> JITSession::make_context() {
    return std::make_unique<LLVMContext>();
}

JITSession::~JITSession() = default;

} /* namespace curry */
