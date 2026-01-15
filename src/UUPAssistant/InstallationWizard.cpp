#include "pch.h"
#include "UUPAssistant.h"
#include "Resources/resource.h"
#include "DiskPartTable.h"

#include <Dbt.h>
#include <Shlwapi.h>
#include <virtdisk.h>

#include <thread>
#include <algorithm>
#include <format>
#include <filesystem>

extern "C" NTSTATUS WINAPI RtlGetVersion(PRTL_OSVERSIONINFOW);

using namespace std;
using namespace Lourdle::UIFramework;

import Constants;


InstallationWizard::InstallationWizard(SessionContext& ctx) : Window(GetFontSize(nullptr) * 80, GetFontSize(nullptr) * 50, GetString(String_InstallWindows), WS_BORDER | WS_SYSMENU | WS_MINIMIZEBOX | WS_CAPTION | WS_CLIPCHILDREN, nullptr, LoadMenuW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDR_MENU))),
Refresh(this, &InstallationWizard::RefreshInfo, ButtonStyle::Text), Next(this, &InstallationWizard::Continue, ButtonStyle::Text), DontBoot(this, &InstallationWizard::DontBootButtonClicked, ButtonStyle::AutoCheckbox), ExtraBootOption(this, &InstallationWizard::SwitchExtraBootOption, ButtonStyle::AutoCheckbox), BootMode(this),
PartList(this, 0), BootPartList(this, 0), ctx(ctx), Detail(this, 0, DWORD(WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL)),
TargetPath(this, 0), Boot(this, 0), BrowseTargetPath(this, 128), BrowseBootPath(this, 128), BootFromVHD(this, 0),
hIcon(LoadIconW(nullptr, IDI_WARNING)), Label(this, 0), VMSysAutoMgmt(true), DeleteLetterAfterInstallation(false), idThread(0),
AssigningLetterDlg(this), SettingVM(this), SettingOOBE(this), Ring(this)
{
	bDoubleBuffer = true;
	FormatOptions.bForce = 0;
	FormatOptions.bQuick = 1;
	FormatOptions.ReFS = 0;
	auto pxUnit = GetFontSize(nullptr);

	Refresh.SetWindowText(GetString(String_Refresh));
	Next.SetWindowText(GetString(String_Next));

	PartList.AddWindowStyle(LVS_SINGLESEL | LVS_REPORT);
	PartList.EnableWindow(false);
	PartList.MoveWindow(pxUnit * 2, pxUnit * 5, pxUnit * 76, pxUnit * 16);

	PartList.InsertColumn(GetString(String_Letter).GetPointer(), pxUnit * 3, 0);
	PartList.InsertColumn(GetString(String_Filesystem).GetPointer(), pxUnit * 6, 1);
	PartList.InsertColumn(GetString(String_Label).GetPointer(), pxUnit * 18, 2);
	PartList.InsertColumn(GetString(String_Size).GetPointer(), pxUnit * 7, 3);
	PartList.InsertColumn(GetString(String_FreeSpace), pxUnit * 7, 4);
	PartList.InsertColumn(GetString(String_Offset).GetPointer(), pxUnit * 7, 5);
	PartList.InsertColumn(GetString(String_PartStyle).GetPointer(), pxUnit * 7, 6);
	PartList.InsertColumn(GetString(String_Disk).GetPointer(), pxUnit * 10, 7);
	PartList.InsertColumn(GetString(String_Removable).GetPointer(), pxUnit * 4, 8);
	PartList.InsertColumn(GetString(String_Number).GetPointer(), pxUnit * 4, 9);

	BootPartList.AddWindowStyle(LVS_SINGLESEL | LVS_REPORT);
	BootPartList.MoveWindow(pxUnit * 41, pxUnit * 35, pxUnit * 37, pxUnit * 8);

	BootPartList.InsertColumn(GetString(String_Letter).GetPointer(), pxUnit * 3, 0);
	BootPartList.InsertColumn(GetString(String_Label).GetPointer(), pxUnit * 10, 1);
	BootPartList.InsertColumn(GetString(String_Size).GetPointer(), pxUnit * 6, 2);
	BootPartList.InsertColumn(GetString(String_Disk).GetPointer(), pxUnit * 10, 3);
	BootPartList.InsertColumn(GetString(String_Number).GetPointer(), pxUnit * 3, 4);

	DontBoot.SetWindowText(GetString(String_DontBoot));
	BootFromVHD.SetWindowText(GetString(String_BootFromVHD));
	BootFromVHD.ShowWindow(SW_HIDE);
	BootFromVHD.MoveWindow(pxUnit * 41, pxUnit * 30, pxUnit * 30, pxUnit * 2);

	bDoubleBuffer = true;
	RegisterCommand(UpdateDlg::OpenDialog, this, &ctx, nullptr, ID_40001, 0);
	RegisterCommand(AppDlg::OpenDialog, this, &ctx, nullptr, ID_40002, 0);
	RegisterCommand(DriverDlg::OpenDialog, this, &ctx, nullptr, ID_40003, 0);
	RegisterCommand(&Dialog::ModalDialogBox, &AssigningLetterDlg, 0, nullptr, ID_40004, 0);
	RegisterCommand(&Dialog::ModalDialogBox, &SettingVM, 0, nullptr, ID_40005, 0);
	RegisterCommand(&Dialog::ModalDialogBox, &SettingOOBE, 0, nullptr, ID_40006, 0);
	RegisterCommand({ nullptr, ID_40007, 0 }, [](PVOID hMenu, LPARAM)
		{
			MENUITEMINFOW mii = {
				.cbSize = sizeof(mii),
				.fMask = MIIM_STATE
			};
			GetMenuItemInfoW(reinterpret_cast<HMENU>(hMenu), 0, TRUE, &mii);
			mii.fState = mii.fState == MFS_CHECKED ? MFS_UNCHECKED : MFS_CHECKED;
			SetMenuItemInfoW(reinterpret_cast<HMENU>(hMenu), 0, TRUE, &mii);
		}
	, 0, GetSubMenu(GetSubMenu(GetMenu(hWnd), 0), 6));
	RegisterCommand({ nullptr, ID_40008, 0 }, [](PVOID p, LPARAM)
		{
			HMENU hMenu = GetSubMenu(GetSubMenu(GetMenu(reinterpret_cast<InstallationWizard*>(p)->GetHandle()), 0), 6);
			MENUITEMINFOW mii = {
				.cbSize = sizeof(mii),
				.fMask = MIIM_STATE
			};
			GetMenuItemInfoW(hMenu, 1, TRUE, &mii);
			mii.fState = mii.fState == MFS_CHECKED ? MFS_UNCHECKED : MFS_CHECKED;
			SetMenuItemInfoW(hMenu, 1, TRUE, &mii);

			if (mii.fState == MFS_CHECKED && reinterpret_cast<InstallationWizard*>(p)->ReImagePart.dwPart != 0)
			{
				auto& ReImagePart = reinterpret_cast<InstallationWizard*>(p)->ReImagePart;
				reinterpret_cast<InstallationWizard*>(p)->MessageBox(GetString(String_REIsNotAvaliable), GetString(String_Notice), MB_ICONWARNING | MB_OK);
				if (!PathFileExistsW(
					GetPartitionFsPath(ReImagePart.dwDisk, ReImagePart.dwPart) + L"Recovery\\WindowsRE\\Winre.wim"
				))
					ReImagePart.dwPart = 0;
			}
		}
	, 0, this);
	RegisterCommand(&InstallationWizard::SwitchInstallationMethod, nullptr, ID_40009, 0);
	RegisterCommand({ nullptr, ID_40010, 0 }, [](HWND hWnd)
		{
			CHAR szSysDir[MAX_PATH];
			GetSystemDirectoryA(szSysDir, MAX_PATH);
			ShellExecuteA(hWnd, "open", "cmd.exe", nullptr, szSysDir, SW_SHOWNORMAL);
		});
	RegisterCommand({ nullptr, ID_40011, 0 }, [](HWND hWnd)
		{
			ShellExecuteA(hWnd, "open", "diskmgmt.msc", nullptr, nullptr, SW_SHOWNORMAL);
		});
	RegisterCommand(&InstallationWizard::OpenSetESPsDlg, nullptr, ID_40012, 0);
	RegisterCommand(&InstallationWizard::OpenSetActivePartsDlg, nullptr, ID_40013, 0);
	RegisterCommand(&InstallationWizard::OpenSetBootRecordsDlg, nullptr, ID_40014, 0);
	RegisterCommand(&InstallationWizard::OpenSetRecPartsDlg, nullptr, ID_40015, 0);
	HMENU hMenu = GetMenu(hWnd);
	hMenu = GetSubMenu(hMenu, 1);

	FIRMWARE_TYPE ft;
	GetFirmwareType(&ft);
	if (ctx.TargetImageInfo.SupportLegacyBIOS)
	{
		BootMode.InsertString(L"Legacy BIOS", 0);
		if (ft == FirmwareTypeBios)
			BootMode.SetCurSel(0);
	}
	if (ctx.TargetImageInfo.SupportEFI)
	{
		BootMode.InsertString(L"UEFI", 0);
		if (ft == FirmwareTypeUefi)
			BootMode.SetCurSel(0);
	}
	if (BootMode.GetCurSel() == -1)
		BootMode.SetCurSel(0);

	hMenu = CreatePopupMenu();
	for (UINT i = 0; i != ctx.TargetImageInfo.UpgradableEditions.size(); ++i)
	{
		WORD id = Random();
		AppendMenuW(hMenu, 0, id, ctx.TargetImageInfo.UpgradableEditions[i].c_str());
		if (ctx.TargetImageInfo.UpgradableEditions[i] == ctx.TargetImageInfo.Edition)
			CheckMenuRadioItem(hMenu, 0, i, i, MF_BYPOSITION);
		RegisterCommand({ nullptr, id, 0 }, [](PVOID hMenu, LPARAM lParam)
			{
				CheckMenuRadioItem(reinterpret_cast<HMENU>(hMenu), 0, GetMenuItemCount(reinterpret_cast<HMENU>(hMenu)) - 1, static_cast<UINT>(lParam), MF_BYPOSITION);
			}
		, i, hMenu);
	}

	MENUITEMINFOW mii = {
		.cbSize = sizeof(mii),
		.fMask = MIIM_SUBMENU,
		.hSubMenu = hMenu
	};
	SetMenuItemInfoW(GetSubMenu(GetSubMenu(GetMenu(hWnd), 0), 6), 2, TRUE, &mii);

	Detail.MoveWindow(pxUnit * 2, pxUnit * 24, pxUnit * 38, pxUnit * 19);
	Refresh.MoveWindow(pxUnit * 73, pxUnit * 22, pxUnit * 5, pxUnit * 2);
	Next.MoveWindow(pxUnit * 72, pxUnit * 47, pxUnit * 6, pxUnit * 2);
	TargetPath.ShowWindow(SW_HIDE);
	Boot.ShowWindow(SW_HIDE);
	BrowseTargetPath.ShowWindow(SW_HIDE);
	BrowseBootPath.ShowWindow(SW_HIDE);
	BrowseTargetPath.SetWindowText(GetString(String_BrowseDir));
	BrowseBootPath.SetWindowText(GetString(String_BrowseDir));
	TargetPath.MoveWindow(pxUnit * 2, pxUnit * 4, pxUnit * 27, pxUnit * 2, false);
	Boot.MoveWindow(pxUnit * 2, pxUnit * 11, pxUnit * 27, pxUnit * 2, false);
	BrowseTargetPath.MoveWindow(pxUnit * 31, pxUnit * 4, pxUnit * 7, pxUnit * 2, false);
	BrowseBootPath.MoveWindow(pxUnit * 31, pxUnit * 11, pxUnit * 7, pxUnit * 2, false);
	RegisterCommand(&InstallationWizard::EditTextChanged, TargetPath, static_cast<WORD>(GetWindowLongW(TargetPath, GWL_ID)), EN_CHANGE);
	RegisterCommand(&InstallationWizard::EditTextChanged, Boot, static_cast<WORD>(GetWindowLongW(Boot, GWL_ID)), EN_CHANGE);
	auto BrowsePath = [](Window* p, LPARAM pEdit)
		{
			WindowBase Parent = GetParent(p->GetHandle());
			String Dir;
			if (GetOpenFolderName(&Parent, Dir))
				reinterpret_cast<Edit*>(pEdit)->SetWindowText(Dir);
		};
	RegisterCommand({ BrowseTargetPath, 128, 0 }, BrowsePath, reinterpret_cast<LPARAM>(&TargetPath));
	RegisterCommand({ BrowseBootPath, 128, 0 }, BrowsePath, reinterpret_cast<LPARAM>(&Boot));
	RegisterCommand(&InstallationWizard::SwitchBootMode, BootMode, static_cast<WORD>(GetWindowLongW(BootMode, GWL_ID)), CBN_SELCHANGE);

	String Tip = GetString(String_LearnMoreAboutBootEX);
	TTTOOLINFOW ti = {
		.cbSize = sizeof(ti),
		.uFlags = TTF_SUBCLASS | TTF_IDISHWND | TTF_PARSELINKS | TTF_TRANSPARENT,
		.hwnd = ExtraBootOption,
		.uId = reinterpret_cast<UINT_PTR>(ExtraBootOption.GetHandle()),
		.lpszText = Tip.GetPointer()
	};
	ToolTip.AddTool(&ti);
	ToolTip.SetMaxTipWidth(pxUnit * 30);

	ShowWindow(SW_SHOWNORMAL);
	Ring.MoveWindow(pxUnit * 2, pxUnit * 22, pxUnit * 2, pxUnit * 2);
	Ring.Start();
	SwitchInstallationMethod();
	UpdateWindow();
}

