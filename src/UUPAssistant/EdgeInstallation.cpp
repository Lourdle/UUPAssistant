#include "pch.h"
#include "misc.h"
#include "Resources/resource.h"

#include <Windows.h>
#include <wimgapi.h>

#include <string>
#include <filesystem>

using namespace std;
namespace fs = filesystem;
using namespace Lourdle::UIFramework;

// Do not use RegCopyTreeW, because it copies the whole registry tree, including the security settings. This will break the security settings of the destination registry key and the system may become unstable.
static bool CopyRegKeyTree(HKEY hKeySrc, PCWSTR pSubKey, HKEY hKeyDest)
{
	HKEY hKey;
	LSTATUS lStatus = RegOpenKeyExW(hKeySrc, pSubKey, 0, KEY_ALL_ACCESS, &hKey);
	if (lStatus != ERROR_SUCCESS)
		return false;

	DWORD cbMaxSubKeyLen, cSubKeys, cValues, cbMaxValueNameLen, cbMaxValueLen;
	lStatus = RegQueryInfoKeyW(hKey, nullptr, nullptr, nullptr, &cSubKeys, &cbMaxSubKeyLen, nullptr, &cValues, &cbMaxValueNameLen, &cbMaxValueLen, nullptr, nullptr);
	if (lStatus != ERROR_SUCCESS)
	{
		RegCloseKey(hKey);
		return false;
	}

	MyUniqueBuffer<PBYTE> pValue = cbMaxValueLen;
	MyUniquePtr<WCHAR> pValueName = cbMaxValueNameLen + 1;
	for (DWORD i = 0; i != cValues; ++i)
	{
		DWORD cbValue = cbMaxValueLen;
		DWORD cbValueName = cbMaxValueNameLen + 1;
		DWORD dwType;
		lStatus = RegEnumValueW(hKey, i, pValueName, &cbValueName, nullptr, &dwType, pValue, &cbValue);
		if (lStatus != ERROR_SUCCESS)
			goto failure1;
		lStatus = RegSetValueExW(hKeyDest, pValueName, 0, dwType, pValue, cbValue);
		if (lStatus != ERROR_SUCCESS)
		{
		failure1:
			RegCloseKey(hKey);
			SetLastError(lStatus);
			return false;
		}
	}

	MyUniquePtr<WCHAR> pKeyName = cbMaxSubKeyLen + 1;
	for (DWORD i = 0; i != cSubKeys; ++i)
	{
		DWORD cbKeyName = cbMaxSubKeyLen + 1;
		HKEY hSubKey;
		lStatus = RegEnumKeyExW(hKey, i, pKeyName, &cbKeyName, nullptr, nullptr, nullptr, nullptr);
		if (lStatus != ERROR_SUCCESS)
			goto failure2;
		lStatus = RegCreateKeyExW(hKeyDest, pKeyName, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &hSubKey, nullptr);
		if (lStatus != ERROR_SUCCESS)
			goto failure2;

		if (!CopyRegKeyTree(hKey, pKeyName, hSubKey))
		{
			lStatus = GetLastError();
		failure2:
			RegCloseKey(hKey);
			return false;
		}
		RegCloseKey(hSubKey);
	}

	RegCloseKey(hKey);
	return true;
}

