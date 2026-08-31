/*
 * compiler_classic.c — the classic (pre-IR) special-form dispatcher.
 *
 * Split out of the old single-file compiler.c (pure code motion, no
 * behavior change -- see compiler.c's own header comment for the full
 * five-way split). Holds every classic compile_* special-form compiler,
 * classify_head (shared with ir_lower's own dispatch), and compile()/
 * compile_classic()/compile_seq() -- the top-level dispatcher, its
 * genuinely-IR-free reference-implementation variant (used by
 * compiler_ir_checks.c's differential self-checks), and the body/script
 * sequencer.
 *
 * Shared symbols (the real `Compiler` struct, foundation helpers defined in
 * compiler.c, the IR pipeline entry points defined in ir_lower.c/ir_emit.c)
 * are declared in compiler_internal.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "compiler.h"
#include "compiler_internal.h"
#include "chunk.h"
#include "ir.h"
#include "opcode.h"
#include "value.h"
#include "object.h"
#include "symbol.h"
#include "gc.h"
#include "numeric.h"
#include "env.h"
#include "eval.h"
#include "vm.h"
#include "builtins.h"
#include "lang_registry.h"
#include "profiling.h"
#include "reader.h"
#include "record_type.h"
#include "syntax_rules.h"
#include "sx_algebra.h"
#include "sx_pattern.h"
#include "curry_features.h"
#include "set.h"

/* Forces compile()'s own dispatch (specifically IR_OR_CLASSIC, below) to
 * always take the classic compile_X path, never ir_lower/ir_emit --
 * checked once per compile() call, so it applies to the WHOLE recursive
 * compile tree for as long as it's set, not just the top-level call.
 * That matters because compile_if/compile_let/etc. call compile()
 * itself (not some pluggable alternative) for their own subexpressions
 * -- a thread-local flag compile() checks at its own entry is what makes
 * "classic all the way down" achievable without duplicating compile()'s
 * entire dispatch switch a second time just to keep it classic-only
 * (which would also need to stay in sync with the real one forever).
 * Set/cleared by compile_classic (below), never anywhere else -- see its
 * own comment for why compiler_ir_self_check/compiler_ir_optimize_check
 * need this now that compile() itself routes through the IR. */
static CURRY_THREAD_LOCAL bool g_force_classic_compile = false;

static void compile_lambda(Compiler *parent, val_t params, val_t body,
                            const char *name, int line) {
    Compiler c;
    init_compiler(&c, parent, name);

    int arity = compile_params(&c, params);
    c.chunk->arity = arity;
    begin_scope(&c);  /* body scope: depth 1+, so compile_define uses OP_STORE_LOCAL */

    lambda_prescan(&c, body, line);

    compile_seq(&c, body, true, line);
    Chunk *ch = end_compiler(&c);

    /* Preserve source AST and upvalue names for tiered JIT hot-swap. */
    ch->src_lambda = scm_cons(S_LAMBDA, scm_cons(params, body));
    if (c.upval_count > 0) {
        ch->upval_names = (val_t *)gc_alloc_raw_pinned((size_t)c.upval_count * sizeof(val_t));
        for (int i = 0; i < c.upval_count; i++)
            ch->upval_names[i] = c.upvals[i].name;
    }

    /* In parent: emit OP_CLOSURE followed by upvalue descriptors */
    int ci = chunk_add_const(parent->chunk, (val_t)(uintptr_t)ch);
    emit_ab(parent, OP_CLOSURE, (uint8_t)ci, line);
    for (int i = 0; i < c.upval_count; i++) {
        chunk_emit(parent->chunk, c.upvals[i].is_local ? 1 : 0, line);
        chunk_emit(parent->chunk, (uint8_t)c.upvals[i].index,   line);
    }
}

/* ── Special forms ───────────────────────────────────────────────────── */

static void compile_if(Compiler *c, val_t args, bool tail, int line) {
    require_min_args(args, 2, "if");
    val_t test  = vcar(args);  args = vcdr(args);
    val_t then  = vcar(args);  args = vcdr(args);
    val_t els   = vis_pair(args) ? vcar(args) : V_VOID;

    compile(c, test, false, line);
    int else_jmp = emit_jump(c, OP_JUMP_FALSE, line);

    compile(c, then, tail, line);
    int end_jmp  = emit_jump(c, OP_JUMP, line);

    patch_jump(c, else_jmp);
    compile(c, els, tail, line);
    patch_jump(c, end_jmp);
}

/* Store half of a define, minus the trailing "define returns void": the
 * computed value must already be on top of the stack. Factored out of
 * emit_define_store (below) so compile_define_values can call it once
 * per bound name without accumulating one OP_VOID per name -- only ONE
 * OP_VOID should represent the whole define-values form's return, not
 * one per variable. */
void emit_define_store_novoid(Compiler *c, val_t name, int line) {
    if (c->scope_depth == 0) {
        /* Top-level: DEF_GLOBAL */
        emit_ab(c, OP_DEF_GLOBAL, (uint8_t)chunk_add_const(c->chunk, name), line);
    } else {
        /* Internal define: check if pre-declared local slot exists */
        int slot = resolve_local(c, name);
        if (slot >= 0 && c->locals[slot].depth < 0) {
            /* Initialise the pre-declared slot */
            c->locals[slot].depth = c->scope_depth;
            emit_ab(c, OP_STORE_LOCAL, (uint8_t)slot, line);
        } else {
            /* New local */
            add_local(c, name);
            mark_initialised(c);
            /* value already on stack; STORE_LOCAL to new slot */
            emit_ab(c, OP_STORE_LOCAL,
                    (uint8_t)(c->local_count - 1), line);
        }
    }
}

/* Emit the store half of a define: the computed value must already be on
 * top of the stack. Shared by compile_define (after compiling an ordinary
 * value expression) and compile_symbolic (after emitting hand-rolled,
 * lookup-hygienic call bytecode instead of a compilable AST value). */
void emit_define_store(Compiler *c, val_t name, int line) {
    emit_define_store_novoid(c, name, line);
    emit(c, OP_VOID, line);   /* define returns void */
}

static void compile_define(Compiler *c, val_t args, int line) {
    require_min_args(args, 1, "define");
    val_t target = vcar(args);
    val_t rest   = vcdr(args);

    val_t name;
    val_t value;

    if (vis_symbol(target)) {
        /* (define x expr) */
        name  = target;
        value = vis_pair(rest) ? vcar(rest) : V_VOID;
        compile(c, value, false, line);
    } else if (vis_pair(target)) {
        /* (define (f params...) body...) → lambda sugar */
        name = vcar(target);
        val_t params = vcdr(target);
        compile_lambda(c, params, rest, as_sym(name)->data, line);
    } else {
        fprintf(stderr, "compiler: bad define form\n");
        emit_const(c, V_VOID, line);
        return;
    }

    emit_define_store(c, name, line);
}

/* (define-values (var...) expr) — R7RS. Unlike `receive` (compile_receive,
 * below), this can't desugar to a call-with-values + consumer lambda: its
 * targets are bindings in the ENCLOSING scope, and a consumer lambda's
 * formals are a different, inner scope with no way to reach back out and
 * populate the outer scope's locals/globals via ordinary recompiled
 * Scheme source. Direct codegen instead: compile expr once, then peel
 * off one component at a time with OP_VALUES_REF (the read-side
 * counterpart to OP_VALUES; see its own comment in opcode.h) and store
 * each with the same global/local dispatch an ordinary define uses.
 *
 * Scope for this landing: `vars` must be a proper list of symbols.
 * Neither a bare symbol (`(define-values all expr)`, meant to collect
 * every value into a list) nor a dotted/rest tail
 * (`(define-values (a . rest) expr)`) is supported -- raises a clear
 * compile-time error rather than silently defining nothing. This is not
 * a regression: eval.c's own S_DEFINE_VALUES case guards its binding
 * loop with vis_pair(vars), so a bare-symbol `vars` already silently
 * defines nothing today in the tree-walker either -- this just replaces
 * silent no-op with an honest diagnostic. Full rest-arg support, if ever
 * needed, would reuse compile_params' existing formals parsing (already
 * shared by compile_receive) as its template. */
static void compile_define_values(Compiler *c, val_t args, int line) {
    /* vcar/vcdr are unchecked casts (object.h) -- (define-values) with no
     * formals/expr at all makes `args` V_NIL, and vcar(V_NIL) dereferences
     * near-address 0xB, segfaulting the whole process rather than raising
     * a catchable compile error (independent code review, confirmed via
     * repro: `curry -e '(define-values)'` crashed before this guard). */
    if (!vis_pair(args))
        scm_raise(V_FALSE, "define-values: missing formals and expression");
    val_t vars = vcar(args);
    val_t expr = vis_pair(vcdr(args)) ? vcar(vcdr(args)) : V_VOID;

    int count = 0;
    for (val_t p = vars; vis_pair(p); p = vcdr(p)) {
        if (!vis_symbol(vcar(p)))
            scm_raise(V_FALSE, "define-values: expected a symbol in formals");
        /* OP_VALUES_REF's operand is a uint8_t component index (0-255,
         * matching every other single-byte opcode operand in this VM).
         * Past 256 formals, (uint8_t)i below would silently wrap --
         * v256 would overwrite v0's value, v257 v1's, etc., with no
         * error, just wrong bindings (independent security review).
         * Raise a clear compiler-limit error instead, mirroring
         * chunk_add_const's identical 256-constant bound. */
        if (count >= 256)
            scm_raise(V_FALSE,
                "define-values: too many formals (compiler limit: max 256)");
        count++;
    }
    if (!vis_nil(vars) && count == 0)
        scm_raise(V_FALSE,
            "define-values: rest/bare-symbol formals not yet supported");
    {
        val_t p = vars;
        for (int i = 0; i < count; i++) p = vcdr(p);
        if (!vis_nil(p))
            scm_raise(V_FALSE,
                "define-values: rest/bare-symbol formals not yet supported");
    }

    compile(c, expr, false, line);

    if (count == 0) {
        emit(c, OP_POP, line);   /* evaluated for effect only, R7RS */
    } else {
        val_t p = vars;
        for (int i = 0; i < count; i++) {
            val_t name = vcar(p);
            bool  last = (i == count - 1);
            if (!last) emit(c, OP_DUP, line);
            emit_ab(c, OP_VALUES_REF, (uint8_t)i, line);
            emit_define_store_novoid(c, name, line);
            p = vcdr(p);
        }
    }
    emit(c, OP_VOID, line);   /* define-values returns void, like define */
}

/* (defined? sym) — R7RS-adjacent extension (see eval.c's own S_DEFINED_P
 * case). Special form so `sym` is never evaluated. Resolution is split
 * between compile time and runtime:
 *
 *   - A local or upvalue reference (resolve_local/resolve_upvalue) is
 *     lexically ALWAYS bound once compiled -- emit #t directly, no
 *     runtime check.
 *   - Otherwise, a genuine runtime check against TARGET_ENV/GLOBAL_ENV
 *     (OP_DEFINED_GLOBAL, opcode.h/vm.c), since a global can be defined
 *     or not across separately-compiled top-level forms.
 *
 * Documented divergence from eval.c's dynamic env_lookup_or_false check:
 * the tree-walker's defined? on a forward-referenced internal define
 * (letrec* semantics) reads #f before that define's own form has
 * executed, since env_define only happens at that point in sequential
 * execution. Compiled locals don't have an equivalent "not yet run"
 * runtime state to check -- once compile_lambda's prescan reserves a
 * local's slot, it's lexically real for the rest of that scope, same as
 * every other forward-declared internal define this compiler already
 * treats this way (e.g. mutual recursion). defined? on such a name
 * therefore reads #t as soon as it's in lexical scope, not only after
 * its own definition has run. This matches the ordinary/expected use of
 * defined? (checking whether some top-level/global binding exists) and
 * diverges only on the edge case of asking whether a not-yet-reached
 * internal define has run yet -- not tracked, since the VM has no
 * per-slot "assigned yet" bit today. */
static void compile_defined_p(Compiler *c, val_t args, int line) {
    if (!vis_pair(args) || !vis_symbol(vcar(args)))
        scm_raise(V_FALSE, "defined?: expected a symbol");
    val_t sym = vcar(args);
    /* is_upvalue_reachable, not resolve_upvalue: a boolean query has no
     * business registering a real (otherwise unused) upvalue capture as
     * a side effect -- see that function's own comment. */
    if (resolve_local(c, sym) >= 0 || is_upvalue_reachable(c, sym)) {
        emit(c, OP_TRUE, line);
        return;
    }
    int ci = chunk_add_const(c->chunk, sym);
    emit_ab(c, OP_DEFINED_GLOBAL, (uint8_t)ci, line);
}

/* (define-record-type name ctor-form pred field-spec...) — R7RS, or
 * (define-record-type name (fields ...) ...) — R6RS. record_type_build_spec
 * (shared with eval.c's tree-walker case, still needed for library bodies)
 * builds the RTD and every (name params body) triple that needs binding;
 * compile each as an ordinary define so the usual local/global/upvalue
 * machinery in compile_define applies — including the letrec* semantics
 * from this function's pre-declared locals when used inside a lambda body.
 *
 * Passes a gensym'd local/global name as record_type_build_spec's rtd_ref,
 * rather than V_FALSE (which would embed the built RTD as a quoted
 * constant independently in each of the ctor/pred/accessor/mutator
 * closures — safe in memory, but each gets serialized to a .scc file as an
 * independent copy and reconstructed into a non-eq? object on load,
 * breaking %record-pred?'s pointer-equality check; found by testing: a
 * cache-hit run of a script using define-record-type failed its own
 * predicate). Emitting (define <gensym> (%make-record-type 'name
 * 'field-names)) once, ahead of the bindings, and having every binding
 * reference that single shared variable instead keeps identity correct
 * within one execution (fresh or replayed from cache) without embedding
 * the RTD as a constant at all.
 *
 * The gensym'd variable itself is NOT covered by compile_lambda's prescan
 * (which only reserves slots for the record's real, user-visible bindings)
 * — so, when local, its own local slot is reserved right here, immediately
 * before compiling its value, rather than relying on compile_define's
 * "New local" fallback (add a slot, store into it, immediately followed by
 * emit_define_store's trailing OP_VOID). That fallback silently corrupts
 * the slot when nothing reserved it ahead of time: OP_STORE_LOCAL writes
 * to a frame-relative position that, at that exact moment, coincides with
 * the current stack top, and the very next instruction — the trailing
 * OP_VOID — pushes onto that same now-freed position, overwriting the
 * value that was just stored (the same bug found and fixed for `symbolic`,
 * confirmed by re-deriving OP_STORE_LOCAL's frame-relative semantics in
 * vm.c). Reserving first (add_local + a placeholder OP_VOID, mirroring
 * what a prescan reservation does) separates the reserved slot from the
 * position the trailing OP_VOID lands at, avoiding the collision — done
 * inline here rather than by extending the prescan, since the gensym only
 * needs to be unique within this one function call, not coordinated across
 * two separate passes over the body. */