void InstallationWizard::OnDraw(HDC hdc, RECT rect)
{
	if (State == AdjustingWindow)
		return;
	int pxUnit = GetFontSize(nullptr);

	rect.left = pxUnit * 2;
	rect.right -= rect.left;
	rect.bottom = 5 * pxUnit;
	if (idThread)
	{
		DrawText(hdc, String_PartSel, &rect, DT_WORDBREAK | DT_VCENTER);
		RECT rc = rect;
		rect.left = pxUnit * 41;
		rect.top = pxUnit * 23;
		rect.bottom = rect.top + pxUnit * 2;
		DrawText(hdc, String_BootMode, &rect, DT_VCENTER);
		rect = rc;
	}
	else if (!idThread)
	{
		rect.top = pxUnit * 2;
		rect.bottom -= pxUnit;
		DrawText(hdc, String_TargetPath, &rect, DT_VCENTER);
		rect += pxUnit * 7;
		rect.bottom = pxUnit * 2 + rect.top;
		DrawText(hdc, String_BootPath, &rect, DT_VCENTER);
		rect.left = pxUnit * 2;
		rect.top = static_cast<LONG>(pxUnit * 13.5);
		rect.bottom = rect.top + pxUnit * 2;
		DrawText(hdc, String_BootMode, &rect, DT_VCENTER);
		return;
	}

	if (DontBoot.GetCheck() == BST_UNCHECKED && idThread)
	{
		RECT rc = { pxUnit * 41, pxUnit * 67 / 2, pxUnit * 78, pxUnit * 33 };
		if (BootMode.GetWindowText() == L"UEFI")
			DrawText(hdc, String_SelectESP, &rc, DT_WORDBREAK | DT_VCENTER);
		else if (BootMode.GetWindowText() == L"Legacy BIOS" &&
			(ExtraBootOption.IsWindowVisible() || State == Formatting) && ExtraBootOption.GetCheck() == BST_UNCHECKED)
			DrawText(hdc, String_SelectBootPart, &rc, DT_WORDBREAK | DT_VCENTER);
	}

	if (State == Updating || State == UpdatingMultiRequests || State == Formatting)
	{
		rect.top = 22 * pxUnit;
		rect.bottom = rect.top + 2 * pxUnit;
		rect.left = pxUnit * 5;
		DrawText(hdc, State == Formatting ? String_Formatting : String_Updating, &rect, DT_SINGLELINE | DT_VCENTER);
	}
	else if (State == WarningPage || State == WarningSystem || State == WarningSpaceNotEnough)
	{
		DrawIconEx(hdc, pxUnit * 2, pxUnit * 44, hIcon, pxUnit * 2, pxUnit * 2, 0, reinterpret_cast<HBRUSH>(SendMessage(WM_CTLCOLORSTATIC, 0, 0)), DI_IMAGE);
		rect.right -= rect.left;
		rect.left += pxUnit * 3;
		rect.bottom = pxUnit * 46;
		rect.top = rect.bottom - pxUnit * 2;
		DrawText(hdc, State == WarningPage ? String_PagePart : State == WarningSpaceNotEnough ? String_NoEnoughSpace : String_SystemPart, &rect, DT_WORDBREAK | DT_VCENTER);
	}
}

void InstallationWizard::OnDestroy()
{
	PostQuitMessage(0);
}

