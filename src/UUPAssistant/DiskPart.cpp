#include "pch.h"
#include "UUPAssistant.h"
#include "Resources/resource.h"
#include "DiskPartTable.h"

#include <Shlwapi.h>
#include <initguid.h>
#include <vds.h>
#include <comdef.h>
#include <wil/com.h>

#include <algorithm>
#include <thread>
#include <memory>

using namespace std;
using namespace Lourdle::UIFramework;
using namespace wil;


_COM_SMARTPTR_TYPEDEF(IVdsServiceLoader, IID_IVdsServiceLoader);
_COM_SMARTPTR_TYPEDEF(IVdsService, IID_IVdsService);
_COM_SMARTPTR_TYPEDEF(IEnumVdsObject, IID_IEnumVdsObject);
_COM_SMARTPTR_TYPEDEF(IVdsProvider, IID_IVdsProvider);
_COM_SMARTPTR_TYPEDEF(IVdsSwProvider, IID_IVdsSwProvider);
_COM_SMARTPTR_TYPEDEF(IVdsVdProvider, IID_IVdsVdProvider);
_COM_SMARTPTR_TYPEDEF(IVdsPack, IID_IVdsPack);
_COM_SMARTPTR_TYPEDEF(IVdsDisk, IID_IVdsDisk);
_COM_SMARTPTR_TYPEDEF(IVdsAdvancedDisk, IID_IVdsAdvancedDisk);
_COM_SMARTPTR_TYPEDEF(IVdsAdvancedDisk2, IID_IVdsAdvancedDisk2);
_COM_SMARTPTR_TYPEDEF(IVdsDiskPartitionMF, IID_IVdsDiskPartitionMF);
_COM_SMARTPTR_TYPEDEF(IVdsVolume, IID_IVdsVolume);
_COM_SMARTPTR_TYPEDEF(IVdsVolume2, IID_IVdsVolume2);
_COM_SMARTPTR_TYPEDEF(IVdsVolumeMF, IID_IVdsVolumeMF);

struct ComError
{
	HRESULT hResult;
	int nLine;
};

inline
static void CheckHresult(HRESULT hr, int line)
{
	if (FAILED(hr))
		throw ComError({ hr, line });
}
#define CHECK(hr) CheckHresult(hr, __LINE__)

inline
static PCWSTR VdsFsTypeToString(VDS_FILE_SYSTEM_TYPE vfst)
{
	switch (vfst)
	{
	case VDS_FST_RAW:
		return L"RAW";
	case VDS_FST_FAT:
		return L"FAT";
	case VDS_FST_FAT32:
		return L"FAT32";
	case VDS_FST_NTFS:
		return L"NTFS";
	case VDS_FST_CDFS:
		return L"CDFS";
	case VDS_FST_UDF:
		return L"UDF";
	case VDS_FST_EXFAT:
		return L"exFAT";
	case VDS_FST_CSVFS:
		return L"CsvFS";
	case VDS_FST_REFS:
		return L"ReFS";
	default:
		return L"Unknown";
	}
}


static bool SetPart(PCWSTR pDisk, const GUID& DiskGUID, const GUID& PartitionGUID, bool Set, const GUID& TypeGUID, PCWSTR Name)
{
	HANDLE hDisk = CreateFileW(pDisk, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hDisk == INVALID_HANDLE_VALUE)
		return false;
	DISK_GEOMETRY DiskGeometry;
	DWORD cbReturned;
	if (!DeviceIoControl(
		hDisk,
		IOCTL_DISK_GET_DRIVE_GEOMETRY,
		nullptr,
		0,
		&DiskGeometry,
		sizeof(DISK_GEOMETRY),
		&cbReturned,
		nullptr
	))
	{
		CloseHandle(hDisk);
		return false;
	}
	std::unique_ptr<BYTE[]> Sector(new BYTE[DiskGeometry.BytesPerSector]);
	auto MoveToLBA = [hDisk, &DiskGeometry](ULONGLONG ullLBA)->bool
		{
			LARGE_INTEGER pos;
			pos.QuadPart = ullLBA * DiskGeometry.BytesPerSector;
			return SetFilePointerEx(hDisk, pos, nullptr, FILE_BEGIN);
		};

	auto ReadSector = [&]()->bool
		{
			DWORD nr;
			ReadFile(hDisk, Sector.get(), DiskGeometry.BytesPerSector, &nr, nullptr);
			DWORD dwError = GetLastError();
			SetFilePointer(hDisk, -LONG(DiskGeometry.BytesPerSector), nullptr, FILE_CURRENT);
			SetLastError(dwError);
			return nr == DiskGeometry.BytesPerSector;
		};

	auto WriteSector = [&]()->bool
		{
			DWORD nr;
			WriteFile(hDisk, Sector.get(), DiskGeometry.BytesPerSector, &nr, nullptr);
			DWORD dwError = GetLastError();
			SetFilePointer(hDisk, -LONG(DiskGeometry.BytesPerSector), nullptr, FILE_CURRENT);
			SetLastError(dwError);
			return nr == DiskGeometry.BytesPerSector;
		};

	DWORD Table[256] = {};

	for (DWORD i = 0, CRC32 = 0; i < 256; i++)
	{
		CRC32 = i;
		for (char j = 0; j < 8; j++)
		{
			if (CRC32 & 1)
				CRC32 = (CRC32 >> 1) ^ 0xEDB88320;
			else
				CRC32 >>= 1;
		}
		Table[i] = CRC32;
	}

	auto CalcCRC32 = [&](DWORD InitialValue, PVOID pData, DWORD cbData)
		{
			for (DWORD i = 0; i != cbData; ++i)
				InitialValue = ((InitialValue >> 8) & 0xFFFFFF) ^ Table[(InitialValue ^ reinterpret_cast<PBYTE>(pData)[i]) & 0xFF];
			return ~InitialValue;
		};

	ULONGLONG ullBackupHeaderLBA, ullPrimaryHeaderLBA, ullPartitionTableStartingLBA;
	DWORD nMaxNumberOfPartitions;
	auto CheckHeader = [&]()
		{
			MoveToLBA(1);
			ReadSector();

			GPTHeader* pHeader = reinterpret_cast<GPTHeader*>(Sector.get());
			if (memcmp(pHeader->Signature, "EFI PART", 8)
				|| pHeader->dwVersion != 0x00010000
				|| pHeader->cbHeader != sizeof(GPTHeader))
			{
				SetLastError(ERROR_BAD_FORMAT);
				return false;
			}
			if (pHeader->DiskGUID != DiskGUID)
			{
				SetLastError(ERROR_BAD_DEVICE);
				return false;
			}
			ullBackupHeaderLBA = pHeader->ullBackupHeaderLBA;
			ullPrimaryHeaderLBA = pHeader->ullPrimaryHeaderLBA;
			ullPartitionTableStartingLBA = pHeader->ullPartitionTableStartingLBA;
			nMaxNumberOfPartitions = pHeader->nMaxNumberOfPartitions;
			return true;
		};

	if (!CheckHeader())
	{
		CloseHandle(hDisk);
		return false;
	}

	auto SetIt = [&](PartitionEntry& Entry)
		{
			Entry.TypeGUID = Set ? TypeGUID : BDP;
			if (Name)
			{
				ZeroMemory(Entry.Name, sizeof(Entry.Name));
				wcscpy_s(Entry.Name, Set ? Name : L"Basic data partition");
			}
			WriteSector();
		};

	const DWORD EnteiesPerSector = DiskGeometry.BytesPerSector / sizeof(PartitionEntry);
	for (DWORD i = 0; i != nMaxNumberOfPartitions / EnteiesPerSector; ++i)
	{
		MoveToLBA(ullPartitionTableStartingLBA + i);
		ReadSector();
		PartitionEntry* pEntries = reinterpret_cast<PartitionEntry*>(Sector.get());
		for (BYTE j = 0; j != EnteiesPerSector; ++j)
		{
			if (pEntries[j].PartitionGUID != PartitionGUID)
				continue;
			SetIt(pEntries[j]);
			goto SetHeaderCRC;
		}
	}
	MoveToLBA(ullPartitionTableStartingLBA + nMaxNumberOfPartitions);
	ReadSector();
	for (DWORD i = 0; i != nMaxNumberOfPartitions % EnteiesPerSector; ++i)
	{
		PartitionEntry* pEntries = reinterpret_cast<PartitionEntry*>(Sector.get());
		if (pEntries[i].PartitionGUID != PartitionGUID)
			continue;
		SetIt(pEntries[i]);
		goto SetHeaderCRC;
	}

	CloseHandle(hDisk);
	SetLastError(ERROR_NOT_FOUND);
	return false;

SetHeaderCRC:
	DWORD CRC32 = 0;
	for (DWORD i = 0; i != nMaxNumberOfPartitions / EnteiesPerSector; ++i)
	{
		MoveToLBA(ullPartitionTableStartingLBA + i);
		ReadSector();
		CRC32 = CalcCRC32(~CRC32, Sector.get(), DiskGeometry.BytesPerSector);
	}
	MoveToLBA(ullPartitionTableStartingLBA + nMaxNumberOfPartitions);
	ReadSector();
	for (DWORD i = 0; i != nMaxNumberOfPartitions % EnteiesPerSector; ++i)
		CRC32 = CalcCRC32(~CRC32, reinterpret_cast<PartitionEntry*>(Sector.get()) + i, sizeof(PartitionEntry));
	MoveToLBA(1);
	ReadSector();
	auto pHeader = reinterpret_cast<GPTHeader*>(Sector.get());
	pHeader->TableCRCChecksum = CRC32;
	pHeader->HeaderCRCChecksum = 0;
	pHeader->HeaderCRCChecksum = CalcCRC32(-1, pHeader, sizeof(GPTHeader));
	WriteSector();
	FlushFileBuffers(hDisk);
	DeviceIoControl(hDisk, IOCTL_DISK_UPDATE_PROPERTIES, nullptr, 0, nullptr, 0, &cbReturned, nullptr);
	CloseHandle(hDisk);
	return true;
}

