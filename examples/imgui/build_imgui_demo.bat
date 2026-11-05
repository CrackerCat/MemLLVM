@echo off
setlocal

cd /d "%~dp0"

echo =======================================================================
echo   MemLLVM Flat Machine Code Builder: Dear ImGui Demo Window
echo =======================================================================

if not "%LLVM_PATH%"=="" set "LLVM_ROOT=%LLVM_PATH%"
if "%LLVM_ROOT%"=="" if not "%LLVM_DIR%"=="" set "LLVM_ROOT=%LLVM_DIR%"
if "%LLVM_ROOT%"=="" if not "%LLVM_HOME%"=="" set "LLVM_ROOT=%LLVM_HOME%"

if "%CLANG_BIN%"=="" (
    if not "%LLVM_ROOT%"=="" (
        if exist "%LLVM_ROOT%\bin\clang-cl.exe" set "CLANG_BIN=%LLVM_ROOT%\bin\clang-cl.exe"
        if exist "%LLVM_ROOT%\clang-cl.exe" set "CLANG_BIN=%LLVM_ROOT%\clang-cl.exe"
    )
    if exist "C:\LLVM-22-plugin\bin\clang-cl.exe" set "CLANG_BIN=C:\LLVM-22-plugin\bin\clang-cl.exe"
    if exist "C:\LLVM-22-dev\bin\clang-cl.exe" set "CLANG_BIN=C:\LLVM-22-dev\bin\clang-cl.exe"
)
if "%CLANG_BIN%"=="" where clang-cl.exe >nul 2>&1 && set "CLANG_BIN=clang-cl.exe"

if "%LLD_BIN%"=="" (
    if not "%LLVM_ROOT%"=="" (
        if exist "%LLVM_ROOT%\bin\lld-link.exe" set "LLD_BIN=%LLVM_ROOT%\bin\lld-link.exe"
        if exist "%LLVM_ROOT%\lld-link.exe" set "LLD_BIN=%LLVM_ROOT%\lld-link.exe"
    )
    if exist "C:\LLVM-22-plugin\bin\lld-link.exe" set "LLD_BIN=C:\LLVM-22-plugin\bin\lld-link.exe"
    if exist "C:\LLVM-22-dev\bin\lld-link.exe" set "LLD_BIN=C:\LLVM-22-dev\bin\lld-link.exe"
)
if "%LLD_BIN%"=="" where lld-link.exe >nul 2>&1 && set "LLD_BIN=lld-link.exe"

if "%CLANG_BIN%"=="" (
    echo =======================================================================
    echo [-] ERROR: LLVM environment variable is not configured!
    echo [-] You must set LLVM_PATH or LLVM_DIR pointing to your LLVM installation.
    echo [-] Example: set LLVM_PATH=C:\path\to\llvm
    echo =======================================================================
    exit /b 1
)

if "%LLD_BIN%"=="" (
    echo [-] ERROR: lld-link not found! Please check your LLVM installation.
    exit /b 1
)

if "%PLUGIN_DLL%"=="" (
    if not defined PLUGIN_DLL if exist "..\..\build\MemLLVM\Release\MemLLVM.dll" set "PLUGIN_DLL=..\..\build\MemLLVM\Release\MemLLVM.dll"
    if not defined PLUGIN_DLL if exist "..\..\build\Release\MemLLVM.dll" set "PLUGIN_DLL=..\..\build\Release\MemLLVM.dll"
    if not defined PLUGIN_DLL if exist "..\..\release_artifacts\MemLLVM.dll" set "PLUGIN_DLL=..\..\release_artifacts\MemLLVM.dll"
)

set LTL_LIB=..\..\ltl\ltl.lib
set LTL_INC=..\..\ltl\include

echo [*] Compiling Dear ImGui Unity codebase with MemLLVM Plugin...
"%CLANG_BIN%" /std:c++17 "/clang:-fpass-plugin=%PLUGIN_DLL%" /GS- /GR- /Gy /clang:-ffunction-sections /clang:-fdata-sections /clang:-ffreestanding /clang:-fno-builtin /clang:-fno-jump-tables /clang:-fno-exceptions /clang:-fno-rtti /D_HAS_EXCEPTIONS=0 -I"%LTL_INC%" -I. -Ibackends -Iexamples\example_win32_directx11 /O1 /clang:-Oz -c imgui_unity.cpp /Fo:imgui_unity.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )

echo [*] Linking Dear ImGui with lld-link and ltl.lib...
"%LLD_BIN%" /entry:main /subsystem:console /nodefaultlib /safeseh:no /opt:ref /opt:icf /align:512 /merge:.rdata=.text imgui_unity.obj "%LTL_LIB%" -out:imgui_unity.exe
if %ERRORLEVEL% neq 0 ( echo [-] Linking failed! & exit /b 1 )

echo [*] Extracting flat contiguous virtual machine code image...
python extract_flat_bin.py imgui_unity.exe imgui_demo.bin
if %ERRORLEVEL% neq 0 ( echo [-] Extraction failed! & exit /b 1 )

del imgui_unity.obj imgui_unity.exe >nul 2>&1

echo [+] Dear ImGui Demo Window flat binary built successfully: %~dp0imgui_demo.bin
exit /b 0
