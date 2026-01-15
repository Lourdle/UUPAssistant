#include "pch.h"
#include <dismapi.h>
#include <wimgapi.h>
#include <wimlib.h>
#include <winternl.h>
#include <rapidxml/rapidxml.hpp>

#include <thread>
#include <format>


enum SHUTDOWN_ACTION {
	ShutdownNoReboot,
	ShutdownReboot,
	ShutdownPowerOff
};

extern "C" NTSTATUS WINAPI NtShutdownSystem(SHUTDOWN_ACTION);

import Constants;
import CheckMiniNt;

HostContext* g_pHostContext;

using namespace std;
using namespace Lourdle::UIFramework;


void UnloadMountedImageRegistries(PCWSTR pszMountDir)
{
	HANDLE hDir = CreateFileW(pszMountDir, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	DWORD len = GetFinalPathNameByHandleW(hDir, nullptr, 0, VOLUME_NAME_NT);
	if (len > 0)
	{
		MyUniquePtr<WCHAR> pszNtMountPath = len;
		GetFinalPathNameByHandleW(hDir, pszNtMountPath, len, VOLUME_NAME_NT);
		CloseHandle(hDir);
		DWORD dwPathLen = len - 1;
		HKEY hHivelist = nullptr;
		RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\hivelist", 0, KEY_READ, &hHivelist);
		RegQueryInfoKeyW(hHivelist, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &len, nullptr, nullptr, nullptr);
		wstring Value(len, 0);
		wstring Data;
		DWORD cchName = ++len, cbData = 0;
		for (DWORD i = 0; RegEnumValueW(hHivelist, i, const_cast<LPWSTR>(Value.c_str()), &cchName, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS; ++i)
		{
			RegGetValueW(hHivelist, nullptr, Value.c_str(), RRF_RT_REG_SZ, nullptr, nullptr, &cbData);
			Data.resize(cbData / 2 - 1);
			RegGetValueW(hHivelist, nullptr, Value.c_str(), RRF_RT_REG_SZ, nullptr, const_cast<LPWSTR>(Data.c_str()), &cbData);
			PWSTR pszPath = const_cast<PWSTR>(Data.data());
			HANDLE hFile = CreateFileW((wstring(L"\\\\?\\GLOBALROOT") += Data).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (hFile != INVALID_HANDLE_VALUE)
			{
				DWORD size = GetFinalPathNameByHandleW(hFile, nullptr, 0, VOLUME_NAME_NT);
				if (size != 0)
				{
					pszPath = new WCHAR[size];
					GetFinalPathNameByHandleW(hFile, pszPath, size, VOLUME_NAME_NT);
				}
				CloseHandle(hFile);
			}
			if (_wcsnicmp(pszNtMountPath, pszPath, dwPathLen) == 0
				&& _wcsnicmp(Value.c_str(), L"\\Registry\\MACHINE\\", 18) == 0
				&& RegUnLoadKeyW(HKEY_LOCAL_MACHINE, Value.c_str() + 18) == ERROR_SUCCESS)
				--i;
			if (pszPath != Data.c_str())
				delete[] pszPath;
			cchName = len;
		}
		RegCloseKey(hHivelist);
	}
	else
		CloseHandle(hDir);
}

int
WINAPI
wWinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine,
	_In_ int nCmdShow
)
{
	UNREFERENCED_PARAMETER(hPrevInstance);

	if (!g_pHostContext)
	{
		HANDLE hProcess;
		PWSTR PathTemp = nullptr;
		HostAction Action = HostAction::None;
		bool bMiniNT = IsMiniNtBoot();
		{
			STARTUPINFOW si = {
				.cb = sizeof(si),
				.dwFlags = STARTF_USESHOWWINDOW,
				.wShowWindow = static_cast<WORD>(nCmdShow)
			};
			PROCESS_INFORMATION pi;
			WCHAR szPath[MAX_PATH];
			GetSystemDirectoryW(szPath, MAX_PATH);
			SetCurrentDirectoryW(szPath);
			GetModuleFileNameW(hInstance, szPath, MAX_PATH);
			if (!CreateProcessW(szPath, lpCmdLine, nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, nullptr, &si, &pi))
				return HRESULT_FROM_WIN32(GetLastError());
			HostContext ctx = {
				.wParam = reinterpret_cast<WPARAM>(&PathTemp),
				.lParam = reinterpret_cast<LPARAM>(&Action)
			};
			DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), pi.hProcess, &ctx.hParent, 0, FALSE, DUPLICATE_SAME_ACCESS);
			hProcess = pi.hProcess;
			HostContext* p = reinterpret_cast<HostContext*>(VirtualAllocEx(pi.hProcess, nullptr, sizeof(HostContext), MEM_COMMIT, PAGE_READWRITE));
			WriteProcessMemory(pi.hProcess, &g_pHostContext, &p, sizeof(p), nullptr);
			WriteProcessMemory(pi.hProcess, p, &ctx, sizeof(ctx), nullptr);
			ResumeThread(pi.hThread);
			CloseHandle(pi.hThread);

			SetProcessEfficiencyMode(true);
		}
		WaitForSingleObject(hProcess, INFINITE);
		if (Action != HostAction::WaitForProcess || Action != HostAction::Quit)
		{
			CloseHandle(hProcess);
			KillChildren();
		}

		if (!PathTemp)
			return HRESULT_FROM_WIN32(ERROR_CANCELLED);
		else
			SetProcessEfficiencyMode(false);

		AdjustPrivileges({ SE_SHUTDOWN_NAME, SE_RESTORE_NAME, SE_BACKUP_NAME });

		if (GetFileAttributesW(PathTemp) & FILE_ATTRIBUTE_DIRECTORY)
		{
			UnloadMountedImageRegistries(PathTemp);
			HANDLE hImage, hWim;
			bool bUnmounted = true;
			wstring MountDir = PathTemp;
			MountDir += L"Mount";
			if (WIMGetMountedImageHandle(MountDir.c_str(), 0, &hWim, &hImage))
			{
				bUnmounted = WIMUnmountImageHandle(hImage, 0);
				WIMCloseHandle(hImage);
				WIMCloseHandle(hWim);
			}

			if (GetFileAttributesW(MountDir.c_str()) == FILE_ATTRIBUTE_DIRECTORY)
				ForceDeleteDirectory(MountDir.c_str());

			DeleteDirectory(PathTemp);
		}

		VirtualFreeEx(GetCurrentProcess(), PathTemp, 0, MEM_RELEASE);

		switch (Action)
		{
		case HostAction::Shutdown:
			if (bMiniNT)
			{
				BlockInput(TRUE);
				NTSTATUS Status = NtShutdownSystem(ShutdownPowerOff);
				if (!NT_SUCCESS(Status))
				{
					BlockInput(FALSE);
					SetLastError(RtlNtStatusToDosError(Status));
				}
			}
			else
				InitiateSystemShutdownExW(nullptr, nullptr, 0, TRUE, FALSE, SHTDN_REASON_MAJOR_OPERATINGSYSTEM | SHTDN_REASON_MINOR_INSTALLATION | REASON_PLANNED_FLAG);
			break;
		case HostAction::Reboot:
			if (bMiniNT)
			{
				BlockInput(TRUE);
				NTSTATUS Status = NtShutdownSystem(ShutdownReboot);
				if (!NT_SUCCESS(Status))
				{
					BlockInput(FALSE);
					SetLastError(RtlNtStatusToDosError(Status));
				}
			}
			else
				InitiateSystemShutdownExW(nullptr, nullptr, 0, TRUE, TRUE, SHTDN_REASON_MAJOR_OPERATINGSYSTEM | SHTDN_REASON_MINOR_INSTALLATION | REASON_PLANNED_FLAG);
			break;
		case HostAction::WaitForProcess:
		case HostAction::Quit:
		{
			DWORD dwExitCode;
			GetExitCodeProcess(hProcess, &dwExitCode);
			CloseHandle(hProcess);
			if (Action == HostAction::WaitForProcess)
			{
				hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, dwExitCode);
				if (hProcess)
				{
					WaitForSingleObject(hProcess, INFINITE);
					GetExitCodeProcess(hProcess, &dwExitCode);
					CloseHandle(hProcess);
				}
			}
			return dwExitCode;
		}
		default:
			SetLastError(ERROR_INVALID_PARAMETER);
		}
		return HRESULT_FROM_WIN32(GetLastError());
	}
	else if (!g_pHostContext->hParent && g_pHostContext->lParam)
	{
		CreateDirectoryW(L"Temp", nullptr);
		DismInitialize(DismLogErrors, L"DismLog.txt", L"Temp");
		DismSession Session;
		HWND hWnd = reinterpret_cast<HWND>(g_pHostContext->wParam);
		if (FAILED(DismOpenSession(reinterpret_cast<PWSTR>(g_pHostContext->lParam), nullptr, nullptr, &Session)))
		{
			DismString* error;
			DismGetLastErrorMessage(&error);
			SendMessageW(hWnd, UupAssistantMsg::DriverDlg_Error, reinterpret_cast<WPARAM>(error->Value), static_cast<LPARAM>(wcslen(error->Value) + 1) * 2);
			DismDelete(error);
		}
		else
		{
			DismDriverPackage* Packages;
			UINT Count;
			if (FAILED(DismGetDrivers(Session, FALSE, &Packages, &Count)))
			{
				DismString* error;
				DismGetLastErrorMessage(&error);
				SendMessageW(hWnd, UupAssistantMsg::DriverDlg_Error, reinterpret_cast<WPARAM>(error->Value), static_cast<LPARAM>(wcslen(error->Value) + 1) * 2);
				DismDelete(error);
			}
			else
			{
				for (UINT i = 0; i != Count; ++i)
				{
					auto len1 = wcslen(Packages[i].ClassDescription);
					auto len2 = wcslen(Packages[i].ProviderName);
					auto len3 = wcslen(Packages[i].OriginalFileName);

					auto Buffer = MyUniqueBuffer<PWSTR>(sizeof(WCHAR) * (len1 + len2 + len3 + 3) + sizeof(SYSTEMTIME) + sizeof(UINT) * 4);
					auto p = Buffer.get();
					wcscpy_s(p, len1 + 1, Packages[i].ClassDescription);
					p += len1 + 1;
					wcscpy_s(p, len2 + 1, Packages[i].ProviderName);
					p += len2 + 1;
					wcscpy_s(p, len3 + 1, Packages[i].OriginalFileName);
					memcpy(p + len3 + 1, &Packages[i].Date, sizeof(SYSTEMTIME) + sizeof(UINT) * 4);
					SendMessageW(hWnd, UupAssistantMsg::DriverDlg_DriverItem, reinterpret_cast<WPARAM>(Buffer.get()), (len1 + len2 + len3 + 3) * sizeof(WCHAR) + sizeof(SYSTEMTIME) + sizeof(UINT) * 4);
				}

				DismDelete(Packages);
			}
			DismCloseSession(Session);
		}

		VirtualFreeEx(GetCurrentProcess(), reinterpret_cast<LPVOID>(g_pHostContext->lParam), 0, MEM_RELEASE);
		VirtualFreeEx(GetCurrentProcess(), g_pHostContext, 0, MEM_RELEASE);
		DismShutdown();
		DeleteDirectory(L"Temp");
		PostMessageW(hWnd, UupAssistantMsg::DriverDlg_DriverScanCompleted, 0, 0);
		return 0;
	}
	else if (!g_pHostContext->hParent && !g_pHostContext->lParam)
	{
		HWND hWnd = reinterpret_cast<HWND>(g_pHostContext->wParam);
		WCHAR szPath[MAX_PATH];
		GetSystemDirectoryW(szPath, MAX_PATH);
		WCHAR cSystemDrive = szPath[0];
		auto len = wcslen(szPath);
		szPath[len] = '\\';
		++len;

		AdjustPrivileges({ SE_RESTORE_NAME, SE_BACKUP_NAME });

		for (char i = 'A'; i != 'Z' + 1; ++i)
		{
			szPath[0] = i;
			memcpy(szPath + len, L"ntoskrnl.exe", 26);
			VersionStruct version;
			if (GetFileVersion(szPath, version))
			{
				memcpy(szPath + len, L"config\\SOFTWARE", 32);
				RegistryHive RegHive;
				PCWSTR pSoftwareKey = L"SOFTWARE";
				if (cSystemDrive != i)
				{
					if (!RegHive.Reset(szPath, false))
						continue;
					pSoftwareKey = RegHive;
				}

				auto DisplayVersion = GetVersionValue(pSoftwareKey, L"DisplayVersion");
				auto ProductName = GetVersionValue(pSoftwareKey, L"ProductName");
				auto CurrentBuild = GetVersionValue(pSoftwareKey, L"CurrentBuild");
				auto BuildBranch = GetVersionValue(pSoftwareKey, L"BuildBranch");
				auto [dwMajor, dwMinor, dwPrivateBuild, dwSpecialBuild] = version;

				auto build = static_cast<DWORD>(_wtol(CurrentBuild.c_str()));
				if (build > dwPrivateBuild)
					dwPrivateBuild = build;

				auto VersionString = std::format(L"{}.{}.{}.{},", dwMajor, dwMinor, dwPrivateBuild, dwSpecialBuild);
				if (DisplayVersion.empty())
				{
					DisplayVersion = GetVersionValue(pSoftwareKey, L"ReleaseId");
					if (!DisplayVersion.empty())
					{
						VersionString += L' ';
						VersionString += DisplayVersion;
					}
					else
						VersionString.pop_back();
				}
				else
				{
					VersionString += L' ';
					VersionString += DisplayVersion;
				}
				auto size = ProductName.size() + CurrentBuild.size() + BuildBranch.size() + VersionString.size() + 6;
				MyUniquePtr<WCHAR> p = size;
				*p = i;
				auto p2 = p + 1;
				wcscpy_s(p2, ProductName.size() + 1, ProductName.c_str());
				p2 += ProductName.size() + 1;
				wcscpy_s(p2, BuildBranch.size() + 1, BuildBranch.c_str());
				p2 += BuildBranch.size() + 1;
				wcscpy_s(p2, VersionString.size() + 1, VersionString.c_str());
				SendMessageW(hWnd, UupAssistantMsg::DriverDlg_SystemEntry, reinterpret_cast<WPARAM>(p.get()), size * 2);
			}
		}
		VirtualFreeEx(GetCurrentProcess(), g_pHostContext, 0, MEM_RELEASE);
		PostMessageW(hWnd, UupAssistantMsg::DriverDlg_SystemScanCompleted, 0, 0);
		return 0;
	}

	UIFrameworkInit();
	wimlib_global_init(0);
	CoInitialize(nullptr);

	SessionContext GlobalContext = { .bAdvancedOptionsAvaliable = true };
	std::unique_ptr<Window> pWindow(new DirSelection(GlobalContext));
	pWindow->ShowWindow(nCmdShow == SW_SHOWMAXIMIZED ? SW_SHOWNORMAL : nCmdShow);
	pWindow->UpdateWindow();

	auto Cleanup = []()
		{
			wimlib_global_cleanup();
			UIFrameworkUninit();
			// CoUninitialize();
			if (g_pHostContext)
			{
				CloseHandle(g_pHostContext->hParent);
				VirtualFreeEx(GetCurrentProcess(), g_pHostContext, 0, MEM_RELEASE);
			}
		};

	enum class MainFlow : int
	{
		CreateImage = 1,
		Upgrade = 2,
		Install = 3
	};

	switch (static_cast<MainFlow>(EnterMessageLoop()))
	{
	case MainFlow::CreateImage:
		pWindow.reset(new CreateImageWizard(GlobalContext));
		pWindow->ShowWindow(SW_SHOWNORMAL);
		EnterMessageLoop();
		break;
	case MainFlow::Upgrade:
		AdjustPrivileges({ SE_TAKE_OWNERSHIP_NAME });
		pWindow.reset(new InPlaceSetupWizard(GlobalContext));
		pWindow->ShowWindow(SW_SHOWNORMAL);
		{
			HHOOK hHook = nullptr;
			HMODULE hModule = nullptr;
			{
				EnterMessageLoop();
				if (!static_cast<InPlaceSetupWizard*>(pWindow.get())->hProcess)
					break;
				HANDLE hFile = static_cast<InPlaceSetupWizard*>(pWindow.get())->hFile;
				if (hFile == INVALID_HANDLE_VALUE)
				{
					DWORD idProcess = GetProcessId(static_cast<InPlaceSetupWizard*>(pWindow.get())->hProcess);
					CloseHandle(static_cast<InPlaceSetupWizard*>(pWindow.get())->hProcess);
					pWindow.reset();
					const auto action = HostAction::WaitForProcess;
					WriteProcessMemory(g_pHostContext->hParent, reinterpret_cast<LPVOID>(g_pHostContext->lParam), &action, sizeof(action), nullptr);
					Cleanup();
					return idProcess;
				}
				SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);
				DWORD dwSize = GetFileSize(hFile, nullptr);
				MyUniquePtr<WCHAR> pEventName = dwSize / 2 + 1;
				pEventName[dwSize / 2] = 0;
				ReadFile(hFile, pEventName, dwSize, nullptr, nullptr);
				HANDLE hObjects[2] = { CreateEventW(nullptr, FALSE, FALSE, pEventName), static_cast<InPlaceSetupWizard*>(pWindow.get())->hProcess };
				SetProcessEfficiencyMode(true);
				if (WaitForMultipleObjects(2, hObjects, FALSE, INFINITE) - WAIT_OBJECT_0 == 0)
				{
					SetProcessEfficiencyMode(false);
					pWindow.reset(new UpgradeProgress(GlobalContext, hObjects[0]));
					HWND hWnd = pWindow->GetHandle();
					WriteFile(hFile, &hWnd, sizeof(hWnd), nullptr, nullptr);
					FlushFileBuffers(hFile);
					CloseHandle(hFile);

					do if (SetCurrentDirectoryW(L"Panther"))
					{
						hFile = CreateFileW(L"windlp.state.xml", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
						SetCurrentDirectoryW(L"..");
						if (hFile != INVALID_HANDLE_VALUE)
						{
							DWORD dwSize = GetFileSize(hFile, nullptr);
							string Xml;
							if (!ReadText(hFile, Xml, dwSize))
								break;

							rapidxml::xml_document<> doc;
							try
							{
								doc.parse<0>(const_cast<char*>(Xml.c_str()));

								auto pRoot = doc.first_node("WINDLP");
								if (!pRoot)
									break;

								for (auto pTask = pRoot->first_node("TASK"); pTask; pTask = pTask->next_sibling("TASK"))
									for (auto pAction = pTask->first_node("ACTION"); pAction; pAction = pAction->next_sibling("ACTION"))
									{
										auto pActionName = pAction->first_node("ActionName");
										if (pActionName && strcmp(pActionName->value(), "Summary") == 0)
										{
											auto pSelectedMigChoice = pAction->first_node("SelectedMigChoice");
											auto& Type = reinterpret_cast<UpgradeProgress*>(pWindow.get())->InstallationType;
											if (pSelectedMigChoice && pSelectedMigChoice->value_size() == 1)
											{
												switch (pSelectedMigChoice->value()[0])
												{
												case '8':
													Type = UpgradeProgress::Upgrade;
													break;
												case '1':
													Type = UpgradeProgress::CleanInstall;
													break;
												case '2':
													Type = UpgradeProgress::DataOnly;
													break;
												}
												throw Type;
											}
										}
									}
							}
							catch (...) {}
						}
					}while (false);

					thread([](HANDLE hProcess, HWND hWnd)
						{
							WaitForSingleObject(hProcess, INFINITE);
							DWORD dwExitCode;
							GetExitCodeProcess(hProcess, &dwExitCode);
							CloseHandle(hProcess);
							PostMessageW(hWnd, UupAssistantMsg::UpgradeProgress_ProcessExited, dwExitCode, 0);
						}, hObjects[1], hWnd).detach();

					static_cast<UpgradeProgress*>(pWindow.get())->ResetOwner();
					pWindow->ShowWindow(SW_SHOWNORMAL);
					pWindow->UpdateWindow();
					SetEvent(hObjects[0]);
				}
				else
				{
					SetProcessEfficiencyMode(false);
					CloseHandle(hFile);
					CloseHandle(hObjects[0]);
					CloseHandle(hObjects[1]);

					WCHAR szPath[MAX_PATH];
					GetSystemDirectoryW(szPath, MAX_PATH);
					SetCurrentDirectoryW(szPath);
					szPath[3] = 0;
					wcscat_s(szPath, MAX_PATH, L"$WINDOWS.~BT");

					AdjustPrivileges({ SE_TAKE_OWNERSHIP_NAME });
					ForceDeleteDirectory(szPath);
					pWindow = nullptr;
					break;
				}
			}
			EnterMessageLoop();
		}
		break;
	case MainFlow::Install:
		pWindow.reset(new InstallationWizard(GlobalContext));
		EnterMessageLoop();
		if (static_cast<InstallationWizard*>(pWindow.get())->State == InstallationWizard::Done)
		{
			pWindow.reset(new InstallationProgress(static_cast<InstallationWizard*>(pWindow.get())));
			EnterMessageLoop();
		}
		break;
	}

	pWindow.reset();
	Cleanup();
	return 0;
}
