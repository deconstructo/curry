/*
 * (curry math tensor) — loadable module wrapping the built-in tensor type.
 *
 * Registers all tensor Scheme primitives into the module environment so that
 *   (import (curry math tensor))
 * makes them available in the importing scope.
 *
 * Also includes the matrix<->tensor conversion functions (matrix->tensor,
 * tensor->matrix) so that code importing only this module can interoperate
 * with matrix values.
 */

#include <curry.h>
#include "matrix.h"

void curry_module_init(CurryVM *vm) {
    mat_register_tensor_builtins((val_t)curry_vm_env(vm));
}
