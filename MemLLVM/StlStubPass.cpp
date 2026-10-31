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
    BasicBlock *BB = BasicBlock::Create(Ctx, "entry", F);
    IRBuilder<> B(BB);
    B.CreateCall(getOrInsertExitProcess(), {ConstantInt::get(Int32Ty, code)});
    B.CreateUnreachable();
    Changed = true;
    errs() << "[MemLLVM] StlStubPass: Synthesized fatal stub " << F->getName() 
           << " (ExitProcess 0x" << Twine::utohexstr(code) << ")\n";
  };

  auto synthesizeNoop = [&](Function *F) {
    if (!F || !F->isDeclaration()) return;
    BasicBlock *BB = BasicBlock::Create(Ctx, "entry", F);
    IRBuilder<> B(BB);
    if (F->getReturnType()->isVoidTy()) {
      B.CreateRetVoid();
    } else {
      B.CreateRet(Constant::getNullValue(F->getReturnType()));
    }
    Changed = true;
    errs() << "[MemLLVM] StlStubPass: Synthesized no-op stub " << F->getName() << "\n";
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
    {"_CxxThrowException",                     0xE06D7363},
    {"_purecall",                              0xC0000025}
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
  // 2. Safe Fallback for atexit / __cxa_atexit
  // =========================================================================
  const char *atexitNames[] = {"atexit", "__cxa_atexit"};
  for (const char *name : atexitNames) {
    if (Function *F = M.getFunction(name)) {
      if (F->isDeclaration()) {
        BasicBlock *BB = BasicBlock::Create(Ctx, "entry", F);
        IRBuilder<> B(BB);
        B.CreateRet(ConstantInt::get(F->getReturnType(), 0));
        Changed = true;
        errs() << "[MemLLVM] StlStubPass: Synthesized safe fallback " << F->getName() << "\n";
      }
    }
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace memllvm
