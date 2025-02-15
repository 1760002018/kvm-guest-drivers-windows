@echo off
call ..\tools\build.bat piosock.sln "Win7 Win8 Win10" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat sys\piosock.vcxproj "Win10_SDV" %*
