#include "modules.h"
#include "object.h"
#include "eval.h"
#include "env.h"
#include "symbol.h"
#include "builtins.h"
#include "gc.h"
#include "reader.h"
#include "port.h"
#include "lang_registry.h"
#include "curry_features.h"
#include "vm.h"
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#ifdef __linux__
#  include <unistd.h>
#  include <limits.h>
#endif
#ifdef __APPLE__
#  include <mach-o/dyld.h>
#  include <limits.h>
#endif

extern void scm_raise(val_t kind, const char *fmt, ...) __attribute__((noreturn));

/* Module registry: map from name-list -> Module* */
typedef struct ModuleEntry {
    val_t           name;    /* list of symbols */
    Module         *module;
    struct ModuleEntry *next;
} ModuleEntry;

static ModuleEntry *registry = NULL;

/* Issue #143: found during review of #141's identical fix for
 * sx_rules.c's rtab / sx_algebra.c's atab -- curry actors are real OS
 * threads with no global interpreter lock, and this registry (like
 * those tables) was read and written with zero synchronization,
 * despite `import` being reachable from any actor concurrently.
 * registry_insert's prepend ("e->next = registry; registry = e;") is
 * an unsynchronized read-modify-write on the shared head pointer: two
 * concurrent inserts can both read the same old head, and whichever
 * write loses silently drops that entry from the list forever (the
 * exact same lost-update sx_rule_add's identical unsynchronized append
 * had). Single-writer/many-reader, matching sx_rules.c/sx_algebra.c's
 * rtab_lock/atab_lock design: mutation (module import) is comparatively
 * rare next to lookups.
 *
 * Unlike rtab/atab, this registry has no removal function at all
 * (modules are never unloaded), so there is no unlink-vs-traversal
 * hazard to worry about, and no per-entry field is ever mutated after
 * publish except by scan_module_registry's GC evac/fwd fixups -- so
 * registry_lookup's traversal never calls back into arbitrary Scheme
 * code, and needs no snapshot-then-release pattern the way
 * sx_rule_try's guard_fn/action_fn callbacks did. */
static pthread_rwlock_t module_registry_lock = PTHREAD_RWLOCK_INITIALIZER;

static bool names_equal(val_t a, val_t b) {
    while (vis_pair(a) && vis_pair(b)) {
        if (vcar(a) != vcar(b)) return false;
        a = vcdr(a); b = vcdr(b);
    }
    return vis_nil(a) && vis_nil(b);
}

static Module *registry_lookup(val_t name) {
    pthread_rwlock_rdlock(&module_registry_lock);
    Module *found = NULL;
    for (ModuleEntry *e = registry; e; e = e->next)
        if (names_equal(e->name, name)) { found = e->module; break; }
    pthread_rwlock_unlock(&module_registry_lock);
    return found;
}

static void registry_insert(val_t name, Module *mod) {
    /* Allocate before taking the lock, same reasoning as sx_rule_add:
     * gc_alloc_raw_pinned is itself an allocation, which under
     * --gc generational can trigger a minor GC whose ext scanner
     * (scan_module_registry, below) takes this same lock for writing --
     * pthread_rwlock_t is not recursive, so allocating while already
     * holding the lock (even for reading) could self-deadlock. */
    ModuleEntry *e = (ModuleEntry *)gc_alloc_raw_pinned(sizeof(ModuleEntry));
    e->name   = name;
    e->module = mod;
    pthread_rwlock_wrlock(&module_registry_lock);
    e->next   = registry;
    registry  = e;
    pthread_rwlock_unlock(&module_registry_lock);
}

/* ---- Module search path ---- */

#define MAX_SEARCH_DIRS 32
static const char *search_dirs[MAX_SEARCH_DIRS];
static int num_search_dirs = 0;

static void add_search_dir(const char *dir) {
    if (num_search_dirs < MAX_SEARCH_DIRS)
        search_dirs[num_search_dirs++] = strdup(dir);
}

