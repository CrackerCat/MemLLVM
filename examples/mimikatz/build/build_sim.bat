@echo off
setlocal
set "MSVC=C:/Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231"
set "SDK=C:/Program Files (x86)\Windows Kits\10"
set "SDKV=10.0.26100.0"
set "INCLUDE=%MSVC%\include;%SDK%\Include\%SDKV%\ucrt;%SDK%\Include\%SDKV%\um;%SDK%\Include\%SDKV%\shared"
set "LIB=%MSVC%\lib\x64;%SDK%\Lib\%SDKV%\ucrt\x64;%SDK%\Lib\%SDKV%\um\x64"
set "PATH=C:/LLVM-22-dev/bin;%MSVC%/bin/Hostx64/x64;%PATH%"
cd /d "%~dp0.."
clang-cl /nologo /W3 /O1 /MT /GS- build\sim_scloader.c /Fe:build\sim_scloader.exe /link /STACK:16777216 /nologo user32.lib
echo exit=%ERRORLEVEL%
