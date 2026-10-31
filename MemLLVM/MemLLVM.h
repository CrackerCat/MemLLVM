#pragma once

#include "llvm/IR/PassManager.h"
#include "llvm/Plugins/PassPlugin.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace memllvm {

class AutoMemoryPass : public llvm::PassInfoMixin<AutoMemoryPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &MAM);
};

class StlStubPass : public llvm::PassInfoMixin<StlStubPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &MAM);
};

class GlobalLifecyclePass : public llvm::PassInfoMixin<GlobalLifecyclePass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &MAM);
};

// Backward compatibility alias for GlobalConstructorPass
using GlobalConstructorPass = GlobalLifecyclePass;

class GlobalSectionPass : public llvm::PassInfoMixin<GlobalSectionPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &MAM);
};

class GlobalPointerPass : public llvm::PassInfoMixin<GlobalPointerPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &MAM);
};

class AutoLoadLibraryPass : public llvm::PassInfoMixin<AutoLoadLibraryPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &MAM);
};

class StackStringPass : public llvm::PassInfoMixin<StackStringPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &MAM);
};

class ApiResolvePass : public llvm::PassInfoMixin<ApiResolvePass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &MAM);
};

} // namespace memllvm

extern "C" {
__declspec(dllexport) int RunAllTests(void);

#ifdef _WIN32
__declspec(dllexport) void CALLBACK RunTests(HWND hwnd, HINSTANCE hinst, LPSTR lpszCmdLine, int nCmdShow);
__declspec(dllexport) void CALLBACK RunAllTestsW(HWND hwnd, HINSTANCE hinst, LPWSTR lpszCmdLine, int nCmdShow);
#endif
}
