#include "MemLLVM.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace memllvm {

PreservedAnalyses AutoMemoryPass::run(Module &M, ModuleAnalysisManager &MAM) {
  LLVMContext &Ctx = M.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *Int32Ty = Type::getInt32Ty(Ctx);
  Type *Int64Ty = Type::getInt64Ty(Ctx);

  bool Changed = false;

  auto getOrInsertGetProcessHeap = [&]() -> FunctionCallee {
    return M.getOrInsertFunction("GetProcessHeap", FunctionType::get(PtrTy, false));
  };
  auto getOrInsertHeapAlloc = [&]() -> FunctionCallee {
    return M.getOrInsertFunction("HeapAlloc", FunctionType::get(PtrTy, {PtrTy, Int32Ty, Int64Ty}, false));
  };
  auto getOrInsertHeapFree = [&]() -> FunctionCallee {
    return M.getOrInsertFunction("HeapFree", FunctionType::get(Int32Ty, {PtrTy, Int32Ty, PtrTy}, false));
  };

  auto synthesizeNew = [&](Function *F) {
    if (!F || !F->isDeclaration()) return;
    BasicBlock *BB = BasicBlock::Create(Ctx, "entry", F);
    IRBuilder<> B(BB);
    Value *SizeVal = F->getArg(0);
    if (SizeVal->getType() != Int64Ty) {
      SizeVal = B.CreateZExtOrTrunc(SizeVal, Int64Ty);
    }
    Value *IsZero = B.CreateICmpEQ(SizeVal, ConstantInt::get(Int64Ty, 0));
    Value *AllocSize = B.CreateSelect(IsZero, ConstantInt::get(Int64Ty, 1), SizeVal);

    Value *hHeap = B.CreateCall(getOrInsertGetProcessHeap(), {});
    Value *Alloc = B.CreateCall(getOrInsertHeapAlloc(), {hHeap, ConstantInt::get(Int32Ty, 0), AllocSize});
    B.CreateRet(Alloc);
    Changed = true;
    errs() << "[MemLLVM] AutoMemoryPass: Synthesized " << F->getName() << "\n";
  };

  auto synthesizeDelete = [&](Function *F) {
    if (!F || !F->isDeclaration()) return;
    BasicBlock *EntryBB = BasicBlock::Create(Ctx, "entry", F);
    BasicBlock *FreeBB = BasicBlock::Create(Ctx, "do_free", F);
    BasicBlock *RetBB = BasicBlock::Create(Ctx, "exit", F);

    IRBuilder<> B(EntryBB);
    Value *PtrVal = F->getArg(0);
    if (PtrVal->getType() != PtrTy) {
      PtrVal = B.CreatePointerCast(PtrVal, PtrTy);
    }
    Value *IsNull = B.CreateICmpEQ(PtrVal, ConstantPointerNull::get(cast<PointerType>(PtrTy)));
    B.CreateCondBr(IsNull, RetBB, FreeBB);

    B.SetInsertPoint(FreeBB);
    Value *hHeap = B.CreateCall(getOrInsertGetProcessHeap(), {});
    B.CreateCall(getOrInsertHeapFree(), {hHeap, ConstantInt::get(Int32Ty, 0), PtrVal});
    B.CreateBr(RetBB);

    B.SetInsertPoint(RetBB);
    if (F->getReturnType()->isVoidTy()) {
      B.CreateRetVoid();
    } else {
      B.CreateRet(Constant::getNullValue(F->getReturnType()));
    }
    Changed = true;
    errs() << "[MemLLVM] AutoMemoryPass: Synthesized " << F->getName() << "\n";
  };

  const char *newFns[] = {
    "??2@YAPEAX_K@Z",
    "??_U@YAPEAX_K@Z",
    "??2@YAPEAX_KAEBUnothrow_t@std@@@Z",
    "??_U@YAPEAX_KAEBUnothrow_t@std@@@Z"
  };
  for (const char *name : newFns) {
    if (Function *F = M.getFunction(name)) {
      synthesizeNew(F);
    }
  }

  const char *delFns[] = {
    "??3@YAXPEAX@Z",
    "??_V@YAXPEAX@Z",
    "??3@YAXPEAX_K@Z",
    "??_V@YAXPEAX_K@Z",
    "??3@YAXPEAXAEBUnothrow_t@std@@@Z",
    "??_V@YAXPEAXAEBUnothrow_t@std@@@Z"
  };
  for (const char *name : delFns) {
    if (Function *F = M.getFunction(name)) {
      synthesizeDelete(F);
    }
  }

  if (Function *F = M.getFunction("malloc")) {
    synthesizeNew(F);
  }
  if (Function *F = M.getFunction("free")) {
    synthesizeDelete(F);
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace memllvm
