#ifndef CURRY_LLVM_BCJIT_H
#define CURRY_LLVM_BCJIT_H

/* First, and outside extern "C", as in codegen.h: object.h brings in
 * <gmp.h>, whose C++ stream operators cannot have C linkage. */
#include "../value.h"
#include "../object.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <memory>
#include <string>

extern "C" {
#include "../chunk.h"
}

namespace curry {

/* Translate one chunk's bytecode into a module holding one function
 *     i64 name(i32 argc, ptr args, ptr self)
 * (see bcjit.cpp). Throws std::runtime_error, with a message that says
 * why, if the chunk cannot be translated; the caller leaves the closure on
 * bytecode. */
std::unique_ptr<llvm::Module> bcjit_translate(llvm::LLVMContext &ctx, const Chunk *ch,
                                              const std::string &name);

} /* namespace curry */

#endif /* CURRY_LLVM_BCJIT_H */
