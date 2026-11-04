#pragma once

#include "llvm/ADT/StringRef.h"
#include <string>

namespace memllvm {

std::string probeRequiredDllForApi(llvm::StringRef ApiName);
bool isKnownWindowsApi(llvm::StringRef ApiName);

// Queries whether ApiName is an ordinal-only export (or explicit ordinal reference)
// If true, returns true and fills OutDll and OutOrdinal
bool probeOrdinalForApi(llvm::StringRef ApiName, std::string &OutDll, uint32_t &OutOrdinal);

} // namespace memllvm
