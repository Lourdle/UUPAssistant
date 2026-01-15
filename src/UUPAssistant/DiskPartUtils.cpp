#include "pch.h"
#include "UUPAssistant.h"
#include "Resources/resource.h"
#include "DiskPartTable.h"

#include <Dbt.h>
#include <Shlwapi.h>

#include <string>
#include <format>
#include <vector>

using namespace std;
using namespace Lourdle::UIFramework;

constexpr UINT kListViewSelectedAndFocused = LVIS_SELECTED | LVIS_FOCUSED;

struct SetPartDlg : DialogEx<InstallationWizard>
{
	SetPartDlg(InstallationWizard* Parent, int idTitleString, int idText);
	INT_PTR DialogProc(UINT Msg, WPARAM wParam, LPARAM lParam);
	LRESULT OnNotify(LPNMHDR lpNotifyMsgHdr);

	virtual void Apply() = 0;
	void Init(LPARAM);
	virtual void Init(int pxUnit) = 0;
	int InsertItem(const InstallationWizard::PartVolInfo&);
	void OnDraw(HDC, RECT);

	ListView List;
	Button Okay;
	int idText;
};

SetPartDlg::SetPartDlg(InstallationWizard* Parent, int idTitleString, int idText) :
	DialogEx(Parent, GetFontSize(nullptr) * 51, GetFontSize(nullptr) * 22, WS_SYSMENU | WS_BORDER | WS_CAPTION | DS_MODALFRAME | DS_FIXEDSYS, GetString(idTitleString)),
	List(this, 0, WS_VISIBLE | WS_CHILD | WS_BORDER), Okay(this, &SetPartDlg::Apply, ButtonStyle::DefPushButton), idText(idText)
{
}

void SetPartDlg::Init(LPARAM)
{
	if (!GetParent()->idThread)
	{
		SetLastError(ERROR_NOT_SUPPORTED);
		ErrorMessageBox();
		EndDialog(0);
		return;
	}

	int pxUnit = GetFontSize();
	CenterWindow();

	List.MoveWindow(pxUnit * 2, pxUnit * 8, pxUnit * 47, pxUnit * 10);
	Okay.SetWindowText(GetString(String_Okay));
	Okay.MoveWindow(pxUnit * 44, pxUnit * 19, pxUnit * 5, pxUnit * 2);

	List.InsertColumn(GetString(String_Letter).GetPointer(), pxUnit * 5, 0);
	List.InsertColumn(GetString(String_Filesystem).GetPointer(), pxUnit * 6, 1);
	List.InsertColumn(GetString(String_Label).GetPointer(), pxUnit * 10, 2);
	List.InsertColumn(GetString(String_Size).GetPointer(), pxUnit * 7, 3);
	List.InsertColumn(GetString(String_Disk).GetPointer(), pxUnit * 11, 4);
	List.InsertColumn(GetString(String_Number).GetPointer(), pxUnit * 3, 5);

	Init(pxUnit);
	Okay.EnableWindow(false);
}

int SetPartDlg::InsertItem(const InstallationWizard::PartVolInfo& pi)
{
	int i = List.InsertItem();
	List.SetItemText(i, 4, pi.pDiskInfo->Name);
	WCHAR str[16];
	_itow_s(pi.Number, str, 10);
	List.SetItemText(i, 5, str);
	if (IsCharAlphaW(pi.DosDeviceLetter))
	{
		str[0] = pi.DosDeviceLetter;
		str[1] = ':';
		str[2] = 0;
	}
	else
		str[0] = 0;
	List.SetItemText(i, 0, str);
	StrFormatByteSizeW(pi.ullSize, str, 16);
	List.SetItemText(i, 3, str);
	List.SetItemText(i, 1, pi.Filesystem);
	List.SetItemText(i, 2, pi.Label);
	if (pi.PartTableInfo.GPT.Type == ESP)
		List.SetCheckState(i, true);
	return i;
}

void SetPartDlg::OnDraw(HDC hdc, RECT rect)
{
	rect.left = GetFontSize(nullptr) * 2;
	rect.right -= rect.left;
	rect.bottom = rect.left / 2 * 8;

	DrawText(hdc, idText, &rect, DT_WORDBREAK | DT_VCENTER);
}

