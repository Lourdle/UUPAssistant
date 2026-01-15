pushd %~dp0
call checktool configure
cd ..
bin\tools\configure.exe
popd
