#include "pch.h"
#include "framework.h"
#include "FakeWimgAPI.h"
#include "../DllFdHook/DllFdHook.h"

#ifdef _DEBUG
#include <iostream>
using namespace std;
#pragma warning(disable: 4996)
#endif


BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		InitializeDllFd(hModule, GetModuleHandleA("realwimgapi.dll"), DLLFD_FLAG_FDMSG | DLLFD_FLAG_FDRES | DLLFD_FLAG_FDSTR);
#ifdef _DEBUG
		{
			CHAR szPath[MAX_PATH];
			GetModuleFileNameA(hModule, szPath, MAX_PATH);
			PSTR pBackslash = strrchr(szPath, '\\');
			if (pBackslash) *(pBackslash + 1) = '\0';

			SYSTEMTIME st;
			GetLocalTime(&st);

			// 使用 snprintf 一次性格式化
			CHAR szLogFileName[MAX_PATH];
			snprintf(szLogFileName, MAX_PATH, "%sFakeWimgAPI-%04d%02d%02d%02d%02d%02d%03d-%d",
				szPath,
				st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
				GetCurrentProcessId());

			fclose(stdout);
			freopen(szLogFileName, "w", stdout);
		}

		cout << '[' << '\t' << clock() << '\t' << ']' << " Created standard output redirected file.\n"
			<< "Process ID is " << GetCurrentProcessId()
			<< ". The handle to this instance is 0x" << GetModuleHandleW(nullptr) << ". The handle to the module of FakeWimgapi is 0x" << hModule << endl;
		cout << '[' << '\t' << clock() << '\t' << ']' << " Loaded RealWimgapi.dll at 0x" << GetModuleHandleA("realwimgapi.dll") << endl;
#endif
		break;
	case DLL_PROCESS_DETACH:
#ifdef _DEBUG
		cout << '[' << '\t' << clock() << '\t' << ']' << " Process detached." << endl;
		fclose(stdout);
		freopen("CONOUT$", "w", stdout);
#endif
		break;
	}
	return TRUE;
}

