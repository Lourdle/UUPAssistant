#include <Windows.h>
#include <iostream>
#include <vector>
#include <memory>
#include <type_traits>

template <typename T, typename T1, typename T2>
constexpr T RVA2VA(T1 base, T2 rva)
{
    return reinterpret_cast<T>(reinterpret_cast<ULONG_PTR>(base) + rva);
}

struct ModuleDeleter
{
    void operator()(HMODULE h) const
    {
        if (h)
        {
            FreeLibrary(h);
        }
    }
};

using UniqueModule = std::unique_ptr<std::remove_pointer_t<HMODULE>, ModuleDeleter>;

int wmain(int argc, wchar_t* argv[])
{
    if (argc != 2)
    {
        std::wcerr << L"Usage: " << argv[0] << L" <DLL>\n";
        return 1;
    }

    // Use LOAD_LIBRARY_AS_IMAGE_RESOURCE to map the DLL as an image (sections aligned)
    // without executing DllMain. This also handles search path resolution (e.g. System32).
    UniqueModule hModule(LoadLibraryW(argv[1]));
    if (!hModule)
    {
        std::wcerr << L"LoadLibraryW failed: " << GetLastError() << L"\n";
        return 1;
    }

    // When loaded as image resource, the handle is the base address.
    PVOID pBase = reinterpret_cast<PVOID>(hModule.get());

    PIMAGE_DOS_HEADER pDosHeader = static_cast<PIMAGE_DOS_HEADER>(pBase);
    if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE)
    {
        std::wcerr << L"Invalid DOS signature\n";
        return 1;
    }

    PIMAGE_NT_HEADERS pNtHeaders = RVA2VA<PIMAGE_NT_HEADERS>(pBase, pDosHeader->e_lfanew);
    if (pNtHeaders->Signature != IMAGE_NT_SIGNATURE)
    {
        std::wcerr << L"Invalid NT signature\n";
        return 1;
    }

    DWORD ExportDirRVA = 0;
    DWORD ExportDirSize = 0;

    if (pNtHeaders->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    {
        PIMAGE_NT_HEADERS32 pNtHeaders32 = reinterpret_cast<PIMAGE_NT_HEADERS32>(pNtHeaders);
        ExportDirRVA = pNtHeaders32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
        ExportDirSize = pNtHeaders32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
    }
    else if (pNtHeaders->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        PIMAGE_NT_HEADERS64 pNtHeaders64 = reinterpret_cast<PIMAGE_NT_HEADERS64>(pNtHeaders);
        ExportDirRVA = pNtHeaders64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
        ExportDirSize = pNtHeaders64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
    }
    else
    {
        std::wcerr << L"Unknown OptionalHeader magic\n";
        return 1;
    }

    if (ExportDirRVA == 0 || ExportDirSize == 0)
    {
        std::wcerr << L"No export directory found\n";
        return 0;
    }

    PIMAGE_EXPORT_DIRECTORY pExportDirectory = RVA2VA<PIMAGE_EXPORT_DIRECTORY>(pBase, ExportDirRVA);
    PDWORD pAddressOfNames = RVA2VA<PDWORD>(pBase, pExportDirectory->AddressOfNames);

    for (DWORD i = 0; i < pExportDirectory->NumberOfNames; ++i)
    {
        PCSTR name = RVA2VA<PCSTR>(pBase, pAddressOfNames[i]);
        std::cout << name << "\n";
    }

    return 0;
}