inline
static bool SetESP(PCWSTR pDisk, const GUID& DiskGUID, const GUID& PartitionGUID, bool Set)
{
	return SetPart(pDisk, DiskGUID, PartitionGUID, Set, ESP, L"EFI system partition");
}

inline
static bool SetRecPart(PCWSTR pDisk, const GUID& DiskGUID, const GUID& PartitionGUID, bool Set)
{
	return SetPart(pDisk, DiskGUID, PartitionGUID, Set, MSRecovery, nullptr);
}

static WCHAR GetSystemPartitions(wstring& PageFilePartitions)
{
	CHAR szDir[MAX_PATH];
	GetSystemDirectoryA(szDir, MAX_PATH);
	DWORD dwSize{};
	if (RegGetValueA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management", "ExistingPageFiles", RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &dwSize) != ERROR_SUCCESS)
		return *szDir;

	MyUniqueBuffer<PCHAR> Buffer = dwSize;
	RegGetValueA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management", "ExistingPageFiles", RRF_RT_REG_MULTI_SZ, nullptr, Buffer, &dwSize);
	for (DWORD i = 1; i < dwSize; ++i)
		if (Buffer[i] == ':')
			PageFilePartitions.push_back(Buffer[i - 1]);
	return *szDir;
}


static void DiskPartThread(InstallationWizard* p)
{
	MSG msg;
	PeekMessageW(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);

	try
	{
		CHECK(CoInitialize(nullptr));
		IVdsSwProviderPtr pProv;
		IVdsVdProviderPtr pVdProv;
		{
			IVdsServicePtr pService;
			IVdsServiceLoaderPtr Loader;
			CHECK(CoCreateInstance(CLSID_VdsLoader, NULL, CLSCTX_LOCAL_SERVER, IID_IVdsServiceLoader, reinterpret_cast<LPVOID*>(&Loader)));
			CHECK(Loader->LoadService(nullptr, &pService));

			IEnumVdsObjectPtr pEnum;
			CHECK(pService->QueryProviders(VDS_QUERY_SOFTWARE_PROVIDERS, &pEnum));
			ULONG cFetched;
			for (com_ptr<IUnknown> pUnknown; pEnum->Next(1, &pUnknown, &cFetched) == S_OK; pUnknown->Release())
			{
				com_ptr<IVdsProvider> pProvider;
				pUnknown->QueryInterface(&pProvider);
				VDS_PROVIDER_PROP vpp;
				CHECK(pProvider->GetProperties(&vpp));
				CoTaskMemFree(vpp.pwszName);
				if (vpp.ulFlags & VDS_PF_DYNAMIC)
					continue;
				else if (SUCCEEDED(pUnknown->QueryInterface(&pProv)))
					break;
			}

			pEnum->Release();
			CHECK(pService->QueryProviders(VDS_QUERY_VIRTUALDISK_PROVIDERS, &pEnum));
			IUnknown* pUnknown;
			CHECK(pEnum->Next(1, &pUnknown, &cFetched));
			pVdProv = pUnknown;
		}

		auto GetInfo = [=]()
			{
				IEnumVdsObjectPtr pEnumVdsPack;
				CHECK(pProv->QueryPacks(&pEnumVdsPack));
				IUnknown* pUnknown;
				ULONG cFetched;
				auto& v = p->PartInfoVector;
				v.clear();
				wstring PageDrives;
				WCHAR cSysDrive = GetSystemPartitions(PageDrives);
				while (pEnumVdsPack->Next(1, &pUnknown, &cFetched) == S_OK)
				{
					IVdsPackPtr pPack;
					pUnknown->QueryInterface(&pPack);
					pUnknown->Release();
					IEnumVdsObjectPtr pEnumDisk;
					if (pPack->QueryDisks(&pEnumDisk) == S_OK)
						while (pEnumDisk->Next(1, &pUnknown, &cFetched) == S_OK)
						{
							IVdsAdvancedDiskPtr pAdvDisk;
							pUnknown->QueryInterface(&pAdvDisk);
							pUnknown->Release();
							IVdsDiskPartitionMFPtr pPart;
							IVdsDiskPtr pDisk;
							pAdvDisk->QueryInterface(&pDisk);
							VDS_DISK_PROP vdp;
							CHECK(pDisk->GetProperties(&vdp));
							if (vdp.health != VDS_H_HEALTHY
								|| vdp.status != VDS_DS_ONLINE
								|| vdp.dwMediaType != FixedMedia && vdp.dwMediaType != RemovableMedia)
							{
								CoTaskMemFree(vdp.pwszAdaptorName);
								CoTaskMemFree(vdp.pwszDevicePath);
								CoTaskMemFree(vdp.pwszDiskAddress);
								CoTaskMemFree(vdp.pwszFriendlyName);
								CoTaskMemFree(vdp.pwszName);
								continue;
							}
							bool bRemovable = vdp.dwMediaType == RemovableMedia;
							if (!bRemovable)
								switch (vdp.BusType)
								{
								case VDSBusTypeAta:case VDSBusTypeAtapi:case VDSBusTypeSata:case VDSBusTypeSas:
								case VDSBusTypeScsi:case VDSBusTypeMmc:case VDSBusTypeRAID:case VDSBusTypeVirtual:
								case VDSBusTypeUfs:case VDSBusTypeNVMe:case VDSBusTypeFileBackedVirtual:
									break;
								default:
									bRemovable = true;
								}
							auto di = make_shared<InstallationWizard::PartVolInfo::DiskInfo>();
							di->AdaptorName = vdp.pwszAdaptorName;
							di->FriendlyName = vdp.pwszFriendlyName;
							di->Name = vdp.pwszName;
							di->DevicePath = vdp.pwszDevicePath;
							di->ullSize = vdp.ullSize;
							di->ulBytesPerSector = vdp.ulBytesPerSector;

							com_ptr<IVdsVDisk> pVdsVDisk;
							if (SUCCEEDED(pVdProv->GetVDiskFromDisk(pDisk, &pVdsVDisk)))
							{
								VDS_VDISK_PROPERTIES vvp;
								CHECK(pVdsVDisk->GetProperties(&vvp));
								di->VHDPath = vvp.pPath;
								CoTaskMemFree(vvp.pParentPath);
								CoTaskMemFree(vvp.pPath);
								CoTaskMemFree(vvp.pDeviceName);
							}
							CoTaskMemFree(vdp.pwszAdaptorName);
							CoTaskMemFree(vdp.pwszDevicePath);
							CoTaskMemFree(vdp.pwszDiskAddress);
							CoTaskMemFree(vdp.pwszFriendlyName);
							CoTaskMemFree(vdp.pwszName);
							if (vdp.PartitionStyle == VDS_PARTITION_STYLE_MBR)
								di->Signature = vdp.dwSignature;
							else
								di->id = vdp.DiskGuid;

							pDisk->GetProperties(&vdp);
							VDS_PARTITION_PROP* pvpp;
							LONG n, nx;
							VDS_DISK_EXTENT* pvdxs;
							CHECK(pDisk->QueryExtents(&pvdxs, &nx));
							CHECK(pAdvDisk->QueryPartitions(&pvpp, &n));
							unique_ptr<VDS_PARTITION_PROP[], decltype(CoTaskMemFree)*> vpp(pvpp, CoTaskMemFree);
							unique_ptr<VDS_DISK_EXTENT[], decltype(CoTaskMemFree)*> vdxs(pvdxs, CoTaskMemFree);
							pAdvDisk->QueryInterface(&pPart);
							MBR_t MBR = {};
							for (LONG i = 0; i != n; ++i)
							{
								auto& prop = pvpp[i];
								VDS_FILE_SYSTEM_PROP vfsp;
								v.resize(v.size() + 1);
								auto& pi = *v.rbegin();
								pi.Removable = bRemovable;
								pi.GPT = prop.PartitionStyle == VDS_PST_GPT;
								if (pi.GPT)
								{
									pi.PartTableInfo.GPT.id = prop.Gpt.partitionId;
									pi.PartTableInfo.GPT.Type = prop.Gpt.partitionType;
									wcsncpy_s(pi.PartTableInfo.GPT.PartName, prop.Gpt.name, 36);
								}
								else
								{
									pi.PartTableInfo.MBR.bootIndicator = prop.Mbr.bootIndicator;
									pi.PartTableInfo.MBR.Type = prop.Mbr.partitionType;

									if (!MBR.dwSignature)
									{
										HANDLE hDrive = CreateFileW(di->Name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
										if (hDrive == INVALID_HANDLE_VALUE)
											throw GetLastError();
										MyUniqueBuffer<PBYTE> Sector = vdp.ulBytesPerSector;
										if (!ReadFile(hDrive, Sector, vdp.ulBytesPerSector, nullptr, nullptr))
										{
											DWORD dwErrorCode = GetLastError();
											CloseHandle(hDrive);
											throw dwErrorCode;
										}
										memcpy(&MBR, Sector, sizeof(MBR));
										CloseHandle(hDrive);
									}
									pi.PartTableInfo.MBR.Primary = false;
									for (const auto& i : MBR.Partitions)
										if (ULONGLONG(i.StartingLBA) * vdp.ulBytesPerSector == prop.ullOffset)
											pi.PartTableInfo.MBR.Primary = true;
								}
								pi.ullOffset = prop.ullOffset;
								pi.ullSize = prop.ullSize;
								pi.Number = prop.ulPartitionNumber;
								pi.pDiskInfo = di;

								if (SUCCEEDED(pPart->GetPartitionFileSystemProperties(prop.ullOffset, &vfsp)))
								{
									if (FAILED(pAdvDisk->GetDriveLetter(prop.ullOffset, &pi.DosDeviceLetter)))
										pi.DosDeviceLetter = ' ';
								}
								else
								{
									IEnumVdsObjectPtr pEnumVdsVolume;
									CHECK(pPack->QueryVolumes(&pEnumVdsVolume));
									IUnknown* p;
									ULONG cf;
									while (pEnumVdsVolume->Next(1, &p, &cf) == S_OK)
									{
										IVdsVolumePtr pVdsVolume;
										p->QueryInterface(&pVdsVolume);
										p->Release();
										VDS_VOLUME_PROP vvp;
										CHECK(pVdsVolume->GetProperties(&vvp));
										CoTaskMemFree(vvp.pwszName);
										for (LONG i = 0; i != nx; ++i)
											if (pvdxs[i].ullOffset == prop.ullOffset && pvdxs[i].volumeId == vvp.id)
											{
												pi.Page = (vvp.ulFlags & VDS_VF_PAGEFILE) == VDS_VF_PAGEFILE;
												IVdsVolumeMFPtr pVdsVolumeMF;
												pVdsVolume->QueryInterface(&pVdsVolumeMF);
												CHECK(pVdsVolumeMF->GetFileSystemProperties(&vfsp));
												LPWSTR* pPaths;
												LONG np;
												if (SUCCEEDED(pVdsVolumeMF->QueryAccessPaths(&pPaths, &np)))
												{
													for (LONG i = 0; i != np; ++i)
														if (pPaths[i][1] == ':' && pPaths[i][2] == '\\' && pPaths[i][3] == 0)
														{
															pi.DosDeviceLetter = pPaths[i][0];
															break;
														}
													CoTaskMemFree(pPaths);
												}
												goto Mainline;
											}
									}
									vfsp = {};
								}
							Mainline:
								if (vfsp.pwszLabel)
								{
									pi.Label = vfsp.pwszLabel;
									CoTaskMemFree(vfsp.pwszLabel);
								}
								pi.ullFreeSpace = vfsp.ullAvailableAllocationUnits * vfsp.ulAllocationUnitSize;
								pi.Filesystem = VdsFsTypeToString(vfsp.type);
								if (pi.DosDeviceLetter == cSysDrive)
									pi.System = true;
								if (pi.DosDeviceLetter != ' ')
									if (PageDrives.find(pi.DosDeviceLetter) != wstring::npos)
										pi.Page = true;
								WCHAR SystemPath[MAX_PATH];
								GetSystemDirectoryW(SystemPath, MAX_PATH);
								if (SystemPath[0] == pi.DosDeviceLetter)
									pi.System = true;
							}

							sort(v.end() - n, v.end(),
								[](const InstallationWizard::PartVolInfo& v1, const InstallationWizard::PartVolInfo& v2)
								{
									return  v1.ullOffset < v2.ullOffset;
								});
						}
				}
			};

		auto SetList = [p]()
			{
				auto& v = p->PartInfoVector;
				auto& list = p->PartList;
				list.DeleteAllItems();
				for (const auto& part : v)
				{
					if (!part.GPT && (part.PartTableInfo.MBR.Type == 0x05 || part.PartTableInfo.MBR.Type == 0x0F))
						continue;

					int i = list.InsertItem();
					list.SetItemText(i, 7, part.pDiskInfo->Name);
					WCHAR str[16];
					_itow_s(part.Number, str, 10);
					list.SetItemText(i, 9, str);
					if (IsCharAlphaW(part.DosDeviceLetter))
					{
						str[0] = part.DosDeviceLetter;
						str[1] = ':';
						str[2] = 0;
					}
					else
						str[0] = 0;
					list.SetItemText(i, 0, str);
					StrFormatByteSizeW(part.ullSize, str, 16);
					list.SetItemText(i, 3, str);
					StrFormatByteSizeW(part.ullOffset, str, 16);
					list.SetItemText(i, 5, str);
					if (part.Filesystem != L"RAW" && part.Filesystem != L"Unknown")
						StrFormatByteSizeW(part.ullFreeSpace, str, 16);
					else
						str[0] = 0;
					list.SetItemText(i, 4, str);
					list.SetItemText(i, 6, part.GPT ? L"GPT" : L"MBR");
					list.SetItemText(i, 1, part.Filesystem);
					list.SetItemText(i, 2, part.Label);
					LoadStringW(nullptr, String_False - part.Removable, str, 16);
					list.SetItemText(i, 8, str);
				}

				if (p->State == p->UpdatingMultiRequests)
				{
					p->State = p->Updating;
					MSG msg;
					if (!PeekMessageW(&msg, nullptr, p->Msg_Refresh, p->Msg_Refresh, PM_NOREMOVE))
						PostThreadMessageW(GetCurrentThreadId(), p->Msg_Refresh, 0, 0);
				}
				else
				{
					if (!p->DontBoot.GetCheck())
						p->SetBootPartList();
					p->SendMessage(WM_NULL, 0, 0);
					p->Refresh.EnableWindow(true);
					p->EnableSetBootPart();
					p->PartList.EnableWindow(true);
					RECT rc;
					p->Refresh.GetWindowRect(&rc);
					ScreenToClient(p->GetHandle(), reinterpret_cast<PPOINT>(&rc));
					ScreenToClient(p->GetHandle(), reinterpret_cast<PPOINT>(&rc) + 1);
					rc.right = rc.left - 1;
					rc.left = 0;
					p->InvalidateRect(&rc);
					p->State = p->Clear;
					if (p->ctx.TargetImageInfo.bHasBootEX && p->BootMode.GetWindowText() == L"UEFI")
						p->ExtraBootOption.EnableWindow(true);
					p->Ring.ShowWindowAsync(SW_HIDE);
				}
			};

		IVdsAdvancedDiskPtr pBootDisk;
		ULONGLONG ullBootPartOffset;
		msg.message = p->Msg_Refresh;
		do
		{
			if (msg.message == p->Msg_Quit)
				break;

			switch (msg.message)
			{
			case p->Msg_Refresh:
				GetInfo();
				if (PeekMessageW(&msg, nullptr, p->Msg_Quit, p->Msg_Quit, PM_REMOVE))
					break;
				SetList();
				break;
			case p->Msg_SetESP: case p->Msg_SetRec:
			{
				auto SetPart = msg.message == p->Msg_SetESP ? SetESP : SetRecPart;
				unique_ptr<SetPartStruct[]> ps(reinterpret_cast<SetPartStruct*>(msg.wParam));
				for (size_t i = 0; i != msg.lParam; ++i)
					if (!SetPart(ps[i].pDiskInfo->Name, ps[i].pDiskInfo->id, ps[i].id, ps[i].Set))
					{
						p->ErrorMessageBox();
						break;
					}
				p->Refresh.PostCommand();
			}
			break;
			case p->Msg_Pause:
				if (msg.wParam)
					SetEvent(reinterpret_cast<HANDLE>(msg.wParam));
				GetMessageW(&msg, nullptr, p->Msg_Continue, p->Msg_Continue);
				break;
			case p->Msg_Format:
			{
				auto pvi = reinterpret_cast<InstallationWizard::PartVolInfo*>(msg.wParam);
				auto pboot = reinterpret_cast<InstallationWizard::PartVolInfo*>(msg.lParam);
				bool found = false;
				String BootVHD = pvi->pDiskInfo->VHDPath;

				IEnumVdsObjectPtr pEnumVdsPack;
				CHECK(pProv->QueryPacks(&pEnumVdsPack));
				IUnknown* pUnknown;
				ULONG cFetched;
				while (pEnumVdsPack->Next(1, &pUnknown, &cFetched) == S_OK)
				{
					IVdsPackPtr pPack;
					pUnknown->QueryInterface(&pPack);
					pUnknown->Release();
					IEnumVdsObjectPtr pEnumDisk;
					pPack->QueryDisks(&pEnumDisk);
					while (pEnumDisk->Next(1, &pUnknown, &cFetched) == S_OK)
					{
						IVdsAdvancedDiskPtr pVdsAdvDisk = pUnknown;
						pUnknown->Release();
						IVdsDiskPartitionMFPtr pPart;
						IVdsDiskPtr pDisk = pVdsAdvDisk;
						VDS_DISK_PROP vdp;
						CHECK(pDisk->GetProperties(&vdp));
						CoTaskMemFree(vdp.pwszAdaptorName);
						CoTaskMemFree(vdp.pwszDevicePath);
						CoTaskMemFree(vdp.pwszDiskAddress);
						CoTaskMemFree(vdp.pwszFriendlyName);
						CoTaskMemFree(vdp.pwszName);

						if (!(vdp.PartitionStyle == VDS_PST_GPT && pvi->GPT && vdp.DiskGuid == pvi->pDiskInfo->id)
							&& !(vdp.PartitionStyle == VDS_PST_MBR && !pvi->GPT && vdp.dwSignature == pvi->pDiskInfo->Signature))
							if (!pboot
								|| !(vdp.PartitionStyle == VDS_PST_GPT && pboot->GPT && vdp.DiskGuid == pboot->pDiskInfo->id)
								&& !(vdp.PartitionStyle == VDS_PST_MBR && !pboot->GPT && vdp.dwSignature == pboot->pDiskInfo->Signature))
								continue;

						VDS_PARTITION_PROP* pvpp;
						LONG n;
						CHECK(pVdsAdvDisk->QueryPartitions(&pvpp, &n));
						unique_ptr<VDS_PARTITION_PROP[], decltype(CoTaskMemFree)*> vpp(pvpp, CoTaskMemFree);
						for (LONG i = 0; i != n; ++i)
						{
							if (pboot
								&& (vdp.PartitionStyle == VDS_PST_GPT && pboot->GPT && vdp.DiskGuid == pboot->pDiskInfo->id
									|| vdp.PartitionStyle == VDS_PST_MBR && !pboot->GPT && vdp.dwSignature == pboot->pDiskInfo->Signature))
							{
								if (pboot->GPT && pboot->PartTableInfo.GPT.id == pvpp[i].Gpt.partitionId
									|| !pboot->GPT && pboot->ullOffset == pvpp[i].ullOffset)
								{
									pBootDisk = pVdsAdvDisk;
									ullBootPartOffset = pvpp[i].ullOffset;
									if (found)
									{
										WCHAR Boot[4];
										if (SUCCEEDED(pBootDisk->GetDriveLetter(ullBootPartOffset, Boot))
											&& Boot[0] != ' ')
										{
											Boot[1] = ':';
											Boot[2] = '\\';
											Boot[3] = 0;
											p->BootPath = Boot;
										}
									}
									if (found)
										goto EndFormatting;
								}
							}

							if (vdp.PartitionStyle == VDS_PST_GPT && pvi->GPT && vdp.DiskGuid == pvi->pDiskInfo->id
								|| vdp.PartitionStyle == VDS_PST_MBR && !pvi->GPT && vdp.dwSignature == pvi->pDiskInfo->Signature)
								if (pvi->GPT && pvi->PartTableInfo.GPT.id == pvpp[i].Gpt.partitionId
									|| !pvi->GPT && pvi->ullOffset == pvpp[i].ullOffset)
									goto Format;

						ContinueScan:
							continue;

						Format:
							if (pvi->GPT)
								SetESP(pvi->pDiskInfo->Name, pvi->pDiskInfo->id, pvi->PartTableInfo.GPT.id, false);
							com_ptr<IVdsAsync> pVdsAsync;
							bool formatted = false;
							HRESULT hr = pVdsAdvDisk->FormatPartition(pvpp[i].ullOffset, p->FormatOptions.ReFS ? VDS_FST_REFS : VDS_FST_NTFS, p->FormatOptions.Label.GetPointer(), 4096, p->FormatOptions.bForce, p->FormatOptions.bQuick, FALSE, &pVdsAsync);
							if (hr != VDS_E_PARTITION_NOT_OEM)
							{
								CheckHresult(hr, __LINE__ - 2);
								HRESULT hResult;
								VDS_ASYNC_OUTPUT vao;
								CHECK(pVdsAsync->Wait(
									&hResult, &vao));
								CHECK(hResult);
								formatted = true;
							}
							IEnumVdsObjectPtr pEnumVdsVol;
							CHECK(pPack->QueryVolumes(&pEnumVdsVol));
							VDS_DISK_EXTENT* pvdxs;
							LONG nx;
							CHECK(pDisk->QueryExtents(&pvdxs, &nx));
							unique_ptr<VDS_DISK_EXTENT[], decltype(CoTaskMemFree)*> vdxs(pvdxs, CoTaskMemFree);
							while (pEnumVdsVol->Next(1, &pUnknown, &cFetched) == S_OK)
							{
								IVdsVolumePtr pVdsVolume = pUnknown;
								pUnknown->Release();
								VDS_VOLUME_PROP vvp;
								CHECK(pVdsVolume->GetProperties(&vvp));
								CoTaskMemFree(vvp.pwszName);
								for (LONG i = 0; i != nx; ++i)
									if (pvdxs[i].ullOffset == pvi->ullOffset && vvp.id == pvdxs[i].volumeId)
									{
										IVdsVolumeMFPtr pVdsVolumeMF = pVdsVolume;
										if (!formatted)
										{
											CHECK(pVdsVolumeMF->Format(p->FormatOptions.ReFS ? VDS_FST_REFS : VDS_FST_NTFS, p->FormatOptions.Label.GetPointer(), 4096, p->FormatOptions.bForce, p->FormatOptions.bQuick, FALSE, &pVdsAsync));
											HRESULT hResult;
											VDS_ASYNC_OUTPUT vao;
											CHECK(pVdsAsync->Wait(
												&hResult, &vao));
											CHECK(hResult);
										}
										LPWSTR* pPaths;
										if (SUCCEEDED(pVdsVolumeMF->QueryAccessPaths(&pPaths, &nx)))
										{
											for (LONG i = 0; i != nx; ++i)
												if (pPaths[i][1] == ':' && pPaths[i][2] == '\\' && pPaths[i][3] == 0)
												{
													p->Target = pPaths[i];
													CoTaskMemFree(pPaths);
													found = true;
													if (pBootDisk || !pboot)
														goto EndFormatting;
													goto ContinueScan;
												}
											CoTaskMemFree(pPaths);
										}
										WCHAR Path[4] = { p->cLetter, ':', '\\' };
										CHECK(pVdsVolumeMF->AddAccessPath(Path));
										found = true;
										p->Target = Path;
										if (pBootDisk
											&& SUCCEEDED(pBootDisk->GetDriveLetter(ullBootPartOffset, Path))
											&& Path[0] != ' ')
											p->BootPath = Path;
										if (pBootDisk || !pboot)
											goto EndFormatting;
										goto ContinueScan;
									}
							}
						}
					}
				}

				if ((pBootDisk || !pboot)
					&& found)
					goto EndFormatting;

				p->MessageBox(GetString(String_UnableToSelectPart), GetString(String_Notice), MB_ICONERROR);
				p->RefreshInfo();
				p->DontBoot.EnableWindow(true);
				continue;

			EndFormatting:
				if (pboot)
				{
					IVdsDiskPtr pVdsDisk = pBootDisk;
					VDS_PARTITION_PROP vpp;
					VDS_DISK_PROP vdp;
					CHECK(pBootDisk->GetPartitionProperties(ullBootPartOffset, &vpp));
					CHECK(pVdsDisk->GetProperties(&vdp));
					if (p->BootMode.GetWindowText() == L"Legacy BIOS" && p->ExtraBootOption.GetCheck() == BST_CHECKED && !SetPBRAndDiskMBR(vdp.pwszName, vdp.dwSignature, vdp.ulBytesPerSector, vpp.ulPartitionNumber, vpp.ullOffset))
						p->ErrorMessageBox();
					String Path = GetPartitionFsPath(vdp.pwszName, vpp.ulPartitionNumber);
					CoTaskMemFree(vdp.pwszAdaptorName);
					CoTaskMemFree(vdp.pwszDevicePath);
					CoTaskMemFree(vdp.pwszDiskAddress);
					CoTaskMemFree(vdp.pwszFriendlyName);
					CoTaskMemFree(vdp.pwszName);
					p->BootPath = Path;
					if (p->BootFromVHD.IsWindowVisible())
						p->BootVHD = move(BootVHD);
				}

				if (p->ReImagePart.dwPart == -1)
				{
					p->ReImagePart.dwPart = pvi->Number;
					p->ReImagePart.dwDisk = wcstoul(pvi->pDiskInfo->Name.GetPointer() + 17, nullptr, 10);
				}
				p->State = p->Done;
				p->PostMessage(WM_CLOSE, 0, 0);
				goto End;
			}
			break;
			}
		} while (GetMessageW(&msg, nullptr, 0, 0));
	}
	catch (ComError ce)
	{
		p->MessageBox(
			ResStrFormat(String_ComError, ce.nLine, ce.hResult),
			nullptr, MB_ICONERROR);

		goto EnterCustomMode;
	}
	catch (DWORD dwErrorCode)
	{
		SetLastError(dwErrorCode);
		p->ErrorMessageBox();

		goto EnterCustomMode;
	}

End:
	CoUninitialize();
	return;

EnterCustomMode:
	CoUninitialize();
	p->DontBoot.EnableWindow(true);
	p->Ring.ShowWindow(SW_HIDE);
	p->Ring.Stop();
	if (p->ctx.TargetImageInfo.bHasBootEX && p->BootMode.GetWindowText() == L"UEFI")
		p->ExtraBootOption.EnableWindow(true);

	thread(&InstallationWizard::SwitchInstallationMethod, p).detach();
}

static void SetWindowSizeByClientSize(InstallationWizard* p, int w, int h)
{
	p->State = p->AdjustingWindow;
	RECT rectwnd, rect;
	p->GetWindowRect(&rectwnd);
	p->GetClientRect(&rect);
	const auto& width = rect.right;
	const auto& height = rect.bottom;
	rectwnd.left -= w / 2 - width / 2;
	rectwnd.right += w / 2 - width / 2;
	rectwnd.top -= h / 2 - height / 2;
	rectwnd.bottom += h / 2 - height / 2;
	p->GetWindowRect(&rect);
	int px = GetFontSize();
	p->AddExtendedWindowStyle(WS_EX_LAYERED);
	for (BYTE i = 1; i <= 16; ++i)
	{
		if (i / 2)
			p->SetLayeredWindowAttributes(0, 255 - 25 * (4 - abs(4 - i / 2)), LWA_ALPHA);
		p->MoveWindow(rect.left + (rectwnd.left - rect.left) * i / 16, rect.top + (rectwnd.top - rect.top) * i / 16, rect.right - rect.left + (rectwnd.right - rectwnd.left - rect.right + rect.left) * i / 16, rect.bottom - rect.top + (rectwnd.bottom - rectwnd.top - rect.bottom + rect.top) * i / 16);
		RECT Rect;
		p->GetClientRect(&Rect);
		p->Next.MoveWindow(Rect.right - px * 8, Rect.bottom - px * 3, px * 6, px * 2);
		if (p->GetWindowThreadProcessId(nullptr) == GetCurrentThreadId())
			EnterMessageLoopTimeout(2);
		else
			Sleep(3);
	}
	p->RemoveExtendedWindowStyle(WS_EX_LAYERED);
}

void InstallationWizard::SwitchInstallationMethod()
{
	MENUITEMINFOW mii = {
		.cbSize = sizeof(mii),
		.fMask = MIIM_STATE
	};
	auto pxUnit = GetFontSize(nullptr);
	SIZE size;
	DontBoot.GetIdealSize(&size);
	Next.EnableWindow(false);
	BootPartList.DeleteAllItems();

	if (idThread)
	{
		HANDLE hThread = OpenThread(SYNCHRONIZE, FALSE, idThread);
		if (hThread)
		{
			PostThreadMessageW(idThread, Msg_Quit, 0, 0);
			WaitForSingleObject(hThread, INFINITE);
			CloseHandle(hThread);
		}
		idThread = 0;
		mii.fState = MFS_CHECKED;
		Detail.SetWindowText(nullptr);

		Detail.ShowWindow(SW_HIDE);
		Refresh.ShowWindow(SW_HIDE);
		DontBoot.ShowWindow(SW_HIDE);
		BootMode.ShowWindow(SW_HIDE);
		BootPartList.ShowWindow(SW_HIDE);
		BootFromVHD.ShowWindow(SW_HIDE);
		PartList.ShowWindow(SW_HIDE);
		PartList.DeleteAllItems();
		ExtraBootOption.ShowWindow(SW_HIDE);
		SetWindowSizeByClientSize(this, pxUnit * 40, pxUnit * 25);
		TargetPath.ShowWindow(SW_SHOW);
		Boot.ShowWindow(SW_SHOW);
		BootMode.MoveWindow(pxUnit * 10, static_cast<int>(pxUnit * 13.5), pxUnit * 9, pxUnit * 2, false);
		BootMode.ShowWindow(SW_SHOW);
		DontBoot.MoveWindow(pxUnit * 2, pxUnit * 17 - size.cy / 2, size.cx, size.cy, false);
		DontBoot.ShowWindow(SW_SHOW);
		ExtraBootOption.GetIdealSize(&size);
		ExtraBootOption.MoveWindow(pxUnit * 2, pxUnit * 20 - size.cy / 2, size.cx, size.cy, false);
		if (BootMode.GetWindowText() != L"UEFI")
			ExtraBootOption.ShowWindow(SW_HIDE);
		BrowseTargetPath.ShowWindow(SW_SHOW);
		BrowseBootPath.ShowWindow(SW_SHOW);
		EditTextChanged();
		RemoveMenu(GetSubMenu(GetMenu(hWnd), 0), 8, MF_BYPOSITION);
		State = Clear;
	}
	else
	{
		mii.fState = MFS_UNCHECKED;
		DontBoot.ShowWindow(SW_HIDE);
		BootMode.ShowWindow(SW_HIDE);
		ExtraBootOption.ShowWindow(SW_HIDE);
		if (BootMode.GetWindowText() == L"UEFI")
		{
			ExtraBootOption.SetWindowText(GetString(String_UseBootEX));
			ExtraBootOption.EnableWindow(ctx.TargetImageInfo.bHasBootEX);
			ToolTip.Activate();
		}
		else
		{
			ExtraBootOption.SetWindowText(GetString(String_SetSystemPartAsBootPart));
			ExtraBootOption.SetCheck(BST_UNCHECKED);
			ToolTip.Activate(false);
		}
		if (!Detail.IsWindowVisible())
		{
			TargetPath.ShowWindow(SW_HIDE);
			Boot.ShowWindow(SW_HIDE);
			BrowseTargetPath.ShowWindow(SW_HIDE);
			BrowseBootPath.ShowWindow(SW_HIDE);
			ExtraBootOption.ShowWindow(SW_HIDE);
			SetWindowSizeByClientSize(this, pxUnit * 80, pxUnit * 50);
			if (DontBoot.GetCheck() == BST_UNCHECKED)
				BootPartList.ShowWindow(SW_SHOW);
			Detail.ShowWindow(SW_SHOW);
			Refresh.ShowWindow(SW_SHOW);
		}
		if (DontBoot.GetCheck() == BST_UNCHECKED)
			ExtraBootOption.ShowWindow(SW_SHOW);
		PartList.ShowWindow(SW_SHOW);
		ExtraBootOption.GetIdealSize(&size);
		ExtraBootOption.MoveWindow(pxUnit * 41, pxUnit * 28, size.cx, size.cy);
		BootMode.MoveWindow(pxUnit * 41, pxUnit * 25, pxUnit * 9, pxUnit * 2, false);
		BootMode.ShowWindow(SW_SHOW);
		DontBoot.GetIdealSize(&size);
		DontBoot.MoveWindow(pxUnit * 52, pxUnit * 26 - size.cy / 2, size.cx, size.cy, false);
		DontBoot.ShowWindow(SW_SHOW);

		WORD id = Random();
		AppendMenuW(GetSubMenu(GetMenu(hWnd), 0), 0, id, GetString(String_FormatOptions));
		RegisterCommand(&InstallationWizard::SetFormatOptions, nullptr, id, 0);
		State = Updating;
		Ring.ShowWindow(SW_SHOW);
		Ring.Start();

		thread t(DiskPartThread, this);
		idThread = GetThreadId(t.native_handle());
		t.detach();
	}
	SetMenuItemInfoW(GetSubMenu(GetMenu(hWnd), 0), 7, TRUE, &mii);
	RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
}

void InstallationWizard::SetBootPartList()
{
	if (State == UpdatingMultiRequests || !idThread || !BootPartList.IsWindowVisible())
		return;

	BootPartList.DeleteColumn(5);
	BootPartList.DeleteAllItems();

	int n = 0;
	if (BootMode.GetWindowText() == L"UEFI" && DontBoot.GetCheck() == BST_UNCHECKED)
	{
		for (const auto& i : PartInfoVector)
			if (i.GPT
				&& (i.Filesystem == L"FAT32" || i.Filesystem == L"FAT")
				&& i.PartTableInfo.GPT.Type == ESP)
			{
				BootPartList.InsertItem();
				BootPartList.SetItemText(n, 3, i.pDiskInfo->Name);
				WCHAR buf[16];
				_itow_s(i.Number, buf, 10);
				BootPartList.SetItemText(n, 4, buf);
				StrFormatByteSizeW(i.ullSize, buf, 16);
				BootPartList.SetItemText(n, 2, buf);
				if (IsCharAlphaW(i.DosDeviceLetter))
				{
					buf[0] = i.DosDeviceLetter;
					buf[1] = ':';
					buf[2] = 0;
					BootPartList.SetItemText(n, 0, buf);
				}
				BootPartList.SetItemText(n, 1, i.Label);
				++n;
			}
	}
	else if (BootMode.GetWindowText() == L"Legacy BIOS")
	{
		int index = PartList.GetSelectionMark();
		if (index != -1)
		{
			for (int j = 0; index != j; ++j)
				if (!PartInfoVector[j].GPT && (PartInfoVector[j].PartTableInfo.MBR.Type == 0x05 || PartInfoVector[j].PartTableInfo.MBR.Type == 0x0F))
					++index;
			if (PartInfoVector[index].GPT)
			{
				ExtraBootOption.EnableWindow(false);
				Next.EnableWindow(false);
			}
		}
		else
		{
			ExtraBootOption.EnableWindow(false);
			BootPartList.ShowWindow(SW_SHOW);
			Next.EnableWindow(false);
		}
		
		BootPartList.InsertColumn(GetString(String_Active), GetFontSize() * 3, 5);
		for (const auto& i : PartInfoVector)
			if (!i.GPT
				&& (i.Filesystem == L"FAT32" || i.Filesystem == L"FAT" || i.Filesystem == L"exFAT" || i.Filesystem == L"NTFS"))
			{
				BootPartList.InsertItem();
				BootPartList.SetItemText(n, 3, i.pDiskInfo->Name);
				WCHAR buf[16];
				_itow_s(i.Number, buf, 10);
				BootPartList.SetItemText(n, 4, buf);
				StrFormatByteSizeW(i.ullSize, buf, 16);
				BootPartList.SetItemText(n, 2, buf);
				if (IsCharAlphaW(i.DosDeviceLetter))
				{
					buf[0] = i.DosDeviceLetter;
					buf[1] = ':';
					buf[2] = 0;
					BootPartList.SetItemText(n, 0, buf);
				}
				BootPartList.SetItemText(n, 1, i.Label);
				BootPartList.SetItemText(n, 5, GetString(String_False - i.PartTableInfo.MBR.bootIndicator));
				++n;
			}
	}
}


static void Thread2(InstallationWizard* p)
{
	MSG msg;
	PeekMessageW(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);

	try
	{
		CHECK(CoInitialize(nullptr));
		IVdsSwProviderPtr pProv;
		{
			IVdsServicePtr pService;
			IVdsServiceLoaderPtr Loader;
			CHECK(CoCreateInstance(CLSID_VdsLoader, NULL, CLSCTX_LOCAL_SERVER, IID_IVdsServiceLoader, reinterpret_cast<LPVOID*>(&Loader)));
			CHECK(Loader->LoadService(nullptr, &pService));

			IEnumVdsObjectPtr pEnum;
			CHECK(pService->QueryProviders(VDS_QUERY_SOFTWARE_PROVIDERS, &pEnum));
			ULONG cFetched;
			for (com_ptr<IUnknown> pUnknown; pEnum->Next(1, &pUnknown, &cFetched) == S_OK; pUnknown->Release())
			{
				com_ptr<IVdsProvider> pProvider;
				pUnknown->QueryInterface(&pProvider);
				VDS_PROVIDER_PROP vpp;
				CHECK(pProvider->GetProperties(&vpp));
				CoTaskMemFree(vpp.pwszName);
				if (vpp.ulFlags & VDS_PF_DYNAMIC && SUCCEEDED(pUnknown->QueryInterface(&pProv)))
					break;
			}
		}

		while (GetMessageW(&msg, nullptr, 0, 0))
		{
			if (msg.message == p->Msg_Refresh)
			{
				p->AssigningLetterDlg.Volumes.clear();
				IEnumVdsObjectPtr pEnumPack;
				CHECK(pProv->QueryPacks(&pEnumPack));
				IUnknown* pUnknown;
				ULONG cFetched;
				auto& v = p->AssigningLetterDlg.Volumes;
				while (pEnumPack->Next(1, &pUnknown, &cFetched) == S_OK)
				{
					IVdsPackPtr pPack = pUnknown;
					pUnknown->Release();
					IEnumVdsObjectPtr pEnum;
					CHECK(pPack->QueryVolumes(&pEnum));
					while (pEnum->Next(1, &pUnknown, &cFetched) == S_OK)
					{
						IVdsVolume2Ptr pVdsVolume = pUnknown;
						pUnknown->Release();
						VDS_VOLUME_PROP2 vvp;
						CHECK(pVdsVolume->GetProperties2(&vvp));
						CoTaskMemFree(vvp.pwszName);
						v.resize(v.size() + 1);
						auto& vi = *v.rbegin();
						vi.ullSize = vvp.ullSize;
						ULONGLONG ull = *reinterpret_cast<ULONGLONG*>(vvp.pUniqueId);
						vi.id = *reinterpret_cast<GUID*>(vvp.pUniqueId + 8);
						CoTaskMemFree(vvp.pUniqueId);
						switch (vvp.type)
						{
						case VDS_VT_SIMPLE:
							vi.Type = GetString(String_SimpleVolume);
							break;
						case VDS_VT_SPAN:
							vi.Type = GetString(String_SpanVolume);
							break;
						case VDS_VT_STRIPE:
							vi.Type = GetString(String_StripeVolume);
							break;
						case VDS_VT_MIRROR:
							vi.Type = GetString(String_MirrorVolume);
							break;
						case VDS_VT_PARITY:
							vi.Type = GetString(String_ParityVolume);
							break;
						default:
							vi.Type = L"Unknown";
						}

						IVdsVolumeMFPtr pVdsVolumeMF = pVdsVolume;
						VDS_FILE_SYSTEM_PROP vfsp;
						CHECK(pVdsVolumeMF->GetFileSystemProperties(&vfsp));
						vi.Filesystem = VdsFsTypeToString(vfsp.type);
						if (vfsp.pwszLabel)
						{
							vi.Label = vfsp.pwszLabel;
							CoTaskMemFree(vfsp.pwszLabel);
						}

						PWSTR* pPaths;
						LONG n;
						if (SUCCEEDED(pVdsVolumeMF->QueryAccessPaths(&pPaths, &n)))
						{
							for (LONG i = 0; i != n; ++i)
								if (pPaths[i][1] == ':' && pPaths[i][2] == '\\' && pPaths[i][3] == '\0')
								{
									vi.Letter = pPaths[i][0];
									break;
								}
							CoTaskMemFree(pPaths);
						}
					}
				}

				if (PeekMessageW(&msg, nullptr, p->Msg_Refresh, p->Msg_Refresh, PM_NOREMOVE))
					continue;

				HANDLE hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
				PostThreadMessageW(p->idThread, p->Msg_Pause, reinterpret_cast<WPARAM>(hEvent), 0);
				WaitForSingleObject(hEvent, INFINITE);
				CloseHandle(hEvent);

				auto& list = p->AssigningLetterDlg.PartVolList;
				list.DeleteAllItems();
				for (auto& i : p->PartInfoVector)
				{
					if (i.Removable
						|| !i.GPT && (i.PartTableInfo.MBR.Type == 0x05 || i.PartTableInfo.MBR.Type == 0x0F))
						continue;

					WCHAR buf[16] = {};
					int index = list.InsertItem();
					for (const auto& j : p->LetterInfoVector)
						if (j.ByGUID && i.GPT && j.id == i.PartTableInfo.GPT.id
							|| !j.ByGUID && !i.GPT && j.MBR.Signature == i.pDiskInfo->Signature && j.MBR.ullOffset == i.ullOffset / i.pDiskInfo->ulBytesPerSector)
						{
							buf[0] = j.cLetter;
							list.SetItemText(index, 0, buf);
							break;
						}

					buf[0] = i.DosDeviceLetter;
					list.SetItemText(index, 1, buf);
					list.SetItemText(index, 2, i.Label.GetPointer());
					StrFormatByteSizeW(i.ullSize, buf, 16);
					list.SetItemText(index, 3, buf);
					list.SetItemText(index, 4, i.Filesystem);
					list.SetItemText(index, 5, GetString(String_SimpleVolume).GetPointer());
					list.SetItemText(index, 6, GetString(String_False).GetPointer());
					list.SetItemText(index, 7, i.pDiskInfo->Name);
				}

				for (auto& i : p->AssigningLetterDlg.Volumes)
				{
					WCHAR buf[16] = {};
					int index = list.InsertItem();
					for (const auto& j : p->LetterInfoVector)
						if (j.id == i.id)
						{
							buf[0] = j.cLetter;
							list.SetItemText(index, 0, buf);
							break;
						}

					buf[0] = i.Letter;
					list.SetItemText(index, 1, buf);
					list.SetItemText(index, 2, i.Label.GetPointer());
					StrFormatByteSizeW(i.ullSize, buf, 16);
					list.SetItemText(index, 3, buf);
					list.SetItemText(index, 4, i.Filesystem);
					list.SetItemText(index, 5, i.Type.GetPointer());
					list.SetItemText(index, 6, GetString(String_True).GetPointer());
				}

				list.SetSelectionMark(-1);
				list.ShowWindow(SW_SHOW);
				p->AssigningLetterDlg.Refresh.EnableWindow(true);

				PostThreadMessageW(p->idThread, p->Msg_Continue, 0, 0);
			}
			else if (msg.message == p->Msg_Quit)
				break;
		}
	}
	catch (ComError ce)
	{
		p->AssigningLetterDlg.MessageBox(ResStrFormat(String_ComError, ce.nLine, ce.hResult),
			nullptr, MB_ICONERROR);
		p->idThread = 0;
	}
	CoUninitialize();
}

InstallationWizard::LetterDlg::LetterDlg(InstallationWizard* p) : Dialog(p, GetFontSize() * 58, GetFontSize() * 20, WS_SYSMENU | WS_CAPTION | WS_BORDER | DS_FIXEDSYS | DS_MODALFRAME, GetString(String_AssignLetters)),
PartVolList(this, 0, WS_CHILD | WS_BORDER | LVS_SINGLESEL), SetIt(this, &InstallationWizard::LetterDlg::Set), Letter(this, 0), Refresh(this, &InstallationWizard::LetterDlg::RefreshInfo)
{
	thread t(Thread2, static_cast<InstallationWizard*>(p));
	idThread = GetThreadId(t.native_handle());
	t.detach();
}
