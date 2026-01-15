#include "pch.h"
#include "framework.h"
#include "FakeWimgAPI.h"
#include "MessageIds.h"
#include "resource.h"
#include "../common/MyRaii.h"

#include <algorithm>
#include <string>

import CheckMiniNt;

#ifdef _DEBUG
#include <iostream>

using namespace std;

#define RETURN(x) do {\
	auto result = x;\
	cout << "Result: " << result << endl;\
	if (!result)\
		cout << "Error: " << GetLastError() << endl;\
	return result;\
} while(false)

static decltype(cout.operator<<(0)) operator<<(decltype(cout) & s, PCWSTR psz)
{
	int MultiByteLen = WideCharToMultiByte(CP_ACP, 0, psz, -1, nullptr, 0, nullptr, nullptr);
	MyUniquePtr<CHAR> pMultiByte = MultiByteLen;
	WideCharToMultiByte(CP_ACP, 0, psz, -1, pMultiByte, MultiByteLen, nullptr, nullptr);
	s << pMultiByte;
	return s;
}

#define LOG(x) cout << x
#else
#define RETURN(x) return x
#define LOG(x) #x
#endif


static std::vector<HANDLE> g_ImageVector;
static std::vector<HANDLE> g_WimVector;
static std::vector<HANDLE> g_MountedImageVector;

HWND g_hWnd;
static HANDLE g_hEvent;
static HANDLE g_hProcess;

static struct
{
	BYTE Deleted : 1;
}g_Mask;

static void GetWindowsBtSourcesPath(PWSTR pszPath)
{
	GetModuleFileNameW(nullptr, pszPath, MAX_PATH);
	wcscpy_s(pszPath + 3, MAX_PATH - 3, L"$WINDOWS.~BT\\Sources\\");
}

static std::vector<HANDLE>::iterator find(std::vector<HANDLE>& v, HANDLE h)
{
	return std::find(v.begin(), v.end(), h);
}

static void UUPAssistantTerminated()
{
	MEMORY_BASIC_INFORMATION mbi;
	VirtualQuery(UUPAssistantTerminated, &mbi, sizeof(mbi));
	LPWSTR p = nullptr;
	int cchLen = LoadStringW(reinterpret_cast<HMODULE>(mbi.AllocationBase), String_UUPAssistantTerminated, reinterpret_cast<LPWSTR>(&p), 0);
	if (!cchLen)
		return;
	MyUniquePtr<WCHAR> Text = cchLen + 1;
	wcscpy_s(Text, cchLen + 1, p);
	MessageBeep(MB_ICONERROR);
	MessageBoxW(FindWindowCurrentProcess(), Text, nullptr, MB_ICONERROR);
}


#define FORWARD(f) comment(linker, "/EXPORT:" #f "=realwimgapi." #f)


