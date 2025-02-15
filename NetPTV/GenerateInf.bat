@echo off
pushd "%~dp0"
if /i "%~n1"=="netptv_no_RSS" call :netptv_no_RSS
if /i "%~n1"=="netptv_no_RSC" call :netptv_no_RSC
if /i "%~n1"=="netptv" call :netptv
popd
goto :eof
:netptv_no_RSS
copy /y netptv-base.txt netptv_no_RSS.inx.tmp > nul
call :update netptv_no_RSS.inx
goto :eof
:netptv_no_RSC
copy /y netptv-base.txt + netptv-add-rss.txt netptv_no_RSC.inx.tmp > nul
call :update netptv_no_RSC.inx
goto :eof
:netptv
copy /y netptv-base.txt + netptv-add-rss.txt + netptv-add-rsc.txt netptv.inx.tmp > nul
call :update netptv.inx
goto :eof
:update
fc /b %1 %1.tmp >nul 2>&1
if errorlevel 1 copy /y %1.tmp %1 > nul
del %1.tmp
goto :eof
