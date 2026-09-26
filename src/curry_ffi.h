#ifndef CURRY_FFI_H
#define CURRY_FFI_H

/*
 * General C FFI for Curry Scheme — libffi backend.
 *
 * Type mapping (Scheme symbol → C / libffi):
 *
 *   void                 ffi_type_void
 *   int  int32  int32_t  ffi_type_sint32
 *   uint uint32          ffi_type_uint32
 *   long int64  int64_t  ffi_type_sint64
 *   size-t ulong uint64  ffi_type_uint64
 *   float                ffi_type_float
 *   double               ffi_type_double
 *   c-ptr  pointer       ffi_type_pointer   (T_CPTR ↔ void*)
 *   string  c-string     ffi_type_pointer   (Scheme string ↔ char*)
 *   bool                 ffi_type_sint32    (0/#f, non-zero/#t)
 *
 * Zero-copy matrix/tensor passthrough:
 *   (with-pinned-matrix  m  var body ...) — binds var to T_CPTR of m->data
 *   (with-pinned-tensor  t  var body ...) — binds var to T_CPTR of tensor_data(t)
 *   Under Boehm GC pinning is a no-op; the protocol is in place for
 *   future moving collectors.
 *
 * Scheme API (primitives — higher-level in lib/curry/modules/curry/ffi.scm):
 *   (%ffi-load path)                        → foreign-lib
 *   (%ffi-make-fn lib c-name ret-tag arg-tag-list) → foreign-fn
 *   (%ffi-call fn args)                     → Scheme value
 *   (%ffi-make-fn-variadic lib c-name ret-tag fixed-arg-tag-list) → foreign-fn
 *   (%ffi-call-variadic fn fixed-args variadic-typed-args) → Scheme value
 *     variadic-typed-args is a list of (type-symbol . value) pairs — C
 *     variadic calls have no static signature, so each call site must say
 *     what type each trailing argument is. A 'float pair is silently
 *     promoted to double (C's own default argument promotion for
 *     variadic float arguments — libffi requires this explicitly, see
 *     ffi_prep_cif_var's documentation).
 *   (%ffi-make-callback proc ret-tag arg-tag-list) → foreign-callback
 *     Exposes a Scheme procedure as a real, callable C function pointer
 *     (a libffi closure) -- for handing to a C function that expects a
 *     callback. Fixed-arity only (no variadic callbacks). 'string/
 *     'c_string is not a supported ret-tag (see ffi_make_callback in
 *     ffi.c for why). A C library can invoke this from any thread it
 *     chooses, including one curry never registered -- closure_trampoline
 *     defensively calls gc_register_thread()/vm_init() itself.
 *   (%ffi-callback-ptr cb)                  → c-ptr (the function pointer)
 *   (%ffi-callback-free! cb)                → void
 *     NOT automatic/GC-finalized -- the caller must only call this once
 *     certain the C library will never invoke the callback again; the
 *     ForeignCallback object itself is GC:PIN and never collected on its
 *     own for exactly this reason.
 *   (%ffi-make-cptr address-fixnum)         → c-ptr
 *   (%ffi-cptr-address c-ptr)               → fixnum
 *   (%ffi-matrix-ptr matrix)                → c-ptr (double*)
 *   (%ffi-tensor-ptr tensor)                → c-ptr (double*)
 *   (foreign-lib? v)  (foreign-fn? v)  (c-ptr? v)
 *   (foreign-lib-path lib)                  → string
 *
 *   (%ffi-make-struct-type field-tag-list field-name-list) → ffi-struct-type
 *     A struct-by-value type descriptor: field-tag-list is a list of
 *     scalar type-tag symbols (one per field, in C declaration order --
 *     no nested structs, v1 scope). field-name-list is either '() (no
 *     names -- fields addressed by index only) or a list of symbols the
 *     same length as field-tag-list, letting %ffi-struct-ref/-set!
 *     address a field by name instead of index. Layout (size/alignment/
 *     per-field byte offsets) is computed by libffi itself, not
 *     hand-rolled. Use the resulting value as an arg-tag or ret-tag with
 *     %ffi-make-fn/%ffi-call to pass or receive a struct BY VALUE -- NOT
 *     supported in %ffi-make-fn-variadic/%ffi-call-variadic's trailing
 *     arguments or in %ffi-make-callback's signature (v1 scope; both
 *     reject a struct-type tag with a clear, explicit error, not
 *     silently).
 *   (%ffi-struct-size struct-type)          → fixnum (byte size)
 *   (%ffi-struct-make struct-type)          → bytevector
 *     A fresh, zeroed instance of struct-type -- exactly %ffi-struct-size
 *     bytes. A struct INSTANCE is always a plain bytevector (see
 *     FfiStructType's own doc comment in object.h); there is no separate
 *     "struct instance" heap type.
 *   (%ffi-struct-ref struct-type instance field) → Scheme value
 *   (%ffi-struct-set! struct-type instance field value) → void
 *     Read/write one field of a struct instance in place. `field` is
 *     either a 0-based exact integer index, or a symbol naming the field
 *     (only if struct-type was given field-name-list).
 */

#include "value.h"
#include "object.h"
#include <stdbool.h>

void  ffi_init(void);

val_t ffi_load_library(const char *path);
val_t ffi_make_fn(val_t lib, const char *c_name, val_t ret_tag, val_t arg_tags);
val_t ffi_call_fn(val_t fn, val_t args);
val_t ffi_make_fn_variadic(val_t lib, const char *c_name, val_t ret_tag, val_t fixed_arg_tags);
val_t ffi_call_fn_variadic(val_t fn, val_t fixed_args, val_t variadic_typed_args);
val_t ffi_make_callback(val_t proc, val_t ret_tag, val_t arg_tags);
val_t ffi_callback_ptr(val_t cb);
val_t ffi_callback_free(val_t cb);
val_t ffi_make_struct_type(val_t field_tags, val_t field_names);
val_t ffi_struct_size(val_t struct_type);
val_t ffi_struct_make(val_t struct_type);
val_t ffi_struct_ref(val_t struct_type, val_t bv, val_t field);
val_t ffi_struct_set(val_t struct_type, val_t bv, val_t field, val_t value);
val_t ffi_make_cptr(void *ptr);

void  ffi_register_builtins(val_t env);

#endif /* CURRY_FFI_H */