static void compile_define_record_type(Compiler *c, val_t rest, int line) {
    static int gensym_counter = 0;
    char buf[32];
    snprintf(buf, sizeof(buf), "%%%%rtd%d", gensym_counter++);
    val_t rtd_ref = sym_intern_cstr(buf);

    RecordTypeSpec spec;
    record_type_build_spec(rest, rtd_ref, &spec);

    RecordType *rtd = as_rtd(spec.rtd_val);
    val_t field_names = V_NIL;
    for (int i = (int)rtd->nfields - 1; i >= 0; i--)
        field_names = scm_cons(rtd->field_names[i], field_names);
    val_t make_rtd_call = scm_cons(sym_intern_cstr("%make-record-type"),
        scm_cons(scm_cons(S_QUOTE, scm_cons(rtd->name, V_NIL)),
         scm_cons(scm_cons(S_QUOTE, scm_cons(field_names, V_NIL)), V_NIL)));

    if (c->scope_depth > 0) {
        add_local(c, rtd_ref);
        c->locals[c->local_count - 1].depth = -1; /* uninitialised */
        emit(c, OP_VOID, line); /* reserve stack slot */
    }
    compile_define(c, scm_cons(rtd_ref, scm_cons(make_rtd_call, V_NIL)), line);

    for (int i = 0; i < spec.count; i++) {
        emit(c, OP_POP, line); /* discard the previous binding's OP_VOID */
        val_t lam = scm_cons(S_LAMBDA,
                     scm_cons(spec.bindings[i].params, spec.bindings[i].body));
        val_t def_args = scm_cons(spec.bindings[i].name, scm_cons(lam, V_NIL));
        compile_define(c, def_args, line);

        /* Stash this binding's freshly-defined closure back onto the
         * RUNTIME RTD (referenced via rtd_ref, not the compile-time-only
         * `rtd` above -- that one only exists to extract name/nfields/
         * field_names for the %make-record-type call emitted earlier and
         * is never itself the object %make-record-type builds at
         * runtime) so record-type-constructor/-predicate/-accessors/
         * -mutators can retrieve it later (SRFI-279's rtd-properties). */
        val_t stash_call;
        switch (spec.bindings[i].role) {
            case RTD_ROLE_CONSTRUCTOR:
                stash_call = scm_cons(sym_intern_cstr("%rtd-set-constructor!"),
                    scm_cons(rtd_ref, scm_cons(spec.bindings[i].name, V_NIL)));
                break;
            case RTD_ROLE_PREDICATE:
                stash_call = scm_cons(sym_intern_cstr("%rtd-set-predicate!"),
                    scm_cons(rtd_ref, scm_cons(spec.bindings[i].name, V_NIL)));
                break;
            case RTD_ROLE_ACCESSOR:
                stash_call = scm_cons(sym_intern_cstr("%rtd-set-accessor!"),
                    scm_cons(rtd_ref, scm_cons(vfix((intptr_t)spec.bindings[i].field_index),
                        scm_cons(spec.bindings[i].name, V_NIL))));
                break;
            case RTD_ROLE_MUTATOR:
                stash_call = scm_cons(sym_intern_cstr("%rtd-set-mutator!"),
                    scm_cons(rtd_ref, scm_cons(vfix((intptr_t)spec.bindings[i].field_index),
                        scm_cons(spec.bindings[i].name, V_NIL))));
                break;
            default:
                stash_call = V_VOID;
                break;
        }
        emit(c, OP_POP, line); /* discard this binding's own OP_VOID */
        compile(c, stash_call, false, line);
    }
}

/* (symbolic x y z ...) — bind each name as a fresh symbolic unknown.
 * Unlike define-syntax's transformer, sym-var produces an ordinary RUNTIME
 * value (a SymVar), not a compile-time macro, so this needs no
 * compile_time_eval — it's a plain (sym-var 'name) call per name, using
 * emit_define_store for the same local/global store logic compile_define
 * uses, exactly like define-record-type's bindings.
 *
 * The call to sym-var is emitted by hand — OP_LOAD_GLOBAL for the symbol
 * `sym-var`, bypassing the local/upvalue checks emit_load would otherwise
 * do — rather than compiling an ordinary AST call form referencing the
 * symbol `sym-var`. This isn't optional: a local variable literally named
 * sym-var would otherwise shadow the primitive and silently change what
 * `symbolic` does (found by review — `(let ((sym-var ...)) (symbolic a)
 * a)` returned the shadowing lambda's result instead of a SymVar). The
 * tree-walker's S_SYMBOLIC case never had this hazard: it calls
 * sx_make_var(name) directly in C, never through a Scheme-level binding.
 *
 * An earlier version of this fix instead embedded the ALREADY-RESOLVED
 * sym-var Primitive value directly as a bytecode constant (compile-time
 * env_lookup_or_false, then quote the result). That's wrong for a
 * different reason: a Primitive closes over a C function pointer, which
 * cannot be serialized into a .scc file — confirmed by testing, it
 * segfaults even on a single fresh (non-cached) run, since the
 * script-execution loop unconditionally writes a .scc cache after
 * compiling each top-level form. OP_LOAD_GLOBAL only embeds the SYMBOL
 * `sym-var` (always plain, serializable data) and looks it up in
 * GLOBAL_ENV fresh at runtime — hygienic (bypasses local shadowing) without
 * embedding a non-serializable object, and it still calls sym-var fresh
 * each time this code executes (not caching/sharing sym-var's RESULT,
 * which would incorrectly share one SymVar across repeated calls to an
 * enclosing function — a different, worse bug avoided by construction). */
static void compile_symbolic(Compiler *c, val_t rest, int line) {
    val_t sym_var_sym = sym_intern_cstr("sym-var");
    bool first = true;
    for (val_t p = rest; vis_pair(p); p = vcdr(p)) {
        val_t name = vcar(p);
        if (!vis_symbol(name)) {
            fprintf(stderr, "compiler: symbolic: expected symbol, got non-symbol\n");
            continue;
        }
        if (!first) emit(c, OP_POP, line); /* discard previous intermediate OP_VOID */
        emit_ab(c, OP_LOAD_GLOBAL, (uint8_t)chunk_add_const(c->chunk, sym_var_sym), line);
        emit_const(c, name, line);
        emit_ab(c, OP_CALL, 1, line);
        emit_define_store(c, name, line);
        first = false;
    }
    if (first) emit(c, OP_VOID, line); /* (symbolic) with no names */
}

/* Compile expr as an independent, parent-less top-level unit and run it
 * immediately — used to evaluate macro-transformer expressions
 * (define-syntax/let-syntax/letrec-syntax) at compile time.  Having no
 * parent Compiler means expr cannot resolve an enclosing lambda's locals or
 * upvalues as compiler-tracked variables; it can only see GLOBAL_ENV,
 * exactly like top-level define-syntax always could.  That's not a new
 * restriction: a transformer-constructing expression referencing a
 * not-yet-computed runtime-only local is meaningless anyway, since macro
 * expansion happens at compile time, before the enclosing function has ever
 * run.  compiler_compile()/vm_run() are already invoked reentrantly
 * elsewhere (e.g. apply() during use-site macro expansion below can itself
 * trigger vm_run for a compiled transformer), so nesting them here follows
 * an established pattern rather than introducing a new one — but that
 * pattern requires pushing the closure as the callee before vm_run (see
 * vm_eval's own comment in vm.c): pop_frame's return path assumes a pushed
 * callee+args below the frame, so a reentrant vm_run entered without one
 * (e.g. this call firing while the debugger's `,debug`/`p` command has
 * vm_run paused mid-frame) would corrupt the suspended frame's stack by one
 * slot. Top-level (non-reentrant) callers wouldn't have noticed, since
 * frame_count == 0 takes pop_frame's full-reset path instead.
 *
 * Exception-safe: compiler_compile() brackets its work in
 * gc_inhibit_minor()/gc_resume_minor(), so a transformer expression that
 * raises during compilation or evaluation (a bad syntax-rules form, an
 * unbound reference, etc.) would otherwise longjmp past the matching
 * gc_resume_minor() with nothing to rebalance it — permanently leaking
 * gc_inhibit_count and disabling minor-GC safepoints for the rest of the
 * process.  SCM_PROTECT snapshots and restores it (along with the shadow
 * stack and JIT call depth) regardless of how many nested inhibit/resume
 * calls happened inside, then this re-raises the same exception so normal
 * error reporting (REPL, guard, etc.) is unaffected. */
static val_t compile_time_eval(val_t expr) {
    ExnHandler h;
    val_t result = V_VOID;
    bool  raised = false;
    SCM_PROTECT(h, {
        val_t cl_val  = compiler_compile(expr);
        BcClosure *cl = as_bcclosure(cl_val);
        vm_push(cl_val);
        result = vm_run(cl, 0);
    }, {
        raised = true;
    });
    if (raised) scm_raise_val(h.exn);
    return result;
}

/* (define-syntax name transformer-expr). At top level (scope_depth == 0)
 * this registers into GLOBAL_ENV immediately — eagerly, at compile time,
 * rather than ONLY deferring to a runtime tree-eval call — so a macro
 * defined and used within the very same compiled unit (e.g. the same
 * top-level begin block or script) is visible to its own later forms,
 * which the old tree-eval-punt behavior could not do (it only ever took
 * effect once the *next separately compiled* top-level form ran).
 *
 * The eager registration alone doesn't survive a .scc cache-hit replay,
 * though: on a cache hit the compiler never runs again, so if that were
 * the ONLY effect, the macro would silently vanish from GLOBAL_ENV on
 * every run except the one that originally produced the cache (e.g.
 * breaking `-i` dropping into a REPL after a cached script run). So this
 * ALSO emits runtime bytecode that re-registers the same macro when the
 * bytecode executes (fresh or replayed from cache) — deliberately NOT by
 * embedding the compile-time-evaluated `transformer` procedure value
 * directly as a constant: a syntax-rules transformer is a Primitive
 * closing over a C function pointer, which cannot be serialized into a
 * .scc file (confirmed by testing: doing so segfaults on the next
 * process's cache-hit load).
 *
 * Two cases, both wired through emitting (tree-eval '(define-syntax name
 * runtime-xfm-expr)) — i.e. always going through define-syntax's own
 * single Syntax-wrapping step (in the tree-walker's S_DEFINE_SYNTAX case),
 * never building or embedding a Syntax struct here directly. An earlier
 * version of this code called %rebuild-syntax-rules via a plain (define
 * ...) and wrapped its result in a second, redundant Syntax struct; since
 * %rebuild-syntax-rules is an ordinary, discoverable global primitive, a
 * user writing (define-syntax bogus (%rebuild-syntax-rules ...)) directly
 * produced a Syntax-wrapping-a-Syntax value nothing else expected,
 * corrupting VM state when used (found by review). Routing everything
 * through one (define-syntax ...) form, and having %rebuild-syntax-rules
 * return a bare transformer (the same shape (syntax-rules ...) itself
 * evaluates to — see sr_rebuild_syntax's doc comment), fixes that: direct
 * misuse now just produces an ordinary, correctly-single-wrapped macro (or
 * a normal Scheme error, if the literals/rules/ellipsis are malformed).
 *
 *   - transformer-expr produced an ordinary syntax-rules transformer (by
 *     far the common case): sr_transformer_data extracts its underlying
 *     literals/rules/ellipsis — always plain, serializable pattern/
 *     template data, never evaluated Scheme code — and runtime-xfm-expr
 *     becomes (%rebuild-syntax-rules 'literals 'rules 'ellipsis). This
 *     never re-runs transformer-expr itself, so a transformer-expr with
 *     side-effecting code around the (syntax-rules ...) form (e.g.
 *     `(begin (side-effect!) (syntax-rules ...))`) only actually executes
 *     once, at compile time.
 *   - Anything else (a procedural transformer): there's no way to
 *     decompose an arbitrary closure into serializable pure data, so
 *     runtime-xfm-expr is just the ORIGINAL transformer-expr, re-evaluated
 *     at runtime — exactly what the pre-existing tree-eval punt already
 *     did. This DOES mean transformer-expr's side effects (if any) run
 *     twice on a fresh run: once via compile_time_eval, once via the
 *     runtime tree-eval call. Accepted for this rare case: define-syntax
 *     is a one-shot, load-time form, never a hot path, and a procedural
 *     transformer genuinely cannot survive a .scc cache reload any other
 *     way.
 *
 * Inside a lambda/let-syntax body, the macro is scoped to this
 * compilation's syntax_locals instead of leaking into GLOBAL_ENV (fixing
 * the same local-scope leak that define-record-type had), and needs no
 * runtime reconstruction at all: an internal macro is fully consumed by
 * the compiler expanding its use sites within the same lambda body, which
 * are already baked into that body's bytecode by the time compilation
 * ends — nothing outside that (lexically-scoped, one-shot) compilation
 * could ever need it to exist again later. */
static void compile_define_syntax(Compiler *c, val_t args, int line) {
    require_min_args(args, 2, "define-syntax");
    val_t name        = vcar(args);
    val_t xfm_expr    = vcar(vcdr(args));
    /* Make `name` itself visible to sr_is_protected (syntax_rules.c)
     * while its OWN (syntax-rules ...) is being compiled, so a self-
     * recursive locally-scoped macro's own name in its template doesn't
     * get incorrectly renamed (it lives only in this Compiler's
     * syntax_locals, never in any runtime env sr_current_env's def_env
     * could see) -- harmless to also do this for a top-level
     * (scope_depth == 0) define-syntax, where GLOBAL_ENV already covers
     * it via add_syntax_local below being unreachable for that branch. */
    val_t saved_locals = sr_get_current_local_macros();
    sr_set_current_local_macros(scm_cons(name, saved_locals));
    val_t transformer = V_VOID;
    ExnHandler ds_h;
    SCM_PROTECT(ds_h,
        transformer = compile_time_eval(xfm_expr),
        { sr_set_current_local_macros(saved_locals); scm_raise_val(ds_h.exn); });
    sr_set_current_local_macros(saved_locals);

    if (c->scope_depth == 0) {
        Syntax *syn = CURRY_NEW(Syntax);
        syn->hdr.type = T_SYNTAX; syn->hdr.flags = 0;
        syn->transformer = transformer;
        env_define(GLOBAL_ENV, name, vptr(syn));

        val_t literals, rules, ellipsis;
        val_t runtime_xfm_expr;
        if (sr_transformer_data(transformer, &literals, &rules, &ellipsis)) {
            /* Pass this chunk's own target_env (chunk.h) through as a 4th,
             * quoted argument so the runtime-rebuilt transformer's def_env
             * matches the compile-time one (sr_rebuild_syntax_env,
             * syntax_rules.c) instead of always defaulting to GLOBAL_ENV --
             * without this, a target_env-scoped macro's runtime transformer
             * couldn't see its own library's local helpers as "protected"
             * from hygienic renaming (see sr_is_protected). Embedding a
             * live env value here is fine for this same-process run (quote
             * just returns it verbatim, same as literals/rules/ellipsis
             * above); it shares the same not-yet-.scc-safe gap as
             * Chunk::target_env itself (both deferred together). */
            val_t rebuild_args = scm_cons(scm_cons(S_QUOTE, scm_cons(literals, V_NIL)),
                 scm_cons(scm_cons(S_QUOTE, scm_cons(rules, V_NIL)),
                  scm_cons(scm_cons(S_QUOTE, scm_cons(ellipsis, V_NIL)), V_NIL)));
            if (c->chunk->target_env != V_VOID)
                rebuild_args = scm_append(rebuild_args,
                    scm_cons(scm_cons(S_QUOTE, scm_cons(c->chunk->target_env, V_NIL)), V_NIL));
            runtime_xfm_expr = scm_cons(sym_intern_cstr("%rebuild-syntax-rules"), rebuild_args);
        } else {
            runtime_xfm_expr = xfm_expr;
        }

        val_t whole_form    = scm_cons(S_DEFINE_SYNTAX,
                                scm_cons(name, scm_cons(runtime_xfm_expr, V_NIL)));
        val_t tree_eval_sym = sym_intern_cstr("tree-eval");
        emit_ab(c, OP_LOAD_GLOBAL,
                (uint8_t)chunk_add_const(c->chunk, tree_eval_sym), line);
        emit_const(c, whole_form, line);
        emit_ab(c, OP_CALL, 1, line);
    } else {
        add_syntax_local(c, name, transformer);
        emit(c, OP_VOID, line); /* define-syntax returns void */
    }
}

