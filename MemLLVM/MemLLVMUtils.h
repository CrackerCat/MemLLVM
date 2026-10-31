#pragma once

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include <cstdint>

namespace memllvm {

uint32_t calculateRor13(llvm::StringRef str);
llvm::Function *findOrSynthesizeEntryPoint(llvm::Module &M);

} // namespace memllvm
