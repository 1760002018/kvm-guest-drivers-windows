@echo on

rmdir /S /Q Install
rmdir /S /Q Debug
rmdir /S /Q Release

del /F *.log *.wrn *.err

cd piogpudo
call clean.bat
cd ..

