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
#include "vm.h"
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

/* Shared bound on total argument count and per-arg storage size, used both
 * at define-foreign time (reject an over-wide signature before it can
 * reach a call) and at call time (size the marshaling stack buffers in
 * ffi_call_fn/ffi_call_fn_variadic). Defined here, ahead of every
 * function that needs it, rather than down by ffi_call_fn where it used
 * to live -- ffi_make_fn's own arg-count check (added below) needs it
 * too, and a check that runs after the fact doesn't prevent anything. */
#define FFI_MAX_ARGS 64
#define FFI_ARG_BUF  16   /* max sizeof any supported C type */

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
    if (!strcmp(s,"c_ptr") || !strcmp(s,"pointer") ||
        !strcmp(s,"void*") || !strcmp(s,"string")  ||
        !strcmp(s,"c_string"))                          return &ffi_type_pointer;
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
    if (!strcmp(t,"c_ptr") || !strcmp(t,"pointer") || !strcmp(t,"void*")) {
        void *p = vis_cptr(v)   ? as_cptr(v)->ptr
                : vis_false(v)  ? NULL
                : vis_fixnum(v) ? (void *)(uintptr_t)vunfix(v)
                : NULL;
        memcpy(buf, &p, sizeof(p)); return true;
    }
    if (!strcmp(t,"string") || !strcmp(t,"c_string")) {
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
    if (!strcmp(t,"c_ptr") || !strcmp(t,"pointer") || !strcmp(t,"void*")) {
        void *p; memcpy(&p, buf, sizeof(p)); return ffi_make_cptr(p);
    }
    if (!strcmp(t,"string") || !strcmp(t,"c_string")) {
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
    if (nargs > FFI_MAX_ARGS)
        scm_raise(V_FALSE, "ffi-make-fn: %s: %d args exceeds max %d",
                  c_name, nargs, FFI_MAX_ARGS);

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
    ff->variadic   = false;
    ff->name       = strdup(c_name);
    return vptr(ff);
}

/* ---- Variadic foreign functions ----
 *
 * C variadic calls (printf-shaped) have no fixed signature for the
 * trailing arguments, so unlike ffi_make_fn's cif (built once, reused
 * forever), a variadic call's cif has to be rebuilt per call from that
 * call's actual argument types -- libffi's own ffi_prep_cif_var contract:
 * a cif prepared for one (nfixed, ntotal, atypes) shape is only valid for
 * calls matching that exact shape. See ffi_call_fn_variadic below. */

val_t ffi_make_fn_variadic(val_t lib_val, const char *c_name, val_t ret_tag, val_t fixed_arg_tags) {
    if (!vis_foreignlib(lib_val))
        scm_raise(V_FALSE, "ffi-make-fn-variadic: not a foreign-lib");

    void *fn = dlsym(as_foreignlib(lib_val)->handle, c_name);
    if (!fn) scm_raise(V_FALSE, "ffi-make-fn-variadic: symbol not found: %s", c_name);

    int nfixed = scm_list_length(fixed_arg_tags);
    if (nfixed < 0) scm_raise(V_FALSE, "ffi-make-fn-variadic: fixed-arg-types must be a proper list");
    if (nfixed > FFI_MAX_ARGS)
        scm_raise(V_FALSE, "ffi-make-fn-variadic: %s: %d fixed args exceeds max %d",
                  c_name, nfixed, FFI_MAX_ARGS);

    ffi_type **atypes = malloc((nfixed ? (size_t)nfixed : 1) * sizeof(ffi_type *));
    val_t tlist = fixed_arg_tags;
    for (int i = 0; i < nfixed; i++) {
        ffi_type *ft = ffi_type_for_tag(vcar(tlist));
        if (!ft) scm_raise(V_FALSE, "ffi-make-fn-variadic: unknown arg type '%s' for %s",
                           tag_str(vcar(tlist)), c_name);
        atypes[i] = ft;
        tlist = vcdr(tlist);
    }

    ffi_type *rtype = ffi_type_for_tag(ret_tag);
    if (!rtype) scm_raise(V_FALSE, "ffi-make-fn-variadic: unknown return type '%s' for %s",
                          tag_str(ret_tag), c_name);

    /* A cif for the zero-variadic-args call shape (nfixed == ntotal) is
     * valid per ffi_prep_cif_var's own docs and lets a call with no
     * trailing arguments skip rebuilding one; a call that does supply
     * variadic arguments always builds its own instead (see below). */
    ffi_cif *cif = malloc(sizeof(ffi_cif));
    if (ffi_prep_cif_var(cif, FFI_DEFAULT_ABI, (unsigned)nfixed, (unsigned)nfixed,
                         rtype, nfixed ? atypes : NULL) != FFI_OK)
        scm_raise(V_FALSE, "ffi-make-fn-variadic: ffi_prep_cif_var failed for %s", c_name);

    ForeignFn *ff = CURRY_NEW_PINNED(ForeignFn);
    ff->hdr.type   = T_FOREIGN_FN;
    ff->hdr.flags  = 0;
    ff->fn         = fn;
    ff->cif        = cif;
    ff->cif_atypes = atypes;
    ff->arg_tags   = fixed_arg_tags;
    ff->ret_tag    = ret_tag;
    ff->nargs      = nfixed;
    ff->variadic   = true;
    ff->name       = strdup(c_name);
    return vptr(ff);
}

/* Marshal one variadic-tail argument, applying C's default argument
 * promotion for float -> double (libffi requires the promoted type here;
 * passing ffi_type_float for a variadic slot is documented undefined
 * behavior in ffi_prep_cif_var, since the callee's va_arg(ap, double)
 * expects the promoted width). curry's other supported scalar types
 * (int32/uint32/int64/uint64/pointer/string) are already at or above
 * their C default-promoted width, so no other promotion is needed. */
static bool marshal_variadic_arg(val_t v, val_t tag, void *buf, ffi_type **out_type) {
    const char *t = norm_tag(tag_str(tag));
    if (!strcmp(t, "float")) {
        double d = num_to_double(v);
        memcpy(buf, &d, sizeof(d));
        *out_type = &ffi_type_double;
        return true;
    }
    ffi_type *ft = ffi_type_for_tag(tag);
    if (!ft) return false;
    *out_type = ft;
    return marshal_arg(v, tag, buf);
}

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

/* fixed_args: a proper list matching ff's fixed prefix exactly, same as
 * ffi_call_fn. variadic_typed_args: a proper list of (type-symbol . value)
 * pairs for the trailing C variadic arguments -- there's no static
 * signature to read these types from, so the call site has to say what
 * each one is (see marshal_variadic_arg's float->double promotion note
 * above). Every call rebuilds its own cif: libffi requires a variadic
 * cif's (nfixed, ntotal, atypes) to match the actual call being made, and
 * that shape can differ from one call to the next (e.g. a format string
 * with a different number/type of conversions each time). */
val_t ffi_call_fn_variadic(val_t ff_val, val_t fixed_args, val_t variadic_typed_args) {
    if (!vis_foreignfn(ff_val))
        scm_raise(V_FALSE, "ffi-call-variadic: not a foreign-fn");
    ForeignFn *ff = as_foreignfn(ff_val);
    if (!ff->variadic)
        scm_raise(V_FALSE, "ffi-call-variadic: %s was not declared variadic "
                  "(define-foreign it with #:variadic)", ff->name);

    int nfixed = ff->nargs;
    int got_fixed = scm_list_length(fixed_args);
    if (got_fixed != nfixed)
        scm_raise(V_FALSE, "ffi-call-variadic: %s expects %d fixed arg%s, got %d",
                  ff->name, nfixed, nfixed == 1 ? "" : "s", got_fixed);

    int nvar = scm_list_length(variadic_typed_args);
    if (nvar < 0)
        scm_raise(V_FALSE, "ffi-call-variadic: variadic-typed-args must be a proper list");

    int total = nfixed + nvar;
    if (total > FFI_MAX_ARGS)
        scm_raise(V_FALSE, "ffi-call-variadic: %s: %d total args exceeds max %d",
                  ff->name, total, FFI_MAX_ARGS);

    uint8_t   storage[FFI_MAX_ARGS * FFI_ARG_BUF];
    void     *ptrs[FFI_MAX_ARGS];
    ffi_type *atypes[FFI_MAX_ARGS];

    ffi_type **fixed_atypes = (ffi_type **)ff->cif_atypes;
    val_t tag_list = ff->arg_tags;
    val_t arg_list = fixed_args;
    for (int i = 0; i < nfixed; i++) {
        ptrs[i]   = storage + i * FFI_ARG_BUF;
        atypes[i] = fixed_atypes[i];
        if (!marshal_arg(vcar(arg_list), vcar(tag_list), ptrs[i]))
            scm_raise(V_FALSE, "ffi-call-variadic: cannot marshal fixed arg %d (type '%s') for %s",
                      i + 1, tag_str(vcar(tag_list)), ff->name);
        arg_list = vcdr(arg_list);
        tag_list = vcdr(tag_list);
    }

    val_t var_list = variadic_typed_args;
    for (int i = 0; i < nvar; i++) {
        val_t pair = vcar(var_list);
        if (!vis_pair(pair))
            scm_raise(V_FALSE, "ffi-call-variadic: variadic arg %d for %s must be a "
                      "(type . value) pair", i + 1, ff->name);
        val_t tag = vcar(pair);
        val_t val = vcdr(pair);
        int idx = nfixed + i;
        ptrs[idx] = storage + idx * FFI_ARG_BUF;
        if (!marshal_variadic_arg(val, tag, ptrs[idx], &atypes[idx]))
            scm_raise(V_FALSE, "ffi-call-variadic: cannot marshal variadic arg %d (type '%s') for %s",
                      i + 1, tag_str(tag), ff->name);
        var_list = vcdr(var_list);
    }

    ffi_type *rtype = ffi_type_for_tag(ff->ret_tag);
    if (!rtype) scm_raise(V_FALSE, "ffi-call-variadic: unknown return type for %s", ff->name);

    ffi_cif cif;
    if (ffi_prep_cif_var(&cif, FFI_DEFAULT_ABI, (unsigned)nfixed, (unsigned)total,
                         rtype, total ? atypes : NULL) != FFI_OK)
        scm_raise(V_FALSE, "ffi-call-variadic: ffi_prep_cif_var failed for %s", ff->name);

    uint8_t ret_buf[FFI_ARG_BUF] = {0};
    void (*fn_ptr)(void);
    memcpy(&fn_ptr, &ff->fn, sizeof(fn_ptr));
    ffi_call(&cif, fn_ptr, ret_buf, total ? ptrs : NULL);
    return unmarshal_ret(ret_buf, ff->ret_tag);
}

/* ---- Foreign callbacks (a Scheme procedure exposed to C as a real,
 * callable function pointer) ----
 *
 * libffi's ffi_prep_closure_loc builds a small stub of executable machine
 * code -- the ForeignCallback's `code` pointer, handed to C as the actual
 * function pointer -- that, when C calls it, invokes closure_trampoline
 * below with (cif, ret, args, user_data). This is the reverse of an
 * ordinary FFI call: instead of curry calling into C, C is calling into
 * curry, so every marshaling direction is flipped from ffi_call_fn's
 * (args are now C values arriving that need unmarshaling to Scheme; the
 * return value is now a Scheme value that needs marshaling back to C). */

/* A closure's return slot has a real, well-documented libffi ABI wrinkle
 * that a plain call's argument marshaling does not: an integer/pointer
 * return narrower than a full register (ffi_arg, e.g. a 32-bit int on a
 * 64-bit host) must still be WRITTEN as a full ffi_arg, not at its
 * nominal width -- libffi's closure trampoline reads the return slot
 * back at register width regardless of the declared C type, and the
 * platform ABI expects sign/zero-extension to already be done. This is
 * why this is a separate function from marshal_arg (correct for a call's
 * arguments, which each get their own natural-width storage slot) rather
 * than reusing it here.
 *
 * 'string'/'c_string' is deliberately NOT a supported callback return
 * type: marshal_arg's existing string handling for a *call argument*
 * hands over str_data()'s pointer uncopied, which is safe for the
 * duration of that one call -- but a callback's return value may be held
 * and used by the C library indefinitely after this trampoline returns,
 * and nothing keeps a curry-heap String object reachable from Boehm GC's
 * perspective once it's reachable only via a raw pointer buried inside
 * arbitrary C library state Boehm doesn't know how to scan. Rejected
 * outright at ffi_make_callback definition time (see below) rather than
 * left to fail unpredictably on first use. */
static void marshal_callback_ret(val_t v, val_t tag, void *ret) {
    const char *t = norm_tag(tag_str(tag));
    if (!strcmp(t, "void")) return;
    if (!strcmp(t, "double")) {
        double d = num_to_double(v);
        memcpy(ret, &d, sizeof(d));
        return;
    }
    if (!strcmp(t, "float")) {
        /* Floating-point returns use the platform's separate float
         * register class, not the integer ffi_arg promotion below --
         * written at natural width, matching libffi's own examples. */
        float f = (float)num_to_double(v);
        memcpy(ret, &f, sizeof(f));
        return;
    }
    /* Every other supported return type is integer-or-pointer-shaped.
     * ffi_arg is libffi's own register-width unsigned integer typedef --
     * casting through intptr_t (signed, same width) for the signed cases
     * sign-extends correctly; the unsigned cases zero-extend via a plain
     * widening assignment, which C does correctly on its own. */
    ffi_arg out = 0;
    if (!strcmp(t,"int") || !strcmp(t,"int32") || !strcmp(t,"int32_t") || !strcmp(t,"bool")) {
        int32_t n = (int32_t)(vis_fixnum(v) ? vunfix(v) : vis_true(v) ? 1 : 0);
        out = (ffi_arg)(intptr_t)n;
    } else if (!strcmp(t,"uint") || !strcmp(t,"uint32") || !strcmp(t,"uint32_t")) {
        uint32_t n = (uint32_t)(vis_fixnum(v) ? (uintptr_t)vunfix(v) : 0);
        out = (ffi_arg)n;
    } else if (!strcmp(t,"long") || !strcmp(t,"int64") || !strcmp(t,"int64_t") ||
               !strcmp(t,"intptr") || !strcmp(t,"ssize_t")) {
        int64_t n = vis_fixnum(v) ? (int64_t)vunfix(v) : 0;
        out = (ffi_arg)(intptr_t)n;
    } else if (!strcmp(t,"ulong") || !strcmp(t,"uint64") || !strcmp(t,"uint64_t") ||
               !strcmp(t,"size_t") || !strcmp(t,"uintptr")) {
        uint64_t n = vis_fixnum(v) ? (uint64_t)(uintptr_t)vunfix(v) : 0;
        out = (ffi_arg)n;
    } else if (!strcmp(t,"c_ptr") || !strcmp(t,"pointer") || !strcmp(t,"void*")) {
        void *p = vis_cptr(v)   ? as_cptr(v)->ptr
                : vis_false(v)  ? NULL
                : vis_fixnum(v) ? (void *)(uintptr_t)vunfix(v)
                : NULL;
        out = (ffi_arg)(uintptr_t)p;
    }
    /* Unknown/unsupported tag: out stays 0 -- unreachable in practice,
     * ffi_make_callback already validated every tag (including rejecting
     * string/c_string) before this trampoline could ever be installed. */
    memcpy(ret, &out, sizeof(out));
}

static void closure_trampoline(ffi_cif *cif, void *ret, void **args, void *user_data) {
    (void)cif;
    ForeignCallback *fc = (ForeignCallback *)user_data;

    /* The C library that owns this function pointer can invoke it from
     * ANY thread it chooses -- not necessarily one curry itself ever
     * spawned or registered (an async callback fired from a library's
     * own worker thread is exactly the case this guards). Same two-call
     * pattern actors.c uses when spawning a brand-new pthread:
     * gc_register_thread() is a documented no-op if already registered;
     * vm_init() is NOT idempotent (it unconditionally mallocs a fresh VM
     * struct), hence the explicit `if (!vm)` guard -- calling it
     * unconditionally on an already-registered thread (the common case:
     * most callbacks, e.g. qsort's comparator, fire synchronously on the
     * same thread that made the original FFI call) would leak a second
     * VM struct and orphan the first, corrupting that thread's VM state. */
    gc_register_thread();
    if (!vm) vm_init();

    int nargs = fc->nargs;
    val_t argv[FFI_MAX_ARGS];
    val_t tag_list = fc->arg_tags;
    for (int i = 0; i < nargs; i++) {
        argv[i] = unmarshal_ret(args[i], vcar(tag_list));
        tag_list = vcdr(tag_list);
    }

    /* A Scheme exception must never longjmp back into the C library that
     * invoked this trampoline -- it has no idea what curry's setjmp-based
     * unwinding is, and the result is undefined behavior (identical
     * rationale to modules/qt6/qt6.cpp's SCHEME_CALL macro at its own
     * C++-callback boundary). Report to stderr and fall through to a
     * zeroed return value instead of propagating -- SCM_PROTECT's own
     * on_exn path already restores vm->sp/frame_count/open_upvalues via
     * its built-in vm_exn_state_save/restore, so no extra manual
     * VM-state bookkeeping is needed here beyond that. */
    ExnHandler h;
    val_t result = V_VOID;
    bool raised = false;
    SCM_PROTECT(h, {
        result = apply_arr(fc->proc, nargs, argv);
    }, {
        raised = true;
        fprintf(stderr, "[ffi callback] uncaught exception in a foreign-callback procedure "
                        "-- returning a default value to the C caller\n");
    });

    /* NOT memset(ret, 0, FFI_ARG_BUF): `ret` is libffi's own buffer, sized
     * for this closure's actual declared return type (as narrow as
     * sizeof(float) for a 'float callback) -- FFI_ARG_BUF (16, "max
     * sizeof any supported C type") is only a safe size for curry's OWN
     * oversized scratch buffers elsewhere (e.g. ffi_call_fn's ret_buf), not
     * for a buffer libffi itself allocated and sized precisely. Writing a
     * flat 16 zero bytes here would write past what libffi reserved,
     * corrupting adjacent stack memory in this trampoline's own frame.
     * marshal_callback_ret already writes exactly the right width for
     * every supported ret_tag; vfix(0) is a valid input for all of them
     * (int-family -> 0, double/float -> 0.0, pointer -> NULL, void ->
     * ignored) without risking num_to_double raising on a non-number,
     * which V_FALSE would have done here -- a second raise from inside
     * this already-caught exception's on_exn branch. */
    if (raised) marshal_callback_ret(vfix(0), fc->ret_tag, ret);
    else marshal_callback_ret(result, fc->ret_tag, ret);
}

val_t ffi_make_callback(val_t proc, val_t ret_tag, val_t arg_tags) {
    int nargs = scm_list_length(arg_tags);
    if (nargs < 0) scm_raise(V_FALSE, "ffi-make-callback: arg-types must be a proper list");
    if (nargs > FFI_MAX_ARGS)
        scm_raise(V_FALSE, "ffi-make-callback: %d args exceeds max %d", nargs, FFI_MAX_ARGS);

    ffi_type **atypes = malloc((nargs ? (size_t)nargs : 1) * sizeof(ffi_type *));
    val_t tlist = arg_tags;
    for (int i = 0; i < nargs; i++) {
        ffi_type *ft = ffi_type_for_tag(vcar(tlist));
        if (!ft) {
            free(atypes);
            scm_raise(V_FALSE, "ffi-make-callback: unknown arg type '%s'", tag_str(vcar(tlist)));
        }
        atypes[i] = ft;
        tlist = vcdr(tlist);
    }

    const char *rt = norm_tag(tag_str(ret_tag));
    if (!strcmp(rt, "string") || !strcmp(rt, "c_string")) {
        free(atypes);
        scm_raise(V_FALSE, "ffi-make-callback: 'string is not a supported callback return "
                  "type -- the returned pointer's lifetime can't be tracked once control "
                  "returns to C; write into a caller-supplied buffer argument instead");
    }
    ffi_type *rtype = ffi_type_for_tag(ret_tag);
    if (!rtype) {
        free(atypes);
        scm_raise(V_FALSE, "ffi-make-callback: unknown return type '%s'", tag_str(ret_tag));
    }

    ffi_cif *cif = malloc(sizeof(ffi_cif));
    if (ffi_prep_cif(cif, FFI_DEFAULT_ABI, (unsigned)nargs, rtype,
                     nargs ? atypes : NULL) != FFI_OK) {
        free(atypes); free(cif);
        scm_raise(V_FALSE, "ffi-make-callback: ffi_prep_cif failed");
    }

    void *code = NULL;
    ffi_closure *closure = (ffi_closure *)ffi_closure_alloc(sizeof(ffi_closure), &code);
    if (!closure) {
        free(atypes); free(cif);
        scm_raise(V_FALSE, "ffi-make-callback: ffi_closure_alloc failed");
    }

    ForeignCallback *fc = CURRY_NEW_PINNED(ForeignCallback);
    fc->hdr.type  = T_FOREIGN_CALLBACK;
    fc->hdr.flags = 0;
    fc->closure   = closure;
    fc->code      = code;
    fc->cif       = cif;
    fc->cif_atypes = atypes;
    fc->proc      = proc;
    fc->arg_tags  = arg_tags;
    fc->ret_tag   = ret_tag;
    fc->nargs     = nargs;
    fc->freed     = false;
    fc->gc_root_slot = NULL;

    /* user_data is this ForeignCallback itself -- closure_trampoline gets
     * it back as the 4th argument on every call. Root registration
     * happens strictly AFTER this succeeds: `fc` is not yet reachable
     * from anywhere else at this point (CURRY_NEW_PINNED is an ordinary
     * collectible allocation, not an immortal one -- see the struct's own
     * doc comment in object.h), so a failure path below can freely free
     * `closure`/`cif`/`atypes` and let `fc` itself become ordinary
     * garbage with no dangling-pointer risk, exactly because nothing has
     * rooted it yet. */
    if (ffi_prep_closure_loc(closure, cif, closure_trampoline, fc, code) != FFI_OK) {
        ffi_closure_free(closure);
        free(atypes); free(cif);
        scm_raise(V_FALSE, "ffi-make-callback: ffi_prep_closure_loc failed");
    }

    /* Boehm only scans GC_MALLOC_UNCOLLECTABLE memory for registered
     * roots (see gc_register_root's own comment in gc.c: "the slot is
     * GC_MALLOC_UNCOLLECTABLE, so Boehm conservatively scans it" --
     * ordinary malloc'd memory is invisible to it) -- this is what
     * actually keeps `fc` reachable for as long as the C library might
     * still hold and invoke its bare function pointer, independent of
     * whether anything on the Scheme side still references the
     * <foreign-callback> value. gc_register_root_val stores vptr(fc) into
     * the slot and registers it in one locked critical section. */
    val_t *root_slot = (val_t *)GC_MALLOC_UNCOLLECTABLE(sizeof(val_t));
    gc_register_root_val(root_slot, vptr(fc));
    fc->gc_root_slot = root_slot;

    return vptr(fc);
}

val_t ffi_callback_ptr(val_t cb_val) {
    if (!vis_foreigncallback(cb_val))
        scm_raise(V_FALSE, "ffi-callback-ptr: not a foreign-callback");
    ForeignCallback *fc = as_foreigncallback(cb_val);
    if (fc->freed)
        scm_raise(V_FALSE, "ffi-callback-ptr: this foreign-callback was already freed");
    return ffi_make_cptr(fc->code);
}

val_t ffi_callback_free(val_t cb_val) {
    if (!vis_foreigncallback(cb_val))
        scm_raise(V_FALSE, "ffi-callback-free!: not a foreign-callback");
    ForeignCallback *fc = as_foreigncallback(cb_val);
    if (fc->freed)
        scm_raise(V_FALSE, "ffi-callback-free!: this foreign-callback was already freed "
                  "(double free)");
    /* Deliberately NOT a GC finalizer -- see the ForeignCallback struct's
     * own doc comment in object.h for why this has to be the caller's
     * explicit responsibility: a C library that still holds this
     * callback's function pointer could invoke it at any time until told
     * otherwise, and nothing here can know whether that's still true. */
    ffi_closure_free(fc->closure);
    free(fc->cif);
    free(fc->cif_atypes);
    /* Undo ffi_make_callback's gc_register_root_val: past this point `fc`
     * is ordinary GC-reachability-governed memory again, same as any
     * other collectible object -- fine, since nothing should still be
     * calling through a freed closure's code pointer anyway. The slot
     * itself was GC_MALLOC_UNCOLLECTABLE and must be GC_FREE'd explicitly
     * (Boehm never reclaims UNCOLLECTABLE memory on its own), matching
     * vm_free()'s identical GC_FREE-after-GC_MALLOC_UNCOLLECTABLE pattern
     * for the per-thread VM struct. */
    gc_unregister_root(fc->gc_root_slot);
    GC_FREE(fc->gc_root_slot);
    fc->freed = true;
    return V_VOID;
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
    if (!vis_string(av[0])) scm_raise(V_FALSE, "%%ffi-load: path must be a string");
    val_t lib = ffi_load_library(str_data(as_str(av[0])));
    as_foreignlib(lib)->path = av[0];
    return lib;
}
static val_t prim_ffi_make_fn(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_foreignlib(av[0])) scm_raise(V_FALSE, "%%ffi-make-fn: not a foreign-lib");
    if (!vis_string(av[1]))     scm_raise(V_FALSE, "%%ffi-make-fn: c-name must be a string");
    if (!vis_symbol(av[2]))     scm_raise(V_FALSE, "%%ffi-make-fn: ret-type must be a symbol");
    return ffi_make_fn(av[0], str_data(as_str(av[1])), av[2], av[3]);
}
static val_t prim_ffi_call(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return ffi_call_fn(av[0], av[1]); }
static val_t prim_ffi_make_fn_variadic(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_foreignlib(av[0])) scm_raise(V_FALSE, "%%ffi-make-fn-variadic: not a foreign-lib");
    if (!vis_string(av[1]))     scm_raise(V_FALSE, "%%ffi-make-fn-variadic: c-name must be a string");
    if (!vis_symbol(av[2]))     scm_raise(V_FALSE, "%%ffi-make-fn-variadic: ret-type must be a symbol");
    return ffi_make_fn_variadic(av[0], str_data(as_str(av[1])), av[2], av[3]);
}
static val_t prim_ffi_call_variadic(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return ffi_call_fn_variadic(av[0], av[1], av[2]); }
static val_t prim_ffi_make_callback(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_symbol(av[1])) scm_raise(V_FALSE, "%%ffi-make-callback: ret-type must be a symbol");
    return ffi_make_callback(av[0], av[1], av[2]);
}
static val_t prim_ffi_callback_ptr(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return ffi_callback_ptr(av[0]); }
static val_t prim_ffi_callback_free(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return ffi_callback_free(av[0]); }
static val_t prim_foreigncallback_p(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return vbool(vis_foreigncallback(av[0])); }
static val_t prim_make_cptr(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    void *p = vis_fixnum(av[0]) ? (void *)(uintptr_t)vunfix(av[0]) : NULL;
    return ffi_make_cptr(p);
}
static val_t prim_cptr_address(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_cptr(av[0])) scm_raise(V_FALSE, "%%ffi-cptr-address: not a c-ptr");
    return vfix((intptr_t)(uintptr_t)as_cptr(av[0])->ptr);
}
static val_t prim_ffi_matrix_ptr(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_matrix(av[0])) scm_raise(V_FALSE, "%%ffi-matrix-ptr: not a matrix");
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
    if (!vis_tensor(av[0])) scm_raise(V_FALSE, "%%ffi-tensor-ptr: not a tensor");
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
/* General raw-buffer pinning: a bytevector's own bytes, read/writable by a
 * foreign call, for the FFI needs matrix/tensor pinning don't cover — input
 * arrays of a non-double element type (e.g. HDF5's hsize_t dimension list)
 * and out-parameters a C function writes results into. Callers decode the
 * written bytes back in Scheme (e.g. the u64be/f64be helpers already used
 * for FITS/NetCDF) rather than this exposing typed accessors itself. */