/* Convert (curry json) -> "curry/json" */
static void name_to_path(val_t name, char *buf, size_t cap) {
    size_t pos = 0;
    while (vis_pair(name)) {
        /* R7RS library names may contain exact non-negative integers as
         * well as identifiers (e.g. (srfi 1)) — sym_cstr() assumes a
         * symbol object, so a bare fixnum component is rendered as its
         * decimal digits instead. */
        char numbuf[24];
        const char *seg;
        val_t component = vcar(name);
        if (vis_fixnum(component)) {
            snprintf(numbuf, sizeof(numbuf), "%ld", (long)vunfix(component));
            seg = numbuf;
        } else if (vis_symbol(component)) {
            seg = sym_cstr(component);
        } else {
            /* Anything else (bignum, string, boolean, ...) is not a valid
             * R7RS library-name component (identifier or exact
             * non-negative integer) — sym_cstr()/as_sym() assume a symbol
             * object and would misinterpret this value's header, so raise
             * a clear error instead of reading garbage or crashing. */
            scm_raise(V_FALSE, "invalid library name component (expected an identifier or exact integer)");
        }
        size_t slen = strlen(seg);
        if (pos + slen + 2 < cap) {
            if (pos > 0) buf[pos++] = '/';
            memcpy(buf + pos, seg, slen);
            pos += slen;
        }
        name = vcdr(name);
    }
    buf[pos] = '\0';
}

/* ---- C extension API ---- */

struct CurryVM {
    val_t env;
};

CurryVM *curry_vm_new(val_t module_env) {
    CurryVM *vm = (CurryVM *)gc_alloc_raw_pinned(sizeof(CurryVM));
    vm->env = module_env;
    return vm;
}

val_t curry_vm_env(CurryVM *vm) { return vm->env; }

void curry_define_fn(CurryVM *vm, const char *name, CurryFn fn,
                     int min_args, int max_args, void *ud) {
    Primitive *p = CURRY_NEW_PINNED(Primitive);
    p->hdr.type  = T_PRIMITIVE; p->hdr.flags = 0;
    p->name      = name;
    p->min_args  = min_args;
    p->max_args  = max_args;
    p->fn        = fn;
    p->ud        = ud;
    env_define(vm->env, sym_intern_cstr(name), vptr(p));
}

void curry_define_val(CurryVM *vm, const char *name, val_t value) {
    env_define(vm->env, sym_intern_cstr(name), value);
}

/* ---- Module allocation ---- */

/* has_exports = false means "no export list" — every binding in mod_env is
 * importable (C modules, plain .scm files loaded without define-library, and
 * always-on builtin module aliases). has_exports = true means only the
 * symbols in `exports` are importable. */
static Module *make_module(val_t name, val_t mod_env, val_t exports,
                            bool has_exports, void *dl_handle) {
    Module *mod = CURRY_NEW_PINNED(Module);
    mod->hdr.type    = T_MODULE; mod->hdr.flags = 0;
    mod->name        = name;
    mod->env         = as_env(mod_env);
    mod->exports     = exports;
    mod->has_exports = has_exports;
    mod->dl_handle   = dl_handle;
    return mod;
}

/* ---- Load a C extension .so ---- */

static Module *load_c_module(val_t name, const char *so_path) {
    /* Probe silently first — missing file is normal (we fall back to .scm). */
    if (access(so_path, F_OK) != 0) return NULL;

    void *handle = dlopen(so_path, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        /* File exists but failed to load — that IS worth reporting. */
        fprintf(stderr, "modules: dlopen failed: %s\n", dlerror());
        return NULL;
    }

    CurryModuleInitFn init_fn = (CurryModuleInitFn)dlsym(handle, "curry_module_init");
    if (!init_fn) {
        fprintf(stderr, "modules: no curry_module_init in %s\n", so_path);
        dlclose(handle);
        return NULL;
    }

    val_t mod_env = env_new_root();
    CurryVM *vm = curry_vm_new(mod_env);
    init_fn(vm);

    return make_module(name, mod_env, V_NIL, false, handle);
}

/* ---- Load a Scheme .sld / .scm library file ---- */

