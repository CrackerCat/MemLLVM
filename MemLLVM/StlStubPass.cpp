#include "MemLLVM.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace memllvm {

PreservedAnalyses StlStubPass::run(Module &M, ModuleAnalysisManager &MAM) {
  LLVMContext &Ctx = M.getContext();
  Type *Int32Ty = Type::getInt32Ty(Ctx);
  bool Changed = false;

  auto getOrInsertExitProcess = [&]() -> FunctionCallee {
    return M.getOrInsertFunction("ExitProcess", FunctionType::get(Type::getVoidTy(Ctx), {Int32Ty}, false));
  };

  auto synthesizeAbort = [&](Function *F, uint32_t code) {
    if (!F || !F->isDeclaration()) return;
    Comdat *C = M.getOrInsertComdat(F->getName());
    C->setSelectionKind(Comdat::Any);
    F->setComdat(C);
    F->setLinkage(GlobalValue::LinkOnceODRLinkage);

    BasicBlock *BB = BasicBlock::Create(Ctx, "entry", F);
    IRBuilder<> B(BB);
    B.CreateCall(getOrInsertExitProcess(), {ConstantInt::get(Int32Ty, code)});
    B.CreateUnreachable();
    Changed = true;
    if (getenv("MEMLLVM_VERBOSE")) {
      errs() << "[MemLLVM] StlStubPass: Synthesized fatal stub " << F->getName() 
             << " (ExitProcess 0x" << Twine::utohexstr(code) << ")\n";
    }
  };

  auto synthesizeNoop = [&](Function *F) {
    if (!F || !F->isDeclaration()) return;
    Comdat *C = M.getOrInsertComdat(F->getName());
    C->setSelectionKind(Comdat::Any);
    F->setComdat(C);
    F->setLinkage(GlobalValue::LinkOnceODRLinkage);

    BasicBlock *BB = BasicBlock::Create(Ctx, "entry", F);
    IRBuilder<> B(BB);
    if (F->getReturnType()->isVoidTy()) {
      B.CreateRetVoid();
    } else {
      B.CreateRet(Constant::getNullValue(F->getReturnType()));
    }
    Changed = true;
    if (getenv("MEMLLVM_VERBOSE")) {
      errs() << "[MemLLVM] StlStubPass: Synthesized no-op stub " << F->getName() << "\n";
    }
  };

  struct FatalStubEntry {
    const char *name;
    uint32_t code;
  };

  FatalStubEntry stubs[] = {
    {"?_Xbad_alloc@std@@YAXXZ",               0xC0000017},
    {"?_Throw_bad_alloc@std@@YAXXZ",           0xC0000017},
    {"?_Throw_bad_array_new_length@std@@YAXXZ",0xC0000017},
    {"?_Xlength_error@std@@YAXPEBD@Z",         0xC0000005},
    {"?_Xout_of_range@std@@YAXPEBD@Z",         0xC000008C},
    {"?_Xinvalid_argument@std@@YAXPEBD@Z",     0xC000000D},
    {"?_Xoverflow_error@std@@YAXPEBD@Z",       0xC0000095},
    {"?_Xruntime_error@std@@YAXPEBD@Z",        0xC0000001},
    {"_invalid_parameter_noinfo_noreturn",     0xC000000D},
    {"_purecall",                              0xC0000025},
    {"?_Xregex_error@std@@YAXW4error_type@regex_constants@1@@Z", 0xC0000002},
    {"?_Xbad_function_call@std@@YAXXZ",        0xC0000003}
  };

  for (const auto &s : stubs) {
    if (Function *F = M.getFunction(s.name)) {
      synthesizeAbort(F, s.code);
    }
  }

  const char *noopFns[] = {
    "__std_exception_copy",
    "__std_exception_destroy",
    "?_Set_raise_handler@std@@YAP6AXAEBVexception@stdext@@@ZP6AX0@Z@Z",
    "?uncaught_exception@std@@YA_NXZ",
    "?uncaught_exceptions@std@@YAHXZ",
    "_Init_thread_lock",
    "_Init_thread_unlock"
  };
  for (const char *name : noopFns) {
    if (Function *F = M.getFunction(name)) {
      synthesizeNoop(F);
    }
  }

  // Synthesize std::_Raise_handler global function pointer if declared
  if (GlobalVariable *GV = M.getGlobalVariable("?_Raise_handler@std@@3P6AXAEBVexception@stdext@@@ZEA")) {
    if (GV->isDeclaration()) {
      GV->setInitializer(ConstantPointerNull::get(cast<PointerType>(GV->getValueType())));
      GV->setConstant(false);
      GV->setLinkage(GlobalValue::InternalLinkage);
      Changed = true;
      errs() << "[MemLLVM] StlStubPass: Synthesized std::_Raise_handler = nullptr\n";
    }
  }

  // Synthesize const type_info::`vftable' dummy pointer if declared
  if (GlobalVariable *GV = M.getGlobalVariable("??_7type_info@@6B@")) {
    if (GV->isDeclaration()) {
      Comdat *C = M.getOrInsertComdat(GV->getName());
      C->setSelectionKind(Comdat::Any);
      GV->setComdat(C);
      GV->setLinkage(GlobalValue::LinkOnceODRLinkage);
      GV->setInitializer(Constant::getNullValue(GV->getValueType()));
      Changed = true;
      errs() << "[MemLLVM] StlStubPass: Synthesized type_info::`vftable`\n";
    }
  }

  // =========================================================================
  // 1. Thread-Safe Statics Support: Localize _Init_thread_epoch and synthesize helpers
  // =========================================================================
  if (GlobalVariable *EpochGV = M.getGlobalVariable("_Init_thread_epoch")) {
    EpochGV->setThreadLocal(false);
    EpochGV->setInitializer(ConstantInt::get(Int32Ty, 0x80000000));
    EpochGV->setLinkage(GlobalValue::InternalLinkage);
    Changed = true;
    errs() << "[MemLLVM] StlStubPass: Localized _Init_thread_epoch to non-TLS global\n";
  }

  if (Function *F = M.getFunction("_Init_thread_header")) {
    if (F->isDeclaration()) {
      Comdat *C = M.getOrInsertComdat(F->getName());
      C->setSelectionKind(Comdat::Any);
      F->setComdat(C);
      F->setLinkage(GlobalValue::LinkOnceODRLinkage);

      BasicBlock *BB = BasicBlock::Create(Ctx, "entry", F);
      BasicBlock *SetBB = BasicBlock::Create(Ctx, "init", F);
      BasicBlock *RetBB = BasicBlock::Create(Ctx, "done", F);

      IRBuilder<> B(BB);
      Value *pOnce = F->getArg(0);
      Value *Val = B.CreateLoad(Int32Ty, pOnce);
      Value *IsZero = B.CreateICmpEQ(Val, ConstantInt::get(Int32Ty, 0));
      B.CreateCondBr(IsZero, SetBB, RetBB);

      B.SetInsertPoint(SetBB);
      B.CreateStore(ConstantInt::get(Int32Ty, -1), pOnce);
      B.CreateBr(RetBB);

      B.SetInsertPoint(RetBB);
      B.CreateRetVoid();
      Changed = true;
      errs() << "[MemLLVM] StlStubPass: Synthesized _Init_thread_header\n";
    }
  }

  if (Function *F = M.getFunction("_Init_thread_footer")) {
    if (F->isDeclaration()) {
      Comdat *C = M.getOrInsertComdat(F->getName());
      C->setSelectionKind(Comdat::Any);
      F->setComdat(C);
      F->setLinkage(GlobalValue::LinkOnceODRLinkage);

      BasicBlock *BB = BasicBlock::Create(Ctx, "entry", F);
      IRBuilder<> B(BB);
      Value *pOnce = F->getArg(0);
      B.CreateStore(ConstantInt::get(Int32Ty, 1), pOnce);
      if (GlobalVariable *EpochGV = M.getGlobalVariable("_Init_thread_epoch")) {
        B.CreateStore(ConstantInt::get(Int32Ty, 1), EpochGV);
      }
      B.CreateRetVoid();
      Changed = true;
      errs() << "[MemLLVM] StlStubPass: Synthesized _Init_thread_footer\n";
    }
  }

  if (Function *F = M.getFunction("_Init_thread_abort")) {
    if (F->isDeclaration()) {
      Comdat *C = M.getOrInsertComdat(F->getName());
      C->setSelectionKind(Comdat::Any);
      F->setComdat(C);
      F->setLinkage(GlobalValue::LinkOnceODRLinkage);

      BasicBlock *BB = BasicBlock::Create(Ctx, "entry", F);
      IRBuilder<> B(BB);
      Value *pOnce = F->getArg(0);
      B.CreateStore(ConstantInt::get(Int32Ty, 0), pOnce);
      B.CreateRetVoid();
      Changed = true;
      errs() << "[MemLLVM] StlStubPass: Synthesized _Init_thread_abort\n";
    }
  }

  // =========================================================================
  // 2. Real atexit / __cxa_atexit are provided by LTL runtime (crt_mem.cpp)
  // =========================================================================

  // =========================================================================
  // 3. Native Emulated TLS: Lower llvm.threadlocal.address to MemLLVM_GetThreadLocalAddress
  // =========================================================================
  SmallVector<Function *, 4> TlsIntrinsicFns;
  for (Function &F : M) {
    if (F.getName().starts_with("llvm.threadlocal.address")) {
      TlsIntrinsicFns.push_back(&F);
    }
  }

  if (!TlsIntrinsicFns.empty()) {
    Type *PtrTy = PointerType::getUnqual(Ctx);
    Type *Int64Ty = Type::getInt64Ty(Ctx);
    FunctionCallee GetTlsCallee = M.getOrInsertFunction(
        "MemLLVM_GetThreadLocalAddress",
        FunctionType::get(PtrTy, {PtrTy, Int64Ty, PtrTy}, false));

    for (Function *TlsFn : TlsIntrinsicFns) {
      SmallVector<CallInst *, 16> TlsCalls;
      for (User *U : TlsFn->users()) {
        if (auto *CI = dyn_cast<CallInst>(U)) {
          TlsCalls.push_back(CI);
        }
      }

      for (CallInst *CI : TlsCalls) {
        Value *Arg = CI->getArgOperand(0);
        auto *GV = dyn_cast<GlobalVariable>(Arg->stripPointerCasts());
        if (!GV) continue;

        Type *ValTy = GV->getValueType();
        uint64_t AllocSize = M.getDataLayout().getTypeAllocSize(ValTy);

        // 1. Get or create index GV
        std::string IndexName = (GV->getName() + ".tl_index").str();
        GlobalVariable *IndexGV = M.getGlobalVariable(IndexName);
        if (!IndexGV) {
          IndexGV = new GlobalVariable(
              M, Int32Ty, false, GlobalValue::InternalLinkage,
              ConstantInt::get(Int32Ty, 0), IndexName);
        }

        // 2. Get or create template GV
        std::string TemplName = (GV->getName() + ".tl_templ").str();
        GlobalVariable *TemplGV = M.getGlobalVariable(TemplName);
        if (!TemplGV) {
          Constant *Init = GV->hasInitializer() ? GV->getInitializer() : Constant::getNullValue(ValTy);
          TemplGV = new GlobalVariable(
              M, ValTy, true, GlobalValue::InternalLinkage,
              Init, TemplName);
        }

        // 3. Replace CI with MemLLVM_GetThreadLocalAddress(IndexGV, AllocSize, TemplGV)
        IRBuilder<> B(CI);
        Value *Res = B.CreateCall(GetTlsCallee, {IndexGV, ConstantInt::get(Int64Ty, AllocSize), TemplGV});
        CI->replaceAllUsesWith(Res);
        CI->eraseFromParent();
        Changed = true;
        errs() << "[MemLLVM] StlStubPass: Lowered thread_local access for @" << GV->getName() << "\n";
      }
    }

    // Clear thread_local attribute on all globals so backend doesn't emit TLS sections or emutls
    for (GlobalVariable &GV : M.globals()) {
      if (GV.isThreadLocal()) {
        GV.setThreadLocal(false);
      }
    }
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace memllvm
