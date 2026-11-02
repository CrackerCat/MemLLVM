@echo off
setlocal
set "REPO=D:/MemLLVM/examples/mimikatz"
set "LLVM=C:/LLVM-22-dev/bin"
set "MSVC=C:/Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231"
set "SDK=C:/Program Files (x86)\Windows Kits\10"
set "SDKV=10.0.26100.0"
set "INCLUDE=%MSVC%\include;%SDK%\Include\%SDKV%\ucrt;%SDK%\Include\%SDKV%\um;%SDK%\Include\%SDKV%\shared"
set "LIB=%MSVC%\lib\x64;%SDK%\Lib\%SDKV%\ucrt\x64;%SDK%\Lib\%SDKV%\um\x64"
set "PATH=%LLVM%;%MSVC%\bin\Hostx64\x64;%PATH%"
cd /d "%REPO%"
clang-cl /c /nologo /W0 /MT /GS- /Ox --target=amd64-pc-windows-msvc /utf-8 ^
  /clang:-fpass-plugin=D:/MemLLVM/bin/MemLLVM.dll /clang:-fno-jump-tables /clang:-fno-builtin ^
  /D WIN32 /D NDEBUG /D _CONSOLE /D _UNICODE /D UNICODE ^
  /Fo:"%REPO%\build\test_stdio.obj" "build\test_stdio.c"
if errorlevel 1 (echo [-] compile failed & exit /b 1)
lld-link /nologo /nodefaultlib /entry:main /subsystem:console /machine:x64 ^
  /opt:ref /opt:icf /pdb:none /incremental:no ^
  /out:"%REPO%\build\test_stdio.exe" ^
  "%REPO%\build\test_stdio.obj" "D:/MemLLVM/ltl/ltl.lib"
echo [*] link exit=%ERRORLEVEL%
