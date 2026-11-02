#include "MemLLVMUtils.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"

#include <string>

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

namespace {

// Slot section name using `$m` subgroup. LLD and MSVC link.exe sort COFF `$` subgroups
// alphabetically within a merged section (.mli$a < .mli$m < .mli$z).
// Sentinels .mli$a (__memllvm_mli_start) and .mli$z (__memllvm_mli_end) in ltl.lib bracket
// all TU initializer slots in .mli$m across translation units.
constexpr const char *kInitSection = ".mli$m";
constexpr const char *kTuInitName = "__memllvm_tu_init";

} // namespace

Function *getDirectCallee(const CallBase &CB) {
  if (CB.isInlineAsm())
    return nullptr;
  // stripPointerCasts() also folds a bitcast/addrspacecast hop, which is what
  // pre-opaque-pointer IR used to carry; keeping it makes the helper work for
  // both shapes.
  return dyn_cast<Function>(CB.getCalledOperand()->stripPointerCasts());
}

bool isStaticShimSymbol(StringRef Name) {
  // LLVM prefixes a global's name with \x01 when the symbol must be emitted
  // verbatim (no target mangling). The byte is invisible in diagnostics, so a
  // "\x01_fileno" declaration would slip past every exact-match test below and
  // get resolved dynamically even though ltl defines it. Strip it first.
  if (!Name.empty() && Name.front() == '\x01')
    Name = Name.drop_front();

  // 1. Any MSVC C++ mangled symbol (?...) belongs to C++ modules / LTL / templates, NOT external Win32 DLL exports
  if (Name.starts_with("?"))
    return true;

  // 2. Runtime shims, threading, memory, intrinsics, and CRT builtins provided statically by LTL
  //
  // _fileno / _setmode must stay with ltl: __acrt_iob_func hands out ltl's own
  // 16-byte dummy FILE blobs, so the host UCRT _fileno would dereference one
  // and read a garbage fd; _setmode then range-checks that fd, fails, and
  // routes through _invalid_parameter -- the process dies before the banner.
  if (Name.starts_with("_Mtx_") || Name.starts_with("_Cnd_") ||
      Name.starts_with("_Thrd_") || Name.starts_with("__emutls_") ||
      Name.starts_with("__std_") || Name.starts_with("__CxxFrameHandler") ||
      Name.starts_with("__chkstk") ||
      Name.contains("@std@@") || Name.contains("@stdext@@") ||
      Name.contains("@Concurrency@@") ||
      Name.starts_with("_Smtx_") || Name == "_beginthreadex" ||
      Name.starts_with("MemLLVM_") || Name.starts_with("_MemLLVM_") ||
      Name == "_Getctype" || Name == "_Tolower" || Name == "_Toupper" ||
      Name == "_Getcoll" || Name == "_Getcvt" || Name == "_Getdateorder" ||
      Name == "_Strcoll" || Name == "_Strxfrm" || Name == "_Wcscoll" ||
      Name == "_Mbrtowc" || Name == "_Wcrtomb" ||
      Name == "_get_stream_buffer_pointers" ||
      Name == "_lock_file" || Name == "_unlock_file" ||
      Name.contains("__ExceptionPtr") ||
      Name == "memcpy" || Name == "memmove" || Name == "memset" ||
      Name == "floor" || Name == "ceil" || Name == "round" ||
      Name == "floorf" || Name == "ceilf" ||
      Name == "strcmp" || Name == "strncmp" || Name == "_strnicmp" ||
      Name == "_stricmp" || Name == "_wcsicmp" || Name == "wcsncmp" || Name == "_wcsnicmp" ||
      Name == "wcscmp" || Name == "_memicmp" ||
      Name == "memcmp" || Name == "wcsstr" || Name == "wcschr" ||
      Name == "qsort" || Name == "bsearch" ||
      Name == "atexit" || Name == "__cxa_atexit" || Name == "exit" || Name == "_exit" ||
      Name == "_doexit" || Name == "_initterm" || Name == "_initterm_e" ||
      Name == "__memllvm_get_ansi_argv" || Name == "__memllvm_get_wide_argv" ||
      Name == "__memllvm_dispatch_tu_inits" ||
      Name == "GetActiveProcessorCount" ||
      // Complete Stdio and FILE* family (prefix rules ensure zero omissions)
      Name.starts_with("__stdio_common_") ||
      Name.starts_with("__acrt_") ||
      Name == "__acrt_iob_func" ||
      Name.starts_with("fopen") || Name.starts_with("_wfopen") ||
      Name.starts_with("freopen") || Name.starts_with("_wfreopen") ||
      Name.starts_with("fclose") || Name == "_fcloseall" ||
      Name.starts_with("fread") || Name.starts_with("fwrite") ||
      Name.starts_with("fseek") || Name.starts_with("_fseek") ||
      Name.starts_with("ftell") || Name.starts_with("_ftell") ||
      Name.starts_with("fget") || Name.starts_with("_fget") ||
      Name.starts_with("fput") || Name.starts_with("_fput") ||
      Name.starts_with("getc") || Name.starts_with("getw") ||
      Name.starts_with("putc") || Name.starts_with("putw") ||
      Name.starts_with("_getc") || Name.starts_with("_getw") ||
      Name.starts_with("_putc") || Name.starts_with("_putw") ||
      Name.starts_with("unget") || Name.starts_with("_unget") ||
      Name.starts_with("feof") || Name.starts_with("_feof") ||
      Name.starts_with("ferror") || Name.starts_with("_ferror") ||
      Name.starts_with("clearerr") || Name.starts_with("_clearerr") ||
      Name.starts_with("fflush") || Name.starts_with("_fflush") ||
      Name == "fgetpos" || Name == "fsetpos" || Name == "rewind" ||
      Name == "setvbuf" || Name == "setbuf" ||
      Name == "remove" || Name == "_wremove" ||
      Name == "rename" || Name == "_wrename" ||
      Name == "getchar" || Name == "getwchar" ||
      Name == "putchar" || Name == "putwchar" ||
      Name == "puts" ||
      Name == "printf" || Name == "wprintf" ||
      Name == "vprintf" || Name == "vwprintf" ||
      Name == "fprintf" || Name == "fwprintf" ||
      Name == "vfprintf" || Name == "vfwprintf" ||
      Name == "sprintf" || Name == "snprintf" || Name == "swprintf" ||
      Name == "vsprintf" || Name == "vsnprintf" || Name == "vswprintf" ||
      Name == "_fileno" || Name == "_setmode" ||
      Name == "__iob_func" || Name == "__p__iob" ||
      Name == "__C_specific_handler" ||
      Name == "_fltused") {
    return true;
  }
  return false;
}

