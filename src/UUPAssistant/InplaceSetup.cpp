#include "pch.h"
#include "UUPAssistant.h"
#include "Resources/resource.h"

#include <Psapi.h>
#include <Shlwapi.h>
#include <wimgapi.h>

#include <string>
#include <thread>
#include <functional>
#include <format>

using namespace Lourdle::UIFramework;
using namespace std;

import Constants;

struct EnumWindowsContext
{
	HWND hWnd;
	WCHAR szSetupPath[MAX_PATH];
};

static BOOL CALLBACK EnumWindowsProc(HWND hWnd, EnumWindowsContext* ctx)
{
	DWORD pid;
	WCHAR szPath[MAX_PATH];
	GetWindowThreadProcessId(hWnd, &pid);
	HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
	if (hProcess)
	{
		GetModuleFileNameExW(hProcess, nullptr, szPath, MAX_PATH);
		CloseHandle(hProcess);
	}
	if (_wcsnicmp(szPath, ctx->szSetupPath, MAX_PATH) == 0)
	{
		ctx->hWnd = hWnd;
		return FALSE;
	}
	return TRUE;
}

HWND UpgradeProgress::ResetOwner()
{
	EnumWindowsContext ewctx = {
		.hWnd = nullptr
	};
	GetSystemDirectoryW(ewctx.szSetupPath, MAX_PATH);
	wcscpy_s(ewctx.szSetupPath + 3, MAX_PATH - 3, L"$WINDOWS.~BT\\Sources\\SetupHost.exe");
	EnumWindows(reinterpret_cast<WNDENUMPROC>(EnumWindowsProc), reinterpret_cast<LPARAM>(&ewctx));
	if (ewctx.hWnd)
	{
		SetWindowLongPtrW(hWnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(ewctx.hWnd));
		return ewctx.hWnd;
	}
	else
	{
		AddExtendedWindowStyle(WS_EX_TOPMOST);
		return nullptr;
	}
}

UpgradeProgress::~UpgradeProgress()
{
	CloseHandle(hEvent);
}

UpgradeProgress::UpgradeProgress(SessionContext& ctx, HANDLE hEvent) : Window(GetFontSize() * 30, GetFontSize() * 40, GetString(String_InstallWindows), WS_MINIMIZEBOX | WS_CAPTION | WS_BORDER | WS_CLIPCHILDREN | WS_OVERLAPPED, nullptr),
StateDetail(this, 0, DWORD(WS_VISIBLE | WS_CHILD | ES_READONLY | ES_AUTOVSCROLL | ES_MULTILINE | WS_VSCROLL)), State(0), Cancel(false), ctx(ctx), hEvent(hEvent), InstallationType(Unknown), InstallNetFx3(false), StateStatic(this), Ring(this)
{
	int pxUnit = GetFontSize();
	int nHeight = GetFontFullSize();
	Ring.MoveWindow(pxUnit * 2, pxUnit * 20, nHeight, nHeight);
	Ring.Start();
	StateStatic.MoveWindow(pxUnit * 3 + nHeight, pxUnit * 20, pxUnit * 27 - nHeight, nHeight);
	StateDetail.AddExtendedWindowStyle(WS_EX_CLIENTEDGE | WS_EX_RIGHTSCROLLBAR);
	StateDetail.MoveWindow(pxUnit * 2, pxUnit * 25, pxUnit * 26, pxUnit * 12);
	StateDetail.SetLimitText(-1);

	RECT rect;
	GetWindowRect(&rect);
	int cx = rect.right - rect.left, cy = rect.bottom - rect.top;
	MoveWindow(GetSystemMetrics(SM_CXSCREEN) / 50 * 49 - cx, GetSystemMetrics(SM_CYSCREEN) / 50 * 49 - cy, cx, cy);
}

static
DWORD
CALLBACK
MessageCallback(
	DWORD  dwMessageId,
	WPARAM wParam,
	LPARAM lParam,
	UpgradeProgress* p
)
{
	if (p->Cancel)
		return WIM_MSG_ABORT_IMAGE;

	if (dwMessageId == WIM_MSG_PROGRESS)
		p->StateStatic.SetWindowText(ResStrFormat(String_ExportingWinRe, static_cast<UINT>(wParam)));
	
	return WIM_MSG_SUCCESS;
}

