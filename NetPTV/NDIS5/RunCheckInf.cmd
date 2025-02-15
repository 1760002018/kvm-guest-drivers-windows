mkdir htm
del /q htm\*
call c:\winddk\7600.16385.1\tools\chkinf\chkinf.bat Install\Vista\amd64\netptv.inf Install\Vista\x86\netptv.inf Install\XP\amd64\netptv.inf Install\XP\x86\netptv.inf
start htm\summary.htm