static val_t prim_ffi_bytevector_ptr(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_bytes(av[0])) scm_raise(V_FALSE, "%%ffi-bytevector-ptr: not a bytevector");
    gc_pin(as_bytes(av[0]));
    return ffi_make_cptr(as_bytes(av[0])->data);
}
static val_t prim_ffi_bytevector_unpin(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (vis_bytes(av[0])) gc_unpin(as_bytes(av[0]));
    return V_VOID;
}
/* Read n bytes from an arbitrary address into a fresh bytevector — needed
 * when a C function hands back a pointer to its own heap memory (e.g.
 * HDF5's variable-length string attributes are returned as a char* the
 * caller must copy out, not written inline into a caller-supplied buffer
 * the way fixed-size out-params are). av[0] is a c-ptr or a raw fixnum
 * address, matching marshal_arg's existing c-ptr acceptance of either. */
static val_t prim_ffi_peek_bytes(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    void *src = vis_cptr(av[0])   ? as_cptr(av[0])->ptr
              : vis_fixnum(av[0]) ? (void *)(uintptr_t)vunfix(av[0])
              : NULL;
    if (!vis_fixnum(av[1])) scm_raise(V_FALSE, "%%ffi-peek-bytes: n must be exact integer");
    intptr_t n = vunfix(av[1]);
    if (n < 0) scm_raise(V_FALSE, "%%ffi-peek-bytes: n must be non-negative");
    if (!src && n > 0) scm_raise(V_FALSE, "%%ffi-peek-bytes: NULL pointer");
    Bytevector *bv = (Bytevector *)gc_alloc_atomic(sizeof(Bytevector) + (size_t)n);
    bv->hdr.type = T_BYTEVECTOR; bv->hdr.flags = 0; bv->len = (uint32_t)n;
    if (n > 0) memcpy(bv->data, src, (size_t)n);
    return vptr(bv);
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
    ffi_def(env, "%ffi-make-fn-variadic", prim_ffi_make_fn_variadic, 4, 4);
    ffi_def(env, "%ffi-call-variadic",  prim_ffi_call_variadic, 3, 3);
    ffi_def(env, "%ffi-make-callback",  prim_ffi_make_callback, 3, 3);
    ffi_def(env, "%ffi-callback-ptr",   prim_ffi_callback_ptr,  1, 1);
    ffi_def(env, "%ffi-callback-free!", prim_ffi_callback_free, 1, 1);
    ffi_def(env, "foreign-callback?",   prim_foreigncallback_p, 1, 1);
    ffi_def(env, "%ffi-make-cptr",      prim_make_cptr,         1, 1);
    ffi_def(env, "%ffi-cptr-address",   prim_cptr_address,      1, 1);
    ffi_def(env, "%ffi-matrix-ptr",     prim_ffi_matrix_ptr,    1, 1);
    ffi_def(env, "%ffi-matrix-unpin",   prim_ffi_matrix_unpin,  1, 1);
    ffi_def(env, "%ffi-tensor-ptr",     prim_ffi_tensor_ptr,    1, 1);
    ffi_def(env, "%ffi-tensor-unpin",   prim_ffi_tensor_unpin,  1, 1);
    ffi_def(env, "%ffi-bytevector-ptr", prim_ffi_bytevector_ptr, 1, 1);
    ffi_def(env, "%ffi-bytevector-unpin", prim_ffi_bytevector_unpin, 1, 1);
    ffi_def(env, "%ffi-peek-bytes",     prim_ffi_peek_bytes,    2, 2);
    ffi_def(env, "c-ptr?",              prim_cptr_p,            1, 1);
    ffi_def(env, "foreign-lib?",        prim_foreignlib_p,      1, 1);
    ffi_def(env, "foreign-fn?",         prim_foreignfn_p,       1, 1);
    ffi_def(env, "foreign-lib-path",    prim_foreignlib_path,   1, 1);
}
