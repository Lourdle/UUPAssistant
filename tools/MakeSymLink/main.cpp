#include <Windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <filesystem>

constexpr const WCHAR GlobalRootPrefix[] = L"\\\\?\\GLOBALROOT";

namespace fs = std::filesystem;

struct HandleDeleter
{
    void operator()(HANDLE h) const
    {
        if (h != INVALID_HANDLE_VALUE && h != NULL)
            CloseHandle(h);
    }
};

using UniqueHandle = std::unique_ptr<void, HandleDeleter>;

static std::wstring GetFinalPathName(HANDLE hFile)
{
    DWORD dwSize = GetFinalPathNameByHandleW(hFile, nullptr, 0, VOLUME_NAME_NT);
    if (dwSize == 0)
        return {};

    constexpr size_t GlobalRootLen = ARRAYSIZE(GlobalRootPrefix) - 1;
    std::wstring buffer(dwSize + GlobalRootLen, 0);
    memcpy(&buffer[0], GlobalRootPrefix, GlobalRootLen * sizeof(WCHAR));
    dwSize = GetFinalPathNameByHandleW(hFile, &buffer[GlobalRootLen], dwSize, VOLUME_NAME_NT);
    if (dwSize == 0)
        return {};

    return buffer;
}

static std::wstring ResolvePath(const std::wstring& path)
{
    UniqueHandle hFile(CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    
    if (hFile.get() == INVALID_HANDLE_VALUE)
        return {};

    return GetFinalPathName(hFile.get());
}

static bool CheckLink(const std::wstring& link, const std::wstring& target)
{
    if (!fs::exists(link))
    {
        std::wcout << link << L" not found\n";
        return false;
    }

    std::wstring LinkFinalPath = ResolvePath(link);
    if (LinkFinalPath.empty())
    {
        std::wcout << L"Link broken. Delete it\n";
        fs::remove(link);
        return false;
    }

    std::wcout << L"Source: " << LinkFinalPath << L"\n";

    std::wstring TargetFinalPath = ResolvePath(target);
    if (TargetFinalPath.empty())
    {
        std::wcerr << L"Target not found\n";
        return true;
    }
    std::wcout << L"Target: " << TargetFinalPath << L"\n";

    bool bResult = (LinkFinalPath == TargetFinalPath);

    if (!bResult)
    {
        std::wcout << link << L" -> " << target << L". Delete link\n";
        fs::remove(link);
    }
    return bResult;
}

int wmain(int argc, wchar_t* argv[])
{
    if (argc != 3)
    {
        std::wcerr << L"Usage: " << argv[0] << L" <Link> <Target>\n";
        return ERROR_BAD_ARGUMENTS;
    }

    std::wstring Link = argv[1];
    std::wstring Target = argv[2];

    if (!CheckLink(Link, Target))
    {
        std::wstring TargetFinalPath = ResolvePath(Target);
        if (TargetFinalPath.empty())
        {
            std::wcerr << L"ERROR: " << Target << L": CreateFile failed: " << GetLastError() << L"\n";
            return GetLastError();
        }

        DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
        if (fs::is_directory(Target))
            flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;

        if (!CreateSymbolicLinkW(Link.c_str(), TargetFinalPath.c_str(), flags))
        {
            if (!CopyFileW(TargetFinalPath.c_str(), Link.c_str(), FALSE))
            {
                std::wcerr << L"ERROR: " << L"Create link " << Link << L" -> " << Target << L" failed: " << GetLastError() << L"\n";
                return GetLastError();
            }
        }
        else
            std::wcout << L"Link " << Link << L" -> " << Target << L" created\n";
    }

    return 0;
}
