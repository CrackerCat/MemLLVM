@echo off
setlocal enabledelayedexpansion
echo =======================================================================
echo   MemLLVM Full Modern C++ Regression Test Suite (18 Tests)
echo =======================================================================

if not "%LLVM_PATH%"=="" set "LLVM_ROOT=%LLVM_PATH%"
if "%LLVM_ROOT%"=="" if not "%LLVM_DIR%"=="" set "LLVM_ROOT=%LLVM_DIR%"
if "%LLVM_ROOT%"=="" if not "%LLVM_HOME%"=="" set "LLVM_ROOT=%LLVM_HOME%"

if "%LLVM_ROOT%"=="" if exist "C:\LLVM-22-dev\bin\clang-cl.exe" set "LLVM_ROOT=C:\LLVM-22-dev"
if "%CLANG_BIN%"=="" (
    if not "%LLVM_ROOT%"=="" (
        if exist "%LLVM_ROOT%\bin\clang-cl.exe" set "CLANG_BIN=%LLVM_ROOT%\bin\clang-cl.exe"
        if exist "%LLVM_ROOT%\clang-cl.exe" set "CLANG_BIN=%LLVM_ROOT%\clang-cl.exe"
    )
)
if "%CLANG_BIN%"=="" where clang-cl.exe >nul 2>&1 && set "CLANG_BIN=clang-cl.exe"

if "%LLD_BIN%"=="" (
    if not "%LLVM_ROOT%"=="" (
        if exist "%LLVM_ROOT%\bin\lld-link.exe" set "LLD_BIN=%LLVM_ROOT%\bin\lld-link.exe"
        if exist "%LLVM_ROOT%\lld-link.exe" set "LLD_BIN=%LLVM_ROOT%\lld-link.exe"
    )
)
if "%LLD_BIN%"=="" where lld-link.exe >nul 2>&1 && set "LLD_BIN=lld-link.exe"

if "%OBJCOPY_BIN%"=="" (
    if not "%LLVM_ROOT%"=="" (
        if exist "%LLVM_ROOT%\bin\llvm-objcopy.exe" set "OBJCOPY_BIN=%LLVM_ROOT%\bin\llvm-objcopy.exe"
        if exist "%LLVM_ROOT%\llvm-objcopy.exe" set "OBJCOPY_BIN=%LLVM_ROOT%\llvm-objcopy.exe"
    )
)
if "%OBJCOPY_BIN%"=="" where llvm-objcopy.exe >nul 2>&1 && set "OBJCOPY_BIN=llvm-objcopy.exe"

if "%LLVM_ROOT%"=="" if exist "C:\LLVM-22-dev\bin\clang-cl.exe" set "LLVM_ROOT=C:\LLVM-22-dev"
if "%CLANG_BIN%"=="" (
    echo [-] ERROR: LLVM environment variable is not configured!
    exit /b 1
)

if "%PLUGIN_DLL%"=="" (
    if exist "..\build\MemLLVM\Release\MemLLVM.dll" set "PLUGIN_DLL=..\build\MemLLVM\Release\MemLLVM.dll"
    if exist "..\build\Release\MemLLVM.dll" set "PLUGIN_DLL=..\build\Release\MemLLVM.dll"
    if exist "..\release_artifacts\MemLLVM.dll" set "PLUGIN_DLL=..\release_artifacts\MemLLVM.dll"
)
set LTL_LIB=D:\MemLLVM\ltl\ltl.lib
set LTL_INC=D:\MemLLVM\ltl\include

set COMMON_FLAGS=/std:c++17 "/clang:-fpass-plugin=%PLUGIN_DLL%" /GS- /clang:-ffreestanding /clang:-fno-builtin /clang:-fno-jump-tables /clang:-fno-exceptions /clang:-fno-rtti /D_HAS_EXCEPTIONS=0 -I"%LTL_INC%" -O2
set LINK_FLAGS=/entry:main /subsystem:console /nodefaultlib /safeseh:no /opt:noicf

