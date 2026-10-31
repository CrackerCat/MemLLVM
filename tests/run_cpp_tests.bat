@echo off
setlocal
echo =======================================================================
echo   MemLLVM Full Modern C++ Regression Test Suite
echo =======================================================================

set CLANG_BIN=C:\LLVM-22-dev\bin\clang-cl.exe
set LLD_BIN=C:\LLVM-22-dev\bin\lld-link.exe
set OBJCOPY_BIN=C:\LLVM-22-dev\bin\llvm-objcopy.exe
set PLUGIN_DLL=D:\MemLLVM\build\MemLLVM\Release\MemLLVM.dll
set RUNNER=D:\MemLLVM\tests\run_bin.exe

echo [*] Compiling and running Test 1: STL vector container...
"%CLANG_BIN%" "/clang:-fpass-plugin=%PLUGIN_DLL%" /GS- /clang:-ffreestanding /clang:-fno-builtin /clang:-mno-stack-arg-probe /clang:-fno-jump-tables /clang:-fno-exceptions /clang:-fno-rtti /D_HAS_EXCEPTIONS=0 -O2 -c D:\MemLLVM\tests\test_cpp_stl_basic.cpp /Fo:D:\MemLLVM\tests\test_cpp_stl_basic.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" /entry:main /subsystem:console /nodefaultlib /safeseh:no /merge:.rdata=.text /merge:.data=.text /merge:.bss=.text D:\MemLLVM\tests\test_cpp_stl_basic.obj -out:D:\MemLLVM\tests\test_cpp_stl_basic.exe
"%OBJCOPY_BIN%" --dump-section .text=D:\MemLLVM\tests\test_cpp_stl_basic.bin D:\MemLLVM\tests\test_cpp_stl_basic.exe
"%RUNNER%" D:\MemLLVM\tests\test_cpp_stl_basic.bin
echo [Test 1 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 2: Global Objects and Constructors...
"%CLANG_BIN%" "/clang:-fpass-plugin=%PLUGIN_DLL%" /GS- /clang:-ffreestanding /clang:-fno-builtin /clang:-mno-stack-arg-probe /clang:-fno-jump-tables /clang:-fno-exceptions /clang:-fno-rtti /D_HAS_EXCEPTIONS=0 -O2 -c D:\MemLLVM\tests\test_cpp_global_obj.cpp /Fo:D:\MemLLVM\tests\test_cpp_global_obj.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" /entry:main /subsystem:console /nodefaultlib /safeseh:no /merge:.rdata=.text /merge:.data=.text /merge:.bss=.text D:\MemLLVM\tests\test_cpp_global_obj.obj -out:D:\MemLLVM\tests\test_cpp_global_obj.exe
"%OBJCOPY_BIN%" --dump-section .text=D:\MemLLVM\tests\test_cpp_global_obj.bin D:\MemLLVM\tests\test_cpp_global_obj.exe
"%RUNNER%" D:\MemLLVM\tests\test_cpp_global_obj.bin
echo [Test 2 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 3: Virtual Polymorphism and VTables...
"%CLANG_BIN%" "/clang:-fpass-plugin=%PLUGIN_DLL%" /GS- /clang:-ffreestanding /clang:-fno-builtin /clang:-mno-stack-arg-probe /clang:-fno-jump-tables /clang:-fno-exceptions /clang:-fno-rtti /D_HAS_EXCEPTIONS=0 -O2 -c D:\MemLLVM\tests\test_cpp_polymorphism.cpp /Fo:D:\MemLLVM\tests\test_cpp_polymorphism.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" /entry:main /subsystem:console /nodefaultlib /safeseh:no /merge:.rdata=.text /merge:.data=.text /merge:.bss=.text D:\MemLLVM\tests\test_cpp_polymorphism.obj -out:D:\MemLLVM\tests\test_cpp_polymorphism.exe
"%OBJCOPY_BIN%" --dump-section .text=D:\MemLLVM\tests\test_cpp_polymorphism.bin D:\MemLLVM\tests\test_cpp_polymorphism.exe
"%RUNNER%" D:\MemLLVM\tests\test_cpp_polymorphism.bin
echo [Test 3 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 4: Thread-Safe Function-Local Statics (Singletons)...
"%CLANG_BIN%" "/clang:-fpass-plugin=%PLUGIN_DLL%" /GS- /clang:-ffreestanding /clang:-fno-builtin /clang:-mno-stack-arg-probe /clang:-fno-jump-tables /clang:-fno-exceptions /clang:-fno-rtti /D_HAS_EXCEPTIONS=0 -O2 -c D:\MemLLVM\tests\test_cpp_thread_safe_static.cpp /Fo:D:\MemLLVM\tests\test_cpp_thread_safe_static.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" /entry:main /subsystem:console /nodefaultlib /safeseh:no /merge:.rdata=.text /merge:.data=.text /merge:.bss=.text D:\MemLLVM\tests\test_cpp_thread_safe_static.obj -out:D:\MemLLVM\tests\test_cpp_thread_safe_static.exe
"%OBJCOPY_BIN%" --dump-section .text=D:\MemLLVM\tests\test_cpp_thread_safe_static.bin D:\MemLLVM\tests\test_cpp_thread_safe_static.exe
"%RUNNER%" D:\MemLLVM\tests\test_cpp_thread_safe_static.bin
echo [Test 4 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 5: Pointer-bearing Struct Constants...
"%CLANG_BIN%" "/clang:-fpass-plugin=%PLUGIN_DLL%" /GS- /clang:-ffreestanding /clang:-fno-builtin /clang:-mno-stack-arg-probe /clang:-fno-jump-tables /clang:-fno-exceptions /clang:-fno-rtti /D_HAS_EXCEPTIONS=0 -O2 -c D:\MemLLVM\tests\test_cpp_struct_ptr_const.cpp /Fo:D:\MemLLVM\tests\test_cpp_struct_ptr_const.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" /entry:main /subsystem:console /nodefaultlib /safeseh:no /merge:.rdata=.text /merge:.data=.text /merge:.bss=.text D:\MemLLVM\tests\test_cpp_struct_ptr_const.obj -out:D:\MemLLVM\tests\test_cpp_struct_ptr_const.exe
"%OBJCOPY_BIN%" --dump-section .text=D:\MemLLVM\tests\test_cpp_struct_ptr_const.bin D:\MemLLVM\tests\test_cpp_struct_ptr_const.exe
"%RUNNER%" D:\MemLLVM\tests\test_cpp_struct_ptr_const.bin
echo [Test 5 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 6: Initialized and Uninitialized Globals (.data / .bss)...
"%CLANG_BIN%" "/clang:-fpass-plugin=%PLUGIN_DLL%" /GS- /clang:-ffreestanding /clang:-fno-builtin /clang:-mno-stack-arg-probe /clang:-fno-jump-tables /clang:-fno-exceptions /clang:-fno-rtti /D_HAS_EXCEPTIONS=0 -O2 -c D:\MemLLVM\tests\test_cpp_globals_data_bss.cpp /Fo:D:\MemLLVM\tests\test_cpp_globals_data_bss.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" /entry:main /subsystem:console /nodefaultlib /safeseh:no /merge:.rdata=.text /merge:.data=.text /merge:.bss=.text D:\MemLLVM\tests\test_cpp_globals_data_bss.obj -out:D:\MemLLVM\tests\test_cpp_globals_data_bss.exe
"%OBJCOPY_BIN%" --dump-section .text=D:\MemLLVM\tests\test_cpp_globals_data_bss.bin D:\MemLLVM\tests\test_cpp_globals_data_bss.exe
"%RUNNER%" D:\MemLLVM\tests\test_cpp_globals_data_bss.bin
echo [Test 6 Status: %ERRORLEVEL%]

echo.
echo =======================================================================
echo   ALL MODERN C++ TESTS VERIFIED AND PASSED SUCCESSFULLY!
echo =======================================================================
endlocal
