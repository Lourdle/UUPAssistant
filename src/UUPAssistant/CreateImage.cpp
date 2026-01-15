#include "pch.h"
#include "Resources/resource.h"
#include "Xml.h"

#include <dismapi.h>
#include <wimgapi.h>

#include <thread>
#include <algorithm>
#include <functional>
#include <cwctype>
#include <Shlwapi.h>
#include <TlHelp32.h>
#include <filesystem>

using namespace Lourdle::UIFramework;
using namespace std;
namespace fs = std::filesystem;

constexpr DWORD kWindows11_24H2_Build = 26100;

void SetReferenceFiles(WIMStruct* wim)
{
	std::error_code ec;
	auto iter = fs::directory_iterator("RefESDs", ec);
	if (ec) return;
	for (const auto& entry : iter)
		if (entry.is_regular_file(ec) && _wcsicmp(entry.path().extension().c_str(), L".esd") == 0)
		{
			auto path = entry.path().wstring();
			auto file = path.c_str();
			wimlib_reference_resource_files(wim, &file, 1, 0, 0);
		}
}

void SetReferenceFiles(HANDLE hWim, const std::wstring& TempPath)
{
	std::error_code ec;
	auto RefPath = fs::path(TempPath) / "RefESDs";
	auto iter = fs::directory_iterator(RefPath, ec);
	if (ec) return;
	for (const auto& entry : iter)
		if (entry.is_regular_file(ec) && _wcsicmp(entry.path().extension().c_str(), L".esd") == 0)
			WIMSetReferenceFile(hWim, entry.path().c_str(), WIM_FLAG_ALLOW_LZMS | WIM_REFERENCE_APPEND);
}

static bool CopyHive(PCWSTR pszExistingHive, PCWSTR pszNewHive)
{
	if (!CopyFileW(pszExistingHive, pszNewHive, FALSE))
		return false;

	DWORD nLength = 0;
	GetFileSecurityW(pszExistingHive, BACKUP_SECURITY_INFORMATION, nullptr, 0, &nLength);
	if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
		return false;

	std::unique_ptr<BYTE[]> sd(new BYTE[nLength]);
	if (!GetFileSecurityW(pszExistingHive, BACKUP_SECURITY_INFORMATION, sd.get(), nLength, &nLength))
		return false;

	if (!SetFileSecurityW(pszNewHive, BACKUP_SECURITY_INFORMATION, sd.get()))
		return false;

	return true;
}

static String GetWindowsEdition(PCWSTR pszImagePath)
{
	fs::path imagePath(pszImagePath);
	RegistryHive RegHive((imagePath / L"Windows\\System32\\config\\SOFTWARE").c_str(), false);
	auto ProductName = GetVersionValue(RegHive, L"ProductName");

	HMODULE hModule = LoadLibraryExW((imagePath / L"Windows\\Branding\\Basebrd\\basebrd.dll").c_str(), nullptr, LOAD_LIBRARY_AS_IMAGE_RESOURCE);
	if (hModule == nullptr)
		return ProductName.c_str();

	auto hRes = FindResourceA(hModule, MAKEINTRESOURCEA(1), MAKEINTRESOURCEA(reinterpret_cast<ULONG_PTR>(RT_RCDATA)));
	if (hRes)
	{
		DWORD dwSize = SizeofResource(hModule, hRes);
		HGLOBAL hGlobal = LoadResource(hModule, hRes);
		PWSTR p = static_cast<PWSTR>(LockResource(hGlobal));
		size_t len = ProductName.size();
		
		size_t maxIdx = dwSize / sizeof(wchar_t);
		if (maxIdx > len)
		{
			size_t loopEnd = maxIdx - len;
			for (size_t j = 1; j < loopEnd; ++j)
			{
				if (wcsncmp(p + j, ProductName.c_str(), len) == 0)
				{
					WORD wId = p[j - 1];
					auto FriendlyName = GetString(wId, hModule);
					
					PWCH percent = wcschr(FriendlyName.GetPointer(), '%');
					if (percent)
					{
						auto trim = percent;
						while (trim > FriendlyName.GetPointer())
							if (trim[-1] == L' ') --trim;
							else break;
						*trim = '\0';
					}
					
					FreeLibrary(hModule);
					return FriendlyName;
				}
			}
		}
	}
	FreeLibrary(hModule);
	return ProductName.c_str();
}


template<typename T>
void RegisterWriteProgressLambda(WIMStruct* wim, T lambda)
{
	wimlib_register_progress_function(wim, [](enum wimlib_progress_msg msg_type,
		union wimlib_progress_info* info,
		void* progctx)
		{
			if (msg_type == WIMLIB_PROGRESS_MSG_WRITE_STREAMS)
				if (!reinterpret_cast<T*>(progctx)->operator()(static_cast<int>(info->write_streams.completed_bytes * 100ULL / info->write_streams.total_bytes)))
					return WIMLIB_PROGRESS_STATUS_ABORT;
			return WIMLIB_PROGRESS_STATUS_CONTINUE;
		}, &lambda);
}

// Once, I do the LZMS compression test without the thread count adjustment, it caused a BSOD due to memory exhaustion.
static int get_num_threads(bool bLZMS = true)
{
	MEMORYSTATUSEX memStatus;
	memStatus.dwLength = sizeof(memStatus);
	GlobalMemoryStatusEx(&memStatus);

	constexpr LONGLONG kMiB = 1024LL * 1024LL;
	constexpr LONGLONG kReservedTotalPhysDivisor = 10;
	constexpr LONGLONG kLzmsMiBPerThread = 640LL;
	constexpr LONGLONG kNonLzmsMiBPerThread = 24LL;

	auto availableMemory = static_cast<LONGLONG>(memStatus.ullAvailPhys)
		- static_cast<LONGLONG>(memStatus.ullTotalPhys / kReservedTotalPhysDivisor);

	const LONGLONG perThreadBytes = (bLZMS ? kLzmsMiBPerThread : kNonLzmsMiBPerThread) * kMiB;
	DWORD maxThreads = 1;
	if (availableMemory > 0 && perThreadBytes > 0)
	{
		const LONGLONG byMemory = availableMemory / perThreadBytes;
		maxThreads = static_cast<DWORD>(max<LONGLONG>(1, byMemory));
	}

	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);

	return static_cast<int>(max<DWORD>(1, min(maxThreads, sysInfo.dwNumberOfProcessors)));
}

static int CopyImage(PCWSTR pszSourceImage, PCWSTR pszDestinationImage, wimlib_progress_func_t progfunc = nullptr, void* progctx = nullptr)
{
	WIMStruct* src, * dst;
	int code = wimlib_open_wim(pszSourceImage, 0, &src);
	if (code != WIMLIB_ERR_SUCCESS)
		return code;
	wimlib_wim_info info;
	wimlib_get_wim_info(src, &info);
	code = wimlib_create_new_wim(static_cast<wimlib_compression_type>(info.compression_type), &dst);
	if (code != WIMLIB_ERR_SUCCESS)
	{
		wimlib_free(src);
		return code;
	}

	code = wimlib_export_image(src, WIMLIB_ALL_IMAGES, dst, nullptr, nullptr, 0);
	wimlib_set_wim_info(dst, &info, WIMLIB_CHANGE_BOOT_INDEX);
	if (progfunc)
		wimlib_register_progress_function(dst, progfunc, progctx);
	code = wimlib_write(dst, pszDestinationImage, WIMLIB_ALL_IMAGES, 0, get_num_threads(info.compression_type == WIMLIB_COMPRESSION_TYPE_LZMS));
	wimlib_free(dst);
	wimlib_free(src);
	return code;
}

template<typename T>
int CopyImage(PCWSTR pszSourceImage, PCWSTR pszDestinationImage, T lambda)
{
	return CopyImage(pszSourceImage, pszDestinationImage, [](enum wimlib_progress_msg msg_type,
		union wimlib_progress_info* info,
		void* progctx)
		{
			if (msg_type == WIMLIB_PROGRESS_MSG_WRITE_STREAMS)
				if(!reinterpret_cast<T*>(progctx)->operator()(static_cast<UINT>(info->write_streams.completed_bytes * 100ULL / info->write_streams.total_bytes)))
					return WIMLIB_PROGRESS_STATUS_ABORT;
			return WIMLIB_PROGRESS_STATUS_CONTINUE;
		}, &lambda);
}

static bool IsTargetEditionMissing(const vector<String>& Editions, const SessionContext& ctx)
{
	bool result = false;
	for (const auto& i : Editions)
		if (i == ctx.TargetImageInfo.Edition)
			result = true;
		else
			return false;
	return result;
}

