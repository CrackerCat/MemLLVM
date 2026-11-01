#include "MemLLVM.h"
#include "MemLLVMUtils.h"
#include "ResolverBitcode.h"
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

using namespace llvm;

namespace memllvm {

static bool isStaticShimSymbol(StringRef Name) {
  if (Name.starts_with("_Mtx_") || Name.starts_with("_Cnd_") || 
      Name.starts_with("_Thrd_") || Name.starts_with("__emutls_") || 
      Name.starts_with("__std_") ||
      Name.contains("@std@@") || Name.contains("@stdext@@") ||
      Name.starts_with("?cout@") || Name.starts_with("?cerr@") || Name.starts_with("?cin@") ||
      Name.starts_with("?_Throw_Cpp_error@") ||
      Name.starts_with("_Smtx_") || Name == "_beginthreadex" ||
      Name.starts_with("MemLLVM_") || Name.starts_with("_MemLLVM_") ||
      Name == "memcpy" || Name == "memmove" || Name == "memset") {
    return true;
  }
  return false;
}

PreservedAnalyses ApiResolvePass::run(Module &M, ModuleAnalysisManager &MAM) {
  errs() << "[MemLLVM] Running ApiResolvePass on module: " << M.getName() << "\n";

  // 1. First check if any external Windows API calls exist
  bool HasCallsToResolve = false;
  for (Function &F : M) {
    if (F.isDeclaration() || F.getName() == "MemLLVM_GetApiAddress")
      continue;

    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (auto *CI = dyn_cast<CallInst>(&I)) {
          if (CI->isInlineAsm())
            continue;

          Function *Callee = CI->getCalledFunction();
          if (Callee && Callee->isDeclaration() && !Callee->isIntrinsic() &&
              !Callee->getName().starts_with("llvm.") &&
              Callee->getName() != "MemLLVM_GetApiAddress" &&
              !isStaticShimSymbol(Callee->getName())) {
            HasCallsToResolve = true;
            break;
          }
        }
      }
      if (HasCallsToResolve) break;
    }
    if (HasCallsToResolve) break;
  }

  if (!HasCallsToResolve) {
    errs() << "[MemLLVM] No external Windows API calls found in module.\n";
    findOrSynthesizeEntryPoint(M);
    return PreservedAnalyses::all();
  }

  // 2. Ensure MemLLVM_GetApiAddress is linked into M BEFORE collecting instruction pointers
  Function *ResolverFn = M.getFunction("MemLLVM_GetApiAddress");
  if (!ResolverFn) {
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

  // 3. Now collect all CallInsts to resolve with fresh, stable instruction pointers
  SmallVector<CallInst *, 16> CallsToResolve;
  for (Function &F : M) {
    if (F.isDeclaration() || F.getName() == "MemLLVM_GetApiAddress")
      continue;

    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (auto *CI = dyn_cast<CallInst>(&I)) {
          if (CI->isInlineAsm())
            continue;

          Function *Callee = CI->getCalledFunction();
          if (Callee && Callee->isDeclaration() && !Callee->isIntrinsic() &&
              !Callee->getName().starts_with("llvm.") &&
              Callee->getName() != "MemLLVM_GetApiAddress" &&
              !isStaticShimSymbol(Callee->getName())) {
            CallsToResolve.push_back(CI);
          }
        }
      }
    }
  }

  // 4. Rewrite each external call to dynamic indirect call
  std::set<Function *> ResolvedCallees;
  for (size_t idx = 0; idx < CallsToResolve.size(); ++idx) {
    CallInst *CI = CallsToResolve[idx];
    Function *Callee = CI->getCalledFunction();
    if (!Callee) continue;
    StringRef ApiName = Callee->getName();
    uint32_t ApiHash = calculateRor13(ApiName);
    if (ApiName == "DefWindowProcA") {
      ApiHash = calculateRor13("NtdllDefWindowProc_A");
      errs() << "[MemLLVM] Note: Mapping forwarded API DefWindowProcA -> NtdllDefWindowProc_A\n";
    } else if (ApiName == "DefWindowProcW") {
      ApiHash = calculateRor13("NtdllDefWindowProc_W");
      errs() << "[MemLLVM] Note: Mapping forwarded API DefWindowProcW -> NtdllDefWindowProc_W\n";
    } else if (ApiName == "HeapAlloc") {
      ApiHash = calculateRor13("RtlAllocateHeap");
      errs() << "[MemLLVM] Note: Mapping forwarded API HeapAlloc -> RtlAllocateHeap\n";
    } else if (ApiName == "HeapFree") {
      ApiHash = calculateRor13("RtlFreeHeap");
      errs() << "[MemLLVM] Note: Mapping forwarded API HeapFree -> RtlFreeHeap\n";
    } else if (ApiName == "AcquireSRWLockExclusive") {
      ApiHash = calculateRor13("RtlAcquireSRWLockExclusive");
      errs() << "[MemLLVM] Note: Mapping forwarded API AcquireSRWLockExclusive -> RtlAcquireSRWLockExclusive\n";
    } else if (ApiName == "ReleaseSRWLockExclusive") {
      ApiHash = calculateRor13("RtlReleaseSRWLockExclusive");
      errs() << "[MemLLVM] Note: Mapping forwarded API ReleaseSRWLockExclusive -> RtlReleaseSRWLockExclusive\n";
    } else if (ApiName == "TryAcquireSRWLockExclusive") {
      ApiHash = calculateRor13("RtlTryAcquireSRWLockExclusive");
      errs() << "[MemLLVM] Note: Mapping forwarded API TryAcquireSRWLockExclusive -> RtlTryAcquireSRWLockExclusive\n";
    } else if (ApiName == "AcquireSRWLockShared") {
      ApiHash = calculateRor13("RtlAcquireSRWLockShared");
      errs() << "[MemLLVM] Note: Mapping forwarded API AcquireSRWLockShared -> RtlAcquireSRWLockShared\n";
    } else if (ApiName == "ReleaseSRWLockShared") {
      ApiHash = calculateRor13("RtlReleaseSRWLockShared");
      errs() << "[MemLLVM] Note: Mapping forwarded API ReleaseSRWLockShared -> RtlReleaseSRWLockShared\n";
    } else if (ApiName == "TryAcquireSRWLockShared") {
      ApiHash = calculateRor13("RtlTryAcquireSRWLockShared");
      errs() << "[MemLLVM] Note: Mapping forwarded API TryAcquireSRWLockShared -> RtlTryAcquireSRWLockShared\n";
    } else if (ApiName == "InitializeSRWLock") {
      ApiHash = calculateRor13("RtlInitializeSRWLock");
      errs() << "[MemLLVM] Note: Mapping forwarded API InitializeSRWLock -> RtlInitializeSRWLock\n";
    } else if (ApiName == "SleepConditionVariableSRW") {
      ApiHash = calculateRor13("RtlSleepConditionVariableSRW");
      errs() << "[MemLLVM] Note: Mapping forwarded API SleepConditionVariableSRW -> RtlSleepConditionVariableSRW\n";
    } else if (ApiName == "SleepConditionVariableCS") {
      ApiHash = calculateRor13("RtlSleepConditionVariableCS");
      errs() << "[MemLLVM] Note: Mapping forwarded API SleepConditionVariableCS -> RtlSleepConditionVariableCS\n";
    } else if (ApiName == "WakeConditionVariable") {
      ApiHash = calculateRor13("RtlWakeConditionVariable");
      errs() << "[MemLLVM] Note: Mapping forwarded API WakeConditionVariable -> RtlWakeConditionVariable\n";
    } else if (ApiName == "WakeAllConditionVariable") {
      ApiHash = calculateRor13("RtlWakeAllConditionVariable");
      errs() << "[MemLLVM] Note: Mapping forwarded API WakeAllConditionVariable -> RtlWakeAllConditionVariable\n";
    } else if (ApiName == "InitializeConditionVariable") {
      ApiHash = calculateRor13("RtlInitializeConditionVariable");
      errs() << "[MemLLVM] Note: Mapping forwarded API InitializeConditionVariable -> RtlInitializeConditionVariable\n";
    }
    ResolvedCallees.insert(Callee);

    errs() << "[MemLLVM] (" << (idx + 1) << "/" << CallsToResolve.size() 
           << ") Rewriting call to external API: " << ApiName 
           << " (ROR13: 0x" << Twine::utohexstr(ApiHash) << ") in function: "
           << (CI->getFunction() ? CI->getFunction()->getName() : "<none>") << "\n";
    errs().flush();

    IRBuilder<> Builder(CI);
    Value *RawFuncPtr = Builder.CreateCall(ResolverFn, {Builder.getInt32(ApiHash)}, "api_ptr");

    SmallVector<Value *, 8> Args;
    for (unsigned i = 0; i < CI->arg_size(); ++i) {
      Args.push_back(CI->getArgOperand(i));
    }

    CallInst *IndirectCall = Builder.CreateCall(CI->getFunctionType(), RawFuncPtr, Args);
    IndirectCall->setCallingConv(CI->getCallingConv());
    IndirectCall->setDebugLoc(CI->getDebugLoc());

    CI->replaceAllUsesWith(IndirectCall);
    CI->eraseFromParent();
    errs() << "   [Done " << (idx + 1) << "]\n";
    errs().flush();
  }

  // 3. External function declarations with zero uses do not need to be deleted
  // (leaving them avoids breaking LLVM CodeGen AST references or lowering passes)

  errs() << "[MemLLVM] ApiResolvePass successfully resolved "
         << CallsToResolve.size() << " call(s).\n";

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
