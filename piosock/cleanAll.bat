@echo on

rmdir /S /Q Install
rmdir /S /Q Install_Debug

del /F *.log *.wrn *.err

pushd lib
call cleanAll.bat
popd

pushd sys
call cleanAll.bat
popd

pushd piosock-test
call cleanAll.bat
popd

pushd piosocklib-test
call cleanAll.bat
popd

pushd "PiosockPackage"
call cleanAll.bat
popd