template<typename T>
String ToString(T num)
{
	WCHAR buf[sizeof(T) * 2 + 1];
	buf[sizeof(T) * 2] = 0;
	for (int i = 0; i != sizeof(T) * 2; ++i)
	{
		buf[i] = '0' + num % 16;
		if (buf[i] > '9')
			buf[i] = buf[i] - '9' + 'A' - 1;
		num /= 16;
	}
	for (int i = 0; i != sizeof(T); ++i)
	{
		WCHAR tmp = buf[i];
		buf[i] = buf[sizeof(T) * 2 - 1 - i];
		buf[sizeof(T) * 2 - 1 - i] = tmp;
	}
	return buf;
}

static InstallationWizard::PartVolInfo* GetSelectedPartitions(InstallationWizard* p, InstallationWizard::PartVolInfo*& pboot, int i = CTL_ERR, int index = CTL_ERR)
{
	if (i == CTL_ERR) i = p->PartList.GetSelectionMark();
	if (i == CTL_ERR) i = -2;
	else for (int j = 0; i != j; ++j)
		if (!p->PartInfoVector[j].GPT && (p->PartInfoVector[j].PartTableInfo.MBR.Type == 0x05 || p->PartInfoVector[j].PartTableInfo.MBR.Type == 0x0F))
			++i;
	auto ptarget = i != -2 ? &p->PartInfoVector[i] : nullptr;

	pboot = nullptr;
	if (index == CTL_ERR)
		index = p->BootPartList.GetSelectionMark();
	if (index == CTL_ERR)
		return ptarget;
	if (p->DontBoot.GetCheck() == BST_UNCHECKED)
		if (p->BootMode.GetWindowText() == L"UEFI")
			for (auto& i : p->PartInfoVector)
			{
				if (i.GPT && i.PartTableInfo.GPT.Type == ESP)
					if (index == 0)
					{
						pboot = &i;
						break;
					}
					else --index;
			}
		else if (p->ExtraBootOption.GetCheck() == BST_UNCHECKED)
		{
			for (auto& i : p->PartInfoVector)
				if (!i.GPT
					&& (i.Filesystem == L"FAT32" || i.Filesystem == L"FAT" || i.Filesystem == L"exFAT" || i.Filesystem == L"NTFS"))
					if (index == 0)
					{
						pboot = &i;
						break;
					}
					else --index;
		}
		else pboot = ptarget;

	return ptarget;
}

