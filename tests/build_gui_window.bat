@echo off
setlocal
echo =======================================================================
echo   MemLLVM Win32 Interactive DirectX 11 GUI: C++ Source -^> Pure .bin
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

if "%OBJCOPY_BIN%"=="" (
    if not "%LLVM_ROOT%"=="" (
        if exist "%LLVM_ROOT%\bin\llvm-objcopy.exe" set "OBJCOPY_BIN=%LLVM_ROOT%\bin\llvm-objcopy.exe"
        if exist "%LLVM_ROOT%\llvm-objcopy.exe" set "OBJCOPY_BIN=%LLVM_ROOT%\llvm-objcopy.exe"
    )
)
if "%OBJCOPY_BIN%"=="" where llvm-objcopy.exe >nul 2>&1 && set "OBJCOPY_BIN=llvm-objcopy.exe"

if "%CLANG_BIN%"=="" (
    echo [-] ERROR: LLVM environment variable is not configured!
    exit /b 1
)

if "%PLUGIN_DLL%"=="" (
    if not defined PLUGIN_DLL if exist "..\build\MemLLVM\Release\MemLLVM.dll" set "PLUGIN_DLL=..\build\MemLLVM\Release\MemLLVM.dll"
    if not defined PLUGIN_DLL if exist "..\build\Release\MemLLVM.dll" set "PLUGIN_DLL=..\build\Release\MemLLVM.dll"
    if not defined PLUGIN_DLL if exist "..\release_artifacts\MemLLVM.dll" set "PLUGIN_DLL=..\release_artifacts\MemLLVM.dll"
)

echo [*] Compiling gui_window.cpp with MemLLVM Pass Plugin + Freestanding flags...
"%CLANG_BIN%" "/clang:-fpass-plugin=%PLUGIN_DLL%" ^
    /GS- ^
    /clang:-ffreestanding ^
    /clang:-fno-builtin ^
    /clang:-fno-jump-tables ^
    /clang:-fno-exceptions ^
    /clang:-fno-rtti ^
    /clang:-x /clang:c++ ^
    /D_HAS_EXCEPTIONS=0 ^
    -O2 ^
    -c gui_window.cpp /Fo:gui_window.obj

if %ERRORLEVEL% neq 0 (
    echo [-] Clang compilation failed!
    goto :end
)

echo [*] Linking into zero-dependency flat section (Merging .rdata into .text)...
"%LLD_BIN%" /entry:main /subsystem:windows /nodefaultlib /safeseh:no /merge:.rdata=.text gui_window.obj -out:gui_temp.exe

if %ERRORLEVEL% neq 0 (
    echo [-] Linking failed!
    goto :end
)

echo [*] Extracting raw .text machine code to gui_window.bin...
"%OBJCOPY_BIN%" --dump-section .text=gui_window.bin gui_temp.exe

if %ERRORLEVEL% neq 0 (
    echo [-] Section extraction failed!
    goto :end
)

:: Clean up all intermediate files, keeping ONLY the raw .bin machine code
del gui_window.obj
del gui_temp.exe

echo.
echo [+] SUCCESS: Extracted pure flat machine code to gui_window.bin!
echo [+] Final Artifact: gui_window.bin (DirectX 11, NO PE headers, offset 0x00 entry)

:end
endlocal
