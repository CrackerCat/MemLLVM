#include "MemLLVM.h"
#include "MemLLVMUtils.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
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

#include <chrono>
#include <cstring>
#include <random>
#include <set>
#include <vector>

using namespace llvm;

namespace memllvm {

namespace {

static uint8_t getXorByte(uint32_t Key, size_t i) {
  uint8_t k = static_cast<uint8_t>((Key >> ((i % 4) * 8)) & 0xFF);
  return static_cast<uint8_t>(k ^ ((i * 37 + 0x5A) & 0xFF));
}

static bool canTransformGlobalString(GlobalVariable *GV, SmallVectorImpl<uint8_t> &OutBytes) {
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

  // Size limit: between 1 and 512 bytes (protects stack frames from overflow)
  if (Bytes.empty() || Bytes.size() > 512)
    return false;

  bool isStringSymbol = GV->getName().starts_with("??_C@") || 
                        GV->getName().contains(".str");
  if (!isStringSymbol)
    return false;

  // Check all users: every user must be a CallBase or GEP leading to CallBase
  // Strings used in ReturnInst, StoreInst to globals, etc. are skipped to prevent escaping dangling pointers.
  SmallVector<User *, 16> WorkList(GV->user_begin(), GV->user_end());
  while (!WorkList.empty()) {
    User *U = WorkList.pop_back_val();
    if (auto *CE = dyn_cast<ConstantExpr>(U)) {
      for (User *SubU : CE->users())
        WorkList.push_back(SubU);
      continue;
    }
    if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
      for (User *SubU : GEP->users())
        WorkList.push_back(SubU);
      continue;
    }
    if (auto *CB = dyn_cast<CallBase>(U)) {
      if (!CB->getFunction())
        return false;
      continue;
    }
    // Any non-call instruction (e.g. ReturnInst, StoreInst) means string escapes
    return false;
  }

  OutBytes = std::move(Bytes);
  return true;
}

static bool transformGlobalString(GlobalVariable *GV, ArrayRef<uint8_t> PlainBytes, Module &M) {
  size_t N = PlainBytes.size();
  if (N == 0)
    return false;

  LLVMContext &Ctx = M.getContext();

  // 1. Unpack all ConstantExpr users into normal instructions inside functions
  SmallVector<ConstantExpr *, 8> CEUsers;
  for (User *U : GV->users()) {
    if (auto *CE = dyn_cast<ConstantExpr>(U)) {
      CEUsers.push_back(CE);
    }
  }

  while (!CEUsers.empty()) {
    ConstantExpr *CE = CEUsers.pop_back_val();
    SmallVector<User *, 8> SubUsers(CE->user_begin(), CE->user_end());
    for (User *SubU : SubUsers) {
      if (auto *SubCE = dyn_cast<ConstantExpr>(SubU)) {
        CEUsers.push_back(SubCE);
      } else if (auto *I = dyn_cast<Instruction>(SubU)) {
        Instruction *NewInst = CE->getAsInstruction();
        NewInst->insertBefore(I->getIterator());
        I->replaceUsesOfWith(CE, NewInst);
      }
    }
  }

  // 2. Identify all CallBase call sites and target functions
  std::set<Function *> TargetFunctions;
  std::set<CallBase *> TargetCallSites;

  SmallVector<User *, 16> WorkList(GV->user_begin(), GV->user_end());
  while (!WorkList.empty()) {
    User *U = WorkList.pop_back_val();
    if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
      for (User *SubU : GEP->users())
        WorkList.push_back(SubU);
      continue;
    }
    if (auto *CB = dyn_cast<CallBase>(U)) {
      TargetCallSites.insert(CB);
      if (Function *F = CB->getFunction())
        TargetFunctions.insert(F);
    }
  }

  if (TargetCallSites.empty())
    return false;

  // 3. Generate random 32-bit key and encrypt the global string initializer
  std::random_device RD;
  uint32_t Key = RD() ^ static_cast<uint32_t>(N * 31337);
  SmallVector<uint8_t, 64> CipherBytes(N);
  for (size_t i = 0; i < N; ++i) {
    CipherBytes[i] = PlainBytes[i] ^ getXorByte(Key, i);
  }

