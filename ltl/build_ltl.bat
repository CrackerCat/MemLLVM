@echo off
setlocal

cd /d "%~dp0"
set LTL_ROOT=%~dp0
if "%LTL_ROOT:~-1%"=="\" set LTL_ROOT=%LTL_ROOT:~0,-1%

if "%CLANG_BIN%"=="" set CLANG_BIN=C:\LLVM-22-dev\bin\clang-cl.exe
if not exist "%CLANG_BIN%" set CLANG_BIN=clang-cl.exe

if "%LIB_BIN%"=="" set LIB_BIN=C:\LLVM-22-dev\bin\llvm-lib.exe
if not exist "%LIB_BIN%" set LIB_BIN=llvm-lib.exe

if "%PLUGIN_DLL%"=="" set PLUGIN_DLL=%LTL_ROOT%\..\build\MemLLVM\Release\MemLLVM.dll
if not exist "%PLUGIN_DLL%" set PLUGIN_DLL=%LTL_ROOT%\..\build\Release\MemLLVM.dll

echo [*] Compiling MemLLVM-LTL components...

"%CLANG_BIN%" /c /O2 /GS- /GR- /clang:-fno-jump-tables /clang:-fno-unwind-tables /clang:-fno-asynchronous-unwind-tables /clang:-fno-exceptions /D_HAS_EXCEPTIONS=0 -I"%LTL_ROOT%\include" "/clang:-fpass-plugin=%PLUGIN_DLL%" "%LTL_ROOT%\src\concurrency.cpp" /Fo"%LTL_ROOT%\src\concurrency.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" /c /O2 /GS- /GR- /clang:-fno-jump-tables /clang:-fno-unwind-tables /clang:-fno-asynchronous-unwind-tables /clang:-fno-exceptions /D_HAS_EXCEPTIONS=0 -I"%LTL_ROOT%\include" "/clang:-fpass-plugin=%PLUGIN_DLL%" "%LTL_ROOT%\src\emutls.cpp" /Fo"%LTL_ROOT%\src\emutls.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" /c /O2 /GS- /GR- /clang:-fno-jump-tables /clang:-fno-unwind-tables /clang:-fno-asynchronous-unwind-tables /clang:-fno-exceptions /D_HAS_EXCEPTIONS=0 -I"%LTL_ROOT%\include" "/clang:-fpass-plugin=%PLUGIN_DLL%" "%LTL_ROOT%\src\filesystem.cpp" /Fo"%LTL_ROOT%\src\filesystem.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" /c /O2 /GS- /GR- /clang:-fno-jump-tables /clang:-fno-unwind-tables /clang:-fno-asynchronous-unwind-tables /clang:-fno-exceptions /D_HAS_EXCEPTIONS=0 -I"%LTL_ROOT%\include" "/clang:-fpass-plugin=%PLUGIN_DLL%" "%LTL_ROOT%\src\crt_mem.cpp" /Fo"%LTL_ROOT%\src\crt_mem.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" /c /O2 /GS- /GR- /clang:-fno-jump-tables /clang:-fno-unwind-tables /clang:-fno-asynchronous-unwind-tables /clang:-fno-exceptions /D_HAS_EXCEPTIONS=0 -I"%LTL_ROOT%\include" "/clang:-fpass-plugin=%PLUGIN_DLL%" "%LTL_ROOT%\src\iostream.cpp" /Fo"%LTL_ROOT%\src\iostream.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" /c /O2 /GS- /GR- /clang:-fno-jump-tables /clang:-fno-unwind-tables /clang:-fno-asynchronous-unwind-tables /clang:-fno-exceptions /D_HAS_EXCEPTIONS=0 -I"%LTL_ROOT%\include" "/clang:-fpass-plugin=%PLUGIN_DLL%" "%LTL_ROOT%\src\locale.cpp" /Fo"%LTL_ROOT%\src\locale.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" /c /O2 /GS- /GR- /clang:-fno-jump-tables /clang:-fno-unwind-tables /clang:-fno-asynchronous-unwind-tables /clang:-fno-exceptions /D_HAS_EXCEPTIONS=0 -I"%LTL_ROOT%\include" "/clang:-fpass-plugin=%PLUGIN_DLL%" "%LTL_ROOT%\src\regex.cpp" /Fo"%LTL_ROOT%\src\regex.obj"
if %ERRORLEVEL% neq 0 exit /b 1

"%CLANG_BIN%" /c /O2 /GS- /GR- /clang:-fno-jump-tables /clang:-fno-unwind-tables /clang:-fno-asynchronous-unwind-tables /clang:-fno-exceptions /D_HAS_EXCEPTIONS=0 -I"%LTL_ROOT%\include" "/clang:-fpass-plugin=%PLUGIN_DLL%" "%LTL_ROOT%\src\async.cpp" /Fo"%LTL_ROOT%\src\async.obj"
if %ERRORLEVEL% neq 0 exit /b 1

echo [*] Archiving into ltl.lib...
"%LIB_BIN%" /out:"%LTL_ROOT%\ltl.lib" "%LTL_ROOT%\src\concurrency.obj" "%LTL_ROOT%\src\emutls.obj" "%LTL_ROOT%\src\filesystem.obj" "%LTL_ROOT%\src\crt_mem.obj" "%LTL_ROOT%\src\iostream.obj" "%LTL_ROOT%\src\locale.obj" "%LTL_ROOT%\src\regex.obj" "%LTL_ROOT%\src\async.obj"
if %ERRORLEVEL% neq 0 exit /b 1

del "%LTL_ROOT%\src\*.obj" >nul 2>&1

echo [+] MemLLVM-LTL built successfully: %LTL_ROOT%\ltl.lib
endlocal
