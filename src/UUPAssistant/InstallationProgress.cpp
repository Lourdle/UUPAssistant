#include "pch.h"
#include "UUPAssistant.h"
#include "Resources/resource.h"

#include <thread>
#include <string>
#include <format>

using namespace std;
using namespace Lourdle::UIFramework;

import Constants;

InstallationProgress::InstallationProgress(InstallationWizard* p) : Window(GetFontSize() * 30, GetFontSize() * 40, GetString(String_InstallWindows), WS_MINIMIZEBOX | WS_CAPTION | WS_BORDER | WS_CLIPCHILDREN),
ctx(p->ctx), InstDotNet3(p->InstDotNet3), DontInstWinRe(p->DontInstWinRe), Target(move(p->Target)), Edition(p->Edition), BootPath(p->BootPath), State(-1), DeleteLetterAfterInstallation(p->DeleteLetterAfterInstallation), EFI(p->EFI), BootVHD(p->BootVHD),
StateDetail(this, 0, DWORD(WS_VISIBLE | WS_CHILD | ES_READONLY | ES_AUTOVSCROLL | ES_MULTILINE | WS_VSCROLL)), StateStatic(this), Ring(this),
AfterInstallation(this, 0, WS_VISIBLE | WS_CHILD | SS_LEFT), Shutdown(this, 0, ButtonStyle::AutoRadioButton), Reboot(this, 0, ButtonStyle::AutoRadioButton), Quit(this, 0, ButtonStyle::AutoRadioButton)
{
	bDoubleBuffer = true;
	Reboot.SetWindowText(GetString(String_Reboot));
	Shutdown.SetWindowText(GetString(String_Shutdown));
	Quit.SetWindowText(GetString(String_Quit));
	AfterInstallation.SetWindowText(GetString(String_AfterInstallation));
	Reboot.SetCheck(BST_CHECKED);

	SIZE size1;
	SIZE size2;
	SIZE size3;
	Reboot.GetIdealSize(&size1);
	Shutdown.GetIdealSize(&size2);
	Quit.GetIdealSize(&size3);

	const int pxUnit = GetFontSize();
	int x = pxUnit * 11;
	const int y = pxUnit * 38 + pxUnit / 2 - size1.cy / 2;
	const int nHeight = GetFontFullSize();

	Reboot.MoveWindow(x, y, size1.cx, size1.cy);
	Shutdown.MoveWindow(x += size1.cx + pxUnit, y, size2.cx, size2.cy);
	Quit.MoveWindow(x += size2.cx + pxUnit, y, size3.cx, size3.cy);

	AfterInstallation.MoveWindow(pxUnit * 2, y + size1.cy / 2 - nHeight / 2, pxUnit * 9, nHeight);

	Ring.MoveWindow(pxUnit * 2, pxUnit * 20, nHeight, nHeight);
	StateStatic.MoveWindow(pxUnit * 3 + nHeight, pxUnit * 20, pxUnit * 27 - nHeight, nHeight);
	StateDetail.AddExtendedWindowStyle(WS_EX_CLIENTEDGE | WS_EX_RIGHTSCROLLBAR);
	StateDetail.MoveWindow(pxUnit * 2, pxUnit * 25, pxUnit * 26, pxUnit * 12);
	StateDetail.SetLimitText(-1);

#pragma pack(push, 1)
	constexpr ULONGLONG kMountedDevicesGptPrefix = 0x3A44493A4F494D44ULL;

	struct MBRPart
	{
		DWORD Signature;
		ULONGLONG Reserved : 8;
		ULONGLONG OffsetRemainderTimes2 : 8;
		ULONGLONG OffsetDiv128 : 48;
	};
	struct GUIDPart
	{
		ULONGLONG MountManagerPrefix;
		GUID PartitionGUID;
	};
#pragma pack(pop)

	for (const auto& i : p->LetterInfoVector)
	{
		RegKey key =
		{
			.SYSTEM = true,
			.SubKey = L"MountedDevices",
			.ValueName = L"\\DosDevices\\X:",
			.Size = DWORD(i.ByGUID ? sizeof(GUIDPart) : sizeof(MBRPart)),
			.Type = REG_BINARY,
		};
		key.Data.reset(new BYTE[key.Size]);
		key.ValueName[12] = i.cLetter;
		if (i.ByGUID)
		{
			*reinterpret_cast<GUIDPart*>(key.Data.get()) = GUIDPart{
				.MountManagerPrefix = kMountedDevicesGptPrefix,
				.PartitionGUID = i.id
			};
		}
		else
		{
			*reinterpret_cast<MBRPart*>(key.Data.get()) = MBRPart{
				.Signature = i.MBR.Signature,
				.Reserved = 0,
				.OffsetRemainderTimes2 = i.MBR.ullOffset % 128 * 2,
				.OffsetDiv128 = i.MBR.ullOffset / 128
			};
		}
		Keys.push_back(std::move(key));
	}
	if (!p->VMSysAutoMgmt)
	{
		Keys.resize(Keys.size() + 1);
		auto& key = *Keys.rbegin();
		key.SubKey = L"ControlSet001\\Control\\Session Manager\\Memory Management";
		key.ValueName = L"PagingFiles";
		key.SYSTEM = true;
		key.Type = REG_MULTI_SZ;
		wstring Pages;
		for (const auto& i : p->VMSettings)
		{
			Pages += format(L"{}:\\pagefile.sys {} {}", static_cast<WCHAR>(i.Letter) - 'A' + 'a', DWORD(i.ISize), DWORD(i.MSize));
			Pages += L'\0';
		}
		key.Size = static_cast<DWORD>((Pages.size() + 1) * sizeof(WCHAR));
		key.Data.reset(new BYTE[key.Size]);
		memcpy(key.Data.get(), Pages.c_str(), key.Size);
	}

	Unattend = p->SettingOOBE.Settings.ToUnattendXml(p->ctx);

	if (p->ReImagePart.dwPart != 0)
	{
		InstallationWizard::PartVolInfo* Part = nullptr;
		for (auto& i : p->PartInfoVector)
			if (wcstoul(i.pDiskInfo->Name.GetPointer() + 17, nullptr, 10) == p->ReImagePart.dwDisk
				&& i.Number == p->ReImagePart.dwPart)
			{
				Part = &i;
				break;
			}

		if (Part)
		{
			RePartIsGPT = Part->GPT;
			RePartInfo.offset = Part->ullOffset;
			if (RePartIsGPT)
				RePartInfo.guid = Part->pDiskInfo->id;
			else
				RePartInfo.id = Part->pDiskInfo->Signature;
			ReImagePart.dwDisk = wcstoul(Part->pDiskInfo->Name.GetPointer() + 17, nullptr, 10);
			ReImagePart.dwPart = Part->Number;
		}
	}

	ShowWindow(SW_SHOWNORMAL);
	Ring.Start();
	ShutdownBlockReasonCreate(hWnd, GetString(String_Installing));

	void InstallThread(InstallationProgress*);
	thread(InstallThread, this).detach();
}

