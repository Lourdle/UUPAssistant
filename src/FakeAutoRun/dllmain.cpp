#include <Windows.h>
#include <Shlwapi.h>
#include "../DllFdHook/DllFdHook.h"
#include "../../deps/Detours/src/detours.h"

static HMODULE g_hAutoRun;
static decltype(CreateProcessW)* g_pCreateProcessWTrampoline;

static BOOL WINAPI MyCreateProcessW(
	LPCWSTR lpApplicationName,
	LPWSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes,
	LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCWSTR lpCurrentDirectory,
	LPSTARTUPINFOW lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation
)
{
	BOOL succeeded = g_pCreateProcessWTrampoline(lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes, bInheritHandles, dwCreationFlags | CREATE_SUSPENDED, lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
	if (succeeded)
	{
		WCHAR szProcessName[MAX_PATH];
		DWORD size = MAX_PATH;

		if (QueryFullProcessImageNameW(lpProcessInformation->hProcess, 0, szProcessName, &size))
		{
			PCWSTR pszFileName = PathFindFileNameW(szProcessName);
			if (_wcsicmp(pszFileName, L"SetupPrep.exe") == 0)
			{
				DWORD cbData = GetFileVersionInfoSizeW(szProcessName, nullptr);
				if (cbData != 0)
				{
					PVOID pData = malloc(cbData);
					GetFileVersionInfoW(szProcessName, 0, cbData, pData);
					VS_FIXEDFILEINFO* pFixedFileInfo;
					UINT uLength;
					bool result = VerQueryValueW(pData, L"\\", reinterpret_cast<LPVOID*>(&pFixedFileInfo), &uLength);
					if (result)
						result = HIWORD(pFixedFileInfo->dwFileVersionLS) >= 26100;
					free(pData);

					if (result)
					{
						RegDeleteTreeA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\CompatMarkers");
						RegDeleteTreeA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Shared");
						RegDeleteTreeA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\TargetVersionUpgradeExperienceIndicators");

						HKEY hKey;
						if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\HwReqChk", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
						{
							constexpr BYTE lpBypasses[] = "SQ_SecureBootCapable=TRUE\0SQ_SecureBootEnabled=TRUE\0SQ_TpmVersion=2\0SQ_RamMB=8192\0";
							RegSetValueExA(hKey, "HwReqChkVars", 0, REG_MULTI_SZ, lpBypasses, sizeof(lpBypasses));
							RegCloseKey(hKey);
						}

						if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\Setup\\MoSetup", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
						{
							DWORD dwTrue = TRUE;
							RegSetValueExA(hKey, "AllowUpgradesWithUnsupportedTPMOrCPU", 0, REG_DWORD, reinterpret_cast<PBYTE>(&dwTrue), sizeof(dwTrue));
							RegCloseKey(hKey);
						}
					}
				}
			}
			else if (_wcsicmp(pszFileName, L"setup.exe") == 0)
			{
				HKEY hKey;
				if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\Setup\\LabConfig", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
				{
					DWORD dwTrue = TRUE;
					RegSetValueExA(hKey, "BypassTPMCheck", 0, REG_DWORD, reinterpret_cast<PBYTE>(&dwTrue), sizeof(dwTrue));
					RegSetValueExA(hKey, "BypassRAMCheck", 0, REG_DWORD, reinterpret_cast<PBYTE>(&dwTrue), sizeof(dwTrue));
					RegSetValueExA(hKey, "BypassSecureBootCheck", 0, REG_DWORD, reinterpret_cast<PBYTE>(&dwTrue), sizeof(dwTrue));
					RegCloseKey(hKey);
				}
			}

			if (!(dwCreationFlags & CREATE_SUSPENDED))
				ResumeThread(lpProcessInformation->hThread);
		}
	}

	return succeeded;
}

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		g_hAutoRun = LoadLibraryA("RealAutoRun.dll");
		if (!g_hAutoRun)
			return FALSE;

		InitializeDllFd(hModule, g_hAutoRun, DLLFD_FLAG_ALL);
		g_pCreateProcessWTrampoline = reinterpret_cast<decltype(CreateProcessW)*>(GetKernelExport("CreateProcessW"));

		{
			DetourTransactionBegin();
			ULONG ulCount;
			HANDLE* hThreads = DetourUpdateAllThreads(&ulCount);
			DetourAttach(reinterpret_cast<PVOID*>(&g_pCreateProcessWTrampoline), MyCreateProcessW);
			DetourTransactionCommit();
			CloseThreadsAndFree(hThreads, ulCount);
		}

		DisableThreadLibraryCalls(hModule);
		break;
	case DLL_PROCESS_DETACH:
		FreeLibrary(g_hAutoRun);
		{
			DetourTransactionBegin();
			ULONG ulThreadCount;
			HANDLE* hThreads = DetourUpdateAllThreads(&ulThreadCount);
			if (g_pCreateProcessWTrampoline != reinterpret_cast<decltype(CreateProcessW)*>(GetKernelExport("CreateProcessW")))
				DetourDetach(reinterpret_cast<PVOID*>(&g_pCreateProcessWTrampoline), MyCreateProcessW);
			DetourTransactionCommit();
			CloseThreadsAndFree(hThreads, ulThreadCount);
		}
		break;
	}
	return TRUE;
}
