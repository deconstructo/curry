/*
 * General C FFI for Curry Scheme — libffi backend.
 *
 * Each (define-foreign ...) form creates a ForeignFn object (T_FOREIGN_FN)
 * containing a libffi ffi_cif and the resolved symbol address.  Calling the
 * Scheme procedure marshals args → C values, dispatches via ffi_call, then
 * unmarshals the return value.
 */

#include <ffi.h>      /* libffi — must come before curry's ffi.h */
#include "curry_ffi.h"
#include "gc.h"
#include "eval.h"
#include "env.h"
#include "symbol.h"
#include "object.h"
#include "value.h"
#include "numeric.h"
#include "builtins.h"
#include "matrix.h"

#include <ffi.h>
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

/* ---- Utilities ---- */

static inline const char *tag_str(val_t tag) {
    return vis_symbol(tag) ? sym_cstr(tag) : "?";
}

/* Normalise Scheme-style hyphenated type names to C-style underscored ones.
 * "size-t" → "size_t",  "int64-t" → "int64_t", etc.
 * Returns a pointer to a static buffer; not re-entrant. */
static const char *norm_tag(const char *s) {
    static char buf[32];
    size_t n = strlen(s);
    if (n >= sizeof(buf)) return s;
    for (size_t i = 0; i <= n; i++)
        buf[i] = (s[i] == '-') ? '_' : s[i];
    return buf;
}

/* ---- Type mapping: Scheme symbol → libffi type ---- */

static ffi_type *ffi_type_for_tag(val_t tag) {
    if (!vis_symbol(tag)) return NULL;
    const char *s = norm_tag(sym_cstr(tag));
    if (!strcmp(s,"void"))                              return &ffi_type_void;
    if (!strcmp(s,"int")   || !strcmp(s,"int32")   ||
        !strcmp(s,"int32_t") || !strcmp(s,"bool"))      return &ffi_type_sint32;
    if (!strcmp(s,"uint")  || !strcmp(s,"uint32")  ||
        !strcmp(s,"uint32_t"))                          return &ffi_type_uint32;
    if (!strcmp(s,"long")  || !strcmp(s,"int64")   ||
        !strcmp(s,"int64_t")|| !strcmp(s,"intptr") ||
        !strcmp(s,"ssize_t"))                           return &ffi_type_sint64;
    if (!strcmp(s,"ulong") || !strcmp(s,"uint64")  ||
        !strcmp(s,"uint64_t")|| !strcmp(s,"size_t")||
        !strcmp(s,"uintptr"))                           return &ffi_type_uint64;
    if (!strcmp(s,"float"))                             return &ffi_type_float;
    if (!strcmp(s,"double"))                            return &ffi_type_double;
    if (!strcmp(s,"c-ptr") || !strcmp(s,"pointer") ||
        !strcmp(s,"void*") || !strcmp(s,"string")  ||
        !strcmp(s,"c-string"))                          return &ffi_type_pointer;
    return NULL;
}

/* ---- Marshal Scheme value → C storage buffer ---- */

static bool marshal_arg(val_t v, val_t tag, void *buf) {
    const char *t = norm_tag(tag_str(tag));
    if (!strcmp(t,"int") || !strcmp(t,"int32") || !strcmp(t,"int32_t") || !strcmp(t,"bool")) {
        int32_t n = (int32_t)(vis_fixnum(v) ? vunfix(v)
                            : vis_true(v)   ? 1 : 0);
        memcpy(buf, &n, sizeof(n)); return true;
    }
    if (!strcmp(t,"uint") || !strcmp(t,"uint32") || !strcmp(t,"uint32_t")) {
        uint32_t n = (uint32_t)(vis_fixnum(v) ? (uintptr_t)vunfix(v) : 0);
        memcpy(buf, &n, sizeof(n)); return true;
    }
    if (!strcmp(t,"long") || !strcmp(t,"int64") || !strcmp(t,"int64_t") ||
        !strcmp(t,"intptr") || !strcmp(t,"ssize_t")) {
        int64_t n = vis_fixnum(v) ? (int64_t)vunfix(v) : 0;
        memcpy(buf, &n, sizeof(n)); return true;
    }
    if (!strcmp(t,"ulong") || !strcmp(t,"uint64") || !strcmp(t,"uint64_t") ||
        !strcmp(t,"size_t") || !strcmp(t,"uintptr")) {
        uint64_t n = vis_fixnum(v) ? (uint64_t)(uintptr_t)vunfix(v) : 0;
        memcpy(buf, &n, sizeof(n)); return true;
    }
    if (!strcmp(t,"double")) {
        double d = num_to_double(v);
        memcpy(buf, &d, sizeof(d)); return true;
    }
    if (!strcmp(t,"float")) {
        float f = (float)num_to_double(v);
        memcpy(buf, &f, sizeof(f)); return true;
    }
    if (!strcmp(t,"c-ptr") || !strcmp(t,"pointer") || !strcmp(t,"void*")) {
        void *p = vis_cptr(v)   ? as_cptr(v)->ptr
                : vis_false(v)  ? NULL
                : vis_fixnum(v) ? (void *)(uintptr_t)vunfix(v)
                : NULL;
        memcpy(buf, &p, sizeof(p)); return true;
    }
    if (!strcmp(t,"string") || !strcmp(t,"c-string")) {
        const char *s = vis_string(v)  ? str_data(as_str(v))
                      : vis_false(v)   ? NULL
                      : NULL;
        memcpy(buf, &s, sizeof(s)); return true;
    }
    return false;
}

