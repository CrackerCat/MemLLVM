add_llvm_pass_plugin(MemLLVM
  MemLLVM.h
  MemLLVMUtils.h
  MemLLVMUtils.cpp
  SdkImportResolver.h
  SdkImportResolver.cpp
  AutoMemoryPass.cpp
  StlStubPass.cpp
  GlobalLifecyclePass.cpp
  GlobalSectionPass.cpp
  GlobalPointerPass.cpp
  AutoLoadLibraryPass.cpp
  StackStringPass.cpp
  ApiResolvePass.cpp
  PassRegistry.cpp
  SelfTestEngine.cpp
  ResolverBitcode.h
  MemLLVM.def
)

target_include_directories(MemLLVM PRIVATE
  ${LLVM_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(MemLLVM PRIVATE ${LLVM_DEFINITIONS})

if (COMMAND llvm_map_components_to_libnames)
  llvm_map_components_to_libnames(MEM_LLVM_LIBS Core Analysis Passes TransformUtils Support Linker BitReader)
  target_link_libraries(MemLLVM PRIVATE ${MEM_LLVM_LIBS})
endif()

if (MSVC)
  # /utf-8: read sources as UTF-8 and set the execution charset to UTF-8.
  #
  # Without it MSVC decodes the file with the system ANSI code page (936 on a
  # zh-CN host). UTF-8 comments then get read as GBK, and because 0x7C ('|') is
  # a legal GBK trail byte a dangling lead byte at the end of a comment can eat
  # one '|' out of a '||'. The result still compiles -- it just silently becomes
  # a bitwise or, which is how `Name == "_fileno"` dropped out of an
  # isStaticShimSymbol() chain while every diagnostic still looked correct.
  target_compile_options(MemLLVM PRIVATE /W3 /utf-8
    /wd4267 /wd4244 /wd4146 /wd4245 /wd4324 /wd4458)
endif()

# Mirror the freshly built plugin to <repo>/bin/MemLLVM.dll.
#
# The integration guide tells external .vcxproj files to use
#   /clang:-fpass-plugin="$(MEMLLVM_DIR)\bin\MemLLVM.dll"
# but the CMake build only ever produced build/MemLLVM/Release/MemLLVM.dll, so
# that path either did not exist or held a stale copy -- and a stale plugin
# fails in the worst possible way (it silently compiles, just without the
# newest transformations). Keeping bin/ in sync makes the documented path mean
# what it says.
add_custom_command(TARGET MemLLVM POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/bin"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:MemLLVM>" "${CMAKE_SOURCE_DIR}/bin/MemLLVM.dll"
  COMMENT "Mirroring MemLLVM.dll -> bin/MemLLVM.dll"
)