static bool IsVHDOnBootDisk(PCWSTR pszVHDPath, InstallationWizard::PartVolInfo* pboot)
{
	HANDLE hFile = CreateFileW(pszVHDPath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	DWORD cb = GetFinalPathNameByHandleW(hFile, nullptr, 0, VOLUME_NAME_NT);
	if (!cb)
	{
		CloseHandle(hFile);
		return false;
	}
	MyUniquePtr<WCHAR> pszPath = cb + 14;
	memcpy(pszPath, L"\\\\?\\GLOBALROOT", 14 * sizeof(WCHAR));
	GetFinalPathNameByHandleW(hFile, pszPath + 14, cb, VOLUME_NAME_NT);
	pszPath[14 + cb - GetFinalPathNameByHandleW(hFile, nullptr, 0, VOLUME_NAME_NONE)] = 0;
	CloseHandle(hFile);

	hFile = CreateFileW(pszPath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	STORAGE_DEVICE_NUMBER sdn;
	if (DeviceIoControl(hFile, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &sdn, sizeof(sdn), &cb, nullptr))
	{
		CloseHandle(hFile);
		return sdn.DeviceNumber == wcstoul(pboot->pDiskInfo->Name.GetPointer() + 17, nullptr, 10);
	}
	CloseHandle(hFile);
	return false;
}

LRESULT InstallationWizard::OnNotify(LPNMHDR lpNotifyMessageHandler)
{
	if (lpNotifyMessageHandler->hwndFrom == PartList && lpNotifyMessageHandler->code == LVN_ITEMCHANGED && State != Updating && State != UpdatingMultiRequests)
	{
		if (reinterpret_cast<LPNMLISTVIEW>(lpNotifyMessageHandler)->iItem == CTL_ERR || !(reinterpret_cast<LPNMLISTVIEW>(lpNotifyMessageHandler)->uNewState & LVIS_SELECTED))
		{
			Next.EnableWindow(false);
			Detail.SetWindowText(nullptr);
			Invalidate(false);
			return 0;
		}
		PartVolInfo* pboot;
		const auto& info = *GetSelectedPartitions(this, pboot, reinterpret_cast<LPNMLISTVIEW>(lpNotifyMessageHandler)->iItem);

		if (!pboot
			|| info.pDiskInfo->VHDPath.Empty()
			|| pboot->pDiskInfo == info.pDiskInfo)
			BootFromVHD.ShowWindow(SW_HIDE);
		else if (IsVHDOnBootDisk(info.pDiskInfo->VHDPath, pboot))
			BootFromVHD.ShowWindow(SW_SHOW);
		else
			BootFromVHD.ShowWindow(SW_HIDE);

		String PartDetail;

		if (info.GPT)
		{
			const GUID& type = info.PartTableInfo.GPT.Type;
			String PartGUID = GUID2String(info.PartTableInfo.GPT.id);
			String TypeGUID = GUID2String(type);
			PartDetail = ResStrFormat(String_GPTDetail, PartGUID.GetPointer(), info.PartTableInfo.GPT.PartName, TypeGUID.GetPointer());
			String Type;
			if (type == ESP)
				Type = GetString(String_ESP);
			else if (type == BDP)
				Type = GetString(String_BDP);
			else if (type == MSReserved)
				Type = GetString(String_MSReserved);
			else if (type == MSRecovery)
				Type = GetString(String_MSRecovery);
			if (!Type.Empty())
				PartDetail += format(L" ({})", Type.GetPointer());
		}
		else
		{
			String type = ToString(info.PartTableInfo.MBR.Type);
			String bootIndicator = GetString(String_False - info.PartTableInfo.MBR.bootIndicator);
			String Primary = GetString(String_False - info.PartTableInfo.MBR.Primary);
			PartDetail = ResStrFormat(String_MBRDetail, Primary.GetPointer(), bootIndicator.GetPointer(), type.GetPointer());
		}
		String Identifier = info.GPT ? GUID2String(info.pDiskInfo->id) : ToString(info.pDiskInfo->Signature);
		WCHAR buf[16];
		StrFormatByteSizeW(info.pDiskInfo->ullSize, buf, 16);
		String Detail = ResStrFormat(String_Detail, info.pDiskInfo->FriendlyName.GetPointer(), Identifier.GetPointer(), info.pDiskInfo->AdaptorName.GetPointer(), info.pDiskInfo->DevicePath.GetPointer(), buf, PartDetail.GetPointer());
		this->Detail.SetWindowText(Detail);

		if (info.System)
			State = WarningSystem;
		else if (info.Page)
			State = WarningPage;
		else if (info.ullSize < kMinTargetDiskBytes)
			State = WarningSpaceNotEnough;
		else
			State = Clear;

		if (State == Clear && (pboot || DontBoot.GetCheck()))
			Next.EnableWindow(true);
		else
			Next.EnableWindow(false);

		if (BootMode.GetWindowText() == L"Legacy BIOS")
			if (info.GPT)
			{
				ExtraBootOption.EnableWindow(false);
				if (ExtraBootOption.GetCheck())
				{
					ExtraBootOption.SetCheck(BST_UNCHECKED);
					SwitchExtraBootOption();
				}
			}
			else
				ExtraBootOption.EnableWindow(info.PartTableInfo.MBR.Primary);

		Invalidate(false);
	}
	else if (lpNotifyMessageHandler->hwndFrom == BootPartList && lpNotifyMessageHandler->code == LVN_ITEMCHANGED && State == Clear && reinterpret_cast<LPNMLISTVIEW>(lpNotifyMessageHandler)->iItem != CTL_ERR)
	{
		PartVolInfo* pboot;
		auto ptarget = GetSelectedPartitions(this, pboot, CTL_ERR, reinterpret_cast<LPNMLISTVIEW>(lpNotifyMessageHandler)->iItem);
		if (pboot && ptarget)
		{
			Next.EnableWindow(true);
			if (ptarget->pDiskInfo->VHDPath.Empty()
				|| ptarget->pDiskInfo == pboot->pDiskInfo)
				BootFromVHD.ShowWindow(SW_HIDE);
			else if (IsVHDOnBootDisk(ptarget->pDiskInfo->VHDPath, pboot))
				BootFromVHD.ShowWindow(SW_SHOW);
			else
				BootFromVHD.ShowWindow(SW_HIDE);
		}
		else
			BootFromVHD.ShowWindow(SW_HIDE);
	}
	else if (lpNotifyMessageHandler->code == TTN_LINKCLICK)
		ShellExecuteA(nullptr, "open", "https://support.microsoft.com/help/5025885", nullptr, nullptr, SW_SHOWNORMAL);

	return 0;
}

DWORD InstallationWizard::OnDeviceChanged(WORD wEvent, LPCVOID)
{
	switch (wEvent)
	{
	case DBT_DEVNODES_CHANGED:
	case DBT_DEVICEARRIVAL:
	case DBT_DEVICEREMOVECOMPLETE:
		if (State != Formatting)
			RefreshInfo();
	}
	return TRUE;
}

void InstallationWizard::OnClose()
{
	if (State == Formatting)
		return;
	if (State == Done)
	{
		HMENU hMenu = GetSubMenu(GetSubMenu(GetMenu(hWnd), 0), 6);
		MENUITEMINFOW mii = { .cbSize = sizeof(mii), .fMask = MIIM_STATE };
		GetMenuItemInfoW(hMenu, 0, TRUE, &mii);
		InstDotNet3 = mii.fState == MFS_CHECKED;
		GetMenuItemInfoW(hMenu, 1, TRUE, &mii);
		DontInstWinRe = mii.fState == MFS_CHECKED;
		hMenu = GetSubMenu(hMenu, 2);
		Edition = -1;
		do
			GetMenuItemInfoW(hMenu, ++Edition, TRUE, &mii);
		while (mii.fState != MFS_CHECKED);
		if (BootMode.GetWindowText() == L"UEFI")
		{
			EFI = 1;
			ctx.TargetImageInfo.bHasBootEX = ExtraBootOption.GetCheck() == TRUE;
		}
		else EFI = 0;
	}
	else
	{
		EnableWindow(false);
		HANDLE hThread = OpenThread(SYNCHRONIZE, FALSE, idThread);
		if (hThread)
		{
			PostThreadMessageW(idThread, Msg_Quit, 0, 0);
			do ::DispatchAllMessages();
			while (WaitForSingleObject(hThread, 10) != WAIT_OBJECT_0);
			CloseHandle(hThread);
		}
		hThread = OpenThread(SYNCHRONIZE, FALSE, AssigningLetterDlg.idThread);
		if (hThread)
		{
			PostThreadMessageW(AssigningLetterDlg.idThread, Msg_Quit, 0, 0);
			do
				::DispatchAllMessages();
			while (WaitForSingleObject(hThread, 10) != WAIT_OBJECT_0);
			CloseHandle(hThread);
		}
	}
	DestroyWindow();
}

HBRUSH InstallationWizard::OnControlColorStatic(HDC hDC, WindowBase Window)
{
	if (Window == Detail)
		return OnControlColorEdit(hDC, Window);
	else
		return Window::OnControlColorStatic(hDC, Window);
}

void InstallationWizard::OnSize(BYTE type, int nClientWidth, int nClientHeight, WindowBatchPositioner)
{
	SetProcessEfficiencyMode(type == SIZE_MAXIMIZED);
}

void InstallationWizard::EnableSetBootPart(bool bEnable)
{
	HMENU hMenu = GetMenu(hWnd);
	hMenu = GetSubMenu(hMenu, 1);
	MENUITEMINFOW mii = {
		.cbSize = sizeof(mii),
		.fMask = MIIM_STATE,
		.fState = UINT(bEnable ? MFS_ENABLED : MFS_DISABLED)
	};
	SetMenuItemInfoW(hMenu, 2, TRUE, &mii);
	SetMenuItemInfoW(hMenu, 3, TRUE, &mii);
	SetMenuItemInfoW(hMenu, 4, TRUE, &mii);
	SetMenuItemInfoW(hMenu, 5, TRUE, &mii);
}

void InstallationWizard::EditTextChanged()
{
	if (TargetPath.GetWindowTextLength() != 0)
		if (DontBoot.GetCheck() == BST_CHECKED || Boot.GetWindowTextLength() != 0)
		{
			Next.EnableWindow(true);
			return;
		}

	Next.EnableWindow(false);
}

void InstallationWizard::RefreshInfo()
{
	if (!idThread || State == UpdatingMultiRequests)
		return;

	if (State == Updating)
		State = UpdatingMultiRequests;
	else
	{
		State = Updating;
		Ring.ShowWindow(SW_SHOW);
		Ring.Start();
		Refresh.EnableWindow(false);
		Next.EnableWindow(false);
		PartList.EnableWindow(false);
		EnableSetBootPart(false);
		BootPartList.DeleteAllItems();
		Detail.SetWindowText(nullptr);
		ExtraBootOption.SetCheck(BST_UNCHECKED);
		ExtraBootOption.EnableWindow(false);
		Invalidate(false);
		PostThreadMessageW(idThread, Msg_Refresh, 0, 0);
	}
}

inline
static bool GetAllAttachedVHDs(std::vector<pair<String, DWORD>>& VHDs)
{
	MyUniqueBuffer<PWSTR> pathListBuffer;
	DWORD opStatus;
	ULONG pathListSizeInBytes = 0;

	do
	{
		opStatus = GetAllAttachedVirtualDiskPhysicalPaths(&pathListSizeInBytes, pathListBuffer);
		if (opStatus == ERROR_SUCCESS)
			break;

		if (opStatus != ERROR_INSUFFICIENT_BUFFER)
			goto Cleanup;

		pathListBuffer.reset(pathListSizeInBytes);
		if (!pathListBuffer)
		{
			opStatus = ERROR_OUTOFMEMORY;
			goto Cleanup;
		}

	} while (opStatus == ERROR_INSUFFICIENT_BUFFER);

	if (!pathListBuffer || !pathListBuffer[0])
		goto Cleanup;

	for (PWSTR path = pathListBuffer; *path; path += wcslen(path) + 1)
	{
		HANDLE hDisk;
		VIRTUAL_STORAGE_TYPE storageType{};
		OPEN_VIRTUAL_DISK_PARAMETERS openParameters = {
			.Version = OPEN_VIRTUAL_DISK_VERSION_2,
			.Version2 = { .GetInfoOnly = TRUE }
		};
		if (OpenVirtualDisk(&storageType, path, VIRTUAL_DISK_ACCESS_NONE, OPEN_VIRTUAL_DISK_FLAG_NO_PARENTS, &openParameters, &hDisk) != ERROR_SUCCESS)
			continue;
		GET_VIRTUAL_DISK_INFO diskInfo = {
			.Version = GET_VIRTUAL_DISK_INFO_VIRTUAL_STORAGE_TYPE
		};
		ULONG diskInfoSize = sizeof(GET_VIRTUAL_DISK_INFO);
		if (GetVirtualDiskInformation(hDisk, &diskInfoSize, &diskInfo, nullptr) != ERROR_SUCCESS)
		{
			CloseHandle(hDisk);
			continue;
		}
		WCHAR PhysicalPath[32];
		diskInfoSize = sizeof(PhysicalPath);
		if (GetVirtualDiskPhysicalPath(hDisk, &diskInfoSize, PhysicalPath) != ERROR_SUCCESS)
		{
			CloseHandle(hDisk);
			continue;
		}
		CloseHandle(hDisk);
		if (diskInfo.VirtualStorageType.DeviceId == VIRTUAL_STORAGE_TYPE_DEVICE_VHD
			|| diskInfo.VirtualStorageType.DeviceId == VIRTUAL_STORAGE_TYPE_DEVICE_VHDX)
			VHDs.push_back(make_pair(path, wcstoul(PhysicalPath + 17, nullptr, 10)));
	}


Cleanup:
	if (opStatus != ERROR_SUCCESS)
		SetLastError(opStatus);

	return opStatus == ERROR_SUCCESS;
}

void InstallationWizard::Continue()
{
	Next.EnableWindow(false);
	if (!idThread)
	{
		String text = TargetPath.GetWindowText();
		if (text.end()[-1] == ':')
			text += '\\';
		HANDLE hFile = CreateFileW(text, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			ErrorMessageBox();
			return;
		}
		if (!(GetFileAttributesByHandle(hFile) & FILE_ATTRIBUTE_DIRECTORY))
		{
			CloseHandle(hFile);
			SetLastError(ERROR_DIRECTORY);
			ErrorMessageBox();
			return;
		}
		if (GetFinalPathNameByHandleW(hFile, nullptr, 0, VOLUME_NAME_NONE) != 2)
		{
			MessageBox(GetString(String_NotVolumeRoot), GetString(String_Notice), MB_ICONERROR);
			CloseHandle(hFile);
			return;
		}

		STORAGE_DEVICE_NUMBER sdn;
		DWORD dwBytesReturned = 0;
		DWORD TargetDiskNumber = static_cast<DWORD>(-1);
		if (!DeviceIoControl(hFile, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &sdn, sizeof(sdn), &dwBytesReturned, nullptr))
			TargetDiskNumber = sdn.DeviceNumber;

		WCHAR fs[16] = {};
		GetVolumeInformationByHandleW(hFile, nullptr, 0, nullptr, 0, nullptr, fs, 16);
		CloseHandle(hFile);
		if (!fs[0])
			wcscpy_s(fs, L"Unkonwn");
		if (_wcsicmp(fs, L"ReFS") == 0)
		{
			if (BootMode.GetWindowText() != L"UEFI")
			{
				MessageBox(GetString(String_CannotBootFromReFSunderLegacy), GetString(String_Notice), MB_ICONERROR);
				return;
			}
			else if (MessageBox(GetString(String_ReFSVolume), GetString(String_Notice), MB_ICONQUESTION | MB_YESNO) == IDNO)
			{
				Next.EnableWindow(true);
				return;
			}
		}
		else if (_wcsicmp(fs, L"NTFS") != 0)
		{
			MessageBox(ResStrFormat(String_FilesystemNotSupported, fs),
				GetString(String_Notice), MB_ICONERROR);
			return;
		}

		std::error_code ec;
		auto iter = std::filesystem::directory_iterator(text.GetPointer(), ec);
		if (ec)
		{
			ErrorMessageBox();
			return;
		}

		SetCurrentDirectoryW(ctx.PathUUP.c_str());
		WIMStruct* wim;
		int errcode = wimlib_open_wim(ctx.TargetImageInfo.SystemESD.c_str(), 0, &wim);
		if (errcode != 0)
		{
			MessageBox(wimlib_get_error_string(static_cast<wimlib_error_code>(errcode)), nullptr, MB_ICONERROR);
			ExitProcess(E_UNEXPECTED);
		}
		vector<String> v;
		errcode = wimlib_iterate_dir_tree(wim, 3, L"\\", WIMLIB_ITERATE_DIR_TREE_FLAG_CHILDREN,
			[](const struct wimlib_dir_entry* dentry, void* user_ctx)
			{
				if (_wcsicmp(dentry->filename, L"$Recycle.Bin") != 0)
					reinterpret_cast<vector<String>*>(user_ctx)->push_back(dentry->filename);
				return 0;
			}, &v);
		wimlib_free(wim);
		if (errcode != 0)
		{
			MessageBox(wimlib_get_error_string(static_cast<wimlib_error_code>(errcode)), nullptr, MB_ICONERROR);
			ExitProcess(E_UNEXPECTED);
		}

		vector<String> FilesNeedToDeleted;
		for (const auto& entry : iter)
		{
			auto filename = entry.path().filename();
			auto it = std::find_if(v.begin(), v.end(),
				[&](const String& fn) { return fn.CompareCaseInsensitive(filename.c_str()); });
			if (it != v.end())
			{
				FilesNeedToDeleted.push_back(std::move(*it));
				v.erase(it);
			}
		}

		if (!FilesNeedToDeleted.empty())
		{
			wstring Text = GetString(String_FoundSystemFiles).GetPointer();
			for (const auto& i : FilesNeedToDeleted)
				Text += format(L"\r\n    ●{}", i.begin());
			MessageBox(Text.c_str(), GetString(String_Notice), MB_ICONINFORMATION);
			Next.EnableWindow(true);
			return;
		}

		ULARGE_INTEGER Size;
		if (!GetDiskFreeSpaceExW(text, nullptr, nullptr, &Size))
		{
			ErrorMessageBox();
			return;
		}
		if (Size.QuadPart < kMinTargetDiskBytes)
		{
			MessageBox(GetString(String_VolumeFreeSpaceNotEnough), GetString(String_Notice), MB_ICONERROR);
			Next.EnableWindow(true);
			return;
		}

		Target = std::move(text);

		DWORD BootDiskNumber = static_cast<DWORD>(-1);
		if (DontBoot.GetCheck() == BST_UNCHECKED)
		{
			text = Boot.GetWindowText();
			if (text.end()[-1] == ':')
				text += '\\';
			else if (_wcsnicmp(text, L"\\\\?\\GLOBALROOT", 14) == 0 && text.end()[-1] != '\\')
				text += '\\';
			HANDLE hFile = CreateFileW(text, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
			if (hFile == INVALID_HANDLE_VALUE)
			{
				ErrorMessageBox();
				return;
			}
			if (!(GetFileAttributesByHandle(hFile) & FILE_ATTRIBUTE_DIRECTORY))
			{
				CloseHandle(hFile);
				SetLastError(ERROR_DIRECTORY);
				ErrorMessageBox();
				return;
			}

			if (!DeviceIoControl(hFile, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &sdn, sizeof(sdn), &dwBytesReturned, nullptr))
				BootDiskNumber = sdn.DeviceNumber;

			if (GetFinalPathNameByHandleW(hFile, nullptr, 0, VOLUME_NAME_NONE) != 2)
			{
				MessageBox(GetString(String_NotVolumeRoot), GetString(String_Notice), MB_ICONERROR);
				CloseHandle(hFile);
				return;
			}

			auto len = GetFinalPathNameByHandleW(hFile, nullptr, 0, VOLUME_NAME_NT);
			if (len == 0)
			{
				ErrorMessageBox();
				CloseHandle(hFile);
				return;
			}
			BootPath.Resize(len + 14);
			wcscpy_s(BootPath.GetPointer(), len + 14, L"\\\\?\\GLOBALROOT");
			GetFinalPathNameByHandleW(hFile, BootPath.GetPointer() + 14, len, VOLUME_NAME_NT);
			CloseHandle(hFile);

			vector<pair<String, DWORD>> VHDs;
			if (BootDiskNumber != TargetDiskNumber
				&& GetAllAttachedVHDs(VHDs))
				for (const auto& [Path, Number] : VHDs)
					if (Number == TargetDiskNumber)
					{
						switch (MessageBox(GetString(String_BootFromVHD), GetString(String_Notice), MB_ICONQUESTION | MB_YESNOCANCEL))
						{
						case IDYES:
							BootVHD = Path;
						case IDNO:
							break;
						case IDCANCEL:
							return;
						}
						break;
					}
		}
		State = Done;
		OnClose();
		return;
	}

	InstallationWizard::PartVolInfo* pboot, * ptarget = GetSelectedPartitions(this, pboot);
	if (BootMode.GetWindowText() == L"Legacy BIOS" && ExtraBootOption.GetCheck() == BST_CHECKED)
		pboot = ptarget;
	if (ptarget->DosDeviceLetter == ' ' || ptarget->DosDeviceLetter == 0)
	{
		DeleteLetterAfterInstallation = true;
		DWORD dwLogicalDrives = GetLogicalDrives();
		for (BYTE i = 0; i < 26; ++i)
			if (!(dwLogicalDrives & (1UL << i)))
			{
				cLetter = 'A' + i;
				break;
			}

		if (cLetter == ' ' || cLetter == 0)
		{
			MessageBox(GetString(String_NeedFreeLetter), GetString(String_Notice), MB_ICONWARNING);
			return;
		}
	}

	Ring.ShowWindow(SW_SHOW);
	Ring.Start();
	Refresh.EnableWindow(false);
	DontBoot.EnableWindow(false);
	ExtraBootOption.EnableWindow(false);
	PartList.EnableWindow(false);
	State = Formatting;
	Invalidate();
	::DispatchAllMessages();
	PostThreadMessageW(idThread, Msg_Format, reinterpret_cast<WPARAM>(ptarget), reinterpret_cast<LPARAM>(pboot));
}

void InstallationWizard::DontBootButtonClicked()
{
	bool bDontBoot = DontBoot.GetCheck() == BST_CHECKED;
	Boot.EnableWindow(!bDontBoot);
	BrowseBootPath.EnableWindow(!bDontBoot);
	BootMode.EnableWindow(!bDontBoot);

	if (!idThread)
	{
		if (BootMode.GetWindowText() == L"UEFI")
			ExtraBootOption.ShowWindow(!bDontBoot);
		EditTextChanged();
		return;
	}

	if (bDontBoot)
	{
		BootPartList.DeleteAllItems();
		BootPartList.ShowWindow(SW_HIDE);
		if (PartList.GetSelectionMark() != -1 && State == Clear)
			Next.EnableWindow(true);
		ExtraBootOption.ShowWindow(SW_HIDE);
	}
	else
	{
		BootPartList.ShowWindow(SW_SHOW);
		BootFromVHD.ShowWindow(SW_SHOW);
		ExtraBootOption.ShowWindow(SW_SHOW);
		SetBootPartList();
		Next.EnableWindow(false);
		ExtraBootOption.SetCheck(BST_UNCHECKED);
	}

	BootFromVHD.ShowWindow(SW_HIDE);
	Invalidate(false);
}

void InstallationWizard::SwitchExtraBootOption()
{
	if (BootMode.GetWindowText() == L"Legacy BIOS")
	{
		if (ExtraBootOption.GetCheck())
		{
			BootPartList.ShowWindow(SW_HIDE);
			BootPartList.DeleteAllItems();
			if (State == Clear)
				Next.EnableWindow(true);
		}
		else
		{
			BootPartList.ShowWindow(SW_SHOW);
			SetBootPartList();
			Next.EnableWindow(false);
		}

		RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASENOW | RDW_UPDATENOW);
	}
}

void InstallationWizard::SwitchBootMode()
{
	if (BootMode.GetWindowText() == L"UEFI")
	{
		ExtraBootOption.SetWindowText(GetString(String_UseBootEX));
		ToolTip.Activate();
		ExtraBootOption.EnableWindow(ctx.TargetImageInfo.bHasBootEX);
		if (!idThread)
			ExtraBootOption.ShowWindow(SW_SHOW);
	}
	else
	{
		if (!idThread)
			ExtraBootOption.ShowWindow(SW_HIDE);
		else if (FormatOptions.ReFS)
		{
			MessageBox(GetString(String_CannotBootFromReFSunderLegacy), GetString(String_Notice), MB_ICONERROR);
			BootMode.SetCurSel(int(!BootMode.GetCurSel()));
			return;
		}
		ExtraBootOption.SetWindowText(GetString(String_SetSystemPartAsBootPart));
		ToolTip.Activate(false);

		int sel = PartList.GetSelectionMark();
		if (sel != -1)
		{
			auto& Part = PartInfoVector[sel];
			ExtraBootOption.EnableWindow(!Part.GPT);
		}
		else
			ExtraBootOption.EnableWindow(false);
	}

	SIZE size;
	ExtraBootOption.GetIdealSize(&size);
	ExtraBootOption.SetWindowPos(0, 0, size.cx, size.cy, SWP_NOMOVE);
	ExtraBootOption.SetCheck(BST_UNCHECKED);
	RedrawWindow(nullptr, nullptr, RDW_UPDATENOW | RDW_INVALIDATE);
	if (!idThread)
	{
		EditTextChanged();
		return;
	}

	Next.EnableWindow(false);
	SetBootPartList();
}

void InstallationWizard::SetFormatOptions()
{
	struct FormatOptionsDlg : DialogEx2<InstallationWizard>
	{
		FormatOptionsDlg(InstallationWizard* p) : DialogEx2(p, GetFontSize() * 22, GetFontSize() * 14, WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_FIXEDSYS, nullptr),
			Label(this, 0), Force(this, 0, ButtonStyle::AutoCheckbox), Quick(this, 0, ButtonStyle::AutoCheckbox), ReFS(this, &FormatOptionsDlg::EnableFormatingAsReFS, ButtonStyle::Checkbox)
		{
			PCWSTR text;
			int len = LoadStringW(GetModuleHandleW(nullptr), String_FormatOptions, reinterpret_cast<LPWSTR>(&text), 0);

			wstring str(text, len);
			auto pos = str.find('&');
			if (str[pos - 1] == '(')
				str.erase(pos - 1, 4);
			else
				str.erase(pos);

			Dialog::DialogTitle = str.c_str();
		}

		void Init()
		{
			CenterWindow(Parent);
			auto& opt = static_cast<InstallationWizard*>(Parent)->FormatOptions;
			Label.SetWindowText(opt.Label);
			Force.SetWindowText(GetString(String_Force));
			Force.SetCheck(opt.bForce);
			Quick.SetWindowText(GetString(String_Quick));
			Quick.SetCheck(opt.bQuick);
			ReFS.SetWindowText(GetString(String_FormatAsReFS));
			ReFS.SetCheck(opt.ReFS);

			Label.MoveWindow(GetFontSize() * 2, GetFontSize() * 3, GetFontSize() * 18, GetFontSize() * 2);
			SIZE size;
			Force.GetIdealSize(&size);
			Force.MoveWindow(GetFontSize() * 2, GetFontSize() * 8, size.cx, size.cy);
			Quick.GetIdealSize(&size);
			Quick.MoveWindow(GetFontSize() * 2, GetFontSize() * 8 + size.cy, size.cx, size.cy);
			ReFS.GetIdealSize(&size);
			ReFS.MoveWindow(GetFontSize() * 2, GetFontSize() * 8 + size.cy * 2, size.cx, size.cy);
		}

		void OnDraw(HDC hdc, RECT rect)
		{
			rect.top = GetFontSize();
			rect.left = rect.top * 2;
			DrawText(hdc, String_Label, &rect, DT_SINGLELINE);
		}

		void OnDestroy()
		{
			auto& opt = GetParent()->FormatOptions;
			opt.Label = Label.GetWindowText();
			opt.bForce = Force.GetState();
			opt.bQuick = Quick.GetState();
		}
		void EnableFormatingAsReFS()
		{
			UINT idString;
			wstring AdditionalText;
			auto& FormatOptions = GetParent()->FormatOptions;
			do
			{
				if (!FormatOptions.ReFS)
				{
					const auto& Version = GetParent()->ctx.TargetImageInfo.Version;
					VersionStruct TargetVersion;
					ParseVersionString(Version.c_str(), TargetVersion);

					if (const auto arch = GetParent()->ctx.TargetImageInfo.Arch;
						TargetVersion.dwBuild < 20185
						|| arch != PROCESSOR_ARCHITECTURE_AMD64
						&& arch != PROCESSOR_ARCHITECTURE_ARM64)
					{
						idString = String_CannotFormatAsReFS;
						AdditionalText = Version;
						break;
					}

					OSVERSIONINFOW osvi;
					VersionStruct version;
					GetNtKernelVersion(version);
					RtlGetVersion(&osvi);
					if (osvi.dwBuildNumber < 10586)
					{
						idString = String_UnableToFormatAsReFS;
						AdditionalText = format(L"{}.{}.{}.{}", osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber, version.dwSpBuild);
						break;
					}

					if (static_cast<InstallationWizard*>(Parent)->BootMode.GetWindowText() == L"Legacy BIOS")
					{
						idString = String_CannotBootFromReFSunderLegacy;
						break;
					}
				}

				if (MessageBox(GetString(String_ReFSVolume), GetString(String_Notice), MB_ICONQUESTION | MB_YESNO) == IDYES)
				{
					FormatOptions.ReFS = ~FormatOptions.ReFS;
					ReFS.SetCheck(FormatOptions.ReFS);
				}
				return;
			} while (false);
			MessageBox(GetString(idString) + AdditionalText.c_str(), GetString(String_Notice), MB_ICONERROR);
		}

		Edit Label;
		Button Force;
		Button Quick;
		Button ReFS;
	}Dlg(this);

	Dlg.ModalDialogBox();
}

void InstallationWizard::LetterDlg::Init(LPARAM)
{
	if (!static_cast<InstallationWizard*>(Parent)->idThread || !idThread)
	{
		SetLastError(ERROR_ACCESS_DENIED);
		ErrorMessageBox();
		EndDialog(0);
		return;
	}
	int pxUnit = GetFontSize();
	CenterWindow(Parent);

	PartVolList.InsertColumn(GetString(String_Letter).GetPointer(), pxUnit * 3, 0);
	PartVolList.InsertColumn(GetString(String_CurrentSystemLetter).GetPointer(), pxUnit * 5, 1);
	PartVolList.InsertColumn(GetString(String_Label).GetPointer(), pxUnit * 14, 2);
	PartVolList.InsertColumn(GetString(String_Size).GetPointer(), pxUnit * 7, 3);
	PartVolList.InsertColumn(GetString(String_Filesystem).GetPointer(), pxUnit * 6, 4);
	PartVolList.InsertColumn(GetString(String_Type).GetPointer(), pxUnit * 6, 5);
	PartVolList.InsertColumn(GetString(String_Dynamic).GetPointer(), pxUnit * 3, 6);
	PartVolList.InsertColumn(GetString(String_Disk).GetPointer(), pxUnit * 10, 7);
	PartVolList.MoveWindow(pxUnit, pxUnit * 6, pxUnit * 56, pxUnit * 10);

	Letter.ShowWindow(SW_HIDE);
	Letter.MoveWindow(pxUnit * 25, pxUnit * 9, pxUnit * 6, 1);

	Refresh.EnableWindow(false);
	SetIt.EnableWindow(false);
	Refresh.SetWindowText(GetString(String_Refresh));
	SetIt.SetWindowText(GetString(String_Set));
	Refresh.MoveWindow(pxUnit, pxUnit * 17, pxUnit * 5, pxUnit * 2);
	SetIt.MoveWindow(pxUnit * 52, pxUnit * 17, pxUnit * 5, pxUnit * 2);

	PostThreadMessageW(idThread, Msg_Refresh, 0, 0);
}

void InstallationWizard::LetterDlg::OnDraw(HDC hdc, RECT rect)
{
	rect.top = GetFontSize();
	rect.left = rect.top;
	rect.right -= rect.left;
	DrawText(hdc, String_SetLetters, &rect, DT_WORDBREAK);

	if ((GetWindowLongW(Refresh, GWL_STYLE) & WS_DISABLED) == WS_DISABLED)
		DrawText(hdc, String_Updating, &rect, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
}

LRESULT InstallationWizard::LetterDlg::OnNotify(LPNMHDR lpnmhdr)
{
	if (lpnmhdr->hwndFrom == PartVolList && lpnmhdr->code == LVN_ITEMCHANGED && PartVolList.IsWindowVisible())
	{
		int sel = reinterpret_cast<LPNMLISTVIEW>(lpnmhdr)->iItem;
		if (reinterpret_cast<LPNMLISTVIEW>(lpnmhdr)->uNewState & LVIS_SELECTED)
			SetIt.EnableWindow(sel + 1);
	}

	return 0;
}

void InstallationWizard::LetterDlg::OnDestroy()
{
	auto& liv = static_cast<InstallationWizard*>(Parent)->LetterInfoVector;
	auto& v = static_cast<InstallationWizard*>(Parent)->VMSettings;
	for (size_t i = 0; i != v.size();)
	{
		auto it = find_if(liv.begin(), liv.end(),
			[&v, i](const LetterInfo& li)
			{
				return v[i].Letter == li.cLetter;
			});

		if (it == liv.end())
			v.erase(v.begin() + i);
		else ++i;
	}

	sort(liv.begin(), liv.end(),
		[](const LetterInfo& _1, const LetterInfo& _2)
		{
			return _1.cLetter < _2.cLetter;
		});
}

INT_PTR InstallationWizard::LetterDlg::DialogProc(UINT Msg, WPARAM wParam, LPARAM lParam)
{
	if (Msg == WM_DEVICECHANGE
		&& (wParam == DBT_DEVNODES_CHANGED || wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE))
		RefreshInfo();
	return Dialog::DialogProc(Msg, wParam, lParam);
}

void InstallationWizard::LetterDlg::Set()
{
	auto& v = static_cast<InstallationWizard*>(Parent)->LetterInfoVector;

	if (!PartVolList.IsWindowVisible())
	{
		int count = 0;
		for (const auto& i : static_cast<InstallationWizard*>(Parent)->PartInfoVector)
			if (!i.Removable
				&& (i.GPT
					|| !i.GPT && (i.PartTableInfo.MBR.Type != 0x05 && i.PartTableInfo.MBR.Type != 0x0F)))
				++count;

		auto GetPart = [this](int i)
			{
				int n = 0;
				for (const auto& j : static_cast<InstallationWizard*>(Parent)->PartInfoVector)
					if (n == i)
						if (j.Removable
							|| !j.GPT && (j.PartTableInfo.MBR.Type == 0x05 || j.PartTableInfo.MBR.Type == 0x0F))
							continue;
						else return j;
					else ++n;

				return static_cast<InstallationWizard*>(Parent)->PartInfoVector.at(-1);
			};

		String Letter = this->Letter.GetWindowText();
		int sel = PartVolList.GetSelectionMark();
		if (Letter == GetString(String_Empty))
		{
			String SettedLetter = PartVolList.GetItemText(sel, 0);
			if (SettedLetter.Empty())
				goto Back;
			PartVolList.SetItemText(sel, 0, PCWSTR(nullptr));
			for (auto i = v.begin(); i != v.end(); ++i)
				if (i->cLetter == SettedLetter[0])
				{
					v.erase(i);
					goto Back;
				}
		}

		if (sel < count)
		{
			const auto& part = GetPart(sel);
			for (auto& i : v)
				if (i.ByGUID && part.GPT && i.id == part.PartTableInfo.GPT.id
					|| !i.ByGUID && !part.GPT && i.MBR.Signature == part.pDiskInfo->Signature && i.MBR.ullOffset == part.ullOffset / part.pDiskInfo->ulBytesPerSector)
				{
					i.cLetter = static_cast<char>(Letter[0]);
					PartVolList.SetItemText(sel, 0, Letter.GetPointer());
					goto Back;
				}

			v.resize(v.size() + 1);
			auto& li = *v.rbegin();
			li.cLetter = static_cast<char>(Letter[0]);
			li.ByGUID = part.GPT;
			if (li.ByGUID)
				li.id = part.PartTableInfo.GPT.id;
			else
			{
				li.MBR.Signature = part.pDiskInfo->Signature;
				li.MBR.ullOffset = part.ullOffset / part.pDiskInfo->ulBytesPerSector;
			}

			PartVolList.SetItemText(sel, 0, Letter.GetPointer());
		}
		else
		{
			const auto& part = Volumes[static_cast<size_t>(sel) - count];

			for (auto& i : v)
				if (i.ByGUID && i.id == part.id)
				{
					i.cLetter = static_cast<int>(Letter[0]);
					PartVolList.SetItemText(sel, 0, Letter.GetPointer());
					goto Back;
				}

			v.resize(v.size() + 1);
			auto& li = *v.rbegin();
			li.cLetter = static_cast<char>(Letter[0]);
			li.ByGUID = true;
			li.id = part.id;
			PartVolList.SetItemText(sel, 0, Letter.GetPointer());
		}

	Back:
		PartVolList.ShowWindow(SW_SHOW);
		this->Letter.ShowWindow(SW_HIDE);
		return;
	}
	else
	{
		int sel = PartVolList.GetSelectionMark();
		PartVolList.ShowWindow(SW_HIDE);
		String SettedLetter = PartVolList.GetItemText(sel, 0);
		while (Letter.DeleteString(0) != CTL_ERR);
		Letter.InsertString(GetString(String_Empty), 0);
		string List;
		List.resize(26);
		for (int i = 0; i != 26; ++i)
			List[i] = 'A' + i;
		for (const auto& i : v)
		{
			if (i.cLetter == SettedLetter[0])
				continue;
			auto pos = List.find(i.cLetter);
			if (pos != string::npos)
				List.erase(List.cbegin() + pos);
		}
		WCHAR buf[2];
		buf[1] = 0;
		for (char c : List)
		{
			buf[0] = c;
			Letter.AddString(buf);
		}
		auto pos = List.find(static_cast<char>(SettedLetter[0]));
		if (pos == string::npos)
			Letter.SetCurSel(0);
		else
			Letter.SetCurSel(static_cast<int>(pos + 1));
		Letter.ShowWindow(SW_SHOW);
	}
}

void InstallationWizard::LetterDlg::RefreshInfo()
{
	PartVolList.ShowWindow(SW_HIDE);
	Letter.ShowWindow(SW_HIDE);
	Refresh.EnableWindow(false);
	SetIt.EnableWindow(false);
	Invalidate();
	PostThreadMessageW(idThread, Msg_Refresh, 0, 0);
}

InstallationWizard::SettingVMDlg::SettingVMDlg(InstallationWizard* p) : Dialog(p, GetFontSize() * 36, GetFontSize() * 22, WS_SYSMENU | WS_CAPTION | WS_BORDER | DS_MODALFRAME | DS_FIXEDSYS, GetString(String_SetVM)),
VolumeList(this, 0, WS_CHILD | WS_BORDER | LVS_SINGLESEL), Auto(this, &InstallationWizard::SettingVMDlg::AutoMgmt, ButtonStyle::AutoCheckbox),
Set(this, &InstallationWizard::SettingVMDlg::SetIt)
{
}

static wstring MakePageSizeRange(VMSetting page)
{
	auto RangeString = format(L"{} MB", ULONG(page.ISize));
	if (page.ISize != page.MSize)
		RangeString += format(L" ~ {} MB", ULONG(page.MSize));
	return RangeString;
}

void InstallationWizard::SettingVMDlg::Init(LPARAM)
{
	int pxUnit = GetFontSize();
	CenterWindow(Parent);

	VolumeList.InsertColumn(GetString(String_Letter).GetPointer(), pxUnit * 7, 0);
	VolumeList.InsertColumn(GetString(String_PageSetting).GetPointer(), pxUnit * 16, 1);
	for (const auto& i : static_cast<InstallationWizard*>(Parent)->LetterInfoVector)
	{
		int index = VolumeList.InsertItem();
		for (auto j : static_cast<InstallationWizard*>(Parent)->VMSettings)
			if (j.Letter == i.cLetter)
			{
				if (j.ISize == 0 && j.MSize == 0)
					VolumeList.SetItemText(index, 1, GetString(String_SysMgmtPage).GetPointer());
				else
					VolumeList.SetItemText(index, 1, MakePageSizeRange(j).c_str());
				goto SetItemLetter;
			}

		VolumeList.SetItemText(index, 1, GetString(String_Empty).GetPointer());
	SetItemLetter:
		WCHAR buf[] = { WCHAR(i.cLetter), L'\0' };
		VolumeList.SetItemText(index, 0, buf);
	}

	Auto.SetWindowText(GetString(String_VMSysAutoMgmt));
	Set.SetWindowText(GetString(String_Set));
	Auto.SetCheck(static_cast<InstallationWizard*>(Parent)->VMSysAutoMgmt);
	SIZE size;
	Auto.GetIdealSize(&size);
	auto [cx, cy] = size;
	Auto.MoveWindow(pxUnit * 35 - cx, pxUnit * 20 - cy / 2, cx, cy);
	if (!static_cast<InstallationWizard*>(Parent)->VMSysAutoMgmt)
		VolumeList.ShowWindow(SW_SHOW);
	VolumeList.MoveWindow(pxUnit * 2, pxUnit * 8, pxUnit * 32, pxUnit * 10);
	Set.MoveWindow(pxUnit, pxUnit * 19, pxUnit * 6, pxUnit * 2);
	Set.EnableWindow(false);
}

LRESULT InstallationWizard::SettingVMDlg::OnNotify(LPNMHDR lpnmhdr)
{
	if (lpnmhdr->hwndFrom == VolumeList && lpnmhdr->code == LVN_ITEMCHANGED && VolumeList.IsWindowVisible())
	{
		int sel = reinterpret_cast<LPNMLISTVIEW>(lpnmhdr)->iItem;
		if (reinterpret_cast<LPNMLISTVIEW>(lpnmhdr)->uNewState & LVIS_SELECTED)
			Set.EnableWindow(sel + 1);
	}

	return 0;
}

void InstallationWizard::SettingVMDlg::OnDraw(HDC hdc, RECT rect)
{
	rect.left = rect.top = GetFontSize();
	rect.right -= rect.left;
	DrawText(hdc, String_VMSetting, &rect, DT_WORDBREAK);

	if (static_cast<InstallationWizard*>(Parent)->VMSysAutoMgmt)
	{
		rect.top *= 3;
		rect.right -= rect.left;
		rect.left *= 2;
		DrawText(hdc, String_VMSysMgmtDescription, &rect, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
	}
}

void InstallationWizard::SettingVMDlg::AutoMgmt()
{
	bool b = !static_cast<InstallationWizard*>(Parent)->VMSysAutoMgmt;
	static_cast<InstallationWizard*>(Parent)->VMSysAutoMgmt = b;
	Set.EnableWindow(false);
	VolumeList.ShowWindow(b ? SW_HIDE : SW_SHOW);
	if (!b) VolumeList.SetSelectionMark(-1);
}

void InstallationWizard::SettingVMDlg::SetIt()
{
	struct Panel : Dialog
	{
		Panel(WindowBase* p) : Dialog(p, GetFontSize() * 19, GetFontSize() * 14, WS_SYSMENU | WS_CAPTION | WS_BORDER | DS_MODALFRAME | DS_FIXEDSYS, GetString(String_SetPage)),
			SysMgmt(this, &Panel::SelChanged, ButtonStyle::AutoRadioButton), Custom(this, &Panel::SelChanged, ButtonStyle::AutoRadioButton), NoPage(this, &Panel::SelChanged, ButtonStyle::AutoRadioButton),
			ISize(this, 0), MSize(this, 0)
		{}

		Button SysMgmt;
		Button Custom;
		Edit ISize;
		Edit MSize;
		Button NoPage;
		VMSetting* Setting;

		void Init(LPARAM lParam)
		{
			SIZE size;
			int pxUnit = GetFontSize();
			CenterWindow(Parent);
			Setting = reinterpret_cast<VMSetting*>(lParam);

			SysMgmt.SetWindowText(GetString(String_SysMgmtPage));
			Custom.SetWindowText(GetString(String_CustomizePage));
			NoPage.SetWindowText(GetString(String_NoPage));

			SysMgmt.GetIdealSize(&size);
			SysMgmt.MoveWindow(pxUnit, pxUnit, size.cx, size.cy);
			Custom.GetIdealSize(&size);
			Custom.MoveWindow(pxUnit, pxUnit + size.cy, size.cx, size.cy);
			ISize.MoveWindow(pxUnit * 12, pxUnit * 5, pxUnit * 6, pxUnit * 2);
			MSize.MoveWindow(pxUnit * 12, pxUnit * 8, pxUnit * 6, pxUnit * 2);
			NoPage.GetIdealSize(&size);
			NoPage.MoveWindow(pxUnit, pxUnit * 11, size.cx, size.cy);

			if (Setting->ISize == 0)
			{
				if (Setting->MSize == 1)
					NoPage.SetCheck(BST_CHECKED);
				else
					SysMgmt.SetCheck(BST_CHECKED);

				ISize.EnableWindow(false);
				MSize.EnableWindow(false);
			}
			else
			{
				Custom.SetCheck(BST_CHECKED);

				ISize.SetWindowText(to_wstring(Setting->ISize).c_str());
				MSize.SetWindowText(to_wstring(Setting->MSize).c_str());

				ISize.EnableWindow(true);
				MSize.EnableWindow(true);
			}

		}

		void OnDraw(HDC hdc, RECT rect)
		{
			rect.left = GetFontSize();
			rect.top = rect.left * 5;
			rect.bottom = rect.top + rect.left * 2;
			DrawText(hdc, String_InitSize, &rect, DT_SINGLELINE | DT_VCENTER);
			rect += rect.left * 3;
			DrawText(hdc, String_MaxSize, &rect, DT_SINGLELINE | DT_VCENTER);
		}

		void SelChanged()
		{
			bool bEnable = Custom.GetCheck() == BST_CHECKED;
			ISize.EnableWindow(bEnable);
			MSize.EnableWindow(bEnable);
		}

		void OnOK()
		{
			if (Custom.GetCheck() == BST_CHECKED)
			{
				String i = ISize.GetWindowText();
				String m = MSize.GetWindowText();
				long iSize, mSize;
				if (i.GetLength() > 8 || m.GetLength() > 8)
					goto InvalidData;
				for (WCHAR c : i)
					if (c < '0' || c > '9')
						goto InvalidData;

				iSize = _wtol(i);
				mSize = _wtoi(m);
				if (iSize < 16 || mSize >= 16777216 || mSize < iSize)
					goto InvalidData;
				Setting->ISize = iSize;
				Setting->MSize = mSize;
				EndDialog(1);
				return;

			InvalidData:
				MessageBox(GetString(String_InvalidPageSize), GetString(String_Notice), MB_ICONERROR);
				return;
			}
			else if (NoPage.GetCheck() == BST_CHECKED)
				EndDialog(0);
			else
			{
				Setting->MSize = 1;
				EndDialog(1);
			}
		}

		bool OnClose()
		{
			OnOK();
			return true;
		}

		void OnCancel()
		{
			OnOK();
		}
	};

	Panel VMPanel(this);

	auto& v = static_cast<InstallationWizard*>(Parent)->VMSettings;
	int sel = VolumeList.GetSelectionMark();
	String letter = VolumeList.GetItemText(sel, 0);
	auto item = find_if(v.begin(), v.end(),
		[&](const VMSetting& i) { return i.Letter == letter[0]; });
	if (item == v.end())
	{
		v.push_back({ letter[0], 0, 1 });
		item = v.end() - 1;
	}

	if (VMPanel.ModalDialogBox(reinterpret_cast<LPARAM>(&*item)))
		if (item->MSize == 1)
		{
			item->ISize = item->MSize = 0;
			VolumeList.SetItemText(sel, 1, GetString(String_SysMgmtPage).GetPointer());
		}
		else
			VolumeList.SetItemText(sel, 1, MakePageSizeRange(*item).c_str());
	else
	{
		v.erase(item);
		VolumeList.SetItemText(sel, 1, GetString(String_Empty));
	}
}
