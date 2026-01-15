:start
cd /d %~dp0\..\deps

echo Checking the zip submodule...
pushd zip
call :checkdir
if %errorlevel% lss 1 (
	echo The zip submodule not found. Cloning...
	if exist .git call :delfile .git
	popd
	git submodule update --init --recursive
) else (
	echo The zip submodule found.
	popd
)


echo Checking the Detours submodule...
pushd Detours
call :checkdir
if %errorlevel% lss 1 (
	echo The Detours submodule not found. Cloning...
	if exist .git call :delfile .git
	popd
	git submodule update --init --recursive
) else (
	echo The Detours submodule found.
	popd
)


powershell -ExecutionPolicy Bypass -File ..\scripts\locate_adk.ps1
exit /b

:delfile
attrib -h %1
del %1
exit /b

:checkdir
set count=0
for /f %%i in ('dir /b') do set /a count+=1
exit /b %count%
