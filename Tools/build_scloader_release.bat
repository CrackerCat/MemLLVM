@echo off
setlocal
rem MSBuild's ToolTask keeps environment variables in a case-insensitive
rem dictionary, so HTTPS_PROXY and https_proxy in the parent process crash it
rem with ArgumentException. Clear them before building.
set "HTTP_PROXY="
set "HTTPS_PROXY="
set "ALL_PROXY="
set "NO_PROXY="
set "http_proxy="
set "https_proxy="
set "all_proxy="
set "no_proxy="
set "MSB=C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
if not exist "%MSB%" (
  echo MSBUILD_NOT_FOUND
  exit /b 1
)
cd /d E:\C++project\CUI
"%MSB%" SCLoader\SCLoader.vcxproj /p:Configuration=Release /p:Platform=x64 /m /v:minimal /nologo
if errorlevel 1 (
  echo BUILD_FAIL
  exit /b 1
)
echo BUILD_OK
endlocal
