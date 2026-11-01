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

#include <functional>
#include <set>

using namespace llvm;

namespace memllvm {

static Constant *replacePointerLeavesWithNull(Constant *C) {
  if (!C) return nullptr;

  if (C->getType()->isPointerTy()) {
    if (isa<GlobalValue>(C) || isa<ConstantExpr>(C)) {
      return ConstantPointerNull::get(cast<PointerType>(C->getType()));
    }
    return C;
  }

  if (auto *CA = dyn_cast<ConstantArray>(C)) {
    SmallVector<Constant *, 8> Elements;
    bool Changed = false;
    for (unsigned i = 0; i < CA->getNumOperands(); ++i) {
      Constant *op = CA->getOperand(i);
      Constant *newOp = replacePointerLeavesWithNull(op);
      if (newOp != op) Changed = true;
      Elements.push_back(newOp);
    }
    return Changed ? ConstantArray::get(CA->getType(), Elements) : CA;
  }

  if (auto *CS = dyn_cast<ConstantStruct>(C)) {
    SmallVector<Constant *, 8> Elements;
    bool Changed = false;
    for (unsigned i = 0; i < CS->getNumOperands(); ++i) {
      Constant *op = CS->getOperand(i);
      Constant *newOp = replacePointerLeavesWithNull(op);
      if (newOp != op) Changed = true;
      Elements.push_back(newOp);
    }
    return Changed ? ConstantStruct::get(CS->getType(), Elements) : CS;
  }

  return C;
}

PreservedAnalyses GlobalPointerPass::run(Module &M, ModuleAnalysisManager &MAM) {
  Function *EntryFn = findOrSynthesizeEntryPoint(M);
  if (!EntryFn || EntryFn->empty())
    return PreservedAnalyses::all();

  struct PointerRebaseEntry {
    GlobalVariable *GV;
    SmallVector<uint32_t, 4> Path;
    Constant *Target;
  };

  std::function<void(GlobalVariable *, Constant *, SmallVector<uint32_t, 4> &, SmallVector<PointerRebaseEntry, 16> &)>
      collectRebases = [&](GlobalVariable *GV, Constant *C, SmallVector<uint32_t, 4> &CurPath,
                           SmallVector<PointerRebaseEntry, 16> &Out) {
    if (!C) return;

    if (C->getType()->isPointerTy()) {
      if (isa<GlobalValue>(C) || isa<ConstantExpr>(C)) {
        PointerRebaseEntry Entry;
        Entry.GV = GV;
        Entry.Path = CurPath;
        Entry.Target = C;
        Out.push_back(Entry);
      }
      return;
    }

    if (auto *CA = dyn_cast<ConstantArray>(C)) {
      for (unsigned i = 0; i < CA->getNumOperands(); ++i) {
        CurPath.push_back(i);
        collectRebases(GV, CA->getOperand(i), CurPath, Out);
        CurPath.pop_back();
      }
      return;
    }

    if (auto *CS = dyn_cast<ConstantStruct>(C)) {
      for (unsigned i = 0; i < CS->getNumOperands(); ++i) {
        CurPath.push_back(i);
        collectRebases(GV, CS->getOperand(i), CurPath, Out);
        CurPath.pop_back();
      }
      return;
    }
  };

  SmallVector<PointerRebaseEntry, 16> Rebases;
  std::set<GlobalVariable *> ModifiedGVs;

  for (GlobalVariable &GV : M.globals()) {
    if (!GV.hasInitializer())
      continue;
    if (GV.getName().starts_with("llvm."))
      continue;

    Constant *Init = GV.getInitializer();
    if (!Init)
      continue;

    SmallVector<uint32_t, 4> CurPath;
    size_t prevCount = Rebases.size();
    collectRebases(&GV, Init, CurPath, Rebases);
    if (Rebases.size() > prevCount) {
      ModifiedGVs.insert(&GV);
    }
  }

  if (Rebases.empty())
    return PreservedAnalyses::all();

  errs() << "[MemLLVM] Running GlobalPointerPass: auto-rebasing " 
         << Rebases.size() << " pointer slot(s) across " << ModifiedGVs.size() << " global(s)\n";

  Instruction *InsertBefore = &*EntryFn->getEntryBlock().getFirstInsertionPt();
  for (Instruction &I : EntryFn->getEntryBlock()) {
    if (auto *CI = dyn_cast<CallInst>(&I)) {
      if (CI->getCalledFunction() && CI->getCalledFunction()->getName() == "LoadLibraryA") {
        InsertBefore = I.getNextNode();
      }
    }
  }

  IRBuilder<> Builder(InsertBefore);
  LLVMContext &Ctx = M.getContext();
  Type *Int32Ty = Type::getInt32Ty(Ctx);

  for (auto &Entry : Rebases) {
    Value *SlotPtr = nullptr;
    if (Entry.Path.empty()) {
      SlotPtr = Entry.GV;
    } else {
      SmallVector<Value *, 5> GepIndices;
      GepIndices.push_back(ConstantInt::get(Int32Ty, 0));
      for (uint32_t idx : Entry.Path) {
        GepIndices.push_back(ConstantInt::get(Int32Ty, idx));
      }
      SlotPtr = Builder.CreateInBoundsGEP(Entry.GV->getValueType(), Entry.GV, GepIndices);
    }
    Builder.CreateStore(Entry.Target, SlotPtr);
  }

  for (GlobalVariable *GV : ModifiedGVs) {
    errs() << "[MemLLVM] Auto-rebasing global/vtable: @" << GV->getName() << "\n";
    // Replace only pointer leaves with null, preserving all non-pointer fields (integers, enums, floats)
    Constant *CleanInit = replacePointerLeavesWithNull(GV->getInitializer());
    GV->setInitializer(CleanInit);
    // Keep GV constness as-is so COMDAT section names (.rdata vs .data) remain consistent across TUs
    if (GV->hasSection() && GV->getSection().starts_with(".text")) {
      GV->setSection("");
    }
  }

  return PreservedAnalyses::none();
}

} // namespace memllvm
