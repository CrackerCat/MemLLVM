#include "MemLLVM.h"
#include "MemLLVMUtils.h"
#include "llvm/ADT/SmallVector.h"
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

#include <algorithm>
#include <vector>

using namespace llvm;

namespace memllvm {

PreservedAnalyses GlobalLifecyclePass::run(Module &M, ModuleAnalysisManager &MAM) {
  bool Changed = false;
  Function *EntryFn = findOrSynthesizeEntryPoint(M);

  // 1. Process Global Constructors (@llvm.global_ctors)
  GlobalVariable *CtorsVar = M.getGlobalVariable("llvm.global_ctors");
  if (CtorsVar && CtorsVar->hasInitializer()) {
    if (ConstantArray *Init = dyn_cast<ConstantArray>(CtorsVar->getInitializer())) {
      struct CtorEntry {
        int32_t priority;
        Function *func;
      };
      std::vector<CtorEntry> Ctors;

      for (unsigned i = 0; i < Init->getNumOperands(); ++i) {
        ConstantStruct *CS = dyn_cast<ConstantStruct>(Init->getOperand(i));
        if (!CS || CS->getNumOperands() < 2) continue;
        ConstantInt *PriorityCI = dyn_cast<ConstantInt>(CS->getOperand(0));
        Function *CtorFn = dyn_cast<Function>(CS->getOperand(1)->stripPointerCasts());
        if (PriorityCI && CtorFn && !CtorFn->isDeclaration()) {
          Ctors.push_back({(int32_t)PriorityCI->getSExtValue(), CtorFn});
        }
      }

      std::stable_sort(Ctors.begin(), Ctors.end(), [](const CtorEntry &a, const CtorEntry &b) {
        return a.priority < b.priority;
      });

      if (EntryFn && !EntryFn->empty()) {
        // Fixed anchor at start of entry function, after companion resource loader preamble
        Instruction *InsertBefore = &*EntryFn->getEntryBlock().getFirstInsertionPt();
        for (Instruction &I : EntryFn->getEntryBlock()) {
          if (auto *CI = dyn_cast<CallInst>(&I)) {
            if (getDirectCallee(*CI) && getDirectCallee(*CI)->getName() == "LoadLibraryA") {
              InsertBefore = I.getNextNode();
            }
          }
        }

        IRBuilder<> Builder(InsertBefore);

        // First dispatch cross-TU initializers registered in .mli section (GlobalPointerPass, non-entry ctors, DLL preloads)
        FunctionCallee DispatchInitsFn = M.getOrInsertFunction("__memllvm_dispatch_tu_inits",
            FunctionType::get(Type::getVoidTy(M.getContext()), false));
        Builder.CreateCall(DispatchInitsFn);
        if (getenv("MEMLLVM_VERBOSE")) {
          errs() << "[MemLLVM] GlobalLifecyclePass: Injected __memllvm_dispatch_tu_inits() at entry\n";
        }

        for (const auto &item : Ctors) {
          if (getenv("MEMLLVM_VERBOSE")) {
            errs() << "[MemLLVM] GlobalLifecyclePass: Inserting call to global constructor @" 
                   << item.func->getName() << " (Priority: " << item.priority << ")\n";
          }
          Builder.CreateCall(item.func, {});
        }
        CtorsVar->eraseFromParent();
        if (getenv("MEMLLVM_VERBOSE")) {
          errs() << "[MemLLVM] GlobalLifecyclePass: Erased @llvm.global_ctors to eliminate .CRT$XCU section in entry TU.\n";
        }
        Changed = true;
      } else {
        // Non-entry TU: dispatch constructors via per-TU init function in .mli section
        Function *TuInit = getOrCreateTuInit(M);
        Instruction *InsertBefore = TuInit->getEntryBlock().getTerminator();
        IRBuilder<> Builder(InsertBefore);
        for (const auto &item : Ctors) {
          if (getenv("MEMLLVM_VERBOSE")) {
            errs() << "[MemLLVM] GlobalLifecyclePass: Appending call to global constructor @" 
                   << item.func->getName() << " (Priority: " << item.priority << ") into TU init\n";
          }
          Builder.CreateCall(item.func, {});
        }
        registerTuInit(M, TuInit);
        CtorsVar->eraseFromParent();
        Changed = true;
      }
    }
  }

  // 1b. If entry module has no global constructors of its own, still ensure __memllvm_dispatch_tu_inits() is called!
  if (EntryFn && !EntryFn->empty() && (!CtorsVar || !CtorsVar->hasInitializer())) {
    Instruction *InsertBefore = &*EntryFn->getEntryBlock().getFirstInsertionPt();
    for (Instruction &I : EntryFn->getEntryBlock()) {
      if (auto *CI = dyn_cast<CallInst>(&I)) {
        if (getDirectCallee(*CI) && getDirectCallee(*CI)->getName() == "LoadLibraryA") {
          InsertBefore = I.getNextNode();
        }
      }
    }
    IRBuilder<> Builder(InsertBefore);
    FunctionCallee DispatchInitsFn = M.getOrInsertFunction("__memllvm_dispatch_tu_inits",
        FunctionType::get(Type::getVoidTy(M.getContext()), false));
    Builder.CreateCall(DispatchInitsFn);
    if (getenv("MEMLLVM_VERBOSE")) {
      errs() << "[MemLLVM] GlobalLifecyclePass: Injected __memllvm_dispatch_tu_inits() at entry (no local ctors)\n";
    }
    Changed = true;
  }

  // Auto-initialize std::cout / std::cerr if referenced in entry module
  if (EntryFn && !EntryFn->empty()) {
    if (M.getGlobalVariable("?cout@std@@3V?$basic_ostream@DU?$char_traits@D@std@@@1@A") ||
        M.getGlobalVariable("?cerr@std@@3V?$basic_ostream@DU?$char_traits@D@std@@@1@A") ||
        M.getGlobalVariable("?cin@std@@3V?$basic_istream@DU?$char_traits@D@std@@@1@A")) {
      LLVMContext &Ctx = M.getContext();
      FunctionCallee InitCoutFn = M.getOrInsertFunction("MemLLVM_InitCout",
          FunctionType::get(Type::getVoidTy(Ctx), false));
      Instruction *InsertBefore = &*EntryFn->getEntryBlock().getFirstInsertionPt();
      for (Instruction &I : EntryFn->getEntryBlock()) {
        if (auto *CI = dyn_cast<CallInst>(&I)) {
          if (getDirectCallee(*CI) && getDirectCallee(*CI)->getName() == "LoadLibraryA") {
            InsertBefore = I.getNextNode();
          }
        }
      }
      IRBuilder<> Builder(InsertBefore);
      Builder.CreateCall(InitCoutFn);
      Changed = true;
      if (getenv("MEMLLVM_VERBOSE")) {
        errs() << "[MemLLVM] GlobalLifecyclePass: Injected MemLLVM_InitCout() for std::cout/cerr\n";
      }
    }
  }

  // 2. Ensure _doexit() is invoked before entry function returns
  if (EntryFn && !EntryFn->empty()) {
    SmallVector<ReturnInst *, 4> RetInsts;
    for (BasicBlock &BB : *EntryFn) {
      if (auto *RI = dyn_cast<ReturnInst>(BB.getTerminator())) {
        RetInsts.push_back(RI);
      }
    }

    if (!RetInsts.empty()) {
      FunctionCallee DoExitFn = M.getOrInsertFunction("_doexit",
          FunctionType::get(Type::getVoidTy(M.getContext()), false));
      for (ReturnInst *RI : RetInsts) {
        IRBuilder<> Builder(RI);
        Builder.CreateCall(DoExitFn);
        Changed = true;
      }
      if (getenv("MEMLLVM_VERBOSE")) {
        errs() << "[MemLLVM] GlobalLifecyclePass: Inserted _doexit() call before "
               << RetInsts.size() << " return point(s) in @" << EntryFn->getName() << "\n";
      }
    }
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace memllvm
