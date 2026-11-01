@echo off
setlocal

cd /d "%~dp0"

echo =======================================================================
echo   MemLLVM Flat Machine Code Builder: Native Win32 Scintilla Editor
echo =======================================================================

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

if "%LLD_BIN%"=="" (
    if not "%LLVM_ROOT%"=="" (
        if exist "%LLVM_ROOT%\bin\lld-link.exe" set "LLD_BIN=%LLVM_ROOT%\bin\lld-link.exe"
        if exist "%LLVM_ROOT%\lld-link.exe" set "LLD_BIN=%LLVM_ROOT%\lld-link.exe"
    )
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
    if exist "..\..\build\MemLLVM\Release\MemLLVM.dll" set "PLUGIN_DLL=..\..\build\MemLLVM\Release\MemLLVM.dll"
    if exist "..\..\build\Release\MemLLVM.dll" set "PLUGIN_DLL=..\..\build\Release\MemLLVM.dll"
    if exist "..\..\release_artifacts\MemLLVM.dll" set "PLUGIN_DLL=..\..\release_artifacts\MemLLVM.dll"
)

set LTL_LIB=..\..\ltl\ltl.lib
set LTL_INC=..\..\ltl\include

echo [*] Compiling Scintilla Editor Unity codebase with MemLLVM Plugin...
"%CLANG_BIN%" /std:c++20 /EHsc -DNDEBUG -D_WINDOWS -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DUNICODE -D_UNICODE "/clang:-fpass-plugin=%PLUGIN_DLL%" /GS- /GR- /clang:-fno-builtin /clang:-mno-stack-arg-probe /clang:-fno-jump-tables -I"%LTL_INC%" -I. -Iscintilla\include -Iscintilla\src -Iscintilla\lexlib -Iscintilla\win32 -O2 -c scintilla_unity.cpp /Fo:scintilla_unity.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )

echo [*] Linking Scintilla Editor with lld-link and ltl.lib...
"%LLD_BIN%" /entry:main /subsystem:windows /nodefaultlib /safeseh:no scintilla_unity.obj "%LTL_LIB%" -out:scintilla_unity.exe
if %ERRORLEVEL% neq 0 ( echo [-] Linking failed! & exit /b 1 )

echo [*] Extracting flat contiguous virtual machine code image...
python extract_flat_bin.py scintilla_unity.exe scintilla_editor.bin
if %ERRORLEVEL% neq 0 ( echo [-] Extraction failed! & exit /b 1 )

del scintilla_unity.obj scintilla_unity.exe >nul 2>&1

echo [+] Native Win32 Scintilla Editor flat binary built successfully: scintilla_editor.bin
exit /b 0
