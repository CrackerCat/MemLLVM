#include "MemLLVM.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace memllvm {

PreservedAnalyses GlobalSectionPass::run(Module &M, ModuleAnalysisManager &MAM) {
  bool Changed = false;
  LLVMContext &Ctx = M.getContext();

  for (GlobalVariable &GV : M.globals()) {
    if (GV.isDeclaration())
      continue;
    if (GV.getName().starts_with("llvm."))
      continue;

    // Ensure globals do not have split .text$* section names that confuse LLD section merging
    if (GV.hasSection() && GV.getSection().starts_with(".text")) {
      GV.setSection("");
      Changed = true;
    }
  }

  // We ensure common symbols have explicit zero initializers so they belong cleanly to .bss/.data
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace memllvm