/* (let-syntax ((name xfm-expr)...) body...)
 * (letrec-syntax ((name xfm-expr)...) body...)
 * Uses the SAME Compiler `c` (no nested lambda/closure, unlike let/letrec):
 * macros carry no runtime stack footprint, so there is no slot-layout
 * hazard to isolate — begin_scope/end_scope's local-macro trimming (see
 * end_scope) is all the isolation this needs, exactly as it already is for
 * a plain nested block. R7RS distinguishes let-syntax (transformer
 * expressions see only the OUTER scope) from letrec-syntax (transformer
 * expressions see each other too, for mutually-recursive macros); in
 * practice that distinction is moot here, since compile_time_eval compiles
 * each transformer expression as an independent parent-less unit that
 * cannot observe this Compiler's syntax_locals either way — a transformer
 * that itself needs to PROCEDURALLY INVOKE a sibling local macro during
 * its own construction is an exotic case this implementation doesn't
 * support, no differently for let-syntax vs. letrec-syntax.
 *
 * A narrower, much more common case IS supported, though: a sibling
 * macro's own NAME appearing in another sibling's (or its own) template,
 * for sr_is_protected's (syntax_rules.c) hygiene decision at LATER
 * macro-use time -- e.g. two mutually-recursive local macros, or one
 * self-recursive local macro. All these binding names are pushed onto
 * sr_current_local_macros (syntax_rules.c) up front, before any of
 * their transformer expressions are compiled, treating let-syntax and
 * letrec-syntax identically (matching this function's own existing
 * behavior elsewhere) — this is a static namelist for a rename
 * decision, not construction-time visibility, so it doesn't need the
 * unsupported procedural-invocation case above. */
static void compile_let_syntax(Compiler *c, val_t args, bool tail, int line) {
    require_min_args(args, 1, "let-syntax");  /* shared with letrec-syntax */
    val_t bindings = vcar(args);
    val_t body     = vcdr(args);

    begin_scope(c);

    val_t saved_locals = sr_get_current_local_macros();
    val_t names = saved_locals;
    for (val_t b = bindings; vis_pair(b); b = vcdr(b))
        names = scm_cons(vcar(vcar(b)), names);
    sr_set_current_local_macros(names);

    val_t b = bindings;
    ExnHandler ls_h;
    SCM_PROTECT(ls_h, {
        while (vis_pair(b)) {
            val_t bind = vcar(b);
            val_t name = vcar(bind);
            val_t xfm  = compile_time_eval(vcar(vcdr(bind)));
            add_syntax_local(c, name, xfm);
            b = vcdr(b);
        }
    }, { sr_set_current_local_macros(saved_locals); scm_raise_val(ls_h.exn); });
    sr_set_current_local_macros(saved_locals);

    compile_seq(c, body, tail, line);
    end_scope(c, line);
}

static void compile_set(Compiler *c, val_t args, int line) {
    require_min_args(args, 2, "set!");
    val_t name = vcar(args);
    val_t expr = vcar(vcdr(args));
    compile(c, expr, false, line);
    emit_store(c, name, line);
    emit(c, OP_VOID, line);
}

static void compile_begin(Compiler *c, val_t body, bool tail, int line) {
    if (vis_nil(body)) { emit(c, OP_VOID, line); return; }
    compile_seq(c, body, tail, line);
}

static void compile_and(Compiler *c, val_t args, bool tail, int line) {
    if (vis_nil(args)) { emit(c, OP_TRUE, line); return; }

    int patches[MAX_LOCALS]; int np = 0;
    while (vis_pair(args)) {
        val_t next = vcdr(args);
        bool last  = vis_nil(next);
        compile(c, vcar(args), last && tail, line);
        if (!last) {
            patches[np++] = emit_jump(c, OP_JUMP_FALSE, line);
        }
        args = next;
    }
    /* All true: fall through with the last value on stack */
    int end = emit_jump(c, OP_JUMP, line);
    /* Patch all short-circuit points to emit #f */
    int false_pos = chunk_pos(c->chunk);
    emit(c, OP_FALSE, line);
    patch_jump(c, end);
    /* Patch all OP_JUMP_FALSE to false_pos */
    for (int i = 0; i < np; i++)
        chunk_patch16(c->chunk, patches[i], (uint16_t)false_pos);
}

static void compile_or(Compiler *c, val_t args, bool tail, int line) {
    (void)tail;
    if (vis_nil(args)) { emit(c, OP_FALSE, line); return; }

    int patches[MAX_LOCALS]; int np = 0;
    while (vis_pair(args)) {
        val_t next = vcdr(args);
        bool last  = vis_nil(next);
        compile(c, vcar(args), false, line);
        if (!last) {
            emit(c, OP_DUP, line);
            patches[np++] = emit_jump(c, OP_JUMP_TRUE, line);
            emit(c, OP_POP, line);
        }
        args = next;
    }
    /* Fall through: last value is the result */
    int end = chunk_pos(c->chunk);
    (void)end;
    for (int i = 0; i < np; i++)
        patch_jump(c, patches[i]);
}

/* Conservative textual scan: does `expr` (or anything nested inside it,
 * except inside a (quote ...) subform) contain (set! name ...) anywhere,
 * OR any macro use at all (local syntax-local, or a global/target_env
 * T_SYNTAX binding)? Deliberately ignores lexical shadowing for the
 * set!-target check -- a false positive there (an inner binding that
 * happens to share `name` but isn't really the same variable) only costs
 * a missed optimization. The macro-use check is a SEPARATE, unconditional
 * bail-out (found by code review, confirmed via repro): a plain textual
 * scan of the RAW body can only ever see set! forms written literally in
 * source -- it is blind to a macro that itself EXPANDS to (set! name
 * ...), since expansion happens later, during compile(). A named-let
 * loop whose body used a macro that expanded to `(set! loop other-fn)`
 * previously still got OP_SELF_TAIL_CALL (the scan found no literal
 * set!), so the compiler kept trusting "this call always means myself"
 * even after the loop variable was reassigned to something else --
 * confirmed: the loop silently kept calling the stale closure instead of
 * the reassigned one. Rather than expanding every macro here just to
 * scan the result (duplicating compile()'s own expansion machinery), any
 * macro use anywhere in the body unconditionally disables the
 * optimization -- correct by construction (no expansion result can ever
 * need scanning if the whole body already bails out), at the cost of
 * also declining the optimization for named-let loops that happen to use
 * some unrelated macro that doesn't touch the loop variable at all. That
 * tradeoff is deliberate: only a hard NO is guaranteed to never regress
 * to a wrong answer as new macros get written.
 *
 * Mirrors sr_collect_free_symbols's own "don't descend into quote"
 * pattern (syntax_rules.c), and compile()'s own is-macro check (checked
 * local-syntax first, then target_env, then GLOBAL_ENV) for the macro
 * detection. lang_translate on the head catches an Akkadian-spelled
 * set! too, matching how compile()'s own dispatch translates every head
 * before comparing against S_SET. */
static bool expr_mentions_set_target(Compiler *c, val_t expr, val_t name) {
    if (!vis_pair(expr)) return false;
    val_t raw_head = vcar(expr);
    val_t head = lang_translate(raw_head);
    if (head == S_QUOTE) return false;
    if (head == S_SET && vis_pair(vcdr(expr)) && vcar(vcdr(expr)) == name)
        return true;
    if (vis_symbol(raw_head)) {
        val_t transformer;
        bool is_macro = resolve_syntax_local(c, raw_head, &transformer);
        if (!is_macro && c->chunk->target_env != V_VOID)
            is_macro = vis_syntax(env_lookup_or_false(c->chunk->target_env, raw_head));
        if (!is_macro)
            is_macro = vis_syntax(env_lookup_or_false(GLOBAL_ENV, raw_head));
        if (is_macro) return true;  /* unconditional bail-out, see comment above */
    }
    for (val_t p = expr; vis_pair(p); p = vcdr(p))
        if (expr_mentions_set_target(c, vcar(p), name)) return true;
    return false;
}

bool body_mentions_set_target(Compiler *c, val_t body, val_t name) {
    for (val_t p = body; vis_pair(p); p = vcdr(p))
        if (expr_mentions_set_target(c, vcar(p), name)) return true;
    return false;
}


static void compile_let(Compiler *c, val_t args, bool tail, int line) {
    require_min_args(args, 1, "let");
    val_t bindings = vcar(args);
    val_t body     = vcdr(args);

    /* Named let: (let loop ((x v) ...) body)
       Semantics: letrec ((loop (lambda (x ...) body))) (loop v ...)

       Compiled as a zero-arg outer wrapper to isolate the loop's frame:
         outer-wrapper:
           slot 0 = loop (void initially, then the closure)
           push void placeholder
           push OP_CLOSURE for loop (captures slot 0 as upvalue)
           OP_STORE_LOCAL 0        ; slot 0 = closure
           OP_LOAD_LOCAL 0         ; push closure as callee
           compile each init value  ; push args
           OP_TAIL_CALL N          ; jump into the loop
         parent: OP_CLOSURE outer-wrapper; OP_CALL/OP_TAIL_CALL 0 */
    if (vis_symbol(bindings)) {
        val_t loop_name = bindings;
        require_min_args(body, 1, "let");  /* named-let's own bindings list */
        bindings = vcar(body);
        body     = vcdr(body);

        int argc = 0;
        val_t b = bindings;
        while (vis_pair(b)) { argc++; b = vcdr(b); }

        /* Build forward-order params list */
        val_t params = V_NIL;
        b = bindings;
        while (vis_pair(b)) { params = scm_cons(vcar(vcar(b)), params); b = vcdr(b); }
        val_t fwd = V_NIL;
        while (vis_pair(params)) { fwd = scm_cons(vcar(params), fwd); params = vcdr(params); }

        /* Zero-arg outer wrapper */
        Compiler outer;
        init_compiler(&outer, c, as_sym(loop_name)->data);
        outer.chunk->arity = 0;

        /* Slot 0 = loop name (void placeholder) */
        add_local(&outer, loop_name);
        mark_initialised(&outer);
        emit(&outer, OP_VOID, line);

        /* Inner loop lambda; loop_name resolves as upvalue from outer's slot 0.
         * Arm the self-tail-call thread-local so a tail-position call to
         * loop_name within this exact body compiles to OP_SELF_TAIL_CALL
         * instead of the ordinary upvalue-load-then-call sequence -- see
         * Compiler::self_tail_name's comment and compile_call. The
         * set!-scan runs over the RAW (uncompiled) body once, up front,
         * so every self-tail-call site compiled anywhere in this body
         * consistently sees the same answer regardless of where in the
         * body a set! to loop_name might textually appear relative to
         * a given tail call. */
        g_compile_self_tail_name    = loop_name;
        g_compile_self_tail_mutated = body_mentions_set_target(c, body, loop_name);
        compile_lambda(&outer, fwd, body, as_sym(loop_name)->data, line);
        emit_ab(&outer, OP_STORE_LOCAL, 0, line);  /* store closure → slot 0 */
        emit_ab(&outer, OP_LOAD_LOCAL,  0, line);  /* callee */
        b = bindings;
        while (vis_pair(b)) {
            compile(&outer, vcar(vcdr(vcar(b))), false, line);
            b = vcdr(b);
        }
        emit_ab(&outer, OP_TAIL_CALL, (uint8_t)argc, line);
        Chunk *och = end_compiler(&outer);

        int ci = chunk_add_const(c->chunk, (val_t)(uintptr_t)och);
        emit_ab(c, OP_CLOSURE, (uint8_t)ci, line);
        for (int i = 0; i < outer.upval_count; i++) {
            chunk_emit(c->chunk, outer.upvals[i].is_local ? 1 : 0, line);
            chunk_emit(c->chunk, (uint8_t)outer.upvals[i].index,   line);
        }
        emit_ab(c, tail ? OP_TAIL_CALL : OP_CALL, 0, line);
        return;
    }

    /* Plain let: compile as ((lambda (x y ...) body) v_x v_y ...)
       The lambda creates a fresh frame, so slot 0 = first init arg
       regardless of what else is on the caller's stack. */
    {
        /* Build params list in forward order */
        val_t params = V_NIL;
        int argc = 0;
        val_t b = bindings;
        while (vis_pair(b)) { params = scm_cons(vcar(vcar(b)), params); argc++; b = vcdr(b); }
        val_t fwd = V_NIL;
        while (vis_pair(params)) { fwd = scm_cons(vcar(params), fwd); params = vcdr(params); }

        /* Closure pushed first (callee), then init values.  Named with the
           enclosing chunk's name: a let frame is still lexically "in" it,
           which is what a backtrace should say. */
        compile_lambda(c, fwd, body, c->name, line);
        b = bindings;
        while (vis_pair(b)) {
            compile(c, vcar(vcdr(vcar(b))), false, line);
            b = vcdr(b);
        }
        emit_ab(c, tail ? OP_TAIL_CALL : OP_CALL, (uint8_t)argc, line);
    }
}

static void compile_let_star(Compiler *c, val_t args, bool tail, int line) {
    require_min_args(args, 1, "let*");
    val_t bindings = vcar(args);
    val_t body     = vcdr(args);

    /* Compile as nested single-binding lambdas:
       (let* ((x v) rest...) body) → ((lambda (x) (let* rest... body)) v)
       This avoids slot-index mismatches when let* appears as a call argument. */
    if (vis_nil(bindings)) {
        compile_seq(c, body, tail, line);
        return;
    }
    val_t binding  = vcar(bindings);
    val_t name     = vcar(binding);
    val_t init     = vcar(vcdr(binding));
    val_t rest     = vcdr(bindings);
    /* Build inner let*: (let* rest... body) as the lambda body.
       We nest it by constructing a synthetic list form. */
    val_t inner_body;
    if (vis_nil(rest)) {
        inner_body = body;
    } else {
        /* Construct (let* rest body) as a list to pass to compile_seq */
        val_t let_star_sym = sym_intern_cstr("let*");
        val_t inner_form   = scm_cons(let_star_sym,
                                      scm_cons(rest, body));
        inner_body = scm_cons(inner_form, V_NIL);
    }
    val_t params = scm_cons(name, V_NIL);
    compile_lambda(c, params, inner_body, c->name, line);
    compile(c, init, false, line);
    emit_ab(c, tail ? OP_TAIL_CALL : OP_CALL, 1, line);
}

static void compile_letrec(Compiler *c, val_t args, bool tail, int line) {
    require_min_args(args, 1, "letrec");
    val_t bindings = vcar(args);
    val_t body     = vcdr(args);

    /* Wrap in a zero-arg lambda to get a clean frame, so slot 0 = first
       letrec binding regardless of what is already on the caller's stack. */
    Compiler lc;
    init_compiler(&lc, c, "<letrec>");
    lc.chunk->arity = 0;

    begin_scope(&lc);

    /* Count bindings */
    int nb = 0;
    val_t bcount = bindings;
    while (vis_pair(bcount)) { nb++; bcount = vcdr(bcount); }

    /* Pre-declare all locals with void placeholders */
    val_t b = bindings;
    while (vis_pair(b)) {
        val_t name = vcar(vcar(b));
        add_local(&lc, name);
        mark_initialised(&lc);
        emit(&lc, OP_VOID, line);
        b = vcdr(b);
    }

    int base_slot = lc.local_count - nb;

    /* Compile and store each init (they can reference each other via upvalues) */
    b = bindings;
    int i = 0;
    while (vis_pair(b)) {
        val_t init = vcar(vcdr(vcar(b)));
        compile(&lc, init, false, line);
        emit_ab(&lc, OP_STORE_LOCAL, (uint8_t)(base_slot + i), line);
        b = vcdr(b); i++;
    }

    compile_seq(&lc, body, true, line);
    end_scope(&lc, line);

    Chunk *lch = end_compiler(&lc);

    int ci = chunk_add_const(c->chunk, (val_t)(uintptr_t)lch);
    emit_ab(c, OP_CLOSURE, (uint8_t)ci, line);
    for (int k = 0; k < lc.upval_count; k++) {
        chunk_emit(c->chunk, lc.upvals[k].is_local ? 1 : 0, line);
        chunk_emit(c->chunk, (uint8_t)lc.upvals[k].index, line);
    }
    emit_ab(c, tail ? OP_TAIL_CALL : OP_CALL, 0, line);
}

