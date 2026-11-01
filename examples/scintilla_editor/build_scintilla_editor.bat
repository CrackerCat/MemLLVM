@echo off
setlocal

cd /d "%~dp0"

echo =======================================================================
echo   MemLLVM Flat Machine Code Builder: Native Win32 Scintilla Editor
echo =======================================================================

if "%CLANG_BIN%"=="" set CLANG_BIN=C:\LLVM-22-dev\bin\clang-cl.exe
if not exist "%CLANG_BIN%" set CLANG_BIN=clang-cl.exe

if "%LLD_BIN%"=="" set LLD_BIN=C:\LLVM-22-dev\bin\lld-link.exe
if not exist "%LLD_BIN%" set LLD_BIN=lld-link.exe

if "%PLUGIN_DLL%"=="" set PLUGIN_DLL=..\..\build\MemLLVM\Release\MemLLVM.dll
if not exist "%PLUGIN_DLL%" set PLUGIN_DLL=..\..\build\Release\MemLLVM.dll

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
