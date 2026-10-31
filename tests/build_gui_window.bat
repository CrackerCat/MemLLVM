@echo off
setlocal
echo =======================================================================
echo   MemLLVM Win32 Interactive DirectX 11 GUI: C++ Source -^> Pure .bin
echo =======================================================================

set CLANG_BIN=C:\LLVM-22-dev\bin\clang-cl.exe
set LLD_BIN=C:\LLVM-22-dev\bin\lld-link.exe
set OBJCOPY_BIN=C:\LLVM-22-dev\bin\llvm-objcopy.exe
set PLUGIN_DLL=D:\MemLLVM\build\MemLLVM\Release\MemLLVM.dll

echo [*] Compiling gui_window.cpp with MemLLVM Pass Plugin + Freestanding flags...
"%CLANG_BIN%" "/clang:-fpass-plugin=%PLUGIN_DLL%" ^
    /GS- ^
    /clang:-ffreestanding ^
    /clang:-fno-builtin ^
    /clang:-mno-stack-arg-probe ^
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