/* Walks a lambda-formals shape (proper list, dotted list, or a bare
 * rest-symbol) and returns a same-shaped structure with every identifier
 * replaced by a fresh "%%lvB_P" temp symbol (B = binding index within the
 * enclosing let-values form, P = position within this binding's formals).
 * Prepends each (original . temp) pair onto *pairs (order doesn't matter --
 * it only ever becomes an unordered `let` binding list). */
static val_t let_values_temp_formals(val_t formals, int bi, int *pi, val_t *pairs) {
    if (vis_nil(formals)) return V_NIL;
    char namebuf[40];
    if (vis_symbol(formals)) {
        snprintf(namebuf, sizeof(namebuf), "%%lv%d_%d", bi, (*pi)++);
        val_t tmp = sym_intern_cstr(namebuf);
        *pairs = scm_cons(scm_cons(formals, scm_cons(tmp, V_NIL)), *pairs);
        return tmp;
    }
    val_t orig_name = vcar(formals);
    snprintf(namebuf, sizeof(namebuf), "%%lv%d_%d", bi, (*pi)++);
    val_t tmp = sym_intern_cstr(namebuf);
    *pairs = scm_cons(scm_cons(orig_name, scm_cons(tmp, V_NIL)), *pairs);
    return scm_cons(tmp, let_values_temp_formals(vcdr(formals), bi, pi, pairs));
}

/* (let-values (((a b) e1) ((c) e2) ...) body ...)
 * Desugars to nested call-with-values, one per binding, each consumer
 * lambda using FRESH temp names rather than the source-level formal
 * names -- so a later producer expression (e.g. e2) can never observe an
 * earlier binding (e.g. a/b), matching let-values' parallel-binding
 * semantics (as opposed to let*-values' sequential visibility). The
 * outermost consumer, once every producer has run, closes with a single
 * `let` that binds the real names to their temps:
 *   (call-with-values (lambda () e1)
 *     (lambda (%%lv0_0 %%lv0_1)
 *       (call-with-values (lambda () e2)
 *         (lambda (%%lv1_0)
 *           (let ((a %%lv0_0) (b %%lv0_1) (c %%lv1_0)) body ...)))))
 *
 * Each nested call-with-values is the sole/last expression of its
 * enclosing consumer lambda's body, so it's always compiled tail=true
 * (compile_lambda already treats a lambda's own body as tail position
 * regardless of the lambda's own calling context) -- meaning the whole
 * chain, and this desugaring's use above it, gets genuine TCO via
 * OP_TAIL_CALL_WITH_VALUES (see the dispatch for S_CALL_WITH_VALUES in
 * the main compile() switch) when the overall let-values form itself
 * sits in tail position. */
static val_t let_values_expand(val_t bindings, val_t body, int bi, val_t pairs) {
    if (vis_nil(bindings)) {
        val_t let_bindings = V_NIL;
        val_t p = pairs;
        while (vis_pair(p)) { let_bindings = scm_cons(vcar(p), let_bindings); p = vcdr(p); }
        return scm_cons(S_LET, scm_cons(let_bindings, body));
    }
    require_min_args(bindings, 1, "let-values");
    val_t binding  = vcar(bindings);
    require_min_args(binding, 2, "let-values");  /* one (formals producer) pair */
    val_t formals  = vcar(binding);
    val_t producer = vcar(vcdr(binding));
    int pi = 0;
    val_t temp_formals = let_values_temp_formals(formals, bi, &pi, &pairs);
    val_t inner = let_values_expand(vcdr(bindings), body, bi + 1, pairs);
    val_t producer_lam = scm_cons(S_LAMBDA, scm_cons(V_NIL, scm_cons(producer, V_NIL)));
    val_t consumer_lam = scm_cons(S_LAMBDA, scm_cons(temp_formals, scm_cons(inner, V_NIL)));
    val_t cwv = sym_intern_cstr("call-with-values");
    return scm_cons(cwv, scm_cons(producer_lam, scm_cons(consumer_lam, V_NIL)));
}

static void compile_let_values(Compiler *c, val_t args, bool tail, int line) {
    require_min_args(args, 1, "let-values");
    val_t bindings = vcar(args);
    val_t body     = vcdr(args);
    if (vis_nil(bindings)) { compile_seq(c, body, tail, line); return; }
    compile(c, let_values_expand(bindings, body, 0, V_NIL), tail, line);
}

/* (let*-values (((a b) e1) rest...) body ...)
 * Desugars to nested call-with-values using the REAL formal names directly
 * (no temp-name indirection needed): each subsequent producer expression
 * is compiled as part of the previous binding's consumer-lambda body, so
 * it correctly sees earlier bindings -- exactly let*-values' sequential
 * semantics. Mirrors compile_let_star's existing self-embedding recursion
 * (reusing S_LET_STAR_VALUES for the inner form) rather than the
 * expand-then-compile-once style of compile_let_values above. */
static void compile_let_star_values(Compiler *c, val_t args, bool tail, int line) {
    require_min_args(args, 1, "let*-values");
    val_t bindings = vcar(args);
    val_t body     = vcdr(args);
    if (vis_nil(bindings)) { compile_seq(c, body, tail, line); return; }

    require_min_args(bindings, 1, "let*-values");
    val_t binding  = vcar(bindings);
    require_min_args(binding, 2, "let*-values");  /* one (formals producer) pair */
    val_t formals  = vcar(binding);
    val_t producer = vcar(vcdr(binding));
    val_t rest     = vcdr(bindings);

    val_t inner_body;
    if (vis_nil(rest)) {
        inner_body = body;
    } else {
        val_t inner_form = scm_cons(S_LET_STAR_VALUES, scm_cons(rest, body));
        inner_body = scm_cons(inner_form, V_NIL);
    }

    val_t producer_lam = scm_cons(S_LAMBDA, scm_cons(V_NIL, scm_cons(producer, V_NIL)));
    val_t consumer_lam = scm_cons(S_LAMBDA, scm_cons(formals, inner_body));
    val_t cwv = sym_intern_cstr("call-with-values");
    val_t expanded = scm_cons(cwv, scm_cons(producer_lam, scm_cons(consumer_lam, V_NIL)));
    compile(c, expanded, tail, line);
}

static void compile_cond(Compiler *c, val_t clauses, bool tail, int line) {
    /* (cond (test expr...) ... (else expr...)) */
    /* Every non-else clause now pushes exactly one end_patches entry
     * (the "jump past the trailing OP_VOID fallback" fix applies
     * uniformly to every clause, not just non-last ones anymore -- see
     * this function's own clause-by-clause comments), so a cond with N
     * non-else clauses needs N slots here, not N-1 as before. 512 is a
     * generous ceiling (previously 64, unchecked, so a >64-clause plain
     * cond already silently overran this array before this diff --
     * confirmed by review); the explicit check below turns any case
     * that still exceeds it into a clean compile-time error instead of
     * stack corruption. */
    int end_patches[512]; int np = 0;
#define COND_PUSH_PATCH(off) do { \
        if (np >= (int)(sizeof(end_patches) / sizeof(end_patches[0]))) \
            scm_raise(V_FALSE, "cond: too many clauses (compiler limit)"); \
        end_patches[np++] = (off); \
    } while (0)

    while (vis_pair(clauses)) {
        val_t clause = vcar(clauses);
        val_t test   = vcar(clause);
        val_t exprs  = vcdr(clause);
        clauses      = vcdr(clauses);
        /* No `last`-gating left anywhere below: every non-else clause
         * (value-only, => arrow, and plain-body alike) now always jumps
         * past the trailing "no clause matched" OP_VOID fallback and
         * always compiles its body with the cond's own `tail` flag,
         * regardless of clause position -- see this loop's own
         * per-clause comments for why. */

        val_t S_ELSE = sym_intern_cstr("else");
        if (test == S_ELSE) {
            compile_seq(c, exprs, tail, line);
            goto cond_done;
        }

        compile(c, test, false, line);

        /* (cond (test) ...) — test is the value. Always DUP/jump, even
         * as the last clause -- gating this on `!last` (as this code
         * used to) left a matched last clause's single test value on
         * the stack with nothing to jump past the trailing "no clause
         * matched" OP_VOID fallback below, which then unconditionally
         * pushed a second value on top of it, corrupting the stack for
         * the caller (confirmed: `(cond (5))` raised a spurious "not a
         * procedure" error). Same bug class, and same fix, as the
         * plain-body clause below and the => arrow clause just below
         * that. */
        if (vis_nil(exprs)) {
            emit(c, OP_DUP, line);
            int skip = emit_jump(c, OP_JUMP_TRUE, line);
            emit(c, OP_POP, line);
            COND_PUSH_PATCH(skip);
            continue;
        }

        /* (cond (test => proc)) */
        val_t S_ARROW = sym_intern_cstr("=>");
        if (vis_pair(exprs) && vcar(exprs) == S_ARROW && vis_pair(vcdr(exprs))) {
            val_t proc = vcar(vcdr(exprs));
            /* DUP so test value survives the JUMP_FALSE pop */
            emit(c, OP_DUP, line);
            int skip = emit_jump(c, OP_JUMP_FALSE, line);
            /* truthy: original test is on stack; push proc and swap */
            compile(c, proc, false, line);
            emit(c, OP_SWAP, line);   /* (proc test) */
            emit_ab(c, tail ? OP_TAIL_CALL : OP_CALL, 1, line);
            /* Always jump past the trailing OP_VOID fallback, even as
             * the last clause -- same bug class as the plain-body and
             * test-only clauses above/below (confirmed: a matched,
             * textually-last `=> ` clause silently discarded its
             * result, falling through into the OP_POP meant only for
             * the #f path and then the fallback OP_VOID). */
            COND_PUSH_PATCH(emit_jump(c, OP_JUMP, line));
            patch_jump(c, skip);
            /* #f path: JUMP_FALSE already popped dup; original #f is on stack */
            emit(c, OP_POP, line);
            continue;
        }

        int skip = emit_jump(c, OP_JUMP_FALSE, line);
        /* JUMP_FALSE already popped the test — no OP_POP needed.
         * Compile with the cond's own `tail` flag, not `tail && last`:
         * exactly one clause's body ever executes per evaluation of the
         * whole cond, so EVERY clause's body is in the same tail
         * position the whole cond expression is, not just the textually
         * last one. Gating on `last` here meant a call in any non-last
         * clause's body never got OP_TAIL_CALL, silently growing the
         * stack on every iteration of a tail-recursive loop written as
         * `(let loop (...) (cond (test1 (loop ...)) (test2 (loop ...))
         * (else result)))` -- exactly the shape of a state-machine-style
         * loop with an early-exit clause, and exactly what (srfi s252
         * property-testing)'s %run-property-trials does (its recursive
         * call sits in a cond's *middle* clause), which is how this was
         * found: `(test-property ... 150000)` overflowed the VM's
         * 256-frame guard despite the recursive call already being
         * outside `guard`, single-call-site, and documented as tail-
         * position. The matching `=> ` clause branch just above already
         * gets this right (uses plain `tail`, not `tail && last`) --
         * this brings the plain-body clause in line with it. */
        compile_seq(c, exprs, tail, line);
        /* Always jump past the "no clause matched" OP_VOID fallback
         * below, even for the last clause — omitting it here (as this
         * code previously did for `last`) let a matched last clause's
         * value fall straight through into that OP_VOID, silently
         * discarding it and corrupting the stack for the caller (e.g.
         * `(cond (#f 1) (#t 2))` returned void instead of 2, and in a
         * call-argument position desynced the stack enough to raise a
         * spurious "not a procedure" error). Dead code when the clause
         * body ends in a tail call (control never reaches it), harmless
         * either way. */
        COND_PUSH_PATCH(emit_jump(c, OP_JUMP, line));
        patch_jump(c, skip);
    }
    emit(c, OP_VOID, line);   /* no clause matched */

cond_done:
    for (int i = 0; i < np; i++) patch_jump(c, end_patches[i]);
}
#undef COND_PUSH_PATCH

static void compile_case(Compiler *c, val_t args, bool tail, int line) {
    /* Desugar: (case key clause...) →
     *   (let ((%%k key)) (cond (clause') ...))
     * where non-else clause ((d...) body...) → ((memv %%k '(d...)) body...)
     * and   arrow clause ((d...) => proc)    → ((memv %%k '(d...)) (proc %%k)) */
    require_min_args(args, 1, "case");
    val_t key     = vcar(args);
    val_t clauses = vcdr(args);
    val_t ksym    = sym_intern_cstr("%%case-key%%");
    val_t memv    = sym_intern_cstr("memv");

    /* Build cond clauses */
    val_t cond_head = V_NIL, *cond_tail = &cond_head;
    while (vis_pair(clauses)) {
        val_t clause = vcar(clauses);  clauses = vcdr(clauses);
        val_t datums = vcar(clause);
        val_t body   = vcdr(clause);

        val_t test;
        if (datums == S_ELSE) {
            test = S_ELSE;
        } else {
            val_t quoted = scm_cons(S_QUOTE, scm_cons(datums, V_NIL));
            test = scm_cons(memv, scm_cons(ksym, scm_cons(quoted, V_NIL)));
        }

        val_t cond_body;
        if (vis_pair(body) && vcar(body) == S_ARROW && vis_pair(vcdr(body))) {
            /* (case key ((d) => proc)) → call (proc %%k) */
            val_t proc = vcar(vcdr(body));
            cond_body = scm_cons(scm_cons(proc, scm_cons(ksym, V_NIL)), V_NIL);
        } else {
            cond_body = body;
        }

        val_t cc = scm_cons(test, cond_body);
        *cond_tail = scm_cons(cc, V_NIL);
        cond_tail  = &as_pair(*cond_tail)->cdr;
    }

    val_t cond_expr = scm_cons(S_COND, cond_head);
    val_t binding   = scm_cons(scm_cons(ksym, scm_cons(key, V_NIL)), V_NIL);
    val_t let_expr  = scm_cons(S_LET, scm_cons(binding, scm_cons(cond_expr, V_NIL)));
    compile(c, let_expr, tail, line);
}

static void compile_when(Compiler *c, val_t args, bool tail, int line) {
    require_min_args(args, 1, "when");
    val_t test = vcar(args);
    val_t body = vcdr(args);
    compile(c, test, false, line);
    int skip = emit_jump(c, OP_JUMP_FALSE, line);
    /* JUMP_FALSE already popped test; compile body for truthy path */
    compile_seq(c, body, tail, line);
    int end = emit_jump(c, OP_JUMP, line);
    patch_jump(c, skip);
    /* #f path: test already popped, push void */
    emit(c, OP_VOID, line);
    patch_jump(c, end);
}

/* (delay expr...) / (delay-force expr...): compile the body as an
 * ordinary zero-arg thunk (reusing compile_lambda -- no new closure-
 * construction codegen needed), then call %delay-promise/
 * %delay-force-promise (builtins.c) to wrap it into a lazy Promise.
 * Was previously handled only by eval.c's tree-walker (S_DELAY/
 * S_DELAY_FORCE) with no compiler equivalent at all -- (delay ...) at
 * top level or anywhere else compiled raised unbound-variable, since
 * the compiler had never heard of it. Found while checking compiler/
 * tree-walker parity before switching define-library bodies to
 * compiled execution (they'd previously only worked because that one
 * path happened to still be tree-walked). */
static void compile_delay(Compiler *c, val_t body, bool is_force, bool tail, int line) {
    val_t prim_sym = sym_intern_cstr(is_force ? "%delay-force-promise" : "%delay-promise");
    emit_ab(c, OP_LOAD_GLOBAL, (uint8_t)chunk_add_const(c->chunk, prim_sym), line);
    compile_lambda(c, V_NIL, body, NULL, line);
    emit_ab(c, tail ? OP_TAIL_CALL : OP_CALL, 1, line);
}

static void compile_unless(Compiler *c, val_t args, bool tail, int line) {
    require_min_args(args, 1, "unless");
    val_t test = vcar(args);
    val_t body = vcdr(args);
    compile(c, test, false, line);
    int skip = emit_jump(c, OP_JUMP_TRUE, line);
    /* JUMP_TRUE already popped test; compile body for #f path */
    compile_seq(c, body, tail, line);
    int end = emit_jump(c, OP_JUMP, line);
    patch_jump(c, skip);
    /* truthy path: test already popped, push void */
    emit(c, OP_VOID, line);
    patch_jump(c, end);
}

