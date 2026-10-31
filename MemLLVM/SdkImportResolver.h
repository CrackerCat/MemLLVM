#pragma once

#include "llvm/ADT/StringRef.h"
#include <string>

namespace memllvm {

std::string probeRequiredDllForApi(llvm::StringRef ApiName);

} // namespace memllvm
