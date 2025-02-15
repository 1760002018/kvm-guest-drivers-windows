@echo off
call ..\tools\build.bat piogpu.sln "Win8 Win10" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat piogpudo\piogpudo.vcxproj "Win10_SDV" %*