static void compile_do(Compiler *c, val_t args, bool tail, int line) {
    /* (do ((var init step) ...) (test expr...) body...)
       Wrap in a zero-arg lambda so do vars start at slot 0, avoiding
       slot-index conflicts when do appears as a call argument. */
    require_min_args(args, 2, "do");
    val_t var_specs = vcar(args);
    val_t term      = vcar(vcdr(args));
    val_t body      = vcdr(vcdr(args));
    require_min_args(term, 1, "do");  /* do's own (test expr...) clause */
    val_t test_expr = vcar(term);
    val_t result    = vcdr(term);

    Compiler lc;
    init_compiler(&lc, c, "<do>");
    lc.chunk->arity = 0;
    Compiler *d = &lc;       /* alias so the rest of the function is readable */
    begin_scope(d);

    /* Evaluate and bind init values */
    val_t vs = var_specs;
    while (vis_pair(vs)) {
        val_t spec = vcar(vs);
        val_t init = vcar(vcdr(spec));
        compile(d, init, false, line);
        add_local(d, vcar(spec));
        mark_initialised(d);
        vs = vcdr(vs);
    }

    /* Loop head */
    int loop_start = chunk_pos(d->chunk);

    /* Test — OP_JUMP_TRUE pops its condition in both branches */
    compile(d, test_expr, false, line);
    int exit_jmp = emit_jump(d, OP_JUMP_TRUE, line);

    /* Body (loop-continues path) */
    vs = body;
    while (vis_pair(vs)) {
        val_t next = vcdr(vs);
        compile(d, vcar(vs), false, line);
        emit(d, OP_POP, line);   /* discard body expression results */
        vs = next;
    }

    /* Compute all step values first (so they don't see their own update) */
    int nv = 0;
    vs = var_specs;
    while (vis_pair(vs)) { nv++; vs = vcdr(vs); }

    /* Tier 2.4 fix (found by independent code review; same bug class,
     * same fix, as SF_VALUES/SF_APPLY/SF_CALL_WITH_VALUES's own "Tier 2.4
     * fix" comments above): this loop predates reserve_pending_slot and
     * was never updated for it. A step expression that is itself a
     * let / let* / letrec / letrec* / named-let now splices real locals
     * directly into `d` -- without this bracketing, a LATER step's own
     * splice computes its new locals' slot indices against a
     * d->local_count that doesn't account for the EARLIER steps' own
     * already-pushed, still-pending values, aliasing physical stack
     * positions those earlier steps are still using.
     *
     * `base` itself is unrelated to this bracketing: it identifies the nv
     * PERSISTENT loop-variable slots (added via add_local during the init
     * phase above, at the very top of this function), which is exactly
     * `saved - nv` -- `saved` (== d->local_count right before this loop)
     * only equals `d->local_count - nv` measured AFTER the loop when
     * nothing in between changes local_count, which no longer holds once
     * a step can splice new locals of its own; capturing `saved` before
     * this loop and subtracting nv from IT is what stays correct in both
     * cases. Confirmed as a real, silent wrong-value bug: `(do ((i 0 (+ i
     * 1)) (acc 0 (+ acc (let ((x 1)) x)))) ((= i 3) acc))` returned 0
     * instead of 3 before this fix. */
    int saved = d->local_count;
    vs = var_specs;
    while (vis_pair(vs)) {
        val_t spec = vcar(vs);
        val_t step = vis_pair(vcdr(vcdr(spec))) ? vcar(vcdr(vcdr(spec))) : vcar(spec);
        compile(d, step, false, line);
        reserve_pending_slot(d);
        vs = vcdr(vs);
    }
    int base = saved - nv;
    release_pending_slots(d, saved);
    for (int i = nv - 1; i >= 0; i--)
        emit_ab(d, OP_STORE_LOCAL, (uint8_t)(base + i), line);

    /* Jump back to loop head */
    int back = emit_jump(d, OP_JUMP, line);
    chunk_patch16(d->chunk, back, (uint16_t)loop_start);

    /* Exit (condition already popped by JUMP_TRUE) */
    patch_jump(d, exit_jmp);

    if (vis_nil(result))
        emit(d, OP_VOID, line);
    else
        compile_seq(d, result, true, line);

    end_scope(d, line);

    Chunk *dch = end_compiler(d);
    int ci = chunk_add_const(c->chunk, (val_t)(uintptr_t)dch);
    emit_ab(c, OP_CLOSURE, (uint8_t)ci, line);
    for (int k = 0; k < lc.upval_count; k++) {
        chunk_emit(c->chunk, lc.upvals[k].is_local ? 1 : 0, line);
        chunk_emit(c->chunk, (uint8_t)lc.upvals[k].index, line);
    }
    emit_ab(c, tail ? OP_TAIL_CALL : OP_CALL, 0, line);
}

/* ── with-exception-handler native compilation ───────────────────────── */

static void compile_with_exception_handler(Compiler *c, val_t args,
                                           bool tail, int line) {
    /* (with-exception-handler handler thunk)
     *
     * Emitted bytecode:
     *   <handler>               ← pushed first; lives below the call
     *   OP_PUSH_HANDLER catch   ← installs setjmp; saves sp (past handler)
     *   <thunk>                 ← pushed after the save point
     *   OP_CALL 0               ← (thunk) → result; stack: [handler result]
     *   OP_POP_HANDLER          ← normal exit: remove handler
     *   OP_SWAP; OP_POP         ← discard handler, keep result
     *   OP_JUMP end
     *  catch:
     *   ; sp restored to past handler, exception pushed → [handler exn]
     *   OP_CALL 1               ← (handler exn) → result
     *  end:
     */
    require_min_args(args, 2, "with-exception-handler");
    val_t handler_expr = vcar(args);
    val_t thunk_expr   = vcar(vcdr(args));

    /* Tier 2.4 fix (same bug class, same fix, as SF_VALUES/SF_APPLY/
     * SF_CALL_WITH_VALUES/compile_do's own "Tier 2.4 fix" comments
     * elsewhere in this file): handler_expr's own fresh result is a
     * still-pending value on the physical stack while thunk_expr
     * compiles below -- if thunk_expr is itself (or evaluates via) a
     * let / let* / letrec / letrec* / named-let, its own splice must not treat
     * `c->local_count` as if it already accounted for handler's value,
     * or its own new locals alias handler's physical stack slot.
     * Confirmed as a real bug: `(with-exception-handler (lambda (e)
     * 'handled) (let ((f (lambda () 42))) f))` returned 'handled instead
     * of calling the thunk and returning 42. */
    int saved = c->local_count;

    /* Compile handler (stays below the protected call on the stack) */
    compile(c, handler_expr, false, line);
    reserve_pending_slot(c);

    /* OP_PUSH_HANDLER: saves sp at this point (past handler, before thunk) */
    int catch_placeholder = emit_jump(c, OP_PUSH_HANDLER, line);

    /* Compile thunk and call it with no arguments */
    compile(c, thunk_expr, false, line);
    reserve_pending_slot(c);
    release_pending_slots(c, saved);
    emit_ab(c, OP_CALL, 0, line);          /* (thunk) → result */

    /* Normal path: remove handler, discard it, keep result */
    emit(c, OP_POP_HANDLER, line);
    emit(c, OP_SWAP, line);                /* [result handler] */
    emit(c, OP_POP, line);                 /* [result] */
    int end_jmp = emit_jump(c, OP_JUMP, line);

    /* Catch path: exception is on top, handler is below */
    patch_jump(c, catch_placeholder);
    emit_ab(c, tail ? OP_TAIL_CALL : OP_CALL, 1, line); /* (handler exn) */

    patch_jump(c, end_jmp);
}

/* ── parameterize desugaring ─────────────────────────────────────────── */

static void compile_guard(Compiler *c, val_t args, bool tail, int line) {
    /* (guard (var clause...) body...)
     * Expands to:
     *   (call/cc (lambda (%guard-k)
     *     (with-exception-handler
     *       (lambda (var)
     *         (cond (test (%guard-k expr)) ... (else (raise var))))
     *       (lambda () body...))))
     * Each (else expr) clause wraps expr in (%guard-k ...) rather than raise. */
    require_min_args(args, 1, "guard");
    val_t var_and_clauses = vcar(args);
    val_t body            = vcdr(args);
    require_min_args(var_and_clauses, 1, "guard");  /* guard's own (var clause...) */
    val_t var             = vcar(var_and_clauses);
    val_t clauses         = vcdr(var_and_clauses);

    val_t S_WEH   = sym_intern_cstr("with-exception-handler");
    val_t S_RAISE = sym_intern_cstr("raise");
    val_t S_COND2 = sym_intern_cstr("cond");
    val_t S_ELSE2 = sym_intern_cstr("else");
    val_t gk      = sym_intern_cstr("%guard-k");

    /* Count clauses and check for else */
    bool has_else = false;
    val_t cl = clauses;
    while (vis_pair(cl)) {
        val_t test = vcar(vcar(cl));
        if (test == S_ELSE2) has_else = true;
        cl = vcdr(cl);
    }

    /* Build cond clauses in forward order (collect into array, then cons) */
    val_t clause_arr[64];
    int ci = 0;
    cl = clauses;
    while (vis_pair(cl) && ci < 64) {
        val_t clause = vcar(cl);
        val_t test   = vcar(clause);
        val_t cbody  = vcdr(clause);
        val_t rest;
        if (!vis_pair(cbody)) {
            /* Bodyless clause (test) — return test's own value without
             * re-evaluating it, via cond's => arrow form (proc receives
             * the test's value as its one argument). */
            rest = scm_cons(S_ARROW, scm_cons(gk, V_NIL));
        } else {
            /* A clause body is (expr ...), i.e. an implicit begin — wrap
             * multi-expression bodies in an explicit (begin ...) so every
             * expression actually runs, not just the first. */
            val_t body_expr = vis_pair(vcdr(cbody))
                ? scm_cons(S_BEGIN, cbody)
                : vcar(cbody);
            rest = scm_cons(scm_cons(gk, scm_cons(body_expr, V_NIL)), V_NIL);
        }
        clause_arr[ci++] = scm_cons(test == S_ELSE2 ? S_ELSE2 : test, rest);
        cl = vcdr(cl);
    }
    /* Append default (else (raise var)) if no else clause present */
    if (!has_else && ci < 64) {
        val_t raise_form = scm_cons(S_RAISE, scm_cons(var, V_NIL));
        clause_arr[ci++] = scm_cons(S_ELSE2, scm_cons(raise_form, V_NIL));
    }
    /* Build cond form from clause_arr in reverse (cons builds reversed list) */
    val_t cond_list = V_NIL;
    for (int i = ci - 1; i >= 0; i--)
        cond_list = scm_cons(clause_arr[i], cond_list);
    val_t handler_cond = scm_cons(S_COND2, cond_list);

    /* (lambda (var) cond-form) */
    val_t handler_lam = scm_cons(S_LAMBDA,
                            scm_cons(scm_cons(var, V_NIL),
                                scm_cons(handler_cond, V_NIL)));
    /* (lambda () body...) */
    val_t thunk_lam = scm_cons(S_LAMBDA, scm_cons(V_NIL, body));
    /* (with-exception-handler handler thunk) */
    val_t weh_form = scm_cons(S_WEH,
                        scm_cons(handler_lam, scm_cons(thunk_lam, V_NIL)));
    /* (lambda (%guard-k) weh-form) */
    val_t outer_lam = scm_cons(S_LAMBDA,
                         scm_cons(scm_cons(gk, V_NIL),
                             scm_cons(weh_form, V_NIL)));
    /* (call/cc outer-lam) */
    val_t expanded = scm_cons(S_CALL_CC, scm_cons(outer_lam, V_NIL));
    compile(c, expanded, tail, line);
}

static void compile_parameterize(Compiler *c, val_t args, bool tail, int line) {
    /* (parameterize ((p1 v1) (p2 v2) ...) body ...)
     * Desugar to:
     *   (let ((%%p0 p1) (%%p1 p2) ...)
     *     (let ((%%old0 (%%p0)) (%%old1 (%%p1)) ...)
     *       (dynamic-wind
     *         (lambda () (%%p0 v1) (%%p1 v2) ...)
     *         (lambda () body ...)
     *         (lambda () (%%p0 %%old0) (%%p1 %%old1) ...))))
     * so local variables in body are captured as upvalues, not looked up
     * in GLOBAL_ENV. */
    require_min_args(args, 1, "parameterize");
    val_t param_list = vcar(args);
    val_t body       = vcdr(args);

    /* Count bindings */
    int n = 0;
    { val_t b = param_list; while (vis_pair(b)) { n++; b = vcdr(b); } }

    if (n == 0) {
        compile_seq(c, body, tail, line);
        return;
    }

    /* Extract per-binding data and generate gensym names */
#define MAX_PARAMS 32
    val_t pref[MAX_PARAMS], oref[MAX_PARAMS];
    val_t param_expr[MAX_PARAMS], val_expr[MAX_PARAMS];
    char namebuf[32];
    if (n > MAX_PARAMS) n = MAX_PARAMS;

    val_t b = param_list;
    for (int i = 0; i < n; i++, b = vcdr(b)) {
        val_t binding = vcar(b);
        require_min_args(binding, 2, "parameterize");  /* one (param val) pair */
        param_expr[i] = vcar(binding);
        val_expr[i]   = vcar(vcdr(binding));
        snprintf(namebuf, sizeof(namebuf), "%%prm%d", i);
        pref[i] = sym_intern_cstr(namebuf);
        snprintf(namebuf, sizeof(namebuf), "%%old%d", i);
        oref[i] = sym_intern_cstr(namebuf);
    }
#undef MAX_PARAMS

    val_t S_DW = sym_intern_cstr("dynamic-wind");

    /* Build outer let bindings: ((%%p0 p1) ...) */
    val_t outer_bindings = V_NIL;
    for (int i = n - 1; i >= 0; i--)
        outer_bindings = scm_cons(scm_cons(pref[i],
                                  scm_cons(param_expr[i], V_NIL)),
                                  outer_bindings);

    /* Build inner let bindings: ((%%old0 (%%p0)) ...) */
    val_t inner_bindings = V_NIL;
    for (int i = n - 1; i >= 0; i--)
        inner_bindings = scm_cons(scm_cons(oref[i],
                                  scm_cons(scm_cons(pref[i], V_NIL), V_NIL)),
                                  inner_bindings);

    /* before-lambda body: ((%%p0 v1) ...) */
    val_t before_body = V_NIL;
    for (int i = n - 1; i >= 0; i--)
        before_body = scm_cons(scm_cons(pref[i], scm_cons(val_expr[i], V_NIL)),
                               before_body);
    val_t before_lam = scm_cons(S_LAMBDA, scm_cons(V_NIL, before_body));

    /* after-lambda body: ((%%p0 %%old0) ...) */
    val_t after_body = V_NIL;
    for (int i = n - 1; i >= 0; i--)
        after_body = scm_cons(scm_cons(pref[i], scm_cons(oref[i], V_NIL)),
                              after_body);
    val_t after_lam = scm_cons(S_LAMBDA, scm_cons(V_NIL, after_body));

    /* thunk-lambda: (lambda () body ...) */
    val_t thunk_lam = scm_cons(S_LAMBDA, scm_cons(V_NIL, body));

    /* (dynamic-wind before thunk after) */
    val_t dwind = scm_cons(S_DW,
                   scm_cons(before_lam,
                    scm_cons(thunk_lam,
                     scm_cons(after_lam, V_NIL))));

    /* (let inner-bindings (dynamic-wind ...)) */
    val_t inner_let = scm_cons(S_LET,
                       scm_cons(inner_bindings,
                        scm_cons(dwind, V_NIL)));

    /* (let outer-bindings inner-let) */
    val_t outer_let = scm_cons(S_LET,
                       scm_cons(outer_bindings,
                        scm_cons(inner_let, V_NIL)));

    compile(c, outer_let, tail, line);
}

