#include "pch.h"
#include "UUPAssistant.h"
#include "BCD.h"
#include "DiskPartTable.h"
#include "Resources/resource.h"
#include "Xml.h"

#include <Shlwapi.h>
#include <wimgapi.h>
#include <wimlib.h>

#include <format>
#include <filesystem>
#include <thread>

using namespace std;
using namespace Lourdle::UIFramework;

import Constants;

bool GetDriveInfo(PCWSTR pPath, _PartInfo& PartInfo, _DiskInfo& DiskInfo)
{
	bool bGPT = true;
	HANDLE hDrive = CreateFileW(pPath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hDrive == INVALID_HANDLE_VALUE)
		throw GetLastError();
	else
	{
		DWORD cbReturned;
		PARTITION_INFORMATION_EX partitionInfo;
		DISK_GEOMETRY dg;

		constexpr DWORD cbBuffer = sizeof(DRIVE_LAYOUT_INFORMATION_EX) + 127 * sizeof(PARTITION_INFORMATION_EX);
		MyUniqueBuffer<PDRIVE_LAYOUT_INFORMATION_EX> driveLayout(cbBuffer);

		if (!DeviceIoControl(
			hDrive,
			IOCTL_DISK_GET_PARTITION_INFO_EX,
			nullptr,
			0,
			&partitionInfo,
			sizeof(partitionInfo),
			&cbReturned,
			nullptr
		) || !DeviceIoControl(
			hDrive,
			IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
			nullptr,
			0,
			driveLayout,
			cbBuffer,
			&cbReturned,
			nullptr
		)) {
			DWORD dwErrorCode = GetLastError();
			CloseHandle(hDrive);
			throw dwErrorCode;
		}

		if (partitionInfo.PartitionStyle == PARTITION_STYLE_GPT)
		{
			PartInfo.id = partitionInfo.Gpt.PartitionId;
			DiskInfo.id = driveLayout->Gpt.DiskId;
		}
		else
		{
			if (!DeviceIoControl(
				hDrive,
				IOCTL_DISK_GET_DRIVE_GEOMETRY,
				nullptr,
				0,
				&dg,
				sizeof(dg),
				&cbReturned,
				nullptr
			))
			{
				DWORD dwErrorCode = GetLastError();
				CloseHandle(hDrive);
				throw dwErrorCode;
			}
			PartInfo.ullOffset = partitionInfo.StartingOffset.QuadPart / dg.BytesPerSector;
			DiskInfo.Signature = driveLayout->Mbr.Signature;
			bGPT = false;
		}
		CloseHandle(hDrive);
	}

	return bGPT;
}

static DWORD CALLBACK MessageCallback(
	DWORD dwMessageId,
	WPARAM wParam,
	LPARAM lParam,
	InstallationProgress* p)
{
	if (dwMessageId == WIM_MSG_PROGRESS)
		p->StateStatic.SetWindowText(ResStrFormat(String_ExportingWinRe, static_cast<UINT>(wParam)).GetPointer());
	return WIM_MSG_SUCCESS;
}

static DWORD CALLBACK WIMMessageCallback4Rec(
	DWORD  dwMessageId,
	PCWSTR pszFullPath,
	PBOOL pfProcessFile,
	PBOOL pbApplied
)
{
	if (dwMessageId == WIM_MSG_PROCESS)
		if (*pbApplied)
			return WIM_MSG_ABORT_IMAGE;
		else if (_wcsnicmp(pszFullPath + wcslen(pszFullPath) - 9, L"\\Recovery", 9) == 0)
			*pbApplied = TRUE;
		else
			*pfProcessFile = FALSE;
	return WIM_MSG_SUCCESS;
}

static void AppendMessage(InstallationProgress* p, PCWSTR pszMessage)
{
	int len = p->StateDetail.GetWindowTextLength();
	p->StateDetail.SetSel(len, len);
	p->StateDetail.ReplaceSel(pszMessage);
}

static void AppendErrMsg(InstallationProgress* p)
{
	LPWSTR lpErrMsg;
	HMODULE hModule = GetModuleHandleW(L"wimgapi.dll");
	FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_FROM_HMODULE,
		hModule,
		GetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		reinterpret_cast<LPWSTR>(&lpErrMsg),
		0, nullptr);
	AppendMessage(p, lpErrMsg);
	LocalFree(lpErrMsg);
}

