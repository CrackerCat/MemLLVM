@echo off
rem Build mimikatz Release|x64 with the MemLLVM ClangCL pipeline.
rem Proxy vars must be empty: HTTPS_PROXY and https_proxy coexisting
rem makes MSBuild's ToolTask throw ArgumentException.
set "MSB=C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
set "HTTPS_PROXY="
set "HTTP_PROXY="
set "https_proxy="
set "http_proxy="
"%MSB%" "%~dp0..\mimikatz\mimikatz.vcxproj" /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=D:\MemLLVM\examples\mimikatz\ /m /v:m /nologo %*
