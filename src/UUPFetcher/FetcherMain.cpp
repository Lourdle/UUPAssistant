#include "pch.h"
#include "framework.h"
#include "FetcherMain.h"
#include "resource.h"
#include "ExitCode.h"
#include "global_features.h"
#include "../common/common.h"

#include <string>
#include <vector>
#include <algorithm>
#include <format>
#include <atomic>
#include <chrono>
#include <future>
#include <filesystem>

#include <ShlObj_core.h>
#include <Shlwapi.h>
#include <shellapi.h>

#undef max

using namespace Lourdle::UIFramework;

import Constants;
import WebView;
import Misc;
import DownloadHost;
import CleanDialog;
import Spiders;

constexpr int nPixelsPerPos = 6;

constexpr UINT kMsgBack = WM_USER + 0x114;
constexpr UINT kMsgClose = WM_USER + 0x514;

FetcherMain::FetcherMain() :
	Window(GetFontSize() * 60, GetFontSize() * 40, GetString(String_UUPFetcher),
		WS_CAPTION | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SIZEBOX | WS_CLIPCHILDREN),
	FetchButton(this, &FetcherMain::Fetch, ButtonStyle::CommandLink), ViewTasksButton(this, &FetcherMain::ViewTasks, ButtonStyle::CommandLink), CleanButton(this, &FetcherMain::Clean, ButtonStyle::CommandLink), CreateTaskByUrlButton(this, &FetcherMain::CreateByUrl, ButtonStyle::CommandLink),
	EditBox(this), BackButton(this, &FetcherMain::Back, ButtonStyle::Icon), Progress(this), State(Idle), webview(),
	NextButton(this, &FetcherMain::Next), Browse(this, &FetcherMain::BrowseFile), FileList(this), ViewApps(this, &FetcherMain::ViewAppFiles), DontDownloadApps(this, &FetcherMain::Redraw, ButtonStyle::AutoCheckbox),
	DeleteButton(this, &FetcherMain::Delete), ViewAdditionalEditions(this, &FetcherMain::ViewVirtualEditions), Status(this, 0)
{
	FetchButton.SetWindowText(GetString(String_OpenUUPdump));
	ViewTasksButton.SetWindowText(GetString(String_ViewTasks));
	CleanButton.SetWindowText(GetString(String_Clean));
	CreateTaskByUrlButton.SetWindowText(GetString(String_CreateTaskByUrl));
	NextButton.SetWindowText(GetString(String_Next));
	Browse.SetWindowText(GetString(String_Browse));
	DontDownloadApps.SetWindowText(GetString(String_DontDownloadApps));
	ViewApps.SetWindowText(GetString(String_ViewApps));
	DeleteButton.SetWindowText(GetString(String_Delete));
	ViewAdditionalEditions.SetWindowText(GetString(String_ViewAdditionalEditions));

	HMODULE hModule = LoadLibraryA("shell32.dll");
	hBack = LoadIcon(hModule, MAKEINTRESOURCE(255));
	hCancel = LoadIcon(hModule, MAKEINTRESOURCE(240));
	FreeLibrary(hModule);
	BackButton.SetImageIcon(hBack);
	BackButton.ShowWindow(SW_HIDE);
	Progress.ShowWindow(SW_HIDE);
	Progress.SetRange(0, ProgressStepMax);
	Progress.SetStep(ProgressStepsPerPercent * 24);
	RegisterCommand(&FetcherMain::MyCommand, Progress, 0, kDisplaySummaryPageNotifyId);
	RegisterCommand({ nullptr, kCmdNextId, kCmdNextCode }, [](HWND hWnd, LPARAM hButton)
		{
			RECT rect;
			::GetClientRect(hWnd, &rect);
			SetWindowTextW(reinterpret_cast<HWND>(hButton), GetString(String_Retry));
			::PostMessageW(hWnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(rect.right, rect.bottom));
		}, reinterpret_cast<LPARAM>(NextButton.GetHandle()));
}