  GV->setInitializer(ConstantDataArray::get(Ctx, CipherBytes));
  GV->setConstant(false); // Mark as non-constant so optimizer doesn't fold reads

  if (getenv("MEMLLVM_VERBOSE")) {
    errs() << "[MemLLVM] Scoped JIT Encrypting @" << GV->getName()
           << " (" << N << " bytes) across " << TargetCallSites.size() << " call site(s)\n";
  }

  // 4. Transform each function: allocate local buffer and wrap each call site
  for (Function *F : TargetFunctions) {
    if (F->isDeclaration())
      continue;

    // Allocate ephemeral stack buffer in function entry block
    IRBuilder<> EntryBuilder(&*F->getEntryBlock().getFirstInsertionPt());
    AllocaInst *LocalBuf = EntryBuilder.CreateAlloca(
        ArrayType::get(Type::getInt8Ty(Ctx), N), nullptr, "jit_str");
    LocalBuf->setAlignment(Align(16));

    // For each call site in this function
    for (CallBase *CB : TargetCallSites) {
      if (CB->getFunction() != F)
        continue;

      // 4a. JIT Decrypt immediately before CB
      IRBuilder<> DecryptBuilder(CB);
      if (CB->getDebugLoc()) {
        DecryptBuilder.SetCurrentDebugLocation(CB->getDebugLoc());
      }

      size_t offset = 0;
      while (offset + 8 <= N) {
        uint64_t Key64 = 0;
        for (int b = 0; b < 8; ++b) {
          Key64 |= (static_cast<uint64_t>(getXorByte(Key, offset + b)) << (b * 8));
        }
        Value *CipherGep = DecryptBuilder.CreateConstInBoundsGEP1_64(Type::getInt8Ty(Ctx), GV, offset);
        Value *CipherVal = DecryptBuilder.CreateLoad(Type::getInt64Ty(Ctx), CipherGep);
        Value *PlainVal  = DecryptBuilder.CreateXor(CipherVal, DecryptBuilder.getInt64(Key64));
        Value *LocalGep  = DecryptBuilder.CreateConstInBoundsGEP1_64(Type::getInt8Ty(Ctx), LocalBuf, offset);
        StoreInst *SI    = DecryptBuilder.CreateStore(PlainVal, LocalGep);
        SI->setVolatile(true);
        SI->setAlignment(Align(1));
        offset += 8;
      }
      if (offset + 4 <= N) {
        uint32_t Key32 = 0;
        for (int b = 0; b < 4; ++b) {
          Key32 |= (static_cast<uint32_t>(getXorByte(Key, offset + b)) << (b * 8));
        }
        Value *CipherGep = DecryptBuilder.CreateConstInBoundsGEP1_64(Type::getInt8Ty(Ctx), GV, offset);
        Value *CipherVal = DecryptBuilder.CreateLoad(Type::getInt32Ty(Ctx), CipherGep);
        Value *PlainVal  = DecryptBuilder.CreateXor(CipherVal, DecryptBuilder.getInt32(Key32));
        Value *LocalGep  = DecryptBuilder.CreateConstInBoundsGEP1_64(Type::getInt8Ty(Ctx), LocalBuf, offset);
        StoreInst *SI    = DecryptBuilder.CreateStore(PlainVal, LocalGep);
        SI->setVolatile(true);
        SI->setAlignment(Align(1));
        offset += 4;
      }
      if (offset + 2 <= N) {
        uint16_t Key16 = 0;
        for (int b = 0; b < 2; ++b) {
          Key16 |= (static_cast<uint16_t>(getXorByte(Key, offset + b)) << (b * 8));
        }
        Value *CipherGep = DecryptBuilder.CreateConstInBoundsGEP1_64(Type::getInt8Ty(Ctx), GV, offset);
        Value *CipherVal = DecryptBuilder.CreateLoad(Type::getInt16Ty(Ctx), CipherGep);
        Value *PlainVal  = DecryptBuilder.CreateXor(CipherVal, DecryptBuilder.getInt16(Key16));
        Value *LocalGep  = DecryptBuilder.CreateConstInBoundsGEP1_64(Type::getInt8Ty(Ctx), LocalBuf, offset);
        StoreInst *SI    = DecryptBuilder.CreateStore(PlainVal, LocalGep);
        SI->setVolatile(true);
        SI->setAlignment(Align(1));
        offset += 2;
      }
      if (offset < N) {
        uint8_t Key8 = getXorByte(Key, offset);
        Value *CipherGep = DecryptBuilder.CreateConstInBoundsGEP1_64(Type::getInt8Ty(Ctx), GV, offset);
        Value *CipherVal = DecryptBuilder.CreateLoad(Type::getInt8Ty(Ctx), CipherGep);
        Value *PlainVal  = DecryptBuilder.CreateXor(CipherVal, DecryptBuilder.getInt8(Key8));
        Value *LocalGep  = DecryptBuilder.CreateConstInBoundsGEP1_64(Type::getInt8Ty(Ctx), LocalBuf, offset);
        StoreInst *SI    = DecryptBuilder.CreateStore(PlainVal, LocalGep);
        SI->setVolatile(true);
        SI->setAlignment(Align(1));
        offset += 1;
      }

      // 4b. Replace argument operands in CB that reference GV
      for (unsigned op = 0; op < CB->arg_size(); ++op) {
        Value *Arg = CB->getArgOperand(op);
        if (Arg->stripPointerCasts() == GV) {
          CB->setArgOperand(op, LocalBuf);
        }
      }

      // 4c. Ephemeral Wipe: immediately zero the stack buffer after the call
      if (auto *CI = dyn_cast<CallInst>(CB)) {
        if (!CI->doesNotReturn()) {
          IRBuilder<> WipeBuilder(CI->getNextNode());
          if (CI->getDebugLoc()) WipeBuilder.SetCurrentDebugLocation(CI->getDebugLoc());
          WipeBuilder.CreateMemSet(LocalBuf, WipeBuilder.getInt8(0), N, Align(16), /*isVolatile=*/true);
        }
      } else if (auto *II = dyn_cast<InvokeInst>(CB)) {
        BasicBlock *Norm = II->getNormalDest();
        IRBuilder<> NormBuilder(&*Norm->getFirstInsertionPt());
        NormBuilder.CreateMemSet(LocalBuf, NormBuilder.getInt8(0), N, Align(16), /*isVolatile=*/true);

        BasicBlock *Unwind = II->getUnwindDest();
        IRBuilder<> UnwindBuilder(&*Unwind->getFirstInsertionPt());
        UnwindBuilder.CreateMemSet(LocalBuf, UnwindBuilder.getInt8(0), N, Align(16), /*isVolatile=*/true);
      }
    }
  }

  return true;
}

} // namespace

PreservedAnalyses StackStringPass::run(Module &M, ModuleAnalysisManager &MAM) {
  SmallVector<GlobalVariable *, 32> StringGlobals;
  std::vector<SmallVector<uint8_t, 64>> PlainByteVectors;

  for (GlobalVariable &GV : M.globals()) {
    SmallVector<uint8_t, 64> Bytes;
    if (canTransformGlobalString(&GV, Bytes)) {
      StringGlobals.push_back(&GV);
      PlainByteVectors.push_back(std::move(Bytes));
    }
  }

  if (StringGlobals.empty())
    return PreservedAnalyses::all();

  unsigned TransformedCount = 0;
  for (size_t i = 0; i < StringGlobals.size(); ++i) {
    if (transformGlobalString(StringGlobals[i], PlainByteVectors[i], M)) {
      ++TransformedCount;
    }
  }

  if (TransformedCount > 0 && getenv("MEMLLVM_VERBOSE")) {
    errs() << "[MemLLVM] StackStringPass: Protected " << TransformedCount
           << " string constant(s) with scoped JIT decryption & ephemeral wipe\n";
  }

  return TransformedCount > 0 ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace memllvm
