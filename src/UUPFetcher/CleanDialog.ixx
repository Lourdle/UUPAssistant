module;
#include "pch.h"
#include "framework.h"
#include "FetcherMain.h"
#include "resource.h"
#include "../common/common.h"

#include <Shlwapi.h>
#include <shellapi.h>

#include <string>

import Misc;
using namespace std;
using namespace Lourdle::UIFramework;
export module CleanDialog;

export struct CleanDialog : DialogEx2<FetcherMain>
{
	CleanDialog(FetcherMain* p) : DialogEx2(p, GetFontSize() * 24, GetFontSize() * 21, DS_MODALFRAME | DS_FIXEDSYS | WS_POPUP | WS_CAPTION | WS_SYSMENU, GetString(String_Clean)),
		CleanCacheButton(this, &CleanDialog::CleanCache, ButtonStyle::CommandLink), CleanDataButton(this, &CleanDialog::CleanData, ButtonStyle::CommandLink), BrowseButton(this, &CleanDialog::BrowseDir, ButtonStyle::CommandLink)
	{
		DeleteObject(hFont);
		hFont = EzCreateFont(GetFontSize() * 5 / 4);
		WCHAR szPath[MAX_PATH];
		GetSystemDirectoryW(szPath, MAX_PATH);
		SetCurrentDirectoryW(szPath);
	}

	~CleanDialog()
	{
		DeleteObject(hFont);
	}

	Button CleanCacheButton;
	Button CleanDataButton;
	Button BrowseButton;

	LONGLONG ullCacheSize;
	LONGLONG ullDataSize;

	void Init()
	{
		CenterWindow(Parent);

		CleanCacheButton.SetWindowText(GetString(String_CleanCache));
		CleanDataButton.SetWindowText(GetString(String_CleanData));
		BrowseButton.SetWindowText(GetString(String_Browse));

		SIZE size;
		CleanCacheButton.GetIdealSize(&size);
		CleanCacheButton.MoveWindow(GetFontSize() * 2, GetFontSize() * 2, GetFontSize() * 20, size.cy);
		CleanDataButton.MoveWindow(GetFontSize() * 2, GetFontSize() * 6, GetFontSize() * 20, size.cy);
		BrowseButton.MoveWindow(GetFontSize() * 2, GetFontSize() * 11, GetFontSize() * 20, size.cy);

		auto AppDataPath = GetAppDataPath();
		if (!PathIsDirectoryW(AppDataPath))
		{
			ullCacheSize = 0;
			CleanCacheButton.EnableWindow(FALSE);
			BrowseButton.EnableWindow(FALSE);
			ullDataSize = 0;
			if (!PathIsDirectoryEmptyW(String(AppDataPath, AppDataPath.GetLength() - 11)))
				CleanDataButton.EnableWindow(FALSE);
			return;
		}

		ullDataSize = GetDirectorySize(AppDataPath);
		auto ullSize = GetDirectorySize(AppDataPath + L"\\Cache");
		if (ullSize == -1)
			ullCacheSize = 0;
		else
			ullCacheSize = ullSize;
		ullSize = GetDirectorySize(AppDataPath + L"\\Temp");
		if (ullSize != -1)
			ullDataSize += ullSize;
		if (ullCacheSize == 0)
			CleanCacheButton.EnableWindow(FALSE);
	}

	void OnDraw(HDC hdc, RECT rect)
	{
		rect.top += GetFontSize() * 16;
		rect.left += GetFontSize() * 2;
		WCHAR szDataSize[12];
		String CacheSize = GetString(String_CacheSize) + StrFormatByteSizeW(ullCacheSize, szDataSize, 64);
		String DataSize = GetString(String_DataSize) + StrFormatByteSizeW(ullDataSize, szDataSize, 64);
		DrawText(hdc, CacheSize, &rect, DT_SINGLELINE);
		rect.top += GetFontSize() * 2;
		DrawText(hdc, DataSize, &rect, DT_SINGLELINE);
	}

	void CleanCache()
	{
		auto AppDataPath = GetAppDataPath();
		if (!RemoveDirectoryRecursive(AppDataPath + L"\\Cache")
			&& GetLastError() != ERROR_PATH_NOT_FOUND
			|| !RemoveDirectoryRecursive(AppDataPath + L"\\Temp")
			&& GetLastError() != ERROR_PATH_NOT_FOUND)
		{
			ErrorMessageBox();
			EndDialog(0);
			return;
		}

		ullDataSize -= ullCacheSize;
		ullCacheSize = 0;
		CleanCacheButton.EnableWindow(FALSE);
		RedrawWindow(nullptr, nullptr, RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW);
	}

	void CleanData()
	{
		auto AppDataPath = GetAppDataPath();
		if (!RemoveDirectoryRecursive(AppDataPath)
			&& GetLastError() != ERROR_PATH_NOT_FOUND)
		{
			ErrorMessageBox();
			EndDialog(0);
			return;
		}
		AppDataPath = String(AppDataPath, AppDataPath.GetLength() - 11);
		if (PathIsDirectoryEmptyW(AppDataPath))
			if (!RemoveDirectoryW(AppDataPath))
			{
				ErrorMessageBox();
				EndDialog(0);
				return;
			}
		ullDataSize = 0;
		ullCacheSize = 0;
		CleanDataButton.EnableWindow(FALSE);
		CleanCacheButton.EnableWindow(FALSE);
		BrowseButton.EnableWindow(FALSE);
		RedrawWindow(nullptr, nullptr, RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW);
	}

	void BrowseDir()
	{
		ShellExecuteW(nullptr, L"open", GetAppDataPath().GetPointer(), nullptr, nullptr, SW_SHOWNORMAL);
	}
};