void InstallationProgress::OnClose()
{
}

void InstallationProgress::OnDraw(HDC hdc, RECT rect)
{
	rect.top = GetFontSize() * 2;
	rect.left = rect.top;
	HFONT hf = EzCreateFont(rect.top);
	SelectObject(hdc, hf);
	DrawText(hdc, String_Installing, &rect, DT_SINGLELINE);
	SelectObject(hdc, hFont);
	DeleteObject(hf);

	rect.top *= 4;
	rect.left *= 2;
	rect.bottom = rect.top + GetFontSize() * 3 / 2;
	COLORREF TextColor = SetTextColor(hdc, kSecondaryTextColor);
	DrawText(hdc, String_ApplyingImage, &rect, DT_SINGLELINE | DT_VCENTER);
	rect += GetFontSize() * 2;
	DrawText(hdc, String_InstallingFeatures, &rect, DT_SINGLELINE | DT_VCENTER);
	rect += GetFontSize() * 2;
	DrawText(hdc, String_InstallingUpdates, &rect, DT_SINGLELINE | DT_VCENTER);
	rect += GetFontSize() * 2;
	DrawText(hdc, String_InstallingSoftware, &rect, DT_SINGLELINE | DT_VCENTER);
	rect += GetFontSize() * 2;
	DrawText(hdc, String_ApplyingSettings, &rect, DT_SINGLELINE | DT_VCENTER);
	SetTextColor(hdc, TextColor);

	if (State >= ApplyingImage && State <= ApplyingSettings)
	{
		LOGFONTW lf;
		GetObjectW(hFont, sizeof(LOGFONTW), &lf);
		lf.lfWeight = FW_BOLD;
		hf = CreateFontIndirectW(&lf);
		SelectObject(hdc, hf);
		rect -= (ApplyingSettings - State) * GetFontSize() * 2;
		DrawText(hdc, String_ApplyingImage + State, &rect, DT_SINGLELINE | DT_VCENTER);
		SelectObject(hdc, hFont);
		DeleteObject(hf);
	}
}

void InstallationProgress::OnDestroy()
{
	PostQuitMessage(0);
}

static void CALLBACK TimerProc(HWND hWnd, UINT, int*, DWORD);

enum class CloseSessionOperation : int
{
	Shutdown = 0,
	Reboot = 1,
	Quit = 2,
};

static CloseSessionOperation DetermineCloseSessionOperation(InstallationProgress* parent)
{
	if (parent->Reboot.GetCheck() == BST_CHECKED)
		return CloseSessionOperation::Reboot;
	if (parent->Shutdown.GetCheck() == BST_CHECKED)
		return CloseSessionOperation::Shutdown;
	return CloseSessionOperation::Quit;
}

static UINT GetAboutToStringId(CloseSessionOperation op)
{
	return String_AboutToShutdown + static_cast<int>(op);
}

