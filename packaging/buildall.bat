setlocal

mkdir Install
copy ..\piostor\txtsetup-amd64.oem .\Install\
copy ..\piostor\txtsetup-i386.oem .\Install\
copy ..\piostor\disk1 .\Install\

copy ..\COPYING .\Install\
copy ..\LICENSE .\Install\

endlocal