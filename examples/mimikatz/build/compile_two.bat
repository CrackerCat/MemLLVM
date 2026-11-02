@echo off
setlocal enabledelayedexpansion
set "REPO=D:\MemLLVM\examples\mimikatz"
set "OUT=D:\MemLLVM\examples\mimikatz\build\mllvm"
set "LLVM=C:\LLVM-22-dev\bin"
set "MSVC=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231"
set "SDK=C:\Program Files (x86)\Windows Kits\10"
set "SDKV=10.0.26100.0"
set "INCLUDE=%MSVC%\include;%SDK%\Include\%SDKV%\ucrt;%SDK%\Include\%SDKV%\um;%SDK%\Include\%SDKV%\shared"
set "LIB=%MSVC%\lib\x64;%SDK%\Lib\%SDKV%\ucrt\x64;%SDK%\Lib\%SDKV%\um\x64"
set "PATH=%LLVM%;%MSVC%\bin\Hostx64\x64;%PATH%"
cd /d "%REPO%\mimikatz"
set FAILED=0
set FAILED=0
clang-cl /c /nologo /W0 /WX- /Ox /Ob2 /Os /Oy /GF /MT /GS- /Gy /fp:fast /fp:except- /Gd --target=amd64-pc-windows-msvc -fcommon /utf-8 /clang:-fpass-plugin=D:/MemLLVM/bin/MemLLVM.dll /clang:-fno-jump-tables /clang:-fno-builtin /D WIN32 /D NDEBUG /D _CONSOLE /D _UNICODE /D UNICODE /I"D:\MemLLVM\examples\mimikatz/inc" /Fo"D:\MemLLVM\examples\mimikatz\build\mllvm\kull_m_output.obj" "..\modules\kull_m_output.c" || set FAILED=1
clang-cl /c /nologo /W0 /WX- /Ox /Ob2 /Os /Oy /GF /MT /GS- /Gy /fp:fast /fp:except- /Gd --target=amd64-pc-windows-msvc -fcommon /utf-8 /clang:-fpass-plugin=D:/MemLLVM/bin/MemLLVM.dll /clang:-fno-jump-tables /clang:-fno-builtin /D WIN32 /D NDEBUG /D _CONSOLE /D _UNICODE /D UNICODE /I"D:\MemLLVM\examples\mimikatz/inc" /Fo"D:\MemLLVM\examples\mimikatz\build\mllvm\mimikatz.obj" "mimikatz.c" || set FAILED=1
echo FAILED=!FAILED!
endlocal
