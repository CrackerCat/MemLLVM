@echo off
setlocal
set "REPO=D:\MemLLVM\examples\mimikatz"
set "MSVC=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231"
set "SDK=C:\Program Files (x86)\Windows Kits\10"
set "SDKV=10.0.26100.0"
set "INCLUDE=%MSVC%\include;%SDK%\Include\%SDKV%\ucrt;%SDK%\Include\%SDKV%\um;%SDK%\Include\%SDKV%\shared"
set "LIB=%MSVC%\lib\x64;%SDK%\Lib\%SDKV%\ucrt\x64;%SDK%\Lib\%SDKV%\um\x64"
set "PATH=C:\LLVM-22-dev\bin;%MSVC%\bin\Hostx64\x64;%PATH%"
cd /d "%REPO%\build"

set "CFLAGS=/c /nologo /W0 /WX- /Ox /Ob2 /Os /Oy /GF /MT /GS- /Gy /fp:fast /fp:except- /Gd"
set "CFLAGS=%CFLAGS% --target=amd64-pc-windows-msvc -fcommon /utf-8"
set "CFLAGS=%CFLAGS% /clang:-fpass-plugin=D:/MemLLVM/bin/MemLLVM.dll"
set "CFLAGS=%CFLAGS% /clang:-fno-jump-tables /clang:-mno-stack-arg-probe /clang:-fno-builtin"
set "CFLAGS=%CFLAGS% /D WIN32 /D NDEBUG /D _CONSOLE /D _UNICODE /D UNICODE"

echo [CC] flat_lib.c
clang-cl %CFLAGS% /Fo"flat_lib.obj" "flat_lib.c" || exit /b 1
echo [CC] flat_main.c
clang-cl %CFLAGS% /Fo"flat_main.obj" "flat_main.c" || exit /b 1

echo [LD] flat_test.exe
lld-link /nologo /nodefaultlib /errorlimit:0 ^
  /out:"flat_test.exe" /entry:main /subsystem:console /machine:x64 ^
  /opt:ref /opt:icf /dynamicbase /nxcompat /safeseh:no /pdb:none /incremental:no ^
  "flat_lib.obj" "flat_main.obj" ^
  "D:\MemLLVM\ltl\ltl.lib"
if errorlevel 1 exit /b 1

echo [PACK] flat_test.bin
python "%REPO%\tools\memllvm_pack.py" "flat_test.exe" "flat_test.bin"
endlocal