struct WIMProgressContext
{
	WIMProgressContext(bool& Cancel) : Cancel(Cancel) {}
	function<void(UINT)> fn;
	UINT u = -1;
	bool& Cancel;
	bool bUnmount = false;
};

static DWORD CALLBACK WIMProgressCallback(DWORD dwMessageId, WPARAM wParam, LPARAM lParam, WIMProgressContext* ctx)
{
	if (dwMessageId == WIM_MSG_PROGRESS || dwMessageId == WIM_MSG_MOUNT_CLEANUP_PROGRESS)
	{
		if (ctx->u != static_cast<UINT>(wParam))
		{
			ctx->u = static_cast<UINT>(wParam);
			ctx->fn(static_cast<UINT>(wParam));
		}
	}
	else if (ctx->Cancel)
		return WIM_MSG_ABORT_IMAGE;

	return WIM_MSG_SUCCESS;
}

struct WIMScaningContext
{
	ULONGLONG ullLastTick = GetTickCount64();
	CreateImageContext* ctx;
	bool& Cancel;
};

static DWORD CALLBACK WIMScaningCallback(DWORD dwMessageId, WPARAM wParam, LPARAM lParam, WIMScaningContext* ctx)
{
	if (ctx->Cancel)
		return WIM_MSG_ABORT_IMAGE;
	if (dwMessageId == WIM_MSG_SCANNING)
	{
		ULONGLONG ullTick = GetTickCount64();
		if (ullTick - ctx->ullLastTick > 1000)
		{
			ctx->ullLastTick = ullTick;
			auto format = GetString(String_ScanningFiles);
			auto size = format.GetLength() + 20;
			std::vector<WCHAR> buffer(size);
			swprintf_s(buffer.data(), size, format.GetPointer(), static_cast<DWORD>(lParam));
			ctx->ctx->State.SetWindowText(buffer.data());
		}
	}
	return WIM_MSG_SUCCESS;
}

static void RegisterWIMProgressCallback(HANDLE hWim, WIMProgressContext* ctx, function<void(UINT)> fn)
{
	ctx->fn = fn;
	ctx->u = 0;
	WIMRegisterMessageCallback(hWim, reinterpret_cast<FARPROC>(WIMProgressCallback), ctx);
}

static void RegisterWIMProgressCallback(WIMStruct* wim, WIMProgressContext* ctx, function<void(UINT)> fn)
{
	ctx->fn = fn;
	ctx->u = 0;
	wimlib_register_progress_function(wim, [](enum wimlib_progress_msg msg_type,
		union wimlib_progress_info* info,
		void* progctx)
		{
			if (reinterpret_cast<WIMProgressContext*>(progctx)->Cancel)
				return WIMLIB_PROGRESS_STATUS_ABORT;

			if (msg_type != WIMLIB_PROGRESS_MSG_WRITE_STREAMS
				|| reinterpret_cast<WIMProgressContext*>(progctx)->u == static_cast<UINT>(info->write_streams.completed_bytes * 100ULL / info->write_streams.total_bytes))
				return WIMLIB_PROGRESS_STATUS_CONTINUE;

			reinterpret_cast<WIMProgressContext*>(progctx)->u = static_cast<UINT>(info->write_streams.completed_bytes * 100ULL / info->write_streams.total_bytes);
			reinterpret_cast<WIMProgressContext*>(progctx)->fn(reinterpret_cast<WIMProgressContext*>(progctx)->u);
			return WIMLIB_PROGRESS_STATUS_CONTINUE;
		}, ctx);
}

static bool IsPureNumberString(PCWSTR begin, PCWCH end)
{
	for (auto i = begin; i != end; ++i)
		if (!iswdigit(*i))
			return false;
	return true;
}

