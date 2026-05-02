#ifndef CURRY_H
#define CURRY_H

/*
 * Curry Scheme — public embedding API.
 *
 * Include this header to embed Curry in a C/C++ application or to write
 * a loadable module (shared library).
 *
 * Module entry point convention:
 *
 *   #include <curry.h>
 *
 *   void curry_module_init(CurryVM *vm) {
 *       curry_define_fn(vm, "my-proc", my_proc_impl, 1, 1, NULL);
 *       curry_define_val(vm, "my-const", curry_make_fixnum(42));
 *   }
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Value type ---- */
typedef uintptr_t curry_val;

/* ---- VM handle (opaque) ---- */
typedef struct CurryVM CurryVM;

/* ---- Module init function (exported by each .so module) ---- */
typedef void (*CurryModuleInitFn)(CurryVM *vm);

/* ---- Registering bindings ---- */
typedef curry_val (*CurryFn)(int argc, curry_val *argv, void *ud);

void curry_define_fn(CurryVM *vm, const char *name, CurryFn fn,
                     int min_args, int max_args, void *ud);
void curry_define_val(CurryVM *vm, const char *name, curry_val value);
curry_val curry_vm_env(CurryVM *vm);

/* ---- Value constructors ---- */
curry_val curry_make_fixnum(intptr_t n);
curry_val curry_make_float(double d);
curry_val curry_make_bool(bool b);
curry_val curry_make_char(uint32_t codepoint);
curry_val curry_make_string(const char *s);    /* copies s */
curry_val curry_make_symbol(const char *s);
curry_val curry_make_pair(curry_val car, curry_val cdr);
curry_val curry_nil(void);
curry_val curry_void(void);
curry_val curry_eof(void);

/* ---- Value accessors ---- */
bool       curry_is_fixnum(curry_val v);
bool       curry_is_float(curry_val v);
bool       curry_is_bool(curry_val v);
bool       curry_is_char(curry_val v);
bool       curry_is_string(curry_val v);
bool       curry_is_symbol(curry_val v);
bool       curry_is_pair(curry_val v);
bool       curry_is_nil(curry_val v);
bool       curry_is_void(curry_val v);
bool       curry_is_eof(curry_val v);
bool       curry_is_procedure(curry_val v);
bool       curry_is_true(curry_val v);

intptr_t   curry_fixnum(curry_val v);
double     curry_float(curry_val v);
bool       curry_bool(curry_val v);
uint32_t   curry_char(curry_val v);
const char *curry_string(curry_val v);    /* pointer into GC heap */
const char *curry_symbol(curry_val v);
curry_val  curry_car(curry_val v);
curry_val  curry_cdr(curry_val v);

/* ---- Calling Scheme from C ---- */
curry_val  curry_apply(curry_val proc, int argc, curry_val *argv);

/* ---- Error handling ---- */
/* Call from within a CurryFn to raise an error */
void curry_error(const char *fmt, ...) __attribute__((noreturn));

/* ---- Vector / bytevector ---- */
curry_val  curry_make_vector(uint32_t len, curry_val fill);
uint32_t   curry_vector_length(curry_val v);
curry_val  curry_vector_ref(curry_val v, uint32_t i);
void       curry_vector_set(curry_val v, uint32_t i, curry_val val);

curry_val  curry_make_bytevector(uint32_t len, uint8_t fill);
uint32_t   curry_bytevector_length(curry_val v);
uint8_t    curry_bytevector_ref(curry_val v, uint32_t i);
void       curry_bytevector_set(curry_val v, uint32_t i, uint8_t b);

/* ---- Numeric tower ---- */
curry_val  curry_make_complex(curry_val real, curry_val imag);
curry_val  curry_make_quaternion(double a, double b, double c, double d);
curry_val  curry_make_octonion(const double e[8]);

/* ---- List building helpers ---- */
curry_val  curry_list(int n, ...);  /* curry_list(3, a, b, c) -> (a b c) */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CURRY_H */