void FetcherMain::OnSize(BYTE type, int nClientWidth, int nClientHeight, WindowBatchPositioner wbp)
{
	if (type == SIZE_MINIMIZED)
	{
		SetProcessEfficiencyMode(true);
		return;
	}
	else if (type == SIZE_RESTORED)
		SetProcessEfficiencyMode(false);

	const int pxUnit = GetFontSize();
	switch (State)
	{
	case SummaryPage:
	{
		int nMax;
		GetScrollRange(SB_VERT, nullptr, &nMax);
		nMax *= nPixelsPerPos;
		if (nMax < nClientHeight + nPixelsPerPos)
		{
			nMax = nClientHeight;
			SetScrollRange(SB_VERT, 0, nMax / nPixelsPerPos);
			ShowScrollBar(SB_VERT, false);
		}
		else
		{
			nMax = pxUnit * 80;
			SetScrollRange(SB_VERT, 0, nMax / nPixelsPerPos);
			ShowScrollBar(SB_VERT);
		}
		RECT rect;
		GetClientRect(&rect);
		if (rect.right != nClientWidth)
			return;
		rect = { pxUnit * 5 / 2, pxUnit * 11, nClientWidth / 2 - pxUnit * 5 / 2, pxUnit * 22 };
		rect.bottom += rect.top + wbp.nYOffset;
		rect.top += wbp.nYOffset;

		if (webview)
			webview->webviewController->put_Bounds(rect);

		wbp.MoveWindow(NextButton, nClientWidth - pxUnit * 6, nMax - pxUnit * 3, pxUnit * 5, pxUnit * 2);
		wbp.MoveWindow(Browse, nClientWidth - pxUnit * 6, nMax - pxUnit * 6, pxUnit * 5, pxUnit * 2);
		wbp.MoveWindow(EditBox, pxUnit, nMax - pxUnit * 6, nClientWidth - pxUnit * 8, pxUnit * 2);
		rect.bottom += pxUnit - wbp.nYOffset;
		rect.top -= wbp.nYOffset;
		SIZE size;
		ViewAdditionalEditions.GetIdealSize(&size);
		wbp.MoveWindow(ViewAdditionalEditions, rect.left / 2 + rect.right / 2 - size.cx / 2 - pxUnit, rect.bottom, size.cx + pxUnit * 2, pxUnit * 2);
		rect.bottom += pxUnit * 2;
		DontDownloadApps.GetIdealSize(&size);
		wbp.MoveWindow(DontDownloadApps, pxUnit, nMax - pxUnit * 2 - size.cy / 2, size.cx, size.cy);
		ViewApps.GetIdealSize(&size);
		wbp.MoveWindow(ViewApps, nClientWidth - pxUnit * 5 / 2 - size.cx, rect.bottom - size.cy, size.cx, size.cy);
		wbp.MoveWindow(FileList, pxUnit * 5 / 2, rect.bottom + pxUnit * 5 / 2, nClientWidth - pxUnit * 5, nMax - pxUnit * 8 - rect.bottom - pxUnit * 5 / 2);
	}
	break;
	case WebView:case GettingInfo:
		if (webview)
			webviewResize(this);
		wbp.MoveWindow(EditBox, pxUnit * 7 / 2, pxUnit, nClientWidth - pxUnit * 7 / 2, pxUnit * 2);
		break;
	case Idle:
	{
		SIZE size;
		FetchButton.GetIdealSize(&size);
		wbp.MoveWindow(FetchButton, nClientWidth / 2 - pxUnit * 10, nClientHeight / 2 - pxUnit * 8, pxUnit * 20, size.cy);
		wbp.MoveWindow(ViewTasksButton, nClientWidth / 2 - pxUnit * 10, nClientHeight / 2 - pxUnit * 4, pxUnit * 20, size.cy);
		wbp.MoveWindow(CreateTaskByUrlButton, nClientWidth / 2 - pxUnit * 10, nClientHeight / 2, pxUnit * 20, size.cy);
		wbp.MoveWindow(CleanButton, nClientWidth / 2 - pxUnit * 10, nClientHeight / 2 + pxUnit * 4, pxUnit * 20, size.cy);
	}
	break;
	case TaskList:
		wbp.MoveWindow(FileList, pxUnit * 3, pxUnit * 4, nClientWidth - pxUnit * 6, nClientHeight - pxUnit * 9);
		wbp.MoveWindow(DeleteButton, pxUnit * 3, nClientHeight - pxUnit * 3, pxUnit * 5, pxUnit * 2);
		wbp.MoveWindow(NextButton, nClientWidth - pxUnit * 7, nClientHeight - pxUnit * 3, pxUnit * 5, pxUnit * 2);
		break;
	case TaskSummary:
	case Downloading:
	case Failed:
	case Done:
		if (State == TaskSummary
			|| State == Done && bWriteToStdOut
			|| State == Failed)
			wbp.MoveWindow(NextButton, nClientWidth - pxUnit * 6, nClientHeight - pxUnit * 3, pxUnit * 5, pxUnit * 2);

		wbp.MoveWindow(CleanButton, nClientWidth / 2 - pxUnit * 3, nClientHeight - pxUnit * 3, pxUnit * 6, pxUnit * 2);
		{
			SIZE size;
			ViewAdditionalEditions.GetIdealSize(&size);
			wbp.MoveWindow(ViewAdditionalEditions, pxUnit, nClientHeight - pxUnit * 3, size.cx + pxUnit * 3 / 2, pxUnit * 2);
		}
		if (State == Downloading)
			wbp.MoveWindow(Status, pxUnit * 5 / 2, pxUnit * 7, nClientWidth, pxUnit * 6);
		wbp.MoveWindow(FileList, pxUnit * 5 / 2, pxUnit * 13, nClientWidth - pxUnit * 5, nClientHeight - pxUnit * 35 / 2);
		break;
	}
	wbp.MoveWindow(BackButton, 0, pxUnit / 2, pxUnit * 3, pxUnit * 3);
	wbp.MoveWindow(Progress, 0, pxUnit * 7 / 2, nClientWidth, 4);
}


bool FetcherMain::GetAutoScrollInfo(bool bVert, int& nPixelsPerPos, PVOID pfnCaller)
{
	nPixelsPerPos = ::nPixelsPerPos;
	return State == SummaryPage && bVert;
}

void FetcherMain::OnGetMinMaxInfo(PMINMAXINFO pMinMaxInfo)
{
	pMinMaxInfo->ptMinTrackSize.x = GetFontSize() * 50;
	pMinMaxInfo->ptMinTrackSize.y = GetFontSize() * 28;
}

void FetcherMain::OnDestroy()
{
	DestroyIcon(hBack);
	DestroyIcon(hCancel);
	PostQuitMessage(0);
}

static void DrawText(HDC hdc, PCWSTR pString, RECT* prc)
{
	RECT rc = *prc;
	DrawText(hdc, pString, &rc, DT_CALCRECT | DT_WORDBREAK);
	DrawText(hdc, pString, prc, DT_WORDBREAK);
	prc->top += rc.bottom - rc.top;
}

