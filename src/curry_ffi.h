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
 *   (%ffi-make-cptr address-fixnum)         → c-ptr
 *   (%ffi-cptr-address c-ptr)               → fixnum
 *   (%ffi-matrix-ptr matrix)                → c-ptr (double*)
 *   (%ffi-tensor-ptr tensor)                → c-ptr (double*)
 *   (foreign-lib? v)  (foreign-fn? v)  (c-ptr? v)
 *   (foreign-lib-path lib)                  → string
 */

#include "value.h"
#include "object.h"
#include <stdbool.h>

void  ffi_init(void);

val_t ffi_load_library(const char *path);
val_t ffi_make_fn(val_t lib, const char *c_name, val_t ret_tag, val_t arg_tags);
val_t ffi_call_fn(val_t fn, val_t args);
val_t ffi_make_cptr(void *ptr);

void  ffi_register_builtins(val_t env);

#endif /* CURRY_FFI_H */
