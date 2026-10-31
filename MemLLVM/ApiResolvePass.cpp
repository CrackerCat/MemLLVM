#include "MemLLVM.h"
#include "MemLLVMUtils.h"
#include "ResolverBitcode.h"
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

PreservedAnalyses ApiResolvePass::run(Module &M, ModuleAnalysisManager &MAM) {
  errs() << "[MemLLVM] Running ApiResolvePass on module: " << M.getName() << "\n";

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
              Callee->getName() != "MemLLVM_GetApiAddress") {
            CallsToResolve.push_back(CI);
          }
        }
      }
    }
  }

  if (CallsToResolve.empty()) {
    errs() << "[MemLLVM] No external Windows API calls found in module.\n";
    findOrSynthesizeEntryPoint(M);
    return PreservedAnalyses::all();
  }

  // 1. Ensure MemLLVM_GetApiAddress is linked into M
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
  }

  // 2. Rewrite each external call to dynamic indirect call
  std::set<Function *> ResolvedCallees;
  for (CallInst *CI : CallsToResolve) {
    Function *Callee = CI->getCalledFunction();
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
    }
    ResolvedCallees.insert(Callee);

    errs() << "[MemLLVM] Rewriting call to external API: " << ApiName 
           << " (ROR13: 0x" << Twine::utohexstr(ApiHash) << ")\n";

    IRBuilder<> Builder(CI);
    Value *RawFuncPtr = Builder.CreateCall(ResolverFn, {Builder.getInt32(ApiHash)}, "api_ptr");

    SmallVector<Value *, 8> Args;
    for (unsigned i = 0; i < CI->arg_size(); ++i) {
      Args.push_back(CI->getArgOperand(i));
    }

    CallInst *IndirectCall = Builder.CreateCall(CI->getFunctionType(), RawFuncPtr, Args);
    IndirectCall->setCallingConv(CI->getCallingConv());
    IndirectCall->setAttributes(CI->getAttributes());
    IndirectCall->setDebugLoc(CI->getDebugLoc());

    CI->replaceAllUsesWith(IndirectCall);
    CI->eraseFromParent();
  }

  // 3. Remove external function declarations
  for (Function *Callee : ResolvedCallees) {
    if (Callee->use_empty()) {
      errs() << "[MemLLVM] Erased external function declaration: " << Callee->getName() << "\n";
      Callee->eraseFromParent();
    }
  }

  errs() << "[MemLLVM] ApiResolvePass successfully resolved "
         << CallsToResolve.size() << " call(s).\n";

  // Ensure entry function remains strictly at offset 0x00 (front of module)
  findOrSynthesizeEntryPoint(M);

  return PreservedAnalyses::none();
}

} // namespace memllvm
