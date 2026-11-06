@echo off
setlocal

cd /d "%~dp0"
set LTL_ROOT=%~dp0
if "%LTL_ROOT:~-1%"=="\" set LTL_ROOT=%LTL_ROOT:~0,-1%

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

if "%LIB_BIN%"=="" (
    if not "%LLVM_ROOT%"=="" (
        if exist "%LLVM_ROOT%\bin\llvm-lib.exe" set "LIB_BIN=%LLVM_ROOT%\bin\llvm-lib.exe"
        if exist "%LLVM_ROOT%\llvm-lib.exe" set "LIB_BIN=%LLVM_ROOT%\llvm-lib.exe"
    )
)
if "%LIB_BIN%"=="" where llvm-lib.exe >nul 2>&1 && set "LIB_BIN=llvm-lib.exe"

if "%LLVM_ROOT%"=="" if exist "C:\LLVM-22-dev\bin\clang-cl.exe" set "LLVM_ROOT=C:\LLVM-22-dev"
if "%CLANG_BIN%"=="" (
    echo =======================================================================
    echo [-] ERROR: LLVM environment variable is not configured!
    echo [-] You must set LLVM_PATH or LLVM_DIR pointing to your LLVM installation.
    echo [-] Example: set LLVM_PATH=C:\path\to\llvm
    echo =======================================================================
    exit /b 1
)

if "%LIB_BIN%"=="" (
    echo [-] ERROR: llvm-lib not found! Please check your LLVM installation.
    exit /b 1
)

if "%PLUGIN_DLL%"=="" (
    rem First match wins: without the "if not defined" guards every subsequent
    rem hit overwrites the previous one, so a stale release_artifacts DLL would
    rem silently shadow the freshly built plugin.
    if not defined PLUGIN_DLL if exist "%LTL_ROOT%\..\build\MemLLVM\Release\MemLLVM.dll" set "PLUGIN_DLL=%LTL_ROOT%\..\build\MemLLVM\Release\MemLLVM.dll"
    if not defined PLUGIN_DLL if exist "%LTL_ROOT%\..\build\Release\MemLLVM.dll" set "PLUGIN_DLL=%LTL_ROOT%\..\build\Release\MemLLVM.dll"
    if not defined PLUGIN_DLL if exist "%LTL_ROOT%\..\release_artifacts\MemLLVM.dll" set "PLUGIN_DLL=%LTL_ROOT%\..\release_artifacts\MemLLVM.dll"
)

echo [*] Compiling MemLLVM-LTL components...
set MEMLLVM_BUILDING_LTL=1

set COMP_FLAGS=/c /O1 /clang:-Oz /Gy /clang:-ffunction-sections /clang:-fdata-sections /GS- /GR- /clang:-fno-jump-tables /clang:-fno-unwind-tables /clang:-fno-asynchronous-unwind-tables /clang:-fno-exceptions /D_HAS_EXCEPTIONS=0 -I"%LTL_ROOT%\include" "/clang:-fpass-plugin=%PLUGIN_DLL%"
set EH_COMP_FLAGS=/c /O1 /clang:-Oz /Gy /clang:-ffunction-sections /clang:-fdata-sections /GS- /GR- /clang:-fno-jump-tables /EHsc -I"%LTL_ROOT%\include" "/clang:-fpass-plugin=%PLUGIN_DLL%"

"%CLANG_BIN%" %COMP_FLAGS% "%LTL_ROOT%\src\concurrency.cpp" /Fo"%LTL_ROOT%\src\concurrency.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" %COMP_FLAGS% "%LTL_ROOT%\src\emutls.cpp" /Fo"%LTL_ROOT%\src\emutls.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" %COMP_FLAGS% "%LTL_ROOT%\src\filesystem.cpp" /Fo"%LTL_ROOT%\src\filesystem.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" %COMP_FLAGS% "%LTL_ROOT%\src\crt_mem.cpp" /Fo"%LTL_ROOT%\src\crt_mem.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" %EH_COMP_FLAGS% "%LTL_ROOT%\src\crt_exceptions.cpp" /Fo"%LTL_ROOT%\src\crt_exceptions.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" %COMP_FLAGS% "%LTL_ROOT%\src\crt_atexit.cpp" /Fo"%LTL_ROOT%\src\crt_atexit.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" %COMP_FLAGS% "%LTL_ROOT%\src\stdio_printf.cpp" /Fo"%LTL_ROOT%\src\stdio_printf.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" %COMP_FLAGS% "%LTL_ROOT%\src\stdio_puts.cpp" /Fo"%LTL_ROOT%\src\stdio_puts.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" %COMP_FLAGS% "%LTL_ROOT%\src\iostream.cpp" /Fo"%LTL_ROOT%\src\iostream.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" %COMP_FLAGS% "%LTL_ROOT%\src\locale.cpp" /Fo"%LTL_ROOT%\src\locale.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" %COMP_FLAGS% "%LTL_ROOT%\src\regex.cpp" /Fo"%LTL_ROOT%\src\regex.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" %COMP_FLAGS% "%LTL_ROOT%\src\async.cpp" /Fo"%LTL_ROOT%\src\async.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" %COMP_FLAGS% "%LTL_ROOT%\src\crt_resources.cpp" /Fo"%LTL_ROOT%\src\crt_resources.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" %COMP_FLAGS% "%LTL_ROOT%\src\random.cpp" /Fo"%LTL_ROOT%\src\random.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" %COMP_FLAGS% "%LTL_ROOT%\src\chrono_tz.cpp" /Fo"%LTL_ROOT%\src\chrono_tz.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" %COMP_FLAGS% "%LTL_ROOT%\src\execution.cpp" /Fo"%LTL_ROOT%\src\execution.obj"
if %ERRORLEVEL% neq 0 exit /b 1