struct CloseSessionDlg : Dialog
{
	CloseSessionDlg(InstallationProgress* Parent) : Dialog(Parent, GetFontSize() * 22, GetFontSize() * 7, WS_CAPTION | WS_BORDER | DS_MODALFRAME | DS_FIXEDSYS, GetString(String_Installed)),
		Operation(DetermineCloseSessionOperation(Parent)),
		RebootButton(this, &CloseSessionDlg::Reboot, Operation == CloseSessionOperation::Reboot ? ButtonStyle::DefPushButton : ButtonStyle::Text),
		ShutdownButton(this, &CloseSessionDlg::Shutdown, Operation == CloseSessionOperation::Shutdown ? ButtonStyle::DefPushButton : ButtonStyle::Text),
		Exit(this, &CloseSessionDlg::OnCancel, Operation == CloseSessionOperation::Quit ? ButtonStyle::DefPushButton : ButtonStyle::Text)
	{
		Parent->Shutdown.EnableWindow(false);
		Parent->Reboot.EnableWindow(false);
		Parent->Quit.EnableWindow(false);

		RegisterCommand(&CloseSessionDlg::OnOK, IDOK, 1);
		RegisterCommand(&CloseSessionDlg::OnCancel, IDCANCEL, 1);
	}

	Button ShutdownButton;
	Button RebootButton;
	Button Exit;

	static constexpr int DefaultCountdownSeconds = 15;
	int CountdownSeconds = DefaultCountdownSeconds;
	CloseSessionOperation Operation;

	void Init(LPARAM)
	{
		int pxUnit = GetFontSize();
		RebootButton.SetWindowText(GetString(String_Reboot));
		ShutdownButton.SetWindowText(GetString(String_Shutdown));
		Exit.SetWindowText(GetString(String_Quit));
		RebootButton.MoveWindow(pxUnit, pxUnit * 4, pxUnit * 6, pxUnit * 2);
		ShutdownButton.MoveWindow(pxUnit * 8, pxUnit * 4, pxUnit * 6, pxUnit * 2);
		Exit.MoveWindow(pxUnit * 15, pxUnit * 4, pxUnit * 6, pxUnit * 2);
		CenterWindow();
		SetTimer(hWnd, reinterpret_cast<UINT_PTR>(&CountdownSeconds), 1000, reinterpret_cast<TIMERPROC>(TimerProc));
	}

	static void SetAction(HostAction action)
	{
		WriteProcessMemory(g_pHostContext->hParent, reinterpret_cast<LPVOID>(g_pHostContext->lParam), &action, sizeof(action), nullptr);
	}

	void Shutdown()
	{
		SetAction(HostAction::Shutdown);
		OnCancel();
	}

	void Reboot()
	{
		SetAction(HostAction::Reboot);
		OnCancel();
	}

	void OnCancel()
	{
		DestroyWindow();
		PostQuitMessage(0);
	}

	void OnOK()
	{
		switch (Operation)
		{
		case CloseSessionOperation::Reboot:
			Reboot();
			break;
		case CloseSessionOperation::Shutdown:
			Shutdown();
			break;
		case CloseSessionOperation::Quit:
			OnCancel();
			break;
		}
	}

	void OnDraw(HDC hdc, RECT rect)
	{
		rect.top = GetFontSize();
		rect.left = rect.top;
		rect.right -= rect.top;
		DrawText(hdc, ResStrFormat(GetAboutToStringId(Operation), CountdownSeconds), -1, &rect, DT_WORDBREAK);
	}
};

static void CALLBACK TimerProc(HWND hWnd, UINT, int* seconds, DWORD)
{
	if (*seconds != 1)
	{
		--*seconds;
		InvalidateRect(hWnd, nullptr, TRUE);
	}
	else
	{
		KillTimer(hWnd, reinterpret_cast<UINT_PTR>(seconds));
		Dialog::GetObjectPointer<CloseSessionDlg>(hWnd)->OnOK();
	}
}

LRESULT InstallationProgress::WindowProc(UINT Msg, WPARAM wParam, LPARAM lParam)
{
	if (Msg == UupAssistantMsg::Installer_DestroyWindow)
	{
		DestroyWindow();
		return 0;
	}
	else if (Msg == UupAssistantMsg::Installer_ShowPostInstallDialog)
	{
		SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
		ShutdownBlockReasonDestroy(hWnd);

		CloseSessionDlg cs(this);
		cs.CreateModelessDialog(0);
		cs.ShowWindow(SW_SHOWNORMAL);
		cs.SetActiveWindow();

		ACCEL Accel[] =
		{
			{ FVIRTKEY, VK_RETURN, IDOK },
			{ FVIRTKEY, VK_ESCAPE, IDCANCEL },
		};
		HACCEL hAccel = CreateAcceleratorTableW(Accel, ARRAYSIZE(Accel));

		MessageBeep(MB_ICONINFORMATION);
		FlashWindow(hWnd, TRUE);
		EnterMessageLoop(hAccel, cs);
		DestroyAcceleratorTable(hAccel);
		DestroyWindow();
		return 0;
	}

	return Window::WindowProc(Msg, wParam, lParam);
}
