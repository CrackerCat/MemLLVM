@echo off
setlocal

cd /d "%~dp0"
set LTL_ROOT=%~dp0
if "%LTL_ROOT:~-1%"=="\" set LTL_ROOT=%LTL_ROOT:~0,-1%

if not "%LLVM_PATH%"=="" set "LLVM_ROOT=%LLVM_PATH%"
if "%LLVM_ROOT%"=="" if not "%LLVM_DIR%"=="" set "LLVM_ROOT=%LLVM_DIR%"
if "%LLVM_ROOT%"=="" if not "%LLVM_HOME%"=="" set "LLVM_ROOT=%LLVM_HOME%"

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
    if exist "%LTL_ROOT%\..\build\MemLLVM\Release\MemLLVM.dll" set "PLUGIN_DLL=%LTL_ROOT%\..\build\MemLLVM\Release\MemLLVM.dll"
    if exist "%LTL_ROOT%\..\build\Release\MemLLVM.dll" set "PLUGIN_DLL=%LTL_ROOT%\..\build\Release\MemLLVM.dll"
    if exist "%LTL_ROOT%\..\release_artifacts\MemLLVM.dll" set "PLUGIN_DLL=%LTL_ROOT%\..\release_artifacts\MemLLVM.dll"
)

echo [*] Compiling MemLLVM-LTL components...

"%CLANG_BIN%" /c /O2 /GS- /GR- /clang:-fno-jump-tables /clang:-fno-unwind-tables /clang:-fno-asynchronous-unwind-tables /clang:-fno-exceptions /D_HAS_EXCEPTIONS=0 -I"%LTL_ROOT%\include" "/clang:-fpass-plugin=%PLUGIN_DLL%" "%LTL_ROOT%\src\concurrency.cpp" /Fo"%LTL_ROOT%\src\concurrency.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" /c /O2 /GS- /GR- /clang:-fno-jump-tables /clang:-fno-unwind-tables /clang:-fno-asynchronous-unwind-tables /clang:-fno-exceptions /D_HAS_EXCEPTIONS=0 -I"%LTL_ROOT%\include" "/clang:-fpass-plugin=%PLUGIN_DLL%" "%LTL_ROOT%\src\emutls.cpp" /Fo"%LTL_ROOT%\src\emutls.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" /c /O2 /GS- /GR- /clang:-fno-jump-tables /clang:-fno-unwind-tables /clang:-fno-asynchronous-unwind-tables /clang:-fno-exceptions /D_HAS_EXCEPTIONS=0 -I"%LTL_ROOT%\include" "/clang:-fpass-plugin=%PLUGIN_DLL%" "%LTL_ROOT%\src\filesystem.cpp" /Fo"%LTL_ROOT%\src\filesystem.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" /c /O2 /GS- /GR- /clang:-fno-jump-tables /clang:-fno-unwind-tables /clang:-fno-asynchronous-unwind-tables /clang:-fno-exceptions /D_HAS_EXCEPTIONS=0 -I"%LTL_ROOT%\include" "/clang:-fpass-plugin=%PLUGIN_DLL%" "%LTL_ROOT%\src\crt_mem.cpp" /Fo"%LTL_ROOT%\src\crt_mem.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" /c /O2 /GS- /GR- /clang:-fno-jump-tables /clang:-fno-unwind-tables /clang:-fno-asynchronous-unwind-tables /clang:-fno-exceptions /D_HAS_EXCEPTIONS=0 -I"%LTL_ROOT%\include" "/clang:-fpass-plugin=%PLUGIN_DLL%" "%LTL_ROOT%\src\iostream.cpp" /Fo"%LTL_ROOT%\src\iostream.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" /c /O2 /GS- /GR- /clang:-fno-jump-tables /clang:-fno-unwind-tables /clang:-fno-asynchronous-unwind-tables /clang:-fno-exceptions /D_HAS_EXCEPTIONS=0 -I"%LTL_ROOT%\include" "/clang:-fpass-plugin=%PLUGIN_DLL%" "%LTL_ROOT%\src\locale.cpp" /Fo"%LTL_ROOT%\src\locale.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" /c /O2 /GS- /GR- /clang:-fno-jump-tables /clang:-fno-unwind-tables /clang:-fno-asynchronous-unwind-tables /clang:-fno-exceptions /D_HAS_EXCEPTIONS=0 -I"%LTL_ROOT%\include" "/clang:-fpass-plugin=%PLUGIN_DLL%" "%LTL_ROOT%\src\regex.cpp" /Fo"%LTL_ROOT%\src\regex.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" /c /O2 /GS- /GR- /clang:-fno-jump-tables /clang:-fno-unwind-tables /clang:-fno-asynchronous-unwind-tables /clang:-fno-exceptions /D_HAS_EXCEPTIONS=0 -I"%LTL_ROOT%\include" "/clang:-fpass-plugin=%PLUGIN_DLL%" "%LTL_ROOT%\src\async.cpp" /Fo"%LTL_ROOT%\src\async.obj"
if %ERRORLEVEL% neq 0 exit /b 1

echo [*] Archiving into ltl.lib...
"%LIB_BIN%" /out:"%LTL_ROOT%\ltl.lib" "%LTL_ROOT%\src\concurrency.obj" "%LTL_ROOT%\src\emutls.obj" "%LTL_ROOT%\src\filesystem.obj" "%LTL_ROOT%\src\crt_mem.obj" "%LTL_ROOT%\src\iostream.obj" "%LTL_ROOT%\src\locale.obj" "%LTL_ROOT%\src\regex.obj" "%LTL_ROOT%\src\async.obj"
if %ERRORLEVEL% neq 0 exit /b 1

del "%LTL_ROOT%\src\*.obj" >nul 2>&1

echo [+] MemLLVM-LTL built successfully: %LTL_ROOT%\ltl.lib
endlocal