set CXX20_FLAGS=/c /std:c++20 /O1 /clang:-Oz /Gy /clang:-ffunction-sections /clang:-fdata-sections /GS- /GR- /clang:-fno-jump-tables /clang:-fno-unwind-tables /clang:-fno-asynchronous-unwind-tables /clang:-fno-exceptions /D_HAS_EXCEPTIONS=0 -I"%LTL_ROOT%\include" "/clang:-fpass-plugin=%PLUGIN_DLL%"

"%CLANG_BIN%" %CXX20_FLAGS% "%LTL_ROOT%\src\atomic_wait.cpp" /Fo"%LTL_ROOT%\src\atomic_wait.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" %CXX20_FLAGS% "%LTL_ROOT%\src\stacktrace.cpp" /Fo"%LTL_ROOT%\src\stacktrace.obj"
if %ERRORLEVEL% neq 0 exit /b 1

set TABLE_FLAGS=/c /std:c++20 /O2 /Gy /clang:-ffunction-sections /clang:-fdata-sections /GS- /GR- /clang:-fno-jump-tables /clang:-fno-unwind-tables /clang:-fno-asynchronous-unwind-tables /clang:-fno-exceptions /D_HAS_EXCEPTIONS=0 -I"%LTL_ROOT%\include"
"%CLANG_BIN%" %TABLE_FLAGS% "%LTL_ROOT%\src\format_tables.cpp" /Fo"%LTL_ROOT%\src\format_tables.obj"
if %ERRORLEVEL% neq 0 exit /b 1

echo [*] Archiving into ltl_format.lib (standalone modular ryu lookup tables)...
"%LIB_BIN%" /out:"%LTL_ROOT%\ltl_format.lib" "%LTL_ROOT%\src\format_tables.obj"
if %ERRORLEVEL% neq 0 exit /b 1

echo [*] Archiving into ltl.lib...
"%LIB_BIN%" /out:"%LTL_ROOT%\ltl.lib" "%LTL_ROOT%\src\concurrency.obj" "%LTL_ROOT%\src\emutls.obj" "%LTL_ROOT%\src\filesystem.obj" "%LTL_ROOT%\src\crt_mem.obj" "%LTL_ROOT%\src\crt_exceptions.obj" "%LTL_ROOT%\src\crt_atexit.obj" "%LTL_ROOT%\src\stdio_printf.obj" "%LTL_ROOT%\src\stdio_puts.obj" "%LTL_ROOT%\src\iostream.obj" "%LTL_ROOT%\src\locale.obj" "%LTL_ROOT%\src\regex.obj" "%LTL_ROOT%\src\async.obj" "%LTL_ROOT%\src\crt_resources.obj" "%LTL_ROOT%\src\random.obj" "%LTL_ROOT%\src\chrono_tz.obj" "%LTL_ROOT%\src\execution.obj" "%LTL_ROOT%\src\atomic_wait.obj" "%LTL_ROOT%\src\stacktrace.obj" "%LTL_ROOT%\src\format_tables.obj"
if %ERRORLEVEL% neq 0 exit /b 1

if exist "%LTL_ROOT%\..\..\MemLLVM-SDK-windows-x64\lib" (
    copy /y "%LTL_ROOT%\ltl.lib" "%LTL_ROOT%\..\..\MemLLVM-SDK-windows-x64\lib\ltl.lib" >nul
    copy /y "%LTL_ROOT%\ltl_format.lib" "%LTL_ROOT%\..\..\MemLLVM-SDK-windows-x64\lib\ltl_format.lib" >nul
    copy /y "%LTL_ROOT%\ltl.lib" "%LTL_ROOT%\..\..\MemLLVM-SDK-windows-x64\ltl\ltl.lib" >nul
    copy /y "%LTL_ROOT%\ltl_format.lib" "%LTL_ROOT%\..\..\MemLLVM-SDK-windows-x64\ltl\ltl_format.lib" >nul
)
if exist "D:\MemLLVM-SDK-windows-x64\lib" (
    copy /y "%LTL_ROOT%\ltl.lib" "D:\MemLLVM-SDK-windows-x64\lib\ltl.lib" >nul
    copy /y "%LTL_ROOT%\ltl_format.lib" "D:\MemLLVM-SDK-windows-x64\lib\ltl_format.lib" >nul
    copy /y "%LTL_ROOT%\ltl.lib" "D:\MemLLVM-SDK-windows-x64\ltl\ltl.lib" >nul
    copy /y "%LTL_ROOT%\ltl_format.lib" "D:\MemLLVM-SDK-windows-x64\ltl\ltl_format.lib" >nul
)

del "%LTL_ROOT%\src\*.obj" >nul 2>&1

echo [+] MemLLVM-LTL built successfully: %LTL_ROOT%\ltl.lib
endlocal
