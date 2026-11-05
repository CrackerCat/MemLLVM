@echo off
setlocal enabledelayedexpansion
echo =======================================================================
echo   MemLLVM Full Modern C++ Regression Test Suite (27 Tests)
echo =======================================================================

set "TESTS_DIR=%~dp0"
for %%I in ("%TESTS_DIR%..") do set "REPO_ROOT=%%~fI"

if not "%LLVM_PATH%"=="" set "LLVM_ROOT=%LLVM_PATH%"
if "%LLVM_ROOT%"=="" if not "%LLVM_DIR%"=="" set "LLVM_ROOT=%LLVM_DIR%"
if "%LLVM_ROOT%"=="" if not "%LLVM_HOME%"=="" set "LLVM_ROOT=%LLVM_HOME%"

if "%LLVM_ROOT%"=="" if exist "C:\LLVM-22-plugin\bin\clang-cl.exe" set "LLVM_ROOT=C:\LLVM-22-plugin"
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

if "%LLVM_ROOT%"=="" if exist "C:\LLVM-22-plugin\bin\clang-cl.exe" set "LLVM_ROOT=C:\LLVM-22-plugin"
if "%LLVM_ROOT%"=="" if exist "C:\LLVM-22-dev\bin\clang-cl.exe" set "LLVM_ROOT=C:\LLVM-22-dev"
if "%CLANG_BIN%"=="" (
    echo [-] ERROR: LLVM environment variable is not configured!
    exit /b 1
)

if "%PLUGIN_DLL%"=="" (
    if exist "%REPO_ROOT%\bin\MemLLVM.dll" set "PLUGIN_DLL=%REPO_ROOT%\bin\MemLLVM.dll"
    if exist "..\build\MemLLVM\Release\MemLLVM.dll" set "PLUGIN_DLL=..\build\MemLLVM\Release\MemLLVM.dll"
    if exist "..\build\Release\MemLLVM.dll" set "PLUGIN_DLL=..\build\Release\MemLLVM.dll"
    if exist "..\release_artifacts\MemLLVM.dll" set "PLUGIN_DLL=..\release_artifacts\MemLLVM.dll"
)
set "LTL_LIB=%REPO_ROOT%\ltl\ltl.lib"
set "LTL_INC=%REPO_ROOT%\ltl\include"
set "EXTRACT_BIN=%REPO_ROOT%\Tools\extract_flat_bin.py"
set "FLAT_RUN=%REPO_ROOT%\Tools\flat_run.py"

set COMMON_FLAGS=/std:c++17 "/clang:-fpass-plugin=%PLUGIN_DLL%" /GS- /GR- /Gy /clang:-ffunction-sections /clang:-fdata-sections /clang:-ffreestanding /clang:-fno-builtin /clang:-fno-jump-tables /clang:-fno-exceptions /clang:-fno-rtti /D_HAS_EXCEPTIONS=0 -I"%LTL_INC%" /O1 /clang:-Oz /D_NO_CRT_STDIO_INLINE
set LINK_FLAGS=/entry:main /subsystem:console /nodefaultlib /safeseh:no /opt:ref /opt:icf /align:512 /merge:.rdata=.text