INT_PTR SetPartDlg::DialogProc(UINT Msg, WPARAM wParam, LPARAM lParam)
{
	if (Msg == WM_DEVICECHANGE
		&& (wParam == DBT_DEVNODES_CHANGED || wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE))
		EndDialog(0);
	return Dialog::DialogProc(Msg, wParam, lParam);
}

LRESULT SetPartDlg::OnNotify(LPNMHDR lpNotifyMsgHdr)
{
	if (lpNotifyMsgHdr->hwndFrom == List && lpNotifyMsgHdr->code == LVN_ITEMCHANGED)
	{
		auto pnm = reinterpret_cast<LPNMLISTVIEW>(lpNotifyMsgHdr);
		bool bCheckboxStyle = List.GetExtendedListViewStyle() & LVS_EX_CHECKBOXES;
		if (pnm->iItem > -1)
		{
			const UINT newState = pnm->uNewState;

			const bool bSelectEvent = !bCheckboxStyle && ((newState & kListViewSelectedAndFocused) == kListViewSelectedAndFocused);
			const bool bCheckboxEvent = bCheckboxStyle && ((newState & LVIS_STATEIMAGEMASK) != 0);
			if (bSelectEvent || bCheckboxEvent)
				Okay.EnableWindow(true);
		}
	}

	return 0;
}


void InstallationWizard::OpenSetESPsDlg()
{
	struct SetESPDlg : SetPartDlg
	{
		SetESPDlg(InstallationWizard* Parent) : SetPartDlg(Parent, String_SetESPs, String_SelectFATPart)
		{
		}

		void Init(int pxUnit)
		{
			List.AddExtendedListViewStyle(LVS_EX_CHECKBOXES);

			for (const auto& pi : GetParent()->PartInfoVector)
				if (pi.GPT && (pi.Filesystem == L"FAT" || pi.Filesystem == L"FAT32"))
					InsertItem(pi);
		}

		void Apply()
		{
			vector<int> v;
			int i = 0, j = 0;
			for (const auto& pi : GetParent()->PartInfoVector)
			{
				if (pi.GPT && (pi.Filesystem == L"FAT" || pi.Filesystem == L"FAT32"))
					if (List.GetCheckState(j++) == TRUE != (pi.PartTableInfo.GPT.Type == ESP))
						v.push_back(i);
				++i;
			}

			if (!v.empty())
			{
				SetPartStruct* p = new SetPartStruct[v.size()];
				for (i = 0; i != v.size(); ++i)
				{
					const auto& piv = GetParent()->PartInfoVector[v[i]];
					p[i].id = piv.PartTableInfo.GPT.id;
					p[i].pDiskInfo = piv.pDiskInfo;
					p[i].Set = piv.PartTableInfo.GPT.Type != ESP;
				}

				PostThreadMessageW(GetParent()->idThread, Msg_SetESP, reinterpret_cast<WPARAM>(p), v.size());

				GetParent()->RefreshInfo();
			}

			EndDialog(0);
		}

	}SetESP(this);
	SetESP.ModalDialogBox(0);
}

static bool SetBootloader(PCWSTR pDisk, DWORD dwSignature)
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

	MyUniquePtr<BYTE> Sector(DiskGeometry.BytesPerSector);
	MBR_t& MBR = *reinterpret_cast<MBR_t*>(Sector.get());
	if (!ReadFile(hDisk, Sector, DiskGeometry.BytesPerSector, nullptr, nullptr))
	{
		CloseHandle(hDisk);
		return false;
	}
	if (MBR.dwSignature != dwSignature)
	{
		CloseHandle(hDisk);
		SetLastError(ERROR_FILE_NOT_FOUND);
		return false;
	}

	HGLOBAL hResData = LoadResource(nullptr, FindResourceA(nullptr, MAKEINTRESOURCEA(File_Bootloader), "FILE"));
	if (!hResData)
	{
		CloseHandle(hDisk);
		SetLastError(ERROR_NOT_FOUND);
		return false;
	}

	memcpy(MBR.Bootloader, LockResource(hResData), sizeof(MBR.Bootloader));
	MBR.BootableSignarure[0] = 0x55;
	MBR.BootableSignarure[1] = 0xAA;
	SetFilePointer(hDisk, 0, nullptr, FILE_BEGIN);
	bool ret = WriteFile(hDisk, Sector, DiskGeometry.BytesPerSector, nullptr, nullptr);
	CloseHandle(hDisk);
	return ret;
}

