@echo off
setlocal
cd /d "%~dp0"

set "CC="
if not "%LLVM_PATH%"=="" if exist "%LLVM_PATH%\bin\clang-cl.exe" set "CC=%LLVM_PATH%\bin\clang-cl.exe"
if "%CC%"=="" if not "%LLVM_DIR%"=="" if exist "%LLVM_DIR%\bin\clang-cl.exe" set "CC=%LLVM_DIR%\bin\clang-cl.exe"
if "%CC%"=="" if exist "C:\LLVM-22-plugin\bin\clang-cl.exe" set "CC=C:\LLVM-22-plugin\bin\clang-cl.exe"
if "%CC%"=="" if exist "C:\LLVM-22-dev\bin\clang-cl.exe" set "CC=C:\LLVM-22-dev\bin\clang-cl.exe"
if "%CC%"=="" where clang-cl.exe >nul 2>&1 && set "CC=clang-cl.exe"

if "%CC%"=="" (
    echo [-] ERROR: clang-cl.exe not found!
    exit /b 1
)

echo [*] Building scloader_cli.exe with %CC%...
"%CC%" /nologo /W3 /O2 /MT scloader_cli.c /Fe:scloader_cli.exe /link /SUBSYSTEM:CONSOLE kernel32.lib user32.lib advapi32.lib
if errorlevel 1 ( echo [-] BUILD_FAIL & exit /b 1 )
echo [+] BUILD_OK: %~dp0scloader_cli.exe
endlocal