Function *getOrCreateTuInit(Module &M) {
  if (Function *Existing = M.getFunction(kTuInitName))
    return Existing;

  LLVMContext &Ctx = M.getContext();
  FunctionType *FTy = FunctionType::get(Type::getVoidTy(Ctx), false);
  Function *F = Function::Create(FTy, GlobalValue::InternalLinkage, kTuInitName, M);
  // Keep it out of the inliner's reach so a later pass can still append stores.
  F->addFnAttr(Attribute::NoInline);

  BasicBlock *BB = BasicBlock::Create(Ctx, "entry", F);
  IRBuilder<> B(BB);
  B.CreateRetVoid();

  errs() << "[MemLLVM] Created per-TU init function @" << kTuInitName
         << " (module has no entry point; the packer will dispatch it)\n";
  return F;
}

void registerTuInit(Module &M, Function *InitFn) {
  LLVMContext &Ctx = M.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);

  // One slot per TU, so the name only has to be unique across TUs. Deriving it
  // from the module identifier keeps it stable and collision-resistant; the
  // symbol is externally visible on purpose, otherwise /OPT:REF would drop the
  // slot and the packer would never see this TU's initialiser.
  std::string SlotName =
      std::string("__memllvm_init_slot_") +
      std::to_string(calculateRor13(M.getModuleIdentifier()));
  if (M.getGlobalVariable(SlotName))
    return;

  auto *GV = new GlobalVariable(M, PtrTy, /*isConstant=*/true,
                                GlobalValue::ExternalLinkage, InitFn, SlotName);
  GV->setSection(kInitSection);
  GV->setAlignment(Align(8));

  errs() << "[MemLLVM] Registered @" << SlotName << " into " << kInitSection << "\n";
}

