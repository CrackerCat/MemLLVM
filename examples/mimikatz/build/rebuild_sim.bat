@echo off
setlocal
cd /d D:\MemLLVM\examples\mimikatz\build
"C:\LLVM-22-dev\bin\clang-cl.exe" /nologo /W3 /O2 /MT sim_scloader.c /Fe:sim_scloader.exe /link /SUBSYSTEM:CONSOLE kernel32.lib user32.lib advapi32.lib
if errorlevel 1 ( echo BUILD_FAIL & exit /b 1 )
echo BUILD_OK
endlocal
