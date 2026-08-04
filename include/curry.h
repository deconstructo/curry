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
#include <stdio.h>

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
curry_val curry_make_string(const char *s);    /* copies s (NUL-terminated C string) */
/* Copies exactly `len` bytes of s (which need not itself be NUL-terminated,
 * and may contain embedded NUL bytes — Curry strings are length-prefixed,
 * not NUL-terminated at the language level). Use this instead of
 * curry_make_string() whenever the source buffer's length is already
 * known and isn't guaranteed free of embedded NULs — e.g. a length-
 * prefixed wire-protocol read — so a NUL byte partway through doesn't
 * silently truncate the result. */
curry_val curry_make_string_n(const char *s, uint32_t len);
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
bool       curry_is_vector(curry_val v);
bool       curry_is_bytevector(curry_val v);
bool       curry_is_true(curry_val v);
bool       curry_is_error(curry_val v);
const char *curry_error_message(curry_val v); /* NULL if message is not a string */
/* True for any numeric-tower value (fixnum/flonum/bignum/rational/
 * complex/etc), not just the fixnum/float cases curry_is_fixnum/
 * curry_is_float cover. */
bool       curry_is_number(curry_val v);
/* Convert any numeric-tower value to its closest double — for bignums/
 * rationals/etc a module has no other way to introspect through this
 * API. Lossy where the exact value doesn't fit a double exactly (same
 * tradeoff as Scheme's own exact->inexact). Returns NAN (not a raised
 * error) if v isn't a number — matching every other accessor in this
 * header, which assume the caller already checked the type. Check
 * curry_is_number(v) first if you need to distinguish "wasn't a number"
 * from a legitimate NaN input. */
double     curry_number_to_double(curry_val v);

intptr_t   curry_fixnum(curry_val v);
double     curry_float(curry_val v);
bool       curry_bool(curry_val v);
uint32_t   curry_char(curry_val v);
const char *curry_string(curry_val v);    /* pointer into GC heap */
/* True byte length of a string, INCLUDING any embedded NUL bytes (Curry
 * strings are length-prefixed, not NUL-terminated at the language level —
 * `(string-length (string #\a (integer->char 0) #\b))` is 3). Modules
 * that pass a curry_string() pointer to a strlen()-based C API silently
 * truncate any string containing an embedded NUL at the first one; use
 * this length explicitly with a length-aware API instead wherever a
 * module can't rule that out. */
uint32_t   curry_string_length(curry_val v);
const char *curry_symbol(curry_val v);
curry_val  curry_car(curry_val v);
curry_val  curry_cdr(curry_val v);

/* ---- Calling Scheme from C ---- */
curry_val  curry_apply(curry_val proc, int argc, curry_val *argv);

/* ---- VM state save/restore (for C++ event boundaries) ----
 *
 * When curry_apply is wrapped in a setjmp exception handler (e.g. SCM_PROTECT),
 * a Scheme exception longjmps past the vm->sp restoration inside apply().
 * Save the VM state before the call and restore it in the exception handler to
 * prevent stack-pointer corruption from cascading into heap corruption.
 *
 *   CurryVMState s;
 *   curry_vm_state_save(&s);
 *   SCM_PROTECT(h, curry_apply(...), curry_vm_state_restore(&s));
 */
typedef struct {
    void *sp;
    int   frame_count;
    void *open_upvalues;
} CurryVMState;

void curry_vm_state_save(CurryVMState *s);
void curry_vm_state_restore(const CurryVMState *s);
void *curry_vm_sp(void);  /* diagnostic: current vm->sp value */

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

/* Package argc values as an R7RS multiple-values object, the same
 * representation (values a b c) produces -- a caller using
 * call-with-values or a define-values/let-values binding form sees
 * exactly argc separate values. argc==1 returns argv[0] itself, unwrapped
 * (a single value is never boxed), matching (values x). */
curry_val  curry_make_values(int argc, curry_val *argv);

/* ---- Ports ----
 * Wrap an existing file descriptor (e.g. a socket) as a Curry port, usable
 * directly with read-line/write-string/read-char/etc. Takes ownership of
 * fd (closes it via GC finalizer, or when the returned port is explicitly
 * closed) — do not close fd yourself after this call. Returns a false-y
 * value (curry_is_true false) if fdopen() fails. */
curry_val  curry_make_port_from_fd(int fd, bool output, bool binary);

/* Wrap an already-open FILE* (e.g. a funopen/fopencookie custom stream
 * backed by something other than a plain fd, such as a TLS session) as a
 * port, taking ownership — the port's GC finalizer (or an explicit
 * close-port) will fclose(fp), which for a custom stream invokes
 * whatever close callback it was created with. */
curry_val  curry_make_port_from_file(FILE *fp, bool output, bool binary);

/* Extract the underlying OS file descriptor from a file-backed port, for
 * callers that want to poll/select on its readiness. Returns -1 for a
 * string port or a closed port. */
int        curry_port_fd(curry_val port);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CURRY_H */