Function *findOrSynthesizeEntryPoint(Module &M) {
  // 1. Check for standard main
  Function *EntryFn = M.getFunction("main");
  if (EntryFn && !EntryFn->isDeclaration() && !EntryFn->empty()) {
    if (EntryFn->arg_size() == 0) {
      if (EntryFn != &*M.begin()) {
        M.getFunctionList().splice(M.begin(), M.getFunctionList(), EntryFn->getIterator());
        errs() << "[MemLLVM] Auto-promoted parameterless entry function @main to binary offset 0x00!\n";
      }
      return EntryFn;
    }

    // User's main has parameters: int main(int argc, char **argv)
    // Synthesize an ABI-compliant entry wrapper with argc >= 1 and valid argv[0]
    LLVMContext &Ctx = M.getContext();
    Type *Int32Ty = Type::getInt32Ty(Ctx);
    Type *PtrTy = PointerType::get(Ctx, 0);

    // Rename original main to avoid symbol collision
    EntryFn->setName("__memllvm_real_main");
    EntryFn->setLinkage(GlobalValue::InternalLinkage);

    FunctionType *MainTy = FunctionType::get(Int32Ty, false);
    Function *MainFn = Function::Create(MainTy, GlobalValue::ExternalLinkage, "main", M);
    BasicBlock *BB = BasicBlock::Create(Ctx, "entry", MainFn);
    IRBuilder<> B(BB);

    FunctionCallee GetArgvFn = M.getOrInsertFunction(
        "__memllvm_get_ansi_argv",
        FunctionType::get(Type::getVoidTy(Ctx), {PtrTy, PtrTy}, false));

    AllocaInst *ArgcSlot = B.CreateAlloca(Int32Ty, nullptr, "argc");
    AllocaInst *ArgvSlot = B.CreateAlloca(PtrTy, nullptr, "argv");
    B.CreateStore(ConstantInt::get(Int32Ty, 1), ArgcSlot);
    B.CreateStore(ConstantPointerNull::get(cast<PointerType>(PtrTy)), ArgvSlot);

    B.CreateCall(GetArgvFn, {ArgcSlot, ArgvSlot});

    Value *Argc = B.CreateLoad(Int32Ty, ArgcSlot);
    Value *Argv = B.CreateLoad(PtrTy, ArgvSlot);

    SmallVector<Value *, 2> Args;
    if (EntryFn->arg_size() >= 1) Args.push_back(Argc);
    if (EntryFn->arg_size() >= 2) Args.push_back(Argv);

    Value *RetVal = B.CreateCall(EntryFn, Args);
    if (RetVal->getType()->isIntegerTy()) {
      B.CreateRet(RetVal);
    } else {
      B.CreateRet(ConstantInt::get(Int32Ty, 0));
    }

    errs() << "[MemLLVM] Synthesized canonical @main wrapper forwarding to @__memllvm_real_main with reconstructed argc/argv\n";
    M.getFunctionList().splice(M.begin(), M.getFunctionList(), MainFn->getIterator());
    errs() << "[MemLLVM] Auto-promoted entry function @main to binary offset 0x00!\n";
    return MainFn;
  }

  // 2. Check for WinMain / wWinMain
  //    Signature: (HINSTANCE, HINSTANCE, LPSTR/LPWSTR, int)
  //    NOTE: wmain is deliberately NOT included here. Its signature is
  //    (int argc, wchar_t **argv); feeding it an HINSTANCE as argc and NULL as
  //    argv makes the callee dereference NULL+8 on its first argv loop.
  Function *WinMainFn = M.getFunction("WinMain");
  if (!WinMainFn || WinMainFn->isDeclaration() || WinMainFn->empty()) {
    WinMainFn = M.getFunction("wWinMain");
  }

  if (WinMainFn && !WinMainFn->isDeclaration() && !WinMainFn->empty()) {
    LLVMContext &Ctx = M.getContext();
    Type *Int32Ty = Type::getInt32Ty(Ctx);
    Type *PtrTy = PointerType::get(Ctx, 0);

    FunctionType *MainTy = FunctionType::get(Int32Ty, false);
    Function *MainFn = Function::Create(MainTy, GlobalValue::ExternalLinkage, "main", M);
    BasicBlock *BB = BasicBlock::Create(Ctx, "entry", MainFn);
    IRBuilder<> B(BB);

    // In flat binary / raw machine code execution, resources cannot be loaded from the host process.
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

  // 2b. Check for wmain (CRT signature: int argc, wchar_t **argv)
  Function *WMainFn = M.getFunction("wmain");
  if (WMainFn && !WMainFn->isDeclaration() && !WMainFn->empty()) {
    LLVMContext &Ctx = M.getContext();
    Type *Int32Ty = Type::getInt32Ty(Ctx);
    Type *PtrTy = PointerType::get(Ctx, 0);

    FunctionType *MainTy = FunctionType::get(Int32Ty, false);
    Function *MainFn = Function::Create(MainTy, GlobalValue::ExternalLinkage, "main", M);
    BasicBlock *BB = BasicBlock::Create(Ctx, "entry", MainFn);
    IRBuilder<> B(BB);

    FunctionCallee GetArgvFn = M.getOrInsertFunction(
        "__memllvm_get_wide_argv",
        FunctionType::get(Type::getVoidTy(Ctx), {PtrTy, PtrTy}, false));

    AllocaInst *ArgcSlot = B.CreateAlloca(Int32Ty, nullptr, "argc");
    AllocaInst *ArgvSlot = B.CreateAlloca(PtrTy, nullptr, "argv");
    B.CreateStore(ConstantInt::get(Int32Ty, 1), ArgcSlot);
    B.CreateStore(ConstantPointerNull::get(cast<PointerType>(PtrTy)), ArgvSlot);

    B.CreateCall(GetArgvFn, {ArgcSlot, ArgvSlot});

    Value *Argc = B.CreateLoad(Int32Ty, ArgcSlot);
    Value *Argv = B.CreateLoad(PtrTy, ArgvSlot);

    // Legacy wide format specifier options for printf
    Type *Int64Ty = Type::getInt64Ty(Ctx);
    FunctionCallee OptFn = M.getOrInsertFunction("__local_stdio_printf_options",
                                                 FunctionType::get(PtrTy, false));
    Value *OptPtr = B.CreateCall(OptFn);
    Value *OptVal = B.CreateLoad(Int64Ty, OptPtr);
    B.CreateStore(B.CreateOr(OptVal, ConstantInt::get(Int64Ty, 0x4)), OptPtr);

    SmallVector<Value *, 2> Args;
    if (WMainFn->arg_size() >= 1)
      Args.push_back(Argc);
    if (WMainFn->arg_size() >= 2)
      Args.push_back(Argv);

    Value *RetVal = B.CreateCall(WMainFn, Args);
    if (RetVal->getType()->isIntegerTy()) {
      B.CreateRet(RetVal);
    } else {
      B.CreateRet(ConstantInt::get(Int32Ty, 0));
    }

    errs() << "[MemLLVM] Synthesized @main wrapper forwarding to CRT entry @"
           << WMainFn->getName() << " with reconstructed argc (>=1), argv\n";
    M.getFunctionList().splice(M.begin(), M.getFunctionList(), MainFn->getIterator());
    return MainFn;
  }

  // 3. Library module: no main/WinMain/wmain found
  return nullptr;
}

} // namespace memllvm
