#include "pch.h"
#include "misc.h"

#include <filesystem>
#include <TlHelp32.h>

#include <thread>
#include <vector>

using namespace std;
using namespace Lourdle::UIFramework;


std::wstring GetVersionValue(PCWSTR pSoftwareKeyName, PCWSTR pValueName)
{
	DWORD cbData = 0;
	wstring SubKey = pSoftwareKeyName;
	SubKey += L"\\Microsoft\\Windows NT\\CurrentVersion";

	LSTATUS status = RegGetValueW(HKEY_LOCAL_MACHINE, SubKey.c_str(), pValueName, RRF_RT_REG_SZ, nullptr, nullptr, &cbData);
	if (status != ERROR_SUCCESS || cbData == 0)
		return {};

	wstring Value;
	Value.resize(cbData / sizeof(WCHAR) - 1); // Resize to hold characters including null terminator

	status = RegGetValueW(HKEY_LOCAL_MACHINE, SubKey.c_str(), pValueName, RRF_RT_REG_SZ, nullptr, Value.data(), &cbData);
	if (status != ERROR_SUCCESS)
		return {};

	// Remove null terminator if present (RegGetValueW includes it in cbData for REG_SZ)
	if (!Value.empty() && Value.back() == L'\0')
		Value.pop_back();

	return Value;
}

bool GetFileVersion(PCWSTR pszFilePath, VersionStruct& version)
{
	DWORD cbData = GetFileVersionInfoSizeW(pszFilePath, nullptr);
	if (cbData == 0)
		return false;
	MyUniqueBuffer<PVOID> data(cbData);
	GetFileVersionInfoW(pszFilePath, 0, cbData, data);
	VS_FIXEDFILEINFO* pFixedFileInfo;
	UINT uLength;
	if (!VerQueryValueW(data, L"\\", reinterpret_cast<LPVOID*>(&pFixedFileInfo), &uLength))
		return false;

	version = VersionStruct{
		.dwMajor = HIWORD(pFixedFileInfo->dwFileVersionMS),
		.dwMinor = LOWORD(pFixedFileInfo->dwFileVersionMS),
		.dwBuild = HIWORD(pFixedFileInfo->dwFileVersionLS),
		.dwSpBuild = LOWORD(pFixedFileInfo->dwFileVersionLS)
	};
	return true;
}

void GetNtKernelVersion(VersionStruct& version)
{
	WCHAR szKernelPath[MAX_PATH];
	GetSystemDirectoryW(szKernelPath, MAX_PATH);
	wcscat_s(szKernelPath, L"\\ntoskrnl.exe");
	GetFileVersion(szKernelPath, version);
}

DWORD GetFileAttributesByHandle(HANDLE hFile)
{
	BY_HANDLE_FILE_INFORMATION bhfi;
	GetFileInformationByHandle(hFile, &bhfi);
	return bhfi.dwFileAttributes;
}

String GetFinalPathName(HANDLE hFile)
{
	String name;
	DWORD dwSize = GetFinalPathNameByHandleW(hFile, nullptr, 0, VOLUME_NAME_DOS);
	if (dwSize != 0)
	{
		name.Resize(dwSize);
		GetFinalPathNameByHandleW(hFile, name.GetPointer(), dwSize, VOLUME_NAME_DOS);
	}
	return name;
}

bool ParseVersionString(PCWSTR pszVersion, VersionStruct& version)
{
	version = { 0, 0, 0, 0 };
	return swscanf_s(pszVersion, L"%lu.%lu.%lu.%lu", 
		&version.dwMajor, &version.dwMinor, &version.dwBuild, &version.dwSpBuild) == 4;
}

String GUID2String(const GUID& guid)
{
	LPOLESTR str;
	StringFromCLSID(guid, &str);
	String Str = str;
	CoTaskMemFree(str);
	return Str;
}

struct FileCreator::fc_ctx : filesystem::path
{
	using filesystem::path::path;
};