void FetcherMain::OnDraw(HDC hdc, RECT rect)
{
	int pxUnit = GetFontSize();

	switch (State)
	{
	case GettingInfo:
		DrawText(hdc, String_GettingInfo, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		break;
	case SummaryPage:
		rect.top += pxUnit * 4;
		hFont = SelectObject(hdc, EzCreateFont(pxUnit * 5 / 4));
		DrawText(hdc, String_Summary, &rect, DT_SINGLELINE | DT_CENTER);
		rect.top += pxUnit * 5 / 2;
		rect.bottom = rect.top + pxUnit * 27;
		DrawText(hdc, uup.Name, -1, &rect, DT_SINGLELINE | DT_CENTER);
		if (webview)
		{
			rect.left = rect.right / 2 + pxUnit * 5 / 2;
			rect.right -= pxUnit * 5 / 2;
		}
		else
		{
			rect.left = rect.right / 4;
			rect.right -= rect.left;
		}
		rect.top += pxUnit * 4;
		DrawText(hdc, String_Language, &rect, DT_SINGLELINE);
		hFont = SelectObject(hdc, hFont);
		rect.top += pxUnit * 3 / 2;
		DrawText(hdc, (uup.LocaleName + ' ') += uup.Language, &rect);
		if (!uup.EditionFriendlyNames.Empty())
		{
			rect.top += pxUnit / 4;
			hFont = SelectObject(hdc, hFont);
			DrawText(hdc, String_Edition, &rect, DT_SINGLELINE);
			rect.top += pxUnit * 3 / 2;
			hFont = SelectObject(hdc, hFont);
			DrawText(hdc, uup.EditionFriendlyNames, &rect);
		}
		rect.top += pxUnit / 4;
		hFont = SelectObject(hdc, hFont);
		DrawText(hdc, String_SysFileSize, &rect, DT_SINGLELINE);
		rect.top += pxUnit * 3 / 2;
		hFont = SelectObject(hdc, hFont);
		DrawText(hdc, uup.SystemSize, &rect);
		rect.top += pxUnit / 4;
		hFont = SelectObject(hdc, hFont);
		if (!uup.Apps.empty())
		{
			DrawText(hdc, String_AppSize, &rect, DT_SINGLELINE);
			rect.top += pxUnit * 3 / 2;
			hFont = SelectObject(hdc, hFont);
			DrawText(hdc, uup.AppSize, &rect);
			rect.top += pxUnit / 4;
			hFont = SelectObject(hdc, hFont);
			DrawText(hdc, String_TotalSize, &rect, DT_SINGLELINE);
			rect.top += pxUnit * 3 / 2;
			hFont = SelectObject(hdc, hFont);
			DrawText(hdc, DontDownloadApps.GetCheck() == BST_CHECKED ? uup.SystemSize : uup.UUPSize, &rect);
			hFont = SelectObject(hdc, hFont);
		}
		DeleteObject(SelectObject(hdc, hFont));
		if (!webview)
		{
			rect.right = rect.left * 4;
			rect.left = rect.right / 2 + pxUnit * 5 / 2;
		}
		DrawText(hdc, uup.Apps.empty() ? String_NoApps : String_Apps, &rect, DT_WORDBREAK | DT_BOTTOM);
		break;
	case TaskSummary:
	case Downloading:
	case Failed:
	case Done:
		rect.top += pxUnit * 3;
		DrawText(hdc, (uup.LocaleName + ' ') += uup.Language, -1, &rect, DT_CENTER | DT_SINGLELINE);
		rect.top += pxUnit * 3 / 2;
		DrawText(hdc, uup.EditionFriendlyNames, -1, &rect, DT_SINGLELINE | DT_CENTER);
		rect.left += pxUnit * 3;
		rect.right -= pxUnit * 3;
		rect.top -= pxUnit * 9 / 2;
		rect.bottom = rect.top + pxUnit * 3;
		SelectObject(hdc, EzCreateFont(pxUnit * 5 / 4));
		DrawText(hdc, uup.Name, -1, &rect, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
		DeleteObject(SelectObject(hdc, hFont));
		break;
	case Idle:
		DrawText(hdc, String_Declaration, &rect, DT_CENTER | DT_BOTTOM);
		break;
	}
}

LRESULT FetcherMain::OnNotify(LPNMHDR p)
{
	if (State == TaskList
		&& p->hwndFrom == FileList
		&& p->code == LVN_ITEMCHANGED
		&& reinterpret_cast<LPNMLISTVIEW>(p)->iItem != CTL_ERR)
		if (reinterpret_cast<LPNMLISTVIEW>(p)->uNewState & LVIS_SELECTED)
		{
			DeleteButton.EnableWindow(TRUE);
			NextButton.EnableWindow(FileList.GetSelectedCount() == 1);
		}
		else if (reinterpret_cast<LPNMLISTVIEW>(p)->uOldState & LVIS_SELECTED)
		{
			int n = FileList.GetSelectedCount();
			DeleteButton.EnableWindow(n > 0);
			NextButton.EnableWindow(n == 1);
		}
	return 0;
}

void FetcherMain::MyCommand()
{
	if (State == SummaryPage)
	{
		if (webview)
		{
			webview->webview->NavigateToString(uup.HtmlVritualEditions.GetPointer());
			webview->webviewController->put_IsVisible(TRUE);
		}
		EditBox.SetWindowText(nullptr);
		EditBox.SetReadOnly(false);
		FileList.InsertColumn(GetString(String_File).GetPointer(), GetFontSize() * 47, 0);
		FileList.InsertColumn(GetString(String_Size).GetPointer(), GetFontSize() * 8, 1);
		FileList.InsertColumn(L"SHA-1", GetFontSize() * 25, 2);
		WCHAR szSize[10];
		for (auto& i : uup.System)
		{
			int index = FileList.InsertItem();
			FileList.SetItemText(index, 0, i.Name.GetPointer());
			StrFormatByteSizeW(i.Size, szSize, 10);
			FileList.SetItemText(index, 1, szSize);
			FileList.SetItemText(index, 2, i.SHA1.GetPointer());
		}
		DontDownloadApps.SetCheck(uup.Apps.empty());
		DontDownloadApps.EnableWindow(!uup.Apps.empty());
		ViewApps.EnableWindow(!uup.Apps.empty());
		ShowScrollBar(SB_VERT);
		RECT rect;
		GetClientRect(&rect);
		SCROLLINFO si = {
			.cbSize = sizeof(si),
			.fMask = SIF_POS | SIF_RANGE | SIF_PAGE,
			.nMax = std::max(rect.bottom, long(GetFontSize() * 80)) / nPixelsPerPos,
			.nPage = UINT(rect.bottom / nPixelsPerPos)
		};
		SetScrollInfo(SB_VERT, &si);
		return;
	}
	EnterMessageLoopTimeout(700);
	if (BackButton.GetImageIcon() == hBack)
	{
		Progress.ShowWindow(SW_HIDE);
		Progress.SetPos(0);
	}
}

static void ResetChildWindowsPos(Lourdle::UIFramework::Window* pWindow)
{
	RECT rect;
	pWindow->GetClientRect(&rect);
	pWindow->OnSize(0, rect.right, rect.bottom, WindowBatchPositioner());
}

void FetcherMain::ViewTasks()
{
	uup.Reset();
	State = TaskList;
	Invalidate();
	ViewTasksButton.ShowWindow(SW_HIDE);
	FetchButton.ShowWindow(SW_HIDE);
	CleanButton.ShowWindow(SW_HIDE);
	CreateTaskByUrlButton.ShowWindow(SW_HIDE);
	NextButton.EnableWindow(false);
	DeleteButton.EnableWindow(false);
	FileList.InsertColumn(GetString(String_Time).GetPointer(), GetFontSize() * 12, 0);
	FileList.InsertColumn(L"UUP", GetFontSize() * 25, 1);
	FileList.InsertColumn(GetString(String_PathToSaveUUP).GetPointer(), GetFontSize() * 22, 2);
	FileList.InsertColumn(GetString(String_Edition).GetPointer(), GetFontSize() * 12, 3);
	FileList.InsertColumn(GetString(String_Language).GetPointer(), GetFontSize() * 8, 4);
	FileList.InsertColumn(GetString(String_Configuration).GetPointer(), GetFontSize() * 14, 5);

	do if (SetCurrentDirectoryW(GetAppDataPath())
		&& SetCurrentDirectoryW(L"Config"))
	{
		std::error_code ec;
		auto iter = std::filesystem::directory_iterator(std::filesystem::current_path(), ec);
		if (ec) break;

		struct Config
		{
			std::filesystem::file_time_type ft;
			std::wstring Name;
		};
		std::vector<Config> cfg;

		for (auto& entry : iter)
			cfg.push_back({ entry.last_write_time(), entry.path().filename().wstring() });

		std::sort(cfg.begin(), cfg.end(), [](const Config& a, const Config& b) -> bool
			{
				return a.ft > b.ft;
			});

		auto ftNow = std::filesystem::file_time_type::clock::now();
		auto scNow = std::chrono::system_clock::now();
		for (size_t i = 0; i != cfg.size();)
		{
			using namespace std::chrono;
			auto& j = cfg[i];
			auto sys_time = time_point_cast<system_clock::duration>(j.ft - ftNow + scNow);
			auto sec_time = floor<seconds>(sys_time);
			zoned_time local{ current_zone(), sec_time };

			auto TimeString = std::format(L"{:%Y-%m-%d %H:%M:%S}", local);
			HKEY hKey;
			bool erase = false;
			if (RegLoadAppKeyW(j.Name.c_str(), &hKey, KEY_QUERY_VALUE, REG_PROCESS_APPKEY, 0) == ERROR_SUCCESS)
			{
				int index = FileList.InsertItem();
				WCHAR sz[MAX_PATH];
				DWORD dwSize = sizeof(sz);
				if (RegQueryValueExW(hKey, L"Name", nullptr, nullptr, reinterpret_cast<PBYTE>(sz), &dwSize) != ERROR_SUCCESS)
					goto failure;
				FileList.SetItemText(index, 1, sz);
				dwSize = sizeof(sz);
				if (RegQueryValueExW(hKey, L"Path", nullptr, nullptr, reinterpret_cast<PBYTE>(sz), &dwSize) != ERROR_SUCCESS)
					goto failure;
				FileList.SetItemText(index, 2, sz);
				dwSize = sizeof(sz);
				if (RegQueryValueExW(hKey, L"EditionFriendlyNames", nullptr, nullptr, reinterpret_cast<PBYTE>(sz), &dwSize) != ERROR_SUCCESS)
					goto failure;
				FileList.SetItemText(index, 3, sz);
				dwSize = sizeof(sz);
				if (RegQueryValueExW(hKey, L"LocaleName", nullptr, nullptr, reinterpret_cast<PBYTE>(sz), &dwSize) != ERROR_SUCCESS)
					goto failure;
				FileList.SetItemText(index, 4, sz);
				FileList.SetItemText(index, 0, TimeString.c_str());
				FileList.SetItemText(index, 5, j.Name.c_str());
				goto end;

			failure:
				FileList.DeleteItem(index);
				erase = true;
			end:
				RegCloseKey(hKey);
			}
			else
				erase = true;

			if (erase)
				cfg.erase(cfg.begin() + i);
			else
				++i;
		}

		iter = std::filesystem::directory_iterator(std::filesystem::current_path(), ec);
		if (ec) break;
		for (const auto& entry : iter)
		{
			const auto& filename = entry.path().filename();
			auto it = std::find_if(cfg.begin(), cfg.end(),
				[&filename](const Config& cfg) {return cfg.Name == filename; });
			if (it != cfg.end())
			{
				cfg.erase(it);
				continue;
			}

			if (entry.is_directory())
				RemoveDirectoryRecursive(entry.path().c_str());
			else
				DeleteFileW(entry.path().c_str());
		}
	}while (false);
	FileList.ShowWindow(SW_SHOW);
	DeleteButton.ShowWindow(SW_SHOW);
	NextButton.ShowWindow(SW_SHOW);
	BackButton.ShowWindow(SW_SHOW);
	ResetChildWindowsPos(this);
}

void FetcherMain::Clean()
{
	if (State == Idle)
		CleanDialog(this).ModalDialogBox();
	else if (MessageBox(GetString(String_UUPDirCleaning), L"?", MB_ICONQUESTION | MB_YESNO) == IDYES)
	{
		std::error_code ec;
		auto iter = std::filesystem::directory_iterator(uup.Path.GetPointer(), ec);
		if (ec)
		{
			ErrorMessageBox();
			return;
		}

		ULONG ulDeletedFiles = 0, ulDeletedDirs = 0;
		for (const auto& entry : iter)
		{
			if (entry.is_directory())
			{
				std::filesystem::remove_all(entry.path(), ec);
				if (ec)
				{
					ErrorMessageBox();
					return;
				}
				else
					++ulDeletedDirs;
			}
			else
			{
				auto filename = entry.path().filename().wstring();
				auto lambda = [&filename](const uup_struct::File& file)
					{
						return file.Name.CompareCaseInsensitive(filename.c_str());
					};

				auto it = std::find_if(uup.System.begin(), uup.System.end(), lambda);
				if (it != uup.System.end())
					continue;
				it = std::find_if(uup.Apps.begin(), uup.Apps.end(), lambda);
				if (it != uup.Apps.end())
					continue;

				if (!DeleteFileW(entry.path().c_str()))
				{
					ErrorMessageBox();
					return;
				}
				else
					++ulDeletedFiles;
			}
		}

		auto Format = GetString(String_DeletedFiles);
		auto Length = Format.GetLength() + 32;
		MyUniquePtr<WCHAR> Buffer = Length;
		swprintf_s(Buffer, Length, Format.GetPointer(), ulDeletedDirs, ulDeletedFiles);
		MessageBox(Buffer, Format.GetPointer() + Format.GetLength(), MB_ICONINFORMATION);
	}
}

void FetcherMain::CreateByUrl()
{
	struct CreateTaskDialog : Dialog
	{
		CreateTaskDialog(FetcherMain* p) : Dialog(p, GetFontSize() * 65, GetFontSize() * 16, DS_MODALFRAME | DS_FIXEDSYS | WS_POPUP | WS_CAPTION | WS_SYSMENU, GetString(String_CreateTaskByUrl)),
			EditBox(this), NextButton(this, &CreateTaskDialog::OnOK), OpenButton(this, &CreateTaskDialog::Open)
		{
		}

		Edit EditBox;
		Button OpenButton;
		Button NextButton;

		void Init(LPARAM)
		{
			OpenButton.SetWindowText(GetString(String_OpenUUPdump));
			NextButton.SetWindowText(GetString(String_Next));
			EditBox.MoveWindow(GetFontSize(), GetFontSize() * 10, GetFontSize() * 63, GetFontSize() * 2);
			OpenButton.MoveWindow(GetFontSize() * 50, GetFontSize() * 13, GetFontSize() * 9, GetFontSize() * 2);
			NextButton.MoveWindow(GetFontSize() * 60, GetFontSize() * 13, GetFontSize() * 4, GetFontSize() * 2);
		}

		void Open()
		{
			ShellExecuteW(nullptr, L"open", UUPDUMP_WEBSITE_BASE_URL, nullptr, nullptr, SW_SHOWNORMAL);
		}

		void OnOK()
		{
			if (auto Path = GetAppDataPath(); !CreateDirectoryRecursive(Path)
				|| !SetCurrentDirectoryW(Path))
			{
				ErrorMessageBox();
				return;
			}
			String str = EditBox.GetWindowText();
			if (wcsncmp(str.GetPointer(), (std::wstring(UUPDUMP_WEBSITE_BASE_URL) += L"/download.php?").c_str(), 33) != 0)
			{
				SetLastError(ERROR_BAD_FORMAT);
				ErrorMessageBox();
				return;
			}

			int pxUnit = GetFontSize();
			auto Parent = static_cast<FetcherMain*>(this->Parent);
			Parent->EditBox.SetWindowText(str);
			Parent->State = GettingInfo;
			Parent->Invalidate();
			Parent->FetchButton.ShowWindow(SW_HIDE);
			Parent->ViewTasksButton.ShowWindow(SW_HIDE);
			Parent->CleanButton.ShowWindow(SW_HIDE);
			Parent->CreateTaskByUrlButton.ShowWindow(SW_HIDE);
			Parent->BackButton.ShowWindow(SW_SHOW);

			RECT rect;
			Parent->GetClientRect(&rect);
			Parent->EditBox.MoveWindow(pxUnit * 7 / 2, pxUnit, rect.right - pxUnit * 7 / 2, pxUnit * 2);
			Parent->EditBox.SetReadOnly(true);
			Parent->ShowWindow(SW_SHOW);
			Parent->Progress.ShowWindow(SW_SHOW);
			StartSpiderThread(Parent, L"");
			EndDialog(0);
		}

		void OnDraw(HDC hdc, RECT rect)
		{
			rect.left += GetFontSize();
			rect.right -= GetFontSize();
			rect.top += GetFontSize();
			DrawText(hdc, String_TipOfCreatingTask, &rect, DT_WORDBREAK | DT_NOPREFIX);
		}
	};
	
	CreateTaskDialog(this).ModalDialogBox(0);
}

template<typename T>
inline bool TrySetValue(T* TargetAddress, T Value)
{
	__try
	{
		*TargetAddress = Value;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

void FetcherMain::Back()
{
	switch (State)
	{
	case WebView:
		if (HICON hIcon = BackButton.GetImageIcon(); hIcon == hBack)
		{
			BOOL b;
			if (webview)
				webview->webview->get_CanGoBack(&b);
			else
				b = FALSE;
			if (b)
				webview->webview->GoBack();
			else
			{
				webview.reset();
				State = Idle;
				CleanButton.AddWindowStyle(BS_COMMANDLINK);
				BackButton.ShowWindow(SW_HIDE);
				EditBox.MoveWindow(0, 0, 0, 0);
				EditBox.SetWindowText(nullptr);
				ViewTasksButton.ShowWindow(SW_SHOW);
				FetchButton.ShowWindow(SW_SHOW);
				CleanButton.ShowWindow(SW_SHOW);
				CreateTaskByUrlButton.ShowWindow(SW_SHOW);
				RECT rect;
				GetClientRect(&rect);
				Progress.ShowWindow(SW_HIDE);
				Progress.SetPos(0);
				OnSize(0, rect.right, rect.bottom, WindowBatchPositioner());
			}
		}
		else if (hIcon == hCancel)
		{
			webview->webview->Stop();
			BOOL b;
			webview->webview->get_CanGoBack(&b);
			if (!b)
			{
				BackButton.SetImageIcon(hBack);
				Back();
			}
		}
		break;
	case GettingInfo:case SummaryPage:
		if (webview)
			webview->webviewController->put_IsVisible(State == GettingInfo);
		Progress.SetPos(0);
		if (webview)
			if (State == SummaryPage)
				webview->webview->GoBack();
			else
			{
				PWSTR p;
				webview->webview->get_Source(&p);
				EditBox.SetWindowText(p);
				CoTaskMemFree(p);
				Progress.ShowWindow(SW_HIDE);
			}
		State = WebView;
		NextButton.MoveWindow(0, 0, 0, 0);
		Browse.MoveWindow(0, 0, 0, 0);
		FileList.MoveWindow(0, 0, 0, 0);
		ViewApps.MoveWindow(0, 0, 0, 0);
		ViewAdditionalEditions.MoveWindow(0, 0, 0, 0);
		DontDownloadApps.MoveWindow(0, 0, 0, 0);
		EditBox.SetReadOnly(true);
		SetScrollPos(SB_VERT, 0);
		ShowScrollBar(SB_VERT, false);
		ResetChildWindowsPos(this);
		FileList.DeleteAllItems();
		while (FileList.DeleteColumn(0));
		NextButton.EnableWindow(true);
		if (!webview)
		{
			State = WebView;
			Back();
		}
		break;
	case TaskList:
		State = Idle;
		CleanButton.AddWindowStyle(BS_COMMANDLINK);
		FileList.MoveWindow(0, 0, 0, 0);
		DeleteButton.MoveWindow(0, 0, 0, 0);
		NextButton.MoveWindow(0, 0, 0, 0);
		BackButton.ShowWindow(SW_HIDE);
		ViewTasksButton.ShowWindow(SW_SHOW);
		FetchButton.ShowWindow(SW_SHOW);
		CleanButton.ShowWindow(SW_SHOW);
		CreateTaskByUrlButton.ShowWindow(SW_SHOW);
		ResetChildWindowsPos(this);
		FileList.DeleteAllItems();
		while (FileList.DeleteColumn(0));
		NextButton.EnableWindow(true);
		break;
	case TaskSummary:
		if (!TrySetValue(&State, webview ? SummaryPage : TaskList))
		{
			PostMessage(kMsgBack, 0, 0);
			return;
		}
		FileList.DeleteAllItems();
		ViewAdditionalEditions.MoveWindow(0, 0, 0, 0);
		while (FileList.DeleteColumn(0));
		if (State == SummaryPage)
			MyCommand();
		else
			ViewTasks();
		CleanButton.ShowWindow(SW_HIDE);
		break;
	case Downloading:
		{
			DWORD tid = GetDownloadHostThreadId();
			if (tid)
				PostThreadMessageW(tid, kMsgExitProcess, 0, 0);
			if (HANDLE hThread = tid ? OpenThread(SYNCHRONIZE, FALSE, tid) : nullptr; hThread)
			{
				HCURSOR hCursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
				SetCursor(hCursor);
				WaitForSingleObject(hThread, INFINITE);
				CloseHandle(hThread);
				DestroyCursor(hCursor);
			}
		}
	case Done:case Failed:
		CleanButton.AddWindowStyle(BS_COMMANDLINK);
		Status.ShowWindow(SW_HIDE);
		Status.SetWindowText(nullptr);
		if (State == Failed)
			NextButton.SetWindowText(GetString(String_Next));
		State = Idle;
		bDoubleBuffer = false;
		FileList.DeleteAllItems();
		while (FileList.DeleteColumn(0));
		FileList.MoveWindow(0, 0, 0, 0);
		NextButton.MoveWindow(0, 0, 0, 0);
		BackButton.ShowWindow(SW_HIDE);
		ViewAdditionalEditions.MoveWindow(0, 0, 0, 0);
		ViewTasksButton.ShowWindow(SW_SHOW);
		FetchButton.ShowWindow(SW_SHOW);
		CleanButton.ShowWindow(SW_SHOW);
		CreateTaskByUrlButton.ShowWindow(SW_SHOW);
		ResetChildWindowsPos(this);
		break;
	}

	Redraw();
}

void FetcherMain::OnClose()
{
	if (!TrySetValue(&State, State))
	{
		PostMessage(kMsgClose, 0, 0);
		return;
	}
	DestroyWindow();
	webview.reset();
}

void FetcherMain::Next()
{
	if (State == SummaryPage)
	{
		NextButton.EnableWindow(false);
		std::filesystem::path ConfigPath = GetAppDataPath().GetPointer();
		ConfigPath /= L"Config";
		if (!std::filesystem::create_directories(ConfigPath)
			&& GetLastError() != ERROR_ALREADY_EXISTS)
		{
			ErrorMessageBox();
			NextButton.EnableWindow(true);
			return;
		}

		String Path = EditBox.GetWindowText();
		if (auto attrib = GetFileAttributesW(Path); attrib == INVALID_FILE_ATTRIBUTES)
		{
			ErrorMessageBox();
			NextButton.EnableWindow(true);
			return;
		}
		else if (!(attrib & FILE_ATTRIBUTE_DIRECTORY))
		{
			SetLastError(ERROR_DIRECTORY);
			ErrorMessageBox();
			NextButton.EnableWindow(true);
			return;
		}
		else if (!PathIsDirectoryEmptyW(Path))
		{
			SetLastError(ERROR_DIR_NOT_EMPTY);
			ErrorMessageBox();
			NextButton.EnableWindow(true);
			return;
		}

		HANDLE hDir = CreateFileW(Path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		if (hDir == INVALID_HANDLE_VALUE)
		{
			ErrorMessageBox();
			NextButton.EnableWindow(true);
			return;
		}
		DWORD dwSize = GetFinalPathNameByHandleW(hDir, nullptr, 0, VOLUME_NAME_DOS);
		if (!dwSize)
		{
			ErrorMessageBox();
			CloseHandle(hDir);
			NextButton.EnableWindow(true);
			return;
		}
		Path.Resize(dwSize);
		GetFinalPathNameByHandleW(hDir, Path.GetPointer(), dwSize, VOLUME_NAME_DOS);
		CloseHandle(hDir);

		auto Config = ConfigPath / std::to_wstring(uup.id);
		HKEY hKey = nullptr;

		HRSRC hRes = FindResourceA(nullptr, MAKEINTRESOURCEA(EmptyHive), "HIVE");
		if (hRes)
		{
			HGLOBAL hResData = LoadResource(nullptr, hRes);
			if (hResData)
			{
				DWORD dwResSize = SizeofResource(nullptr, hRes);
				LPVOID lpResData = LockResource(hResData);

				HANDLE hFile = CreateFileW(Config.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (hFile != INVALID_HANDLE_VALUE)
				{
					DWORD dwBytesWritten;
					WriteFile(hFile, lpResData, dwResSize, &dwBytesWritten, nullptr);
					CloseHandle(hFile);
				}
			}
		}

		LSTATUS status = RegLoadAppKeyW(Config.c_str(), &hKey, KEY_ALL_ACCESS, REG_PROCESS_APPKEY, 0);
		if (status != ERROR_SUCCESS)
		{
			SetLastError(status);
			ErrorMessageBox();
			DeleteFileW(Config.c_str());
			NextButton.EnableWindow(true);
			return;
		}
		RegSetValueExW(hKey, nullptr, 0, REG_QWORD, reinterpret_cast<PBYTE>(&uup.id), sizeof(uup.id));
		RegSetValueExW(hKey, L"ID", 0, REG_SZ, reinterpret_cast<PBYTE>(uup.UUPid.GetPointer()), static_cast<DWORD>(uup.UUPid.GetLength() + 1) * 2);
		RegSetValueExW(hKey, L"AppID", 0, REG_SZ, reinterpret_cast<PBYTE>(uup.AppUUPid.GetPointer()), static_cast<DWORD>(uup.AppUUPid.GetLength() + 1) * 2);
		RegSetValueExW(hKey, L"Name", 0, REG_SZ, reinterpret_cast<PBYTE>(uup.Name.GetPointer()), static_cast<DWORD>(uup.Name.GetLength() + 1) * 2);
		RegSetValueExW(hKey, L"LocaleName", 0, REG_SZ, reinterpret_cast<PBYTE>(uup.LocaleName.GetPointer()), static_cast<DWORD>(uup.LocaleName.GetLength() + 1) * 2);
		RegSetValueExW(hKey, L"Editions", 0, REG_SZ, reinterpret_cast<PBYTE>(uup.Editions.GetPointer()), static_cast<DWORD>(uup.Editions.GetLength() + 1) * 2);
		RegSetValueExW(hKey, L"Language", 0, REG_SZ, reinterpret_cast<PBYTE>(uup.Language.GetPointer()), static_cast<DWORD>(uup.Language.GetLength() + 1) * 2);
		RegSetValueExW(hKey, L"Path", 0, REG_SZ, reinterpret_cast<PBYTE>(Path.GetPointer()), static_cast<DWORD>(Path.GetLength() + 1) * 2);
		RegSetValueExW(hKey, L"EditionFriendlyNames", 0, REG_SZ, reinterpret_cast<PBYTE>(uup.EditionFriendlyNames.GetPointer()), static_cast<DWORD>(uup.EditionFriendlyNames.GetLength() + 1) * 2);
		for (auto& i : uup.AdditionalEditions)
			RegSetKeyValueW(hKey, L"VirtualEditions", i.first, REG_SZ, i.second.GetPointer(), static_cast<DWORD>(i.second.GetLength() + 1) * 2);
		for (auto& i : uup.System)
			RegSetKeyValueW(hKey, L"Files", i.Name.GetPointer(), REG_SZ, i.SHA1.GetPointer(), static_cast<DWORD>(i.SHA1.GetLength() + 1) * 2);
		if (!DontDownloadApps.GetCheck())
			for (auto& i : uup.Apps)
				RegSetKeyValueW(hKey, L"Apps", i.Name.GetPointer(), REG_SZ, i.SHA1.GetPointer(), static_cast<DWORD>(i.SHA1.GetLength() + 1) * 2);

		status = RegCloseKey(hKey);
		if (status != ERROR_SUCCESS)
		{
			SetLastError(status);
			ErrorMessageBox();
			DeleteFileW(Config.c_str());
			NextButton.EnableWindow(true);
			return;
		}

		uup.Path = std::move(Path);
		GoToTaskSummary();
	}
	else if (State == TaskList)
	{
		NextButton.EnableWindow(false);
		int sel = FileList.GetSelectionMark();
		if (sel == CTL_ERR)
		{
			NextButton.EnableWindow(false);
			DeleteButton.EnableWindow(false);
			return;
		}
		HKEY hKey;
		LSTATUS status = RegLoadAppKeyW(FileList.GetItemText(sel, 5), &hKey, KEY_QUERY_VALUE, REG_PROCESS_APPKEY, 0);
		if (status != ERROR_SUCCESS)
		{
			SetLastError(status);
			ErrorMessageBox();
			NextButton.EnableWindow(true);
			DeleteButton.EnableWindow(true);
			return;
		}

		auto GetValue = [&](PCWSTR pSubKey, PCWSTR pValue, String& data)
			{
				DWORD dwSize = 0;
				status = RegGetValueW(hKey, pSubKey, pValue, RRF_RT_REG_SZ, nullptr, nullptr, &dwSize);
				if (status != ERROR_SUCCESS)
					throw Exception(status);
				data.Resize(dwSize / 2 - 1);
				status = RegGetValueW(hKey, pSubKey, pValue, RRF_RT_REG_SZ, nullptr, data.GetPointer(), &dwSize);
				if (status != ERROR_SUCCESS)
					throw Exception(status);
			};

		try
		{
			GetValue(nullptr, L"ID", uup.UUPid);
			GetValue(nullptr, L"AppID", uup.AppUUPid);
			GetValue(nullptr, L"Name", uup.Name);
			GetValue(nullptr, L"LocaleName", uup.LocaleName);
			GetValue(nullptr, L"Editions", uup.Editions);
			GetValue(nullptr, L"Language", uup.Language);
			GetValue(nullptr, L"Path", uup.Path);
			GetValue(nullptr, L"EditionFriendlyNames", uup.EditionFriendlyNames);
			HKEY hKey2;
			status = RegOpenKeyExW(hKey, L"Files", 0, KEY_QUERY_VALUE, &hKey2);
			if (status != ERROR_SUCCESS)
				throw Exception(status);
			else
			{
				WCHAR szFile[MAX_PATH];
				for (DWORD i = 0; ; i++)
				{
					DWORD dwSize = sizeof(szFile) / 2;
					status = RegEnumValueW(hKey2, i, szFile, &dwSize, nullptr, nullptr, nullptr, nullptr);
					if (status != ERROR_SUCCESS)
					{
						if (status != ERROR_NO_MORE_ITEMS)
						{
							RegCloseKey(hKey2);
							throw Exception(status);
						}
						break;
					}
					String Value;
					GetValue(L"Files", szFile, Value);
					uup.System.push_back({ szFile, std::move(Value) });
				}
				RegCloseKey(hKey2);
			}
			status = RegOpenKeyExW(hKey, L"VirtualEditions", 0, KEY_QUERY_VALUE, &hKey2);
			if (status == ERROR_SUCCESS)
			{
				WCHAR szEdition[60];
				for (DWORD i = 0; ; i++)
				{
					DWORD dwSize = sizeof(szEdition);
					status = RegEnumValueW(hKey2, i, szEdition, &dwSize, nullptr, nullptr, nullptr, nullptr);
					if (status != ERROR_SUCCESS)
					{
						if (status != ERROR_NO_MORE_ITEMS)
						{
							RegCloseKey(hKey2);
							throw Exception(status);
						}
						break;
					}
					String Value;
					GetValue(L"VirtualEditions", szEdition, Value);
					uup.AdditionalEditions[szEdition] = std::move(Value);
				}
				RegCloseKey(hKey2);
			}
			status = RegOpenKeyExW(hKey, L"Apps", 0, KEY_QUERY_VALUE, &hKey2);
			if (status == ERROR_SUCCESS)
			{
				WCHAR szApp[MAX_PATH];
				for (DWORD i = 0; ; i++)
				{
					DWORD dwSize = sizeof(szApp) / 2;
					status = RegEnumValueW(hKey2, i, szApp, &dwSize, nullptr, nullptr, nullptr, nullptr);
					if (status != ERROR_SUCCESS)
					{
						if (status != ERROR_NO_MORE_ITEMS)
						{
							RegCloseKey(hKey2);
							throw Exception(status);
						}
						break;
					}
					String Value;
					GetValue(L"Apps", szApp, Value);
					uup.Apps.push_back({ szApp, std::move(Value) });
				}
				RegCloseKey(hKey2);
			}

			RegCloseKey(hKey);
			auto sort = [](uup_struct::FileList_t v)
				{
					std::sort(v.begin(), v.end(), [](const uup_struct::File& a, const uup_struct::File& b) -> bool
						{
							return wcscmp(a.Name, b.Name) > 0;
						});
				};
			sort(uup.System);
			sort(uup.Apps);
		}
		catch (Exception& e)
		{
			RegCloseKey(hKey);
			uup.AdditionalEditions.clear();
			SetLastError(e.dwSysErrCode);
			ErrorMessageBox();
			NextButton.EnableWindow(true);
			DeleteButton.EnableWindow(true);
			return;
		}

		webview = nullptr;
		GoToTaskSummary();
	}
	else if (State == TaskSummary || State == Failed)
	{
		webview.reset();
		if (State == Failed)
			NextButton.SetWindowText(GetString(String_Next));
		State = Downloading;
		RECT rect;
		GetClientRect(&rect);
		int pxUnit = GetFontSize();
		Status.MoveWindow(pxUnit * 5 / 2, pxUnit * 7, rect.right, pxUnit * 6);
		Status.ShowWindow(SW_SHOW);
		NextButton.MoveWindow(0, 0, 0, 0);
		bDoubleBuffer = true;
		StartDownloadHost(*this);
	}
	else if (State == Done)
	{
		WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), uup.Path.GetPointer(), static_cast<DWORD>(sizeof(WCHAR) * (uup.Path.GetLength() + 1)), nullptr, nullptr);
		ExitProcess(kExitCodePathWrittenToStdout);
	}
}

void FetcherMain::BrowseFile()
{
	OleInitialize(nullptr);
	String Path;
	if (GetOpenFolderName(this, Path))
		EditBox.SetWindowText(Path);
	OleUninitialize();
}

void FetcherMain::ViewAppFiles()
{
	struct AppList : Dialog
	{
		AppList(FetcherMain* Parent) : Dialog(Parent, GetFontSize() * 75, GetFontSize() * 30, DS_MODALFRAME | DS_FIXEDSYS | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_SIZEBOX | WS_MAXIMIZEBOX, GetString(String_ViewApps)),
			FileList(this, 0, WS_CHILD | WS_VISIBLE)
		{
		}

		void Init(LPARAM)
		{
			FileList.InsertColumn(GetString(String_File).GetPointer(), GetFontSize() * 40, 0);
			FileList.InsertColumn(GetString(String_Size).GetPointer(), GetFontSize() * 8, 1);
			FileList.InsertColumn(L"SHA-1", GetFontSize() * 25, 2);
			WCHAR szSize[10];
			for (auto& i : static_cast<FetcherMain*>(Parent)->uup.Apps)
			{
				int index = FileList.InsertItem();
				FileList.SetItemText(index, 0, i.Name.GetPointer());
				StrFormatByteSizeW(i.Size, szSize, 10);
				FileList.SetItemText(index, 1, szSize);
				FileList.SetItemText(index, 2, i.SHA1.GetPointer());
			}
			CenterWindow(Parent);
			RECT rect;
			GetClientRect(&rect);
			FileList.MoveWindow(0, 0, rect.right, rect.bottom);
		}

		void OnSize(BYTE ResizeType, int nClientWidth, int nClientHeight, WindowBatchPositioner wbp)
		{
			wbp.MoveWindow(FileList, 0, 0, nClientWidth, nClientHeight);
		}

		ListView FileList;
	};

	AppList(this).ModalDialogBox(0);
}

void FetcherMain::ViewVirtualEditions()
{
	struct EditionList : Dialog
	{
		EditionList(FetcherMain* Parent) : Dialog(Parent, GetFontSize() * 40, GetFontSize() * 20, WS_SIZEBOX | DS_MODALFRAME | DS_FIXEDSYS | WS_POPUP | WS_CAPTION | WS_SYSMENU, GetString(String_ViewAdditionalEditions)),
			List(this, 0, WS_CHILD | WS_VISIBLE)
		{
		}

		void Init(LPARAM)
		{
			List.InsertColumn(L"EditionID", GetFontSize() * 15, 0);
			List.InsertColumn(GetString(String_Edition).GetPointer(), GetFontSize() * 23, 1);

			for (auto& i : static_cast<FetcherMain*>(Parent)->uup.AdditionalEditions)
			{
				int index = List.InsertItem();
				List.SetItemText(index, 0, i.first);
				List.SetItemText(index, 1, i.second);
			}
			CenterWindow(Parent);
			RECT rect;
			GetClientRect(&rect);
			List.MoveWindow(0, 0, rect.right, rect.bottom);
		}

		void OnSize(BYTE ResizeType, int nClientWidth, int nClientHeight, WindowBatchPositioner wbp)
		{
			wbp.MoveWindow(List, 0, 0, nClientWidth, nClientHeight);
		}

		ListView List;
	};

	EditionList(this).ModalDialogBox(0);
}

void FetcherMain::Delete()
{
	if (FileList.GetItemCount() > 0)
		for (int i = FileList.GetItemCount(); i != -1; --i)
			if (FileList.GetItemState(i, LVIS_SELECTED) & LVIS_SELECTED)
			{
				DeleteFileW(FileList.GetItemText(i, 5));
				FileList.DeleteItem(i);
			}
	NextButton.EnableWindow(false);
	DeleteButton.EnableWindow(false);
}

void FetcherMain::Redraw()
{
	Invalidate(!bDoubleBuffer);
	UpdateWindow();
}

void FetcherMain::GoToTaskSummary()
{
	if (!SetCurrentDirectoryW(uup.Path))
	{
		ErrorMessageBox();
		return;
	}
	NextButton.EnableWindow(false);
	State = TaskSummary;
	SetScrollPos(SB_VERT, 0);
	ShowScrollBar(SB_VERT, false);
	FileList.MoveWindow(0, 0, 0, 0);
	DeleteButton.MoveWindow(0, 0, 0, 0);
	EditBox.MoveWindow(0, 0, 0, 0);
	Browse.MoveWindow(0, 0, 0, 0);
	DontDownloadApps.MoveWindow(0, 0, 0, 0);
	ViewApps.MoveWindow(0, 0, 0, 0);
	ViewAdditionalEditions.MoveWindow(0, 0, 0, 0);
	if (webview)
		webview->webviewController->put_IsVisible(FALSE);

	FileList.DeleteAllItems();
	while (FileList.DeleteColumn(0));
	Invalidate();
	ResetChildWindowsPos(this);

	FileList.InsertColumn(GetString(String_File).GetPointer(), GetFontSize() * 45, 0);
	FileList.InsertColumn(GetString(String_Progress).GetPointer(), GetFontSize() * 7, 1);

	auto SetProgress = [&](int index, const uup_struct::File& i)
		{
			DWORD dw = GetFileAttributesW(i.Name);
			if (dw == INVALID_FILE_ATTRIBUTES)
				FileList.SetItemText(index, 1, GetString(String_NotStart).GetPointer());
			else if (dw & FILE_ATTRIBUTE_DIRECTORY)
			{
				RemoveDirectoryRecursive(i.Name);
				FileList.SetItemText(index, 1, GetString(String_NotStart).GetPointer());
			}
			else
			{
				LRDLDLTASKMETADATA* p;
				LRDLDLTASKINFO info;
				if (LRDLdlGetTaskInfo(i.Name, &info, &p))
				{
					ULONGLONG ullFetched = 0;
					for (BYTE i = 0; i != info.nThreads; ++i)
						ullFetched += info.pszThreadProgress[i].ullFetchedBytes;
					FileList.SetItemText(index, 1, std::format(L"{}%", ullFetched * 100 / info.ResourceInfo.ullResourceBytes).c_str());
				}
				else
					FileList.SetItemText(index, 1, GetString(String_Finished).GetPointer());
				LRDLdlDelete(p);
			}
		};


	std::atomic_bool EarlyExit = false;
	std::atomic_bool Done = false;
	auto AddItems = [&]() {
		for (auto& i : uup.Apps)
		{
			int index = FileList.InsertItem();
			FileList.SetItemText(index, 0, i.Name.GetPointer());
			SetProgress(index, i);
			if (EarlyExit) return;
		}
		for (auto& i : uup.System)
		{
			int index = FileList.InsertItem();
			FileList.SetItemText(index, 0, i.Name.GetPointer());
			SetProgress(index, i);
			if (EarlyExit) return;
		}
		Done = true;
		PostMessage(WM_NULL, 0, 0);
	};


	if (!webview)
	{
		auto fut = std::async(std::launch::async, AddItems);
		DWORD dwOldProtect;
		VirtualProtect(&State, sizeof(State), PAGE_READONLY, &dwOldProtect);
		for (MSG msg; GetMessageW(&msg, nullptr, 0, 0) && !Done; DispatchMessageW(&msg))
		{
			if (msg.hwnd == hWnd) switch (msg.message)
			{
			case kMsgClose:
			case kMsgBack:
				EarlyExit = true;
				do FileList.DispatchAllMessages();
				while (fut.wait_for(std::chrono::milliseconds(1)) == std::future_status::timeout);
				VirtualProtect(&State, sizeof(State), dwOldProtect, &dwOldProtect);
				(this->*(msg.message == kMsgClose ? &FetcherMain::OnClose : &FetcherMain::Back))();
				return;
			}
			TranslateMessage(&msg);
		}
		VirtualProtect(&State, sizeof(State), dwOldProtect, &dwOldProtect);

		if (EarlyExit) return;
	}
	else AddItems();

	NextButton.EnableWindow(true);
	CleanButton.RemoveWindowStyle(BS_COMMANDLINK);
	CleanButton.ShowWindow(SW_SHOW);
}
