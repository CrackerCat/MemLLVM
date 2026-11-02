@echo off
setlocal
set "CLANG=C:\LLVM-22-dev\bin\clang-cl.exe"
set "LLD=C:\LLVM-22-dev\bin\lld-link.exe"
set "PLUGIN=D:/MemLLVM/bin/MemLLVM.dll"
set "LTLINC=D:/MemLLVM/ltl/include"
cd /d D:\MemLLVM\Tools

"%CLANG%" /c /nologo /W3 /std:c++17 /clang:-fpass-plugin=%PLUGIN% /GS- ^
  /clang:-ffreestanding /clang:-fno-builtin /clang:-fno-jump-tables ^
  /clang:-fno-exceptions /clang:-fno-rtti /D_HAS_EXCEPTIONS=0 ^
  -I"%LTLINC%" -O2 probe_crt2.c /Fo:probe_crt2.obj
if errorlevel 1 ( echo COMPILE_FAIL & exit /b 1 )

"%LLD%" /entry:main /subsystem:console /nodefaultlib /safeseh:no /opt:noicf ^
  probe_crt2.obj D:\MemLLVM\ltl\ltl.lib -out:probe_crt2.exe
if errorlevel 1 ( echo LINK_FAIL & exit /b 1 )

python D:\MemLLVM\examples\imgui\extract_flat_bin.py probe_crt2.bin probe_crt2.exe
if errorlevel 1 ( echo PACK_FAIL & exit /b 1 )

echo BUILD_OK
endlocal
