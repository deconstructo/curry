/*
 * (curry math matrix) — loadable module wrapping the built-in matrix type.
 *
 * Registers all matrix Scheme primitives into the module environment so that
 *   (import (curry math matrix))
 * makes them available in the importing scope.
 *
 * The underlying primitives are compiled into curry_core and globally
 * pre-registered at startup; this module makes them importable as a
 * first-class named library per the module system contract.
 */

#include <curry.h>
#include "matrix.h"

void curry_module_init(CurryVM *vm) {
    mat_register_matrix_builtins((val_t)curry_vm_env(vm));
}