static bool SetActivePartition(PCWSTR pDisk, DWORD dwSignature, DWORD dwPartLBA)
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

	MyUniquePtr<BYTE> Sector(DiskGeometry.BytesPerSector);
	MBR_t& MBR = *reinterpret_cast<MBR_t*>(Sector.get());
	if (!ReadFile(hDisk, Sector, DiskGeometry.BytesPerSector, nullptr, nullptr))
	{
		CloseHandle(hDisk);
		return false;
	}
	if (MBR.dwSignature != dwSignature)
	{
		CloseHandle(hDisk);
		SetLastError(ERROR_FILE_NOT_FOUND);
		return false;
	}

	for (auto& i : MBR.Partitions)
		i.State = 0 ? i.StartingLBA != dwPartLBA : 0x80;

	for (auto& i : MBR.Partitions)
	{
		if (i.StartingLBA != dwPartLBA)
			continue;

		SetFilePointer(hDisk, 0, nullptr, FILE_BEGIN);
		if (!WriteFile(hDisk, Sector, DiskGeometry.BytesPerSector, nullptr, nullptr))
		{
			CloseHandle(hDisk);
			return false;
		}
		FlushFileBuffers(hDisk);
		DeviceIoControl(hDisk, IOCTL_DISK_UPDATE_PROPERTIES, nullptr, 0, nullptr, 0, &cbReturned, nullptr);
		CloseHandle(hDisk);
		return true;
	}

	CloseHandle(hDisk);
	SetLastError(ERROR_NOT_FOUND);
	return false;
}

String GetPartitionFsPath(ULONG DiskNumber, ULONG Number)
{
	return std::format(L"\\\\?\\GLOBALROOT\\Device\\Harddisk{}\\Partition{}\\", DiskNumber, Number);
}

String GetPartitionFsPath(PCWSTR pDiskName, ULONG Number)
{
	return GetPartitionFsPath(wcstoul(pDiskName + 17, nullptr, 10), Number);
}

static HANDLE OpenPartition(PCWSTR pDiskName, DWORD Signature, ULONG ulBytesPerSector, ULONG Number)
{
	HANDLE hFile = CreateFileW(pDiskName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
		return nullptr;

	MyUniquePtr<BYTE> Sector(ulBytesPerSector);
	MBR_t& MBR = *reinterpret_cast<MBR_t*>(Sector.get());
	if (!ReadFile(hFile, Sector.get(), ulBytesPerSector, nullptr, nullptr))
	{
		CloseHandle(hFile);
		return nullptr;
	}
	if (MBR.dwSignature != Signature)
	{
		CloseHandle(hFile);
		SetLastError(ERROR_FILE_NOT_FOUND);
		return nullptr;
	}
	CloseHandle(hFile);

	auto Path = GetPartitionFsPath(pDiskName, Number);
	Path[Path.GetLength() - 1] = 0;
	hFile = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	return hFile == INVALID_HANDLE_VALUE ? nullptr : hFile;
}


static bool NtfsSetPBR(PCWSTR pDiskName, DWORD Signature, ULONG ulBytesPerSector, ULONG Number)
{
	HANDLE hFile = OpenPartition(pDiskName, Signature, ulBytesPerSector, Number);
	if (!hFile)
		return false;

	HRSRC hResInfo = FindResourceA(nullptr, MAKEINTRESOURCEA(File_NtfsBootmgrLdr), "FILE");
	if (!hResInfo)
	{
		CloseHandle(hFile);
		SetLastError(ERROR_NOT_FOUND);
		return false;
	}
	DWORD cbData = (SizeofResource(nullptr, hResInfo) + 84) / ulBytesPerSector * ulBytesPerSector + ulBytesPerSector * DWORD((SizeofResource(nullptr, hResInfo) + 84) % ulBytesPerSector != 0);
	MyUniquePtr<BYTE> p(cbData);
	memset(p, 0, cbData);

	ReadFile(hFile, p, ulBytesPerSector, nullptr, nullptr);
	memcpy(p.get() + 84, LockResource(LoadResource(nullptr, hResInfo)), SizeofResource(nullptr, hResInfo));
	
	SetFilePointer(hFile, -LONG(ulBytesPerSector), nullptr, FILE_CURRENT);
	bool ret = WriteFile(hFile, p, cbData, nullptr, nullptr);
	CloseHandle(hFile);
	return ret;
}

static bool ExfatSetPBR(PCWSTR pDiskName, DWORD Signature, ULONG ulBytesPerSector, ULONG Number)
{
	HANDLE hFile = OpenPartition(pDiskName, Signature, ulBytesPerSector, Number);
	if (!hFile)
		return false;

	HRSRC hResInfo = FindResourceA(nullptr, MAKEINTRESOURCEA(File_ExfatBootmgrLdr), "FILE");
	if (!hResInfo)
	{
		CloseHandle(hFile);
		SetLastError(ERROR_NOT_FOUND);
		return false;
	}

	MyUniquePtr<BYTE> Buffer(12 * ulBytesPerSector);
	memset(Buffer.get() + ulBytesPerSector, 0, 11 * ulBytesPerSector);
	
	ReadFile(hFile, Buffer, ulBytesPerSector, nullptr, nullptr);
	SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);
	memcpy(Buffer.get() + 120, LockResource(LoadResource(nullptr, hResInfo)), SizeofResource(nullptr, hResInfo));
	for (DWORD i = 1; i != 11; ++i)
	{
		Buffer[(i + 1) * ulBytesPerSector - 2] = 0x55;
		Buffer[(i + 1) * ulBytesPerSector - 1] = 0xAA;
	}

	DWORD Checksum = 0;
	for (DWORD i = 0; i != 11 * ulBytesPerSector; ++i)
		if (i != 106 && i != 107 && i != 112)
			Checksum = (Checksum << 31 | Checksum >> 1) + Buffer[i];
	for (DWORD i = 0; i != ulBytesPerSector; ++i)
		Buffer[11 * ulBytesPerSector + i] = reinterpret_cast<PBYTE>(&Checksum)[i % 4];

	BOOL ret = true;
	for (BYTE mark = 0; mark != 2; ++mark)
		ret &= WriteFile(hFile, Buffer, 12 * ulBytesPerSector, nullptr, nullptr);
	CloseHandle(hFile);
	return ret;
}

