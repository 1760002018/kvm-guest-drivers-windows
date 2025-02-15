@echo off
call ..\tools\build.bat pioscsi.sln "Wlh Win7 Win8 Win10" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat pioscsi.vcxproj "Win8_SDV Win10_SDV" %*
