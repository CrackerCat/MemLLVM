@echo off
setlocal
cd /d D:\MemLLVM\examples\mimikatz\build
"C:\LLVM-22-dev\bin\clang-cl.exe" /nologo /W3 /O2 /MT drive_scloader.c /Fe:drive_scloader.exe /link /SUBSYSTEM:CONSOLE user32.lib gdi32.lib
if errorlevel 1 ( echo BUILD_FAIL & exit /b 1 )
echo BUILD_OK
endlocal