/* ---- Unmarshal C return value → Scheme ---- */

static val_t unmarshal_ret(void *buf, val_t tag) {
    const char *t = norm_tag(tag_str(tag));
    if (!strcmp(t,"void"))   return V_VOID;
    if (!strcmp(t,"int") || !strcmp(t,"int32") || !strcmp(t,"int32_t") || !strcmp(t,"bool")) {
        int32_t n; memcpy(&n, buf, sizeof(n));
        return !strcmp(t,"bool") ? vbool(n != 0) : vfix((intptr_t)n);
    }
    if (!strcmp(t,"uint") || !strcmp(t,"uint32") || !strcmp(t,"uint32_t")) {
        uint32_t n; memcpy(&n, buf, sizeof(n)); return vfix((intptr_t)(int32_t)n);
    }
    if (!strcmp(t,"long") || !strcmp(t,"int64") || !strcmp(t,"int64_t") ||
        !strcmp(t,"intptr") || !strcmp(t,"ssize_t")) {
        int64_t n; memcpy(&n, buf, sizeof(n)); return vfix((intptr_t)n);
    }
    if (!strcmp(t,"ulong") || !strcmp(t,"uint64") || !strcmp(t,"uint64_t") ||
        !strcmp(t,"size_t") || !strcmp(t,"uintptr")) {
        uint64_t n; memcpy(&n, buf, sizeof(n)); return vfix((intptr_t)(int64_t)n);
    }
    if (!strcmp(t,"double")) {
        double d; memcpy(&d, buf, sizeof(d)); return num_make_float(d);
    }
    if (!strcmp(t,"float")) {
        float f; memcpy(&f, buf, sizeof(f)); return num_make_float((double)f);
    }
    if (!strcmp(t,"c-ptr") || !strcmp(t,"pointer") || !strcmp(t,"void*")) {
        void *p; memcpy(&p, buf, sizeof(p)); return ffi_make_cptr(p);
    }
    if (!strcmp(t,"string") || !strcmp(t,"c-string")) {
        char *s; memcpy(&s, buf, sizeof(s));
        if (!s) return V_FALSE;
        uint32_t len = (uint32_t)strlen(s);
        String *str = (String *)gc_alloc_atomic(sizeof(String) + len + 1);
        str->hdr.type = T_STRING; str->hdr.flags = 0;
        str->len = len; str->hash = 0; str->orig_cap = len; str->ext = NULL;
        memcpy(str->data, s, len + 1);
        return vptr(str);
    }
    return V_VOID;
}

/* ---- Public API ---- */

void ffi_init(void) {
    /* Nothing needed at startup. */
}

val_t ffi_make_cptr(void *ptr) {
    CPtr *c = CURRY_NEW(CPtr);
    c->hdr.type = T_CPTR; c->hdr.flags = 0;
    c->ptr = ptr;
    return vptr(c);
}

val_t ffi_load_library(const char *path) {
    void *h = dlopen(path, RTLD_LAZY | RTLD_GLOBAL);
    if (!h) scm_raise(V_FALSE, "foreign-load-library: cannot open '%s': %s",
                      path, dlerror());
    ForeignLib *lib = CURRY_NEW_PINNED(ForeignLib);
    lib->hdr.type  = T_FOREIGN_LIB;
    lib->hdr.flags = 0;
    lib->handle    = h;
    lib->path      = V_FALSE; /* set from Scheme after allocation */
    return vptr(lib);
}

