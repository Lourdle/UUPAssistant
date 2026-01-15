pushd %~dp0

call :checklib msvcrt x64
call :checklib msvcrt ARM64
call :checklib msvcp_win x64
call :checklib msvcp_win ARM64
call :checklib msvcp60 %1
call :checklib ucrtbase %1

popd
exit /b

:checklib
if not exist ..\bin\%2\%1.lib (
	echo Library %1 does not exist. Building...
	md ..\bin\%2
	pushd ..\bin\%2
	echo LIBRARY %1>%1.def
	echo EXPORTS>>%1.def
	..\tools\DumpExports.exe %1.dll>>%1.def
	if %1 == msvcrt (
		find "__CxxFrameHandler4" %1.def >nul || echo __CxxFrameHandler4>>%1.def
		find "__C_specific_handler" %1.def >nul || echo __C_specific_handler>>%1.def
		find "ceilf" %1.def >nul || echo ceilf>>%1.def
	)
	lib /NOLOGO /MACHINE:%2 /DEF:%1.def /OUT:%1.lib
	popd
)