FileCreator::FileCreator(PCWSTR outdir)
{
	if (!outdir) outdir = L".";

	HANDLE hDir = CreateFileW(outdir, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (hDir == INVALID_HANDLE_VALUE) return;
	String BasePath = GetFinalPathName(hDir);
	CloseHandle(hDir);

	ctx.reset(new fc_ctx(BasePath.GetPointer()));
}

HANDLE FileCreator::Create(PCWSTR name)
{
	auto Path = *ctx / name;
	auto DirPath = Path.parent_path();
	HANDLE hFile = INVALID_HANDLE_VALUE;
	if (filesystem::create_directories(DirPath)
		|| GetLastError() == ERROR_ALREADY_EXISTS)
		hFile = CreateFileW(Path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
	return hFile;
}

FileCreator::operator bool()
{
	return ctx.get();
}

bool AdjustPrivileges(initializer_list<PCTSTR> NameList)
{
	if (NameList.size() == 0)
		return true;

	MyUniquePtr<LUID_AND_ATTRIBUTES> privileges(NameList.size());
	DWORD Count = 0;
	for (auto name : NameList)
	{
		auto& [luid, attr] = privileges[Count++];
		if (!LookupPrivilegeValueW(nullptr, name, &luid))
			return false;
		attr = SE_PRIVILEGE_ENABLED;
	}

	const auto bufferSize = sizeof(TOKEN_PRIVILEGES) + sizeof(LUID_AND_ATTRIBUTES) * (Count - 1);
	MyUniqueBuffer<PTOKEN_PRIVILEGES> ptp(bufferSize);
	ptp->PrivilegeCount = Count;
	for (DWORD i = 0; i < Count; ++i)
		ptp->Privileges[i] = privileges[i];

	HANDLE hToken = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken))
		return false;

	BOOL result = AdjustTokenPrivileges(hToken, FALSE, ptp, 0, nullptr, nullptr);
	CloseHandle(hToken);
	return result;
}

String ResStrFormat(UINT uResID, ...)
{
	String format = GetString(uResID);

	va_list args;
	va_start(args, uResID);
	int size = _vscwprintf(format, args);
	if (size < 0)
	{
	failure:
		va_end(args);
		return format;
	}

	PVOID buffer = HeapAlloc(GetProcessHeap(), 0, (size + 1) * sizeof(WCHAR));
	if (!buffer)
		goto failure;

	vswprintf_s(reinterpret_cast<PWSTR>(buffer), size + 1, format, args);
	va_end(args);
	return *reinterpret_cast<String*>(&buffer);
}

void KillChildren(HANDLE hProcess)
{
	DWORD idProcess = GetProcessId(hProcess);
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, idProcess);
	if (hSnapshot == INVALID_HANDLE_VALUE)
		return;
	PROCESSENTRY32W pe{ .dwSize = sizeof(pe) };
	Process32FirstW(hSnapshot, &pe);
	do if (pe.th32ParentProcessID == idProcess)
	{
		HANDLE hChildProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
		if (hChildProcess)
		{
			KillChildren(hChildProcess);
			TerminateProcess(hChildProcess, 0);
			CloseHandle(hChildProcess);
		}
	}
	while (Process32NextW(hSnapshot, &pe));
	CloseHandle(hSnapshot);
}

