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

      if (!Ctors.empty() && EntryFn && !EntryFn->empty()) {
        std::stable_sort(Ctors.begin(), Ctors.end(), [](const CtorEntry &a, const CtorEntry &b) {
          return a.priority < b.priority;
        });

        Instruction *InsertBefore = &*EntryFn->getEntryBlock().getFirstInsertionPt();
        for (Instruction &I : EntryFn->getEntryBlock()) {
          if (auto *CI = dyn_cast<CallInst>(&I)) {
            if (CI->getCalledFunction() && CI->getCalledFunction()->getName() == "LoadLibraryA") {
              InsertBefore = I.getNextNode();
            }
          } else if (isa<StoreInst>(&I)) {
            InsertBefore = I.getNextNode();
          }
        }

        IRBuilder<> Builder(InsertBefore);
        for (const auto &item : Ctors) {
          errs() << "[MemLLVM] GlobalLifecyclePass: Inserting call to global constructor @" 
                 << item.func->getName() << " (Priority: " << item.priority << ")\n";
          Builder.CreateCall(item.func, {});
        }
        Changed = true;
      }

      CtorsVar->eraseFromParent();
      errs() << "[MemLLVM] GlobalLifecyclePass: Erased @llvm.global_ctors to eliminate .CRT$XCU section.\n";
      Changed = true;
    }
  }

  // Auto-initialize std::cout / std::cerr if referenced in entry module
  if (EntryFn && !EntryFn->empty()) {
    if (M.getGlobalVariable("?cout@std@@3V?$basic_ostream@DU?$char_traits@D@std@@@1@A") ||
        M.getGlobalVariable("?cerr@std@@3V?$basic_ostream@DU?$char_traits@D@std@@@1@A")) {
      LLVMContext &Ctx = M.getContext();
      FunctionCallee InitCoutFn = M.getOrInsertFunction("MemLLVM_InitCout",
          FunctionType::get(Type::getVoidTy(Ctx), false));
      Instruction *InsertBefore = &*EntryFn->getEntryBlock().getFirstInsertionPt();
      for (Instruction &I : EntryFn->getEntryBlock()) {
        if (auto *CI = dyn_cast<CallInst>(&I)) {
          if (CI->getCalledFunction() && CI->getCalledFunction()->getName() == "LoadLibraryA") {
            InsertBefore = I.getNextNode();
          }
        }
      }
      IRBuilder<> Builder(InsertBefore);
      Builder.CreateCall(InitCoutFn);
      Changed = true;
      errs() << "[MemLLVM] GlobalLifecyclePass: Injected MemLLVM_InitCout() for std::cout/cerr\n";
    }
  }

  // 2. Intercept and lower global destructors registered via atexit / __cxa_atexit
  struct DtorCall {
    Value *Fn;
    Value *Arg;
  };
  SmallVector<DtorCall, 8> Dtors;
  SmallVector<CallInst *, 8> AtexitCalls;

  for (Function &F : M) {
    if (F.isDeclaration()) continue;
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (auto *CI = dyn_cast<CallInst>(&I)) {
          Function *Callee = CI->getCalledFunction();
          if (Callee) {
            if (Callee->getName() == "atexit" && CI->arg_size() >= 1) {
              Value *FnArg = CI->getArgOperand(0);
              if (isa<Constant>(FnArg)) {
                Dtors.push_back({FnArg, nullptr});
                AtexitCalls.push_back(CI);
              }
            } else if (Callee->getName() == "__cxa_atexit" && CI->arg_size() >= 2) {
              Value *FnArg = CI->getArgOperand(0);
              Value *ObjArg = CI->getArgOperand(1);
              // Only lower across functions if both function and object pointer are constants (e.g. global objects)
              if (isa<Constant>(FnArg) && isa<Constant>(ObjArg)) {
                Dtors.push_back({FnArg, ObjArg});
                AtexitCalls.push_back(CI);
              }
            }
          }
        }
      }
    }
  }

  for (CallInst *CI : AtexitCalls) {
    CI->replaceAllUsesWith(ConstantInt::get(CI->getType(), 0));
    CI->eraseFromParent();
    Changed = true;
  }

  // Execute destructors in reverse LIFO order before EntryFn returns
  std::reverse(Dtors.begin(), Dtors.end());

  if (!Dtors.empty() && EntryFn && !EntryFn->empty()) {
    SmallVector<ReturnInst *, 4> RetInsts;
    for (BasicBlock &BB : *EntryFn) {
      if (auto *RI = dyn_cast<ReturnInst>(BB.getTerminator())) {
        RetInsts.push_back(RI);
      }
    }

    Type *PtrTy = PointerType::get(M.getContext(), 0);
    FunctionType *FT0 = FunctionType::get(Type::getVoidTy(M.getContext()), false);
    FunctionType *FT1 = FunctionType::get(Type::getVoidTy(M.getContext()), {PtrTy}, false);

    for (ReturnInst *RI : RetInsts) {
      IRBuilder<> Builder(RI);
      for (const auto &d : Dtors) {
        if (d.Arg) {
          Builder.CreateCall(FT1, d.Fn, {d.Arg});
        } else {
          Builder.CreateCall(FT0, d.Fn, {});
        }
        Changed = true;
      }
    }

    errs() << "[MemLLVM] GlobalLifecyclePass: Inserted " << Dtors.size() 
           << " reverse LIFO destructor calls before " << RetInsts.size() 
           << " return point(s) in @" << EntryFn->getName() << "\n";
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace memllvm