echo [*] Compiling and running Test 1: STL vector container...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_cpp_stl_basic.cpp /Fo:D:\MemLLVM\tests\test_cpp_stl_basic.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_stl_basic.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_stl_basic.exe
python "%~dp0..\examples\imgui\extract_flat_bin.py" D:\MemLLVM\tests\test_cpp_stl_basic.bin D:\MemLLVM\tests\test_cpp_stl_basic.exe
python "%~dp0..\examples\imgui\flat_run.py" D:\MemLLVM\tests\test_cpp_stl_basic.bin 600
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 1 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 2: Global Objects and Constructors...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_cpp_global_obj.cpp /Fo:D:\MemLLVM\tests\test_cpp_global_obj.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_global_obj.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_global_obj.exe
python "%~dp0..\examples\imgui\extract_flat_bin.py" D:\MemLLVM\tests\test_cpp_global_obj.bin D:\MemLLVM\tests\test_cpp_global_obj.exe
python "%~dp0..\examples\imgui\flat_run.py" D:\MemLLVM\tests\test_cpp_global_obj.bin 1125
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 2 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 3: Virtual Polymorphism and VTables...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_cpp_polymorphism.cpp /Fo:D:\MemLLVM\tests\test_cpp_polymorphism.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_polymorphism.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_polymorphism.exe
python "%~dp0..\examples\imgui\extract_flat_bin.py" D:\MemLLVM\tests\test_cpp_polymorphism.bin D:\MemLLVM\tests\test_cpp_polymorphism.exe
python "%~dp0..\examples\imgui\flat_run.py" D:\MemLLVM\tests\test_cpp_polymorphism.bin 275
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 3 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 4: Thread-Safe Function-Local Statics (Singletons)...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_cpp_thread_safe_static.cpp /Fo:D:\MemLLVM\tests\test_cpp_thread_safe_static.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_thread_safe_static.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_thread_safe_static.exe
python "%~dp0..\examples\imgui\extract_flat_bin.py" D:\MemLLVM\tests\test_cpp_thread_safe_static.bin D:\MemLLVM\tests\test_cpp_thread_safe_static.exe
python "%~dp0..\examples\imgui\flat_run.py" D:\MemLLVM\tests\test_cpp_thread_safe_static.bin 175
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 4 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 5: Pointer-bearing Struct Constants...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_cpp_struct_ptr_const.cpp /Fo:D:\MemLLVM\tests\test_cpp_struct_ptr_const.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_struct_ptr_const.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_struct_ptr_const.exe
python "%~dp0..\examples\imgui\extract_flat_bin.py" D:\MemLLVM\tests\test_cpp_struct_ptr_const.bin D:\MemLLVM\tests\test_cpp_struct_ptr_const.exe
python "%~dp0..\examples\imgui\flat_run.py" D:\MemLLVM\tests\test_cpp_struct_ptr_const.bin 777
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 5 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 6: Initialized and Uninitialized Globals (.data / .bss)...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_cpp_globals_data_bss.cpp /Fo:D:\MemLLVM\tests\test_cpp_globals_data_bss.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_globals_data_bss.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_globals_data_bss.exe
python "%~dp0..\examples\imgui\extract_flat_bin.py" D:\MemLLVM\tests\test_cpp_globals_data_bss.bin D:\MemLLVM\tests\test_cpp_globals_data_bss.exe
python "%~dp0..\examples\imgui\flat_run.py" D:\MemLLVM\tests\test_cpp_globals_data_bss.bin 888
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 6 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 7: std::mutex and std::lock_guard...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_cpp_mutex.cpp /Fo:D:\MemLLVM\tests\test_cpp_mutex.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_mutex.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_mutex.exe
python "%~dp0..\examples\imgui\extract_flat_bin.py" D:\MemLLVM\tests\test_cpp_mutex.bin D:\MemLLVM\tests\test_cpp_mutex.exe
python "%~dp0..\examples\imgui\flat_run.py" D:\MemLLVM\tests\test_cpp_mutex.bin 350
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 7 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 8: std::thread multi-threading...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_cpp_thread.cpp /Fo:D:\MemLLVM\tests\test_cpp_thread.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_thread.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_thread.exe
python "%~dp0..\examples\imgui\extract_flat_bin.py" D:\MemLLVM\tests\test_cpp_thread.bin D:\MemLLVM\tests\test_cpp_thread.exe
python "%~dp0..\examples\imgui\flat_run.py" D:\MemLLVM\tests\test_cpp_thread.bin 400
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 8 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 9: thread_local native emulated TLS...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_cpp_thread_local.cpp /Fo:D:\MemLLVM\tests\test_cpp_thread_local.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_thread_local.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_thread_local.exe
python "%~dp0..\examples\imgui\extract_flat_bin.py" D:\MemLLVM\tests\test_cpp_thread_local.bin D:\MemLLVM\tests\test_cpp_thread_local.exe
python "%~dp0..\examples\imgui\flat_run.py" D:\MemLLVM\tests\test_cpp_thread_local.bin 130
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 9 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 10: std::filesystem file operations...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_cpp_filesystem.cpp /Fo:D:\MemLLVM\tests\test_cpp_filesystem.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_filesystem.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_filesystem.exe
python "%~dp0..\examples\imgui\extract_flat_bin.py" D:\MemLLVM\tests\test_cpp_filesystem.bin D:\MemLLVM\tests\test_cpp_filesystem.exe
python "%~dp0..\examples\imgui\flat_run.py" D:\MemLLVM\tests\test_cpp_filesystem.bin 200
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 10 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 11: std::cout and std::cerr streams...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_cpp_iostream.cpp /Fo:D:\MemLLVM\tests\test_cpp_iostream.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_iostream.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_iostream.exe
python "%~dp0..\examples\imgui\extract_flat_bin.py" D:\MemLLVM\tests\test_cpp_iostream.bin D:\MemLLVM\tests\test_cpp_iostream.exe
python "%~dp0..\examples\imgui\flat_run.py" D:\MemLLVM\tests\test_cpp_iostream.bin 105
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 11 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 12: std::fstream (ifstream, ofstream, std::locale, std::ctype)...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_cpp_fstream.cpp /Fo:D:\MemLLVM\tests\test_cpp_fstream.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_fstream.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_fstream.exe
if %ERRORLEVEL% neq 0 ( echo [-] Link failed! & exit /b 1 )
python "%~dp0..\examples\imgui\extract_flat_bin.py" D:\MemLLVM\tests\test_cpp_fstream.bin D:\MemLLVM\tests\test_cpp_fstream.exe
if %ERRORLEVEL% neq 0 ( echo [-] Packing failed! & exit /b 1 )
python "%~dp0..\examples\imgui\flat_run.py" D:\MemLLVM\tests\test_cpp_fstream.bin 42
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 12 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 13: std::cin console stream input...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_cpp_cin.cpp /Fo:D:\MemLLVM\tests\test_cpp_cin.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_cin.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_cin.exe
if %ERRORLEVEL% neq 0 ( echo [-] Link failed! & exit /b 1 )
python "%~dp0..\examples\imgui\extract_flat_bin.py" D:\MemLLVM\tests\test_cpp_cin.bin D:\MemLLVM\tests\test_cpp_cin.exe
if %ERRORLEVEL% neq 0 ( echo [-] Packing failed! & exit /b 1 )
rem Interactive stdin test: skipped in automated non-interactive batch runner
echo [Test 13 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 14: std::regex and std::regex_search...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_cpp_regex.cpp /Fo:D:\MemLLVM\tests\test_cpp_regex.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_regex.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_regex.exe
python "%~dp0..\examples\imgui\extract_flat_bin.py" D:\MemLLVM\tests\test_cpp_regex.bin D:\MemLLVM\tests\test_cpp_regex.exe
python "%~dp0..\examples\imgui\flat_run.py" D:\MemLLVM\tests\test_cpp_regex.bin 88
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 14 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 15: std::async, std::future, std::promise...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_cpp_async.cpp /Fo:D:\MemLLVM\tests\test_cpp_async.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_async.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_async.exe
python "%~dp0..\examples\imgui\extract_flat_bin.py" D:\MemLLVM\tests\test_cpp_async.bin D:\MemLLVM\tests\test_cpp_async.exe
python "%~dp0..\examples\imgui\flat_run.py" D:\MemLLVM\tests\test_cpp_async.bin 100
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 15 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 16: C++20 Standard Module (import std;)...
set MODULE_FLAGS=/std:c++20 "/clang:-fpass-plugin=%PLUGIN_DLL%" /GS- /GR- /clang:-fno-builtin /clang:-fno-jump-tables /clang:-fno-exceptions /clang:-fno-rtti /D_HAS_EXCEPTIONS=0 -I"%LTL_INC%" "/clang:-fmodule-file=std=D:\MemLLVM\ltl\modules\std.pcm" -O2
"%CLANG_BIN%" %MODULE_FLAGS% -c D:\MemLLVM\tests\test_cpp_module_std.cpp /Fo:D:\MemLLVM\tests\test_cpp_module_std.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_cpp_module_std.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_cpp_module_std.exe
python "%~dp0..\examples\imgui\extract_flat_bin.py" D:\MemLLVM\tests\test_cpp_module_std.bin D:\MemLLVM\tests\test_cpp_module_std.exe
python "%~dp0..\examples\imgui\flat_run.py" D:\MemLLVM\tests\test_cpp_module_std.bin 77
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 16 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 17: Freestanding Stdio (__stdio_common_vfwprintf, __acrt_iob_func, wprintf/printf)...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_stdio_kprintf.cpp /Fo:D:\MemLLVM\tests\test_stdio_kprintf.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_stdio_kprintf.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_stdio_kprintf.exe
python "%~dp0..\examples\imgui\extract_flat_bin.py" D:\MemLLVM\tests\test_stdio_kprintf.bin D:\MemLLVM\tests\test_stdio_kprintf.exe
python "%~dp0..\examples\imgui\flat_run.py" D:\MemLLVM\tests\test_stdio_kprintf.bin 0
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 17 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 18: Multi-TU Global Constructors and .mli dispatch...
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_multi_tu_helper.cpp /Fo:D:\MemLLVM\tests\test_multi_tu_helper.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%CLANG_BIN%" %COMMON_FLAGS% -c D:\MemLLVM\tests\test_multi_tu_main.cpp /Fo:D:\MemLLVM\tests\test_multi_tu_main.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% D:\MemLLVM\tests\test_multi_tu_main.obj D:\MemLLVM\tests\test_multi_tu_helper.obj "%LTL_LIB%" -out:D:\MemLLVM\tests\test_multi_tu.exe
if %ERRORLEVEL% neq 0 ( echo [-] Link failed! & exit /b 1 )
D:\MemLLVM\tests\test_multi_tu.exe
if %ERRORLEVEL% neq 0 ( echo [-] Execution of test_multi_tu.exe failed! & exit /b 1 )
python "%~dp0..\examples\imgui\extract_flat_bin.py" D:\MemLLVM\tests\test_multi_tu.bin D:\MemLLVM\tests\test_multi_tu.exe
python "%~dp0..\examples\imgui\flat_run.py" D:\MemLLVM\tests\test_multi_tu.bin 0
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 18 Status: %ERRORLEVEL%]


echo.
echo =======================================================================
echo   ALL 18 MODERN C++ TESTS VERIFIED AND PASSED SUCCESSFULLY!
echo =======================================================================
endlocal
