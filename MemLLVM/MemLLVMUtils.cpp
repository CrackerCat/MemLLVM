#include "MemLLVMUtils.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace memllvm {

uint32_t calculateRor13(StringRef str) {
  uint32_t hash = 0;
  for (unsigned char c : str) {
    hash = (hash >> 13) | (hash << (32 - 13));
    hash += c;
  }
  return hash;
}

Function *findOrSynthesizeEntryPoint(Module &M) {
  // 1. Check for standard main
  Function *EntryFn = M.getFunction("main");
  if (EntryFn && !EntryFn->isDeclaration() && !EntryFn->empty()) {
    if (EntryFn != &*M.begin()) {
      M.getFunctionList().splice(M.begin(), M.getFunctionList(), EntryFn->getIterator());
      errs() << "[MemLLVM] Auto-promoted entry function @main to binary offset 0x00!\n";
    }
    return EntryFn;
  }

  // 2. Check for WinMain / wWinMain / wmain
  Function *WinMainFn = M.getFunction("WinMain");
  if (!WinMainFn || WinMainFn->isDeclaration() || WinMainFn->empty()) {
    WinMainFn = M.getFunction("wWinMain");
  }
  if (!WinMainFn || WinMainFn->isDeclaration() || WinMainFn->empty()) {
    WinMainFn = M.getFunction("wmain");
  }

  if (WinMainFn && !WinMainFn->isDeclaration() && !WinMainFn->empty()) {
    LLVMContext &Ctx = M.getContext();
    Type *Int32Ty = Type::getInt32Ty(Ctx);
    Type *PtrTy = PointerType::get(Ctx, 0);

    FunctionType *MainTy = FunctionType::get(Int32Ty, false);
    Function *MainFn = Function::Create(MainTy, GlobalValue::ExternalLinkage, "main", M);
    BasicBlock *BB = BasicBlock::Create(Ctx, "entry", MainFn);
    IRBuilder<> B(BB);

    // In flat binary / raw shellcode execution, resources cannot be loaded from the shellcode host runner (e.g. run_bin.exe).
    // Instead, probe and load companion resource DLL matching module name (e.g. Notepad4.dll).
    StringRef SrcFile = M.getSourceFileName();
    size_t DotPos = SrcFile.rfind('.');
    size_t SlashPos = SrcFile.find_last_of("/\\");
    size_t StartPos = (SlashPos == StringRef::npos) ? 0 : SlashPos + 1;
    std::string BaseName = (DotPos != StringRef::npos && DotPos > StartPos)
                               ? SrcFile.substr(StartPos, DotPos - StartPos).str()
                               : "resources";
    std::string DllName = BaseName + ".dll";

    FunctionCallee LoadLib = M.getOrInsertFunction("LoadLibraryA", FunctionType::get(PtrTy, {PtrTy}, false));
    Constant *DllStr = ConstantDataArray::getString(Ctx, DllName, true);
    GlobalVariable *DllGV = new GlobalVariable(M, DllStr->getType(), true, GlobalValue::PrivateLinkage, DllStr, ".str.res_dll");
    Value *ResMod = B.CreateCall(LoadLib, {B.CreatePointerCast(DllGV, PtrTy)});

    FunctionCallee GetMod = M.getOrInsertFunction("GetModuleHandleA", FunctionType::get(PtrTy, {PtrTy}, false));
    Value *HostMod = B.CreateCall(GetMod, {ConstantPointerNull::get(cast<PointerType>(PtrTy))});

    Value *HasResDll = B.CreateIsNotNull(ResMod);
    Value *HInst = B.CreateSelect(HasResDll, ResMod, HostMod);
    Value *NullPtr = ConstantPointerNull::get(cast<PointerType>(PtrTy));
    Constant *EmptyStr = ConstantDataArray::getString(Ctx, "", true);
    GlobalVariable *EmptyGV = new GlobalVariable(M, EmptyStr->getType(), true, GlobalValue::PrivateLinkage, EmptyStr, ".str.empty_cmd");
    Value *CmdLine = B.CreatePointerCast(EmptyGV, PtrTy);
    Value *ShowCmd = ConstantInt::get(Int32Ty, 5); // SW_SHOW = 5

    SmallVector<Value *, 4> Args;
    if (WinMainFn->arg_size() >= 1) Args.push_back(HInst);
    if (WinMainFn->arg_size() >= 2) Args.push_back(NullPtr);
    if (WinMainFn->arg_size() >= 3) Args.push_back(CmdLine);
    if (WinMainFn->arg_size() >= 4) Args.push_back(ShowCmd);

    Value *RetVal = B.CreateCall(WinMainFn, Args);
    if (RetVal->getType()->isIntegerTy()) {
      B.CreateRet(RetVal);
    } else {
      B.CreateRet(ConstantInt::get(Int32Ty, 0));
    }

    errs() << "[MemLLVM] Synthesized canonical @main wrapper forwarding to @" 
           << WinMainFn->getName() << "\n";
    M.getFunctionList().splice(M.begin(), M.getFunctionList(), MainFn->getIterator());
    errs() << "[MemLLVM] Auto-promoted entry function @main to binary offset 0x00!\n";
    return MainFn;
  }

  // 3. Library module: no main/WinMain found
  return nullptr;
}

} // namespace memllvm
