@echo off
setlocal
echo =======================================================================
echo   MemLLVM Flat Binary Generator: C Source -^> Pure Machine Code (.bin)
echo =======================================================================

set CLANG_BIN=C:\LLVM-22-dev\bin\clang-cl.exe
set LLD_BIN=C:\LLVM-22-dev\bin\lld-link.exe
set OBJCOPY_BIN=C:\LLVM-22-dev\bin\llvm-objcopy.exe
set PLUGIN_DLL=D:\MemLLVM\build\MemLLVM\Release\MemLLVM.dll

echo [*] Compiling showcase.c with MemLLVM Pass Plugin + Freestanding flags...
"%CLANG_BIN%" "/clang:-fpass-plugin=%PLUGIN_DLL%" ^
    /GS- ^
    /clang:-ffreestanding ^
    /clang:-fno-builtin ^
    /clang:-mno-stack-arg-probe ^
    /clang:-fno-jump-tables ^
    /clang:-fno-exceptions ^
    /clang:-fno-rtti ^
    -O2 ^
    -c showcase.c /Fo:showcase.obj

if %ERRORLEVEL% neq 0 (
    echo [-] Clang compilation failed!
    goto :end
)

echo [*] Linking into zero-dependency flat section (Merging .rdata, .data, .bss into .text)...
"%LLD_BIN%" /entry:main /subsystem:console /nodefaultlib /safeseh:no /merge:.rdata=.text /merge:.data=.text /merge:.bss=.text showcase.obj -out:showcase_temp.exe

if %ERRORLEVEL% neq 0 (
    echo [-] Linking failed!
    goto :end
)

echo [*] Extracting raw .text machine code to showcase.bin...
"%OBJCOPY_BIN%" --dump-section .text=showcase.bin showcase_temp.exe

if %ERRORLEVEL% neq 0 (
    echo [-] Section extraction failed!
    goto :end
)

:: Clean up all intermediate files, keeping ONLY the raw .bin machine code
del showcase.obj
del showcase_temp.exe
if exist showcase.exe del showcase.exe

echo.
echo [+] SUCCESS: Extracted pure flat machine code to showcase.bin!
echo [+] Final Artifact: showcase.bin (Raw machine code, NO PE headers, offset 0x00 entry)

:end
endlocal
