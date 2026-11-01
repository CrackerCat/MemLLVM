cmake_minimum_required (VERSION 3.20)

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("MemLLVM" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT DEFINED LLVM_DIR)
  if (DEFINED ENV{LLVM_DIR} AND NOT "$ENV{LLVM_DIR}" STREQUAL "")
    set(LLVM_DIR "$ENV{LLVM_DIR}" CACHE PATH "Path to LLVM cmake directory")
  elseif (DEFINED ENV{LLVM_PATH} AND NOT "$ENV{LLVM_PATH}" STREQUAL "")
    if (EXISTS "$ENV{LLVM_PATH}/lib/cmake/llvm")
      set(LLVM_DIR "$ENV{LLVM_PATH}/lib/cmake/llvm" CACHE PATH "Path to LLVM cmake directory")
    else()
      set(LLVM_DIR "$ENV{LLVM_PATH}" CACHE PATH "Path to LLVM cmake directory")
    endif()
  endif()
endif()

if (NOT DEFINED LLVM_DIR)
  message(FATAL_ERROR
    "\n=======================================================================\n"
    "[-] ERROR: LLVM environment variable is not configured!\n"
    "[-] You MUST set LLVM_PATH or LLVM_DIR before configuring MemLLVM:\n"
    "[-]   Windows CMD: set LLVM_PATH=C:\\path\\to\\llvm\n"
    "[-]   PowerShell : $env:LLVM_PATH=\"C:\\path\\to\\llvm\"\n"
    "[-]   Permanent  : setx LLVM_PATH \"C:\\path\\to\\llvm\"\n"
    "=======================================================================\n"
  )
endif()

find_package(LLVM REQUIRED CONFIG PATHS "${LLVM_DIR}" NO_DEFAULT_PATH)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "LLVM include dirs: ${LLVM_INCLUDE_DIRS}")

# Enable out-of-tree plugins
set(LLVM_ENABLE_PLUGINS ON)
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

add_subdirectory ("MemLLVM")
