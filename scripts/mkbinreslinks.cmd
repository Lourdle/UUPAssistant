cd /d %~dp0\..

if %1 == amd64 (
	set platform=x64
) else (
	set platform=%1
)

bin\tools\MakeSymLink bin\FakeSetupHost.exe bin\%platform%\Release\FakeSetupHost.exe

bin\tools\MakeSymLink bin\FakeWimgAPI.dll bin\%platform%\%2\FakeWimgAPI.dll

if not exist bin\%platform%\Oscdimg.cab makecab deps\adk\%1\Oscdimg\oscdimg.exe bin\%platform%\Oscdimg.cab
bin\tools\MakeSymLink bin\Oscdimg.cab bin\%platform%\Oscdimg.cab
