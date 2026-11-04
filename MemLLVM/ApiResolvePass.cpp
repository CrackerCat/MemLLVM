#include "MemLLVM.h"
#include "MemLLVMUtils.h"
#include "ResolverBitcode.h"
#include "SdkImportResolver.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <set>
#include <utility>

using namespace llvm;

namespace memllvm {

// A call site is only worth rewriting when its callee is a plain external
// declaration of a known Win32 export. Shared by the pre-scan and the real
// collection pass so the two can never drift apart.
//
// NOTE: callers must pass getDirectCallee(CB), not CB.getCalledFunction().
// See the comment on memllvm::getDirectCallee for why the latter silently
// reports prototype-less C declarations as indirect calls on LLVM 22.
static bool shouldResolveCallee(const CallBase &CB, Function *Callee) {
  return Callee && Callee->isDeclaration() && !Callee->isIntrinsic() &&
         !Callee->getName().starts_with("llvm.") &&
         Callee->getName() != "MemLLVM_GetApiAddress" &&
         !isStaticShimSymbol(Callee->getName()) &&
         isKnownWindowsApi(Callee->getName());
}

// Dynamic API name hashing using ROR13. Forwarders and exports are dynamically
// discovered and resolved at runtime by Resolver.c without brittle static remapping tables.
static uint32_t getApiHash(StringRef ApiName) {
  return calculateRor13(ApiName);
}

// True when \p F is a known Win32 declaration referenced by something other
// than the callee operand of a call site — i.e. its *address* is used as a
// value. Those references need the resolver just as much as a call does, and
// without this check a TU whose only API usage is `x = Cond ? &ApiA : &ApiB`
// would hit the "nothing to do" early return and keep both imports.
static bool hasAddressTakenApiUse(Function &F) {
  if (!F.isDeclaration() || F.isIntrinsic())
    return false;
  if (F.getName().starts_with("llvm.") ||
      F.getName() == "MemLLVM_GetApiAddress" ||
      isStaticShimSymbol(F.getName()) || !isKnownWindowsApi(F.getName()))
    return false;
  for (Use &U : F.uses()) {
    auto *CB = dyn_cast<CallBase>(U.getUser());
    if (CB && CB->getCalledOperand() == &F)
      continue;
    return true;
  }
  return false;
}

PreservedAnalyses ApiResolvePass::run(Module &M, ModuleAnalysisManager &MAM) {
  errs() << "[MemLLVM] Running ApiResolvePass on module: " << M.getName() << "\n";

  // 0. Strip dllimport storage class from static shim declarations so LLVM emits
  // direct relative calls rather than indirect calls through __imp_* IAT slots.
  for (Function &F : M) {
    if (F.isDeclaration() && isStaticShimSymbol(F.getName()) &&
        F.hasDLLImportStorageClass()) {
      F.setDLLStorageClass(GlobalValue::DefaultStorageClass);
    }
  }

  // 1. Quick check if any external Windows API calls or slots exist
  bool HasCallsToResolve = false;
  for (Function &F : M) {
    if (!F.isDeclaration() || F.isIntrinsic() || F.use_empty())
      continue;
    if (F.getName().starts_with("llvm.") ||
        F.getName() == "MemLLVM_GetApiAddress" ||
        F.getName() == "MemLLVM_GetApiAddressByOrdinal" ||
        F.getName() == "MemLLVM_GetProcAddress" ||
        isStaticShimSymbol(F.getName()) || !isKnownWindowsApi(F.getName()))
      continue;
    HasCallsToResolve = true;
    break;
  }

  // `__imp_X` slots (see step 4c) are globals, not functions, so they need a
  // separate probe — a module whose only API usage is a dllimport call would
  // otherwise take the early return below.
  if (!HasCallsToResolve) {
    for (GlobalVariable &GV : M.globals()) {
      StringRef Name = GV.getName();
      if (!Name.starts_with("__imp_") || GV.use_empty())
        continue;
      StringRef Api = Name.drop_front(6);
      if (!isStaticShimSymbol(Api) && isKnownWindowsApi(Api)) {
        HasCallsToResolve = true;
        break;
      }
    }
  }

  if (!HasCallsToResolve) {
    // Even with no call sites, GlobalPointerPass may have inserted resolver
    // calls for address-taken APIs (see below), so fall through and link it.
    Function *Existing = M.getFunction("MemLLVM_GetApiAddress");
    Function *ExistingOrd = M.getFunction("MemLLVM_GetApiAddressByOrdinal");
    if ((!Existing || Existing->use_empty()) && (!ExistingOrd || ExistingOrd->use_empty())) {
      if (getenv("MEMLLVM_VERBOSE")) {
        errs() << "[MemLLVM] No external Windows API calls found in module.\n";
      }
      findOrSynthesizeEntryPoint(M);
      return PreservedAnalyses::all();
    }
  }

  // 2. Ensure MemLLVM_GetApiAddress is linked into M BEFORE collecting instruction pointers
  //
  // The function may already exist as a bare *declaration* when
  // GlobalPointerPass ran first and recorded address-taken Win32 APIs
  // (function-pointer tables such as sqlite3's aSyscall). Checking only for
  // null would treat that declaration as "already linked" and leave the body
  // missing, which surfaces as an undefined symbol at link time.
  Function *ResolverFn = M.getFunction("MemLLVM_GetApiAddress");
  if (!ResolverFn || ResolverFn->isDeclaration()) {
    auto OldDH = M.getContext().getDiagnosticHandler();

    StringRef BCData(reinterpret_cast<const char *>(ResolverBitcode), ResolverBitcodeSize);
    MemoryBufferRef MemBuf(BCData, "Resolver.bc");
    Expected<std::unique_ptr<Module>> ResMOrErr = parseBitcodeFile(MemBuf, M.getContext());
    if (!ResMOrErr) {
      M.getContext().setDiagnosticHandler(std::move(OldDH));
      errs() << "[MemLLVM] Failed to parse Resolver bitcode: " 
             << toString(ResMOrErr.takeError()) << "\n";
      return PreservedAnalyses::all();
    }

    std::unique_ptr<Module> ResM = std::move(*ResMOrErr);
    ResM->setDataLayout(M.getDataLayout());
    ResM->setTargetTriple(M.getTargetTriple());

    if (Linker::linkModules(M, std::move(ResM))) {
      M.getContext().setDiagnosticHandler(std::move(OldDH));
      errs() << "[MemLLVM] Failed to link Resolver into module\n";
      return PreservedAnalyses::all();
    }

    M.getContext().setDiagnosticHandler(std::move(OldDH));

    ResolverFn = M.getFunction("MemLLVM_GetApiAddress");
    if (!ResolverFn) {
      errs() << "[MemLLVM] Error: MemLLVM_GetApiAddress not found after linking!\n";
      return PreservedAnalyses::all();
    }

    Comdat *C = M.getOrInsertComdat("MemLLVM_GetApiAddress");
    C->setSelectionKind(Comdat::Any);
    ResolverFn->setComdat(C);
    ResolverFn->setLinkage(GlobalValue::LinkOnceODRLinkage);

    if (Function *OrdFn = M.getFunction("MemLLVM_GetApiAddressByOrdinal")) {
      Comdat *COrd = M.getOrInsertComdat("MemLLVM_GetApiAddressByOrdinal");
      COrd->setSelectionKind(Comdat::Any);
      OrdFn->setComdat(COrd);
      OrdFn->setLinkage(GlobalValue::LinkOnceODRLinkage);
    }
    if (Function *GpaFn = M.getFunction("MemLLVM_GetProcAddress")) {
      Comdat *CGpa = M.getOrInsertComdat("MemLLVM_GetProcAddress");
      CGpa->setSelectionKind(Comdat::Any);
      GpaFn->setComdat(CGpa);
      GpaFn->setLinkage(GlobalValue::LinkOnceODRLinkage);
    }
  }

  FunctionCallee ResolverByOrdinalFn = M.getOrInsertFunction(
      "MemLLVM_GetApiAddressByOrdinal",
      FunctionType::get(PointerType::get(M.getContext(), 0),
                        {Type::getInt32Ty(M.getContext()), Type::getInt32Ty(M.getContext())}, false));

  // 3. Collect all external call sites and address-taken uses via declaration def-use chains
  SmallVector<CallBase *, 16> CallsToResolve;
  SmallVector<std::pair<Use *, Function *>, 16> AddrTakenUses;
  unsigned NumInvokes = 0;
  unsigned NumOpaqueAddrUses = 0;

  for (Function &F : M) {
    if (!F.isDeclaration() || F.isIntrinsic() || F.use_empty())
      continue;
    if (F.getName().starts_with("llvm.") ||
        F.getName() == "MemLLVM_GetApiAddress" ||
        F.getName() == "MemLLVM_GetApiAddressByOrdinal" ||
        F.getName() == "MemLLVM_GetProcAddress" ||
        isStaticShimSymbol(F.getName()) || !isKnownWindowsApi(F.getName()))
      continue;

    for (Use &U : F.uses()) {
      User *user = U.getUser();
      if (auto *CB = dyn_cast<CallBase>(user)) {
        if (!CB->isInlineAsm() && CB->getCalledOperand()->stripPointerCasts() == &F) {
          CallsToResolve.push_back(CB);
          if (isa<InvokeInst>(CB))
            ++NumInvokes;
          continue;
        }
      }
      if (!isa<Instruction>(user)) {
        ++NumOpaqueAddrUses;
        continue;
      }
      AddrTakenUses.push_back({&U, &F});
    }
  }

  // 4. Rewrite each external call to dynamic indirect call
  const bool Verbose = (getenv("MEMLLVM_VERBOSE") != nullptr);
  std::set<Function *> ResolvedCallees;
  for (size_t idx = 0; idx < CallsToResolve.size(); ++idx) {
    CallBase *CB = CallsToResolve[idx];
    Function *Callee = getDirectCallee(*CB);
    if (!Callee) continue;
    StringRef ApiName = Callee->getName();
    uint32_t ApiHash = getApiHash(ApiName);
    ResolvedCallees.insert(Callee);

    std::string OrdDll;
    uint32_t Ordinal = 0;
    bool IsOrdinal = probeOrdinalForApi(ApiName, OrdDll, Ordinal);

    if (Verbose) {
      if (IsOrdinal) {
        errs() << "[MemLLVM] (" << (idx + 1) << "/" << CallsToResolve.size()
               << ") Rewriting " << (isa<InvokeInst>(CB) ? "invoke" : "call")
               << " to external ordinal API: " << ApiName << " -> "
               << (OrdDll.empty() ? "<any>" : OrdDll) << " #" << Ordinal << "\n";
      } else {
        errs() << "[MemLLVM] (" << (idx + 1) << "/" << CallsToResolve.size()
               << ") Rewriting " << (isa<InvokeInst>(CB) ? "invoke" : "call")
               << " to external API: " << ApiName
               << " (ROR13: 0x" << Twine::utohexstr(ApiHash) << ") in function: "
               << (CB->getFunction() ? CB->getFunction()->getName() : "<none>") << "\n";
      }
    }

    IRBuilder<> Builder(CB);
    if (CB->getDebugLoc()) {
      Builder.SetCurrentDebugLocation(CB->getDebugLoc());
    } else if (CB->getFunction() && CB->getFunction()->getSubprogram()) {
      Builder.SetCurrentDebugLocation(DILocation::get(M.getContext(), 0, 0, CB->getFunction()->getSubprogram()));
    }
    Value *RawFuncPtr = nullptr;
    if (IsOrdinal) {
      uint32_t DllHash = OrdDll.empty() ? 0 : calculateRor13(OrdDll);
      RawFuncPtr = Builder.CreateCall(
          ResolverByOrdinalFn,
          {Builder.getInt32(DllHash), Builder.getInt32(Ordinal)},
          "api_ord_ptr");
    } else {
      RawFuncPtr =
          Builder.CreateCall(ResolverFn, {Builder.getInt32(ApiHash)}, "api_ptr");
    }

    CB->setCalledOperand(RawFuncPtr);
  }

  // 4b. Address-taken API references that appear in *code*.
  if (!AddrTakenUses.empty() && Verbose) {
    errs() << "[MemLLVM] Resolving " << AddrTakenUses.size()
           << " address-taken API reference(s) used as values in code\n";
  }
  for (auto &P : AddrTakenUses) {
    Use *U = P.first;
    Function *F = P.second;
    Instruction *User = cast<Instruction>(U->getUser());
    uint32_t Hash = getApiHash(F->getName());

    std::string OrdDll;
    uint32_t Ordinal = 0;
    bool IsOrdinal = probeOrdinalForApi(F->getName(), OrdDll, Ordinal);

    // A PHI operand belongs to a specific incoming edge, so the resolver call
    // has to live at the end of that predecessor block instead of before the
    // PHI itself (instructions cannot be inserted ahead of a PHI).
    Value *FnPtr = nullptr;
    if (auto *PN = dyn_cast<PHINode>(User)) {
      BasicBlock *Incoming = PN->getIncomingBlock(*U);
      IRBuilder<> Builder(Incoming->getTerminator());
      if (User->getDebugLoc()) {
        Builder.SetCurrentDebugLocation(User->getDebugLoc());
      } else if (User->getFunction() && User->getFunction()->getSubprogram()) {
        Builder.SetCurrentDebugLocation(DILocation::get(M.getContext(), 0, 0, User->getFunction()->getSubprogram()));
      }
      if (IsOrdinal) {
        uint32_t DllHash = OrdDll.empty() ? 0 : calculateRor13(OrdDll);
        FnPtr = Builder.CreateCall(
            ResolverByOrdinalFn,
            {Builder.getInt32(DllHash), Builder.getInt32(Ordinal)},
            F->getName() + ".ord_addr");
      } else {
        FnPtr = Builder.CreateCall(ResolverFn, {Builder.getInt32(Hash)},
                                   F->getName() + ".addr");
      }
    } else {
      IRBuilder<> Builder(User);
      if (User->getDebugLoc()) {
        Builder.SetCurrentDebugLocation(User->getDebugLoc());
      } else if (User->getFunction() && User->getFunction()->getSubprogram()) {
        Builder.SetCurrentDebugLocation(DILocation::get(M.getContext(), 0, 0, User->getFunction()->getSubprogram()));
      }
      if (IsOrdinal) {
        uint32_t DllHash = OrdDll.empty() ? 0 : calculateRor13(OrdDll);
        FnPtr = Builder.CreateCall(
            ResolverByOrdinalFn,
            {Builder.getInt32(DllHash), Builder.getInt32(Ordinal)},
            F->getName() + ".ord_addr");
      } else {
        FnPtr = Builder.CreateCall(ResolverFn, {Builder.getInt32(Hash)},
                                   F->getName() + ".addr");
      }
    }
    U->set(FnPtr);

    if (Verbose) {
      if (IsOrdinal) {
        errs() << "[MemLLVM]   &" << F->getName() << " -> ordinal("
               << (OrdDll.empty() ? "<any>" : OrdDll) << " #" << Ordinal
               << ") before " << User->getOpcodeName() << "\n";
      } else {
        errs() << "[MemLLVM]   &" << F->getName() << " -> resolver(0x"
               << Twine::utohexstr(Hash) << ") before "
               << User->getOpcodeName() << " in "
               << (User->getFunction() ? User->getFunction()->getName() : "<none>")
               << "\n";
      }
    }
  }
  if (NumOpaqueAddrUses) {
    errs() << "[MemLLVM] WARNING: " << NumOpaqueAddrUses
           << " API reference(s) reachable only through a ConstantExpr were "
              "left untouched; they will remain undefined imports.\n";
  }

  // 4c. `__imp_X` IAT slots.
  //
  // A declaration carrying `__declspec(dllimport)` — inc/msasn1.h does exactly
  // that for the whole ASN1 API — makes clang reference the import slot
  // `__imp_X` rather than the symbol, so the call lowers to
  // `call qword ptr [__imp_X]` (or `jmp` when it is a tail call). getDirectCallee
  // sees an indirect call and steps 3/4 skip it, which is how
  // ASN1_FreeEncoded survived as the last import in mimikatz.
  //
  // Every load of such a slot becomes a resolver call.
  SmallVector<std::pair<LoadInst *, std::string>, 16> ImpSlotLoads;
  for (GlobalVariable &GV : M.globals()) {
    StringRef Name = GV.getName();
    if (!Name.starts_with("__imp_"))
      continue;
    StringRef Api = Name.drop_front(6);
    if (Api.starts_with("__imp_") || isStaticShimSymbol(Api) ||
        !isKnownWindowsApi(Api))
      continue;

    for (Use &U : GV.uses()) {
      if (auto *LI = dyn_cast<LoadInst>(U.getUser()))
        ImpSlotLoads.push_back({LI, Api.str()});
    }
  }

  if (!ImpSlotLoads.empty()) {
    errs() << "[MemLLVM] Resolving " << ImpSlotLoads.size()
           << " dllimport IAT slot load(s) (__imp_*)\n";
  }
  for (auto &P : ImpSlotLoads) {
    LoadInst *LI = P.first;
    const std::string &Api = P.second;
    uint32_t Hash = getApiHash(Api);

    std::string OrdDll;
    uint32_t Ordinal = 0;
    bool IsOrdinal = probeOrdinalForApi(Api, OrdDll, Ordinal);

    IRBuilder<> Builder(LI);
    if (LI->getDebugLoc()) {
      Builder.SetCurrentDebugLocation(LI->getDebugLoc());
    } else if (LI->getFunction() && LI->getFunction()->getSubprogram()) {
      Builder.SetCurrentDebugLocation(DILocation::get(M.getContext(), 0, 0, LI->getFunction()->getSubprogram()));
    }
    Value *FnPtr = nullptr;
    if (IsOrdinal) {
      uint32_t DllHash = OrdDll.empty() ? 0 : calculateRor13(OrdDll);
      FnPtr = Builder.CreateCall(
          ResolverByOrdinalFn,
          {Builder.getInt32(DllHash), Builder.getInt32(Ordinal)},
          Api + ".ord_imp");
      if (Verbose) {
        errs() << "[MemLLVM]   __imp_" << Api << " -> ordinal("
               << (OrdDll.empty() ? "<any>" : OrdDll) << " #" << Ordinal << ")\n";
      }
    } else {
      FnPtr = Builder.CreateCall(ResolverFn, {Builder.getInt32(Hash)},
                                        Api + ".imp");
      if (Verbose) {
        errs() << "[MemLLVM]   __imp_" << Api << " -> resolver(0x"
               << Twine::utohexstr(Hash) << ")\n";
      }
    }
    LI->replaceAllUsesWith(FnPtr);
    LI->eraseFromParent();
  }

  // 3. Audit for any unhandled external imports with remaining uses
  unsigned UnresolvedCount = 0;
  for (Function &F : M) {
    if (F.isDeclaration() && !F.isIntrinsic() &&
        !isStaticShimSymbol(F.getName()) &&
        !F.getName().starts_with("llvm.") &&
        F.getName() != "MemLLVM_GetApiAddress" &&
        F.hasNUsesOrMore(1)) {
      errs() << "[MemLLVM] WARNING: Unresolved external import remains: @"
             << F.getName() << " (" << F.getNumUses() << " use(s))\n";
      UnresolvedCount++;
    }
  }

  errs() << "[MemLLVM] =================== API Resolution Summary ===================\n";
  errs() << "[MemLLVM]   Direct/Invoke Call Sites: " << CallsToResolve.size()
         << " (" << NumInvokes << " in SEH __try blocks)\n";
  errs() << "[MemLLVM]   Address-Taken References: " << AddrTakenUses.size() << "\n";
  errs() << "[MemLLVM]   __imp_* Slot Loads      : " << ImpSlotLoads.size() << "\n";
  errs() << "[MemLLVM]   Unresolved Imports      : " << UnresolvedCount << "\n";
  errs() << "[MemLLVM] ===============================================================\n";

  // Ensure entry function remains strictly at offset 0x00 (front of module)
  findOrSynthesizeEntryPoint(M);

  errs() << "[MemLLVM] Running Module verifier...\n";
  raw_fd_ostream &out = errs();
  if (llvm::verifyModule(M, &out)) {
    errs() << "[MemLLVM] VERIFIER FAILED!\n";
  } else {
    errs() << "[MemLLVM] Module verified OK!\n";
  }

  return PreservedAnalyses::none();
}

} // namespace memllvm
