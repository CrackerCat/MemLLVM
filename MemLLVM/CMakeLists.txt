add_llvm_pass_plugin(MemLLVM
  MemLLVM.cpp
  MemLLVM.h
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
  target_compile_options(MemLLVM PRIVATE /W3 /wd4267 /wd4244 /wd4146 /wd4245 /wd4324 /wd4458)
endif()