/* True iff expr is literally (quote SYM) for some symbol SYM — the shape a
 * source-level 'sym reader-expands to. Used by compile_define_algebra (and
 * its matching internal-define prescan case in compile_lambda above) to
 * detect the overwhelmingly common case where an operator name is a
 * compile-time-known literal rather than a runtime-computed expression.
 * Compares via lang_translate rather than a raw S_QUOTE check, so an
 * Akkadian/cuneiform spelling of quote (e.g. kīma) is recognized too —
 * found by review: without this, (define-algebra (kīma myop) ...) missed
 * the compile-time-literal fast path and fell back to the tree-eval path,
 * reproducing the global-leak bug this function exists to fix for that
 * one spelling. */
bool is_quoted_symbol(val_t expr, val_t *out_sym) {
    if (vis_pair(expr) && lang_translate(vcar(expr)) == S_QUOTE &&
        vis_pair(vcdr(expr)) && vis_symbol(vcar(vcdr(expr))) &&
        vcdr(vcdr(expr)) == V_NIL) {
        *out_sym = vcar(vcdr(expr));
        return true;
    }
    return false;
}

/* Shared by compile_define_rule and compile_define_ruleset: parse one
 * (pattern -> template [#:when guard]) clause. Mirrors eval.c's
 * S_DEFINE_RULE/S_DEFINE_RULESET parsing exactly — the arrow token's own
 * value is never checked, only its position (element 1), matching the
 * pre-existing tree-walker's leniency there. */
static void parse_rule_clause(val_t clause, val_t *pattern, val_t *tmpl, val_t *guard_expr) {
    *pattern = vcar(clause);
    *tmpl    = vcar(vcdr(vcdr(clause)));
    val_t trailing = vcdr(vcdr(vcdr(clause)));
    *guard_expr = V_FALSE;
    if (vis_pair(trailing) && vcar(trailing) == S_KW_WHEN && vis_pair(vcdr(trailing)))
        *guard_expr = vcar(vcdr(trailing));
}

/* Build one (%define-rule! 'pattern 'pvars guard-lambda action-lambda
 * ruleset-name-ast) call — the runtime registration this clause desugars
 * to. pvars is computed HERE, at compile time, via sx_pattern_vars: the
 * pattern is always static source syntax (never evaluated, matching the
 * tree-walker), so there is no need to re-derive it at runtime on every
 * call the way eval.c's tree-walker case does on every invocation.
 * ruleset_name_ast is (quote NAME) for define-ruleset or V_FALSE (itself,
 * self-evaluating) for a standalone define-rule. */
static val_t build_define_rule_call(val_t pattern, val_t tmpl, val_t guard_expr,
                                     val_t ruleset_name_ast) {
    val_t pvars = sx_pattern_vars(pattern);

    val_t pattern_quoted = scm_cons(S_QUOTE, scm_cons(pattern, V_NIL));
    val_t pvars_quoted   = scm_cons(S_QUOTE, scm_cons(pvars, V_NIL));

    val_t action_lam = scm_cons(S_LAMBDA, scm_cons(pvars, scm_cons(tmpl, V_NIL)));
    val_t guard_lam  = (guard_expr != V_FALSE)
        ? scm_cons(S_LAMBDA, scm_cons(pvars, scm_cons(guard_expr, V_NIL)))
        : V_FALSE;

    val_t reg_sym = sym_intern_cstr("%define-rule!");
    return scm_cons(reg_sym,
            scm_cons(pattern_quoted,
             scm_cons(pvars_quoted,
              scm_cons(guard_lam,
               scm_cons(action_lam,
                scm_cons(ruleset_name_ast, V_NIL))))));
}

/* (define-rule pattern -> template [#:when guard]) — one rewrite rule.
 * Desugars to a single build_define_rule_call, compiled as an ordinary
 * call. Unlike define-record-type/symbolic/with-assumptions, this needs no
 * internal-define prescan slot reservation: it binds no name into the
 * enclosing scope at all (see compile_lambda's prescan, S_DEFINE_RULE
 * case) — it only registers a rule into sx_rules.c's process-wide rule
 * table, so there is no lexical-scoping bug to fix here, only the
 * tree-walker's guard/template closures being built against GLOBAL_ENV
 * unconditionally (via tree-eval) instead of the actual enclosing lexical
 * environment, which broke any guard/template referencing a local
 * variable. Native codegen fixes that the same way compile_lambda always
 * has: ordinary closures over the real enclosing scope. */
static void compile_define_rule(Compiler *c, val_t rest, bool tail, int line) {
    if (!vis_pair(rest) || !vis_pair(vcdr(rest)) || !vis_pair(vcdr(vcdr(rest)))) {
        fprintf(stderr, "compiler: define-rule: expected (define-rule pattern -> template)\n");
        emit(c, OP_VOID, line);
        return;
    }
    val_t pattern, tmpl, guard_expr;
    parse_rule_clause(rest, &pattern, &tmpl, &guard_expr);
    val_t call = build_define_rule_call(pattern, tmpl, guard_expr, V_FALSE);
    compile(c, call, tail, line);
}

/* (define-ruleset name (pattern -> template [#:when guard]) ...) — same
 * per-clause desugaring as compile_define_rule, looped over every clause
 * and tagged with the ruleset name (a bare, never-evaluated symbol,
 * matching the tree-walker — quoted here since it's compile-time-known).
 * Malformed clauses are skipped, matching eval.c's per-clause `continue`. */
static void compile_define_ruleset(Compiler *c, val_t rest, bool tail, int line) {
    if (!vis_pair(rest)) {
        fprintf(stderr, "compiler: define-ruleset: expected (define-ruleset name clause ...)\n");
        emit(c, OP_VOID, line);
        return;
    }
    val_t name         = vcar(rest);
    val_t name_quoted  = scm_cons(S_QUOTE, scm_cons(name, V_NIL));

    /* Collect in reverse (cheap prepend), then reverse back so rules
     * register — and therefore fire, per sx_rules.c's append-to-chain-end
     * ordering — in source order, matching the tree-walker. */
    val_t calls_rev = V_NIL;
    int n = 0;
    for (val_t cl = vcdr(rest); vis_pair(cl); cl = vcdr(cl)) {
        val_t clause = vcar(cl);
        if (!vis_pair(clause) || !vis_pair(vcdr(clause)) || !vis_pair(vcdr(vcdr(clause))))
            continue; /* skip malformed clause, matching eval.c */
        val_t pattern, tmpl, guard_expr;
        parse_rule_clause(clause, &pattern, &tmpl, &guard_expr);
        calls_rev = scm_cons(build_define_rule_call(pattern, tmpl, guard_expr, name_quoted),
                              calls_rev);
        n++;
    }
    if (n == 0) { emit(c, OP_VOID, line); return; }

    val_t calls = V_NIL;
    for (val_t p = calls_rev; vis_pair(p); p = vcdr(p)) calls = scm_cons(vcar(p), calls);

    compile_seq(c, calls, tail, line);
}

/* (define-algebra op-expr [#:commutative? b] [#:associative? b]
 *                 [#:identity v] [#:absorbing v] [#:relations fn])
 * Registers algebra info for op-expr's value and auto-binds it to
 * (lambda args (apply sym-expr 'op args)) — see eval.c's S_DEFINE_ALGEBRA.
 *
 * The registration call is always the same shape: (%define-algebra!
 * op-expr kw1 val1-expr kw2 val2-expr ...), with keyword values compiled
 * as ordinary runtime expressions (matching the tree-walker, which
 * evaluates them too — only the keyword tokens themselves are literal).
 *
 * The auto-bind's scoping is where this diverges from a pure mechanical
 * translation, and is worth spelling out: when op-expr is a literal
 * (quote sym) — by far the common usage, see tests/sx_algebra_tests.scm —
 * the bound name is known at COMPILE time, so it gets a real lexical
 * binding via an ordinary (define sym ...), exactly like
 * compile_define_record_type's bindings: correct at top level AND
 * correctly local when used inside a lambda body, unlike the tree-walker's
 * env_define(env, op_name, proc), which — reached only via tree-eval's
 * hardcoded GLOBAL_ENV — always leaked the binding to global scope even
 * when define-algebra appeared inside a function (confirmed: `(define (f)
 * (define-algebra 'myop ...) ...)` left `myop` callable at top level after
 * calling f once).
 *
 * When op-expr is NOT a literal quoted symbol (its value is only known at
 * runtime), there is no way to give it a real lexical binding in a
 * slot-based compiled VM — every local's stack slot is fixed at compile
 * time, the same fundamental limit R7RS's own (define <computed-name> ...)
 * runs into. This rare case is left on the pre-existing tree-eval path
 * (dynamic env_define against GLOBAL_ENV), which is the correct place for
 * a genuinely dynamic top-level-only binding to live, not a bug to fix. */
static void compile_define_algebra(Compiler *c, val_t rest, bool tail, int line) {
    if (!vis_pair(rest)) {
        fprintf(stderr, "compiler: define-algebra: expected operator as first argument\n");
        emit(c, OP_VOID, line);
        return;
    }
    val_t op_expr = vcar(rest);
    val_t kws     = vcdr(rest);

    val_t sym;
    if (!is_quoted_symbol(op_expr, &sym)) {
        /* Dynamic operator name: keep the existing tree-eval behavior. */
        val_t tree_eval_sym = sym_intern_cstr("tree-eval");
        emit_ab(c, OP_LOAD_GLOBAL,
                (uint8_t)chunk_add_const(c->chunk, tree_eval_sym), line);
        emit_const(c, scm_cons(S_DEFINE_ALGEBRA, rest), line);
        emit_ab(c, tail ? OP_TAIL_CALL : OP_CALL, 1, line);
        return;
    }

    val_t reg_sym  = sym_intern_cstr("%define-algebra!");
    val_t reg_call = scm_cons(reg_sym, scm_cons(op_expr, kws));

    val_t sym_expr_sym = sym_intern_cstr("sym-expr");
    val_t apply_sym    = sym_intern_cstr("apply");
    val_t args_sym     = sym_intern_cstr("args");
    val_t op_quoted    = scm_cons(S_QUOTE, scm_cons(sym, V_NIL));
    val_t body = scm_cons(apply_sym,
                  scm_cons(sym_expr_sym,
                   scm_cons(op_quoted,
                    scm_cons(args_sym, V_NIL))));
    val_t lam      = scm_cons(S_LAMBDA, scm_cons(args_sym, scm_cons(body, V_NIL)));
    val_t def_form = scm_cons(S_DEFINE, scm_cons(sym, scm_cons(lam, V_NIL)));

    compile_seq(c, scm_cons(reg_call, scm_cons(def_form, V_NIL)), tail, line);
}

/* (with-assumptions ((var assumption...) ...) body...) — mirrors
 * compile_parameterize immediately above almost exactly: same
 * capture-old/set-new/dynamic-wind-restore shape, just swapping the
 * parameter-procedure call for two tiny new primitives
 * (%assumption-flags, %assumption-set!, %assumption-restore! — see
 * builtins_curry.c) that read/OR-in/overwrite a SymVar's assumption
 * bits directly, matching the tree-walker's S_WITH_ASSUMPTIONS case
 * (eval.c) which this replaces for compiled code. Each clause's
 * assumption keywords (bare symbols, never evaluated — matching the
 * tree-walker) are resolved to a flag bitmask at COMPILE time via
 * sx_assumption_flag and embedded as a self-evaluating fixnum
 * constant, so there's no runtime keyword-lookup cost per entry.
 *
 * One deliberate behavioral divergence from the tree-walker, found by
 * review: eval.c's S_WITH_ASSUMPTIONS interleaves each clause's
 * snapshot-then-set, so if the SAME SymVar appears in two clauses of
 * one with-assumptions form, the second clause's snapshot already
 * includes the first clause's flags, and only the first clause's
 * original value is restored — leaving a residual flag set after the
 * form exits. Here all clauses' original flags are snapshotted
 * upfront (the inner let, before any %assumption-set! runs), so a
 * repeated var is restored to its TRUE original state. Strictly more
 * correct; not expected to be relied upon either way, so not treated
 * as a compatibility break — see tests/sx_algebra_tests.scm.
 * Desugars to:
 *   (let ((%%wa-v0 var-expr0) ...)
 *     (let ((%%wa-o0 (%assumption-flags %%wa-v0)) ...)
 *       (dynamic-wind
 *         (lambda () (%assumption-set! %%wa-v0 FLAGS0) ...)
 *         (lambda () body...)
 *         (lambda () (%assumption-restore! %%wa-v0 %%wa-o0) ...)))) */
static void compile_with_assumptions(Compiler *c, val_t args, bool tail, int line) {
    if (!vis_pair(args)) {
        fprintf(stderr, "compiler: with-assumptions: missing binding list\n");
        emit(c, OP_VOID, line);
        return;
    }
    val_t clauses = vcar(args);
    val_t body    = vcdr(args);

    int n = 0;
    { val_t cl = clauses; while (vis_pair(cl)) { n++; cl = vcdr(cl); } }

#define WA_MAX_CLAUSES 32
    val_t vref[WA_MAX_CLAUSES], oref[WA_MAX_CLAUSES];
    val_t var_expr[WA_MAX_CLAUSES], flags_const[WA_MAX_CLAUSES];
    char namebuf[32];
    if (n > WA_MAX_CLAUSES) n = WA_MAX_CLAUSES;

    val_t cl = clauses;
    for (int i = 0; i < n; i++, cl = vcdr(cl)) {
        val_t clause = vcar(cl);
        if (!vis_pair(clause)) { i--; n--; continue; }
        var_expr[i] = vcar(clause);
        uint32_t flags = 0;
        for (val_t a = vcdr(clause); vis_pair(a); a = vcdr(a))
            flags |= sx_assumption_flag(vcar(a));
        flags_const[i] = vfix((intptr_t)flags);
        snprintf(namebuf, sizeof(namebuf), "%%wa-v%d", i);
        vref[i] = sym_intern_cstr(namebuf);
        snprintf(namebuf, sizeof(namebuf), "%%wa-o%d", i);
        oref[i] = sym_intern_cstr(namebuf);
    }
#undef WA_MAX_CLAUSES

    if (n == 0) {
        compile_seq(c, body, tail, line);
        return;
    }

    val_t flags_sym   = sym_intern_cstr("%assumption-flags");
    val_t set_sym     = sym_intern_cstr("%assumption-set!");
    val_t restore_sym = sym_intern_cstr("%assumption-restore!");
    val_t S_DW        = sym_intern_cstr("dynamic-wind");

    /* Outer let bindings: ((%wa-v0 var-expr0) ...) */
    val_t outer_bindings = V_NIL;
    for (int i = n - 1; i >= 0; i--)
        outer_bindings = scm_cons(scm_cons(vref[i], scm_cons(var_expr[i], V_NIL)),
                                  outer_bindings);

    /* Inner let bindings: ((%wa-o0 (%assumption-flags %wa-v0)) ...) */
    val_t inner_bindings = V_NIL;
    for (int i = n - 1; i >= 0; i--)
        inner_bindings = scm_cons(
            scm_cons(oref[i],
             scm_cons(scm_cons(flags_sym, scm_cons(vref[i], V_NIL)), V_NIL)),
            inner_bindings);

    /* before-lambda body: ((%assumption-set! %wa-v0 FLAGS0) ...) */
    val_t before_body = V_NIL;
    for (int i = n - 1; i >= 0; i--)
        before_body = scm_cons(
            scm_cons(set_sym, scm_cons(vref[i], scm_cons(flags_const[i], V_NIL))),
            before_body);
    val_t before_lam = scm_cons(S_LAMBDA, scm_cons(V_NIL, before_body));

    /* after-lambda body: ((%assumption-restore! %wa-v0 %wa-o0) ...) */
    val_t after_body = V_NIL;
    for (int i = n - 1; i >= 0; i--)
        after_body = scm_cons(
            scm_cons(restore_sym, scm_cons(vref[i], scm_cons(oref[i], V_NIL))),
            after_body);
    val_t after_lam = scm_cons(S_LAMBDA, scm_cons(V_NIL, after_body));

    /* thunk-lambda: (lambda () body...) */
    if (body == V_NIL) body = scm_cons(V_VOID, V_NIL);
    val_t thunk_lam = scm_cons(S_LAMBDA, scm_cons(V_NIL, body));

    val_t dwind = scm_cons(S_DW,
                   scm_cons(before_lam,
                    scm_cons(thunk_lam,
                     scm_cons(after_lam, V_NIL))));

    val_t inner_let = scm_cons(S_LET, scm_cons(inner_bindings, scm_cons(dwind, V_NIL)));
    val_t outer_let = scm_cons(S_LET, scm_cons(outer_bindings, scm_cons(inner_let, V_NIL)));

    compile(c, outer_let, tail, line);
}

