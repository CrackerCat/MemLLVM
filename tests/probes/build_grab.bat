@echo off
setlocal
cd /d D:\MemLLVM\Tools
"C:\LLVM-22-dev\bin\clang-cl.exe" /nologo /W3 /O2 /MT grab_window.c /Fe:grab_window.exe /link /SUBSYSTEM:CONSOLE kernel32.lib user32.lib gdi32.lib
if errorlevel 1 ( echo BUILD_FAIL & exit /b 1 )
echo BUILD_OK
endlocal
