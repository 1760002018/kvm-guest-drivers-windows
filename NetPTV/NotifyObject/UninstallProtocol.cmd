@echo off
net session > nul
if not errorlevel 1 goto uninstall
echo Run this batch as an administrator
pause
goto :eof

:uninstall
cd /d "%~dp0"
netcfg -v -u PIOPROT
timeout /t 3
for %%f in (%windir%\inf\oem*.inf) do call :checkinf %%f
echo Done
timeout /t 3
goto :eof

:checkinf
type %1 | findstr /i pioprot.cat
if not errorlevel 1 goto :removeinf
echo %1 is not PIOPROT inf file
goto :eof

:removeinf
echo This is PIOPROT inf file
pnputil /d "%~nx1"
timeout /t 2
goto :eof