val_t ffi_make_fn(val_t lib_val, const char *c_name, val_t ret_tag, val_t arg_tags) {
    if (!vis_foreignlib(lib_val))
        scm_raise(V_FALSE, "ffi-make-fn: not a foreign-lib");

    void *fn = dlsym(as_foreignlib(lib_val)->handle, c_name);
    if (!fn) scm_raise(V_FALSE, "ffi-make-fn: symbol not found: %s", c_name);

    int nargs = scm_list_length(arg_tags);
    if (nargs < 0) scm_raise(V_FALSE, "ffi-make-fn: arg-types must be a proper list");

    /* Build ffi_type** array — malloc'd, permanent */
    ffi_type **atypes = malloc((nargs ? (size_t)nargs : 1) * sizeof(ffi_type *));
    val_t tlist = arg_tags;
    for (int i = 0; i < nargs; i++) {
        ffi_type *ft = ffi_type_for_tag(vcar(tlist));
        if (!ft) scm_raise(V_FALSE, "ffi-make-fn: unknown arg type '%s' for %s",
                           tag_str(vcar(tlist)), c_name);
        atypes[i] = ft;
        tlist = vcdr(tlist);
    }

    ffi_type *rtype = ffi_type_for_tag(ret_tag);
    if (!rtype) scm_raise(V_FALSE, "ffi-make-fn: unknown return type '%s' for %s",
                          tag_str(ret_tag), c_name);

    ffi_cif *cif = malloc(sizeof(ffi_cif));
    if (ffi_prep_cif(cif, FFI_DEFAULT_ABI, (unsigned)nargs, rtype,
                     nargs ? atypes : NULL) != FFI_OK)
        scm_raise(V_FALSE, "ffi-make-fn: ffi_prep_cif failed for %s", c_name);

    ForeignFn *ff = CURRY_NEW_PINNED(ForeignFn);
    ff->hdr.type   = T_FOREIGN_FN;
    ff->hdr.flags  = 0;
    ff->fn         = fn;
    ff->cif        = cif;
    ff->cif_atypes = atypes;
    ff->arg_tags   = arg_tags;
    ff->ret_tag    = ret_tag;
    ff->nargs      = nargs;
    ff->name       = strdup(c_name);
    return vptr(ff);
}

#define FFI_MAX_ARGS 64
#define FFI_ARG_BUF  16   /* max sizeof any supported C type */

val_t ffi_call_fn(val_t ff_val, val_t args) {
    if (!vis_foreignfn(ff_val))
        scm_raise(V_FALSE, "ffi-call: not a foreign-fn");
    ForeignFn *ff = as_foreignfn(ff_val);

    int nargs = ff->nargs;
    int got   = scm_list_length(args);
    if (got != nargs)
        scm_raise(V_FALSE, "ffi-call: %s expects %d arg%s, got %d",
                  ff->name, nargs, nargs == 1 ? "" : "s", got);

    uint8_t  storage[FFI_MAX_ARGS * FFI_ARG_BUF];
    void    *ptrs[FFI_MAX_ARGS];
    val_t tag_list = ff->arg_tags;
    val_t arg_list = args;
    for (int i = 0; i < nargs; i++) {
        ptrs[i] = storage + i * FFI_ARG_BUF;
        if (!marshal_arg(vcar(arg_list), vcar(tag_list), ptrs[i]))
            scm_raise(V_FALSE, "ffi-call: cannot marshal arg %d (type '%s') for %s",
                      i + 1, tag_str(vcar(tag_list)), ff->name);
        arg_list = vcdr(arg_list);
        tag_list = vcdr(tag_list);
    }

    uint8_t ret_buf[FFI_ARG_BUF] = {0};
    /* POSIX guarantees dlsym void* → function pointer conversion.
     * Cast through union to suppress -Wpedantic. */
    void (*fn_ptr)(void);
    memcpy(&fn_ptr, &ff->fn, sizeof(fn_ptr));
    ffi_call((ffi_cif *)ff->cif, fn_ptr, ret_buf, nargs ? ptrs : NULL);
    return unmarshal_ret(ret_buf, ff->ret_tag);
}

/* ---- Scheme primitives ---- */

static void ffi_def(val_t env, const char *name,
                    val_t (*fn)(int, val_t *, void *), int mn, int mx) {
    Primitive *p = (Primitive *)gc_alloc_pinned(sizeof(Primitive));
    p->hdr.type = T_PRIMITIVE; p->hdr.flags = 0;
    p->name = name; p->fn = fn; p->min_args = mn; p->max_args = mx; p->ud = NULL;
    env_define(env, sym_intern_cstr(name), vptr(p));
}