static bool Fat32SetPBR(PCWSTR pDiskName, DWORD Signature, ULONG ulBytesPerSector, ULONG Number)
{
	HANDLE hFile = OpenPartition(pDiskName, Signature, ulBytesPerSector, Number);
	if (!hFile)
		return false;

	HRSRC hResInfo = FindResourceA(nullptr, MAKEINTRESOURCEA(File_Fat32BootmgrLdr), "FILE");
	if (!hResInfo)
	{
		CloseHandle(hFile);
		SetLastError(ERROR_NOT_FOUND);
		return false;
	}

	MyUniquePtr<BYTE> p(ulBytesPerSector);
	ReadFile(hFile, p, ulBytesPerSector, nullptr, nullptr);
	SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);
	memcpy(p.get() + 90, LockResource(LoadResource(nullptr, hResInfo)), SizeofResource(nullptr, hResInfo));
	p[510] = 0x55;
	p[511] = 0xAA;
	BOOL ret = WriteFile(hFile, p, ulBytesPerSector, nullptr, nullptr);
	SetFilePointer(hFile, 5 * ulBytesPerSector, nullptr, FILE_CURRENT);
	ret &= WriteFile(hFile, p, ulBytesPerSector, nullptr, nullptr);

	CloseHandle(hFile);
	return ret;
}

static bool FatSetPBR(PCWSTR pDiskName, DWORD Signature, ULONG ulBytesPerSector, ULONG Number)
{
	HANDLE hFile = OpenPartition(pDiskName, Signature, ulBytesPerSector, Number);
	if (!hFile)
		return false;

	HRSRC hResInfo = FindResourceA(nullptr, MAKEINTRESOURCEA(File_FatBootmgrLdr), "FILE");
	if (!hResInfo)
	{
		CloseHandle(hFile);
		SetLastError(ERROR_NOT_FOUND);
		return false;
	}

	MyUniquePtr<BYTE> Sector(ulBytesPerSector);
	ReadFile(hFile, Sector, ulBytesPerSector, nullptr, nullptr);
	SetFilePointer(hFile, -LONG(ulBytesPerSector), nullptr, FILE_CURRENT);
	memcpy(Sector.get() + 62, LockResource(LoadResource(nullptr, hResInfo)), SizeofResource(nullptr, hResInfo));
	Sector[510] = 0x55;
	Sector[511] = 0xAA;
	bool ret = WriteFile(hFile, Sector, ulBytesPerSector, nullptr, nullptr);

	CloseHandle(hFile);
	return ret;
}

