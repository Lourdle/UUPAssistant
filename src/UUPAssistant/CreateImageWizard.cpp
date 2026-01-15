#include "pch.h"
#include "Resources/resource.h"

#include <Shlwapi.h>
#include <initguid.h>
#include <comdef.h>
#include <wil/com.h>

using namespace Lourdle::UIFramework;
using namespace wil;

inline
static void SwitchPurpose(Window* p)
{
	CreateImageWizard* pThis = static_cast<CreateImageWizard*>(p);
	bool bEndable = pThis->CreateISO.GetCheck();

	pThis->NoLegacyBoot.EnableWindow(bEndable);
	pThis->UefiBootOption.EnableWindow(bEndable);
	if (pThis->ctx.TargetImageInfo.bHasBootEX)
		pThis->BootEX.EnableWindow(bEndable);
	pThis->CDLabel.EnableWindow(bEndable);
	pThis->RemoveHwReq.EnableWindow(bEndable && pThis->ctx.TargetImageInfo.Name.find(L"Windows 11") == 0);
}

static void ChangeFileExtension(Window* p)
{
	CreateImageWizard* pThis = static_cast<CreateImageWizard*>(p);
	PCWSTR pszExt;
	if (pThis->CreateISO.GetCheck())
		pszExt = L".iso";
	else if (pThis->SplitImage.GetCheck() == BST_CHECKED)
		pszExt = L".swm";
	else
		if (pThis->Compression.GetCurSel() == WIMLIB_COMPRESSION_TYPE_LZMS)
			pszExt = L".esd";
		else
			pszExt = L".wim";

	String Path = pThis->Path.GetWindowText();
	if (!PathIsFileSpecW(Path))
	{
		auto p = PathFindExtensionW(Path);
		if (p && wcslen(p) < 4)
			Path.Resize(Path.GetLength() + 4);
		PathRenameExtensionW(Path.GetPointer(), pszExt);
		pThis->Path.SetWindowText(Path);
	}

	SwitchPurpose(p);
}

