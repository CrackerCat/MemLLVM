#include "MemLLVM.h"
#include "MemLLVMUtils.h"
#include "ResolverBitcode.h"
#include "SdkImportResolver.h"
#include "llvm/IR/Verifier.h"
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

  // 1. First check if any external Windows API calls exist
  //
  // NOTE: match CallBase, not CallInst. MSVC SEH (__try/__except) lowers every
  // protected call to `invoke` with an unwind edge into a catchswitch/funclet,
  // so a CallInst-only scan silently ignores all API calls inside __try blocks.
  bool HasCallsToResolve = false;
  for (Function &F : M) {
    // Address-taken API references count as work too (see step 4b).
    if (hasAddressTakenApiUse(F)) {
      HasCallsToResolve = true;
      break;
    }
    if (F.isDeclaration() || F.getName() == "MemLLVM_GetApiAddress")
      continue;

    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (auto *CB = dyn_cast<CallBase>(&I)) {
          if (CB->isInlineAsm())
            continue;
          if (shouldResolveCallee(*CB, getDirectCallee(*CB))) {
            HasCallsToResolve = true;
            break;
          }
        }
      }
      if (HasCallsToResolve) break;
    }
    if (HasCallsToResolve) break;
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
    if (!Existing || Existing->use_empty()) {
      errs() << "[MemLLVM] No external Windows API calls found in module.\n";
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
  }

  // 3. Now collect all call sites to resolve with fresh, stable instruction pointers
  SmallVector<CallBase *, 16> CallsToResolve;
  unsigned NumInvokes = 0;
  for (Function &F : M) {
    if (F.isDeclaration() || F.getName() == "MemLLVM_GetApiAddress")
      continue;

    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (auto *CB = dyn_cast<CallBase>(&I)) {
          if (CB->isInlineAsm())
            continue;
          if (shouldResolveCallee(*CB, getDirectCallee(*CB))) {
            CallsToResolve.push_back(CB);
            if (isa<InvokeInst>(CB))
              ++NumInvokes;
          }
        }
      }
    }
  }

  // 4. Rewrite each external call to dynamic indirect call
  std::set<Function *> ResolvedCallees;
  for (size_t idx = 0; idx < CallsToResolve.size(); ++idx) {
    CallBase *CB = CallsToResolve[idx];
    Function *Callee = getDirectCallee(*CB);
    if (!Callee) continue;
    StringRef ApiName = Callee->getName();
    uint32_t ApiHash = getApiHash(ApiName);
    ResolvedCallees.insert(Callee);

    errs() << "[MemLLVM] (" << (idx + 1) << "/" << CallsToResolve.size()
           << ") Rewriting " << (isa<InvokeInst>(CB) ? "invoke" : "call")
           << " to external API: " << ApiName
           << " (ROR13: 0x" << Twine::utohexstr(ApiHash) << ") in function: "
           << (CB->getFunction() ? CB->getFunction()->getName() : "<none>") << "\n";
    errs().flush();

    // Rewrite in place by swapping the called operand instead of materialising a
    // fresh instruction. This preserves the CFG, which is mandatory for
    // `invoke`: its normal/unwind successor edges belong to the *parent block*
    // and cannot simply be re-created without re-splitting the block.
    //
    // The resolver returns `ptr` under opaque pointers, so the operand swap is
    // type-correct for both direct calls and invokes.
    IRBuilder<> Builder(CB);
    Value *RawFuncPtr =
        Builder.CreateCall(ResolverFn, {Builder.getInt32(ApiHash)}, "api_ptr");

    CB->setCalledOperand(RawFuncPtr);
    errs() << "   [Done " << (idx + 1) << "]\n";
    errs().flush();
  }

  // 4b. Address-taken API references that appear in *code*.
  //
  // kull_m_crypto_sk.c does:
  //     PBCRYPT_ENCRYPT cryptFunc = Encrypt ? BCryptEncrypt : BCryptDecrypt;
  // That is neither a call site (step 3/4 only look at CallBase) nor a global
  // initialiser (GlobalPointerPass only walks GV initialisers), so the two
  // references used to survive all the way to the linker as undefined imports.
  //
  // Every remaining non-call use of a known Win32 declaration is replaced by a
  // resolver call evaluated right before the consuming instruction.
  SmallVector<std::pair<Use *, Function *>, 16> AddrTakenUses;
  unsigned NumOpaqueAddrUses = 0;
  for (Function &F : M) {
    if (!F.isDeclaration() || F.isIntrinsic())
      continue;
    if (F.getName().starts_with("llvm.") ||
        F.getName() == "MemLLVM_GetApiAddress" ||
        isStaticShimSymbol(F.getName()) || !isKnownWindowsApi(F.getName()))
      continue;

    for (Use &U : F.uses()) {
      // A ConstantExpr hop (bitcast etc.) would need the whole expression
      // rebuilt; with opaque pointers this should not happen for a plain
      // function pointer, so surface it instead of silently skipping.
      if (!isa<Instruction>(U.getUser())) {
        ++NumOpaqueAddrUses;
        continue;
      }
      if (auto *CB = dyn_cast<CallBase>(U.getUser())) {
        if (CB->getCalledOperand() == &F)
          continue; // already rewritten in step 4
      }
      AddrTakenUses.push_back({&U, &F});
    }
  }

  if (!AddrTakenUses.empty()) {
    errs() << "[MemLLVM] Resolving " << AddrTakenUses.size()
           << " address-taken API reference(s) used as values in code\n";
  }
  for (auto &P : AddrTakenUses) {
    Use *U = P.first;
    Function *F = P.second;
    Instruction *User = cast<Instruction>(U->getUser());
    uint32_t Hash = getApiHash(F->getName());

    // A PHI operand belongs to a specific incoming edge, so the resolver call
    // has to live at the end of that predecessor block instead of before the
    // PHI itself (instructions cannot be inserted ahead of a PHI).
    Value *FnPtr = nullptr;
    if (auto *PN = dyn_cast<PHINode>(User)) {
      BasicBlock *Incoming = PN->getIncomingBlock(*U);
      IRBuilder<> Builder(Incoming->getTerminator());
      FnPtr = Builder.CreateCall(ResolverFn, {Builder.getInt32(Hash)},
                                 F->getName() + ".addr");
    } else {
      IRBuilder<> Builder(User);
      FnPtr = Builder.CreateCall(ResolverFn, {Builder.getInt32(Hash)},
                                 F->getName() + ".addr");
    }
    U->set(FnPtr);

    errs() << "[MemLLVM]   &" << F->getName() << " -> resolver(0x"
           << Twine::utohexstr(Hash) << ") before "
           << User->getOpcodeName() << " in "
           << (User->getFunction() ? User->getFunction()->getName() : "<none>")
           << "\n";
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

    IRBuilder<> Builder(LI);
    Value *FnPtr = Builder.CreateCall(ResolverFn, {Builder.getInt32(Hash)},
                                      Api + ".imp");
    LI->replaceAllUsesWith(FnPtr);
    LI->eraseFromParent();

    errs() << "[MemLLVM]   __imp_" << Api << " -> resolver(0x"
           << Twine::utohexstr(Hash) << ")\n";
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