extern "C"
{
	BOOL
		WINAPI
		WIMExtractImagePathByWimHandle(
			_In_ HANDLE hWim,
			_In_ DWORD dwImageIndex,
			_In_ PCWSTR pszImagePath,
			_In_ PCWSTR pszDestinationPath,
			_In_ DWORD dwExtractFlags
		);

	BOOL
		WINAPI
		FakeWIMCloseHandle(
			_In_ HANDLE hObject
		)
	{
		LOG("WIMCloseHandle Handle: " << hObject << endl);

		auto it = find(g_ImageVector, hObject);
		if (it != g_ImageVector.end())
		{
			g_ImageVector.erase(it);
			if (g_Mask.Deleted)
				return TRUE;
		}
		else
		{
			it = find(g_WimVector, hObject);
			if (it != g_WimVector.end())
			{
				g_WimVector.erase(it);
				if (g_Mask.Deleted)
					return TRUE;
			}
			else
			{
				it = find(g_MountedImageVector, hObject);
				if (it != g_MountedImageVector.end())
					g_MountedImageVector.erase(it);
			}
		}
		return WIMCloseHandle(hObject);
	}

#ifdef _DEBUG
	BOOL
		WINAPI
		FakeWIMSetReferenceFile(
			_In_ HANDLE hWim,
			_In_ PCWSTR pszPath,
			_In_ DWORD  dwFlags
		)
	{
		LOG("WIMSetReferenceFile hWim: " << hWim << " pszPath: " << pszPath << " dwFlags: " << dwFlags << endl);

		RETURN(WIMSetReferenceFile(hWim, pszPath, dwFlags));
	}
#pragma comment(linker, "/EXPORT:WIMSetReferenceFile=FakeWIMSetReferenceFile")
#else
#pragma FORWARD(WIMSetReferenceFile)
#endif // _DEBUG

	HANDLE
		WINAPI
		FakeWIMCreateFile(
			_In_      PCWSTR pszWimPath,
			_In_      DWORD  dwDesiredAccess,
			_In_      DWORD  dwCreationDisposition,
			_In_      DWORD  dwFlagsAndAttributes,
			_In_      DWORD  dwCompressionType,
			_Out_opt_ PDWORD pdwCreationResult
		)
	{
		LOG("WIMCreateFile pszWimPath: " << pszWimPath << endl);

		HANDLE hWim = WIMCreateFile(pszWimPath, dwDesiredAccess, dwCreationDisposition, dwFlagsAndAttributes, dwCompressionType, pdwCreationResult);
		if (!hWim)
			return nullptr;

		if (_wcsicmp(pszWimPath + 2, L"\\$WINDOWS.~BT\\Sources\\Install.esd") == 0)
			g_WimVector.push_back(hWim);

		RETURN(hWim);
	}


	BOOL
		WINAPI
		FakeWIMApplyImage(
			_In_     HANDLE hImage,
			_In_opt_ PCWSTR pszPath,
			_In_     DWORD  dwApplyFlags
		)
	{
		LOG("WIMApplyImage Handle: " << hImage << "; Path: " << pszPath << "; Flags: " << dwApplyFlags << endl);

		if (find(g_ImageVector, hImage) != g_ImageVector.end())
		{
			LOG("Image is form install.esd" << endl);

			WCHAR szPath[MAX_PATH];
			GetWindowsBtSourcesPath(szPath);
			wcscat_s(szPath, MAX_PATH, L"StoragedOS");
			DWORD cbData = 0;
			if (IsMiniNtBoot())
			{
				LOG("Moved StoragedOS" << endl);

				RemoveDirectoryW(pszPath);
				return MoveFileW(szPath, pszPath);
			}

			LOG("Did not move StoragedOS" << endl);
			return FALSE;
		}

		RETURN(WIMApplyImage(hImage, pszPath, dwApplyFlags));
	}

	BOOL
		WINAPI
		FakeWIMExtractImagePathByWimHandle(
			_In_ HANDLE hWim,
			_In_ DWORD dwImageIndex,
			_In_ PCWSTR pszImagePath,
			_In_ PCWSTR pszDestinationPath,
			_In_ DWORD dwExtractFlags
		)
	{
		LOG("WIMExtractImagePathByWimHandle hWim: " << hWim << " dwImageIndex: " << dwImageIndex << " pszImagePath: " <<
			pszImagePath << " pszDestinationPath: " << pszDestinationPath << " dwExtractFlags: " << dwExtractFlags << endl);

		if (find(g_WimVector, hWim) != g_WimVector.end())
			if (_wcsicmp(pszImagePath, L"\\Windows\\System32\\Recovery\\winre.wim") == 0)
			{
				WCHAR szPath[MAX_PATH];
				GetWindowsBtSourcesPath(szPath);
				wcscat_s(szPath, MAX_PATH, L".uupassistant_data_exchange");
				HANDLE hFile = CreateFileW(szPath, GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (hFile != INVALID_HANDLE_VALUE)
				{
					DWORD dwFileSize = GetFileSize(hFile, nullptr);
					MyUniquePtr<WCHAR>  pEventName = dwFileSize / 2 + 1;
					pEventName[dwFileSize / 2] = 0;
					ReadFile(hFile, pEventName, dwFileSize, nullptr, nullptr);
					g_hEvent = OpenEventW(EVENT_ALL_ACCESS, FALSE, pEventName);
					if (g_hEvent)
					{
						PulseEvent(g_hEvent);
						WaitForSingleObject(g_hEvent, INFINITE);
						ReadFile(hFile, &g_hWnd, sizeof(g_hWnd), nullptr, nullptr);
						CloseHandle(hFile);
						HookWindows();
						DWORD dwProcessId;
						GetWindowThreadProcessId(g_hWnd, &dwProcessId);
						g_hProcess = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_WRITE | SYNCHRONIZE, FALSE, dwProcessId);

						PVOID pMem = VirtualAllocEx(g_hProcess, nullptr, wcslen(pszDestinationPath) * 2 + 2, MEM_COMMIT, PAGE_READWRITE);
						WriteProcessMemory(g_hProcess, pMem, pszDestinationPath, wcslen(pszDestinationPath) * 2 + 2, nullptr);
						ResetEvent(g_hEvent);
						SendMessageW(g_hWnd, UpgradeProgress_StartExportReImg, reinterpret_cast<WPARAM>(pMem), 0);
						HANDLE hObjects[2] = { g_hEvent, g_hProcess };
						DWORD dwResult = WaitForMultipleObjects(2, hObjects, FALSE, INFINITE);
						VirtualFreeEx(g_hProcess, pMem, 0, MEM_RELEASE);
						if (dwResult - WAIT_OBJECT_0 == 1)
						{
							CloseHandle(g_hEvent);
							CloseHandle(g_hProcess);
							UUPAssistantTerminated();
							return FALSE;
						}
						if (GetFileAttributesW(pszDestinationPath) == INVALID_FILE_ATTRIBUTES)
						{
							CloseHandle(g_hEvent);
							CloseHandle(g_hProcess);
							SetLastError(ERROR_UNIDENTIFIED_ERROR);
							return FALSE;
						}
					}
					else
						CloseHandle(hFile);
				}

				GetWindowsBtSourcesPath(szPath);
				wcscat_s(szPath, L".system_installation_succeeded");
				hFile = CreateFileW(szPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (hFile == INVALID_HANDLE_VALUE)
				{
					CloseHandle(g_hEvent);
					CloseHandle(g_hProcess);
					return FALSE;
				}
				CloseHandle(hFile);

				ResetEvent(g_hEvent);
				GetWindowsBtSourcesPath(szPath);
				wcscat_s(szPath, L"StoragedOS");
				CreateDirectoryW(szPath, nullptr);
				PVOID pMem = VirtualAllocEx(g_hProcess, nullptr, wcslen(szPath) * 2 + 2, MEM_COMMIT, PAGE_READWRITE);
				WriteProcessMemory(g_hProcess, pMem, szPath, wcslen(szPath) * 2 + 2, nullptr);
				SendMessageW(g_hWnd, UpgradeProgress_StartApplyImage, reinterpret_cast<WPARAM>(pMem), 0);

				HANDLE hObjects[2] = { g_hEvent, g_hProcess };
				DWORD dwResult = WaitForMultipleObjects(2, hObjects, FALSE, INFINITE);
				if (dwResult - WAIT_OBJECT_0 == 0)
					if (GetFileAttributesW(L".system_installation_succeeded") == INVALID_FILE_ATTRIBUTES)
					{
						CloseHandle(g_hEvent);
						CloseHandle(g_hProcess);
						VirtualFreeEx(g_hProcess, pMem, 0, MEM_RELEASE);
						SetLastError(ERROR_UNIDENTIFIED_ERROR);
						return FALSE;
					}
					else
					{
						ResetEvent(g_hEvent);
						g_Mask.Deleted = true;
						for (HANDLE i : g_ImageVector)
							WIMCloseHandle(i);
						for (HANDLE i : g_WimVector)
							WIMCloseHandle(i);
						SendMessageW(g_hWnd, UpgradeProgress_CreateFakeInstallEsd, 0, 0);
						SendMessageW(g_hWnd, UpgradeProgress_StartDismAndEdge, reinterpret_cast<WPARAM>(pMem), 0);
						dwResult = WaitForMultipleObjects(2, hObjects, FALSE, INFINITE);
						if (dwResult - WAIT_OBJECT_0 == 0)
						{
							if (GetFileAttributesW(L".system_installation_succeeded") == INVALID_FILE_ATTRIBUTES)
							{
								CloseHandle(g_hEvent);
								CloseHandle(g_hProcess);
								VirtualFreeEx(g_hProcess, pMem, 0, MEM_RELEASE);
								SetLastError(ERROR_UNIDENTIFIED_ERROR);
								return FALSE;
							}
							VirtualFreeEx(g_hProcess, pMem, 0, MEM_RELEASE);
						}
						else if (dwResult - WAIT_OBJECT_0 == 1)
						{
							CloseHandle(g_hEvent);
							CloseHandle(g_hProcess);
							UUPAssistantTerminated();
							SetLastError(ERROR_UNIDENTIFIED_ERROR);
							return FALSE;
						}
					}
				else if (dwResult - WAIT_OBJECT_0 == 1)
				{
					CloseHandle(g_hEvent);
					CloseHandle(g_hProcess);
					UUPAssistantTerminated();
					SetLastError(ERROR_UNIDENTIFIED_ERROR);
					return FALSE;
				}

				return TRUE;
			}
			else if (g_Mask.Deleted)
			{
				WCHAR szPath[MAX_PATH];
				GetWindowsBtSourcesPath(szPath);
				wcscat_s(szPath, MAX_PATH, L"StoragedOS");
				wcscat_s(szPath, MAX_PATH, pszImagePath);
				return CopyFileW(szPath, pszDestinationPath, FALSE);
			}

		RETURN(WIMExtractImagePathByWimHandle(hWim, dwImageIndex, pszImagePath, pszDestinationPath, dwExtractFlags));
	}


#ifdef _DEBUG
	BOOL
		WINAPI
		FakeWIMGetImageInformation(
			_In_                                                        HANDLE hImage,
			_Outptr_result_bytebuffer_(*pcbImageInfo) _Outptr_result_z_ PVOID* ppvImageInfo,
			_Out_                                                       PDWORD pcbImageInfo
		)
	{
		cout << "WIMGetImageInformation Handle: " << hImage << endl;
		auto result = WIMGetImageInformation(hImage, ppvImageInfo, pcbImageInfo);
		cout << "Result: " << result << endl;
		if (!result)
			cout << "Error: " << GetLastError() << endl;
		else
		{
			int len = WideCharToMultiByte(CP_ACP, 0, reinterpret_cast<PCWSTR>(*ppvImageInfo) + 1, *pcbImageInfo / 2 - 1, nullptr, 0, nullptr, nullptr);
			MyUniquePtr<CHAR> p = len;
			WideCharToMultiByte(CP_ACP, 0, reinterpret_cast<PCWSTR>(*ppvImageInfo) + 1, *pcbImageInfo / 2 - 1, p, len, nullptr, nullptr);
			cout << "Image Information: ";
			cout.write(p, len) << endl;
		}
		return result;
	}
#pragma comment(linker, "/EXPORT:WIMGetImageInformation=FakeWIMGetImageInformation")
#else
#pragma FORWARD(WIMGetImageInformation)
#endif // _DEBUG

	BOOL
		WINAPI
		FakeWIMGetMountedImageHandle(
			_In_  PCWSTR  pszMountPath,
			_In_  DWORD   dwFlags,
			_Out_ PHANDLE phWimHandle,
			_Out_ PHANDLE phImageHandle
		)
	{
		if (_wcsicmp(pszMountPath + 1, L":\\$WINDOWS.~BT\\Sources\\SafeOS\\SafeOS.Mount") == 0)
		{
			if (!WIMGetMountedImageHandle(pszMountPath, dwFlags, phWimHandle, phImageHandle))
				return FALSE;

			g_MountedImageVector.push_back(*phImageHandle);
			return TRUE;
		}
		return WIMGetMountedImageHandle(pszMountPath, dwFlags, phWimHandle, phImageHandle);
	}


	HANDLE
		WINAPI
		FakeWIMLoadImage(
			_In_ HANDLE hWim,
			_In_ DWORD  dwImageIndex
		)
	{
		if (find(g_WimVector, hWim) != g_WimVector.end())
		{
			HANDLE hImage = WIMLoadImage(hWim, 1);
			if (!hImage)
				return nullptr;
			g_ImageVector.push_back(hImage);
			return hImage;
		}
		else
			return WIMLoadImage(hWim, dwImageIndex);
	}


	BOOL
		WINAPI
		FakeWIMMountImageHandle(
			_In_ HANDLE hImage,
			_In_ PCWSTR pszMountPath,
			_In_ DWORD  dwMountFlags
		)
	{
		if (_wcsicmp(pszMountPath + 1, L":\\$WINDOWS.~BT\\Sources\\SafeOS\\SafeOS.Mount") == 0)
		{
			if (find(g_MountedImageVector, hImage) == g_MountedImageVector.end())
				g_MountedImageVector.push_back(hImage);
			PostMessageW(g_hWnd, UpgradeProgress_ConfiguringSafeOS, 0, 0);
			if (WIMMountImageHandle(hImage, pszMountPath, dwMountFlags))
			{
				PVOID pMem = VirtualAllocEx(g_hProcess, nullptr, wcslen(pszMountPath) * 2 + 2, MEM_COMMIT, PAGE_READWRITE);
				WriteProcessMemory(g_hProcess, pMem, pszMountPath, wcslen(pszMountPath) * 2 + 2, nullptr);
				ResetEvent(g_hEvent);
				PostMessageW(g_hWnd, UpgradeProgress_StartSafeOSDU, reinterpret_cast<WPARAM>(pMem), 0);
				HANDLE hObjects[2] = { g_hEvent, g_hProcess };
				DWORD dwResult = WaitForMultipleObjects(2, hObjects, FALSE, INFINITE);
				VirtualFreeEx(g_hProcess, pMem, 0, MEM_RELEASE);
				if (dwResult - WAIT_OBJECT_0 == 1)
				{
					CloseHandle(g_hEvent);
					CloseHandle(g_hProcess);
					UUPAssistantTerminated();
					return FALSE;
				}
				return TRUE;
			}
			else
				return FALSE;
		}
		return WIMMountImageHandle(hImage, pszMountPath, dwMountFlags);
	}


	BOOL
		WINAPI
		FakeWIMUnmountImageHandle(
			_In_ HANDLE hImage,
			_In_ DWORD  dwUnmountFlags
		)
	{
		if (find(g_MountedImageVector, hImage) == g_MountedImageVector.end())
			return WIMUnmountImageHandle(hImage, dwUnmountFlags);
		else if (WIMUnmountImageHandle(hImage, dwUnmountFlags))
		{
			PostMessageW(g_hWnd, UpgradeProgress_ConfigureSafeOSSucceeded, 0, 0);
			return TRUE;
		}
		else
			return FALSE;
	}
}
