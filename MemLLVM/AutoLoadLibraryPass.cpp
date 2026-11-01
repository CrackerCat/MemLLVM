#include "MemLLVM.h"
#include "MemLLVMUtils.h"
#include "SdkImportResolver.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

#include <set>
#include <string>

using namespace llvm;

namespace memllvm {

PreservedAnalyses AutoLoadLibraryPass::run(Module &M, ModuleAnalysisManager &MAM) {
  // Ensure _fltused is defined to satisfy MSVC/LLD floating point CRT contract under /nodefaultlib
  if (!M.getGlobalVariable("_fltused")) {
    GlobalVariable *FltUsed = new GlobalVariable(
      M,
      Type::getInt32Ty(M.getContext()),
      false,
      GlobalValue::LinkOnceODRLinkage,
      ConstantInt::get(Type::getInt32Ty(M.getContext()), 0x9875),
      "_fltused"
    );
    Comdat *C = M.getOrInsertComdat("_fltused");
    C->setSelectionKind(Comdat::Any);
    FltUsed->setComdat(C);
  }

  Function *EntryFn = findOrSynthesizeEntryPoint(M);
  // NOTE: no early return here. A TU without an entry point (kull_m_asn1.c and
  // friends) still needs its subsystem DLLs preloaded, because
  // MemLLVM_GetApiAddress only walks modules that are already mapped. Bailing
  // out meant only the entry TU's own DLLs were ever loaded, and every other
  // TU's API resolution silently returned NULL -> `call 0`.
  const bool HasEntry = EntryFn && !EntryFn->empty();

  std::set<std::string> RequiredDlls;
  for (Function &F : M) {
    if (F.isDeclaration() || F.getName() == "MemLLVM_GetApiAddress")
      continue;

    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        // Match CallBase so that API references inside __try/__except blocks
        // (lowered to `invoke`) also contribute their owning DLL.
        if (auto *CB = dyn_cast<CallBase>(&I)) {
          if (CB->isInlineAsm())
            continue;
          Function *Callee = getDirectCallee(*CB);
          if (Callee && Callee->isDeclaration() && !Callee->isIntrinsic() &&
              !Callee->getName().starts_with("llvm.") &&
              Callee->getName() != "MemLLVM_GetApiAddress") {
            std::string reqDll = probeRequiredDllForApi(Callee->getName());
            if (!reqDll.empty()) {
              errs() << "[MemLLVM] Auto-deduced DLL via SDK Import Library Index: "
                     << Callee->getName() << " -> " << reqDll << "\n";
              RequiredDlls.insert(reqDll);
            }
          }
        }
      }
    }
  }

  if (RequiredDlls.empty())
    return PreservedAnalyses::all();

  errs() << "[MemLLVM] AutoLoadLibraryPass: Auto-detected " << RequiredDlls.size() 
         << " required subsystem DLL(s)\n";

  LLVMContext &Ctx = M.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  FunctionType *LoadLibType = FunctionType::get(PtrTy, {PtrTy}, false);
  FunctionCallee LoadLibFn = M.getOrInsertFunction("LoadLibraryA", LoadLibType);

  // Pick an injection point: the entry function when this TU has one, otherwise
  // the cross-TU init that the flat-image stub dispatches through `.mli`.
  Function *TuInit = nullptr;
  Instruction *InsertBefore = nullptr;
  if (HasEntry) {
    InsertBefore = &*EntryFn->getEntryBlock().getFirstInsertionPt();
  } else {
    TuInit = getOrCreateTuInit(M);
    InsertBefore = &*TuInit->getEntryBlock().getFirstInsertionPt();
  }

  IRBuilder<> Builder(InsertBefore);

  for (const std::string &dllName : RequiredDlls) {
    errs() << "[MemLLVM] Proactively injecting LoadLibraryA(\"" << dllName << "\") into "
           << (HasEntry ? "entry" : "TU init") << "\n";
    Constant *StrConst = ConstantDataArray::getString(Ctx, dllName, true);
    GlobalVariable *GV = new GlobalVariable(
      M,
      StrConst->getType(),
      true,
      GlobalValue::PrivateLinkage,
      StrConst,
      ".str.autoload." + dllName
    );
    GV->setAlignment(Align(1));
    Builder.CreateCall(LoadLibFn, {GV});
  }

  if (!HasEntry) {
    registerTuInit(M, TuInit);
    errs() << "[MemLLVM] AutoLoadLibraryPass: deferred DLL preload to @"
           << TuInit->getName() << " (dispatched from the entry TU)\n";
  }

  return PreservedAnalyses::none();
}

} // namespace memllvm