/* (receive formals producer-expr body...) — R7RS sugar over
 * call-with-values, which is already a plain builtin primitive (no VM
 * support needed).  Desugar to:
 *   (call-with-values (lambda () producer-expr) (lambda formals body...)) */
static void compile_receive(Compiler *c, val_t args, bool tail, int line) {
    val_t formals  = vcar(args);
    val_t producer = vcar(vcdr(args));
    val_t body     = vcdr(vcdr(args));

    val_t producer_lam = scm_cons(S_LAMBDA, scm_cons(V_NIL, scm_cons(producer, V_NIL)));
    val_t consumer_lam = scm_cons(S_LAMBDA, scm_cons(formals, body));
    val_t cwv = scm_cons(S_CALL_WITH_VALUES,
                 scm_cons(producer_lam,
                  scm_cons(consumer_lam, V_NIL)));

    compile(c, cwv, tail, line);
}

/* ── Application compilation ─────────────────────────────────────────── */

/* Matches compile()'s own "#:keyword symbols are self-evaluating" check
 * (below) so compile_call's fused-global-call fast path can defer to it:
 * a #:keyword head must still compile to "push the symbol itself as a
 * constant, then try to call it" (raising not-a-procedure at runtime),
 * not "look it up as a global variable" (raising unbound-variable) --
 * the fast path bypasses compile()'s general symbol handling entirely,
 * so without this guard it silently changed the error a caller sees for
 * `(#:foo 1 2)` from one condition type to another (found by code
 * review). */
bool is_keyword_symbol(val_t v) {
    Symbol *s = as_sym(v);
    return s->len >= 2 && s->data[0] == '#' && s->data[1] == ':';
}

static void compile_call(Compiler *c, val_t head, val_t args, bool tail, int line) {
    /* Count args */
    int argc = 0;
    val_t a = args;
    while (vis_pair(a)) { argc++; a = vcdr(a); }

    /* Self-tail-call: a tail-position call whose head is exactly the
     * enclosing named-let loop's own self-reference (Compiler::
     * self_tail_name, set up by compile_let's named-let branch). Trusted
     * at compile time ONLY because that reference is a private upvalue
     * slot no code outside this one lambda body can ever reach, and the
     * set!-scan (body_mentions_set_target, run once up front over the
     * whole raw body) already ruled out this body ever mutating it.
     * resolve_local guards against an inner local of the same name
     * shadowing the loop variable at THIS call site specifically (e.g. a
     * nested (let ((loop 5)) (loop)) inside the loop body must resolve to
     * the LOCAL 5, not the outer loop closure). */
    if (tail && vis_symbol(head) && c->self_tail_name != V_FALSE &&
        head == c->self_tail_name && !c->self_tail_mutated &&
        resolve_local(c, head) < 0) {
        /* Still register the self-reference as a captured upvalue (for
         * its SIDE EFFECT, discarding the returned index -- the bytecode
         * itself never loads it via OP_LOAD_UP, since OP_SELF_TAIL_CALL
         * needs no callee on the stack at all) rather than skipping this
         * call entirely. Without this, the closure's own upval_count/
         * upvals[] never gets the self-capture entry that maybe_jit_bcc
         * (src/runtime.c) depends on to detect "this is a self-
         * referencing named-let loop, never promote it to native code" --
         * omitting it let a hot loop cross the JIT threshold and hit a
         * real, previously-dormant JIT codegen bug for self-referencing
         * closures the very guard this restores exists to prevent
         * (confirmed by reproducing: an identical loop failed with
         * "unbound variable: loop" past ~50 iterations before this fix,
         * worked correctly after). */
        resolve_upvalue(c, head);
        /* Tier 2.3: reserve/release brackets this loop -- see
         * reserve_pending_slot's own comment (compiler.c) for why: an
         * argument compiled here can recurse back into ir_emit's IR_CALL
         * inline branch (via compile()'s own IR_OR_CLASSIC), which must
         * not alias an earlier, still-pending sibling argument's stack
         * slot. */
        {
            int saved = c->local_count;
            a = args;
            while (vis_pair(a)) {
                compile(c, vcar(a), false, line);
                reserve_pending_slot(c);
                a = vcdr(a);
            }
            release_pending_slots(c, saved);
        }
        emit_ab(c, OP_SELF_TAIL_CALL, (uint8_t)argc, line);
        return;
    }

    /* Fused global call: a call whose head is a symbol resolving to
     * neither a local nor an upvalue (the same condition emit_load uses
     * to fall through to OP_LOAD_GLOBAL) -- look the global up and call
     * it in one dispatch instead of two. Every non-BcClosure callee type
     * still goes through call_foreign inside the opcode handler itself,
     * exactly as OP_CALL/OP_TAIL_CALL already do -- see the vm.c handler's
     * own comment for why this is safe for every callee type curry has
     * (primitives, continuations, parameter objects, FFI functions), not
     * just closures -- the specific failure Kaappi's own postmortem
     * documented for a naively-fused call opcode that only dispatched two
     * of the possible callee types directly. resolve_upvalue has the
     * side effect of marking an enclosing local captured/registering the
     * upvalue slot if `head` turns out to resolve as one -- unavoidable
     * and harmless here: that bookkeeping is required regardless of
     * which call opcode ultimately gets emitted. */
    if (vis_symbol(head) && !is_keyword_symbol(head) &&
        resolve_local(c, head) < 0 && resolve_upvalue(c, head) < 0) {
        int ci = chunk_add_const(c->chunk, head);
        /* Tier 2.3: see the self-tail-call branch's own comment above. */
        {
            int saved = c->local_count;
            a = args;
            while (vis_pair(a)) {
                compile(c, vcar(a), false, line);
                reserve_pending_slot(c);
                a = vcdr(a);
            }
            release_pending_slots(c, saved);
        }
        emit_abc(c, tail ? OP_TAIL_CALL_GLOBAL : OP_CALL_GLOBAL,
                 (uint8_t)ci, (uint8_t)argc, line);
        return;
    }

    /* Tier 2.3: the callee itself is ALSO a pending value while args are
     * compiled below -- reserve it too, not just the args. */
    {
        int saved = c->local_count;
        compile(c, head, false, line);
        reserve_pending_slot(c);
        a = args;
        while (vis_pair(a)) {
            compile(c, vcar(a), false, line);
            reserve_pending_slot(c);
            a = vcdr(a);
        }
        release_pending_slots(c, saved);
    }

    emit_ab(c, tail ? OP_TAIL_CALL : OP_CALL, (uint8_t)argc, line);
}

/* ── Main dispatch ───────────────────────────────────────────────────── */

/* Every compound-form classification compile()'s own dispatch below can
 * reach, in the same order compile() tests for them. Shared with
 * ir_lower (via classify_head, below) so the Tier 2.1 IR's own "is this
 * actually a plain call?" decision for IR_CALL can never drift out of
 * sync with what compile() itself would do -- a hand-duplicated second
 * copy of this same ~35-symbol chain was rejected as exactly the
 * two-lists-that-must-stay-in-sync hazard this project's own IR_SEQ/
 * IR_VAR_REF postmortems (see ir.h) warn about. `SpecialForm` itself is
 * declared in compiler_internal.h, shared with ir_lower.c's dispatch. */

/* Classification only -- never executes a handler, never mutates `c`
 * except via the macro-lookup path's own resolve_syntax_local/env_lookup
 * calls, which are read-only queries (unlike resolve_local/
 * resolve_upvalue, they register nothing). Sets *transformer_out only
 * when returning SF_MACRO. Conditions and their order are copied
 * verbatim from compile()'s own dispatch chain below -- keep the two in
 * sync by construction: compile() switches on this function's result
 * rather than re-testing `head` itself. */
SpecialForm classify_head(Compiler *c, val_t head, val_t args,
                           val_t *transformer_out) {
    if (head == S_QUOTE) return SF_QUOTE;
    if (head == S_QUASIQUOTE) return SF_QUASIQUOTE;
    if (head == S_IF) return SF_IF;
    if (head == S_BEGIN) return SF_BEGIN;
    if (head == S_COND_EXPAND) return SF_COND_EXPAND;
    if (head == S_DEFINE) return SF_DEFINE;
    if (head == S_DEFINE_VALUES) return SF_DEFINE_VALUES;
    if (head == S_DEFINED_P) return SF_DEFINED_P;
    if (head == S_SET) return SF_SET;
    if (head == S_LAMBDA) return SF_LAMBDA;
    if (head == S_LET) return SF_LET;
    if (head == S_LET_STAR) return SF_LET_STAR;
    if (head == S_LETREC || head == S_LETREC_STAR) return SF_LETREC;
    if (head == S_LET_VALUES) return SF_LET_VALUES;
    if (head == S_LET_STAR_VALUES) return SF_LET_STAR_VALUES;
    if (head == S_AND) return SF_AND;
    if (head == S_OR) return SF_OR;
    if (head == S_COND) return SF_COND;
    if (head == S_CASE) return SF_CASE;
    if (head == S_WHEN) return SF_WHEN;
    if (head == S_UNLESS) return SF_UNLESS;
    if (head == S_DELAY) return SF_DELAY;
    if (head == S_DELAY_FORCE) return SF_DELAY_FORCE;
    if (head == S_DO) return SF_DO;
    if (head == S_VALUES) return SF_VALUES;
    if (head == S_APPLY) return SF_APPLY;
    if (head == S_CALL_WITH_VALUES && vis_pair(args) &&
        vis_pair(vcdr(args)) && vis_nil(vcdr(vcdr(args))))
        return SF_CALL_WITH_VALUES;
    if (head == S_PARAMETERIZE) return SF_PARAMETERIZE;
    if (head == S_GUARD) return SF_GUARD;
    if (head == S_WITH_EXCEPTION_HANDLER) return SF_WITH_EXCEPTION_HANDLER;
    if (head == S_RECEIVE && vis_pair(args) && vis_pair(vcdr(args)))
        return SF_RECEIVE;
    if (head == S_DEFINE_RECORD_TYPE) return SF_DEFINE_RECORD_TYPE;
    if (head == S_DEFINE_SYNTAX) return SF_DEFINE_SYNTAX;
    if (head == S_LET_SYNTAX || head == S_LETREC_SYNTAX) return SF_LET_SYNTAX;
    if (head == S_SYMBOLIC) return SF_SYMBOLIC;
    if (head == S_WITH_ASSUMPTIONS) return SF_WITH_ASSUMPTIONS;
    if (head == S_DEFINE_RULE) return SF_DEFINE_RULE;
    if (head == S_DEFINE_RULESET) return SF_DEFINE_RULESET;
    if (head == S_DEFINE_ALGEBRA) return SF_DEFINE_ALGEBRA;
    if (head == S_IMPORT || head == S_DEFINE_LIBRARY || head == S_LIBRARY)
        return SF_TREE_EVAL;
    if (vis_symbol(head)) {
        val_t transformer;
        bool is_macro = resolve_syntax_local(c, head, &transformer);
        if (!is_macro && c->chunk->target_env != V_VOID) {
            val_t macro = env_lookup_or_false(c->chunk->target_env, head);
            is_macro = vis_syntax(macro);
            if (is_macro) transformer = as_syntax(macro)->transformer;
        }
        if (!is_macro) {
            val_t macro = env_lookup_or_false(GLOBAL_ENV, head);
            is_macro = vis_syntax(macro);
            if (is_macro) transformer = as_syntax(macro)->transformer;
        }
        if (is_macro) { *transformer_out = transformer; return SF_MACRO; }
    }
    return SF_NONE;
}

/* Tries the Tier 2.1/2.2 IR pipeline (ir_lower -> ir_optimize -> ir_emit)
 * for the current `expr`/`tail`/`line` (all in scope wherever this is
 * used, inside compile()'s own switch below); if ir_lower can't actually
 * lower this specific case (falls back to IR_FALLBACK -- only possible
 * for SF_DEFINE today, whose malformed-target shape compile_define
 * itself handles gracefully rather than crashing, so ir_lower can't
 * safely claim unconditional coverage the way SF_IF/SF_LAMBDA/etc. do;
 * see ir_lower's own S_LAMBDA/S_LET comments), falls back to
 * `classic_stmt` instead of calling ir_emit -- calling ir_emit on an
 * IR_FALLBACK node would call compile() again on this SAME expr, which
 * re-classifies to this SAME case and re-enters this SAME macro: an
 * infinite loop on malformed input (found during design review, before
 * compile() ever called ir_lower live -- no existing test exercised
 * malformed input through the live dispatch to catch it). Applied
 * uniformly to every case below, including ones where ir_lower is
 * already known to have unconditional coverage (SF_IF, SF_SET, SF_AND,
 * SF_OR, SF_LAMBDA, SF_LET, SF_LET_STAR, SF_LETREC, and the SF_NONE/
 * ordinary-call fallthrough) -- redundant but cheap for those, and
 * removes any need to keep a second, driftable list of "which cases are
 * actually safe" in sync with ir_lower's own guards.
 *
 * Also checks g_force_classic_compile first -- see its own comment --
 * so compile_classic can force a genuinely IR-free compile of the WHOLE
 * recursive tree for compiler_ir_self_check/compiler_ir_optimize_check's
 * "old" side, which needs one now that this macro means compile() no
 * longer IS classic-only by default. */
#define IR_OR_CLASSIC(classic_stmt) do {                              \
        if (g_force_classic_compile) { classic_stmt; return; }        \
        IRNode *_ir = ir_lower(c, expr, tail, line);                  \
        if (_ir->kind == IR_FALLBACK) { classic_stmt; return; }       \
        ir_emit(c, ir_optimize(_ir));                                 \
        return;                                                       \
    } while (0)

