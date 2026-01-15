pushd %~dp0\..
if not exist bin\tools\%1.exe (
	call :build %1
	popd
	exit /b
)
fc bin\tools\%1.exe.version tools\%1\tool_version.txt /b>nul
if not %errorlevel%==0 (
	echo Tool %1 is out of date. Rebuilding...
	del bin\tools\%1.exe
	call :build %1
)

popd
exit /b

:build
echo Tool %1 does not exist. Building...
MSBuild tools\tools.sln -target:%1 -p:Platform=%PROCESSOR_ARCHITECTURE% -nologo -verbosity:quiet
