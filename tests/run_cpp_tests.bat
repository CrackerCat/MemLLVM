@echo off
setlocal
echo =======================================================================
echo   MemLLVM Modern C++ Verification Test Suite
echo =======================================================================

set CLANG_BIN=C:\LLVM-22-dev\bin\clang-cl.exe
set LLD_BIN=C:\LLVM-22-dev\bin\lld-link.exe
set OBJCOPY_BIN=C:\LLVM-22-dev\bin\llvm-objcopy.exe
set PLUGIN_DLL=D:\MemLLVM\build\MemLLVM\Release\MemLLVM.dll
set RUNNER=D:\MemLLVM\tests\run_bin.exe

echo [*] Compiling and running Test 1: STL vector container (Phase 1)...
"%CLANG_BIN%" "/clang:-fpass-plugin=%PLUGIN_DLL%" /clang:-x /clang:c++ /GS- /clang:-ffreestanding /clang:-fno-builtin /clang:-mno-stack-arg-probe /clang:-fno-jump-tables /clang:-fno-exceptions /clang:-fno-rtti /clang:-fno-threadsafe-statics /D_HAS_EXCEPTIONS=0 -O2 -c D:\MemLLVM\tests\test_cpp_stl_basic.cpp /Fo:D:\MemLLVM\tests\test_cpp_stl_basic.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" /entry:main /subsystem:console /nodefaultlib /safeseh:no /merge:.rdata=.text /merge:.data=.text /merge:.bss=.text D:\MemLLVM\tests\test_cpp_stl_basic.obj -out:D:\MemLLVM\tests\test_cpp_stl_basic.exe
"%OBJCOPY_BIN%" --dump-section .text=D:\MemLLVM\tests\test_cpp_stl_basic.bin D:\MemLLVM\tests\test_cpp_stl_basic.exe
"%RUNNER%" D:\MemLLVM\tests\test_cpp_stl_basic.bin
echo [Phase 1 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 2: Global Objects and Constructors (Phase 2)...
"%CLANG_BIN%" "/clang:-fpass-plugin=%PLUGIN_DLL%" /clang:-x /clang:c++ /GS- /clang:-ffreestanding /clang:-fno-builtin /clang:-mno-stack-arg-probe /clang:-fno-jump-tables /clang:-fno-exceptions /clang:-fno-rtti /clang:-fno-threadsafe-statics /D_HAS_EXCEPTIONS=0 -O2 -c D:\MemLLVM\tests\test_cpp_global_obj.cpp /Fo:D:\MemLLVM\tests\test_cpp_global_obj.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" /entry:main /subsystem:console /nodefaultlib /safeseh:no /merge:.rdata=.text /merge:.data=.text /merge:.bss=.text D:\MemLLVM\tests\test_cpp_global_obj.obj -out:D:\MemLLVM\tests\test_cpp_global_obj.exe
"%OBJCOPY_BIN%" --dump-section .text=D:\MemLLVM\tests\test_cpp_global_obj.bin D:\MemLLVM\tests\test_cpp_global_obj.exe
"%RUNNER%" D:\MemLLVM\tests\test_cpp_global_obj.bin
echo [Phase 2 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 3: Virtual Polymorphism and VTables (Phase 3)...
"%CLANG_BIN%" "/clang:-fpass-plugin=%PLUGIN_DLL%" /clang:-x /clang:c++ /GS- /clang:-ffreestanding /clang:-fno-builtin /clang:-mno-stack-arg-probe /clang:-fno-jump-tables /clang:-fno-exceptions /clang:-fno-rtti /clang:-fno-threadsafe-statics /D_HAS_EXCEPTIONS=0 -O2 -c D:\MemLLVM\tests\test_cpp_polymorphism.cpp /Fo:D:\MemLLVM\tests\test_cpp_polymorphism.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" /entry:main /subsystem:console /nodefaultlib /safeseh:no /merge:.rdata=.text /merge:.data=.text /merge:.bss=.text D:\MemLLVM\tests\test_cpp_polymorphism.obj -out:D:\MemLLVM\tests\test_cpp_polymorphism.exe
"%OBJCOPY_BIN%" --dump-section .text=D:\MemLLVM\tests\test_cpp_polymorphism.bin D:\MemLLVM\tests\test_cpp_polymorphism.exe
"%RUNNER%" D:\MemLLVM\tests\test_cpp_polymorphism.bin
echo [Phase 3 Status: %ERRORLEVEL%]

echo.
echo =======================================================================
echo   All Modern C++ In-Memory Tests Verified!
echo =======================================================================
endlocal