bool SetPBRAndDiskMBR(PCWSTR pDiskName, DWORD Signature, ULONG ulBytesPerSector, ULONG Number, ULONGLONG ullOffset)
{
	return NtfsSetPBR(pDiskName, Signature, ulBytesPerSector, Number)
		&& SetActivePartition(pDiskName, Signature, static_cast<DWORD>(ullOffset / ulBytesPerSector))
		&& SetBootloader(pDiskName, Signature);
}

void InstallationWizard::OpenSetActivePartsDlg()
{
	struct SetActiveParts : SetPartDlg
	{
		SetActiveParts(InstallationWizard* Parent) : SetPartDlg(Parent, String_SetActiveParts, String_ActivePartSetting)
		{
		}

		void Init(int pxUnit)
		{
			List.InsertColumn(GetString(String_Active), pxUnit * 3, 6);
			for (const auto& i : GetParent()->PartInfoVector)
				if (!i.GPT && i.PartTableInfo.MBR.Primary && (i.PartTableInfo.MBR.Type != 0x05 && i.PartTableInfo.MBR.Type != 0x0F))
					List.SetItemText(InsertItem(i), 6, GetString(String_False - i.PartTableInfo.MBR.bootIndicator));
		}

		void Apply()
		{
			int index = List.GetSelectionMark();
			const PartVolInfo* targetPart = nullptr;
			for (const auto& i : GetParent()->PartInfoVector)
				if (!i.GPT && i.PartTableInfo.MBR.Primary && (i.PartTableInfo.MBR.Type != 0x05 && i.PartTableInfo.MBR.Type != 0x0F))
				{
					if (index == 0)
					{
						targetPart = &i;
						break;
					}
					--index;
				}

			if (!SetActivePartition(targetPart->pDiskInfo->Name, targetPart->pDiskInfo->Signature, static_cast<DWORD>(targetPart->ullOffset / targetPart->pDiskInfo->ulBytesPerSector)))
				ErrorMessageBox();
			else
				GetParent()->RefreshInfo();
			EndDialog(0);
		}
	};

	SetActiveParts SAP(this);
	SAP.ModalDialogBox(0);
}

void InstallationWizard::OpenSetBootRecordsDlg()
{
	struct SetBootRecords : SetPartDlg
	{
		SetBootRecords(InstallationWizard* Parent) : SetPartDlg(Parent, String_SetBootRecords, String_BootRecordsSetting)
		{
		}

		void Init(int pxUnit)
		{
			for (const auto& i : GetParent()->PartInfoVector)
				if (!i.GPT
					&& (i.Filesystem == L"NTFS" || i.Filesystem == L"FAT32" || i.Filesystem == L"FAT" || i.Filesystem == L"exFAT"))
					InsertItem(i);
		}

		void Apply()
		{
			int index = List.GetSelectionMark();
			const PartVolInfo* targetPart = nullptr;
			for (const auto& i : GetParent()->PartInfoVector)
				if (!i.GPT
					&& (i.Filesystem == L"NTFS" || i.Filesystem == L"FAT32" || i.Filesystem == L"FAT" || i.Filesystem == L"exFAT"))
				{
					if (index == 0)
					{
						targetPart = &i;
						break;
					}
					--index;
				}

			bool(*SetPBR)(PCWSTR pDiskName, DWORD Signature, ULONG ulBytesPerSector, ULONG Number) = nullptr;
			if (targetPart->Filesystem == L"NTFS")
				SetPBR = NtfsSetPBR;
			else if (targetPart->Filesystem == L"FAT32")
				SetPBR = Fat32SetPBR;
			else if (targetPart->Filesystem == L"FAT")
				SetPBR = FatSetPBR;
			else if (targetPart->Filesystem == L"exFAT")
				SetPBR = ExfatSetPBR;

			if (!SetPBR(targetPart->pDiskInfo->Name, targetPart->pDiskInfo->Signature, targetPart->pDiskInfo->ulBytesPerSector, targetPart->Number)
				|| !SetBootloader(targetPart->pDiskInfo->Name, targetPart->pDiskInfo->Signature))
				ErrorMessageBox();
			EndDialog(0);
		}
	};

	SetBootRecords(this).ModalDialogBox(0);
}