void compile(Compiler *c, val_t expr, bool tail, int line) {

    /* Reader-annotated source line: the reader stamps each cons cell's
       hdr.flags with the 1-based line its car's datum started on (0 for
       pairs built by macros/runtime, which inherit the enclosing line). */
    if (vis_pair(expr) && as_pair(expr)->hdr.flags)
        line = (int)as_pair(expr)->hdr.flags;

    /* ── Self-evaluating atoms ── */
    if (vis_fixnum(expr) || vis_flonum(expr) || vis_bignum(expr) ||
        vis_rational(expr) || vis_complex(expr) || vis_string(expr) ||
        vis_char(expr)) {
        emit_const(c, expr, line);
        return;
    }
    if (expr == V_TRUE)  { emit(c, OP_TRUE,  line); return; }
    if (expr == V_FALSE) { emit(c, OP_FALSE, line); return; }
    if (expr == V_NIL)   { emit(c, OP_NIL,   line); return; }
    if (expr == V_VOID)  { emit(c, OP_VOID,  line); return; }

    /* ── Symbol → variable load ── */
    if (vis_symbol(expr)) {
        /* #:keyword symbols (Guile/Racket-style) are self-evaluating */
        Symbol *ksym = as_sym(expr);
        if (ksym->len >= 2 && ksym->data[0] == '#' && ksym->data[1] == ':') {
            emit_const(c, expr, line);
            return;
        }
        emit_load(c, expr, line);
        return;
    }

    /* ── Non-pair non-symbol: quote it ── */
    if (!vis_pair(expr)) {
        emit_const(c, expr, line);
        return;
    }

    /* ── Compound form (head . args) ── */
    val_t head = lang_translate(vcar(expr));
    val_t args = vcdr(expr);

    val_t transformer = V_FALSE;
    switch (classify_head(c, head, args, &transformer)) {

    /* quote */
    case SF_QUOTE:
        emit_const(c, vis_pair(args) ? vcar(args) : V_NIL, line);
        return;

    /* quasiquote — expand to list-construction code, then compile that */
    case SF_QUASIQUOTE: {
        val_t expanded = expand_qq(vis_pair(args) ? vcar(args) : V_NIL, GLOBAL_ENV, 0);
        compile(c, expanded, tail, line);
        return;
    }

    /* if -- routed through the Tier 2.1 IR (see IR_OR_CLASSIC's comment) */
    case SF_IF: IR_OR_CLASSIC(compile_if(c, args, tail, line));

    /* begin */
    case SF_BEGIN: IR_OR_CLASSIC(compile_begin(c, args, tail, line));

    /* cond-expand — resolved entirely at compile time: pick the first
     * satisfied clause's body (see features.c) and compile it in place,
     * exactly as if it had been written as (begin body...) here. */
    case SF_COND_EXPAND: {
        bool matched;
        val_t body = cond_expand_choose(args, &matched);
        if (!matched) scm_raise(V_FALSE, "cond-expand: no matching clause");
        compile_begin(c, body, tail, line);
        return;
    }

    /* define -- the one case where IR_OR_CLASSIC's fallback path is
     * actually reachable (a malformed target); see its own comment. */
    case SF_DEFINE: IR_OR_CLASSIC(compile_define(c, args, line));

    /* define-values */
    case SF_DEFINE_VALUES: compile_define_values(c, args, line); return;

    /* defined? */
    case SF_DEFINED_P: compile_defined_p(c, args, line); return;

    /* set! */
    case SF_SET: IR_OR_CLASSIC(compile_set(c, args, line));

    /* lambda */
    case SF_LAMBDA: IR_OR_CLASSIC({
        require_min_args(args, 1, "lambda");
        val_t params = vcar(args);
        val_t body   = vcdr(args);
        compile_lambda(c, params, body, NULL, line);
    });

    /* let */
    case SF_LET:      IR_OR_CLASSIC(compile_let(c, args, tail, line));
    case SF_LET_STAR: IR_OR_CLASSIC(compile_let_star(c, args, tail, line));
    case SF_LETREC:   IR_OR_CLASSIC(compile_letrec(c, args, tail, line));
    case SF_LET_VALUES:      compile_let_values(c, args, tail, line);      return;
    case SF_LET_STAR_VALUES: compile_let_star_values(c, args, tail, line); return;

    /* and / or */
    case SF_AND: IR_OR_CLASSIC(compile_and(c, args, tail, line));
    case SF_OR:  IR_OR_CLASSIC(compile_or(c, args, tail, line));

    /* cond / case */
    case SF_COND: compile_cond(c, args, tail, line); return;
    case SF_CASE: compile_case(c, args, tail, line); return;

    /* when / unless */
    case SF_WHEN:   compile_when(c, args, tail, line);   return;
    case SF_UNLESS: compile_unless(c, args, tail, line); return;

    /* delay / delay-force */
    case SF_DELAY:       compile_delay(c, args, false, tail, line); return;
    case SF_DELAY_FORCE: compile_delay(c, args, true,  tail, line); return;

    /* do */
    case SF_DO: compile_do(c, args, tail, line); return;

    /* values */
    case SF_VALUES: {
        /* Tier 2.4 fix (found via a real, confirmed miscompilation:
         * `(values 1 (let ((x 5)) x) 2)` returned (1 1 2) instead of
         * (1 5 2)): this loop predates Tier 2.3's reserve_pending_slot
         * convention and was never updated for it -- compile_call's own
         * identical argument loop already has this bracketing (see its
         * own "Tier 2.3" comment), but SF_VALUES/SF_APPLY/
         * SF_CALL_WITH_VALUES below are classic special forms with no IR-
         * native form of their own, so they never got the same pass. Any
         * one of these several already-pushed-but-untracked pending
         * values (e.g. `1` above) is exactly the "still-pending sibling
         * argument" reserve_pending_slot's own comment warns about: a
         * LATER item that happens to be a let / let* / letrec / letrec* /
         * named-let now splices real locals directly into `c` (Tier 2.4), and
         * without this bracketing it computes its own new slot indices
         * against a `c->local_count` that doesn't yet account for the
         * earlier items' own already-pushed values -- aliasing physical
         * stack positions that are still very much in use. */
        int n = 0;
        val_t a = args;
        int saved = c->local_count;
        while (vis_pair(a)) {
            compile(c, vcar(a), false, line);
            reserve_pending_slot(c);
            n++; a = vcdr(a);
        }
        release_pending_slots(c, saved);
        emit_ab(c, OP_VALUES, (uint8_t)n, line);
        return;
    }

    /* apply — (apply f arg1 ... rest-list) */
    case SF_APPLY: {
        /* Tier 2.4 fix -- see SF_VALUES's own comment just above for the
         * full rationale; same bug, same fix, this is the actual site
         * where it was originally found: `(apply + (list 1 (let ((x 5))
         * x) 2))` returned 4 instead of 8. */
        int n = 0;
        val_t a = args;
        int saved = c->local_count;
        while (vis_pair(a)) {
            compile(c, vcar(a), false, line);
            reserve_pending_slot(c);
            n++; a = vcdr(a);
        }
        release_pending_slots(c, saved);
        emit_ab(c, OP_APPLY, (uint8_t)n, line); /* n = fn + intermediates + last-list */
        return;
    }

    /* call-with-values — (call-with-values producer consumer), literal
     * 2-argument syntactic position only (same unconditional special-
     * casing convention as `apply`/`values` above, not checking for local
     * shadowing of the name; still callable indirectly as an ordinary
     * value through a rebound identifier, just without this fast path).
     * OP_TAIL_CALL_WITH_VALUES vs OP_CALL_WITH_VALUES exists for exactly
     * the same reason OP_TAIL_CALL vs OP_CALL does: prim_call_with_values
     * (builtins.c) invokes its consumer via a real nested C call, so a
     * receive/let-values/let*-values/with-values in tail position of a
     * self-recursive loop would otherwise accumulate one nested call
     * frame per iteration instead of looping forever. Shape already
     * verified by classify_head. */
    case SF_CALL_WITH_VALUES: {
        /* Tier 2.4 fix -- see SF_VALUES's own comment above for the full
         * rationale; same bug (the producer thunk is a still-pending
         * value while the consumer is compiled). */
        int saved = c->local_count;
        compile(c, vcar(args), false, line);
        reserve_pending_slot(c);
        compile(c, vcar(vcdr(args)), false, line);
        reserve_pending_slot(c);
        release_pending_slots(c, saved);
        emit(c, tail ? OP_TAIL_CALL_WITH_VALUES : OP_CALL_WITH_VALUES, line);
        return;
    }

    /* parameterize — desugar to let + dynamic-wind in the compiler so that
       local variables in the body are captured as upvalues, not looked up
       in GLOBAL_ENV (which would fail for BcClosure-local bindings). */
    case SF_PARAMETERIZE:
        compile_parameterize(c, args, tail, line);
        return;

    case SF_GUARD:
        compile_guard(c, args, tail, line);
        return;

    case SF_WITH_EXCEPTION_HANDLER:
        compile_with_exception_handler(c, args, tail, line);
        return;

    /* `receive` is ambiguous: it's both the R7RS special form (requires at
     * least 2 forms after it: formals and a producer expression, body...
     * optional) and — pre-existing in this codebase — the actor-mailbox
     * primitive (arity 0-1: optional timeout). The two are unambiguous by
     * argument count alone, so only treat it as the special form when the
     * shape can't be the primitive (already verified by classify_head);
     * otherwise it classifies as SF_NONE, falling through to an ordinary
     * call so (receive) and (receive timeout) keep working. */
    case SF_RECEIVE:
        compile_receive(c, args, tail, line);
        return;

    case SF_DEFINE_RECORD_TYPE:
        compile_define_record_type(c, args, line);
        return;

    case SF_DEFINE_SYNTAX:
        compile_define_syntax(c, args, line);
        return;

    case SF_LET_SYNTAX:
        compile_let_syntax(c, args, tail, line);
        return;

    case SF_SYMBOLIC:
        compile_symbolic(c, args, line);
        return;

    case SF_WITH_ASSUMPTIONS:
        compile_with_assumptions(c, args, tail, line);
        return;

    case SF_DEFINE_RULE:
        compile_define_rule(c, args, tail, line);
        return;

    case SF_DEFINE_RULESET:
        compile_define_ruleset(c, args, tail, line);
        return;

    case SF_DEFINE_ALGEBRA:
        compile_define_algebra(c, args, tail, line);
        return;

    /* Note: syntax-rules itself is deliberately NOT special-cased here.
     * syntax_rules_register() binds the symbol `syntax-rules` to a T_SYNTAX
     * in GLOBAL_ENV whose transformer (sr_compile_fn) takes the raw
     * (syntax-rules literals rule...) form and returns a self-evaluating
     * T_PRIMITIVE transformer — so a (syntax-rules ...) expression, e.g. as
     * define-syntax's transformer-expr, already gets handled by the
     * ordinary "macro expansion" case below like any other macro use,
     * with no eval()/tree-eval dependency at all. */

    /* Forms the compiler delegates to the tree-walker at runtime:
       import, define-library, library.
       Emitted as OP_TREE_EVAL_CACHED, not a (tree-eval '<form>) call --
       see chunk.h's Chunk::tree_eval_cache and opcode.h's own comment.
       Memoizes per call SITE (this exact bytecode instruction, keyed by
       constant-pool index), not globally: re-executing the SAME chunk
       (e.g. an import nested inside a function called from a loop) only
       actually runs the form through eval() once. No tail/non-tail
       distinction needed -- these forms are side-effecting declarations
       that always return normally, never altering control flow. */
    case SF_TREE_EVAL: {
        int ci = chunk_add_const(c->chunk, expr);
        emit_ab(c, OP_TREE_EVAL_CACHED, (uint8_t)ci, line);
        return;
    }

    /* Macro expansion: head is a symbol bound to a macro (classify_head
       already resolved which one -- local macro via c->syntax_locals,
       this chunk's own target_env, or GLOBAL_ENV, in that shadowing
       order, with `transformer` set accordingly); expand and recompile.
       Expansion errors are deferred to runtime via (raise ...) so that
       guard forms inside lambdas can catch zero-clause or no-match errors. */
    case SF_MACRO: {
        ExnHandler h;
        val_t expanded = V_FALSE;
        val_t exn_val  = V_FALSE;
        uint64_t _expand_t0 = curry_timings_enabled ? profiling_now_ns() : 0;
        /* sr_current_env (syntax_rules.c) is how sr_compile_fn learns
         * a (syntax-rules ...) expression's defining environment, to
         * capture as the resulting transformer's def_env -- eval.c's
         * tree-walker save/set/restores this around every macro
         * apply() (see its own comment at the analogous site), but
         * this compiler.c macro-expansion call had no equivalent,
         * leaving sr_current_env at its V_FALSE default (mapped to
         * GLOBAL_ENV by sr_compile_fn) for every macro compiled here
         * -- including a library's own (define-syntax ... (syntax-
         * rules ...)) compiled against its target_env (chunk.h). That
         * made sr_is_protected's def_env check miss any library-local
         * helper procedure/macro a template referenced (only visible
         * via target_env, never GLOBAL_ENV), incorrectly renaming it
         * to a fresh gensym and breaking self-recursive macros that
         * call a sibling from their own library body (found via
         * (curry schematic extract)'s %match-case calling %match).
         * Save/restore, not a bare set, so a nested macro expansion
         * triggered from within this apply() doesn't leak this env
         * into an unrelated later expansion. */
        val_t saved_sr_env = sr_get_current_env();
        sr_set_current_env(c->chunk->target_env != V_VOID ? c->chunk->target_env : GLOBAL_ENV);
        SCM_PROTECT(h,
            expanded = apply(transformer, scm_cons(expr, V_NIL)),
            exn_val  = h.exn);
        sr_set_current_env(saved_sr_env);
        if (curry_timings_enabled)
            curry_timing_expand_ns += profiling_now_ns() - _expand_t0;
        if (exn_val != V_FALSE) {
            /* Expansion failed — emit (raise <error>) to defer to runtime */
            val_t raise_sym = sym_intern_cstr("raise");
            val_t raise_form = scm_cons(raise_sym,
                                   scm_cons(scm_cons(S_QUOTE,
                                                scm_cons(exn_val, V_NIL)),
                                            V_NIL));
            compile(c, raise_form, tail, line);
        } else {
            compile(c, expanded, tail, line);
        }
        return;
    }

    case SF_NONE:
        break;
    }

    /* Fallthrough: function call -- routed through the IR too. */
    IR_OR_CLASSIC(compile_call(c, head, args, tail, line));
}

/* Compiles `expr` via compile()'s ORIGINAL, pre-IR dispatch -- guaranteed
 * to never touch ir_lower/ir_emit anywhere in the resulting tree, however
 * deep. Needed because compile_if/compile_let/etc. call compile() itself
 * (not a pluggable alternative) for their own subexpressions -- once
 * compile()'s own dispatch routes through the IR (IR_OR_CLASSIC, above),
 * merely calling a classic per-form function once at the top no longer
 * guarantees the WHOLE recursive tree stays IR-free, since that
 * function's own nested compile() calls would immediately route back
 * through the IR again. Setting g_force_classic_compile before calling
 * compile() sidesteps that: it's a thread-local IR_OR_CLASSIC itself
 * checks, so it stays in effect for every nested compile() call in the
 * same dynamic extent, not just this one.
 *
 * This exists for exactly one reason: compiler_ir_self_check and
 * compiler_ir_optimize_check need a genuine, independent classic
 * implementation to compare the IR pipeline against. Before compile()
 * routed through the IR, compile() itself WAS that independent
 * implementation; now it isn't, so this recovers it. Without this,
 * "old_c" and "new_c" in both those functions would silently collapse
 * into the exact same code path, and their comparison would trivially
 * pass regardless of any future bug in ir_lower/ir_emit -- a tautology,
 * not a test (found during design review, before compile() ever called
 * ir_lower live).
 *
 * SCM_PROTECT'd so a raise mid-compile still restores the flag -- a
 * naive save/set/compile/restore would leave it stuck at `true` for the
 * rest of the process on any compile-time error, silently disabling the
 * IR pipeline for every later compile on this thread. */
void compile_classic(Compiler *c, val_t expr, bool tail, int line) {
    bool saved = g_force_classic_compile;
    g_force_classic_compile = true;

    ExnHandler h;
    bool raised = false;
    SCM_PROTECT(h, {
        compile(c, expr, tail, line);
    }, {
        raised = true;
    });

    g_force_classic_compile = saved;
    if (raised) scm_raise_val(h.exn);
}

/* Compile a list of expressions; only the last is in tail position */
void compile_seq(Compiler *c, val_t list, bool tail, int line) {
    if (vis_nil(list)) { emit(c, OP_VOID, line); return; }
    while (vis_pair(list)) {
        val_t expr = vcar(list);
        val_t rest = vcdr(list);
        bool  last = vis_nil(rest);
        /* track the sequence's own cons-cell line so glue ops (OP_POP)
           are stamped with the form they follow, not the seq's first line */
        if (as_pair(list)->hdr.flags)
            line = (int)as_pair(list)->hdr.flags;
        compile(c, expr, tail && last, line);
        if (!last) emit(c, OP_POP, line);
        list = rest;
    }
}
