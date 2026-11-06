#include "MemLLVM.h"
#include "MemLLVMUtils.h"
#include "llvm/ADT/DenseMap.h"
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

#include <cctype>
#include <chrono>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace llvm;

namespace memllvm {

namespace {

static uint8_t getXorByte(uint32_t Key, size_t i) {
  uint8_t k = static_cast<uint8_t>((Key >> ((i % 4) * 8)) & 0xFF);
  return static_cast<uint8_t>(k ^ ((i * 37 + 0x5A) & 0xFF));
}

static bool isTrivialString(ArrayRef<uint8_t> Bytes, bool isWide, int EncryptLevel) {
  if (EncryptLevel >= 2)
    return false; // Aggressive: encrypt everything

  size_t N = Bytes.size();
  if (isWide) {
    while (N >= 2 && Bytes[N - 1] == 0 && Bytes[N - 2] == 0) {
      N -= 2;
    }
    // <= 2 wide characters, skip
    if (N <= 4)
      return true;
    return false;
  }

  while (N > 0 && Bytes[N - 1] == 0) {
    --N;
  }
  // <= 2 characters (e.g. "", "\n", " "), skip
  if (N <= 2)
    return true;

  return false;
}

static bool canTransformGlobalString(GlobalVariable *GV,
                                    SmallVectorImpl<uint8_t> &OutBytes,
                                    bool &OutIsWide,
                                    int EncryptLevel) {
  if (!GV->hasInitializer() || !GV->isConstant())
    return false;
  if (GV->getName().starts_with("llvm."))
    return false;

  Constant *Init = GV->getInitializer();
  if (!Init)
    return false;

  bool isWide = false;
  SmallVector<uint8_t, 64> Bytes;
  if (auto *CDS = dyn_cast<ConstantDataSequential>(Init)) {
    if (!CDS->getElementType()->isIntegerTy(8) && !CDS->getElementType()->isIntegerTy(16))
      return false;
    isWide = CDS->getElementType()->isIntegerTy(16);
    StringRef Raw = CDS->getRawDataValues();
    Bytes.append(Raw.bytes_begin(), Raw.bytes_end());
  } else if (auto *CA = dyn_cast<ConstantArray>(Init)) {
    if (!CA->getType()->getElementType()->isIntegerTy(8) && !CA->getType()->getElementType()->isIntegerTy(16))
      return false;
    isWide = CA->getType()->getElementType()->isIntegerTy(16);
    for (unsigned i = 0; i < CA->getNumOperands(); ++i) {
      if (auto *CI = dyn_cast<ConstantInt>(CA->getOperand(i))) {
        uint64_t Val = CI->getZExtValue();
        Bytes.push_back(static_cast<uint8_t>(Val & 0xFF));
        if (isWide) {
          Bytes.push_back(static_cast<uint8_t>((Val >> 8) & 0xFF));
        }
      } else {
        return false;
      }
    }
  } else {
    return false;
  }

  // Size limit: between 1 and 2048 bytes
  if (Bytes.empty() || Bytes.size() > 2048)
    return false;

  if (isTrivialString(Bytes, isWide, EncryptLevel))
    return false;

  bool isStringSymbol = GV->getName().starts_with("??_C@") || 
                        GV->getName().contains(".str");
  if (!isStringSymbol)
    return false;

  // Check all users: every user must be a CallBase or GEP/ConstantExpr leading to CallBase
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
  OutIsWide = isWide;
  return true;
}

static Function *getOrCreateDecryptHelper(Module &M) {
  const char *HelperName = "__memllvm_jit_decrypt";
  if (Function *F = M.getFunction(HelperName))
    return F;

  LLVMContext &Ctx = M.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);

  // void __memllvm_jit_decrypt(ptr dst, ptr src, i64 len, i32 key)
  FunctionType *FTy = FunctionType::get(
      Type::getVoidTy(Ctx),
      {PtrTy, PtrTy, Type::getInt64Ty(Ctx), Type::getInt32Ty(Ctx)},
      false);

  Function *F = Function::Create(FTy, GlobalValue::InternalLinkage, HelperName, &M);
  F->addFnAttr(Attribute::NoInline);
  F->addFnAttr(Attribute::OptimizeForSize);
  F->addFnAttr(Attribute::MinSize);

  BasicBlock *EntryBB = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *LoopBB  = BasicBlock::Create(Ctx, "loop", F);
  BasicBlock *ExitBB  = BasicBlock::Create(Ctx, "exit", F);

