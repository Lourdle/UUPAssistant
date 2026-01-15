#include <Lourdle.UIFramework.h>

#include "common.h"
#include <winternl.h>

#include <filesystem>

extern "C"
{
typedef struct _FILE_DISPOSITION_INFORMATION {
	BOOLEAN DeleteFile;
} FILE_DISPOSITION_INFORMATION, * PFILE_DISPOSITION_INFORMATION;

NTSTATUS
NTAPI
NtSetInformationFile(
	_In_ HANDLE FileHandle,
	_Out_ PIO_STATUS_BLOCK IoStatusBlock,
	_In_reads_bytes_(Length) PVOID FileInformation,
	_In_ ULONG Length,
	_In_ FILE_INFORMATION_CLASS FileInformationClass
);

constexpr FILE_INFORMATION_CLASS FileDispositionInformation = static_cast<FILE_INFORMATION_CLASS>(13);
}

using namespace Lourdle::UIFramework;
namespace fs = std::filesystem;

void SetProcessEfficiencyMode(bool bEnable)
{
	PROCESS_POWER_THROTTLING_STATE PowerThrottling = {
		.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION,
		.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED,
		.StateMask = ULONG(bEnable ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0)
	};
	SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &PowerThrottling, sizeof(PowerThrottling));
	SetPriorityClass(GetCurrentProcess(), bEnable ? IDLE_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS);
}

IFileOpenDialog* CreateFileOpenDialogInstance()
{
	IFileOpenDialog* pFileOpenDialog = nullptr;
	if (FAILED(CoCreateInstance(CLSID_FileOpenDialog,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&pFileOpenDialog))))
	{
		CLSID FileOpenDialogLegacy = { 0x725F645B, 0xEAED, 0x4fc5, { 0xB1, 0xC5, 0xD9, 0xAD, 0x0A, 0xCC, 0xBA, 0x5E } };
		CoCreateInstance(FileOpenDialogLegacy,
			nullptr,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&pFileOpenDialog));
	}
	return pFileOpenDialog;
}

bool GetOpenFolderName(WindowBase* pOwner, String& refString)
{
	auto pFileOpenDialog = CreateFileOpenDialogInstance();
	if (!pFileOpenDialog)
		return false;

	pFileOpenDialog->SetOptions(FOS_PICKFOLDERS | FOS_NOCHANGEDIR);
	if (FAILED(pFileOpenDialog->Show(pOwner->GetHandle())))
	{
		pFileOpenDialog->Release();
		return false;
	}

	IShellItem* pShellItem = nullptr;
	bool succeeded = SUCCEEDED(pFileOpenDialog->GetResult(&pShellItem));
	if (succeeded && pShellItem)
	{
		PWSTR pszFilePath = nullptr;
		if (SUCCEEDED(pShellItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath)))
		{
			refString = pszFilePath;
			CoTaskMemFree(pszFilePath);
		}
		pShellItem->Release();
	}

	pFileOpenDialog->Release();
	return succeeded;
}

bool CopyDirectory(PCWSTR srcdir, PCWSTR dstdir)
{
	std::error_code ec;
	fs::copy(srcdir, dstdir, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
	return !ec;
}

ULONGLONG GetDirectorySize(PCWSTR pPath)
{
	std::error_code ec;
	if (!fs::exists(pPath, ec))
		return -1;

	auto iter = fs::recursive_directory_iterator(pPath, ec);
	if (ec) return -1;

	ULONGLONG ullSize = 0;
	for (const auto& entry : iter)
	{
		if (fs::is_regular_file(entry, ec) && !fs::is_symlink(entry, ec))
			ullSize += fs::file_size(entry, ec);
	}
	return ullSize;
}

NTSTATUS DeleteFileOnClose(HANDLE FileHandle)
{
	IO_STATUS_BLOCK IoStatusBlock;
	FILE_DISPOSITION_INFORMATION DispositionInfo = {
		.DeleteFile = TRUE
	};
	return NtSetInformationFile(FileHandle, &IoStatusBlock, &DispositionInfo, sizeof(DispositionInfo), FileDispositionInformation);
}