static Module *load_scheme_module(val_t name, const char *path) {
    val_t mod_env = env_extend(GLOBAL_ENV);
    val_t port = port_open_file(path, PORT_INPUT);
    if (vis_false(port)) return NULL;

    /* A second, independent file-reading loop from scm_load's own (see
     * that function's header comment in runtime.c) -- (include ...)
     * declarations inside this file's own define-library body are
     * reached from here, so this needs the same mark/release around its
     * read/eval loop for those to resolve relative to *this* file's
     * directory rather than the process's cwd, and the same
     * exception-safe release-then-reraise on error (SCM_PROTECT) so a
     * module that fails to load partway through doesn't leave stale
     * directory-context entries behind to corrupt a later, unrelated
     * load if the error is caught and execution continues. */
    int mark = load_dir_mark();
    load_push_dir(path);
    val_t result = V_VOID;
    ExnHandler h;
    SCM_PROTECT(h, {
        val_t v;
        while (!vis_eof((v = scm_read(port)))) {
            result = eval(v, mod_env);
        }
    }, { load_dir_release(mark); port_close(port); scm_raise_val(h.exn); });
    load_dir_release(mark);
    port_close(port);
    (void)result;

    return make_module(name, mod_env, V_NIL, false, NULL);
}

/* Strip R6RS (for lib phase ...) wrapper from an import spec. */
static val_t strip_for(val_t spec) {
    if (vis_pair(spec) && vcar(spec) == S_FOR)
        return vcadr(spec);
    return spec;
}

/* ---- GC scanner for module registry ---- */

static void scan_module_registry(void) {
    /* Issue #143 (found the same way as sx_rules_gc_scan/
     * sx_algebra_gc_scan's identical fix in #141): under --gc
     * generational, a minor collection is per-thread, not a
     * stop-the-world pause for other actors, so this scanner can run
     * concurrently with module_registry_lock-protected access from
     * other threads and was mutating entries unguarded. Takes the same
     * write lock registry_insert uses. Safe against self-deadlock:
     * gc_ss_evac/gc_ss_fwd don't allocate, and registry_insert's own
     * allocation happens before it takes the lock (see its comment),
     * so no thread can already be holding this lock at the moment its
     * own allocation triggers the minor collection that runs this
     * scanner. */
    pthread_rwlock_wrlock(&module_registry_lock);
    for (ModuleEntry *e = registry; e; e = e->next) {
        e->name   = (val_t)gc_ss_evac((uintptr_t)e->name);
        e->module = (Module *)gc_ss_fwd(e->module);
    }
    pthread_rwlock_unlock(&module_registry_lock);
}

/* ---- Public API ---- */