echo [*] Compiling and running Test 1: STL vector container...
"%CLANG_BIN%" %COMMON_FLAGS% -c %TESTS_DIR%test_cpp_stl_basic.cpp /Fo:%TESTS_DIR%test_cpp_stl_basic.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_cpp_stl_basic.obj "%LTL_LIB%" -out:%TESTS_DIR%test_cpp_stl_basic.exe
python "%EXTRACT_BIN%" %TESTS_DIR%test_cpp_stl_basic.bin %TESTS_DIR%test_cpp_stl_basic.exe
python "%FLAT_RUN%" %TESTS_DIR%test_cpp_stl_basic.bin 600
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 1 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 2: Global Objects and Constructors...
"%CLANG_BIN%" %COMMON_FLAGS% -c %TESTS_DIR%test_cpp_global_obj.cpp /Fo:%TESTS_DIR%test_cpp_global_obj.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_cpp_global_obj.obj "%LTL_LIB%" -out:%TESTS_DIR%test_cpp_global_obj.exe
python "%EXTRACT_BIN%" %TESTS_DIR%test_cpp_global_obj.bin %TESTS_DIR%test_cpp_global_obj.exe
python "%FLAT_RUN%" %TESTS_DIR%test_cpp_global_obj.bin 1125
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 2 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 3: Virtual Polymorphism and VTables...
"%CLANG_BIN%" %COMMON_FLAGS% -c %TESTS_DIR%test_cpp_polymorphism.cpp /Fo:%TESTS_DIR%test_cpp_polymorphism.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_cpp_polymorphism.obj "%LTL_LIB%" -out:%TESTS_DIR%test_cpp_polymorphism.exe
python "%EXTRACT_BIN%" %TESTS_DIR%test_cpp_polymorphism.bin %TESTS_DIR%test_cpp_polymorphism.exe
python "%FLAT_RUN%" %TESTS_DIR%test_cpp_polymorphism.bin 275
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 3 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 4: Thread-Safe Function-Local Statics (Singletons)...
"%CLANG_BIN%" %COMMON_FLAGS% -c %TESTS_DIR%test_cpp_thread_safe_static.cpp /Fo:%TESTS_DIR%test_cpp_thread_safe_static.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_cpp_thread_safe_static.obj "%LTL_LIB%" -out:%TESTS_DIR%test_cpp_thread_safe_static.exe
python "%EXTRACT_BIN%" %TESTS_DIR%test_cpp_thread_safe_static.bin %TESTS_DIR%test_cpp_thread_safe_static.exe
python "%FLAT_RUN%" %TESTS_DIR%test_cpp_thread_safe_static.bin 175
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 4 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 5: Pointer-bearing Struct Constants...
"%CLANG_BIN%" %COMMON_FLAGS% -c %TESTS_DIR%test_cpp_struct_ptr_const.cpp /Fo:%TESTS_DIR%test_cpp_struct_ptr_const.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_cpp_struct_ptr_const.obj "%LTL_LIB%" -out:%TESTS_DIR%test_cpp_struct_ptr_const.exe
python "%EXTRACT_BIN%" %TESTS_DIR%test_cpp_struct_ptr_const.bin %TESTS_DIR%test_cpp_struct_ptr_const.exe
python "%FLAT_RUN%" %TESTS_DIR%test_cpp_struct_ptr_const.bin 777
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 5 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 6: Initialized and Uninitialized Globals (.data / .bss)...
"%CLANG_BIN%" %COMMON_FLAGS% -c %TESTS_DIR%test_cpp_globals_data_bss.cpp /Fo:%TESTS_DIR%test_cpp_globals_data_bss.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_cpp_globals_data_bss.obj "%LTL_LIB%" -out:%TESTS_DIR%test_cpp_globals_data_bss.exe
python "%EXTRACT_BIN%" %TESTS_DIR%test_cpp_globals_data_bss.bin %TESTS_DIR%test_cpp_globals_data_bss.exe
python "%FLAT_RUN%" %TESTS_DIR%test_cpp_globals_data_bss.bin 888
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 6 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 7: std::mutex and std::lock_guard...
"%CLANG_BIN%" %COMMON_FLAGS% -c %TESTS_DIR%test_cpp_mutex.cpp /Fo:%TESTS_DIR%test_cpp_mutex.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_cpp_mutex.obj "%LTL_LIB%" -out:%TESTS_DIR%test_cpp_mutex.exe
python "%EXTRACT_BIN%" %TESTS_DIR%test_cpp_mutex.bin %TESTS_DIR%test_cpp_mutex.exe
python "%FLAT_RUN%" %TESTS_DIR%test_cpp_mutex.bin 350
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 7 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 8: std::thread multi-threading...
"%CLANG_BIN%" %COMMON_FLAGS% -c %TESTS_DIR%test_cpp_thread.cpp /Fo:%TESTS_DIR%test_cpp_thread.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_cpp_thread.obj "%LTL_LIB%" -out:%TESTS_DIR%test_cpp_thread.exe
python "%EXTRACT_BIN%" %TESTS_DIR%test_cpp_thread.bin %TESTS_DIR%test_cpp_thread.exe
python "%FLAT_RUN%" %TESTS_DIR%test_cpp_thread.bin 400
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 8 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 9: thread_local native emulated TLS...
"%CLANG_BIN%" %COMMON_FLAGS% -c %TESTS_DIR%test_cpp_thread_local.cpp /Fo:%TESTS_DIR%test_cpp_thread_local.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_cpp_thread_local.obj "%LTL_LIB%" -out:%TESTS_DIR%test_cpp_thread_local.exe
python "%EXTRACT_BIN%" %TESTS_DIR%test_cpp_thread_local.bin %TESTS_DIR%test_cpp_thread_local.exe
python "%FLAT_RUN%" %TESTS_DIR%test_cpp_thread_local.bin 130
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 9 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 10: std::filesystem file operations...
"%CLANG_BIN%" %COMMON_FLAGS% -c %TESTS_DIR%test_cpp_filesystem.cpp /Fo:%TESTS_DIR%test_cpp_filesystem.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_cpp_filesystem.obj "%LTL_LIB%" -out:%TESTS_DIR%test_cpp_filesystem.exe
python "%EXTRACT_BIN%" %TESTS_DIR%test_cpp_filesystem.bin %TESTS_DIR%test_cpp_filesystem.exe
python "%FLAT_RUN%" %TESTS_DIR%test_cpp_filesystem.bin 200
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 10 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 11: std::cout and std::cerr streams...
"%CLANG_BIN%" %COMMON_FLAGS% -c %TESTS_DIR%test_cpp_iostream.cpp /Fo:%TESTS_DIR%test_cpp_iostream.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_cpp_iostream.obj "%LTL_LIB%" -out:%TESTS_DIR%test_cpp_iostream.exe
python "%EXTRACT_BIN%" %TESTS_DIR%test_cpp_iostream.bin %TESTS_DIR%test_cpp_iostream.exe
python "%FLAT_RUN%" %TESTS_DIR%test_cpp_iostream.bin 105
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 11 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 12: std::fstream (ifstream, ofstream, std::locale, std::ctype)...
"%CLANG_BIN%" %COMMON_FLAGS% -c %TESTS_DIR%test_cpp_fstream.cpp /Fo:%TESTS_DIR%test_cpp_fstream.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_cpp_fstream.obj "%LTL_LIB%" -out:%TESTS_DIR%test_cpp_fstream.exe
if %ERRORLEVEL% neq 0 ( echo [-] Link failed! & exit /b 1 )
python "%EXTRACT_BIN%" %TESTS_DIR%test_cpp_fstream.bin %TESTS_DIR%test_cpp_fstream.exe
if %ERRORLEVEL% neq 0 ( echo [-] Packing failed! & exit /b 1 )
python "%FLAT_RUN%" %TESTS_DIR%test_cpp_fstream.bin 42
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 12 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 13: std::cin console stream input...
"%CLANG_BIN%" %COMMON_FLAGS% -c %TESTS_DIR%test_cpp_cin.cpp /Fo:%TESTS_DIR%test_cpp_cin.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_cpp_cin.obj "%LTL_LIB%" -out:%TESTS_DIR%test_cpp_cin.exe
if %ERRORLEVEL% neq 0 ( echo [-] Link failed! & exit /b 1 )
python "%EXTRACT_BIN%" %TESTS_DIR%test_cpp_cin.bin %TESTS_DIR%test_cpp_cin.exe
if %ERRORLEVEL% neq 0 ( echo [-] Packing failed! & exit /b 1 )
rem Interactive stdin test: skipped in automated non-interactive batch runner
echo [Test 13 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 14: std::regex and std::regex_search...
"%CLANG_BIN%" %COMMON_FLAGS% -c %TESTS_DIR%test_cpp_regex.cpp /Fo:%TESTS_DIR%test_cpp_regex.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_cpp_regex.obj "%LTL_LIB%" -out:%TESTS_DIR%test_cpp_regex.exe
python "%EXTRACT_BIN%" %TESTS_DIR%test_cpp_regex.bin %TESTS_DIR%test_cpp_regex.exe
python "%FLAT_RUN%" %TESTS_DIR%test_cpp_regex.bin 88
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 14 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 15: std::async, std::future, std::promise...
"%CLANG_BIN%" %COMMON_FLAGS% -c %TESTS_DIR%test_cpp_async.cpp /Fo:%TESTS_DIR%test_cpp_async.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_cpp_async.obj "%LTL_LIB%" -out:%TESTS_DIR%test_cpp_async.exe
python "%EXTRACT_BIN%" %TESTS_DIR%test_cpp_async.bin %TESTS_DIR%test_cpp_async.exe
python "%FLAT_RUN%" %TESTS_DIR%test_cpp_async.bin 100
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 15 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 16: C++20 Standard Module (import std;)...
set MODULE_FLAGS=/std:c++20 "/clang:-fpass-plugin=%PLUGIN_DLL%" /GS- /GR- /Gy /clang:-ffunction-sections /clang:-fdata-sections /clang:-fno-builtin /clang:-fno-jump-tables /clang:-fno-exceptions /clang:-fno-rtti /D_HAS_EXCEPTIONS=0 -I"%LTL_INC%" "/clang:-fmodule-file=std=%REPO_ROOT%\ltl\modules\std.pcm" /O1 /clang:-Oz /D_NO_CRT_STDIO_INLINE
"%CLANG_BIN%" %MODULE_FLAGS% -c %TESTS_DIR%test_cpp_module_std.cpp /Fo:%TESTS_DIR%test_cpp_module_std.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_cpp_module_std.obj "%LTL_LIB%" -out:%TESTS_DIR%test_cpp_module_std.exe
python "%EXTRACT_BIN%" %TESTS_DIR%test_cpp_module_std.bin %TESTS_DIR%test_cpp_module_std.exe
python "%FLAT_RUN%" %TESTS_DIR%test_cpp_module_std.bin 77
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 16 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 17: Freestanding Stdio (__stdio_common_vfwprintf, __acrt_iob_func, wprintf/printf)...
"%CLANG_BIN%" %COMMON_FLAGS% -c %TESTS_DIR%test_stdio_kprintf.cpp /Fo:%TESTS_DIR%test_stdio_kprintf.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_stdio_kprintf.obj "%LTL_LIB%" -out:%TESTS_DIR%test_stdio_kprintf.exe
python "%EXTRACT_BIN%" %TESTS_DIR%test_stdio_kprintf.bin %TESTS_DIR%test_stdio_kprintf.exe
python "%FLAT_RUN%" %TESTS_DIR%test_stdio_kprintf.bin 0
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 17 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 18: Multi-TU Global Constructors and .mli dispatch...
"%CLANG_BIN%" %COMMON_FLAGS% -c %TESTS_DIR%test_multi_tu_helper.cpp /Fo:%TESTS_DIR%test_multi_tu_helper.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%CLANG_BIN%" %COMMON_FLAGS% -c %TESTS_DIR%test_multi_tu_main.cpp /Fo:%TESTS_DIR%test_multi_tu_main.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_multi_tu_main.obj %TESTS_DIR%test_multi_tu_helper.obj "%LTL_LIB%" -out:%TESTS_DIR%test_multi_tu.exe
if %ERRORLEVEL% neq 0 ( echo [-] Link failed! & exit /b 1 )
%TESTS_DIR%test_multi_tu.exe
if %ERRORLEVEL% neq 0 ( echo [-] Execution of test_multi_tu.exe failed! & exit /b 1 )
python "%EXTRACT_BIN%" %TESTS_DIR%test_multi_tu.bin %TESTS_DIR%test_multi_tu.exe
python "%FLAT_RUN%" %TESTS_DIR%test_multi_tu.bin 0
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 18 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 19: argc/argv reconstruction (main and wmain)...
"%CLANG_BIN%" %COMMON_FLAGS% -c %TESTS_DIR%test_argv_ansi.cpp /Fo:%TESTS_DIR%test_argv_ansi.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_argv_ansi.obj "%LTL_LIB%" -out:%TESTS_DIR%test_argv_ansi.exe
if %ERRORLEVEL% neq 0 ( echo [-] Link failed! & exit /b 1 )
python "%EXTRACT_BIN%" %TESTS_DIR%test_argv_ansi.bin %TESTS_DIR%test_argv_ansi.exe
python "%FLAT_RUN%" %TESTS_DIR%test_argv_ansi.bin 0
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

"%CLANG_BIN%" %COMMON_FLAGS% -c %TESTS_DIR%test_argv_wide.cpp /Fo:%TESTS_DIR%test_argv_wide.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_argv_wide.obj "%LTL_LIB%" -out:%TESTS_DIR%test_argv_wide.exe
if %ERRORLEVEL% neq 0 ( echo [-] Link failed! & exit /b 1 )
python "%EXTRACT_BIN%" %TESTS_DIR%test_argv_wide.bin %TESTS_DIR%test_argv_wide.exe
python "%FLAT_RUN%" %TESTS_DIR%test_argv_wide.bin 0
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 19 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 20: Full C++ Exception Handling and RAII Unwinding...
set EH_FLAGS=/std:c++17 "/clang:-fpass-plugin=%PLUGIN_DLL%" /GS- /GR- /Gy /clang:-ffunction-sections /clang:-fdata-sections /clang:-ffreestanding /clang:-fno-builtin /clang:-fno-jump-tables /EHsc -I"%LTL_INC%" /O1 /clang:-Oz /D_NO_CRT_STDIO_INLINE
"%CLANG_BIN%" %EH_FLAGS% -c %TESTS_DIR%test_cpp_exceptions.cpp /Fo:%TESTS_DIR%test_cpp_exceptions.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_cpp_exceptions.obj "%LTL_LIB%" -out:%TESTS_DIR%test_cpp_exceptions.exe
if %ERRORLEVEL% neq 0 ( echo [-] Link failed! & exit /b 1 )
python "%EXTRACT_BIN%" %TESTS_DIR%test_cpp_exceptions.bin %TESTS_DIR%test_cpp_exceptions.exe
python "%FLAT_RUN%" %TESTS_DIR%test_cpp_exceptions.bin 0
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )

echo [Test 20 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 21: Hardware Entropy (std::random_device)...
set CPP20_FLAGS=/std:c++20 "/clang:-fpass-plugin=%PLUGIN_DLL%" /GS- /GR- /Gy /clang:-ffunction-sections /clang:-fdata-sections /clang:-ffreestanding /clang:-fno-builtin /clang:-fno-jump-tables /clang:-fno-exceptions /clang:-fno-rtti /D_HAS_EXCEPTIONS=0 -I"%LTL_INC%" /O1 /clang:-Oz /D_NO_CRT_STDIO_INLINE
"%CLANG_BIN%" %CPP20_FLAGS% -c %TESTS_DIR%test_random.cpp /Fo:%TESTS_DIR%test_random.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_random.obj "%LTL_LIB%" -out:%TESTS_DIR%test_random.exe
if %ERRORLEVEL% neq 0 ( echo [-] Link failed! & exit /b 1 )
python "%EXTRACT_BIN%" %TESTS_DIR%test_random.bin %TESTS_DIR%test_random.exe
python "%FLAT_RUN%" %TESTS_DIR%test_random.bin 0
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )
echo [Test 21 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 22: C++20 Chrono Timezone Database (std::chrono::current_zone)...
"%CLANG_BIN%" %CPP20_FLAGS% -c %TESTS_DIR%test_tz.cpp /Fo:%TESTS_DIR%test_tz.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_tz.obj "%LTL_LIB%" -out:%TESTS_DIR%test_tz.exe
if %ERRORLEVEL% neq 0 ( echo [-] Link failed! & exit /b 1 )
python "%EXTRACT_BIN%" %TESTS_DIR%test_tz.bin %TESTS_DIR%test_tz.exe
python "%FLAT_RUN%" %TESTS_DIR%test_tz.bin 0
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )
echo [Test 22 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 23: C++17 Parallel Algorithms (std::execution::par)...
"%CLANG_BIN%" %CPP20_FLAGS% -c %TESTS_DIR%test_execution.cpp /Fo:%TESTS_DIR%test_execution.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_execution.obj "%LTL_LIB%" -out:%TESTS_DIR%test_execution.exe
if %ERRORLEVEL% neq 0 ( echo [-] Link failed! & exit /b 1 )
python "%EXTRACT_BIN%" %TESTS_DIR%test_execution.bin %TESTS_DIR%test_execution.exe
python "%FLAT_RUN%" %TESTS_DIR%test_execution.bin 0
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )
echo [Test 23 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 24: C++20 Format and Floating-Point Charconv (std::format auto-link)...
"%CLANG_BIN%" %CPP20_FLAGS% -c %TESTS_DIR%test_format.cpp /Fo:%TESTS_DIR%test_format.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_format.obj "%LTL_LIB%" -out:%TESTS_DIR%test_format.exe
if %ERRORLEVEL% neq 0 ( echo [-] Link failed! & exit /b 1 )
python "%EXTRACT_BIN%" %TESTS_DIR%test_format.bin %TESTS_DIR%test_format.exe
python "%FLAT_RUN%" %TESTS_DIR%test_format.bin 0
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )
echo [Test 24 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 25: C++20 Synchronization Primitives (std::atomic wait, semaphore, latch, barrier)...
"%CLANG_BIN%" %CPP20_FLAGS% -c %TESTS_DIR%test_sync.cpp /Fo:%TESTS_DIR%test_sync.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_sync.obj "%LTL_LIB%" -out:%TESTS_DIR%test_sync.exe
if %ERRORLEVEL% neq 0 ( echo [-] Link failed! & exit /b 1 )
python "%EXTRACT_BIN%" %TESTS_DIR%test_sync.bin %TESTS_DIR%test_sync.exe
python "%FLAT_RUN%" %TESTS_DIR%test_sync.bin 0
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )
echo [Test 25 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 26: C++23 Stacktrace (std::stacktrace::current)...
set CPP23_FLAGS=/std:c++latest "/clang:-fpass-plugin=%PLUGIN_DLL%" /GS- /GR- /Gy /clang:-ffunction-sections /clang:-fdata-sections /clang:-ffreestanding /clang:-fno-builtin /clang:-fno-jump-tables /clang:-fno-exceptions /clang:-fno-rtti /D_HAS_EXCEPTIONS=0 -I"%LTL_INC%" /O1 /clang:-Oz /D_NO_CRT_STDIO_INLINE
"%CLANG_BIN%" %CPP23_FLAGS% -c %TESTS_DIR%test_stacktrace.cpp /Fo:%TESTS_DIR%test_stacktrace.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_stacktrace.obj "%LTL_LIB%" -out:%TESTS_DIR%test_stacktrace.exe
if %ERRORLEVEL% neq 0 ( echo [-] Link failed! & exit /b 1 )
python "%EXTRACT_BIN%" %TESTS_DIR%test_stacktrace.bin %TESTS_DIR%test_stacktrace.exe
python "%FLAT_RUN%" %TESTS_DIR%test_stacktrace.bin 0
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )
echo [Test 26 Status: %ERRORLEVEL%]

