@echo off
setlocal
set "REPO=D:\MemLLVM\examples\mimikatz"
set "PATH=C:\LLVM-22-dev\bin;%PATH%"
cd /d "%REPO%"

echo [*] flat link: /nodefaultlib, only ltl.lib + project .min.lib
lld-link /nologo /nodefaultlib /errorlimit:0 ^
  /out:"%REPO%\x64\mimikatz_mllvm.exe" ^
  /entry:main /subsystem:console /machine:x64 ^
  /opt:ref /opt:icf /dynamicbase /nxcompat /safeseh:no ^
  /pdb:none /incremental:no /map:"%REPO%\x64\mimikatz_mllvm.map" ^
  "%REPO%\build\mllvm\*.obj" ^
  "%REPO%\mimikatz\x64\mimikatz.res" ^
  "D:\MemLLVM\ltl\ltl.lib" ^
  "%REPO%\lib\x64\msasn1.min.lib" ^
  "%REPO%\lib\x64\ntdll.min.lib" ^
  "%REPO%\lib\x64\netapi32.min.lib" ^
  "%REPO%\lib\x64\advapi32.hash.lib" ^
  "C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64\uuid.lib" ^
  "C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64\wbemuuid.lib" ^
  "C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64\MsXml2.Lib"
echo [*] lld-link exit=%ERRORLEVEL%
endlocal