CreateImageWizard::CreateImageWizard(SessionContext& ctx) : Window(GetFontSize() * 70, GetFontSize() * 60, GetString(String_CreateImage), WS_BORDER | WS_CAPTION | WS_MINIMIZEBOX | WS_SYSMENU),
UpdateDlg(this, ctx), AppInstalling(this, ctx), DrvDlg(this, ctx), ctx(ctx), Cancel(false), CanClose(true),
InstallUpdates(this, &CreateImageWizard::InstallUpdatesClicked, ButtonStyle::CommandLink), InstallAppxes(this, &CreateImageWizard::InstallAppxesClicked, ButtonStyle::CommandLink),
AddDrivers(this, &CreateImageWizard::AddDriversClicked, ButtonStyle::CommandLink), Next(this, &CreateImageWizard::Execute), Browse(this, &CreateImageWizard::BrowseFile),
InstallDotNetFx3(this, 0, ButtonStyle::AutoCheckbox), NoLegacyBoot(this, 0, ButtonStyle::AutoCheckbox), SplitImage(this, 0, ButtonStyle::AutoCheckbox),
UefiBootOption(this), Compression(this, 200), Editions(this, 0, true), Path(this), CDLabel(this), PartSize(this, 0, DWORD(WS_CHILD | WS_BORDER | ES_NUMBER)), StateDetail(this, 0, DWORD(WS_VISIBLE | WS_BORDER | WS_CHILD | ES_READONLY | ES_AUTOVSCROLL | ES_MULTILINE | WS_VSCROLL)),
CreateISO(this, 100, ButtonStyle::AutoRadioButton), CreateWIM(this, 150, ButtonStyle::AutoRadioButton), BootEX(this, 0, ButtonStyle::AutoCheckbox), RemoveHwReq(this, 0, ButtonStyle::AutoCheckbox),
State(this), Ring(this)
{
	RegisterCommand(&CreateImageWizard::DisableDialogControls, nullptr, 1234, 5678);
	RegisterCommand({ CreateISO, 100, BN_CLICKED }, ChangeFileExtension);
	RegisterCommand({ CreateWIM, 150, BN_CLICKED }, ChangeFileExtension);
	RegisterCommand({ Compression, 200, CBN_SELCHANGE }, ChangeFileExtension);
	if (!ctx.TargetImageInfo.SupportLegacyBIOS)
	{
		NoLegacyBoot.SetCheck(BST_CHECKED);
		NoLegacyBoot.EnableWindow(false);
	}

	int pxUnit = GetFontSize();
	Next.SetWindowText(GetString(String_Next));
	AddDrivers.SetWindowText(GetString(String_AddDrivers));
	InstallAppxes.SetWindowText(GetString(String_InstallApps));
	InstallUpdates.SetWindowText(GetString(String_InstallUpdates));
	InstallDotNetFx3.SetWindowText(GetString(String_InstallDotNetFx3));
	NoLegacyBoot.SetWindowText(GetString(String_NoLegacyBoot));
	SplitImage.SetWindowText(GetString(String_SplitImage));
	BootEX.SetWindowText(GetString(String_UseBootEX));
	RegisterCommand({ SplitImage, 0, 0 }, [](Window* w)
		{
			auto p = static_cast<CreateImageWizard*>(w);
			if (p->SplitImage.GetCheck())
			{
				p->PartSize.ShowWindow(SW_SHOW);
				p->PartSize.SetWindowText(L"2048");
			}
			else
				p->PartSize.ShowWindow(SW_HIDE);
			RECT rc;
			p->PartSize.GetWindowRect(&rc);
			ScreenToClient(*w, reinterpret_cast<LPPOINT>(&rc));
			ScreenToClient(*w, reinterpret_cast<LPPOINT>(&rc) + 1);
			rc.right = rc.left - 1;
			rc.left = 0;
			w->InvalidateRect(&rc);
			ChangeFileExtension(w);
		});
	Compression.AddString(GetString(String_NoneCompression));
	Compression.AddString(GetString(String_FastCompression));
	Compression.AddString(GetString(String_MaxCompression));
	Compression.AddString(GetString(String_ExtremeCompression));
	Compression.SetCurSel(2);
	UefiBootOption.AddString(GetString(String_NoUefiBoot));
	UefiBootOption.AddString(GetString(String_UefiBootSupport));
	UefiBootOption.AddString(GetString(String_UefiBootSupportNoPrompt));
	UefiBootOption.SetCurSel(1);
	CreateISO.SetWindowText(GetString(String_CreateISO));
	CreateWIM.SetWindowText(GetString(String_CreateSystemImage));
	CreateISO.SetCheck(BST_CHECKED);
	Browse.SetWindowText(GetString(String_Browse));
	StateDetail.SetReadOnly(true);
	Editions.InsertColumn(GetString(String_AdditionEditions), pxUnit * 21, 0);
	CDLabel.SetWindowText(L"DVD_ROM");
	RemoveHwReq.SetWindowText(GetString(String_RemoveHwReq));
	for (auto& i : ctx.TargetImageInfo.UpgradableEditions)
	{
		auto index = Editions.InsertItem();
		Editions.SetItemText(index, 0, i.c_str());
		if (i == ctx.TargetImageInfo.Edition)
			Editions.SetCheckState(index, BST_CHECKED);
	}
	if (ctx.TargetImageInfo.Name.find(L"Windows 11") == std::wstring::npos)
		RemoveHwReq.EnableWindow(false);

	SIZE size;
	InstallUpdates.GetIdealSize(&size);
	InstallUpdates.MoveWindow(pxUnit * 2, pxUnit * 3, pxUnit * 15, size.cy);
	InstallAppxes.MoveWindow(pxUnit * 2, pxUnit * 7, pxUnit * 15, size.cy);
	AddDrivers.MoveWindow(pxUnit * 2, pxUnit * 11, pxUnit * 15, size.cy);
	CreateISO.GetIdealSize(&size);
	CreateISO.MoveWindow(pxUnit * 2, pxUnit * 35 / 2 - size.cy / 2, size.cx, size.cy);
	int cx = size.cx;
	CreateWIM.GetIdealSize(&size);
	CreateWIM.MoveWindow(pxUnit * 3 + cx, pxUnit * 35 / 2 - size.cy / 2, size.cx, size.cy);
	InstallDotNetFx3.GetIdealSize(&size);
	InstallDotNetFx3.MoveWindow(pxUnit * 2, pxUnit * 20, size.cx, size.cy);
	SplitImage.GetIdealSize(&size);
	SplitImage.MoveWindow(pxUnit * 2, pxUnit * 22, size.cx, size.cy);
	PartSize.MoveWindow(pxUnit * 10, pxUnit * 24, pxUnit * 4, pxUnit * 2);
	NoLegacyBoot.GetIdealSize(&size);
	NoLegacyBoot.MoveWindow(pxUnit * 2, pxUnit * 27, size.cx, size.cy);
	UefiBootOption.MoveWindow(pxUnit * 2, pxUnit * 31, pxUnit * 18, pxUnit * 2);
	BootEX.GetIdealSize(&size);
	BootEX.MoveWindow(pxUnit * 2, pxUnit * 67 / 2, size.cx, size.cy);
	Compression.MoveWindow(pxUnit * 2, pxUnit * 38, pxUnit * 13, pxUnit * 2);
	Path.MoveWindow(pxUnit * 2, pxUnit * 43, pxUnit * 60, pxUnit * 2);
	Browse.MoveWindow(pxUnit * 63, pxUnit * 43, pxUnit * 5, pxUnit * 2);
	Editions.MoveWindow(pxUnit * 40, pxUnit * 5, pxUnit * 24, pxUnit * 26);
	RemoveHwReq.GetIdealSize(&size);
	RemoveHwReq.MoveWindow(pxUnit * 40, pxUnit * 33, size.cx, size.cy);
	CDLabel.MoveWindow(pxUnit * 40, pxUnit * 37, pxUnit * 20, pxUnit * 2);
	StateDetail.MoveWindow(pxUnit * 2, pxUnit * 46, pxUnit * 66, pxUnit * 10);
	StateDetail.SetLimitText(-1);
	Next.MoveWindow(pxUnit * 62, pxUnit * 57, pxUnit * 6, pxUnit * 2);
	State.MoveWindow(pxUnit * 5, pxUnit * 58 - GetFontFullSize() / 2, pxUnit * 40, GetFontFullSize());

	TTTOOLINFOW ti = {
		.cbSize = sizeof(ti),
		.uFlags = TTF_SUBCLASS | TTF_TRANSPARENT | TTF_IDISHWND | TTF_PARSELINKS,
		.hwnd = BootEX,
		.uId = reinterpret_cast<UINT_PTR>(BootEX.GetHandle())
	};
	String str = GetString(String_LearnMoreAboutBootEX);
	ti.lpszText = str.GetPointer();
	Tips.AddTool(&ti);
	ti.hwnd = SplitImage;
	ti.uId = reinterpret_cast<UINT_PTR>(ti.hwnd);
	str = GetString(String_PartSizeTip);
	ti.lpszText = str.GetPointer();
	Tips.AddTool(&ti);
	ti.hwnd = PartSize;
	ti.uId = reinterpret_cast<UINT_PTR>(ti.hwnd);
	Tips.AddTool(&ti);
	Tips.SetMaxTipWidth(pxUnit * 30);
	BootEX.EnableWindow(ctx.TargetImageInfo.bHasBootEX);

	if (!ctx.bAdvancedOptionsAvaliable)
	{
		InstallDotNetFx3.EnableWindow(false);
		InstallAppxes.EnableWindow(false);
		AppInstalling.EnableWindow(false);
		InstallUpdates.EnableWindow(false);
		AddDrivers.EnableWindow(false);
		Editions.EnableWindow(false);
	}
}

