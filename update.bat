@echo off
setlocal
chcp 65001 >nul 2>&1
title MemLLVM SDK - Auto Updater

cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0update.ps1" %*
if %ERRORLEVEL% neq 0 (
    echo.
    echo [-] Update failed with error code: %ERRORLEVEL%
    pause
)
