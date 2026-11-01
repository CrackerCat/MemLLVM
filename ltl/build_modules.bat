@echo off
setlocal enabledelayedexpansion

echo =======================================================================
echo   MemLLVM C++20 Standard Library Module Builder (import std;)
echo =======================================================================

if not "%LLVM_PATH%"=="" set "LLVM_ROOT=%LLVM_PATH%"
if "%LLVM_ROOT%"=="" if not "%LLVM_DIR%"=="" set "LLVM_ROOT=%LLVM_DIR%"
if "%LLVM_ROOT%"=="" if not "%LLVM_HOME%"=="" set "LLVM_ROOT=%LLVM_HOME%"

if "%CLANGPP%"=="" (
    if not "%LLVM_ROOT%"=="" (
        if exist "%LLVM_ROOT%\bin\clang++.exe" set "CLANGPP=%LLVM_ROOT%\bin\clang++.exe"
        if exist "%LLVM_ROOT%\clang++.exe" set "CLANGPP=%LLVM_ROOT%\clang++.exe"
    )
)
if "%CLANGPP%"=="" where clang++.exe >nul 2>&1 && set "CLANGPP=clang++.exe"

if "%CLANGPP%"=="" (
    echo [-] ERROR: LLVM environment variable is not configured!
    exit /b 1
)
set OUT_DIR=%~dp0modules

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

:: Search for MSVC std.ixx
set STD_IXX=
for /d %%D in ("C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\*") do (
    if exist "%%D\modules\std.ixx" (
        set STD_IXX=%%D\modules\std.ixx
    )
)
if "%STD_IXX%"=="" (
    for /d %%D in ("C:\Program Files\Microsoft Visual Studio\2022\*\VC\Tools\MSVC\*") do (
        if exist "%%D\modules\std.ixx" (
            set STD_IXX=%%D\modules\std.ixx
        )
    )
)

if "%STD_IXX%"=="" (
    echo [-] Error: std.ixx not found in Visual Studio MSVC installation!
    exit /b 1
)

echo [*] Found std.ixx at: %STD_IXX%
echo [*] Precompiling C++20 Standard Module interface to: %OUT_DIR%\std.pcm ...

"%CLANGPP%" -std=c++20 -fms-compatibility -fms-extensions -fno-exceptions -fno-rtti -D_HAS_EXCEPTIONS=0 ^
  -fno-builtin -mno-stack-arg-probe -fno-jump-tables -O2 ^
  -Wno-reserved-module-identifier -Wno-include-angled-in-module-purview ^
  -x c++-module --precompile "%STD_IXX%" -o "%OUT_DIR%\std.pcm"

if %ERRORLEVEL% neq 0 (
    echo [-] Failed to precompile std.pcm!
    exit /b 1
)

echo [+] Successfully built MemLLVM C++20 std module: %OUT_DIR%\std.pcm
exit /b 0
