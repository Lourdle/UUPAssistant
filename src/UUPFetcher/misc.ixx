module;
#include "pch.h"
#include "framework.h"

#include <ShlObj_core.h>
#include <string>
#include <filesystem>

export module Misc;

using namespace Lourdle::UIFramework;
using namespace std;

export struct Exception
{
	Exception(DWORD dwErrCode) : dwSysErrCode(dwErrCode) {}
	DWORD dwSysErrCode;
};

export ULONGLONG Random64()
{
	return ULONGLONG(USHORT(Random())) << 48 | ULONGLONG(USHORT(Random())) << 32 | ULONGLONG(USHORT(Random())) << 16 | ULONGLONG(USHORT(Random()));
}

export String GetAppDataPath()
{
	PWSTR path = nullptr;
	SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path);
	String result = L"\\\\?\\";
	result += path;
	CoTaskMemFree(path);
	result += L"\\Lourdle\\UUPFetcher";
	return result;
}

export bool CreateDirectoryRecursive(PCWSTR path)
{
	return std::filesystem::create_directories(path);
}

export bool RemoveDirectoryRecursive(PCWSTR path)
{
	std::error_code ec;
	std::filesystem::remove_all(path, ec);
	return !ec;
}

export bool MoveDirectory(PCWSTR from, PCWSTR to)
{
	if (DWORD dwAttrib = GetFileAttributesW(to); dwAttrib == INVALID_FILE_ATTRIBUTES)
		if (MoveFileExW(from, to, MOVEFILE_COPY_ALLOWED))
			return true;
		else if (!CreateDirectoryW(to, nullptr))
			return false;
		else if (!(dwAttrib & FILE_ATTRIBUTE_DIRECTORY))
		{
			SetLastError(ERROR_DIRECTORY);
			return false;
		}

	std::error_code ec;
	std::filesystem::path source(from);
	std::filesystem::path destination(to);
	auto iter = std::filesystem::recursive_directory_iterator(source, ec);
	if (ec) return false;
	for (const auto& entry : iter)
	{
		auto destPath = destination / std::filesystem::relative(entry.path(), source);
		if (entry.is_directory())
		{
			if (!std::filesystem::exists(destPath)
				&& !std::filesystem::create_directories(destPath, ec))
				return false;
		}
		else if (entry.is_regular_file())
		{
			SetFileAttributesW(destPath.c_str(), FILE_ATTRIBUTE_NORMAL);
			if (!MoveFileExW(entry.path().c_str(), destPath.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING))
				return false;
		}
	}
	return std::filesystem::remove_all(source) != 0;
}

// RAII wrapper for HANDLE
export struct WinHandle
{
	HANDLE m_hFile;

	WinHandle(HANDLE hFile = nullptr) : m_hFile(hFile) {}
	~WinHandle() { Close(); }

	// Disable copy to prevent double-close
	WinHandle(const WinHandle&) = delete;
	WinHandle& operator=(const WinHandle&) = delete;

	// Enable move
	WinHandle(WinHandle&& other) noexcept : m_hFile(other.m_hFile) {
		other.m_hFile = nullptr;
	}

	WinHandle& operator=(WinHandle&& other) noexcept {
		if (this != &other) {
			Close();
			m_hFile = other.m_hFile;
			other.m_hFile = nullptr;
		}
		return *this;
	}

	operator HANDLE() const { return m_hFile; }

	// Helper to check validity
	bool IsValid() const { return m_hFile && m_hFile != INVALID_HANDLE_VALUE; }

	void Close() {
		if (IsValid()) {
			CloseHandle(m_hFile);
			m_hFile = nullptr;
		}
	}
};