void modules_init(void) {
    gc_ss_register_ext_scanner(scan_module_registry);
    /* Add search dir relative to executable (handles build-tree and install) */
#if defined(__linux__) || defined(__APPLE__)
    {
        char exe[PATH_MAX], dir[PATH_MAX];
        bool got_exe = false;

#  ifdef __linux__
        ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if (n > 0) { exe[n] = '\0'; got_exe = true; }
#  endif
#  ifdef __APPLE__
        uint32_t sz = sizeof(exe);
        if (_NSGetExecutablePath(exe, &sz) == 0) got_exe = true;
#  endif

        if (got_exe) {
            char *slash = strrchr(exe, '/');
            if (slash) {
                *slash = '\0';
                snprintf(dir, sizeof(dir), "%s/mods", exe);
                add_search_dir(dir);
                snprintf(dir, sizeof(dir), "%s/../lib/curry/modules", exe);
                add_search_dir(dir);
            }
        }
    }
#endif
    add_search_dir("lib/curry/modules");

    const char *env_path = getenv("CURRY_MODULE_PATH");
    if (env_path) {
        char *copy = strdup(env_path);
        char *tok  = strtok(copy, ":");
        while (tok) { add_search_dir(tok); tok = strtok(NULL, ":"); }
        /* Note: copy is not freed (intentional - small leak, lives forever) */
    }

    /* Register built-in (scheme base) */
    extern void builtins_register(val_t env);
    builtins_register(GLOBAL_ENV);

    /* Register all R7RS standard library names as aliases for the global env.
     * Everything lives in one flat namespace, so (scheme base), (scheme inexact),
     * (scheme write), etc. all expose the same bindings.
     *
     * NOT "case-lambda": per R7RS, (scheme case-lambda) is its own real
     * library, not part of the flat-namespace core (scheme base) exposes --
     * it was listed here for a while regardless, which meant
     * (import (scheme case-lambda)) silently SUCCEEDED while providing
     * nothing (GLOBAL_ENV has no case-lambda binding), failing only later
     * with a confusing unbound-variable error at the first actual use
     * site instead of a clean "library not found" at import time. Real
     * implementation lives at lib/curry/modules/scheme/case-lambda.sld,
     * resolved the normal on-disk way now that it's no longer intercepted
     * by this alias table first. */
    static const char *scheme_libs[] = {
        "base", "char", "complex", "cxr",
        "eval", "file", "inexact", "lazy", "load",
        "process-context", "read", "repl", "time", "write",
        NULL
    };
    val_t scheme_sym = sym_intern_cstr("scheme");
    for (int i = 0; scheme_libs[i]; i++) {
        val_t name = scm_cons(scheme_sym,
                        scm_cons(sym_intern_cstr(scheme_libs[i]), V_NIL));
        modules_register_builtin(name, GLOBAL_ENV);
    }

    /* Register R6RS (rnrs) and sub-libraries as aliases for the global env.
     * R6RS procedures are almost identical to R7RS; aliasing to global env
     * covers the vast majority of (rnrs) usage. */
    val_t rnrs_sym = sym_intern_cstr("rnrs");
    /* (rnrs) itself */
    modules_register_builtin(scm_cons(rnrs_sym, V_NIL), GLOBAL_ENV);
    /* Two-segment (rnrs X) names */
    static const char *rnrs2[] = {
        "base", "bytevectors", "conditions", "control", "eval",
        "exceptions", "files", "hashtables", "lists", "programs",
        "r5rs", "sorting", "unicode",
        NULL
    };
    for (int i = 0; rnrs2[i]; i++) {
        val_t name = scm_cons(rnrs_sym,
                        scm_cons(sym_intern_cstr(rnrs2[i]), V_NIL));
        modules_register_builtin(name, GLOBAL_ENV);
    }
    /* Three-segment (rnrs X Y) names */
    static const struct { const char *a; const char *b; } rnrs3[] = {
        {"arithmetic", "bitwise"}, {"arithmetic", "fixnums"}, {"arithmetic", "flonums"},
        {"io",         "ports"},   {"io",         "simple"},
        {"mutable",    "pairs"},   {"mutable",    "strings"},
        {"records",    "procedural"}, {"records", "syntactic"}, {"records", "inspection"},
        {NULL, NULL}
    };
    for (int i = 0; rnrs3[i].a; i++) {
        val_t name = scm_cons(rnrs_sym,
                        scm_cons(sym_intern_cstr(rnrs3[i].a),
                        scm_cons(sym_intern_cstr(rnrs3[i].b), V_NIL)));
        modules_register_builtin(name, GLOBAL_ENV);
    }
}

/* Shared by modules_load (raises on failure) and modules_available (used by
 * cond-expand's `(library <name>)` feature requirement, which needs a
 * side-effect-free-on-failure probe rather than a longjmp) -- same registry
 * lookup + on-disk search, just returns NULL instead of raising when nothing
 * is found. A successful probe does register the module, same as a real
 * import would; that's fine (idempotent, and "available" conventionally
 * means "importable right now" in other R7RS implementations too). */
