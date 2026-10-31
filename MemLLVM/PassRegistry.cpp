#include "MemLLVM.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION,
    "MemLLVM",
    LLVM_VERSION_STRING,
    [](PassBuilder &PB) {
      errs() << "[MemLLVM] Plugin registered successfully! (Modular Architecture)\n";

      PB.registerPipelineParsingCallback(
        [](StringRef Name, ModulePassManager &MPM, ArrayRef<PassBuilder::PipelineElement>) {
          if (Name == "mem-llvm") {
            MPM.addPass(memllvm::AutoMemoryPass());
            MPM.addPass(memllvm::StlStubPass());
            MPM.addPass(memllvm::GlobalLifecyclePass());
            MPM.addPass(memllvm::GlobalSectionPass());
            MPM.addPass(memllvm::GlobalPointerPass());
            MPM.addPass(memllvm::AutoLoadLibraryPass());
            MPM.addPass(memllvm::StackStringPass());
            MPM.addPass(memllvm::ApiResolvePass());
            return true;
          }
          return false;
        });

      PB.registerOptimizerLastEPCallback(
        [](ModulePassManager &MPM, OptimizationLevel Level, ThinOrFullLTOPhase Phase) {
          MPM.addPass(memllvm::AutoMemoryPass());
          MPM.addPass(memllvm::StlStubPass());
          MPM.addPass(memllvm::GlobalLifecyclePass());
          MPM.addPass(memllvm::GlobalSectionPass());
          MPM.addPass(memllvm::GlobalPointerPass());
          MPM.addPass(memllvm::AutoLoadLibraryPass());
          MPM.addPass(memllvm::StackStringPass());
          MPM.addPass(memllvm::ApiResolvePass());
        });
    }
  };
}
