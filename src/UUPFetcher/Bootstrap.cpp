#include "pch.h"
#include "framework.h"
#include "FetcherMain.h"

#include <shellapi.h>
#include <thread>

import DownloadHost;
import Misc;
import WebView;

static void DownloadHostCleanup()
{
	CloseHandle(g_pDownloadBackend->hProcess);
	VirtualFreeEx(GetCurrentProcess(), g_pDownloadBackend->pLangAndEdition, 0, MEM_RELEASE);
	VirtualFreeEx(GetCurrentProcess(), g_pDownloadBackend->pId, 0, MEM_RELEASE);
	VirtualFreeEx(GetCurrentProcess(), g_pDownloadBackend->pAppId, 0, MEM_RELEASE);
	VirtualFreeEx(GetCurrentProcess(), g_pDownloadBackend->pSHA1String, 0, MEM_RELEASE);
	VirtualFreeEx(GetCurrentProcess(), g_pDownloadBackend, 0, MEM_RELEASE);
}

inline
static bool ShouldWriteToStdOut(PWSTR pCmdLine)
{
	int argc;
	auto argv = CommandLineToArgvW(pCmdLine, &argc);
	bool b = false;
	if (argv)
	{
		for (int i = 0; i != argc; ++i)
			if (_wcsicmp(argv[i], L"/WritePathToStdOut") == 0)
			{
				b = true;
				break;
			}
		LocalFree(argv);
	}
	return b;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
	UNREFERENCED_PARAMETER(hInstance);
	UNREFERENCED_PARAMETER(hPrevInstance);

	if (g_pDownloadBackend)
	{
		std::thread([]()
			{
				WaitForSingleObject(g_pDownloadBackend->hProcess, INFINITE);
				DownloadHostCleanup();
				ExitProcess(0);
			}
		).detach();

		SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
		{
			MSG msg;
			PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
		} while (DownloadHostMain());
		int nExitCode = g_pDownloadBackend->nExitCode;
		DownloadHostCleanup();
		return nExitCode;
	}
	else
	{
		if (SetCurrentDirectoryW(GetAppDataPath()))
			RemoveDirectoryRecursive(L"Temp");

		Lourdle::UIFramework::UIFrameworkInit();

		FetcherMain main;
		main.bWriteToStdOut = ShouldWriteToStdOut(pCmdLine);
		main.ShowWindow(nCmdShow);
		main.UpdateWindow();
		Lourdle::UIFramework::EnterMessageLoop();

		Lourdle::UIFramework::UIFrameworkUninit();
	}

	return 0;
}