  auto ArgIt = F->arg_begin();
  Value *DstArg = &*ArgIt++;
  DstArg->setName("dst");
  Value *SrcArg = &*ArgIt++;
  SrcArg->setName("src");
  Value *LenArg = &*ArgIt++;
  LenArg->setName("len");
  Value *KeyArg = &*ArgIt++;
  KeyArg->setName("key");

  // entry:
  IRBuilder<> EntryB(EntryBB);
  Value *HasBytes = EntryB.CreateICmpUGT(LenArg, EntryB.getInt64(0), "cmp_len");
  EntryB.CreateCondBr(HasBytes, LoopBB, ExitBB);

  // loop:
  IRBuilder<> LoopB(LoopBB);
  PHINode *IdxPhi = LoopB.CreatePHI(LoopB.getInt64Ty(), 2, "i");
  IdxPhi->addIncoming(LoopB.getInt64(0), EntryBB);

  // uint8_t k = (key >> ((i % 4) * 8)) & 0xFF;
  Value *Rem4 = LoopB.CreateAnd(IdxPhi, LoopB.getInt64(3), "rem4");
  Value *Shl8 = LoopB.CreateShl(Rem4, LoopB.getInt64(3), "shl8");
  Value *Sh32 = LoopB.CreateTrunc(Shl8, LoopB.getInt32Ty(), "sh32");
  Value *KeyShifted = LoopB.CreateLShr(KeyArg, Sh32, "key_sh");
  Value *K = LoopB.CreateTrunc(KeyShifted, LoopB.getInt8Ty(), "k");

  // uint8_t x = k ^ ((i * 37 + 0x5A) & 0xFF);
  Value *Mul37 = LoopB.CreateMul(IdxPhi, LoopB.getInt64(37), "mul");
  Value *Add90 = LoopB.CreateAdd(Mul37, LoopB.getInt64(0x5A), "add");
  Value *Add8  = LoopB.CreateTrunc(Add90, LoopB.getInt8Ty(), "add8");
  Value *MaskX = LoopB.CreateXor(K, Add8, "mask_x");

  // Load src[i], XOR with MaskX, Store volatile to dst[i]
  Value *SrcPtr = LoopB.CreateInBoundsGEP(LoopB.getInt8Ty(), SrcArg, IdxPhi, "src_gep");
  Value *SrcVal = LoopB.CreateLoad(LoopB.getInt8Ty(), SrcPtr, "src_val");
  Value *PlainVal = LoopB.CreateXor(SrcVal, MaskX, "plain_val");

  Value *DstPtr = LoopB.CreateInBoundsGEP(LoopB.getInt8Ty(), DstArg, IdxPhi, "dst_gep");
  StoreInst *Store = LoopB.CreateStore(PlainVal, DstPtr);
  Store->setVolatile(true);

  // Next iteration
  Value *NextIdx = LoopB.CreateAdd(IdxPhi, LoopB.getInt64(1), "next_i");
  IdxPhi->addIncoming(NextIdx, LoopBB);

  Value *LoopCond = LoopB.CreateICmpULT(NextIdx, LenArg, "loop_cond");
  LoopB.CreateCondBr(LoopCond, LoopBB, ExitBB);

  // exit:
  IRBuilder<> ExitB(ExitBB);
  ExitB.CreateRetVoid();

  return F;
}

static Function *getOrCreateWipeHelper(Module &M) {
  const char *HelperName = "__memllvm_jit_wipe";
  if (Function *F = M.getFunction(HelperName))
    return F;

  LLVMContext &Ctx = M.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);

  // void __memllvm_jit_wipe(ptr dst, i64 len)
  FunctionType *FTy = FunctionType::get(
      Type::getVoidTy(Ctx),
      {PtrTy, Type::getInt64Ty(Ctx)},
      false);

  Function *F = Function::Create(FTy, GlobalValue::InternalLinkage, HelperName, &M);
  F->addFnAttr(Attribute::NoInline);
  F->addFnAttr(Attribute::OptimizeForSize);
  F->addFnAttr(Attribute::MinSize);

  BasicBlock *EntryBB = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *LoopBB  = BasicBlock::Create(Ctx, "loop", F);
  BasicBlock *ExitBB  = BasicBlock::Create(Ctx, "exit", F);

  auto ArgIt = F->arg_begin();
  Value *DstArg = &*ArgIt++;
  DstArg->setName("dst");
  Value *LenArg = &*ArgIt++;
  LenArg->setName("len");