bool WriteFileResourceToFile(PCWSTR pszFileName, UINT uResID)
{
	HANDLE hFile = CreateFileW(pszFileName, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	HRSRC hResInfo = FindResourceA(nullptr, MAKEINTRESOURCEA(uResID), "FILE");
	if (!hResInfo)
	{
		CloseHandle(hFile);
		return false;
	}

	HGLOBAL hResData = LoadResource(nullptr, hResInfo);
	if (!hResData)
	{
		CloseHandle(hFile);
		return false;
	}

	DWORD dwSize = SizeofResource(nullptr, hResInfo);
	if (!WriteFile(hFile, LockResource(hResData), dwSize, nullptr, nullptr))
	{
		CloseHandle(hFile);
		return false;
	}

	CloseHandle(hFile);
	return true;
}

bool ReadText(HANDLE hFile, string& text, DWORD n)
{
	if (n == INVALID_FILE_SIZE)
		return false;

	if (n <= 3)
	{
		SetLastError(ERROR_BAD_FORMAT);
		return false;
	}

	MyUniqueBuffer<PSTR> pBuf(n);
	DWORD dwRead;
	if (!ReadFile(hFile, pBuf, n, &dwRead, nullptr))
		return false;
	if (dwRead != n)
	{
		SetLastError(ERROR_INCORRECT_SIZE);
		return false;
	}

	// UTF-8 BOM
	if (pBuf[0] == CHAR(0xEF) && pBuf[1] == CHAR(0xBB) && pBuf[2] == CHAR(0xBF))
	{
		const int Length = int(n - 3);
		int cwch = MultiByteToWideChar(CP_UTF8, 0, pBuf.get() + 3, Length, nullptr, 0);
		if (cwch > 0)
		{
			MyUniquePtr<WCHAR> WideText(cwch);
			MultiByteToWideChar(CP_UTF8, 0, pBuf.get() + 3, Length, WideText, cwch);
			int cch = WideCharToMultiByte(CP_ACP, 0, WideText.get(), cwch, nullptr, 0, nullptr, nullptr);
			if (cch > 0)
			{
				text.resize(cch);
				WideCharToMultiByte(CP_ACP, 0, WideText.get(), cwch, const_cast<LPSTR>(text.c_str()), cch, nullptr, nullptr);
			}
			else return false;
		}
		else return false;
	}
	// UTF-16 LE
	else if (pBuf[0] == CHAR(0xFF) && pBuf[1] == CHAR(0xFE))
	{
		const auto Length = int((n - 2) / sizeof(WCHAR));
		int cch = WideCharToMultiByte(CP_ACP, 0, reinterpret_cast<PWSTR>(pBuf.get() + 2), Length, nullptr, 0, nullptr, nullptr);
		if (cch > 0)
		{
			text.resize(cch);
			WideCharToMultiByte(CP_ACP, 0, reinterpret_cast<PWSTR>(pBuf.get() + 2), Length, const_cast<LPSTR>(text.c_str()), cch, nullptr, nullptr);
		}
		else return false;
	}
	// Treat as ANSI
	else text.assign(pBuf, n);

	return true;
}


RegistryHive::RegistryHive(PCWSTR pszHivePath, bool bOpenKey) : m_hKey(nullptr)
{
	GUID guid;
	CoCreateGuid(&guid);
	m_HiveKey = GUID2String(guid);

	auto lStatus = RegLoadKeyW(HKEY_LOCAL_MACHINE, m_HiveKey.c_str(), pszHivePath);
	if (lStatus != ERROR_SUCCESS)
	{
	failure:
		m_HiveKey.clear();
		SetLastError(lStatus);
		return;
	}

	if (bOpenKey)
	{
		lStatus = RegOpenKeyExW(HKEY_LOCAL_MACHINE, m_HiveKey.c_str(), KEY_ALL_ACCESS, 0, &m_hKey);
		if (lStatus != ERROR_SUCCESS)
			goto failure;
	}
}

RegistryHive::~RegistryHive()
{
	Close();
}

RegistryHive::operator bool() const
{
	return !m_HiveKey.empty();
}

RegistryHive::operator HKEY() const
{
	return m_hKey;
}

RegistryHive::operator PCWSTR() const
{
	return m_HiveKey.c_str();
}

void RegistryHive::Close()
{
	if (!m_HiveKey.empty())
	{
		if (m_hKey)
		{
			RegCloseKey(m_hKey);
			m_hKey = nullptr;
		}
		RegUnLoadKeyW(HKEY_LOCAL_MACHINE, m_HiveKey.c_str());
		m_HiveKey.clear();
	}
}

bool RegistryHive::Reset(PCWSTR pszHivePath, bool bOpenKey)
{
	Close();
	this->RegistryHive::RegistryHive(pszHivePath, bOpenKey);
	return *this;
}


struct TempDir::td_ctx : filesystem::path
{
	using filesystem::path::path;
};

TempDir::TempDir(PCWSTR Path) : ctx(make_unique<td_ctx>(Path))
{
	GUID guid;
	CoCreateGuid(&guid);
	*ctx /= GUID2String(guid).GetPointer();
	CreateDirectoryW(ctx->c_str(), nullptr);
}

std::wstring TempDir::operator/(PCWSTR SubPath) const
{
	return *ctx / SubPath;
}

TempDir::operator PCWSTR() const
{
	return ctx->c_str();
}

TempDir::~TempDir()
{
	DeleteDirectory(ctx->c_str());
}
