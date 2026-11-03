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
  errs() << "[MemLLVM] GlobalSectionPass running on module: " << M.getName() << "\n";

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

  // Automatically inject linker directive to merge .rdata into .text
  // This eliminates 4KB PE section alignment gaps between code and read-only data.
  NamedMDNode *LinkerOptions = M.getOrInsertNamedMetadata("llvm.linker.options");
  bool HasMergeRdata = false;
  for (unsigned i = 0; i < LinkerOptions->getNumOperands(); ++i) {
    MDNode *MD = LinkerOptions->getOperand(i);
    for (unsigned j = 0; j < MD->getNumOperands(); ++j) {
      if (auto *MDS = dyn_cast<MDString>(MD->getOperand(j))) {
        if (MDS->getString().contains("/merge:.rdata=.text")) HasMergeRdata = true;
      }
    }
  }
  if (!HasMergeRdata) {
    Metadata *Args[] = { MDString::get(Ctx, "/merge:.rdata=.text") };
    LinkerOptions->addOperand(MDNode::get(Ctx, Args));
    Changed = true;
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace memllvm
