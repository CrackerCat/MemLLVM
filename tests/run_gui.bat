@echo off
setlocal
cd /d "%~dp0"
echo [*] Executing gui_window.bin with raw machine code runner...
run_bin.exe gui_window.bin
endlocal
