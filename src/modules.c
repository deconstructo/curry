#include "modules.h"
#include "object.h"
#include "eval.h"
#include "env.h"
#include "symbol.h"
#include "builtins.h"
#include "gc.h"
#include "reader.h"
#include "port.h"
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
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

static bool names_equal(val_t a, val_t b) {
    while (vis_pair(a) && vis_pair(b)) {
        if (vcar(a) != vcar(b)) return false;
        a = vcdr(a); b = vcdr(b);
    }
    return vis_nil(a) && vis_nil(b);
}

static Module *registry_lookup(val_t name) {
    for (ModuleEntry *e = registry; e; e = e->next)
        if (names_equal(e->name, name)) return e->module;
    return NULL;
}

static void registry_insert(val_t name, Module *mod) {
    ModuleEntry *e = CURRY_NEW(ModuleEntry);
    e->name   = name;
    e->module = mod;
    e->next   = registry;
    registry  = e;
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
        const char *seg = sym_cstr(vcar(name));
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
    CurryVM *vm = CURRY_NEW_ATOM(CurryVM);
    vm->env = module_env;
    return vm;
}

val_t curry_vm_env(CurryVM *vm) { return vm->env; }

void curry_define_fn(CurryVM *vm, const char *name, CurryFn fn,
                     int min_args, int max_args, void *ud) {
    Primitive *p = CURRY_NEW(Primitive);
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

/* ---- Load a C extension .so ---- */

static Module *load_c_module(val_t name, const char *so_path) {
    void *handle = dlopen(so_path, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
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

    Module *mod = CURRY_NEW(Module);
    mod->hdr.type  = T_MODULE; mod->hdr.flags = 0;
    mod->name      = name;
    mod->env       = as_env(mod_env);
    mod->exports   = V_NIL;  /* export everything for now */
    mod->dl_handle = handle;
    return mod;
}

/* ---- Load a Scheme .sld / .scm library file ---- */

static Module *load_scheme_module(val_t name, const char *path) {
    val_t mod_env = env_extend(GLOBAL_ENV);
    val_t port = port_open_file(path, PORT_INPUT);
    if (vis_false(port)) return NULL;

    val_t result = V_VOID;
    val_t v;
    while (!vis_eof((v = scm_read(port)))) {
        result = eval(v, mod_env);
    }
    port_close(port);
    (void)result;

    Module *mod = CURRY_NEW(Module);
    mod->hdr.type  = T_MODULE; mod->hdr.flags = 0;
    mod->name      = name;
    mod->env       = as_env(mod_env);
    mod->exports   = V_NIL;
    mod->dl_handle = NULL;
    return mod;
}

/* ---- Public API ---- */

void modules_init(void) {
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
     * (scheme write), etc. all expose the same bindings. */
    static const char *scheme_libs[] = {
        "base", "case-lambda", "char", "complex", "cxr",
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
}

val_t modules_load(val_t name_list) {
    Module *mod = registry_lookup(name_list);
    if (mod) return vptr(mod);

    char path_base[256];
    name_to_path(name_list, path_base, sizeof(path_base));

    /* Try each search dir */
    for (int i = 0; i < num_search_dirs; i++) {
        char full[512];

        /* Try .so first */
        snprintf(full, sizeof(full), "%s/%s.so", search_dirs[i], path_base);
        mod = load_c_module(name_list, full);
        if (mod) { registry_insert(name_list, mod); return vptr(mod); }

        /* Try .dylib (macOS) */
        snprintf(full, sizeof(full), "%s/%s.dylib", search_dirs[i], path_base);
        mod = load_c_module(name_list, full);
        if (mod) { registry_insert(name_list, mod); return vptr(mod); }

        /* Try .sld (Scheme library definition) */
        snprintf(full, sizeof(full), "%s/%s.sld", search_dirs[i], path_base);
        mod = load_scheme_module(name_list, full);
        if (mod) { registry_insert(name_list, mod); return vptr(mod); }

        /* Try .scm */
        snprintf(full, sizeof(full), "%s/%s.scm", search_dirs[i], path_base);
        mod = load_scheme_module(name_list, full);
        if (mod) { registry_insert(name_list, mod); return vptr(mod); }
    }

    scm_raise(V_FALSE, "module not found: %s", path_base);
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
    val_t mod_env_val = vptr(mod->env);

    /* Determine which names to import */
    EnvFrame *f = mod->env->frame;
    while (f) {
        for (uint32_t i = 0; i < f->size; i++) {
            val_t sym = f->syms[i];
            val_t val = f->vals[i];

            if (filter == S_ONLY) {
                /* (only name sym...) */
                val_t syms = vcddr(spec);
                bool found = false;
                while (vis_pair(syms)) { if (vcar(syms) == sym) { found = true; break; } syms = vcdr(syms); }
                if (!found) continue;
            } else if (filter == S_EXCEPT) {
                /* (except name sym...) */
                val_t syms = vcddr(spec);
                bool found = false;
                while (vis_pair(syms)) { if (vcar(syms) == sym) { found = true; break; } syms = vcdr(syms); }
                if (found) continue;
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
        }
        f = f->parent;
    }
    (void)mod_env_val;
    return mod_val;
}

void modules_register_builtin(val_t name_list, val_t mod_env) {
    Module *mod = CURRY_NEW(Module);
    mod->hdr.type  = T_MODULE; mod->hdr.flags = 0;
    mod->name      = name_list;
    mod->env       = as_env(mod_env);
    mod->exports   = V_NIL;
    mod->dl_handle = NULL;
    registry_insert(name_list, mod);
}

/* (define-library (name) export-spec... body...) */
val_t modules_define_library(val_t form, val_t env) {
    val_t rest    = vcdr(form);
    val_t name    = vcar(rest);   rest = vcdr(rest);
    val_t lib_env = env_new_root();
    val_t exports = V_NIL;

    while (vis_pair(rest)) {
        val_t clause = vcar(rest); rest = vcdr(rest);
        val_t clause_type = vcar(clause);

        if (clause_type == S_EXPORT) {
            val_t es = vcdr(clause);
            while (vis_pair(es)) { exports = scm_cons(vcar(es), exports); es = vcdr(es); }
            (void)exports; /* will be used when importing */
        } else if (clause_type == S_IMPORT) {
            modules_import(vcadr(clause), lib_env);
        } else if (clause_type == sym_intern_cstr("begin")) {
            val_t body = vcdr(clause);
            while (vis_pair(body)) { eval(vcar(body), lib_env); body = vcdr(body); }
        } else if (clause_type == S_INCLUDE) {
            val_t files = vcdr(clause);
            while (vis_pair(files)) {
                scm_load(as_str(vcar(files))->data, lib_env);
                files = vcdr(files);
            }
        }
    }
    (void)env;
    modules_register_builtin(name, lib_env);
    return V_VOID;
}
