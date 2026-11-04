#include "MemLLVM.h"
#include "MemLLVMUtils.h"
#include "SdkImportResolver.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
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
#include <string>

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
  const bool HasEntry = EntryFn && !EntryFn->empty();
  // NOTE: no early return here. A TU without an entry point (sqlite3.c and most
  // library TUs) still owns global pointer tables that must be filled at run
  // time; those stores go into a per-TU init function which the entry TU
  // dispatches through the .mli$M table.

  struct PointerRebaseEntry {
    GlobalVariable *GV;
    SmallVector<uint32_t, 4> Path;
    Constant *Target;
    // Non-zero when the slot points at an address-taken Win32 export. Such a
    // slot cannot be filled with a link-time address (the symbol does not exist
    // in a flat image), so it is instead populated at entry time through the
    // PEB + ROR13 resolver.
    uint32_t DynamicHash = 0;
    std::string DynamicName;
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

        // Address-taken external API, e.g. sqlite3's `aSyscall` table, which
        // stores &CreateFileW / &WriteFile / &HeapAlloc as data. ApiResolvePass
        // only rewrites call sites, so without this these references survive
        // all the way to the linker as undefined imports.
        if (auto *F = dyn_cast<Function>(C->stripPointerCasts())) {
          if (F->isDeclaration() && !F->isIntrinsic() &&
              !F->getName().starts_with("llvm.") &&
              F->getName() != "MemLLVM_GetApiAddress" &&
              !isStaticShimSymbol(F->getName()) &&
              isKnownWindowsApi(F->getName())) {
            Entry.DynamicHash = calculateRor13(F->getName());
            Entry.DynamicName = F->getName().str();
          }
        }

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
    if (GV.getName().starts_with("__memllvm_init_slot_"))
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

  Instruction *InsertBefore = nullptr;
  Function *TuInit = nullptr;
  if (HasEntry) {
    InsertBefore = &*EntryFn->getEntryBlock().getFirstInsertionPt();
    for (Instruction &I : EntryFn->getEntryBlock()) {
      if (auto *CI = dyn_cast<CallInst>(&I)) {
        if (getDirectCallee(*CI) && getDirectCallee(*CI)->getName() == "LoadLibraryA") {
          InsertBefore = I.getNextNode();
        }
      }
    }
  } else {
    TuInit = getOrCreateTuInit(M);
    InsertBefore = &*TuInit->getEntryBlock().getFirstInsertionPt();
  }

  LLVMContext &Ctx = M.getContext();
  Type *Int32Ty = Type::getInt32Ty(Ctx);

  IRBuilder<> Builder(InsertBefore);
  Function *TargetFn = InsertBefore->getFunction();
  if (InsertBefore->getDebugLoc()) {
    Builder.SetCurrentDebugLocation(InsertBefore->getDebugLoc());
  } else if (TargetFn && TargetFn->getSubprogram()) {
    Builder.SetCurrentDebugLocation(DILocation::get(Ctx, 0, 0, TargetFn->getSubprogram()));
  }

  // Declared lazily: only modules with address-taken APIs need it, and
  // ApiResolvePass links the real body afterwards.
  FunctionCallee ResolverFn;
  bool HaveResolver = false;
  for (auto &Entry : Rebases) {
    if (Entry.DynamicHash) { HaveResolver = true; break; }
  }
  if (HaveResolver) {
    ResolverFn = M.getOrInsertFunction(
        "MemLLVM_GetApiAddress",
        FunctionType::get(PointerType::get(Ctx, 0), {Int32Ty}, false));
    unsigned NumDynamic = 0;
    for (auto &Entry : Rebases) if (Entry.DynamicHash) ++NumDynamic;
    errs() << "[MemLLVM] GlobalPointerPass: resolving " << NumDynamic
           << " address-taken API pointer(s) via PEB/ROR13 at entry\n";
  }

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

    if (Entry.DynamicHash) {
      Value *FnPtr = Builder.CreateCall(
          ResolverFn, {ConstantInt::get(Int32Ty, Entry.DynamicHash)},
          Entry.DynamicName + ".ptr");
      Builder.CreateStore(FnPtr, SlotPtr);
    } else {
      Builder.CreateStore(Entry.Target, SlotPtr);
    }
  }

  for (GlobalVariable *GV : ModifiedGVs) {
    errs() << "[MemLLVM] Auto-rebasing global/vtable: @" << GV->getName() << "\n";
    // Replace only pointer leaves with null, preserving all non-pointer fields (integers, enums, floats)
    Constant *CleanInit = replacePointerLeavesWithNull(GV->getInitializer());
    GV->setInitializer(CleanInit);
    // Mark non-constant so LLVM optimizer does not constant-fold loads from dynamic rebased slots to null,
    // and prevent LLVM's ConstantMerge from collapsing distinct vtables sharing identical zero-initializers.
    GV->setConstant(false);
    GV->setUnnamedAddr(GlobalValue::UnnamedAddr::None);
    if (GV->hasSection() && GV->getSection().starts_with(".text")) {
      GV->setSection("");
    }
  }

  if (!HasEntry) {
    registerTuInit(M, TuInit);
    errs() << "[MemLLVM] GlobalPointerPass: initialisation deferred to @"
           << TuInit->getName() << " (dispatched from the entry TU)\n";
  }

  return PreservedAnalyses::none();
}

} // namespace memllvm
