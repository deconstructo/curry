/*
 * modules/qt6/qt6_common.h — helpers shared between the translation units
 * of the qt6 module (qt6.cpp, gpu.cpp).
 *
 * All of these are defined in qt6.cpp.  They used to be file-static there;
 * the QRhi layer in gpu.cpp needs the same handle encoding, argument
 * checking, GC rooting and exception boundary, so they are exported within
 * the module instead of being duplicated.
 */
#ifndef CURRY_QT6_COMMON_H
#define CURRY_QT6_COMMON_H

#include <cstdint>
#include <curry.h>
#include "eval.h"   /* SCM_PROTECT, ExnHandler */

#include <QString>
#include <Qt>

/* Print a Scheme exception caught at a C++ -> Scheme boundary. */
void qt6_print_exn(const char *where, curry_val exn);

/* Wrap every curry_apply at a C++→Scheme boundary.  Any Scheme exception
 * (longjmp) must not escape into Qt's C++ event machinery, as Qt does not
 * handle foreign non-local exits and the result is undefined behaviour.
 *
 * We also save/restore VM state on exception: if an exception fires inside
 * curry_apply(), the vm->sp restoration inside apply() is bypassed via
 * longjmp, leaving vm->sp corrupted.  Restoring it here prevents the
 * corruption from cascading into heap corruption on subsequent callbacks. */
#define SCHEME_CALL(call) do { \
    ExnHandler _sch_h_; \
    CurryVMState _vm_state_; \
    curry_vm_state_save(&_vm_state_); \
    SCM_PROTECT(_sch_h_, (call), { \
        qt6_print_exn("[qt6]", _sch_h_.exn); \
        curry_vm_state_restore(&_vm_state_); \
    }); \
} while(0)

/* Keep a Scheme value alive for the rest of the program (never released). */
curry_val *keep_alive(curry_val v);

/* Create the QApplication singleton if it doesn't exist yet. */
void ensure_app();

/* Tagged handles: (tag-symbol . pointer-bytevector). */
curry_val ptr_to_val(const char *tag, void *ptr);
void     *val_to_ptr(const char *tag, curry_val v);

/* Checked argument accessors; argpos is 1-based (issue #187). */
const char *checked_string(curry_val v, int argpos, const char *who);
double      checked_float(curry_val v, int argpos, const char *who);
const char *checked_symbol(curry_val v, int argpos, const char *who);
intptr_t    checked_fixnum(curry_val v, int argpos, const char *who);

/* Key name / modifier list encoding shared by every canvas type. */
const char *qt_key_str(int key, const QString &text);
curry_val   build_mods(Qt::KeyboardModifiers m);

#ifdef CURRY_QT6_GPU
/* Registers the gpu-* (QRhi) procedures; defined in gpu.cpp. */
void qt6_gpu_register(CurryVM *vm);
#endif

#endif /* CURRY_QT6_COMMON_H */