void InstallationWizard::OpenSetRecPartsDlg()
{
	HMENU hMenu = GetSubMenu(GetSubMenu(GetMenu(hWnd), 0), 6);
	MENUITEMINFOW mii = {
		.cbSize = sizeof(mii),
		.fMask = MIIM_STATE
	};
	GetMenuItemInfoW(hMenu, 1, TRUE, &mii);

	struct SetRecParts : SetPartDlg
	{
		SetRecParts(InstallationWizard* Parent, bool bNoRe) : SetPartDlg(Parent, String_SetRecParts, String_SettingRecParts),
			SetIt(this, &SetRecParts::SetRecPart), InstallOnSysPart(this, &SetRecParts::OnSysPart, ButtonStyle::AutoCheckbox),
			bNoRe(bNoRe), RePart(Parent->ReImagePart)
		{
		}

		bool bNoRe;
		decltype(InstallationWizard::ReImagePart)& RePart;

		void Init(int pxUnit)
		{
			InstallOnSysPart.SetWindowText(GetString(String_InstallREOnSysPart));
			SetIt.SetWindowText(GetString(String_InstallRe));
			if (RePart.dwPart != 0)
			{
				SetIt.SetWindowText(GetString(String_CancelReInstallation));
				if (RePart.dwPart == -1)
					InstallOnSysPart.SetCheck(BST_CHECKED);
				else
					InstallOnSysPart.EnableWindow(false);
			}
			else
			{
				InstallOnSysPart.EnableWindow(!bNoRe);
				SetIt.EnableWindow(false);
			}

			SIZE size;
			InstallOnSysPart.GetIdealSize(&size);
			InstallOnSysPart.MoveWindow(pxUnit, pxUnit * 20 - size.cy / 2, size.cx, size.cy);
			SetIt.MoveWindow(pxUnit * 33, pxUnit * 19, pxUnit * 10, pxUnit * 2);

			List.AddExtendedListViewStyle(LVS_EX_CHECKBOXES);
			List.InsertColumn(L"RE", static_cast<int>(pxUnit * 2.5), 6);
			for (const auto& i : GetParent()->PartInfoVector)
				if (i.Filesystem == L"NTFS")
				{
					int Index = InsertItem(i);
					if (i.GPT && i.PartTableInfo.GPT.Type == MSRecovery)
						List.SetCheckState(Index, true);
					bool bExists = PathFileExistsW(
						GetPartitionFsPath(i.pDiskInfo->Name, i.Number) + L"Recovery\\WindowsRE\\Winre.wim"
					);
					PCWSTR pMark = wcstoul(i.pDiskInfo->Name.GetPointer() + 17, nullptr, 10) == RePart.dwDisk
						&& i.Number == RePart.dwPart
						? bExists
						? bNoRe
						? L"U"
						: L"O"
						: L"I"
						: bExists
						? L"E"
						: nullptr;
					if (pMark)
						List.SetItemText(Index, 6, pMark);
				}
		}

		const PartVolInfo& SelectPart(int index)
		{
			const PartVolInfo* targetPart = nullptr;
			for (const auto& i : GetParent()->PartInfoVector)
				if (i.Filesystem == L"NTFS")
				{
					if (index == 0)
					{
						targetPart = &i;
						break;
					}
					--index;
				}
			return *targetPart;
		}

		LRESULT OnNotify(LPNMHDR lpNotifyMsgHdr)
		{
			if (lpNotifyMsgHdr->hwndFrom == List)
			{
				const auto pnm = reinterpret_cast<LPNMLISTVIEW>(lpNotifyMsgHdr);
				const UINT kListViewStateUnchecked = INDEXTOSTATEIMAGEMASK(1);
				const UINT kListViewStateChecked = INDEXTOSTATEIMAGEMASK(2);

				if ((lpNotifyMsgHdr->code == LVN_ITEMCHANGING || lpNotifyMsgHdr->code == LVN_ITEMCHANGED) && pnm->iItem > -1)
				{
					auto& Part = SelectPart(pnm->iItem);
					if (!Part.GPT && lpNotifyMsgHdr->code == LVN_ITEMCHANGED)
						if (pnm->uOldState == kListViewStateUnchecked && pnm->uNewState == kListViewStateChecked)
							List.SetCheckState(pnm->iItem, false);
						else if (pnm->uOldState == kListViewStateChecked && pnm->uNewState == kListViewStateUnchecked)
						{
							MessageBox(GetString(String_OnlySupportGPTRecParts), GetString(String_Notice), MB_ICONERROR);
							return TRUE;
						}

					if (pnm->uNewState == kListViewSelectedAndFocused)
						SetIt.EnableWindow(!bNoRe || RePart.dwPart != 0 || bNoRe && List.GetItemText(pnm->iItem, 6) == L"E");
					else if (pnm->uNewState == kListViewStateChecked || pnm->uNewState == kListViewStateUnchecked)
						Okay.EnableWindow(true);
				}
			}

			return 0;
		}

		int GetRePart()
		{
			int nCount = List.GetItemCount();
			for (int i = 0; i != nCount; ++i)
			{
				auto Text = List.GetItemText(i, 6);
				if (Text == L"O" || Text == L"I" || Text == L"U")
					return i;
			}
			return -1;
		}

		void Apply()
		{
			if (InstallOnSysPart.GetCheck() == BST_CHECKED)
				RePart.dwPart = -1;
			else
			{
				int index = GetRePart();
				if (index == -1)
					RePart.dwPart = 0;
				else
				{
					auto& Part = SelectPart(index);
					RePart.dwDisk = wcstoul(Part.pDiskInfo->Name.GetPointer() + 17, nullptr, 10);
					RePart.dwPart = Part.Number;
				}
			}

			int nCount = List.GetItemCount();
			vector<const InstallationWizard::PartVolInfo*> v;
			for (int i = 0; i != nCount; ++i)
			{
				auto& Part = SelectPart(i);

				if ((List.GetCheckState(i) == BST_CHECKED) != (Part.GPT && Part.PartTableInfo.GPT.Type == MSRecovery))
					v.push_back(&Part);
			}

			if (!v.empty())
			{
				SetPartStruct* p = new SetPartStruct[v.size()];
				for (size_t i = 0; i != v.size(); ++i)
				{
					p[i].id = v[i]->PartTableInfo.GPT.id;
					p[i].pDiskInfo = v[i]->pDiskInfo;
					p[i].Set = v[i]->PartTableInfo.GPT.Type != MSRecovery;
				}

				PostThreadMessageW(GetParent()->idThread, Msg_SetRec, reinterpret_cast<WPARAM>(p), v.size());

				GetParent()->RefreshInfo();
			}
			EndDialog(0);
		}

		void SetRecPart()
		{
			int index = GetRePart();
			if (index != -1 || InstallOnSysPart.GetCheck() == BST_CHECKED)
			{
				SetIt.SetWindowText(GetString(String_InstallRe));
				if (index != -1)
				{
					InstallOnSysPart.EnableWindow(!bNoRe);
					List.SetItemText(index, 6, L"E" + INT_PTR(List.GetItemText(index, 6) == L"I"));
				}
				else
					InstallOnSysPart.SetCheck(BST_UNCHECKED);
				if (List.GetSelectionMark() == -1)
					SetIt.EnableWindow(false);
			}
			else
			{
				index = List.GetSelectionMark();
				if (index == -1)
				{
					SetIt.EnableWindow(false);
					return;
				}
				SetIt.SetWindowText(GetString(String_CancelReInstallation));
				InstallOnSysPart.EnableWindow(false);
				List.SetItemText(
					index,
					6,
					List.GetItemText(index, 6) == L"E" ? bNoRe ? L"U" : L"O" : L"I"
				);
			}

			Okay.EnableWindow(true);
		}

		void OnSysPart()
		{
			UINT uResId = InstallOnSysPart.GetCheck() == BST_CHECKED
				? String_CancelReInstallation
				: String_InstallRe;
			SetIt.SetWindowText(GetString(uResId).GetPointer());
			if (uResId == String_CancelReInstallation)
				SetIt.EnableWindow(true);
			else if (List.GetSelectionMark() == -1)
				SetIt.EnableWindow(false);

			Okay.EnableWindow(true);
		}

		Button SetIt;
		Button InstallOnSysPart;
	};

	SetRecParts SRP(this, mii.fState == MFS_CHECKED);
	SRP.ModalDialogBox(0);
}
