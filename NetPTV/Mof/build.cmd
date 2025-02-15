mofcomp.exe -B:.\netptv.bmf ..\Common\netptv.mof
wmimofck.exe -h..\Common\tmpmof.h -m -u .\netptv.bmf
fc ..\Common\netptvmof.h ..\Common\tmpmof.h > nul
if not errorlevel 1 goto thesame
echo Updating netptvmof.h
copy /Y ..\Common\tmpmof.h ..\Common\netptvmof.h
:thesame
del ..\Common\tmpmof.h