  // entry:
  IRBuilder<> EntryB(EntryBB);
  Value *HasBytes = EntryB.CreateICmpUGT(LenArg, EntryB.getInt64(0), "cmp_len");
  EntryB.CreateCondBr(HasBytes, LoopBB, ExitBB);

  // loop:
  IRBuilder<> LoopB(LoopBB);
  PHINode *IdxPhi = LoopB.CreatePHI(LoopB.getInt64Ty(), 2, "i");
  IdxPhi->addIncoming(LoopB.getInt64(0), EntryBB);

  Value *DstPtr = LoopB.CreateInBoundsGEP(LoopB.getInt8Ty(), DstArg, IdxPhi, "dst_gep");
  StoreInst *Store = LoopB.CreateStore(LoopB.getInt8(0), DstPtr);
  Store->setVolatile(true);

  Value *NextIdx = LoopB.CreateAdd(IdxPhi, LoopB.getInt64(1), "next_i");
  IdxPhi->addIncoming(NextIdx, LoopBB);

  Value *LoopCond = LoopB.CreateICmpULT(NextIdx, LenArg, "loop_cond");
  LoopB.CreateCondBr(LoopCond, LoopBB, ExitBB);

  // exit:
  IRBuilder<> ExitB(ExitBB);
  ExitB.CreateRetVoid();

  return F;
}

} // namespace