static Module *modules_try_load(val_t name_list) {
    Module *mod = registry_lookup(name_list);
    if (mod) return mod;

    char path_base[256];
    name_to_path(name_list, path_base, sizeof(path_base));

    /* Try each search dir */
    for (int i = 0; i < num_search_dirs; i++) {
        char full[512];

        /* Try .so first */
        snprintf(full, sizeof(full), "%s/%s.so", search_dirs[i], path_base);
        mod = load_c_module(name_list, full);
        if (mod) { registry_insert(name_list, mod); return mod; }

        /* Try .dylib (macOS) */
        snprintf(full, sizeof(full), "%s/%s.dylib", search_dirs[i], path_base);
        mod = load_c_module(name_list, full);
        if (mod) { registry_insert(name_list, mod); return mod; }

        /* Try .sld (Scheme library definition) */
        snprintf(full, sizeof(full), "%s/%s.sld", search_dirs[i], path_base);
        mod = load_scheme_module(name_list, full);
        if (mod) {
            /* Prefer self-registration: if the file contained (library ...) or
             * (define-library ...), those forms already registered the module
             * under the correct env.  Don't overwrite with the file-level wrapper. */
            Module *self_reg = registry_lookup(name_list);
            if (self_reg) return self_reg;
            registry_insert(name_list, mod);
            return mod;
        }

        /* Try .scm */
        snprintf(full, sizeof(full), "%s/%s.scm", search_dirs[i], path_base);
        mod = load_scheme_module(name_list, full);
        if (mod) {
            Module *self_reg = registry_lookup(name_list);
            if (self_reg) return self_reg;
            registry_insert(name_list, mod);
            return mod;
        }
    }

    return NULL;
}

val_t modules_load(val_t name_list) {
    Module *mod = modules_try_load(name_list);
    if (mod) return vptr(mod);

    char path_base[256];
    name_to_path(name_list, path_base, sizeof(path_base));
    scm_raise(V_FALSE, "module not found: %s", path_base);
}

/* (library <name>) feature requirement inside cond-expand -- true iff
 * name_list would successfully modules_load() right now. */
bool modules_available(val_t name_list) {
    return modules_try_load(name_list) != NULL;
}

/* Apply an importer's filter (only/except/rename/prefix) to one binding and,
 * if it survives, define it in the importing env. orig_sym is the module's
 * own name for the binding (used for the Akkadian/cuneiform alias lookup
 * below, which keys off canonical names, not import-side renames/prefixes). */
static void import_binding(val_t orig_sym, val_t val, val_t spec, val_t filter, val_t env) {
    val_t sym = orig_sym;

    if (filter == S_ONLY) {
        /* (only name sym...) */
        val_t syms = vcddr(spec);
        bool found = false;
        while (vis_pair(syms)) { if (vcar(syms) == sym) { found = true; break; } syms = vcdr(syms); }
        if (!found) return;
    } else if (filter == S_EXCEPT) {
        /* (except name sym...) */
        val_t syms = vcddr(spec);
        bool found = false;
        while (vis_pair(syms)) { if (vcar(syms) == sym) { found = true; break; } syms = vcdr(syms); }
        if (found) return;
    } else if (filter == S_RENAME) {
        /* (rename name (old new)...) - translate sym */
        val_t renames = vcddr(spec);
        while (vis_pair(renames)) {
            val_t pair = vcar(renames);
            if (vcar(pair) == sym) { sym = vcadr(pair); break; }
            renames = vcdr(renames);
        }
    } else if (filter == S_PREFIX) {
        /* (prefix name pfx) */
        val_t pfx = vcaddr(spec);
        char buf[256];
        snprintf(buf, sizeof(buf), "%s%s", sym_cstr(pfx), sym_cstr(sym));
        sym = sym_intern_cstr(buf);
    }
    env_define(env, sym, val);

    /* Foreign-language aliases (Akkadian and any other registered
     * language pack — see lang_registry.h): builtins.c's own startup
     * loop only ever sees names already bound in the global env at that
     * point (see the comment on that loop) — it never sees names that
     * live only inside a module's own environment until something
     * imports them, which is now. Alias under the module's ORIGINAL
     * export name (orig_sym), not whatever a rename/prefix filter
     * turned the local binding into — the synonym is a property of
     * what the module exports, not of the importer's local nickname
     * for it. only/except still apply, since we've already
     * returned past excluded names above. */
    val_t forms[LANG_PR_MAX_FORMS];
    int nforms = lang_pr_lookup(orig_sym, forms, LANG_PR_MAX_FORMS);
    for (int f = 0; f < nforms; f++) env_define(env, forms[f], val);
}

/* Import a single spec into env.
 * Specs: (name) | (only name sym...) | (except name sym...) |
 *        (rename name (old new)...) | (prefix name pfx) */