static bool CheckStatus(InstallationProgress* p, LSTATUS lStatus)
{
	if (lStatus != ERROR_SUCCESS)
	{
		SetLastError(lStatus);
		AppendErrMsg(p);
		return false;
	}
	return true;
}

void InstallThread(InstallationProgress* p)
{
	SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
	auto AppendText = [p](PCWSTR pstr)
		{
			AppendMessage(p, pstr);
		};
	auto AppendErrMsg = [p]()
		{
			::AppendErrMsg(p);
		};
	wstring Buffer;
	auto Flush = [&Buffer, AppendText]()
		{
			AppendText(Buffer.c_str());
			Buffer.clear();
		};
	auto CheckStatus = [p](LSTATUS lStatus)
		{
			return ::CheckStatus(p, lStatus);
		};

	if (p->Target.end()[-1] != '\\')
		p->Target += '\\';
	SetCurrentDirectoryW(p->ctx.PathUUP.c_str());
	WIMStruct* wim;
	int wec = wimlib_open_wim(p->ctx.TargetImageInfo.SystemESD.c_str(), 0, &wim);
	if (wec)
	{
		p->MessageBox(wimlib_get_error_string(static_cast<wimlib_error_code>(wec)), nullptr, MB_ICONERROR);
		ExitProcess(E_UNEXPECTED);
	}

	SetCurrentDirectoryW(p->ctx.PathTemp.c_str());
	SetReferenceFiles(wim);

	p->State = p->ApplyingImage;
	p->Invalidate(false);

	wec = ApplyWIMImage(wim, p->Target, 3, nullptr, AppendText,
		[p](PCWSTR String)
		{
			p->StateStatic.SetWindowText(String);
		});
	if (wec)
	{
		p->MessageBox(wimlib_get_error_string(static_cast<wimlib_error_code>(wec)), nullptr, MB_ICONERROR);
		wimlib_free(wim);
		ExitProcess(E_UNEXPECTED);
	}

	p->State = p->InstallingFeatures;
	p->Invalidate();
	AppendText(L"\r\n");
	if (p->InstDotNet3)
	{
		Buffer += GetString(String_ImageIndex);
		Buffer += L"1. ";
		Buffer += GetString(String_PreparingFile);
		Buffer += p->ctx.PathTemp;
		Buffer += L"sxs";
		Flush();
		PCWSTR Path = L"\\sources\\sxs";
		if (wimlib_extract_paths(wim, 1, p->ctx.PathTemp.c_str(), &Path, 1, WIMLIB_EXTRACT_FLAG_NO_PRESERVE_DIR_STRUCTURE) != 0)
		{
			Buffer += GetString(String_Failed);
			Buffer += L"\r\n";
			Flush();
			p->InstDotNet3 = false;
		}
		else
			AppendText(L"\r\n");
		p->StateStatic.SetWindowText(nullptr);
	}
	wimlib_free(wim);

	DeleteDirectory(L"RefESDs");

	do if (!p->DontInstWinRe)
	{
		Buffer = GetString(String_ImageIndex);
		Buffer += L"2. ";
		Flush();
		HANDLE hWim = WIMCreateFile((p->ctx.PathUUP + p->ctx.TargetImageInfo.SystemESD).c_str(), WIM_GENERIC_READ, WIM_OPEN_EXISTING, WIM_FLAG_ALLOW_LZMS, 0, nullptr);
		if (!hWim)
		{
		failure:
			AppendErrMsg();
			WIMCloseHandle(hWim);
			break;
		}
		CreateDirectoryW(L"Temp", nullptr);
		WIMSetTemporaryPath(hWim, L"Temp");
		String File = p->ReImagePart.dwPart != 0 ? GetPartitionFsPath(p->ReImagePart.dwDisk, p->ReImagePart.dwPart) : p->Target + L"Windows\\System32\\Recovery\\Winre.wim";
		if (p->ReImagePart.dwPart != 0)
		{
			BOOL pbApplied = FALSE;
			WIMRegisterMessageCallback(hWim, reinterpret_cast<FARPROC>(WIMMessageCallback4Rec), &pbApplied);
			HANDLE hImage = WIMLoadImage(hWim, 3);
			WIMApplyImage(hImage, File, 0);
			WIMCloseHandle(hImage);
			WIMUnregisterMessageCallback(hWim, reinterpret_cast<FARPROC>(WIMMessageCallback4Rec));

			filesystem::path Path(File.GetPointer());
			Path /= L"Recovery\\WindowsRE";

			if (!filesystem::create_directories(Path.parent_path()))
				goto failure;

			Path /= L"Winre.wim";

			HANDLE hFile = CreateFileW(Path.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (hFile != INVALID_HANDLE_VALUE)
			{
				FILE_BASIC_INFO fbi = { .FileAttributes = FILE_ATTRIBUTE_NORMAL };
				SetFileInformationByHandle(hFile, FileBasicInfo, &fbi, sizeof(fbi));
				NTSTATUS status = DeleteFileOnClose(hFile);
				CloseHandle(hFile);
				if (!NT_SUCCESS(status))
					goto failure;
			}
			else if (GetLastError() != ERROR_FILE_NOT_FOUND)
				goto failure;

			File = Path.c_str();
		}
		Buffer += GetString(String_ExportingImage);
		Buffer += File;
		Flush();
		HANDLE hWim2 = WIMCreateFile(File, WIM_GENERIC_WRITE | WIM_GENERIC_READ, WIM_CREATE_NEW, 0, WIM_COMPRESS_LZX, nullptr);
		if (!hWim2)
			goto failure;
		WIMRegisterMessageCallback(hWim2, reinterpret_cast<FARPROC>(MessageCallback), p);
		HANDLE hImage = WIMLoadImage(hWim, 2);
		WIMSetTemporaryPath(hWim2, L"Temp");
		if (!WIMExportImage(hImage, hWim2, 0))
			AppendErrMsg();
		else
			AppendText(L"\r\n");
		WIMCloseHandle(hWim2);
		WIMCloseHandle(hImage);
		WIMCloseHandle(hWim);
	} while (false);

	do if (p->ReImagePart.dwPart != 0)
	{
		p->StateStatic.SetWindowText(nullptr);
		AppendText(GetString(String_ConfiguringReAgent));

		HANDLE hFile = CreateFileW(p->Target + L"Windows\\System32\\Recovery\\ReAgent.xml", GENERIC_WRITE | GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			AppendErrMsg();
			break;
		}
		DWORD dwSize = GetFileSize(hFile, nullptr);
		string text;
		if (!ReadText(hFile, text, dwSize))
		{
		rea_failure:
			AppendErrMsg();
			CloseHandle(hFile);
			break;
		}

		rapidxml::xml_document<> doc;
		try
		{
			doc.parse<0>(const_cast<char*>(text.c_str()));
		}
		catch (rapidxml::parse_error)
		{
			SetLastError(ERROR_INVALID_DATA);
			goto rea_failure;
		}
		auto pRoot = doc.first_node();
		if (!pRoot)
		{
		not_found:
			SetLastError(ERROR_NOT_FOUND);
			goto rea_failure;
		}
		auto pImageLocation = pRoot->first_node("ImageLocation");
		if (pImageLocation)
		{
			auto attr = pImageLocation->first_attribute("path");
			if (!attr)
				goto not_found;
			attr->value("\\Recovery\\WindowsRE");
			if (p->RePartIsGPT)
			{
				String guid = GUID2String(p->RePartInfo.guid);
				string id(guid.begin(), guid.end());
				pImageLocation->append_attribute(doc.allocate_attribute("guid", doc.allocate_string(id.c_str())));
			}
			else
			{
				attr = pImageLocation->first_attribute("id");
				if (!attr)
					goto not_found;
				attr->value(doc.allocate_string(to_string(p->RePartInfo.id).c_str()));
			}
			attr = pImageLocation->first_attribute("offset");
			if (!attr)
				goto not_found;
			attr->value(doc.allocate_string(to_string(p->RePartInfo.offset).c_str()));
		}
		else
			goto not_found;

		SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);
		string xml = PrintXml(doc);
		if (!WriteFile(hFile, xml.c_str(), static_cast<DWORD>(xml.size()), nullptr, nullptr)
			|| !SetEndOfFile(hFile))
			AppendErrMsg();
		else
			AppendText(GetString(String_Succeeded));

		CloseHandle(hFile);
	} while (false);

	p->StateStatic.SetWindowText(nullptr);
	SetCurrentDirectoryW(p->ctx.PathTemp.c_str());
	do
	{
		DismWrapper dism(p->Target, p->ctx.PathTemp.c_str(), nullptr, AppendText);
		if (!dism.Session)
			break;
		dism.SetString = [p](PCWSTR String)
			{
				p->StateStatic.SetWindowText(String);
			};
		dism.MessageBox = [p](LPCTSTR lpText, LPCWSTR lpCaption, UINT uType)
			{
				return p->MessageBox(lpText, lpCaption, uType);
			};

		if (p->InstDotNet3)
			dism.EnableDotNetFx3(L"sxs");

		if (p->Edition != -1
			&& p->ctx.TargetImageInfo.UpgradableEditions[p->Edition] != p->ctx.TargetImageInfo.Edition
			&& dism.SetEdition(p->ctx.TargetImageInfo.UpgradableEditions[p->Edition].c_str()))
		{
			AppendText(GetString(String_Succeeded));
			DeleteFileW(p->Target + L"Windows\\" + p->ctx.TargetImageInfo.Edition + L".xml");
		}

		SetCurrentDirectoryW(p->ctx.PathTemp.c_str());
		DeleteDirectory(L"sxs");

		p->State = p->InstallingUpdates;
		p->Invalidate();
		switch (dism.AddUpdates(p->ctx))
		{
		case TRUE:
			goto InstallSoftware;
		case BYTE(-1):
			ExitProcess(E_UNEXPECTED);
		}
		break;

	InstallSoftware:
		p->State = p->InstallingSoftware;
		p->Invalidate();

		if (!dism.AddDrivers(p->ctx))
			break;

		if (!dism.AddApps(p->ctx))
			break;
	} while (false);

	if (p->State != p->InstallingSoftware)
	{
		p->State = p->InstallingSoftware;
		p->Invalidate();
	}
	
	do if (!AdjustPrivileges({ SE_TAKE_OWNERSHIP_NAME, SE_SECURITY_NAME }))
	{
		AppendErrMsg();
		goto EndInstallation;
	}
	else if (p->ctx.bInstallEdge)
	{
		auto EdgePath = p->ctx.PathUUP + L"Edge.wim";
		if (!PathFileExistsW(EdgePath.c_str()))
			break;

		AppendText(GetString(String_InstallingEdge));
		if (!InstallMicrosoftEdge(EdgePath.c_str(), p->Target, p->ctx.PathTemp.c_str(), p->ctx.TargetImageInfo.Arch))
		{
			AppendErrMsg();
			break;
		}
		AppendText(GetString(String_Succeeded));
	} while (false);

	p->State = p->ApplyingSettings;
	p->Invalidate();
	if (!p->Keys.empty())
	{
		p->StateStatic.SetWindowText(GetString(String_ApplyingRegSettings));
		if (p->Keys.empty())
			goto ApplyUnattend;

		RegistryHive hSystem(p->Target + L"Windows\\System32\\config\\SYSTEM", true);
		if (!hSystem)
			AppendErrMsg();

		RegistryHive hSoftware(p->Target + L"Windows\\System32\\config\\SOFTWARE", true);
		if (!hSoftware)
		{
			AppendErrMsg();
			if (!hSystem)
				goto ApplyUnattend;
		}

		for (const auto& i : p->Keys)
		{
			if (i.SYSTEM && !hSystem)
				continue;
			else if (!i.SYSTEM && !hSoftware)
				continue;

			Buffer = format(L"{}{}\\{} {}{}...", GetString(String_RegKey).GetPointer(), i.SYSTEM ? L"SYSTEM" : L"SOFTWARE", i.SubKey, GetString(String_RegValue).GetPointer(), i.ValueName.begin());
			Flush();
			auto ls = RegSetKeyValueW(i.SYSTEM ? hSystem : hSoftware, i.SubKey, i.ValueName, i.Type, i.Data.get(), i.Size);
			SetLastError(ls);
			AppendErrMsg();
		}
	}

ApplyUnattend:
	p->StateStatic.SetWindowText(nullptr);
	do if (!p->Unattend.empty())
	{
		filesystem::path File = p->Target.GetPointer();
		File /= L"Windows\\Panther\\Unattend.xml";
		Buffer = GetString(String_ApplyUnattend);
		Buffer += File;
		Buffer += L"...";

		if (!filesystem::create_directories(File.parent_path()))
		{
			AppendErrMsg();
			break;
		}

		Flush();
		HANDLE hFile = CreateFileW(File.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			AppendErrMsg();
			break;
		}
		bool result = WriteFile(hFile, p->Unattend.c_str(), static_cast<DWORD>(p->Unattend.size()), nullptr, nullptr);
		if (!result)
		{
			AppendErrMsg();
			DeleteFileOnClose(hFile);
		}
		else
			AppendText(GetString(String_Succeeded));
		CloseHandle(hFile);
	} while (false);

	if (!p->BootPath.Empty())
	{
		p->StateStatic.SetWindowText(GetString(String_RebuildingBoot));
		AppendText(GetString(String_LoadingBootInfo));
		HKEY hBCD = nullptr;

		do
		{
			HANDLE hPartition = CreateFileW(p->BootPath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
			if (hPartition == INVALID_HANDLE_VALUE)
			{
				AppendErrMsg();
				goto EndInstallation;
			}
			DWORD len = GetFinalPathNameByHandleW(hPartition, nullptr, 0, VOLUME_NAME_NT);
			if (len > 0)
			{
				wstring bcd(len - 1, 0);
				GetFinalPathNameByHandleW(hPartition, const_cast<PWSTR>(bcd.c_str()), static_cast<DWORD>(bcd.size()) + 1, VOLUME_NAME_NT);
				CloseHandle(hPartition);
				if (p->EFI)
					bcd += L"EFI\\Microsoft\\";
				bcd += L"Boot\\BCD";
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
					if (_wcsicmp(bcd.c_str(), Data.c_str()) == 0)
					{
						if (_wcsnicmp(Value.c_str(), L"\\REGISTRY\\MACHINE\\", 18) == 0)
						{
							LSTATUS l = RegCreateKeyExW(HKEY_LOCAL_MACHINE, Value.c_str() + 18, 0, nullptr, REG_OPTION_BACKUP_RESTORE, KEY_ALL_ACCESS | ACCESS_SYSTEM_SECURITY, nullptr, &hBCD, nullptr);
							if (l != ERROR_SUCCESS)
							{
								RegCloseKey(hHivelist);
								SetLastError(l);
								AppendErrMsg();
								goto EndInstallation;
							}
							break;
						}
						else
						{
							RegCloseKey(hHivelist);
							SetLastError(ERROR_BAD_ENVIRONMENT);
							AppendErrMsg();
							goto EndInstallation;
						}
					}
					cchName = len;
				}
				RegCloseKey(hHivelist);
			}
			else
			{
				AppendErrMsg();
				CloseHandle(hPartition);
			}
		} while (false);

		Buffer = L"\r\n";
		Buffer += GetString(String_CopyingBootFiles);
		Flush();

		wstring Path = p->BootPath.GetPointer();
		auto MkDir = [&Path, &AppendErrMsg]()
			{
				if (!CreateDirectoryW(Path.c_str(), nullptr)
					&& GetLastError() != ERROR_ALREADY_EXISTS)
				{
					AppendErrMsg();
					return false;
				}
				return true;
			};
		if (p->EFI)
		{
			Path += L"EFI\\";
			if (!MkDir())
			{
			mdfailure:
				if (hBCD)
					RegCloseKey(hBCD);
				goto EndInstallation;
			}
		}

		String RecoveryBCD;

		size_t size = Path.size();
		Path += L"Boot\\";
		if (!MkDir())
			goto mdfailure;
		else
		{
			if (p->EFI)
			{
				Path.resize(size);
				Path += L"Microsoft\\";
				if (!MkDir())
					goto mdfailure;
				Path += L"Recovery\\";
				if (!MkDir())
					goto mdfailure;
				RecoveryBCD = Path + L"BCD";
				Path.resize(Path.size() - 9);
				Path += L"Boot\\";
				size = Path.size();
				if (!MkDir())
					goto mdfailure;
			}
			wstring SrcPath = p->Target.GetPointer();
			SrcPath += L"Windows\\Boot\\";
			size_t srcSize = SrcPath.size();
			SrcPath += p->EFI ? L"EFI\\" : L"PCAT\\";
			if (!CopyDirectory(SrcPath.c_str(), Path.c_str()))
			{
				AppendErrMsg();
				if (hBCD)
					RegCloseKey(hBCD);
				goto EndInstallation;
			}

			if (p->EFI && p->ctx.TargetImageInfo.bHasBootEX)
			{
				SrcPath.pop_back();
				SrcPath += L"_EX\\";
				if (!CopyDirectory(SrcPath.c_str(), Path.c_str()))
				{
					AppendErrMsg();
					if (hBCD)
						RegCloseKey(hBCD);
					goto EndInstallation;
				}
			}
			Path += L"BOOTSTAT.DAT";
			HANDLE hFile = CreateFileW(Path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN, nullptr);
			if (hFile != INVALID_HANDLE_VALUE)
			{
				SetFilePointer(hFile, 64 * 1024, nullptr, FILE_BEGIN);
				SetEndOfFile(hFile);
				CloseHandle(hFile);
			}
			Path.resize(Path.size() - 12);
			SrcPath.resize(srcSize);
			Path += L"Resources\\";
			if (!MkDir())
				goto mdfailure;
			SrcPath += L"Resources\\";
			if (!CopyDirectory(SrcPath.c_str(), Path.c_str()))
			{
				AppendErrMsg();
				if (hBCD)
					RegCloseKey(hBCD);
				goto EndInstallation;
			}

			SrcPath.resize(srcSize);
			Path.resize(Path.size() - 10);
			Path += L"Fonts\\";
			if (!MkDir())
				goto mdfailure;
			SrcPath += L"Fonts\\";
			if (!CopyDirectory(SrcPath.c_str(), Path.c_str()))
			{
				AppendErrMsg();
				if (hBCD)
					RegCloseKey(hBCD);
				goto EndInstallation;
			}
			Path.resize(size);

			if (!p->EFI)
			{
				SrcPath = Path;
				SrcPath += L"Boot\\";
				Path += L"bootmgr";
				SrcPath += L"bootmgr";
				SetFileAttributesW(Path.c_str(), FILE_ATTRIBUTE_NORMAL);
				MoveFileExW(SrcPath.c_str(), Path.c_str(), MOVEFILE_REPLACE_EXISTING);
				SetFileAttributesW(Path.c_str(), FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN);
				Path.resize(Path.size() - 7);
				SrcPath.resize(SrcPath.size() - 7);
				Path += L"BOOTNXT";
				SrcPath += L"BOOTNXT";
				SetFileAttributesW(Path.c_str(), FILE_ATTRIBUTE_NORMAL);
				MoveFileExW(SrcPath.c_str(), Path.c_str(), MOVEFILE_REPLACE_EXISTING);
				SetFileAttributesW(Path.c_str(), FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN);
				Path.resize(size);
				Path += L"Boot\\";
				SetFileAttributesW(Path.c_str(), FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN);
			}
			else
			{
				if (p->ctx.TargetImageInfo.bHasBootEX)
				{
					SrcPath.pop_back();
					SrcPath += L"_EX\\";
					Path += L"Fonts\\";
					if (!CopyDirectory(SrcPath.c_str(), Path.c_str()))
					{
						AppendErrMsg();
						if (hBCD)
							RegCloseKey(hBCD);
						goto EndInstallation;
					}
					Path.resize(Path.size() - 6);
					std::error_code ec;
					for (const auto& entry : std::filesystem::recursive_directory_iterator(Path, ec))
					{
						if (entry.is_regular_file(ec))
						{
							wstring filename = entry.path().filename().wstring();
							if (filename.find(L"_EX") != wstring::npos)
							{
								wstring newName = filename;
								size_t pos = newName.find(L"_EX");
								if (pos != wstring::npos)
								{
									newName.erase(pos, 3);
									auto targetPath = entry.path().parent_path() / newName;
									SetFileAttributesW(targetPath.c_str(), FILE_ATTRIBUTE_NORMAL);
									MoveFileExW(entry.path().c_str(), targetPath.c_str(), MOVEFILE_REPLACE_EXISTING);
								}
							}
						}
					}
				}

				wstring EfiBoot(Path.begin(), Path.end() - 15);
				EfiBoot += L"Boot\\";
				if (p->ctx.TargetImageInfo.Arch == PROCESSOR_ARCHITECTURE_ARM64)
					EfiBoot += L"bootaa64.efi";
				else if (p->ctx.TargetImageInfo.Arch == PROCESSOR_ARCHITECTURE_INTEL)
					EfiBoot += L"bootia32.efi";
				else if (p->ctx.TargetImageInfo.Arch == PROCESSOR_ARCHITECTURE_AMD64)
					EfiBoot += L"bootx64.efi";
				Path += L"bootmgfw.efi";
				SetFileAttributesW(EfiBoot.c_str(), FILE_ATTRIBUTE_NORMAL);
				CopyFileW(Path.c_str(), EfiBoot.c_str(), FALSE);
				Path.resize(size);
			}
		}

		Buffer = L"\r\n";
		Buffer += GetString(String_ConfiguringBCD);
		Flush();

		RegistryHive BcdHive;
		if (!hBCD)
		{
			Path += L"BCD";
			if (!BcdHive.Reset(Path.c_str(), true))
			{
				AppendErrMsg();
				goto EndInstallation;
			}
			hBCD = BcdHive;
		}

		Path = p->Target;
		Path += L"Windows\\System32\\config\\BCD-Template";
		auto Template = p->ctx.PathTemp + L"BCD-Template";
		if (!CopyFileW(Path.c_str(), Template.c_str(), FALSE))
		{
			AppendErrMsg();
			if (!BcdHive)
				RegCloseKey(hBCD);
			goto EndInstallation;
		}

		DWORD dwError;
		BCD::BCD bcd(hBCD, Template.c_str(), dwError,
			[&](BCD::BCD& bcd)
			{
				_DiskInfo DiskInfo;
				_PartInfo PartInfo;
				bool bGPT;
				try
				{
					PWCH pch = p->BootPath.GetPointer() + p->BootPath.GetLength() - 1;
					if (*pch == '\\')
						*pch = 0;
					bGPT = GetDriveInfo(p->BootPath, PartInfo, DiskInfo);
				}
				catch (long ErrorCode)
				{
					dwError = ErrorCode;
					bcd.Finalize();
					return;
				}

				BCD::DeviceLocator Locator(DiskInfo, PartInfo, bGPT);
				CheckStatus(bcd.SetAppDevice(Locator));
				CheckStatus(bcd.SetPreferedLocale(p->ctx.TargetImageInfo.Lang.c_str()));
				if (p->EFI)
				{
					CheckStatus(bcd.SetAppPath(L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi"));
					bcd.CreateRecoveryBCD(RecoveryBCD, p->ctx.TargetImageInfo.Lang.c_str(), Locator);
				}
				else
					CheckStatus(bcd.SetAppPath(L"\\Boot\\bootmgr"));

				Path.resize(Path.size() - 19);
				Path += p->ctx.TargetImageInfo.Lang;
				Path += L"\\bootstr.dll.mui";
				HMODULE hModule = LoadLibraryExW(Path.c_str(), nullptr, LOAD_LIBRARY_AS_IMAGE_RESOURCE);
				PWSTR pMemTestDescription = nullptr;
				FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS, hModule, 2, 0, reinterpret_cast<PWSTR>(&pMemTestDescription), 0, nullptr);
				FreeLibrary(hModule);
				BCD::Object MemTest = bcd.CreateMemTestObject();
				if (MemTest)
				{
					CheckStatus(MemTest.SetPreferedLocale(p->ctx.TargetImageInfo.Lang.c_str()));
					if (pMemTestDescription)
					{
						CheckStatus(MemTest.SetDescription(pMemTestDescription));
						LocalFree(pMemTestDescription);
					}
					CheckStatus(MemTest.SetAppDevice(Locator));
					if (p->EFI)
						CheckStatus(MemTest.SetAppPath(L"\\EFI\\Microsoft\\Boot\\memtest.efi"));
					else
						CheckStatus(MemTest.SetAppPath(L"\\Boot\\memtest.exe"));
				}
				else
					AppendErrMsg();
			});
		if (dwError)
		{
			AppendErrMsg();
			if (!BcdHive)
				RegCloseKey(hBCD);
			goto EndInstallation;
		}

		if (dwError != ERROR_SUCCESS)
		{
			if (!BcdHive)
				RegCloseKey(hBCD);
			SetLastError(dwError);
			AppendErrMsg();
			goto EndInstallation;
		}
		using namespace BCD;

		Path.resize(2);
		Path.insert(0, L"\\\\.\\");
		_PartInfo PartInfo;
		_DiskInfo DiskInfo;
		try
		{
			bool bGPT = GetDriveInfo(Path.c_str(), PartInfo, DiskInfo);
			DeviceLocator Locator(DiskInfo, PartInfo, bGPT, p->BootVHD);
			vector<Lourdle::UIFramework::String> v;
			bcd.GetDisplayedObjects(v);
			wstring Name = p->ctx.TargetImageInfo.Name;
			for (auto i = Name.c_str() + Name.size() - 1;; --i)
				if (*i >= '0' && *i <= '9')
				{
					Name.resize(i - Name.c_str() + 1);
					break;
				}

			if (!v.empty())
				for (const auto& i : v)
				{
					Object AppObj = bcd.OpenObject(i);
					if (AppObj)
					{
						if (!AppObj.IsAppObject()
							|| !AppObj.IsSpecificAppDevice(Locator)
							|| !AppObj.IsSpecificSystemDevice(Locator))
							continue;
						auto AppPath = AppObj.GetAppPath();
						if (p->EFI && AppPath.CompareCaseInsensitive(L"\\Windows\\system32\\winload.efi")
							|| !p->EFI && AppPath.CompareCaseInsensitive(L"\\Windows\\system32\\winload.exe"))
						{
							AppObj.Close();
							bcd.DeleteApplicationObject(i);
						}
					}
				}

			GUID guid;
			CoCreateGuid(&guid);
			Object Obj = bcd.CreateApplicationObject(guid);
			if (Obj)
			{
				CheckStatus(Obj.SetDescription(Name.c_str()));
				CheckStatus(Obj.SetPreferedLocale(p->ctx.TargetImageInfo.Lang.c_str()));
				if (p->EFI)
					CheckStatus(Obj.SetAppPath(L"\\Windows\\system32\\winload.efi"));
				else
					CheckStatus(Obj.SetAppPath(L"\\Windows\\system32\\winload.exe"));
				CheckStatus(Obj.SetSystemDevice(Locator));
				CheckStatus(Obj.SetAppDevice(Locator));
				CheckStatus(bcd.AppendDisplayedObject(guid));
				CheckStatus(bcd.SetDefaultObject(guid));
				Obj = bcd.CreateResumeObject(guid);
				if (Obj)
				{
					CheckStatus(Obj.SetPreferedLocale(p->ctx.TargetImageInfo.Lang.c_str()));
					CheckStatus(Obj.SetAppDevice(Locator));
					if (p->EFI)
						CheckStatus(Obj.SetAppPath(L"\\Windows\\system32\\winresume.efi"));
					else
						CheckStatus(Obj.SetAppPath(L"\\Windows\\system32\\winresume.exe"));
					CheckStatus(bcd.SetResumeObject(guid));
				}
				else
					AppendErrMsg();
			}
			else
				AppendErrMsg();
		}
		catch (long ErrorCode)
		{
			SetLastError(ErrorCode);
			AppendErrMsg();
		}
		bcd.Finalize();
		if (!BcdHive)
			RegCloseKey(hBCD);
	}

EndInstallation:
	if (p->DeleteLetterAfterInstallation)
		DeleteVolumeMountPointW(p->Target);
	p->State = -1;
	p->Invalidate();
	p->StateStatic.SetWindowText(nullptr);
	p->PostMessage(UupAssistantMsg::Installer_ShowPostInstallDialog, 0, 0);
}