PreservedAnalyses StackStringPass::run(Module &M, ModuleAnalysisManager &MAM) {
  const char *EnvVal = getenv("MEMLLVM_STRING_ENCRYPT");
  int EncryptLevel = 1; // Default: smart
  if (EnvVal) {
    if (std::strcmp(EnvVal, "0") == 0) {
      return PreservedAnalyses::all();
    }
    EncryptLevel = std::atoi(EnvVal);
  }

  // 1. Identify all eligible string globals
  struct StringInfo {
    SmallVector<uint8_t, 64> Bytes;
    bool isWide;
  };
  DenseMap<GlobalVariable *, StringInfo> EligibleStrings;

  for (GlobalVariable &GV : M.globals()) {
    SmallVector<uint8_t, 64> Bytes;
    bool isWide = false;
    if (canTransformGlobalString(&GV, Bytes, isWide, EncryptLevel)) {
      EligibleStrings[&GV] = {std::move(Bytes), isWide};
    }
  }

  if (EligibleStrings.empty())
    return PreservedAnalyses::all();

  // 2. Unpack ConstantExpr users into normal instructions inside functions
  for (auto &Pair : EligibleStrings) {
    GlobalVariable *GV = Pair.first;
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
  }

  // 3. Map each function to the set of string globals it uses
  std::map<Function *, std::vector<GlobalVariable *>> FuncToStrings;
  for (auto &Pair : EligibleStrings) {
    GlobalVariable *GV = Pair.first;
    std::set<Function *> TargetFunctions;
    for (User *U : GV->users()) {
      if (auto *I = dyn_cast<Instruction>(U)) {
        if (Function *F = I->getFunction()) {
          if (!F->isDeclaration())
            TargetFunctions.insert(F);
        }
      }
    }
    for (Function *F : TargetFunctions) {
      FuncToStrings[F].push_back(GV);
    }
  }

  if (FuncToStrings.empty())
    return PreservedAnalyses::all();

  LLVMContext &Ctx = M.getContext();
  Function *DecryptHelper = getOrCreateDecryptHelper(M);
  Function *WipeHelper    = getOrCreateWipeHelper(M);

  unsigned TransformedStrings = 0;
  unsigned TransformedFuncs   = 0;
  std::random_device RD;

  // 4. Transform each function using Compound Stack String Pool
  for (auto &Entry : FuncToStrings) {
    Function *F = Entry.first;
    const std::vector<GlobalVariable *> &GVList = Entry.second;
    if (GVList.empty())
      continue;

    // 4a. Pack strings into CompoundBytes and record offsets
    SmallVector<uint8_t, 512> CompoundBytes;
    DenseMap<GlobalVariable *, uint64_t> OffsetMap;

    for (GlobalVariable *GV : GVList) {
      const auto &Info = EligibleStrings[GV];
      // If wide string, align to 2 bytes
      if (Info.isWide && (CompoundBytes.size() % 2 != 0)) {
        CompoundBytes.push_back(0);
      }
      OffsetMap[GV] = CompoundBytes.size();
      CompoundBytes.append(Info.Bytes.begin(), Info.Bytes.end());
    }

    size_t TotalLen = CompoundBytes.size();
    if (TotalLen == 0)
      continue;

    // Protect stack frames: cap single-function compound pool to 32 KB
    if (TotalLen > 32768)
      continue;

    // 4b. Encrypt CompoundBytes with a unique 32-bit random key
    uint32_t Key = RD() ^ static_cast<uint32_t>(TotalLen * 31337 + TransformedFuncs);
    SmallVector<uint8_t, 512> CipherBytes(TotalLen);
    for (size_t i = 0; i < TotalLen; ++i) {
      CipherBytes[i] = CompoundBytes[i] ^ getXorByte(Key, i);
    }

    // 4c. Create the read-only compound pool global in module
    Constant *PoolInit = ConstantDataArray::get(Ctx, CipherBytes);
    GlobalVariable *PoolGV = new GlobalVariable(
        M,
        ArrayType::get(Type::getInt8Ty(Ctx), TotalLen),
        /*isConstant=*/true,
        GlobalValue::InternalLinkage,
        PoolInit,
        "__memllvm_pool_" + Twine(TransformedFuncs));
    PoolGV->setAlignment(Align(16));

    // 4d. In function F's entry block: find FirstNonAlloca
    Instruction *FirstNonAlloca = nullptr;
    for (Instruction &I : F->getEntryBlock()) {
      if (!isa<AllocaInst>(&I)) {
        FirstNonAlloca = &I;
        break;
      }
    }

    // Allocate single compound buffer on stack
    IRBuilder<> AllocaBuilder(FirstNonAlloca ? FirstNonAlloca : &*F->getEntryBlock().getFirstInsertionPt());
    AllocaInst *PoolBuf = AllocaBuilder.CreateAlloca(
        ArrayType::get(Type::getInt8Ty(Ctx), TotalLen), nullptr, "jit_pool");
    PoolBuf->setAlignment(Align(16));

    // Decrypt once at function entry
    IRBuilder<> DecryptBuilder(FirstNonAlloca ? FirstNonAlloca : &*F->getEntryBlock().getFirstInsertionPt());
    DecryptBuilder.CreateCall(
        DecryptHelper,
        {PoolBuf, PoolGV, DecryptBuilder.getInt64(TotalLen), DecryptBuilder.getInt32(Key)});

    // 4e. Replace uses of each GV inside function F with GEP(PoolBuf, Offset)
    IRBuilder<> GEPBuilder(FirstNonAlloca ? FirstNonAlloca : &*F->getEntryBlock().getFirstInsertionPt());
    for (GlobalVariable *GV : GVList) {
      uint64_t Off = OffsetMap[GV];
      Value *StrPtr = GEPBuilder.CreateConstInBoundsGEP1_64(Type::getInt8Ty(Ctx), PoolBuf, Off, "str_ptr");

      SmallVector<Instruction *, 16> InstUsers;
      for (User *U : GV->users()) {
        if (auto *I = dyn_cast<Instruction>(U)) {
          if (I->getFunction() == F) {
            InstUsers.push_back(I);
          }
        }
      }
      for (Instruction *I : InstUsers) {
        I->replaceUsesOfWith(GV, StrPtr);
      }
      ++TransformedStrings;
    }

    // 4f. Ephemeral Wipe: securely wipe PoolBuf before each return / resume in F
    SmallVector<Instruction *, 4> ExitInsts;
    for (BasicBlock &BB : *F) {
      Instruction *Term = BB.getTerminator();
      if (isa<ReturnInst>(Term) || isa<ResumeInst>(Term)) {
        ExitInsts.push_back(Term);
      }
    }
    for (Instruction *ExitI : ExitInsts) {
      IRBuilder<> WipeBuilder(ExitI);
      WipeBuilder.CreateCall(WipeHelper, {PoolBuf, WipeBuilder.getInt64(TotalLen)});
    }

    ++TransformedFuncs;
  }

  // 5. Clean up dead original global variables
  for (auto &Pair : EligibleStrings) {
    GlobalVariable *GV = Pair.first;
    if (GV->user_empty()) {
      GV->eraseFromParent();
    }
  }

  if (TransformedStrings > 0 && getenv("MEMLLVM_VERBOSE")) {
    errs() << "[MemLLVM] StackStringPass: Protected " << TransformedStrings
           << " string constant(s) in " << TransformedFuncs
           << " function compound pool(s) with JIT decryption & ephemeral wipe\n";
  }

  return TransformedStrings > 0 ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace memllvm
