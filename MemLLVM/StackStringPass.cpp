#include "MemLLVM.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
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
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include <cstring>
#include <set>

using namespace llvm;

namespace memllvm {

static bool canTransformGlobalString(GlobalVariable *GV) {
  if (!GV->hasInitializer() || !GV->isConstant())
    return false;
  if (GV->getName().starts_with("llvm."))
    return false;

  Constant *Init = GV->getInitializer();
  if (!Init)
    return false;

  SmallVector<uint8_t, 64> Bytes;
  if (auto *CDS = dyn_cast<ConstantDataSequential>(Init)) {
    if (!CDS->getElementType()->isIntegerTy(8) && !CDS->getElementType()->isIntegerTy(16))
      return false;
    StringRef Raw = CDS->getRawDataValues();
    Bytes.append(Raw.bytes_begin(), Raw.bytes_end());
  } else if (auto *CA = dyn_cast<ConstantArray>(Init)) {
    if (!CA->getType()->getElementType()->isIntegerTy(8) && !CA->getType()->getElementType()->isIntegerTy(16))
      return false;
    for (unsigned i = 0; i < CA->getNumOperands(); ++i) {
      if (auto *CI = dyn_cast<ConstantInt>(CA->getOperand(i))) {
        Bytes.push_back(static_cast<uint8_t>(CI->getZExtValue()));
      } else {
        return false;
      }
    }
  } else {
    return false;
  }

  if (Bytes.empty() || Bytes.size() > 256)
    return false;

  bool isStringSymbol = GV->getName().starts_with("??_C@") || 
                        GV->getName().contains(".str") ||
                        (Bytes.size() > 0 && Bytes.back() == 0);
  if (!isStringSymbol)
    return false;

  SmallVector<User *, 16> Users(GV->user_begin(), GV->user_end());
  while (!Users.empty()) {
    User *U = Users.pop_back_val();
    if (auto *I = dyn_cast<Instruction>(U)) {
      if (!I->getFunction())
        return false;
    } else if (auto *CE = dyn_cast<ConstantExpr>(U)) {
      for (User *SubU : CE->users())
        Users.push_back(SubU);
    } else {
      return false;
    }
  }

  return true;
}

static bool transformGlobalString(GlobalVariable *GV, Module &M) {
  Constant *Init = GV->getInitializer();
  SmallVector<uint8_t, 64> Bytes;
  if (auto *CDS = dyn_cast<ConstantDataSequential>(Init)) {
    StringRef Raw = CDS->getRawDataValues();
    Bytes.append(Raw.bytes_begin(), Raw.bytes_end());
  } else if (auto *CA = dyn_cast<ConstantArray>(Init)) {
    for (unsigned i = 0; i < CA->getNumOperands(); ++i) {
      auto *CI = cast<ConstantInt>(CA->getOperand(i));
      Bytes.push_back(static_cast<uint8_t>(CI->getZExtValue()));
    }
  }

  size_t N = Bytes.size();
  if (N == 0)
    return false;

  std::set<Function *> TargetFunctions;
  SmallVector<User *, 16> WorkList(GV->user_begin(), GV->user_end());
  while (!WorkList.empty()) {
    User *U = WorkList.pop_back_val();
    if (auto *I = dyn_cast<Instruction>(U)) {
      if (Function *F = I->getFunction())
        TargetFunctions.insert(F);
    } else if (auto *CE = dyn_cast<ConstantExpr>(U)) {
      for (User *SubU : CE->users())
        WorkList.push_back(SubU);
    }
  }

  if (TargetFunctions.empty())
    return false;

  errs() << "[MemLLVM] Unfolding string constant @" << GV->getName()
         << " (length: " << N << " bytes) into " << TargetFunctions.size() << " function(s)\n";

  for (Function *F : TargetFunctions) {
    if (F->isDeclaration())
      continue;

    // 1. Unpack ConstantExpr inside F
    bool Changed = true;
    while (Changed) {
      Changed = false;
      for (BasicBlock &BB : *F) {
        for (Instruction &I : BB) {
          for (unsigned op = 0; op < I.getNumOperands(); ++op) {
            if (auto *CE = dyn_cast<ConstantExpr>(I.getOperand(op))) {
              bool UsesGV = false;
              SmallVector<ConstantExpr *, 4> CEQueue;
              CEQueue.push_back(CE);
              while (!CEQueue.empty()) {
                ConstantExpr *Cur = CEQueue.pop_back_val();
                for (unsigned cop = 0; cop < Cur->getNumOperands(); ++cop) {
                  if (Cur->getOperand(cop) == GV) {
                    UsesGV = true;
                    break;
                  }
                  if (auto *Sub = dyn_cast<ConstantExpr>(Cur->getOperand(cop)))
                    CEQueue.push_back(Sub);
                }
                if (UsesGV) break;
              }

              if (UsesGV) {
                Instruction *NewInst = CE->getAsInstruction();
                NewInst->insertBefore(I.getIterator());
                I.setOperand(op, NewInst);
                Changed = true;
                break;
              }
            }
          }
          if (Changed) break;
        }
        if (Changed) break;
      }
    }

    // 2. Insert alloca and chunked volatile stores at the entry block
    IRBuilder<> Builder(&*F->getEntryBlock().getFirstInsertionPt());
    LLVMContext &Ctx = F->getContext();
    Type *I8Ty = Type::getInt8Ty(Ctx);
    ArrayType *ArrTy = ArrayType::get(I8Ty, N);
    AllocaInst *Alloca = Builder.CreateAlloca(ArrTy, nullptr, "stack_str");
    Alloca->setAlignment(Align(16));

    size_t Offset = 0;
    while (Offset + 8 <= N) {
      uint64_t Val = 0;
      std::memcpy(&Val, Bytes.data() + Offset, 8);
      Value *Gep = Builder.CreateConstInBoundsGEP1_64(I8Ty, Alloca, Offset);
      StoreInst *SI = Builder.CreateStore(ConstantInt::get(Type::getInt64Ty(Ctx), Val), Gep);
      SI->setVolatile(true);
      SI->setAlignment(Align(1));
      Offset += 8;
    }
    if (Offset + 4 <= N) {
      uint32_t Val = 0;
      std::memcpy(&Val, Bytes.data() + Offset, 4);
      Value *Gep = Builder.CreateConstInBoundsGEP1_64(I8Ty, Alloca, Offset);
      StoreInst *SI = Builder.CreateStore(ConstantInt::get(Type::getInt32Ty(Ctx), Val), Gep);
      SI->setVolatile(true);
      SI->setAlignment(Align(1));
      Offset += 4;
    }
    if (Offset + 2 <= N) {
      uint16_t Val = 0;
      std::memcpy(&Val, Bytes.data() + Offset, 2);
      Value *Gep = Builder.CreateConstInBoundsGEP1_64(I8Ty, Alloca, Offset);
      StoreInst *SI = Builder.CreateStore(ConstantInt::get(Type::getInt16Ty(Ctx), Val), Gep);
      SI->setVolatile(true);
      SI->setAlignment(Align(1));
      Offset += 2;
    }
    if (Offset < N) {
      uint8_t Val = Bytes[Offset];
      Value *Gep = Builder.CreateConstInBoundsGEP1_64(I8Ty, Alloca, Offset);
      StoreInst *SI = Builder.CreateStore(ConstantInt::get(Type::getInt8Ty(Ctx), Val), Gep);
      SI->setVolatile(true);
      SI->setAlignment(Align(1));
      Offset += 1;
    }

    // 3. Replace all uses of GV within F with Alloca
    for (BasicBlock &BB : *F) {
      for (Instruction &I : BB) {
        if (&I == Alloca)
          continue;

        bool IsGeneratedStore = false;
        if (auto *SI = dyn_cast<StoreInst>(&I)) {
          if (SI->isVolatile()) {
            Value *Ptr = SI->getPointerOperand();
            if (Ptr == Alloca)
              IsGeneratedStore = true;
            else if (auto *GEP = dyn_cast<GetElementPtrInst>(Ptr)) {
              if (GEP->getPointerOperand() == Alloca)
                IsGeneratedStore = true;
            }
          }
        }
        if (IsGeneratedStore)
          continue;

        for (unsigned op = 0; op < I.getNumOperands(); ++op) {
          if (I.getOperand(op) == GV) {
            I.setOperand(op, Alloca);
          }
        }
      }
    }
  }

  // 4. Clean up any dead constant expressions and safe erase
  GV->removeDeadConstantUsers();
  if (GV->use_empty()) {
    removeFromUsedLists(M, [GV](Constant *C) { return C == GV; });
    GV->eraseFromParent();
    return true;
  }
  return false;
}

PreservedAnalyses StackStringPass::run(Module &M, ModuleAnalysisManager &MAM) {
  errs() << "[MemLLVM] Running StackStringPass on module: " << M.getName() << "\n";

  SmallVector<GlobalVariable *, 16> GlobalsToTransform;
  for (GlobalVariable &GV : M.globals()) {
    if (canTransformGlobalString(&GV)) {
      GlobalsToTransform.push_back(&GV);
    }
  }

  unsigned TransformedCount = 0;
  for (GlobalVariable *GV : GlobalsToTransform) {
    if (transformGlobalString(GV, M)) {
      TransformedCount++;
    }
  }

  errs() << "[MemLLVM] StackStringPass successfully transformed "
         << TransformedCount << " global string(s).\n";

  if (TransformedCount > 0)
    return PreservedAnalyses::none();
  return PreservedAnalyses::all();
}

} // namespace memllvm