void CreateImageWizard::OnDraw(HDC hdc, RECT rect)
{
	int pxUnit = GetFontSize();
	rect.left += pxUnit * 40;
	rect.top += pxUnit * 3;
	rect.bottom = rect.top + pxUnit * 2;
	DrawText(hdc, String_SelectEditions, &rect, DT_SINGLELINE | DT_VCENTER);
	rect.left -= pxUnit * 38;
	if (PartSize.IsWindowVisible())
	{
		rect.top += pxUnit * 21;
		rect.bottom = rect.top + pxUnit * 2;
		DrawText(hdc, String_PartSize, &rect, DT_SINGLELINE | DT_VCENTER);
		rect.top += pxUnit * 5;
	}
	else
		rect.top += pxUnit * 26;
	rect.bottom = rect.top + pxUnit * 2;
	DrawText(hdc, String_UefiOptions, &rect, DT_SINGLELINE | DT_VCENTER);
	rect.top += pxUnit * 6;
	rect.bottom = rect.top + pxUnit * 2;
	rect.left += pxUnit * 38;
	DrawText(hdc, String_CdLabel, &rect, DT_SINGLELINE | DT_VCENTER);
	rect.left -= pxUnit * 38;
	rect.top += pxUnit;
	rect.bottom = rect.top + pxUnit * 2;
	DrawText(hdc, String_SelectCompression, &rect, DT_SINGLELINE | DT_VCENTER);
	rect.top += pxUnit * 5;
	rect.bottom = rect.top + pxUnit * 2;
	DrawText(hdc, String_Path, &rect, DT_SINGLELINE | DT_VCENTER);
	if (Cancel)
	{
		RECT rc;
		State.GetWindowRect(&rc);
		ScreenToClient(*this, reinterpret_cast<LPPOINT>(&rc));
		ScreenToClient(*this, reinterpret_cast<LPPOINT>(&rc) + 1);
		rc.left = rc.right;
		rc.right = rect.right - pxUnit * 2;
		DrawText(hdc, GetString(String_Cancelling).GetPointer() + 3, &rc, DT_SINGLELINE | DT_RIGHT | DT_VCENTER);
	}
}

