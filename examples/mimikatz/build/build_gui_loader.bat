@echo off
setlocal
set "CLANG=C:\LLVM-22-dev\bin\clang-cl.exe"
cd /d D:\MemLLVM\examples\mimikatz\build
"%CLANG%" /nologo /W3 /O2 /MT /DUNICODE /D_UNICODE sim_gui_loader.c /Fe:sim_gui_loader.exe /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup user32.lib gdi32.lib advapi32.lib
if errorlevel 1 ( echo BUILD_FAIL & exit /b 1 )
copy /y ..\x64\mimikatz_mllvm.bin payload.bin >nul
del /q sim_gui_log.txt 2>nul
start /wait "" sim_gui_loader.exe
echo ---LOG---
type sim_gui_log.txt
endlocal
