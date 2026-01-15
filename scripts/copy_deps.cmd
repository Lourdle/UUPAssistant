:start
call :copyfile "%1libwim-15.dll" "%2\libwim-15.dll"
call :copyfile "%1Lourdle.UIFramework.dll" "%2\Lourdle.UIFramework.dll"
call :copyfile "%1Lourdle.DownloaderAPI.dll" "%2\Lourdle.DownloaderAPI.dll"
exit /b

:copyfile
if not exist %1 (
	copy %2 %1
	exit /b
)
echo %1 exists, comparing...
fc %1 %2 > nul
if %errorlevel% equ 0 (
	echo %1 is up to date, skipping...
) else (
	echo %1 is out of date, copying...
	del %1
	copy %2 %1
)
exit /b