static void AppendMessage(UpgradeProgress* p, PCWSTR pszMessage)
{
	p->StateStatic.SetWindowText(nullptr);
	int len = p->StateDetail.GetWindowTextLength();
	p->StateDetail.SetSel(len, len);
	p->StateDetail.ReplaceSel(pszMessage);
}

inline
static void AppendMessage(UpgradeProgress* p, UINT uStringID)
{
	AppendMessage(p, GetString(uStringID));
}

static void CancelInstallation(UpgradeProgress* p)
{
	HWND hWnd = GetWindow(p->GetHandle(), GW_OWNER);
	SendMessageW(hWnd, WM_COMMAND, UupAssistantMsg::CancelInstallation::wParam, UupAssistantMsg::CancelInstallation::lParam);
	p->Cancel = true;
}

static void ExportReImg(PWSTR pDestination, UpgradeProgress* p)
{
	AppendMessage(p, String_ImageIndex);
	AppendMessage(p, L"2. ");
	AppendMessage(p, String_ExportingImage);
	AppendMessage(p, pDestination);
	AppendMessage(p, L"...");
	HANDLE hWim = WIMCreateFile((p->ctx.PathUUP + p->ctx.TargetImageInfo.SystemESD).c_str(), WIM_GENERIC_READ, WIM_OPEN_EXISTING, WIM_FLAG_ALLOW_LZMS, 0, nullptr);
	if (!hWim)
	{
		p->ErrorMessageBox();
		CancelInstallation(p);
		SetEvent(p->hEvent);
		return;
	}
	HANDLE hWim2 = WIMCreateFile(pDestination, WIM_GENERIC_WRITE | WIM_GENERIC_READ, WIM_CREATE_NEW, 0, WIM_COMPRESS_LZX, nullptr);
	if (!hWim2)
	{
		p->ErrorMessageBox();
		WIMCloseHandle(hWim);
		DeleteFileW(pDestination);
		CancelInstallation(p);
		SetEvent(p->hEvent);
		return;
	}

	HANDLE hImage = nullptr;
	if (!WIMSetTemporaryPath(hWim, p->ctx.PathTemp.c_str())
		|| !WIMSetTemporaryPath(hWim2, p->ctx.PathTemp.c_str())
		|| WIMRegisterMessageCallback(hWim2, reinterpret_cast<FARPROC>(MessageCallback), p) == INVALID_CALLBACK_VALUE
		|| !(hImage = WIMLoadImage(hWim, 2))
		|| !WIMExportImage(hImage, hWim2, 0))
	{
		if (p->Cancel)
		{
		cancel:
			WIMCloseHandle(hWim);
			WIMCloseHandle(hWim2);
			DeleteFileW(pDestination);
			SetEvent(p->hEvent);
			return;
		}
		p->ErrorMessageBox();
		WIMCloseHandle(hWim);
		WIMCloseHandle(hWim2);
		DeleteFileW(pDestination);
		CancelInstallation(p);
		SetEvent(p->hEvent);
		return;
	}

	if (p->Cancel)
		goto cancel;

	WIMCloseHandle(hImage);
	WIMCloseHandle(hWim);
	WIMCloseHandle(hWim2);
	p->StateStatic.SetWindowText(nullptr);
	AppendMessage(p, String_Succeeded);
	SetEvent(p->hEvent);
}