static bool ApplyEdgeComponentRegSettings(HKEY hSoftwareKey, HKEY hSystemKey, const wstring& BasePath, PCWSTR pszComponent, PCWSTR pszTempDir)
{
	fs::path HivePath = BasePath;
	HivePath /= pszComponent;
	HivePath /= pszComponent;
	HivePath.replace_extension(L".dat");

	TempDir tmpDir(pszTempDir);
	auto TempHivePath = tmpDir / HivePath.filename();
	if (!MoveFileExW(HivePath.c_str(), TempHivePath.c_str(), MOVEFILE_COPY_ALLOWED))
		return false;

	HKEY hKey;
	LSTATUS l = RegLoadAppKeyW(TempHivePath.c_str(), &hKey, KEY_ALL_ACCESS | ACCESS_SYSTEM_SECURITY, 0, 0);
	if (l != ERROR_SUCCESS)
	{
		SetLastError(l);
		return false;
	}

	CopyRegKeyTree(hKey, L"REGISTRY\\MACHINE\\SOFTWARE", hSoftwareKey);

	HKEY hSystemKey2;
	if (RegOpenKeyExW(hKey, L"REGISTRY\\MACHINE\\SYSTEM", 0, KEY_ALL_ACCESS, &hSystemKey2) == ERROR_SUCCESS)
	{
		RegRenameKey(hSystemKey2, L"CurrentControlSet", L"ControlSet001");
		CopyRegKeyTree(hSystemKey2, nullptr, hSystemKey);
		RegCloseKey(hSystemKey2);
	}

	RegCloseKey(hKey);
	return true;
}

bool InstallMicrosoftEdge(PCWSTR pszEdgeWim, PCWSTR pszSystemDrive, PCWSTR pszTempDir, WORD wSystemArch)
{
	HANDLE hWim = WIMCreateFile(pszEdgeWim, WIM_GENERIC_READ, WIM_OPEN_EXISTING, WIM_FLAG_ALLOW_LZMS, 0, nullptr);
	if (!hWim)
		return false;
	if (!WIMSetTemporaryPath(hWim, pszTempDir))
	{
		WIMCloseHandle(hWim);
		return false;
	}
	HANDLE hImage = WIMLoadImage(hWim, 1);
	if (!hImage)
	{
		WIMCloseHandle(hWim);
		return false;
	}

	wstring ProgramFiles = L"Program Files";
	if (wSystemArch != PROCESSOR_ARCHITECTURE_INTEL)
		ProgramFiles += L" (x86)";

	fs::path Path = pszSystemDrive;
	Path /= ProgramFiles;
	Path /= L"Microsoft";
	if (!fs::create_directories(Path))
	{
		WIMCloseHandle(hImage);
		WIMCloseHandle(hWim);
		return false;
	}

	if (!WIMApplyImage(hImage, Path.c_str(), 0))
	{
		DWORD dwError = GetLastError();
		WIMCloseHandle(hImage);
		WIMCloseHandle(hWim);
		DeleteDirectory(Path.c_str());
		SetLastError(dwError);
		return false;
	}

	WIMCloseHandle(hImage);
	WIMCloseHandle(hWim);

	fs::path RegPath = pszSystemDrive;
	RegPath /= L"Windows\\System32\\config\\SYSTEM";

	RegistryHive hSystemKey(RegPath.c_str(), true);
	RegPath = RegPath.parent_path() / L"SOFTWARE";
	RegistryHive hSoftwareKey(RegPath.c_str(), true);

	DWORD dwErrCode = ERROR_SUCCESS;
	if (!ApplyEdgeComponentRegSettings(hSoftwareKey, hSystemKey, Path, L"Edge", pszTempDir)
		|| !ApplyEdgeComponentRegSettings(hSoftwareKey, hSystemKey, Path, L"EdgeUpdate", pszTempDir)
		|| !ApplyEdgeComponentRegSettings(hSoftwareKey, hSystemKey, Path, L"EdgeWebView", pszTempDir))
		dwErrCode = GetLastError();


	if (dwErrCode != ERROR_SUCCESS)
	{
		DeleteDirectory(Path.c_str());
		SetLastError(dwErrCode);
		return false;
	}
	return true;
}