void CreateImageWizard::OnClose()
{
	if (CanClose)
		DestroyWindow();
	else
	{
		Cancel = true;
		Invalidate(false);
	}
}

void CreateImageWizard::OnDestroy()
{
	PostQuitMessage(0);
}

LRESULT CreateImageWizard::OnNotify(LPNMHDR p)
{
	if (p->code == TTN_LINKCLICK)
		ShellExecuteA(nullptr, "open", "https://support.microsoft.com/help/5025885", nullptr, nullptr, SW_SHOWNORMAL);
	return 0;
}

void CreateImageWizard::DisableDialogControls()
{
	if (UpdateDlg)
	{
		UpdateDlg.Add.EnableWindow(false);
		UpdateDlg.Remove.EnableWindow(false);
		UpdateDlg.Browse.EnableWindow(false);
		UpdateDlg.BrowseDir.EnableWindow(false);
		UpdateDlg.Path.EnableWindow(false);
		UpdateDlg.CleanComponents.EnableWindow(false);
		DestroyMenu(UpdateDlg.hItemMenu);
		UpdateDlg.hItemMenu = nullptr;
	}
	else if (AppInstalling)
	{
		AppInstalling.Add.EnableWindow(false);
		AppInstalling.Remove.EnableWindow(false);
		AppInstalling.Browse.EnableWindow(false);
		AppInstalling.InstallEdge.EnableWindow(false);
		AppInstalling.Path.EnableWindow(false);
	}
	else if (DrvDlg)
	{
		DrvDlg.Add.EnableWindow(false);
		DrvDlg.Remove.EnableWindow(false);
		DrvDlg.AddDriverFromInstalledOS.EnableWindow(false);
		DrvDlg.BrowseDir.EnableWindow(false);
		DrvDlg.Path.EnableWindow(false);
		DrvDlg.Recurse.EnableWindow(false);
		DrvDlg.BrowseFiles.EnableWindow(false);
		DrvDlg.ForceUnsigned.EnableWindow(false);
	}
	else
		PostCommand(nullptr, 1234, 5678);
}

void CreateImageWizard::InstallUpdatesClicked()
{
	if (!Next.IsWindowVisible())
		DisableDialogControls();
	UpdateDlg.ModalDialogBox();
}

void CreateImageWizard::InstallAppxesClicked()
{
	if (!Next.IsWindowVisible())
		DisableDialogControls();
	AppInstalling.ModalDialogBox();
}

void CreateImageWizard::AddDriversClicked()
{
	if (!Next.IsWindowVisible())
		DisableDialogControls();
	DrvDlg.ModalDialogBox();
}

void CreateImageWizard::BrowseFile()
{
	com_ptr<IFileSaveDialog> pFileSaveDialog;
	HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFileSaveDialog));
	if (FAILED(hr))
	{
		CLSID FileSaveDialogLegacy = { 0xAF02484C, 0xA0A9, 0x4669, { 0x90, 0x51, 0x05, 0x8A, 0xB1, 0x2B, 0x91, 0x95 } };
		hr = CoCreateInstance(FileSaveDialogLegacy, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFileSaveDialog));
		if (FAILED(hr))
			return;
	}
	pFileSaveDialog->SetOptions(FOS_NOCHANGEDIR);
	COMDLG_FILTERSPEC rgSpec = { L"ISO Image", L"*.iso" };
	if (CreateWIM.GetCheck())
		if (SplitImage.GetCheck() == BST_CHECKED)
			rgSpec = { L"Split Windows Image", L"*.swm" };
		else if (Compression.GetCurSel() == WIMLIB_COMPRESSION_TYPE_LZMS)
			rgSpec = { L"Windows Image", L"*.esd" };
		else
			rgSpec = { L"Windows Image", L"*.wim" };
	pFileSaveDialog->SetFileTypes(1, &rgSpec);
	pFileSaveDialog->Show(hWnd);

	com_ptr<IShellItem> pShellItem;
	if (SUCCEEDED(pFileSaveDialog->GetResult(&pShellItem)))
	{
		PWSTR pszFilePath;
		pShellItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
		Path.SetWindowText(pszFilePath);
		CoTaskMemFree(pszFilePath);
	}
}