static val_t prim_ffi_load(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_string(av[0])) scm_raise(V_FALSE, "%ffi-load: path must be a string");
    val_t lib = ffi_load_library(str_data(as_str(av[0])));
    as_foreignlib(lib)->path = av[0];
    return lib;
}
static val_t prim_ffi_make_fn(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_foreignlib(av[0])) scm_raise(V_FALSE, "%ffi-make-fn: not a foreign-lib");
    if (!vis_string(av[1]))     scm_raise(V_FALSE, "%ffi-make-fn: c-name must be a string");
    if (!vis_symbol(av[2]))     scm_raise(V_FALSE, "%ffi-make-fn: ret-type must be a symbol");
    return ffi_make_fn(av[0], str_data(as_str(av[1])), av[2], av[3]);
}
static val_t prim_ffi_call(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return ffi_call_fn(av[0], av[1]); }
static val_t prim_make_cptr(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    void *p = vis_fixnum(av[0]) ? (void *)(uintptr_t)vunfix(av[0]) : NULL;
    return ffi_make_cptr(p);
}
static val_t prim_cptr_address(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_cptr(av[0])) scm_raise(V_FALSE, "%ffi-cptr-address: not a c-ptr");
    return vfix((intptr_t)(uintptr_t)as_cptr(av[0])->ptr);
}
static val_t prim_ffi_matrix_ptr(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_matrix(av[0])) scm_raise(V_FALSE, "%ffi-matrix-ptr: not a matrix");
    gc_pin(as_matrix(av[0]));   /* no-op under Boehm; protocol for moving GC */
    return ffi_make_cptr(as_matrix(av[0])->data);
}
static val_t prim_ffi_matrix_unpin(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (vis_matrix(av[0])) gc_unpin(as_matrix(av[0]));
    return V_VOID;
}
static val_t prim_ffi_tensor_ptr(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_tensor(av[0])) scm_raise(V_FALSE, "%ffi-tensor-ptr: not a tensor");
    gc_pin(as_tensor(av[0]));
    /* tensor_data() is static inline in matrix.c; use the layout directly:
     * dims[ndim] uint32_t elements followed immediately by the double array. */
    Tensor *t = as_tensor(av[0]);
    double *data = (double *)(t->dims + t->ndim);
    return ffi_make_cptr(data);
}
static val_t prim_ffi_tensor_unpin(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (vis_tensor(av[0])) gc_unpin(as_tensor(av[0]));
    return V_VOID;
}
static val_t prim_cptr_p(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return vbool(vis_cptr(av[0])); }
static val_t prim_foreignlib_p(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return vbool(vis_foreignlib(av[0])); }
static val_t prim_foreignfn_p(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return vbool(vis_foreignfn(av[0])); }
static val_t prim_foreignlib_path(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_foreignlib(av[0])) scm_raise(V_FALSE, "foreign-lib-path: not a foreign-lib");
    return as_foreignlib(av[0])->path;
}

void ffi_register_builtins(val_t env) {
    ffi_def(env, "%ffi-load",           prim_ffi_load,          1, 1);
    ffi_def(env, "%ffi-make-fn",        prim_ffi_make_fn,       4, 4);
    ffi_def(env, "%ffi-call",           prim_ffi_call,          2, 2);
    ffi_def(env, "%ffi-make-cptr",      prim_make_cptr,         1, 1);
    ffi_def(env, "%ffi-cptr-address",   prim_cptr_address,      1, 1);
    ffi_def(env, "%ffi-matrix-ptr",     prim_ffi_matrix_ptr,    1, 1);
    ffi_def(env, "%ffi-matrix-unpin",   prim_ffi_matrix_unpin,  1, 1);
    ffi_def(env, "%ffi-tensor-ptr",     prim_ffi_tensor_ptr,    1, 1);
    ffi_def(env, "%ffi-tensor-unpin",   prim_ffi_tensor_unpin,  1, 1);
    ffi_def(env, "c-ptr?",              prim_cptr_p,            1, 1);
    ffi_def(env, "foreign-lib?",        prim_foreignlib_p,      1, 1);
    ffi_def(env, "foreign-fn?",         prim_foreignfn_p,       1, 1);
    ffi_def(env, "foreign-lib-path",    prim_foreignlib_path,   1, 1);
}