val_t modules_import(val_t spec, val_t env) {
    val_t mod_val = V_FALSE;
    val_t name    = spec;
    val_t filter  = V_FALSE; /* symbol indicating filter type */

    if (vis_pair(spec) && (vcar(spec) == S_ONLY || vcar(spec) == S_EXCEPT ||
                           vcar(spec) == S_RENAME || vcar(spec) == S_PREFIX)) {
        filter = vcar(spec);
        name   = vcadr(spec);
    }

    /* (scheme base) and friends: use the global env */
    mod_val = modules_load(name);
    if (!vis_module(mod_val))
        scm_raise(V_FALSE, "import: module load failed");

    Module *mod = as_module(mod_val);

    if (mod->has_exports) {
        /* Explicit (export ...) clause: the export list is normally far
         * smaller than the module's full set of internal bindings, so look
         * each exported name up directly instead of scanning every binding
         * the module happens to define. */
        val_t mod_env_val = vptr(mod->env);
        for (val_t es = mod->exports; vis_pair(es); es = vcdr(es)) {
            val_t sym = vcar(es);
            val_t *slot = env_lookup_slot(mod_env_val, sym);
            if (!slot) continue; /* declared exported but never defined */
            import_binding(sym, *slot, spec, filter, env);
        }
    } else {
        /* No export list: every binding in the module's own environment is
         * importable (C modules, plain .scm files loaded without
         * define-library, and always-on builtin module aliases).
         *
         * Issue #148 (TSan-confirmed): mod->env is a root frame
         * (env_new_root(), same shape as GLOBAL_ENV), and root frames are
         * exactly the ones env.c's seqlock protocol exists for -- another
         * actor can still be executing this module's own top-level body
         * (defining bindings into mod->env one at a time via frame_define)
         * while this actor imports it, e.g. a define-library that
         * self-registers before its body finishes running, then a second
         * concurrent importer's self_reg re-check (modules_try_load) picks
         * up the partially-loaded module. Indexing f->syms[i]/f->vals[i]
         * directly here bypassed that protocol entirely -- a plain,
         * unsynchronized read racing frame_define's unsynchronized
         * f->syms/f->vals reallocation (frame_grow) and rehash. Uses
         * frame_snapshot_bindings (env.c) instead, the same seqlock-
         * validated copy frame_lookup_versioned already does for a single
         * symbol, just for the whole frame at once. */
        EnvFrame *f = mod->env;
        while (f) {
            val_t *syms, *vals;
            uint32_t n = frame_snapshot_bindings(f, &syms, &vals);
            for (uint32_t i = 0; i < n; i++)
                import_binding(syms[i], vals[i], spec, filter, env);
            f = f->parent;
        }
    }
    return mod_val;
}

void modules_register_builtin(val_t name_list, val_t mod_env) {
    registry_insert(name_list, make_module(name_list, mod_env, V_NIL, false, NULL));
}

/* Register a library whose export list has already been determined by an
 * (export ...) clause in define-library / library. exports is a list of
 * exported symbol names (the library's own binding names — R7RS/R6RS export
 * renaming is not supported). */
static void register_library(val_t name_list, val_t mod_env, val_t exports,
                              bool has_exports) {
    registry_insert(name_list, make_module(name_list, mod_env, exports, has_exports, NULL));
}

/* One define-library declaration: (export ...) | (import ...) | (begin ...)
 * | (include ...) | (cond-expand <ce-clause>...). Factored out of
 * modules_define_library so cond-expand can recurse into it -- a matched
 * cond-expand clause's body is itself a list of further declarations (this
 * is exactly how SRFI 279's own 279.sld picks its per-implementation
 * (import ...)/(include ...) pair), not expressions, so it can't just be
 * handed to eval() the way S_BEGIN's body is. */
