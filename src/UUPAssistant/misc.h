#pragma once

#ifndef MISC_H
#define MISC_H

#include <Windows.h>
#include <ShObjIdl.h>
#include <Lourdle.UIFramework.h>

#include <type_traits>
#include <string>
#include <memory>

#include "../common/common.h"


Lourdle::UIFramework::String GUID2String(const GUID& guid);

bool DeleteDirectory(PCWSTR pPath);
bool ForceDeleteDirectory(PCWSTR pPath);

std::wstring GetVersionValue(PCWSTR pSoftwareKeyName, PCWSTR pValueName);

struct VersionStruct
{
	DWORD dwMajor;
	DWORD dwMinor;
	DWORD dwBuild;
	DWORD dwSpBuild;

	auto operator<=>(const VersionStruct&) const = default;
};

bool GetFileVersion(PCWSTR pszFilePath, VersionStruct& version);
void GetNtKernelVersion(VersionStruct& version);
bool ParseVersionString(PCWSTR pszVersion, VersionStruct& version);


DWORD GetFileAttributesByHandle(HANDLE hFile);

Lourdle::UIFramework::String GetFinalPathName(HANDLE hFile);


typedef bool (*CAB_EXPANSION_PROC)(bool Open, PCWSTR pszFileName, USHORT cTotalFileCount, HANDLE& hFile, PVOID pvData);

bool ExpandCabFile(PCWSTR pCabFile, PCWSTR pDestDir, CAB_EXPANSION_PROC pfnExpansionProc, PVOID pvData);
bool ExpandCabFile(PVOID pvCabData, DWORD cbData, PCWSTR pDestDir, CAB_EXPANSION_PROC pfnExpansionProc = nullptr, PVOID pvData = nullptr);

template<class lambda>
bool ExpandCabFile(PCWSTR pCabFile, PCWSTR pDestDir, lambda pfnExpansionProc)
{
	return ExpandCabFile(pCabFile, pDestDir, [](bool Open, PCWSTR pszFileName, USHORT cTotalFileCount, HANDLE& hFile, PVOID pvData) -> bool
		{
			return reinterpret_cast<lambda*>(pvData)->operator()(Open, pszFileName, cTotalFileCount, hFile);
		}, &pfnExpansionProc);
}

bool ExpandPSF(PCWSTR pszPsfFile, PCWSTR pszXmlFile, PCWSTR pszDestDir);


class FileCreator
{
	struct fc_ctx;
	std::shared_ptr<fc_ctx> ctx;
public:
	FileCreator(PCWSTR pszDir);
	FileCreator(const FileCreator&) = default;
	HANDLE Create(PCWSTR pszFileName);
	operator bool();
};


bool ReadText(HANDLE hFile, std::string& text, DWORD n);

void KillChildren(HANDLE hProcess = GetCurrentProcess());

bool AdjustPrivileges(std::initializer_list<PCTSTR> NameList);

Lourdle::UIFramework::String GetPartitionFsPath(ULONG DiskNumber, ULONG Number);
Lourdle::UIFramework::String GetPartitionFsPath(PCWSTR pDiskName, ULONG Number);

Lourdle::UIFramework::String ResStrFormat(UINT uResID, ...);

bool WriteFileResourceToFile(PCWSTR pszFileName, UINT uResID);


class RegistryHive
{
public:
	RegistryHive() = default;
	RegistryHive(const RegistryHive&) = delete;
	RegistryHive(PCWSTR pszHivePath, bool bOpenKey);
	~RegistryHive();

	operator bool() const;

	operator HKEY() const;

	operator PCWSTR() const;

	void Close();

	bool Reset(PCWSTR pszHivePath, bool bOpenKey);

private:
	HKEY m_hKey;
	std::wstring m_HiveKey;
};


class TempDir
{
	struct td_ctx;
	std::unique_ptr<td_ctx> ctx;
public:
	TempDir(PCWSTR Path);
	~TempDir();

	operator PCWSTR() const;
	std::wstring operator/(PCWSTR SubPath) const;
	std::wstring operator/(const std::wstring& SubPath) const
	{
		return *this / SubPath.c_str();
	}
};


#if !defined( _RELSRL) && !defined(_DEBUG)
#define _GetDpiForWindow GetDpiForWindow
#else
extern decltype(::GetDpiForWindow)* _GetDpiForWindow;
#endif

#endif // MISC_H
