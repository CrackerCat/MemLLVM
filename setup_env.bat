@echo off
setlocal
chcp 65001 >nul 2>&1
title MemLLVM SDK - Environment Setup

cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup_env.ps1" %*
if %ERRORLEVEL% neq 0 (
    echo.
    echo [-] Execution failed with error code: %ERRORLEVEL%
    pause
)
