#pragma once

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include <cstdint>

namespace memllvm {

uint32_t calculateRor13(llvm::StringRef str);
llvm::Function *findOrSynthesizeEntryPoint(llvm::Module &M);

/// Return the callee of \p CB when the call target is a statically known
/// `llvm::Function`, or nullptr for indirect / inline-asm calls.
///
/// Do NOT use `CallBase::getCalledFunction()` for this. Since LLVM 22 it also
/// requires `F->getValueType() == CB.getFunctionType()`, so any call whose IR
/// function type differs from the declaration's is reported as *indirect* and
/// silently skipped. That happens for every C declaration written without a
/// prototype — `extern PPEB WINAPI RtlGetCurrentPeb();` is lowered as a
/// varargs declaration while the call site is not, and mimikatz relies on this
/// style throughout `modules/kull_m_process.h`. The symptom is a pass that
/// reports "resolved N call sites" yet leaves an import behind.
llvm::Function *getDirectCallee(const llvm::CallBase &CB);

/// True when \p Name must be satisfied statically (LTL / compiler runtime)
/// rather than through the PEB + ROR13 dynamic resolver. Shared by
/// ApiResolvePass (which rewrites call sites) and GlobalPointerPass (which
/// rewrites address-taken references), so the two can never disagree.
bool isStaticShimSymbol(llvm::StringRef Name);

// ---------------------------------------------------------------------------
// Cross-TU initialisation
//
// MemLLVM passes run per translation unit, but historically every initialisation
// action had to be injected into *this* TU's entry function. A TU without an
// entry point (sqlite3.c is the canonical case) therefore never got its global
// pointer tables initialised, and the address-taken APIs inside them survived to
// the linker as undefined imports.
//
// A TU without an entry point now emits a `__memllvm_tu_init` function and
// publishes its address in the `.mli` section. The linker merges every
// contribution into one contiguous `.mli` section; the flat-binary packer then
// walks it and calls each slot right after applying relocations, before jumping
// to the real entry point.
//
// The MSVC-CRT-style `$A`/`$M`/`$Z` sentinel trick was tried first and rejected:
// lld-link does NOT sort COFF `$` subgroups across input files, so the end
// sentinel could land *before* the slots and the loop silently ran zero times.
// A plain section name is the only form both linkers merge reliably.
// ---------------------------------------------------------------------------

/// Get (or create) this module's init function. Body starts as a bare `ret void`;
/// callers insert their stores before the terminator.
llvm::Function *getOrCreateTuInit(llvm::Module &M);

/// Publish \p InitFn into the `.mli` section so the packer will call it.
void registerTuInit(llvm::Module &M, llvm::Function *InitFn);

} // namespace memllvm
