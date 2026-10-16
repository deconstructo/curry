/*
 * GC strategy registration for the LLVM backend.
 *
 * Registers "curry-statepoint" as a custom GC strategy so that every
 * function emitted by codegen.cpp (which carries gc "curry-statepoint")
 * is handled correctly by LLVM's statepoint infrastructure.
 *
 * Call register_gc_strategy() once before creating any LLVM Module.
 */

#pragma once

namespace curry {
void register_gc_strategy();
} /* namespace curry */
