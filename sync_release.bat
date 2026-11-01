@echo off
setlocal

cd /d "%~dp0"
echo =======================================================================
echo          MemLLVM Automated Release Artifacts Synchronizer
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

if "%CLANG_BIN%"=="" (
    echo =======================================================================
    echo [-] ERROR: LLVM environment variable is not configured!
    echo [-] You must set LLVM_PATH or LLVM_DIR pointing to your LLVM installation.
    echo [-] Example: set LLVM_PATH=C:\path\to\llvm
    echo =======================================================================
    exit /b 1
)

echo [*] Step 1: Building LTL library...
call ltl\build_ltl.bat
if %ERRORLEVEL% neq 0 ( echo [-] Failed to build LTL! & exit /b 1 )

echo [*] Step 2: Building run_bin.exe...
"%CLANG_BIN%" /O2 /Fe:tests\run_bin.exe tests\run_bin.c
if %ERRORLEVEL% neq 0 ( echo [-] Failed to build run_bin.exe! & exit /b 1 )

echo [*] Step 3: Compiling all examples...
python examples\build_all.py
if %ERRORLEVEL% neq 0 ( echo [-] Failed to compile examples! & exit /b 1 )

echo [*] Step 4: Syncing to release_artifacts/...
python -c "import os, shutil; dist=r'release_artifacts'; os.makedirs(dist, exist_ok=True); files=[(r'build\MemLLVM\Release\MemLLVM.dll', 'MemLLVM.dll'), (r'tests\run_bin.exe', 'run_bin.exe'), (r'examples\box2d\box2d.bin', 'box2d.bin'), (r'examples\chip8\chip8.bin', 'chip8.bin'), (r'examples\doom\doom.bin', 'doom.bin'), (r'examples\doom\doom1.wad', 'doom1.wad'), (r'examples\imgui\imgui_demo.bin', 'imgui_demo.bin'), (r'examples\scintilla_editor\scintilla_editor.bin', 'scintilla_editor.bin')]; [shutil.copy2(s, os.path.join(dist, n)) for s, n in files]; print('\n'.join([f'  [+] Synced {n} ({os.path.getsize(os.path.join(dist, n))/1024:.2f} KB)' for _, n in files]))"

echo =======================================================================
echo [+] All release artifacts successfully built and synchronized!
echo [*] To publish, simply commit and push with 'release' in the message:
echo     git add release_artifacts/
echo     git commit -m "release: update binaries"
echo     git push origin master
echo =======================================================================