static bool GetVersion(HANDLE hImage, PCWSTR pszAppName, wstring& refVersionString)
{
	wstring Path = pszAppName;
	Path += L"\\Application\\*";
	struct WFD : WIM_FIND_DATA
	{
		BYTE Reserved[32];
	}wfd;
	HANDLE hFind = WIMFindFirstImageFile(hImage, Path.c_str(), &wfd);
	if (!hFind)
		return false;
	do
	{
		VersionStruct version;
		if (ParseVersionString(wfd.cFileName, version))
		{
			refVersionString = wfd.cFileName;
			WIMCloseHandle(hFind);
			return true;
		}
	} while (WIMFindNextImageFile(hFind, &wfd));
	WIMCloseHandle(hFind);

	SetLastError(ERROR_FILE_NOT_FOUND);
	return false;
}

static bool GetVersion(HKEY hUninstallKey, PCWSTR pszAppName, wstring& refVersionString)
{
	HKEY hKey;
	if (RegOpenKeyExW(hUninstallKey, pszAppName, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
		return false;
	DWORD dwSize;
	if (RegQueryValueExW(hKey, L"Version", nullptr, nullptr, nullptr, &dwSize) != ERROR_SUCCESS)
	{
		RegCloseKey(hKey);
		return false;
	}
	refVersionString.resize(dwSize / sizeof(WCHAR));
	RegQueryValueExW(hKey, L"Version", nullptr, nullptr, reinterpret_cast<PBYTE>(&refVersionString[0]), &dwSize);
	RegCloseKey(hKey);
	return true;
}

bool CheckWhetherNeedToInstallMicrosoftEdge(PCWSTR pszEdgeWim, PCWSTR tmpdir,
	std::wstring& refCurrentEdgeVersion, std::wstring& refCurrentWebView2Version,
	std::wstring& refImageEdgeVersion, std::wstring& refImageWebView2Version
)
{
	HANDLE hWim = WIMCreateFile(pszEdgeWim, WIM_GENERIC_READ, WIM_OPEN_EXISTING, WIM_FLAG_ALLOW_LZMS, 0, nullptr);
	if (!hWim)
		return false;
	if (!WIMSetTemporaryPath(hWim, tmpdir))
	{
		WIMCloseHandle(hWim);
		return false;
	}
	HANDLE hImage = WIMLoadImage(hWim, 1);
	if (!hImage)
	{
		WIMCloseHandle(hWim);
		return false;
	}

	if (!GetVersion(hImage, L"Edge", refImageEdgeVersion)
		|| !GetVersion(hImage, L"EdgeWebView", refImageWebView2Version))
	{
		DWORD dwError = GetLastError();
		WIMCloseHandle(hImage);
		WIMCloseHandle(hWim);
		SetLastError(dwError);
		return false;
	}

	WIMCloseHandle(hImage);
	WIMCloseHandle(hWim);

	HKEY hUninstallKey;
	LSTATUS l = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_READ, &hUninstallKey);
	if (l != ERROR_SUCCESS)
	{
		WIMCloseHandle(hImage);
		WIMCloseHandle(hWim);
		SetLastError(l);
		return false;
	}

	if (!(GetVersion(hUninstallKey, L"Microsoft Edge", refCurrentEdgeVersion)
		| GetVersion(hUninstallKey, L"Microsoft EdgeWebView", refCurrentWebView2Version)))
	{
		if (refCurrentEdgeVersion.empty())
			refCurrentEdgeVersion = GetString(String_NotInstalled);
		if (refCurrentWebView2Version.empty())
			refCurrentWebView2Version = GetString(String_NotInstalled);
		RegCloseKey(hUninstallKey);
		return true;
	}

	RegCloseKey(hUninstallKey);

	VersionStruct Target, Current;
	ParseVersionString(refImageEdgeVersion.c_str(), Target);
	if (!ParseVersionString(refCurrentEdgeVersion.c_str(), Current)
		|| Current < Target)
		return true;

	ParseVersionString(refImageWebView2Version.c_str(), Target);
	if (!ParseVersionString(refCurrentWebView2Version.c_str(), Current)
		|| Current < Target)
		return true;
	return false;
}