void CreateImage(
	SessionContext& ctx, int Compression, PCWSTR pszDestinationImage, PCWSTR pszBootWim,
	bool& Cancel, bool InstallDotNetFx3, std::unique_ptr<CreateImageContext> cictx,
	std::function<void(bool Succeeded)> OnFinish)
{	
	auto Cleanup = [&]()
		{
			fs::path Path = ctx.PathTemp;
			std::error_code ec;
			fs::directory_iterator iter(Path, ec);

			if (!ec) for (auto& entry : iter)
			{
				auto name = entry.path().filename().wstring();
				if (name == L"Mount")
				{
					HANDLE hWim, hImage;
					if (WIMGetMountedImageHandle(entry.path().c_str(), 0, &hWim, &hImage))
					{
						UnloadMountedImageRegistries(L"Mount");
						WIMUnmountImageHandle(hImage, 0);
						WIMCloseHandle(hImage);
						WIMCloseHandle(hWim);
					}
				}
				else if (name == L"Media" && !cictx
					|| name == pszDestinationImage
					|| name == L"Install.swm"
					|| name == L"RefESDs")
					continue;
				else
				{
					name = entry.path().wstring();
					if (entry.is_regular_file())
						DeleteFileW(name.c_str());
					else
						DeleteDirectory(name.c_str());
				}
			}

			if (cictx)
				OnFinish(false);
		};

	struct Cleaner
	{
		decltype(Cleanup)* CleanupLambda;
		HANDLE hEvent;
		Cleaner(decltype(Cleanup)* CleanupLambda)
			: CleanupLambda(CleanupLambda), hEvent(nullptr) {
		}
		~Cleaner()
		{
			if (hEvent)
			{
				SetEvent(hEvent);
				CloseHandle(hEvent);
			}

			(*CleanupLambda)();
		}
	} AutoCleaner(&Cleanup);


	auto AppendText = [&](PCWSTR psz)
		{
			cictx->State.SetWindowText(nullptr);
			int len = cictx->StateDetail.GetWindowTextLength();
			cictx->StateDetail.SetSel(len, len);
			cictx->StateDetail.ReplaceSel(psz);
		};

	auto Error = [&](bool bNextLine = false, UINT uAdditionalType = 0)
		{
			DWORD dwError = GetLastError();
			LPWSTR pszError = nullptr;
			HMODULE hModule = GetModuleHandleW(L"wimgapi.dll");
			FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_HMODULE, hModule, dwError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&pszError), 0, nullptr);
			int result;
			if (cictx->IsDialog)
				result = static_cast<Dialog*>(cictx->DisplayWindow)->MessageBox(pszError, nullptr, MB_ICONERROR | uAdditionalType);
			else
				result = static_cast<Window*>(cictx->DisplayWindow)->MessageBox(pszError, nullptr, MB_ICONERROR | uAdditionalType);
			cictx->State.SetWindowText(nullptr);
			AppendText(pszError);
			LocalFree(pszError);
			return result;
		};

	auto WimlibError = [&](int code, bool bNextLine = false)
		{
			auto psz = wimlib_get_error_string(static_cast<wimlib_error_code>(code));
			if (cictx->IsDialog)
				static_cast<Dialog*>(cictx->DisplayWindow)->MessageBox(psz, L"Wimlib Error", MB_ICONERROR);
			else
				static_cast<Window*>(cictx->DisplayWindow)->MessageBox(psz, L"Wimlib Error", MB_ICONERROR);
			if (bNextLine)
				AppendText(L"\r\n");
			AppendText(psz);
		};

	auto AppendTextMountImage = [&](PCWSTR pszFileName)
		{
			wstring str = GetString(String_ProcessingFile).GetPointer();
			str += pszFileName;
			str += L". ";
			str += GetString(String_MountingImage).GetPointer();
			AppendText(str.c_str());
		};

	auto UnmountWithRetry = [&](HANDLE hImage)
		{
			while (!WIMUnmountImageHandle(hImage, 0))
				if (Error(true, MB_RETRYCANCEL) != IDRETRY)
					return false;
			return true;
		};

	if (ctx.bAdvancedOptionsAvaliable && !InstallDotNetFx3
		&& ctx.AppVector.empty() && ctx.DriverVector.empty() && ctx.UpdateVector.empty()
		&& cictx->Editions.size() == 1 && cictx->Editions[0] == ctx.TargetImageInfo.Edition)
	{
		ctx.bAdvancedOptionsAvaliable = false;
		for (const auto& i : ctx.AppxFeatures)
			if (i.bInstall)
			{
				ctx.bAdvancedOptionsAvaliable = true;
				break;
			}
	}

	HANDLE hWim = WIMCreateFile((ctx.PathUUP + ctx.TargetImageInfo.SystemESD).c_str(), WIM_GENERIC_READ, OPEN_EXISTING, WIM_FLAG_ALLOW_LZMS, 0, nullptr);
	if (!hWim)
	{
		Error(true);
		return;
	}
	WIMSetTemporaryPath(hWim, L"Temp");
	HANDLE hImage = WIMLoadImage(hWim, 2);
	if (!hImage)
	{
		Error(true);
		WIMCloseHandle(hWim);
		return;
	}

	UINT uResId = String_ExportingImageState;
	WIMProgressContext progctx(Cancel);
	HANDLE hWim2 = WIMCreateFile(L"Winre.wim", WIM_GENERIC_READ | WIM_GENERIC_WRITE | WIM_GENERIC_MOUNT, CREATE_ALWAYS, 0, WIM_COMPRESS_LZX, nullptr);
	if (!hWim2)
	{
		Error(true);
		WIMCloseHandle(hImage);
		WIMCloseHandle(hWim);
		return;
	}

	AppendText(GetString(String_ExportingImage));
	AppendText(L"Winre.wim");
	AppendText(L"...");
	WIMSetTemporaryPath(hWim2, L"Temp");

	RegisterWIMProgressCallback(hWim2, &progctx, [&](UINT u)
		{
			cictx->State.SetWindowText(ResStrFormat(uResId, u));
		});
	if (!WIMExportImage(hImage, hWim2, 0))
	{
		Error(true);
		WIMCloseHandle(hImage);
		WIMCloseHandle(hWim);
		WIMCloseHandle(hWim2);
		return;
	}
	WIMCloseHandle(hImage);
	WIMCloseHandle(hWim);

	if (Cancel)
		return;

	AppendText(GetString(String_Succeeded));
	if (ctx.bAdvancedOptionsAvaliable)
	{
		AutoCleaner.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		thread([](HANDLE hEvent, bool& Cancel)
			{
				DuplicateHandle(GetCurrentProcess(), hEvent, GetCurrentProcess(), &hEvent, 0, FALSE, DUPLICATE_SAME_ACCESS);
				while (WaitForSingleObject(hEvent, 50) == WAIT_TIMEOUT)
					if (Cancel)
						KillChildren();
				CloseHandle(hEvent);
			}, AutoCleaner.hEvent, ref(Cancel)).detach();
	}

	if (ctx.bAdvancedOptionsAvaliable && !ctx.SafeOSUpdate.Empty() && ctx.bAddSafeOSUpdate)
	{
		if (pszBootWim && !CopyFileW(L"Winre.wim", L"boot.wim", FALSE))
		{
			WIMCloseHandle(hWim2);
			Error(true);
		}
		uResId = String_MountingImageState;
		AppendTextMountImage(L"Winre.wim");
		CreateDirectoryW(L"Mount", nullptr);
		hImage = WIMLoadImage(hWim2, 1);
		if (!WIMMountImageHandle(hImage, L"Mount", 0))
		{
			Error(true);
			AppendText(GetString(String_UnmountingImage));
			uResId = String_UnmountingImageState;
			WIMUnmountImageHandle(hImage, 0);
			WIMCloseHandle(hImage);
			WIMCloseHandle(hWim2);
			ForceDeleteDirectory(L"Mount");
			return;
		}
		AppendText(GetString(String_Succeeded));
		uResId = String_UnmountingImageState;


		if (!Cancel)
		{
			DismWrapper dism(L"Mount", L"Temp", &Cancel, AppendText);
			if (!dism.Session)
			{
				progctx.bUnmount = true;
				if (!UnmountWithRetry(hImage))
				{
					WIMCloseHandle(hImage);
					WIMCloseHandle(hWim2);
					return;
				}
				progctx.bUnmount = false;
				WIMCloseHandle(hImage);
				goto UseOrigRe;
			}

			AppendText(GetString(String_AddPackage));
			AppendText(ctx.SafeOSUpdate);
			AppendText(L"...");
			if (!dism.AddSinglePackage(ctx.SafeOSUpdate))
			{
				if (Cancel)
					UnloadMountedImageRegistries(L"Mount");
				progctx.bUnmount = true;
				if (!UnmountWithRetry(hImage))
				{
					WIMCloseHandle(hImage);
					WIMCloseHandle(hWim2);
					return;
				}
				WIMCloseHandle(hImage);
				goto UseOrigRe;
			}
			else
				AppendText(GetString(String_Succeeded));
		}

		if (!Cancel)
		{
			uResId = String_CommittingImageState;
			AppendText(GetString(String_CommittingImage));
			WIMScaningContext scanctx{ .ctx = cictx.get(), .Cancel = Cancel };
			WIMRegisterMessageCallback(hWim2, reinterpret_cast<FARPROC>(WIMScaningCallback), &scanctx);
			if (!WIMCommitImageHandle(hImage, 0, nullptr))
			{
				Error(true);
				WIMUnregisterMessageCallback(hWim2, reinterpret_cast<FARPROC>(WIMScaningCallback));
				uResId = String_UnmountingImageState;
				AppendText(GetString(String_UnmountingImage));
				progctx.bUnmount = true;

				if (!UnmountWithRetry(hImage))
				{
					WIMCloseHandle(hImage);
					WIMCloseHandle(hWim2);
					return;
				}
				WIMCloseHandle(hImage);
				goto UseOrigRe;
			}
			AppendText(L"\r\n");
		}
		uResId = String_UnmountingImageState;
		AppendText(GetString(String_UnmountingImage));
		progctx.bUnmount = true;

		if (!UnmountWithRetry(hImage))
		{
			WIMCloseHandle(hImage);
			WIMCloseHandle(hWim2);
			return;
		}
		AppendText(L"\r\n");
		WIMCloseHandle(hImage);
		WIMCloseHandle(hWim2);
	}
	else
	{
		if (!ctx.bAdvancedOptionsAvaliable)
			DeleteDirectory(L"AppxFeatures");

	UseOrigRe:
		WIMCloseHandle(hWim2);
		if (pszBootWim && !Cancel && !CopyFileW(L"Winre.wim", L"boot.wim", FALSE))
		{
			Error(true);
			return;
		}
	}

	if (Cancel)
		return;

	if (pszBootWim)
	{
		AppendText(GetString(String_ConfiguringBootWim));
		hWim2 = WIMCreateFile(L"boot.wim", WIM_GENERIC_READ | WIM_GENERIC_WRITE, OPEN_EXISTING, 0, 0, nullptr);
		if (!hWim2)
		{
			Error(true);
			return;
		}
		WIMSetTemporaryPath(hWim2, L"Temp");
		hImage = WIMLoadImage(hWim2, 1);
		if (!hImage)
		{
			Error(true);
			WIMCloseHandle(hWim2);
			return;
		}
		if (!WIMExportImage(hImage, hWim2, WIM_EXPORT_ALLOW_DUPLICATES))
		{
			Error(true);
			WIMCloseHandle(hImage);
			WIMCloseHandle(hWim2);
			return;
		}
		WIMSetBootImage(hWim2, 2);
		WIMCloseHandle(hImage);
		WIMCloseHandle(hWim2);

		if (Cancel)
			return;

		WIMStruct* wim;
		int code = wimlib_open_wim(L"boot.wim", 0, &wim);
		if (code != WIMLIB_ERR_SUCCESS)
		{
			WimlibError(code, true);
			return;
		}
		else
		{
			PCWSTR pszSoftwareHive = L"\\Windows\\System32\\config\\SOFTWARE";
			code = wimlib_extract_paths(wim, 2, L"Temp", &pszSoftwareHive, 1, WIMLIB_EXTRACT_FLAG_NO_PRESERVE_DIR_STRUCTURE);
			if (code != WIMLIB_ERR_SUCCESS)
			{
			wimfailure:
				WimlibError(code, true);
				wimlib_free(wim);
				return;
			}

			HANDLE hFile = CreateFileW(L"Temp\\SOFTWARE", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			FILETIME ft;
			GetFileTime(hFile, nullptr, nullptr, &ft);
			CloseHandle(hFile);

			RegistryHive hKey(L"Temp\\SOFTWARE", true);
			if (!hKey)
			{
			regfailure_base:
				Error(true);
				wimlib_free(wim);
				return;
			}

			CHAR sz[] = "X:\\$windows.~bt\\";
			auto lStatus = RegSetKeyValueA(hKey, "Microsoft\\Windows NT\\CurrentVersion\\WinPE", "InstRoot", REG_SZ, sz, sizeof(sz));
			RegFlushKey(hKey);
			if (lStatus != ERROR_SUCCESS)
			{
			regfailure:
				SetLastError(lStatus);
				goto regfailure_base;
			}
			
			hKey.Close();
			hFile = CreateFileW(L"Temp\\SOFTWARE", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			SetFileTime(hFile, &ft, &ft, &ft);
			CloseHandle(hFile);
			code = wimlib_add_tree(wim, 1, L"Temp\\SOFTWARE", pszSoftwareHive, 0);
			if (code != WIMLIB_ERR_SUCCESS)
				goto wimfailure;

			if (!hKey.Reset(L"Temp\\SOFTWARE", true))
				goto regfailure_base;

			sz[3] = L'\0';
			if ((lStatus = RegSetKeyValueA(hKey, "Microsoft\\Windows NT\\CurrentVersion\\WinPE", "InstRoot", REG_SZ, sz, 4)) != ERROR_SUCCESS
				|| (lStatus = RegSetKeyValueA(hKey, "Microsoft\\Windows NT\\CurrentVersion\\WinPE", "CustomBackground", REG_EXPAND_SZ, "%systemroot%\\system32\\setup.bmp\0", 33)) != ERROR_SUCCESS)
				goto regfailure;

			if (static_cast<CreateImageWizard*>(cictx->DisplayWindow)->RemoveHwReq.GetCheck() == BST_CHECKED)
			{
				VersionStruct version;
				GetFileVersion(L"Media\\setup.exe", version);
				if (version.dwBuild >= kWindows11_24H2_Build)
				{
					HKEY hHwReqChkKey;
					lStatus = RegCreateKeyExA(hKey, "Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\HwReqChk", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hHwReqChkKey, nullptr);
					if (lStatus == ERROR_SUCCESS)
					{
						constexpr BYTE lpBypasses[] = "SQ_SecureBootCapable=TRUE\0SQ_SecureBootEnabled=TRUE\0SQ_TpmVersion=2\0SQ_RamMB=8192\0";
						lStatus = RegSetValueExA(hHwReqChkKey, "HwReqChkVars", 0, REG_MULTI_SZ, lpBypasses, sizeof(lpBypasses));
						RegCloseKey(hHwReqChkKey);
					}
				}

				if (lStatus != ERROR_SUCCESS)
					goto regfailure;

				PCWSTR pszSystemHive = L"\\Windows\\System32\\config\\SYSTEM";
				code = wimlib_extract_paths(wim, 1, L"Temp", &pszSystemHive, 1, WIMLIB_EXTRACT_FLAG_NO_PRESERVE_DIR_STRUCTURE);
				if (code != WIMLIB_ERR_SUCCESS)
					goto wimfailure;

				if (hKey.Reset(L"Temp\\SYSTEM", true))
					goto regfailure_base;

				DWORD dwTrue = TRUE;
				if ((lStatus = RegSetKeyValueW(hKey, L"Setup\\LabConfig", L"BypassTPMCheck", REG_DWORD, &dwTrue, sizeof(dwTrue))) != ERROR_SUCCESS
					|| (lStatus = RegSetKeyValueW(hKey, L"Setup\\LabConfig", L"BypassRMMCheck", REG_DWORD, &dwTrue, sizeof(dwTrue))) != ERROR_SUCCESS
					|| (lStatus = RegSetKeyValueW(hKey, L"Setup\\LabConfig", L"BypassSecureBootCheck", REG_DWORD, &dwTrue, sizeof(dwTrue))) != ERROR_SUCCESS)
					goto regfailure;

				if (version.dwBuild >= kWindows11_24H2_Build)
				{
					HKEY hMoSetupKey;
					lStatus = RegCreateKeyExA(hKey, "Setup\\MoSetup", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hMoSetupKey, nullptr);
					if (lStatus == ERROR_SUCCESS)
					{
						lStatus = RegSetValueExA(hMoSetupKey, "AllowUpgradesWithUnsupportedTPMOrCPU", 0, REG_DWORD, reinterpret_cast<PBYTE>(&dwTrue), sizeof(dwTrue));
						RegCloseKey(hMoSetupKey);
					}
				}

				hKey.Close();
				if (lStatus != ERROR_SUCCESS)
					goto regfailure;

				hFile = CreateFileW(L"Temp\\SYSTEM", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
				SetFileTime(hFile, &ft, &ft, &ft);
				CloseHandle(hFile);

				wimlib_add_tree(wim, 2, L"Temp\\SYSTEM", pszSystemHive, 0);
			}
			else hKey.Close();

			hFile = CreateFileW(L"Temp\\SOFTWARE", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			SetFileTime(hFile, &ft, &ft, &ft);
			CloseHandle(hFile);

			wimlib_add_tree(wim, 2, L"Temp\\SOFTWARE", L"\\Windows\\System32\\config\\SOFTWARE", 0);
			wimlib_add_tree(wim, 2, L"Media\\sources\\background_cli.bmp", L"\\Windows\\System32\\setup.bmp", 0);
			wimlib_add_tree(wim, 2, L"Media\\sources", L"\\sources", 0);
			wimlib_add_tree(wim, 2, L"Media\\setup.exe", L"\\setup.exe", 0);
			wimlib_delete_path(wim, 1, L"\\Windows\\System32\\winpeshl.ini", WIMLIB_DELETE_FLAG_FORCE);
			wimlib_delete_path(wim, 2, L"\\Windows\\System32\\winpeshl.ini", WIMLIB_DELETE_FLAG_FORCE);

			// Note: In Setup Media, these files are named in all lowercase (e.g. "setuphost.exe").
			// When adding these files to PE, rename them to PE-style casing (e.g. "SetupHost.exe").
			PCWSTR FileList[] =
			{
				L"MediaSetupUIMgr.dll",
				L"ServicingCommon.dll",
				L"SetupCore.dll",
				L"SetupHost.exe",
				L"SetupMgr.dll",
				L"SetupPlatform.cfg",
				L"SetupPlatform.dll",
				L"SetupPlatform.exe",
				L"SetupPrep.exe",
				L"SmiEngine.dll",
				L"WinDlp.dll"
			};

			wstring File;
			File = L"\\sources\\";
			auto len = File.size();
			for (auto i : FileList)
			{
				File.append(i);
				// We must first rename the file to a temporary name or wimlib will ignore the operation.
				// For the temporary name, we directly use last hive loaded key name.
				wimlib_rename_path(wim, 2, File.c_str(), hKey);
				wimlib_rename_path(wim, 2, hKey, File.c_str());
				File.erase(len);
			}

			File += ctx.TargetImageInfo.Lang;
			File.push_back('\\');
			len = File.size();
			for (auto i : FileList)
			{
				File.erase(len);
				File.append(i);
				File.append(L".mui");
				wimlib_rename_path(wim, 2, File.c_str(), hKey);
				wimlib_rename_path(wim, 2, hKey, File.c_str());
			}
			wimlib_rename_path(wim, 2, L"\\sources\\background_cli.bmp", L"\\sources\\background.bmp");
			wimlib_rename_path(wim, 2, L"\\sources\\ARUNIMG.dll", hKey);
			wimlib_rename_path(wim, 2, hKey, L"\\sources\\ARUNIMG.dll");


			wstring str = wimlib_get_image_name(wim, 1);

			auto i = str.begin() + str.find(L"Recovery Environment");
			str.erase(i, i + 20);
			str.insert(i - str.begin(), L"PE");
			wimlib_set_image_name(wim, 1, str.c_str());
			wimlib_set_image_descripton(wim, 1, str.c_str());

			str.erase(i, i + 2);
			str.insert(i - str.begin(), L"Setup");
			wimlib_set_image_name(wim, 2, str.c_str());
			wimlib_set_image_descripton(wim, 2, str.c_str());

			wimlib_set_image_flags(wim, 1, L"9");
			wimlib_set_image_flags(wim, 2, L"2");

			if (!Cancel)
				code = wimlib_overwrite(wim, 0, 0);
			wimlib_free(wim);
			if (code != WIMLIB_ERR_SUCCESS)
			{
				WimlibError(code, true);
				return;
			}
		}

		if (Cancel)
			return;

		code = CopyImage(L"boot.wim", pszBootWim);
		if (code != WIMLIB_ERR_SUCCESS)
		{
			WimlibError(code, true);
			return;
		}
		DeleteFileW(L"boot.wim");
		DeleteDirectory(L"Temp");
		CreateDirectoryW(L"Temp", nullptr);
		AppendText(GetString(String_Succeeded));
	}
	else if (InstallDotNetFx3)
	{
		SetCurrentDirectoryW(ctx.PathUUP.c_str());
		WIMStruct* wim;
		if (wimlib_open_wim(ctx.TargetImageInfo.SystemESD.c_str(), 0, &wim) == 0)
		{
			PCWSTR Path = L"\\sources\\sxs";
			if (!Cancel)
				wimlib_extract_paths(wim, 1, ctx.PathTemp.c_str(), &Path, 1, WIMLIB_EXTRACT_FLAG_NO_PRESERVE_DIR_STRUCTURE);
			wimlib_free(wim);
		}
		SetCurrentDirectoryW(ctx.PathTemp.c_str());
	}

	if (Cancel)
		return;

	if (!ctx.bAdvancedOptionsAvaliable)
	{
		AppendText(GetString(String_ExportingImage));
		AppendText(L"install.wim");
		AppendText(L"...");
		if (!SetCurrentDirectoryW(ctx.PathUUP.c_str()))
		{
			Error(true);
			return;
		}
		WIMStruct* wim;
		int code = wimlib_open_wim(ctx.TargetImageInfo.SystemESD.c_str(), 0, &wim);
		if (code != WIMLIB_ERR_SUCCESS)
		{
			WimlibError(code, true);
			return;
		}
		if (!SetCurrentDirectoryW(ctx.PathTemp.c_str()))
		{
			Error(true);
			wimlib_free(wim);
			return;
		}
		SetReferenceFiles(wim);
		WIMStruct* wim2;
		wimlib_create_new_wim(static_cast<wimlib_compression_type>(Compression), &wim2);
		String Desc = wimlib_get_image_description(wim, 3);
		String Name = wimlib_get_image_name(wim, 3);
		code = wimlib_export_image(wim, 3, wim2, nullptr, nullptr, 0);
		if (code != WIMLIB_ERR_SUCCESS)
		{
			WimlibError(code, true);
			wimlib_free(wim2);
			wimlib_free(wim);
			return;
		}

		RegisterWIMProgressCallback(wim2, &progctx, [&](UINT u)
			{
				cictx->State.SetWindowText(ResStrFormat(String_ExportingImageState, u));
			});
		code = wimlib_write(wim2, L"install.wim", WIMLIB_ALL_IMAGES, 0, get_num_threads(Compression == WIM_COMPRESS_LZMS));
		wimlib_free(wim2);
		wimlib_free(wim);
		if (code != WIMLIB_ERR_SUCCESS)
		{
			WimlibError(code, true);
			return;
		}
		else
			AppendText(GetString(String_Succeeded));

		if (!cictx->IsDialog)
			DeleteDirectory(L"RefESDs");
		code = wimlib_open_wim(L"install.wim", 0, &wim);
		do
		{
			wstring SystemPath = ctx.PathTemp + L"Mount";
			if (ctx.bInstallEdge
				&& !CreateDirectoryW(SystemPath.c_str(), nullptr))
			{
				Error(true);
				break;
			}

			if (ctx.bInstallEdge)
			{
				PCWSTR Paths[] =
				{
					L"\\Windows\\System32\\config\\SYSTEM",
					L"\\Windows\\System32\\config\\SOFTWARE",
				};
				AppendText(GetString(String_InstallingEdge));
				code = wimlib_extract_paths(wim, 1, SystemPath.c_str(), Paths, 2, 0);
				if (code != WIMLIB_ERR_SUCCESS)
				{
					WimlibError(code, true);
					DeleteDirectory(SystemPath.c_str());
					break;
				}
				if (!InstallMicrosoftEdge((ctx.PathUUP + L"Edge.wim").c_str(), SystemPath.c_str(), ctx.PathTemp.c_str(), ctx.TargetImageInfo.Arch))
				{
					DeleteDirectory(SystemPath.c_str());
					Error(true);
					break;
				}
				AppendText(L"\r\n");
				code = wimlib_add_tree(wim, 1, SystemPath.c_str(), L"\\", 0);
				if (code != WIMLIB_ERR_SUCCESS)
				{
					WimlibError(code, true);
					DeleteDirectory(SystemPath.c_str());
				}
			}
		} while (false);

		AppendText(GetString(String_Configuring));
		AppendText(L"\r\n");
		if (code != WIMLIB_ERR_SUCCESS)
		{
			WimlibError(code, true);
			return;
		}
		code = wimlib_add_tree(wim, 1, L"Winre.wim", L"\\Windows\\System32\\Recovery\\Winre.wim", 0);
		if (code != WIMLIB_ERR_SUCCESS)
		{
			WimlibError(code, true);
			wimlib_free(wim);
			return;
		}
		RegisterWIMProgressCallback(wim, &progctx, [&](UINT u)
			{
				cictx->State.SetWindowText(ResStrFormat(String_CommittingImageState, u));
			});
		wimlib_set_image_descripton(wim, 1, Desc);
		wimlib_set_image_name(wim, 1, Name);
		code = wimlib_overwrite(wim, WIMLIB_WRITE_FLAG_NO_CHECK_INTEGRITY, 0);
		wimlib_free(wim);
		if (code != WIMLIB_ERR_SUCCESS)
		{
			WimlibError(code, true);
			return;
		}
		DeleteFileW(L"Winre.wim");
		if (ctx.bInstallEdge)
			DeleteDirectory((ctx.PathTemp + L"Mount").c_str());

		AppendText(GetString(String_ExportingImage));
		AppendText(pszDestinationImage);
		AppendText(L"...");
		code = CopyImage(L"install.wim", pszDestinationImage,
			[&](UINT u)
			{
				if (progctx.u == u)
					return !Cancel;
				progctx.u = u;
				cictx->State.SetWindowText(ResStrFormat(String_ExportingImageState, u));
				return !Cancel;
			});
		if (code != WIMLIB_ERR_SUCCESS)
		{
			WimlibError(code, true);
			return;
		}
		AppendText(L"\r\n");
		AppendText(GetString(String_Succeeded));
		DeleteFileW(L"install.wim");
	}
	else
	{
		AppendText(GetString(String_ExportingImage));
		AppendText(L"install.wim");
		AppendText(L"...");
		hWim = WIMCreateFile((ctx.PathUUP + ctx.TargetImageInfo.SystemESD).c_str(), WIM_GENERIC_READ, OPEN_EXISTING, WIM_FLAG_ALLOW_LZMS, 0, nullptr);
		if (!hWim)
		{
			Error(true);
			return;
		}
		WIMSetTemporaryPath(hWim, L"Temp");
		SetReferenceFiles(hWim, ctx.PathTemp);
		hImage = WIMLoadImage(hWim, 3);
		if (!hImage)
		{
			Error(true);
			WIMCloseHandle(hWim);
			return;
		}

		hWim2 = WIMCreateFile(L"install.wim", WIM_GENERIC_READ | WIM_GENERIC_WRITE | WIM_GENERIC_MOUNT, CREATE_ALWAYS, 0, Compression == WIM_COMPRESS_LZMS ? WIM_COMPRESS_XPRESS : Compression, nullptr);
		if (!hWim2)
		{
			Error(true);
			WIMCloseHandle(hImage);
			WIMCloseHandle(hWim);
			return;
		}
		WIMSetTemporaryPath(hWim2, L"Temp");
		UINT uResId = String_ExportingImageState;
		RegisterWIMProgressCallback(hWim2, &progctx, [&](UINT u)
			{
				cictx->State.SetWindowText(ResStrFormat(uResId, u));
			});
		if (!WIMExportImage(hImage, hWim2, 0))
		{
			Error(true);
			WIMCloseHandle(hImage);
			WIMCloseHandle(hWim);
			WIMCloseHandle(hWim2);
			return;
		}
		AppendText(L"\r\n");
		WIMCloseHandle(hImage);
		WIMCloseHandle(hWim);
		if (Compression != WIM_COMPRESS_LZMS && !cictx->IsDialog)
			DeleteDirectory(L"RefESDs");

		hImage = WIMLoadImage(hWim2, 1);
		if (!hImage)
		{
			Error(true);
			WIMCloseHandle(hWim2);
			return;
		}
		CreateDirectoryW(L"Mount", nullptr);
		AppendTextMountImage(L"install.wim");
		uResId = String_MountingImageState;
		if (!WIMMountImageHandle(hImage, L"Mount", 0))
		{
			Error(true);
			uResId = String_UnmountingImageState;
			AppendText(GetString(String_UnmountingImage));
			WIMUnmountImageHandle(hImage, 0);
			WIMCloseHandle(hImage);
			WIMCloseHandle(hWim2);
			ForceDeleteDirectory(L"Mount");
			return;
		}
		AppendText(GetString(String_Succeeded));

		WIMCloseHandle(hImage);
		WIMCloseHandle(hWim2);

		DismWrapper dism(L"Mount", L"Temp", &Cancel, AppendText);
		size_t nAppxes = 0;
		dism.SetString = [&cictx](PCWSTR pstr)
			{
				cictx->State.SetWindowText(pstr);
			};
		dism.MessageBox = [&cictx](LPCTSTR lpText, LPCWSTR lpCaption, UINT uType)
			{
				return cictx->IsDialog ? static_cast<Dialog*>(cictx->DisplayWindow)->MessageBox(lpText, lpCaption, uType) : static_cast<Window*>(cictx->DisplayWindow)->MessageBox(lpText, lpCaption, uType);
			};

		do
		{
			if (!dism.Session)
				break;

			if (InstallDotNetFx3 && !dism.EnableDotNetFx3(pszBootWim ? L"Media\\sources\\sxs" : L"sxs"))
				break;
			if (!pszBootWim)
				DeleteDirectory(L"sxs");

			if (ctx.SafeOSUpdate.Empty())
			{
				if (!MoveFileW(L"Winre.wim", L"Mount\\Windows\\System32\\Recovery\\Winre.wim"))
				{
					CopyFileW(L"Winre.wim", L"Mount\\Windows\\System32\\Recovery\\Winre.wim", FALSE);
					DeleteFileW(L"Winre.wim");
				}
			}
			else
			{
				CopyImage(L"Winre.wim", L"Mount\\Windows\\System32\\Recovery\\Winre.wim");
				DeleteFileW(L"Winre.wim");
			}

			switch (dism.AddUpdates(ctx))
			{
			case TRUE:
				goto InstallSoftware;
			case BYTE(-1):
				Cancel = true;
				cictx->DisplayWindow->Invalidate(false);
			}
			break;

		InstallSoftware:
			if (!dism.AddDrivers(ctx))
				break;

			for (const auto& i : ctx.AppxFeatures)
				if (i.bInstall)
					++nAppxes;
			nAppxes += ctx.AppVector.size();
			if (nAppxes > 0 && !dism.AddApps(ctx))
				break;

			dism.CloseSession();
		} while (false);

		if (Cancel)
			UnloadMountedImageRegistries(L"Mount");
		else if (ctx.bInstallEdge) do
		{
			auto EdgePath = ctx.PathUUP + L"Edge.wim";
			if (!PathFileExistsW(EdgePath.c_str()))
				break;

			AppendText(GetString(String_InstallingEdge));
			if (!InstallMicrosoftEdge(EdgePath.c_str(), L"Mount", ctx.PathTemp.c_str(), ctx.TargetImageInfo.Arch))
				AppendText(GetString(String_Failed));
			else
				AppendText(GetString(String_Succeeded));
		} while (false);

		WIMScaningContext scanctx{ .ctx = cictx.get(), .Cancel = Cancel };
		if (!WIMGetMountedImageHandle(L"Mount", 0, &hWim, &hImage))
		{
			hWim = nullptr;
			hImage = nullptr;
			goto UnmountAndCleanup;
		}
		uResId = String_CommittingImageState;
		if (!Cancel
			&& (!ctx.UpdateVector.empty() || nAppxes != 0 || ctx.bInstallEdge))
			AppendText(GetString(String_CommittingImage));
		RegisterWIMProgressCallback(hWim, &progctx, [&](UINT u)
			{
				cictx->State.SetWindowText(ResStrFormat(uResId, u));
			});
		WIMRegisterMessageCallback(hWim, reinterpret_cast<FARPROC>(WIMScaningCallback), &scanctx);

		scanctx.ullLastTick = GetTickCount64();
		if (Cancel
			|| (!ctx.UpdateVector.empty() || nAppxes != 0 || ctx.bInstallEdge) && !WIMCommitImageHandle(hImage, 0, nullptr))
		{
		UnmountAndCleanup:
			if (!Cancel)
				Error(true);
			uResId = String_UnmountingImageState;
			AppendText(GetString(String_UnmountingImage));
			WIMUnregisterMessageCallback(hWim, reinterpret_cast<FARPROC>(WIMScaningCallback));
			UnloadMountedImageRegistries(L"Mount");
			progctx.bUnmount = true;

			UnmountWithRetry(hImage);
			cictx->State.SetWindowText(nullptr);
			WIMCloseHandle(hImage);
			WIMCloseHandle(hWim);
			RemoveDirectoryW(L"Mount");
			return;
		}
		if (!ctx.UpdateVector.empty() || nAppxes != 0 || ctx.bInstallEdge)
			AppendText(L"\r\n");
		if (!IsTargetEditionMissing(cictx->Editions, ctx))
		{
			WIMCloseHandle(hImage);
			WIMCloseHandle(hWim);

			CreateDirectoryW(L"Registries\\", nullptr);

			std::vector<String> Editions = cictx->Editions;

			struct EditionBranch
			{
				EditionBranch(PCWSTR Edition) : Edition(Edition) {}
				String Edition;
				std::vector<String> AdditionalEditions;
				std::vector<std::shared_ptr<EditionBranch>> UpgradableEditions;
			};
			vector<shared_ptr<EditionBranch>> EditionTree;

			do
			{
				HANDLE hFile = CreateFileW(L"Mount\\Windows\\servicing\\Editions\\EditionMappings.xml", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
				if (hFile == INVALID_HANDLE_VALUE)
					break;

				string text;
				ReadText(hFile, text, GetFileSize(hFile, nullptr));
				CloseHandle(hFile);

				auto doc = std::make_unique<rapidxml::xml_document<char>>();
				doc->parse<0>(const_cast<char*>(text.c_str()));

				auto pRoot = doc->first_node();
				for (auto p = pRoot->first_node("Edition"); p; p = p->next_sibling("Edition"))
				{
					auto attr = p->first_attribute("virtual");
					auto Name = p->first_node("Name")->value();
					if (Name)
					{
						wstring name(Name, Name + strlen(Name));
						if (find(Editions.begin(), Editions.end(), name.c_str()) == Editions.end())
							continue;
						if (attr && strcmp(attr->value(), "true") == 0)
						{
							auto node = p->first_node("ParentEdition");
							wstring ParentEdition(node->value(), node->value() + node->value_size());
							auto Branch = find_if(EditionTree.begin(), EditionTree.end(), [&](shared_ptr<EditionBranch> p)
								{
									return p->Edition == ParentEdition.c_str();
								});
							if (Branch == EditionTree.end())
							{
								EditionTree.push_back(make_shared<EditionBranch>(ParentEdition.c_str()));
								Branch = EditionTree.end() - 1;
							}

							(*Branch)->AdditionalEditions.push_back(name.c_str());
						}
						else
						{
							auto Branch = find_if(EditionTree.begin(), EditionTree.end(), [&](shared_ptr<EditionBranch> p)
								{
									return p->Edition == name.c_str();
								});
							if (Branch == EditionTree.end())
							{
								EditionTree.push_back(make_shared<EditionBranch>(name.c_str()));
								Branch = EditionTree.end() - 1;
							}

							(*Branch)->AdditionalEditions.push_back(name.c_str());
						}
					}
				}

				hFile = CreateFileW(L"Mount\\Windows\\servicing\\Editions\\EditionMatrix.xml", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
				if (hFile == INVALID_HANDLE_VALUE)
					break;

				text.clear();
				ReadText(hFile, text, GetFileSize(hFile, nullptr));
				CloseHandle(hFile);

				doc->clear();
				doc->parse<0>(const_cast<char*>(text.c_str()));

				pRoot = doc->first_node();
				for (auto p = pRoot->first_node("Edition"); p; p = p->next_sibling("Edition"))
				{
					auto ID = p->first_attribute("ID")->value();
					if (ID)
					{
						wstring EditionID(ID, ID + strlen(ID));
						auto it = find_if(EditionTree.begin(), EditionTree.end(), [&](shared_ptr<EditionBranch> p)
							{
								return p->Edition == EditionID.c_str();
							});
						if (it == EditionTree.end())
							continue;

						for (auto node = p->first_node("Target"); node; node = node->next_sibling("Target"))
						{
							auto Target = node->first_attribute("ID")->value();
							if (Target)
							{
								wstring TargetID(Target, Target + strlen(Target));
								auto it2 = find_if(EditionTree.begin(), EditionTree.end(), [&](shared_ptr<EditionBranch> p)
									{
										return p->Edition == TargetID.c_str();
									});
								if (it2 == EditionTree.end())
									continue;

								(*it)->UpgradableEditions.push_back(*it2);
							}
						}
					}
				}

				function <void(vector<shared_ptr<EditionBranch>>&)> SortBranches;
				SortBranches = [&](vector<shared_ptr<EditionBranch>>& EditionBranches)
					{
						auto lambda = [](shared_ptr<EditionBranch> a, shared_ptr<EditionBranch> b)
							{
								return a->Edition < b->Edition;
							};

						sort(EditionBranches.begin(), EditionBranches.end(), lambda);
						for (auto& i : EditionBranches)
							SortBranches(i->UpgradableEditions);
					};
				SortBranches(EditionTree);

				struct RebuildBranches
				{
					vector<shared_ptr<EditionBranch>>* MaxDepth = nullptr;
					int Depth = 0;

					RebuildBranches& operator()(vector<shared_ptr<EditionBranch>>& EditionBranches, shared_ptr<EditionBranch> Edition, int Depth)
					{
						for (size_t i = 0; i != EditionBranches.size(); ++i)
							if (EditionBranches[i] == Edition)
							{
								if (Depth > 0)
								{
									if (Depth + 1 > this->Depth)
									{
										this->Depth = Depth + 1;
										MaxDepth = &EditionBranches;
									}
									EditionBranches.erase(EditionBranches.begin() + i);
									--i;
								}
							}
							else
								operator()(EditionBranches[i]->UpgradableEditions, Edition, Depth + 1);

						return *this;
					}

					bool PushBack(shared_ptr<EditionBranch> Edition)
					{
						if (MaxDepth)
						{
							MaxDepth->push_back(Edition);
							return true;
						}
						return false;
					}
				};

				for (size_t i = 0; i != EditionTree.size(); ++i)
					if (RebuildBranches()(EditionTree, EditionTree[i], 0).PushBack(EditionTree[i]))
					{
						EditionTree.erase(EditionTree.begin() + i);
						--i;
					}
			} while (false);

			DWORD dwIndex = 1;

			function<bool(EditionBranch*)> Iterate;
			Iterate = [&](EditionBranch* branch)
				{
					if (Cancel)
						return false;

					AppendText(GetString(String_UpgradingEdition));
					if (!dism.OpenSession())
						return false;

					bool bSucceeded = dism.SetEdition(branch->Edition, true);
					dism.CloseSession();
					if (!bSucceeded)
						return true;

					dism.SetString(nullptr);
					AppendText(GetString(String_Succeeded));

					fs::path MountPath = ctx.PathTemp;
					MountPath /= L"Mount\\Windows";
					for (const auto& i : cictx->Editions)
					{
						auto EditionXmlPath = MountPath / (i + L".xml").GetPointer();
						if (fs::exists(EditionXmlPath))
							DeleteFileW(EditionXmlPath.c_str());
					}

					MountPath /= L"System32\\config";
					auto MountedSystemHivePath = MountPath / L"SYSTEM";
					auto MountedSoftwareHivePath = MountPath / L"SOFTWARE";

					fs::path BasePath = ctx.PathTemp;
					BasePath /= L"Registries";
					BasePath /= std::to_wstring(dwIndex);
					CreateDirectoryW(BasePath.c_str(), nullptr);

					for (auto& i : branch->AdditionalEditions)
					{
						AppendText(ResStrFormat(String_SwitchingEdition, i.GetPointer()));

						if (!dism.OpenSession())
							return false;

						bSucceeded = dism.SetEdition(i, true);
						dism.CloseSession();
						if (!bSucceeded)
							continue;

						auto Edition = GetWindowsEdition(L"Mount");
						wstring EditionDir = i.GetPointer();
						EditionDir.push_back(L'$');
						EditionDir += Edition;

						auto Path = BasePath / EditionDir;
						CreateDirectoryW(Path.c_str(), nullptr);
						if (branch->AdditionalEditions.size() != 1)
						{
							auto Software = Path / L"SOFTWARE";
							auto System = Path / L"SYSTEM";
							if (!CopyHive(MountedSoftwareHivePath.c_str(), Software.c_str()))
								goto RegErr;
							if (!CopyHive(MountedSystemHivePath.c_str(), System.c_str()))
							{
							RegErr:
								AppendText(GetString(String_Failed));
								LPWSTR lpszError;
								FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, nullptr, GetLastError(), 0, reinterpret_cast<LPWSTR>(&lpszError), 0, nullptr);
								AppendText(lpszError);
								LocalFree(lpszError);
								DeleteDirectory(Path.c_str());
							}
						}
						AppendText(L"\r\n");
						if (Cancel)
							return false;
					}

					HANDLE hWim, hImage;
					WIMGetMountedImageHandle(L"Mount", 0, &hWim, &hImage);
					RegisterWIMProgressCallback(hWim, &progctx, [&](UINT u)
						{
							cictx->State.SetWindowText(ResStrFormat(uResId, u));
						});
					AppendText(GetString(String_CommittingImage));
					WIMRegisterMessageCallback(hWim, reinterpret_cast<FARPROC>(WIMScaningCallback), &scanctx);
					scanctx.ullLastTick = GetTickCount64();
					uResId = String_CommittingImageState;
					if (WIMCommitImageHandle(hImage, WIM_COMMIT_FLAG_APPEND, nullptr))
					{
						AppendText(L"\r\n");
						++dwIndex;
					}
					else
					{
						AppendText(GetString(String_Failed));
						LPWSTR lpszError;
						FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, nullptr, GetLastError(), 0, reinterpret_cast<LPWSTR>(&lpszError), 0, nullptr);
						AppendText(lpszError);
						LocalFree(lpszError);

						fs::path Path = ctx.PathTemp;
						Path /= L"Registries";
						Path /= std::to_wstring(dwIndex);
						DeleteDirectory(Path.c_str());
					}
					WIMUnregisterMessageCallback(hWim, reinterpret_cast<FARPROC>(WIMScaningCallback));

					if (branch->UpgradableEditions.empty())
					{
						WIMCloseHandle(hImage);
						WIMCloseHandle(hWim);
						return true;
					}

					DWORD dwCurrentImageIndex = dwIndex;
					for (auto& i : branch->UpgradableEditions)
					{
						if (dwCurrentImageIndex != dwIndex)
						{
							uResId = String_UnmountingImageState;
							AppendText(GetString(String_UnmountingImage));
							progctx.bUnmount = true;

							if (!UnmountWithRetry(hImage))
							{
								WIMCloseHandle(hImage);
								WIMCloseHandle(hWim);
								RemoveDirectoryW(L"Mount");
								return false;
							}
							WIMCloseHandle(hImage);
							progctx.bUnmount = false;

							hImage = WIMLoadImage(hWim, dwCurrentImageIndex + 1);
							uResId = String_MountingImageState;
							if (!WIMMountImageHandle(hImage, L"Mount", 0))
							{
								Error(true);
								uResId = String_UnmountingImageState;
								AppendText(GetString(String_UnmountingImage));
								WIMUnmountImageHandle(hImage, 0);
								WIMCloseHandle(hImage);
								WIMCloseHandle(hWim);
								ForceDeleteDirectory(L"Mount");
								return false;
							}
						}

						WIMCloseHandle(hImage);
						WIMCloseHandle(hWim);

						if (!Iterate(i.get()))
							return false;
					}

					return true;
				};

			for (auto& i : EditionTree)
			{
				if (i != EditionTree[0])
				{
					WIMGetMountedImageHandle(L"Mount", 0, &hWim, &hImage);
					RegisterWIMProgressCallback(hWim, &progctx, [&](UINT u)
						{
							cictx->State.SetWindowText(ResStrFormat(uResId, u));
							return uResId == String_UnmountingImageState || !Cancel;
						});
					uResId = String_UnmountingImageState;
					AppendText(GetString(String_UnmountingImage));
					progctx.bUnmount = true;

					if (!UnmountWithRetry(hImage))
					{
						WIMCloseHandle(hImage);
						WIMCloseHandle(hWim);
						return;
					}
					progctx.bUnmount = false;
					AppendText(L"\r\n");

					AppendText(GetString(String_MountingImage));
					uResId = String_MountingImageState;
					WIMCloseHandle(hImage);
					hImage = WIMLoadImage(hWim, 1);
					if (!WIMMountImageHandle(hImage, L"Mount", 0))
					{
						Error(true);
						uResId = String_UnmountingImageState;
						AppendText(GetString(String_UnmountingImage));
						WIMUnmountImageHandle(hImage, 0);
						WIMCloseHandle(hImage);
						WIMCloseHandle(hWim);
						ForceDeleteDirectory(L"Mount");
						return;
					}
					else
						AppendText(GetString(String_Succeeded));
					WIMCloseHandle(hImage);
					WIMCloseHandle(hWim);
				}

				if (!Iterate(i.get()))
					break;
			}

			WIMGetMountedImageHandle(L"Mount", 0, &hWim, &hImage);
			RegisterWIMProgressCallback(hWim, &progctx, [&](UINT u)
				{
					cictx->State.SetWindowText(ResStrFormat(String_UnmountingImageState, u));
				});
			AppendText(GetString(String_UnmountingImage));
			progctx.bUnmount = true;
			UnloadMountedImageRegistries(L"Mount");

			if (!UnmountWithRetry(hImage))
			{
				WIMCloseHandle(hImage);
				WIMCloseHandle(hWim);
				return;
			}
			AppendText(L"\r\n");
			WIMCloseHandle(hImage);
			WIMDeleteImage(hWim, 1);
			WIMCloseHandle(hWim);
		}
		else
		{
			uResId = String_UnmountingImageState;
			AppendText(GetString(String_UnmountingImage));
			progctx.bUnmount = true;

			if (!UnmountWithRetry(hImage))
			{
				WIMCloseHandle(hImage);
				WIMCloseHandle(hWim);
				return;
			}
			WIMUnregisterMessageCallback(hWim, reinterpret_cast<FARPROC>(WIMScaningCallback));
			AppendText(L"\r\n");
			WIMCloseHandle(hImage);
			WIMCloseHandle(hWim);
		}

		if (Compression == WIMLIB_COMPRESSION_TYPE_LZMS)
		{
			AppendText(GetString(String_ExportingImage));
			AppendText(L"install.esd");
			AppendText(L"...");
			if (Cancel)
				return;
			WIMStruct* esd, * wim, * src;
			wimlib_create_new_wim(WIMLIB_COMPRESSION_TYPE_LZMS, &esd);
			int code = wimlib_open_wim(L"install.wim", 0, &wim);
			if (code != WIMLIB_ERR_SUCCESS)
			{
				WimlibError(code, true);
				wimlib_free(esd);
				return;
			}
			SetCurrentDirectoryW(ctx.PathUUP.c_str());
			code = wimlib_open_wim(ctx.TargetImageInfo.SystemESD.c_str(), 0, &src);
			SetCurrentDirectoryW(ctx.PathTemp.c_str());
			if (code != WIMLIB_ERR_SUCCESS)
			{
				WimlibError(code, true);
				wimlib_free(esd);
				wimlib_free(wim);
				return;
			}
			SetReferenceFiles(src);
			wimlib_export_image(src, 3, esd, nullptr, nullptr, WIMLIB_EXPORT_FLAG_NO_NAMES);
			wimlib_export_image(wim, WIMLIB_ALL_IMAGES, esd, nullptr, nullptr, WIMLIB_EXPORT_FLAG_NO_NAMES);
			RegisterWIMProgressCallback(esd, &progctx, [&](UINT u)
				{
					cictx->State.SetWindowText(ResStrFormat(String_ExportingImageState, u));
					return !Cancel;
				});
			if (!Cancel)
				code = wimlib_write(esd, L"install.esd", WIMLIB_ALL_IMAGES, 0, get_num_threads());
			wimlib_free(esd);
			wimlib_free(wim);
			wimlib_free(src);
			DeleteFileW(L"install.wim");
			if (code != WIMLIB_ERR_SUCCESS)
			{
				WimlibError(code, true);
				return;
			}
			AppendText(L"\r\n");

			if (!cictx->IsDialog)
				DeleteDirectory(L"RefESDs");

			HANDLE hWim = WIMCreateFile(L"install.esd", WIM_GENERIC_READ | WIM_GENERIC_WRITE, OPEN_EXISTING, WIM_FLAG_ALLOW_LZMS, 0, nullptr);
			if (!hWim)
			{
				Error(true);
				return;
			}
			WIMSetTemporaryPath(hWim, ctx.PathTemp.c_str());
			WIMDeleteImage(hWim, 1);
			WIMCloseHandle(hWim);
		}

		PCWSTR pSource = Compression == WIMLIB_COMPRESSION_TYPE_LZMS ? L"install.esd" : L"install.wim";
		if (Cancel)
			return;

		if (!IsTargetEditionMissing(cictx->Editions, ctx))
		{
			DWORD dwIndex = 0;
			fs::path RegPath = ctx.PathTemp;
			RegPath /= L"Registries";
			std::error_code ec;
			fs::directory_iterator iter(RegPath, ec);
			if (!ec) for (auto& entry : iter) if (entry.is_directory())
			{
				auto name = entry.path().filename().wstring();
				if (!IsPureNumberString(name.c_str(), name.c_str() + name.length()))
					continue;
				dwIndex = wcstoul(name.c_str(), nullptr, 10);

				iter = fs::directory_iterator(entry.path(), ec);
				if (ec) continue;
				for (auto& entry : iter)
				{
					if (Cancel) return;
					if (!entry.is_directory()) continue;
					auto EditionId = entry.path().filename().wstring();

					PWSTR pszProductName = wcschr(const_cast<wchar_t*>(EditionId.c_str()), L'$');
					if (!pszProductName)
						continue;

					*pszProductName++ = 0;

					AppendText(GetString(String_ExportingEditionImageState));
					AppendText(pszProductName);
					AppendText(L"\r\n");

					WIMStruct* wim;
					int code = wimlib_open_wim(pSource, 0, &wim);
					if (code != WIMLIB_ERR_SUCCESS)
					{
						WimlibError(code, true);
						return;
					}

					auto Software = entry.path() / L"SOFTWARE";
					Software = fs::relative(Software, fs::current_path());
					wimlib_add_tree(wim, dwIndex, Software.c_str(), L"\\Windows\\System32\\config\\SOFTWARE", 0);
					auto System = entry.path() / L"SYSTEM";
					System = fs::relative(System, fs::current_path());
					wimlib_add_tree(wim, dwIndex, System.c_str(), L"\\Windows\\System32\\config\\SYSTEM", 0);
					wimlib_set_image_property(wim, dwIndex, L"WINDOWS/EDITIONID", EditionId.c_str());
					wimlib_set_image_name(wim, dwIndex, pszProductName);
					wimlib_set_image_descripton(wim, dwIndex, pszProductName);
					if (!Cancel)
						code = wimlib_overwrite(wim, 0, 0);
					else
						code = WIMLIB_ERR_ABORTED_BY_PROGRESS;
					wimlib_free(wim);
					if (code != WIMLIB_ERR_SUCCESS)
					{
						WimlibError(code, true);
						return;
					}

					hWim = WIMCreateFile(pSource, WIM_GENERIC_READ | WIM_GENERIC_WRITE, OPEN_EXISTING, WIM_FLAG_ALLOW_LZMS, 0, nullptr);
					if (!hWim)
					{
						Error(true);
						return;
					}
					WIMSetTemporaryPath(hWim, L"Temp");
					hImage = WIMLoadImage(hWim, dwIndex);
					if (!hImage)
					{
						Error(true);
						WIMCloseHandle(hWim);
						return;
					}
					if (!WIMExportImage(hImage, hWim, WIM_EXPORT_ALLOW_DUPLICATES))
					{
						Error(true);
						WIMCloseHandle(hImage);
						WIMCloseHandle(hWim);
						return;
					}
					WIMCloseHandle(hImage);
					WIMCloseHandle(hWim);

					DeleteDirectory(entry.path().c_str());
				}
			}

			if (Cancel)
				return;

			hWim = WIMCreateFile(pSource, WIM_GENERIC_READ | WIM_GENERIC_WRITE, OPEN_EXISTING, WIM_FLAG_ALLOW_LZMS, 0, nullptr);
			if (!hWim)
			{
				Error(true);
				return;
			}
			WIMSetTemporaryPath(hWim, L"Temp");
			for (DWORD i = dwIndex; i != 0; --i)
				WIMDeleteImage(hWim, i);
			WIMCloseHandle(hWim);
			DeleteDirectory(L"Registries\\");
		}

		if (Cancel)
			return;

		AppendText(GetString(String_ExportingImage));
		AppendText(pszDestinationImage);
		AppendText(L"...");
		int code = CopyImage(pSource, pszDestinationImage,
			[&](UINT u)
			{
				if (progctx.u == u)
					return true;
				progctx.u = u;
				cictx->State.SetWindowText(ResStrFormat(String_ExportingImageState, u));
				return !Cancel;
			});
		if (code != WIMLIB_ERR_SUCCESS)
		{
			WimlibError(code, true);
			return;
		}
		AppendText(GetString(String_Succeeded));
		DeleteFileW(pSource);
	}

	OnFinish(true);
	cictx.reset();
}