static void define_library_clause(val_t clause, val_t *exports, bool *has_exports,
                                   val_t lib_env) {
    if (!vis_pair(clause))
        scm_raise(V_FALSE, "define-library: malformed declaration (not a list)");
    val_t clause_type = vcar(clause);

    if (clause_type == S_EXPORT) {
        *has_exports = true;
        val_t es = vcdr(clause);
        while (vis_pair(es)) { *exports = scm_cons(vcar(es), *exports); es = vcdr(es); }
    } else if (clause_type == S_IMPORT) {
        /* (import spec ...) — one or more import specs */
        val_t specs = vcdr(clause);
        while (vis_pair(specs)) {
            modules_import(vcar(specs), lib_env);
            specs = vcdr(specs);
        }
    } else if (clause_type == S_BEGIN) {
        val_t body = vcdr(clause);
        /* Compile+run against lib_env (an env_new_root() frame) instead of
         * tree-walking via eval() -- see chunk.h's Chunk::target_env
         * comment and the eval-elimination migration project memory. Each
         * form is compiled and run individually, same sequencing as the
         * eval() loop it replaces, so a later form still sees an earlier
         * form's own top-level defines. */
        while (vis_pair(body)) { vm_eval(vcar(body), lib_env); body = vcdr(body); }
    } else if (clause_type == S_INCLUDE) {
        val_t files = vcdr(clause);
        while (vis_pair(files)) {
            scm_load(str_data(as_str(vcar(files))), lib_env);
            files = vcdr(files);
        }
    } else if (clause_type == S_COND_EXPAND) {
        bool matched;
        val_t body = cond_expand_choose(vcdr(clause), &matched);
        if (!matched) scm_raise(V_FALSE, "cond-expand: no matching clause");
        while (vis_pair(body)) {
            define_library_clause(vcar(body), exports, has_exports, lib_env);
            body = vcdr(body);
        }
    }
}

/* (define-library (name) clause...) — R7RS */
val_t modules_define_library(val_t form, val_t env) {
    val_t rest = vcdr(form);
    /* A malformed `(define-library)` (no name at all) used to SIGSEGV
     * here on vcar(rest) -- same widespread bug class as compiler.c's
     * require_min_args, confirmed present on main too. */
    if (!vis_pair(rest))
        scm_raise(V_FALSE, "define-library: missing library name");
    val_t name = vcar(rest);   rest = vcdr(rest);
    val_t lib_env = env_new_root();
    val_t exports = V_NIL;
    bool  has_exports = false;

    while (vis_pair(rest)) {
        val_t clause = vcar(rest); rest = vcdr(rest);
        define_library_clause(clause, &exports, &has_exports, lib_env);
    }
    (void)env;
    register_library(name, lib_env, exports, has_exports);
    return V_VOID;
}

/* (library (name) clause...) — R6RS
 *
 * Differs from R7RS define-library in two ways:
 *   1. The keyword is 'library', not 'define-library'.
 *   2. Body forms are inline after export/import clauses, not wrapped in (begin ...).
 *   3. (import spec ...) may contain multiple specs in one clause.
 *   4. (for lib phase) wrappers on import specs are stripped (phase is ignored
 *      in an interpreter — compile-time vs run-time is not meaningful here).
 */
val_t modules_define_r6rs_library(val_t form, val_t env) {
    val_t rest = vcdr(form);
    if (!vis_pair(rest))
        scm_raise(V_FALSE, "library: missing library name");
    val_t name = vcar(rest);   rest = vcdr(rest);
    val_t lib_env = env_new_root();
    val_t exports = V_NIL;
    bool  has_exports = false;

    while (vis_pair(rest)) {
        val_t clause = vcar(rest); rest = vcdr(rest);
        if (!vis_pair(clause)) {
            vm_eval(clause, lib_env);
            continue;
        }
        val_t clause_type = vcar(clause);

        if (clause_type == S_EXPORT) {
            has_exports = true;
            val_t es = vcdr(clause);
            while (vis_pair(es)) { exports = scm_cons(vcar(es), exports); es = vcdr(es); }
        } else if (clause_type == S_IMPORT) {
            /* R6RS: (import import-spec ...) — multiple specs per clause */
            val_t specs = vcdr(clause);
            while (vis_pair(specs)) {
                modules_import(strip_for(vcar(specs)), lib_env);
                specs = vcdr(specs);
            }
        } else {
            /* Inline body form */
            vm_eval(clause, lib_env);
        }
    }
    (void)env;
    register_library(name, lib_env, exports, has_exports);
    return V_VOID;
}