static void ApplyImage(PWSTR pDestination, UpgradeProgress* p)
{
	p->State = p->ApplyingImage;
	p->Invalidate();

	WIMStruct* wim;
	int code = wimlib_open_wim(L"Install.esd", 0, &wim);
	if (code)
	{
		p->MessageBox(wimlib_get_error_string(static_cast<wimlib_error_code>(code)), nullptr, MB_ICONERROR);
		CancelInstallation(p);
		SetEvent(p->hEvent);
		return;
	}
	else
	{
		HANDLE hFile = CreateFileW(L".edition", GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (DWORD dwFileSize; hFile != INVALID_HANDLE_VALUE
			&& (dwFileSize = GetFileSize(hFile, nullptr)) != INVALID_FILE_SIZE)
		{
			MyUniqueBuffer<PWSTR> pszEdition(dwFileSize);
			DWORD dwRead;
			ReadFile(hFile, pszEdition, GetFileSize(hFile, nullptr), &dwRead, nullptr);
			CloseHandle(hFile);

			wstring src = L"\\Windows\\System32\\Licenses\\neutral\\_Default\\";
			wstring dst = src;
			src.append(pszEdition.get(), dwFileSize / sizeof(WCHAR));
			dst += p->ctx.TargetImageInfo.Edition;

			wimlib_rename_path(wim, 1, src.c_str(), dst.c_str());
		}
	}

	code = ApplyWIMImage(wim, pDestination, 1, &p->Cancel,
		[p](PCWSTR psz)
		{
			AppendMessage(p, psz);
		},
		[p](PCWSTR psz)
		{
			p->StateStatic.SetWindowText(psz);
		});
	wimlib_free(wim);

	if (p->Cancel)
	{
		AppendMessage(p, String_Cancelled);
		DeleteFileW(L".system_installation_succeeded");
		p->Cancel = false;
		SetEvent(p->hEvent);
		return;
	}
	if (code != 0)
	{
		AppendMessage(p, String_Failed);
		DeleteFileW(L".system_installation_succeeded");
		p->MessageBox(wimlib_get_error_string(static_cast<wimlib_error_code>(code)), nullptr, MB_ICONERROR);
		CancelInstallation(p);
		SetEvent(p->hEvent);
		return;
	}
	AppendMessage(p, String_Succeeded);
	SetEvent(p->hEvent);
}

static void PerformDismOperationsAndInstallEdge(PWSTR pDestination, UpgradeProgress* p)
{
	p->State = p->InstallingUpdates;
	p->Invalidate();
	auto AppendError = [p]()
		{
			LPWSTR lpErrMsg;
			FormatMessageW(
				FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
				nullptr,
				GetLastError(),
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				reinterpret_cast<LPWSTR>(&lpErrMsg),
				0, nullptr);
			AppendMessage(p, lpErrMsg);
			LocalFree(lpErrMsg);
		};

	HANDLE hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	thread([p](HANDLE hEvent)
		{
			DuplicateHandle(GetCurrentProcess(), hEvent, GetCurrentProcess(), &hEvent, 0, FALSE, DUPLICATE_SAME_ACCESS);
			while (WaitForSingleObject(hEvent, 50) == WAIT_TIMEOUT)
				if (p->Cancel)
				{
					KillChildren();
					break;
				}
			CloseHandle(hEvent);
		}, hEvent).detach();

	do
	{
		DismWrapper dism(pDestination, p->ctx.PathTemp.c_str(), &p->Cancel,
			[p](PCWSTR psz)
			{
				AppendMessage(p, psz);
			});
		if (!dism.Session)
		{
			if (!p->Cancel)
				p->State = p->WorkingOnMigration;
			else
				AppendMessage(p, String_Cancelled);
			break;
		}
		dism.SetString = [p](PCWSTR psz)
			{
				p->StateStatic.SetWindowText(psz);
			};
		dism.MessageBox = [p](LPCTSTR lpText, LPCWSTR lpCaption, UINT uType)
			{
				return p->MessageBox(lpText, lpCaption, uType);
			};

		switch (dism.AddUpdates(p->ctx))
		{
		case BYTE(-1):
			CancelInstallation(p);
		case FALSE:
			break;
		case TRUE:
			HANDLE hFile = CreateFileW(L".edition", GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (DWORD dwFileSize; hFile != INVALID_HANDLE_VALUE
				&& (dwFileSize = GetFileSize(hFile, nullptr)) != INVALID_FILE_SIZE)
			{
				MyUniqueBuffer<PWSTR> pszEdition(dwFileSize + sizeof(WCHAR));
				DWORD dwRead;
				ReadFile(hFile, pszEdition, GetFileSize(hFile, nullptr), &dwRead, nullptr);
				CloseHandle(hFile);
				pszEdition[dwFileSize / sizeof(WCHAR)] = 0;

				if (dism.SetEdition(pszEdition))
				{
					AppendMessage(p, String_Succeeded);
					DeleteFileW((wstring(pDestination) + L"Windows\\" + p->ctx.TargetImageInfo.Edition + L".xml").c_str());
				}
				else break;

				if (p->InstallNetFx3)
					dism.EnableDotNetFx3(L"sxs");
			}
			goto InstallSoftware;
		}
		break;

	InstallSoftware:
		p->StateStatic.SetWindowText(nullptr);
		p->State = p->InstallingSoftware;
		p->Invalidate();
		if (!dism.AddDrivers(p->ctx))
			break;

		if (!dism.AddApps(p->ctx))
			break;
	} while (false);

	if (p->Cancel)
		UnloadMountedImageRegistries(pDestination);
	else if (p->ctx.bInstallEdge)
	{
		wstring Path = p->ctx.PathUUP;
		Path += L"Edge.wim";
		if (GetFileAttributesW(Path.c_str()) != INVALID_FILE_ATTRIBUTES)
		{
			bool bNeedInst = true;
			p->State = p->InstallingSoftware;
			if (p->InstallationType == p->Upgrade)
			{
				AppendMessage(p, String_CheckingEdgeVersions);
				wstring CurrentEdgeVersion, CurrentWebView2Version, ImageEdgeVersion, ImageWebView2Version;
				bNeedInst = CheckWhetherNeedToInstallMicrosoftEdge(Path.c_str(), p->ctx.PathTemp.c_str(), CurrentEdgeVersion, CurrentWebView2Version, ImageEdgeVersion, ImageWebView2Version);
				if (ImageEdgeVersion.empty() || ImageWebView2Version.empty() || ImageEdgeVersion.empty() || ImageWebView2Version.empty())
				{
					bNeedInst = true;
					AppendError();
				}
				else
				{
					AppendMessage(p, String_Succeeded);
					AppendMessage(p,
						ResStrFormat(String_EdgeVersionStrings, CurrentEdgeVersion.c_str(), CurrentWebView2Version.c_str(), ImageEdgeVersion.c_str(), ImageWebView2Version.c_str()));
				}
			}
			if (bNeedInst)
			{
				AppendMessage(p, GetString(String_InstallingEdge));
				if (!InstallMicrosoftEdge(Path.c_str(), pDestination, p->ctx.PathTemp.c_str(), p->ctx.TargetImageInfo.Arch))
					AppendError();
				else
					AppendMessage(p, String_Succeeded);
			}
		}
	}

	p->State = p->WorkingOnMigration;
	p->Invalidate();
	p->StateStatic.SetWindowText(nullptr);

	SetEvent(p->hEvent);
	SetEvent(hEvent);
	CloseHandle(hEvent);
	SetProcessEfficiencyMode(true);
}

static void InstallSafeOSDU(PWSTR pszMountDir, UpgradeProgress* p)
{
	SetProcessEfficiencyMode(false);
	if (!p->ctx.SafeOSUpdate.Empty() || p->ctx.bAddSafeOSUpdate)
	{
		AppendMessage(p, L"\r\n");
		DismWrapper dism(pszMountDir, p->ctx.PathTemp.c_str(), nullptr,
			[p](PCWSTR psz)
			{
				AppendMessage(p, psz);
			});

		if (dism.Session)
		{
			AppendMessage(p, String_AddPackage);
			AppendMessage(p, p->ctx.SafeOSUpdate);
			AppendMessage(p, L"...");
			if (dism.AddSinglePackage(p->ctx.SafeOSUpdate))
				AppendMessage(p, String_Succeeded);
		}
	}

	DeleteDirectory(p->ctx.PathTemp.c_str());
	SetEvent(p->hEvent);
	SetProcessEfficiencyMode(false);
}

struct NetFx3OOBEDlg;

static VOID CALLBACK TimerProc(HWND, UINT, NetFx3OOBEDlg*, DWORD);

struct NetFx3OOBEDlg : DialogEx2<UpgradeProgress, bool>
{
	NetFx3OOBEDlg(UpgradeProgress* Parent) : DialogEx2(Parent, GetFontSize() * 24, GetFontSize() * 13, WS_CAPTION | DS_FIXEDSYS | DS_MODALFRAME, nullptr),
		InstallDotNetFx3(this, Random(), ButtonStyle::AutoCheckbox), SetOOBE(this, ButtonStyle::CommandLink, Random(), &Dialog::ModalDialogBox, &OOBESettings, 0),
		OOBESettings(this), Next(this, IDOK) {}

	Button InstallDotNetFx3;
	Button SetOOBE;
	Button Next;

	SettingOOBEDlg OOBESettings;

	int RemainSeconds = 16;

	bool OnClose()
	{
		OnOK();
		return true;
	}

	void OnOK()
	{
		GetParent()->InstallNetFx3 = InstallDotNetFx3.GetCheck() == BST_CHECKED;
		EndDialog(reinterpret_cast<INT_PTR>(new string(OOBESettings.Settings.ToUnattendXml(GetParent()->ctx))));
	}

	void OnCancel()
	{
		OnOK();
	}

	void Init(bool CleanInstall)
	{
		CenterWindow();
		SetWindowText(GetString(CleanInstall ? String_CleanInstallation : String_DataOnly));
		InstallDotNetFx3.SetWindowText(GetString(String_InstallDotNetFx3));
		SetOOBE.SetWindowText(GetString(String_OOBESettings));
		if (!CleanInstall)
			SetOOBE.EnableWindow(false);

		SetTimer(hWnd, reinterpret_cast<UINT_PTR>(this), 1000, reinterpret_cast<TIMERPROC>(TimerProc));
		TimerProc(nullptr, 0, this, 0);

		SIZE size;
		InstallDotNetFx3.GetIdealSize(&size);
		InstallDotNetFx3.MoveWindow(GetFontSize() * 2, GetFontSize() * 2, size.cx, size.cy);
		SetOOBE.GetIdealSize(&size);
		SetOOBE.MoveWindow(GetFontSize() * 2, GetFontSize() * 5, GetFontSize() * 20, size.cy);
		Next.MoveWindow(GetFontSize() * 14, GetFontSize() * 10, GetFontSize() * 9, GetFontSize() * 2);
	}

	INT_PTR DialogProc(UINT Msg, WPARAM wParam, LPARAM lParam)
	{
		switch (Msg)
		{
		case WM_MOUSEACTIVATE: case WM_MOVING: case WM_LBUTTONDOWN: case WM_COMMAND:
			if (RemainSeconds != -1)
			{
				KillTimer(hWnd, reinterpret_cast<UINT_PTR>(this));
				RemainSeconds = -1;
				Next.SetWindowText(GetString(String_Next));
			}
		}
		return DialogEx2::DialogProc(Msg, wParam, lParam);
	}
};

static VOID CALLBACK TimerProc(HWND hWnd, UINT, NetFx3OOBEDlg* p, DWORD)
{
	if (--p->RemainSeconds != 0)
	{
		auto NextText = format(L"{} ({}s)", GetString(String_Next).GetPointer(), p->RemainSeconds);
		p->Next.SetWindowText(NextText.c_str());
	}
	else
	{
		KillTimer(hWnd, reinterpret_cast<UINT_PTR>(p));
		p->OnOK();
	}
}

LRESULT UpgradeProgress::WindowProc(UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch (Msg)
	{
	case UupAssistantMsg::UpgradeProgress_StartExportReImg:
		if (InstallationType == DataOnly || InstallationType == CleanInstall)
		{
			auto Unattend = reinterpret_cast<string*>(NetFx3OOBEDlg(this).ModalDialogBox(InstallationType == CleanInstall));
			if (!Unattend->empty())
			{
				AppendMessage(this, String_ApplyUnattend);
				AppendMessage(this, L"Panther\\Unattend.xml");

				HANDLE hFile = CreateFileW(L"Panther\\Unattend.xml", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
				WriteFile(hFile, Unattend->c_str(), static_cast<DWORD>(Unattend->size()), nullptr, nullptr);
				CloseHandle(hFile);
				AppendMessage(this, L"\r\n");
			}
			delete Unattend;
		}

		thread(ExportReImg, reinterpret_cast<PWSTR>(wParam), this).detach();
		break;
	case UupAssistantMsg::UpgradeProgress_CreateFakeInstallEsd:
		if (!WriteFileResourceToFile(L"Install.esd", File_EmptyESD))
		{
			ErrorMessageBox();
			SetEvent(hEvent);
			return 0;
		}
	break;
	case UupAssistantMsg::UpgradeProgress_ConfigureSafeOSSucceeded:
		AppendMessage(this, String_Succeeded);
		break;
	case UupAssistantMsg::UpgradeProgress_ConfiguringSafeOS:
		AppendMessage(this, String_ConfiguringSafeOS);
		break;
	case UupAssistantMsg::UpgradeProgress_CancelRequested:
		Cancel = true;
		AppendMessage(this, String_Cancelling);
		break;
	case UupAssistantMsg::UpgradeProgress_StartApplyImage:
		thread(ApplyImage, reinterpret_cast<PWSTR>(wParam), this).detach();
		break;
	case UupAssistantMsg::UpgradeProgress_StartSafeOSDU:
		thread(InstallSafeOSDU, reinterpret_cast<PWSTR>(wParam), this).detach();
		break;
	case UupAssistantMsg::UpgradeProgress_StartDismAndEdge:
		thread(PerformDismOperationsAndInstallEdge, reinterpret_cast<PWSTR>(wParam), this).detach();
		break;
	case UupAssistantMsg::UpgradeProgress_ProcessExited:
		SetProcessEfficiencyMode(false);
		if (!Cancel && wParam)
		{
			HMODULE hModule = LoadLibraryExA("SetupCore.dll", nullptr, LOAD_LIBRARY_AS_IMAGE_RESOURCE);
			LPWSTR lpBuffer = nullptr;
			FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM, hModule, static_cast<DWORD>(wParam), 0, reinterpret_cast<LPWSTR>(&lpBuffer), 0, nullptr);
			FreeLibrary(hModule);
			WCHAR szHexCode[11];
			swprintf_s(szHexCode, L"0x%08X", static_cast<DWORD>(wParam));
			if (*lpBuffer)
				MessageBox(lpBuffer, szHexCode, MB_ICONERROR);
			else
				MessageBox(szHexCode, nullptr, MB_ICONERROR);
			LocalFree(lpBuffer);
		}
		PostQuitMessage(0);
		break;
	default:
		return Window::WindowProc(Msg, wParam, lParam);
	}
	return 0;
}

void UpgradeProgress::OnClose() {}

void UpgradeProgress::OnDraw(HDC hdc, RECT rect)
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
	DrawText(hdc, String_PreparingFiles, &rect, DT_SINGLELINE | DT_VCENTER);
	rect += GetFontSize() * 2;
	DrawText(hdc, String_ApplyingImage, &rect, DT_SINGLELINE | DT_VCENTER);
	rect += GetFontSize() * 2;
	DrawText(hdc, String_InstallingUpdates, &rect, DT_SINGLELINE | DT_VCENTER);
	rect += GetFontSize() * 2;
	DrawText(hdc, String_InstallingSoftware, &rect, DT_SINGLELINE | DT_VCENTER);
	rect += GetFontSize() * 2;
	DrawText(hdc, String_WorkingOnMigration, &rect, DT_SINGLELINE | DT_VCENTER);
	SetTextColor(hdc, TextColor);

	if (State >= PreparingFiles && State <= WorkingOnMigration)
	{
		LOGFONTW lf;
		GetObjectW(hFont, sizeof(LOGFONTW), &lf);
		lf.lfWeight = FW_BOLD;
		hf = CreateFontIndirectW(&lf);
		SelectObject(hdc, hf);

		rect -= (WorkingOnMigration - State) * GetFontSize() * 2;
		UINT uResID;
		switch (State)
		{
		case PreparingFiles:
			uResID = String_PreparingFiles;
			break;
		case ApplyingImage:
			uResID = String_ApplyingImage;
			break;
		case InstallingUpdates:
			uResID = String_InstallingUpdates;
			break;
		case InstallingSoftware:
			uResID = String_InstallingSoftware;
			break;
		case WorkingOnMigration:
			uResID = String_WorkingOnMigration;
			break;
		}
		DrawText(hdc, uResID, &rect, DT_SINGLELINE | DT_VCENTER);
		SelectObject(hdc, hFont);
		DeleteObject(hf);
	}
}
