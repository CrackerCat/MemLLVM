@echo off
setlocal

cd /d "%~dp0"

echo =======================================================================
echo   MemLLVM Flat Machine Code Builder: Dear ImGui Demo Window
echo =======================================================================

if "%CLANG_BIN%"=="" set CLANG_BIN=C:\LLVM-22-dev\bin\clang-cl.exe
if not exist "%CLANG_BIN%" set CLANG_BIN=clang-cl.exe

if "%LLD_BIN%"=="" set LLD_BIN=C:\LLVM-22-dev\bin\lld-link.exe
if not exist "%LLD_BIN%" set LLD_BIN=lld-link.exe

if "%PLUGIN_DLL%"=="" set PLUGIN_DLL=..\..\build\MemLLVM\Release\MemLLVM.dll
if not exist "%PLUGIN_DLL%" set PLUGIN_DLL=..\..\build\Release\MemLLVM.dll

set LTL_LIB=..\..\ltl\ltl.lib
set LTL_INC=..\..\ltl\include

echo [*] Compiling Dear ImGui Unity codebase with MemLLVM Plugin...
"%CLANG_BIN%" /std:c++17 "/clang:-fpass-plugin=%PLUGIN_DLL%" /GS- /GR- /clang:-fno-builtin /clang:-mno-stack-arg-probe /clang:-fno-jump-tables /clang:-fno-exceptions /clang:-fno-rtti /D_HAS_EXCEPTIONS=0 -I"%LTL_INC%" -I. -Ibackends -Iexamples\example_win32_directx11 -O2 -c imgui_unity.cpp /Fo:imgui_unity.obj
if %ERRORLEVEL% neq 0 ( echo [-] Compilation failed! & exit /b 1 )

echo [*] Linking Dear ImGui with lld-link and ltl.lib...
"%LLD_BIN%" /entry:main /subsystem:console /nodefaultlib /safeseh:no imgui_unity.obj "%LTL_LIB%" -out:imgui_unity.exe
if %ERRORLEVEL% neq 0 ( echo [-] Linking failed! & exit /b 1 )

echo [*] Extracting flat contiguous virtual machine code image...
python extract_flat_bin.py imgui_unity.exe imgui_demo.bin
if %ERRORLEVEL% neq 0 ( echo [-] Extraction failed! & exit /b 1 )

del imgui_unity.obj imgui_unity.exe >nul 2>&1

echo [+] Dear ImGui Demo Window flat binary built successfully: %~dp0imgui_demo.bin
exit /b 0
