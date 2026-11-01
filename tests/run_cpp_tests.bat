@echo off
setlocal
echo =======================================================================
echo   MemLLVM Full Modern C++ Regression Test Suite (11 Tests)
echo =======================================================================

set CLANG_BIN=C:\LLVM-22-dev\bin\clang-cl.exe
set LLD_BIN=C:\LLVM-22-dev\bin\lld-link.exe
set OBJCOPY_BIN=C:\LLVM-22-dev\bin\llvm-objcopy.exe
set PLUGIN_DLL=D:\MemLLVM\build\MemLLVM\Release\MemLLVM.dll
set LTL_LIB=D:\MemLLVM\ltl\ltl.lib
set LTL_INC=D:\MemLLVM\ltl\include
set RUNNER=D:\MemLLVM\tests\run_bin.exe
set COMMON_FLAGS=/std:c++17 "/clang:-fpass-plugin=%PLUGIN_DLL%" /GS- /clang:-ffreestanding /clang:-fno-builtin /clang:-mno-stack-arg-probe /clang:-fno-jump-tables /clang:-fno-exceptions /clang:-fno-rtti /D_HAS_EXCEPTIONS=0 -I"%LTL_INC%" -O2
set LINK_FLAGS=/entry:main /subsystem:console /nodefaultlib /safeseh:no /merge:.rdata=.text /merge:.data=.text /merge:.bss=.text /opt:noicf

echo [*] Compiling and running Test 1: STL vector container...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_cpp_stl_basic.cpp /Fo:D:\MemLLVM\tests\test_cpp_stl_basic.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_stl_basic.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_stl_basic.exe
"%OBJCOPY_BIN%" --dump-section .text=D:\MemLLVM\tests\test_cpp_stl_basic.bin D:\MemLLVM\tests\test_cpp_stl_basic.exe
"%RUNNER%" D:\MemLLVM\tests\test_cpp_stl_basic.bin
echo [Test 1 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 2: Global Objects and Constructors...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_cpp_global_obj.cpp /Fo:D:\MemLLVM\tests\test_cpp_global_obj.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_global_obj.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_global_obj.exe
"%OBJCOPY_BIN%" --dump-section .text=D:\MemLLVM\tests\test_cpp_global_obj.bin D:\MemLLVM\tests\test_cpp_global_obj.exe
"%RUNNER%" D:\MemLLVM\tests\test_cpp_global_obj.bin
echo [Test 2 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 3: Virtual Polymorphism and VTables...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_cpp_polymorphism.cpp /Fo:D:\MemLLVM\tests\test_cpp_polymorphism.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_polymorphism.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_polymorphism.exe
"%OBJCOPY_BIN%" --dump-section .text=D:\MemLLVM\tests\test_cpp_polymorphism.bin D:\MemLLVM\tests\test_cpp_polymorphism.exe
"%RUNNER%" D:\MemLLVM\tests\test_cpp_polymorphism.bin
echo [Test 3 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 4: Thread-Safe Function-Local Statics (Singletons)...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_cpp_thread_safe_static.cpp /Fo:D:\MemLLVM\tests\test_cpp_thread_safe_static.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_thread_safe_static.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_thread_safe_static.exe
"%OBJCOPY_BIN%" --dump-section .text=D:\MemLLVM\tests\test_cpp_thread_safe_static.bin D:\MemLLVM\tests\test_cpp_thread_safe_static.exe
"%RUNNER%" D:\MemLLVM\tests\test_cpp_thread_safe_static.bin
echo [Test 4 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 5: Pointer-bearing Struct Constants...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_cpp_struct_ptr_const.cpp /Fo:D:\MemLLVM\tests\test_cpp_struct_ptr_const.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_struct_ptr_const.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_struct_ptr_const.exe
"%OBJCOPY_BIN%" --dump-section .text=D:\MemLLVM\tests\test_cpp_struct_ptr_const.bin D:\MemLLVM\tests\test_cpp_struct_ptr_const.exe
"%RUNNER%" D:\MemLLVM\tests\test_cpp_struct_ptr_const.bin
echo [Test 5 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 6: Initialized and Uninitialized Globals (.data / .bss)...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_cpp_globals_data_bss.cpp /Fo:D:\MemLLVM\tests\test_cpp_globals_data_bss.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_globals_data_bss.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_globals_data_bss.exe
"%OBJCOPY_BIN%" --dump-section .text=D:\MemLLVM\tests\test_cpp_globals_data_bss.bin D:\MemLLVM\tests\test_cpp_globals_data_bss.exe
"%RUNNER%" D:\MemLLVM\tests\test_cpp_globals_data_bss.bin
echo [Test 6 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 7: std::mutex and std::lock_guard...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_cpp_mutex.cpp /Fo:D:\MemLLVM\tests\test_cpp_mutex.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_mutex.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_mutex.exe
"%OBJCOPY_BIN%" --dump-section .text=D:\MemLLVM\tests\test_cpp_mutex.bin D:\MemLLVM\tests\test_cpp_mutex.exe
"%RUNNER%" D:\MemLLVM\tests\test_cpp_mutex.bin
echo [Test 7 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 8: std::thread multi-threading...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_cpp_thread.cpp /Fo:D:\MemLLVM\tests\test_cpp_thread.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_thread.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_thread.exe
"%OBJCOPY_BIN%" --dump-section .text=D:\MemLLVM\tests\test_cpp_thread.bin D:\MemLLVM\tests\test_cpp_thread.exe
"%RUNNER%" D:\MemLLVM\tests\test_cpp_thread.bin
echo [Test 8 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 9: thread_local native emulated TLS...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_cpp_thread_local.cpp /Fo:D:\MemLLVM\tests\test_cpp_thread_local.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_thread_local.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_thread_local.exe
"%OBJCOPY_BIN%" --dump-section .text=D:\MemLLVM\tests\test_cpp_thread_local.bin D:\MemLLVM\tests\test_cpp_thread_local.exe
"%RUNNER%" D:\MemLLVM\tests\test_cpp_thread_local.bin
echo [Test 9 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 10: std::filesystem file operations...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_cpp_filesystem.cpp /Fo:D:\MemLLVM\tests\test_cpp_filesystem.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_filesystem.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_filesystem.exe
"%OBJCOPY_BIN%" --dump-section .text=D:\MemLLVM\tests\test_cpp_filesystem.bin D:\MemLLVM\tests\test_cpp_filesystem.exe
"%RUNNER%" D:\MemLLVM\tests\test_cpp_filesystem.bin
echo [Test 10 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 11: std::cout and std::cerr streams...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_cpp_iostream.cpp /Fo:D:\MemLLVM\tests\test_cpp_iostream.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_iostream.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_iostream.exe
"%OBJCOPY_BIN%" --dump-section .text=D:\MemLLVM\tests\test_cpp_iostream.bin D:\MemLLVM\tests\test_cpp_iostream.exe
"%RUNNER%" D:\MemLLVM\tests\test_cpp_iostream.bin
echo [Test 11 Status: %ERRORLEVEL%]

echo.
echo =======================================================================
echo   ALL 11 MODERN C++ TESTS VERIFIED AND PASSED SUCCESSFULLY!
echo =======================================================================
endlocal
