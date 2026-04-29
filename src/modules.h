#ifndef CURRY_MODULES_H
#define CURRY_MODULES_H

/*
 * Module / library system for Curry Scheme.
 *
 * Implements R7RS define-library / import.
 * Also supports loading compiled C extension modules as shared libraries.
 *
 * Module names are lists of symbols, e.g. (curry json) or (scheme base).
 *
 * C extension API:
 *   A .so module exports a function:
 *     void curry_module_init(CurryVM *vm);
 *   which calls curry_define_fn() / curry_define_val() to register bindings.
 *
 *   The module's Scheme name is declared via:
 *     CURRY_MODULE("curry" "json")
 *
 * Module search path:
 *   1. Built-in modules (scheme base, scheme write, ...)
 *   2. CURRY_MODULE_PATH environment variable (colon-separated)
 *   3. lib/ directory relative to the executable
 */

#include "value.h"
#include <stdbool.h>

/* Initialize the module registry */
void modules_init(void);

/* Load and return a module by name list (e.g. (curry json)) */
val_t modules_load(val_t name_list);

/* Import a module spec into env */
val_t modules_import(val_t spec, val_t env);

/* Register a built-in module */
void modules_register_builtin(val_t name_list, val_t env);

/* Define-library: install a library form */
val_t modules_define_library(val_t form, val_t env);

/* ---- C extension API ---- */

struct CurryVM;
typedef struct CurryVM CurryVM;

typedef val_t (*CurryFn)(int argc, val_t *argv, void *ud);

typedef struct {
    const char *name;
    CurryFn     fn;
    int         min_args;
    int         max_args;  /* -1 = variadic */
    void       *ud;
} CurryFnDef;

/* Call from curry_module_init() to register procedures */
void curry_define_fn(CurryVM *vm, const char *name, CurryFn fn,
                     int min_args, int max_args, void *ud);
void curry_define_val(CurryVM *vm, const char *name, val_t value);

/* CurryVM handle (passed to curry_module_init) */
CurryVM *curry_vm_new(val_t module_env);
val_t    curry_vm_env(CurryVM *vm);

/* Module init function type */
typedef void (*CurryModuleInitFn)(CurryVM *vm);

#endif /* CURRY_MODULES_H */