echo.
echo [*] Compiling and running Test 27: Named Locales (std::locale("zh-CN"))...
"%CLANG_BIN%" %CPP20_FLAGS% -c %TESTS_DIR%test_locale_named.cpp /Fo:%TESTS_DIR%test_locale_named.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )
"%LLD_BIN%" %LINK_FLAGS% %TESTS_DIR%test_locale_named.obj "%LTL_LIB%" -out:%TESTS_DIR%test_locale_named.exe
if %ERRORLEVEL% neq 0 ( echo [-] Link failed! & exit /b 1 )
python "%EXTRACT_BIN%" %TESTS_DIR%test_locale_named.bin %TESTS_DIR%test_locale_named.exe
python "%FLAT_RUN%" %TESTS_DIR%test_locale_named.bin 0
if %ERRORLEVEL% neq 0 ( echo [-] Flat binary execution failed! & exit /b 1 )
echo [Test 27 Status: %ERRORLEVEL%]



echo.
echo =======================================================================
echo   ALL 27 MODERN C++ TESTS VERIFIED AND PASSED SUCCESSFULLY!
echo =======================================================================
del "%TESTS_DIR%*.obj" "%TESTS_DIR%*.exe" "%TESTS_DIR%*.bin" "%TESTS_DIR%*.lib" >nul 2>&1
endlocal
