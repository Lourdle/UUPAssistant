#include "pch.h"
#include "UUPAssistant.h"
#include "misc.h"
#include "Resources/resource.h"

#include <string>
#include <algorithm>
#include <thread>
#include <functional>
#include <filesystem>
#include <memory>

#include <Shlwapi.h>
#include <gdiplus.h>
#include <dismapi.h>

using namespace Gdiplus;
using namespace Lourdle::UIFramework;

import Constants;

constexpr int BaseDpi = 96;

DriverDlg::DriverDlg(WindowBase* Parent, SessionContext& ctx) : DialogEx2(Parent, GetFontSize() * 60, GetFontSize() * 49, WS_CAPTION | WS_BORDER | WS_SYSMENU | DS_FIXEDSYS | DS_MODALFRAME | WS_CLIPCHILDREN, GetString(String_AddDrivers)),
ctx(ctx), DriverList(this, 0, WS_CHILD | WS_BORDER | WS_VISIBLE | LVS_SINGLESEL), BrowseDir(this, &DriverDlg::Browse), Path(this, 0), BrowseFiles(this, &DriverDlg::FileBrowse),
Recurse(this, 0, ButtonStyle::AutoCheckbox), ForceUnsigned(this, 0, ButtonStyle::AutoCheckbox), AddDriverFromInstalledOS(this, &DriverDlg::ScanInstalledDrivers),
Add(this, &DriverDlg::AddItems), Remove(this, &DriverDlg::PopMenu), hRemoveOptionMenu(CreatePopupMenu())
{
	WORD id = Random();
	AppendMenuW(hRemoveOptionMenu, 0, id, GetString(String_RemoveThis));
	RegisterCommand(&DriverDlg::RemoveIt, id, 0);
	id = Random();
	AppendMenuW(hRemoveOptionMenu, 0, id, GetString(String_RemoveSelected));
	RegisterCommand(&DriverDlg::RemoveChecked, id, 0);
	id = Random();
	AppendMenuW(hRemoveOptionMenu, 0, id, GetString(String_RemoveAll));
	RegisterCommand(&DriverDlg::RemoveAll, id, 0);
}

DriverDlg::~DriverDlg()
{
	DestroyMenu(hRemoveOptionMenu);
}

void DriverDlg::Init()
{
	Ring = std::make_unique<ProgressRing>(this);
	Ring->ShowWindow(SW_HIDE);
	DriverList.SetExtendedListViewStyle(LVS_EX_CHECKBOXES);
	DriverList.InsertColumn(GetString(String_File).GetPointer(), GetFontSize() * 25, 0);
	DriverList.InsertColumn(GetString(String_Path).GetPointer(), GetFontSize() * 90, 1);
	for (auto& i : ctx.DriverVector)
	{
		int index = DriverList.InsertItem();
		std::filesystem::path p(i.GetPointer());
		DriverList.SetItemText(index, 0, p.filename().c_str());
		DriverList.SetItemText(index, 1, i.GetPointer());
	}

	BrowseDir.SetWindowText(GetString(String_BrowseDir));
	BrowseFiles.SetWindowText(GetString(String_Browse));
	Add.SetWindowText(GetString(String_Add));
	Recurse.SetWindowText(GetString(String_Recurse));
	ForceUnsigned.SetWindowText(GetString(String_ForceUnsigned));
	Remove.SetWindowText(GetString(String_Remove));
	AddDriverFromInstalledOS.SetWindowText(GetString(String_AddDriverFromInstalledOS));
	ForceUnsigned.SetCheck(ctx.bForceUnsigned);
	Recurse.SetCheck(BST_CHECKED);

	int pxUnit = GetFontSize();
	DriverList.MoveWindow(pxUnit, pxUnit, pxUnit * 58, pxUnit * 30);
	Remove.MoveWindow(pxUnit, pxUnit * 32, pxUnit * 5, pxUnit * 2);
	AddDriverFromInstalledOS.MoveWindow(pxUnit * 47, pxUnit * 32, pxUnit * 12, pxUnit * 2);
	SIZE size;
	ForceUnsigned.GetIdealSize(&size);
	ForceUnsigned.MoveWindow(pxUnit, pxUnit * 35, size.cx, size.cy);
	Recurse.GetIdealSize(&size);
	Recurse.MoveWindow(pxUnit, pxUnit * 35 + size.cy, size.cx, size.cy);
	Path.MoveWindow(pxUnit, pxUnit * 43, pxUnit * 50, pxUnit * 2);
	BrowseDir.MoveWindow(pxUnit * 52, pxUnit * 43, pxUnit * 7, pxUnit * 2);
	BrowseFiles.MoveWindow(pxUnit * 54, pxUnit * 46, pxUnit * 5, pxUnit * 2);
	Add.MoveWindow(pxUnit, pxUnit * 46, pxUnit * 5, pxUnit * 2);
	Ring->MoveWindow(pxUnit * 7, pxUnit * 46, pxUnit * 2, pxUnit * 2);
	CenterWindow(Parent);
}

void DriverDlg::OnDraw(HDC hdc, RECT rect)
{
	rect.left = GetFontSize();
	rect.right -= rect.left;
	rect.bottom = static_cast<LONG>(rect.left * 42.5);

	DrawText(hdc,
		ResStrFormat(String_AddDrv, DriverList.GetItemCount()),
		-1, &rect, DT_WORDBREAK | DT_BOTTOM);
}

bool DriverDlg::OnClose()
{
	return Ring->IsWindowVisible();
}

void DriverDlg::OnDestroy()
{
	ctx.bForceUnsigned = ForceUnsigned.GetCheck() == BST_CHECKED;
	Ring.reset();
}

void DriverDlg::Browse()
{
	String Dir;
	if (GetOpenFolderName(this, Dir))
		Path.SetWindowText(Dir.GetPointer());
}

static void NotApplicableDriverMessageBox(Dialog* p, const std::vector<String>& Drivers)
{
	if (p->MessageBox(ResStrFormat(String_NotApplicableDriver, static_cast<int>(Drivers.size())),
		GetString(String_Notice), MB_ICONINFORMATION | MB_YESNO) == IDYES)
	{
		struct DriverList : DialogEx2<Dialog, const std::vector<String>&>
		{
			DriverList(Dialog* Parent) : DialogEx2(Parent, GetFontSize() * 70, GetFontSize() * 40, WS_CAPTION | WS_SYSMENU | DS_FIXEDSYS | DS_MODALFRAME | WS_SIZEBOX, nullptr),
				List(this)
			{
			}

			ListBox List;

			void Init(const std::vector<String>& Drivers)
			{
				for (auto& i : Drivers)
					List.AddString(i);
				CenterWindow(Parent);
			}

			void OnSize(BYTE ResizeType, int nClientWidth, int nClientHeight, WindowBatchPositioner wbp)
			{
				wbp.MoveWindow(List, 0, 0, nClientWidth, nClientHeight);
			}
		};

		DriverList(p).ModalDialogBox(Drivers);
	}
}

void DriverDlg::FileBrowse()
{
	String File;
	IFileOpenDialog* pFileOpenDialog = CreateFileOpenDialogInstance();
	COMDLG_FILTERSPEC cf = { GetString(String_Inf), L"*.inf"};
	pFileOpenDialog->SetFileTypes(1, &cf);
	DWORD dwOptions;
	pFileOpenDialog->GetOptions(&dwOptions);
	pFileOpenDialog->SetOptions(dwOptions | FOS_ALLOWMULTISELECT | FOS_NOCHANGEDIR | FOS_NODEREFERENCELINKS);

	DWORD dw;
	if (FAILED(pFileOpenDialog->Show(hWnd)))
		return;
	IShellItemArray* sia;
	pFileOpenDialog->GetResults(&sia);
	sia->GetCount(&dw);
	IShellItem* si;
	if (dw == 1)
	{
		sia->GetItemAt(0, &si);
		PWSTR name;
		si->GetDisplayName(SIGDN_DESKTOPABSOLUTEEDITING, &name);
		Path.SetWindowText(name);
		CoTaskMemFree(name);
		si->Release();
		sia->Release();
		return;
	}

	std::vector<String> Drivers;

	for (DWORD i = 0; i != dw; ++i)
	{
		sia->GetItemAt(i, &si);
		PWSTR name;
		si->GetDisplayName(SIGDN_DESKTOPABSOLUTEEDITING, &name);
		si->Release();
		HANDLE hFile = CreateFileW(name, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			ErrorMessageBox();
			CoTaskMemFree(name);
			continue;
		}
		if (!IsApplicableDriver(hFile, ctx.TargetImageInfo.Arch))
		{
			CloseHandle(hFile);
			Drivers.push_back(name);
			CoTaskMemFree(name);
			continue;
		}

		auto PathName = GetFinalPathName(hFile);
		CloseHandle(hFile);
		ctx.DriverVector.push_back(PathName);
		int index = DriverList.InsertItem();
		PWSTR p = PathName.end();
		while (*p != '\\')
			--p;
		*p = 0;
		DriverList.SetItemText(index, 0, p + 1);
		*p = '\\';
		DriverList.SetItemText(index, 1, PathName.GetPointer());
		ctx.DriverVector.push_back(std::move(PathName));

		CoTaskMemFree(name);
	}

	if (!Drivers.empty())
		NotApplicableDriverMessageBox(this, Drivers);


	Invalidate();
	sia->Release();
}

void DriverDlg::AddItems()
{
	Ring->ShowWindow(SW_SHOW);
	Ring->Start();
	Add.EnableWindow(false);
	Path.EnableWindow(false);
	Recurse.EnableWindow(false);
	ForceUnsigned.EnableWindow(false);
	BrowseDir.EnableWindow(false);
	BrowseFiles.EnableWindow(false);
	AddDriverFromInstalledOS.EnableWindow(false);

	std::thread([this]()
		{
			auto Reset = [&]()
				{
					Ring->ShowWindow(SW_HIDE);
					Add.EnableWindow(true);
					Path.EnableWindow(true);
					Recurse.EnableWindow(true);
					ForceUnsigned.EnableWindow(true);
					BrowseDir.EnableWindow(true);
					BrowseFiles.EnableWindow(true);
					AddDriverFromInstalledOS.EnableWindow(true);
				};

			std::wstring pathStr = Path.GetWindowText().GetPointer();
			bool hasWildcard = pathStr.find_first_of(L"*?") != std::wstring::npos;
			namespace fs = std::filesystem;
			
			if (!hasWildcard)
			{
				fs::path p(pathStr);
				std::error_code ec;
				if (fs::is_directory(p, ec))
				{
					if (Recurse.GetCheck())
					{
						std::vector<std::wstring> Drivers;
						if (!FindDrivers(p.c_str(), ctx.TargetImageInfo.Arch, Drivers))
							ErrorMessageBox();
						else
						{
							for (auto& i : Drivers)
							{
								String drv(i.c_str());
								if (std::find(ctx.DriverVector.begin(), ctx.DriverVector.end(), drv) != ctx.DriverVector.end())
									continue;
								
								int index = DriverList.InsertItem();
								fs::path dp(i);
								DriverList.SetItemText(index, 0, dp.filename().c_str());
								DriverList.SetItemText(index, 1, drv.GetPointer());
								ctx.DriverVector.push_back(std::move(drv));
							}
						}
					}
					else
					{
						pathStr = (p / L"*.inf").wstring();
						hasWildcard = true;
					}
				}
				else if (fs::exists(p, ec))
				{
					HANDLE hFile = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
					if (hFile == INVALID_HANDLE_VALUE)
					{
						ErrorMessageBox();
						Reset();
						return;
					}

					if (!IsApplicableDriver(hFile, ctx.TargetImageInfo.Arch))
					{
						CloseHandle(hFile);
						String Text = ResStrFormat(String_NotApplicableDriver, 1);
						*std::find(Text.begin(), Text.end(), L'\r') = 0;
						MessageBox(Text, GetString(String_Notice), MB_ICONINFORMATION);
						Reset();
						return;
					}

					auto FinalPathName = GetFinalPathName(hFile);
					CloseHandle(hFile);
					if (FinalPathName.Empty())
					{
						ErrorMessageBox();
						Reset();
						return;
					}
					
					int index = DriverList.InsertItem();
					fs::path fp(FinalPathName.GetPointer());
					DriverList.SetItemText(index, 0, fp.filename().c_str());
					DriverList.SetItemText(index, 1, FinalPathName.GetPointer());
					ctx.DriverVector.push_back(std::move(FinalPathName));
					
					Invalidate();
					Reset();
					return;
				}
				else
				{
					ErrorMessageBox();
					Reset();
					return;
				}
			}

			if (hasWildcard)
			{
				fs::path p(pathStr);
				fs::path dir = p.parent_path();
				
				if (!fs::exists(dir))
				{
					ErrorMessageBox();
					Reset();
					return;
				}

				std::unordered_set<std::wstring> InfFiles;
				std::vector<String> Drivers;
				
				WIN32_FIND_DATAW wfd;
				HANDLE hFind = FindFirstFileW(pathStr.c_str(), &wfd);
				if (hFind != INVALID_HANDLE_VALUE)
				{
					do
					{
						if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
							continue;
						
						if (PathMatchSpecW(wfd.cFileName, L"*.inf"))
						{
							fs::path filePath = dir / wfd.cFileName;
							HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
							if (hFile == INVALID_HANDLE_VALUE)
							{
								ErrorMessageBox();
								FindClose(hFind);
								Reset();
								return;
							}

							if (!GetAdditionalDrivers(hFile, InfFiles) || !IsApplicableDriver(hFile, ctx.TargetImageInfo.Arch))
							{
								CloseHandle(hFile);
								continue;
							}

							auto FinalPathName = GetFinalPathName(hFile);
							CloseHandle(hFile);
							if (FinalPathName.Empty())
							{
								ErrorMessageBox();
								FindClose(hFind);
								Reset();
								return;
							}
							Drivers.push_back(std::move(FinalPathName));
						}

					} while (FindNextFileW(hFind, &wfd));
					FindClose(hFind);
				}
				else
				{
					ErrorMessageBox();
					Reset();
					return;
				}

				for (auto it = Drivers.begin(); it != Drivers.end(); )
				{
					fs::path dp(it->GetPointer());
					std::wstring fname = dp.filename().wstring();
					bool found = false;
					for (const auto& inf : InfFiles)
					{
						if (_wcsicmp(fname.c_str(), inf.c_str()) == 0)
						{
							found = true;
							break;
						}
					}
					if (found)
						it = Drivers.erase(it);
					else
						++it;
				}

				for (auto& drv : Drivers)
				{
					if (std::find(ctx.DriverVector.begin(), ctx.DriverVector.end(), drv) != ctx.DriverVector.end())
						continue;
						
					int index = DriverList.InsertItem();
					fs::path dp(drv.GetPointer());
					DriverList.SetItemText(index, 0, dp.filename().c_str());
					DriverList.SetItemText(index, 1, drv.GetPointer());
					ctx.DriverVector.push_back(std::move(drv));
				}
			}

			Invalidate();
			Reset();
		}).detach();
}

void DriverDlg::PopMenu()
{
	int sel = DriverList.GetSelectionMark();
	MENUITEMINFOW mii = {
		.cbSize= sizeof(mii),
		.fMask = MIIM_STATE,
		.fState = UINT(sel == CTL_ERR ? MFS_DISABLED : MFS_ENABLED)
	};
	SetMenuItemInfoW(hRemoveOptionMenu, 0, TRUE, &mii);
	POINT point;
	GetCursorPos(&point);
	TrackPopupMenuEx(hRemoveOptionMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_VERNEGANIMATION, point.x, point.y, hWnd, nullptr);
}

static void RemoveItem(int sel, DriverDlg* p)
{
	p->DriverList.DeleteItem(sel);
	auto it = p->ctx.DriverVector.cbegin() + sel;
	if (_wcsnicmp(p->ctx.PathTemp.c_str(), *it, p->ctx.PathTemp.size()) == 0)
	{
		*PathFindFileNameW(*it) = 0;
		DeleteDirectory(*it);
	}
	p->ctx.DriverVector.erase(it);

	p->DriverList.SetSelectionMark(-1);
}

void DriverDlg::RemoveIt()
{
	int sel = DriverList.GetSelectionMark();
	if (sel == CTL_ERR)
		return;

	RemoveItem(sel, this);
	Invalidate();
}

void DriverDlg::RemoveChecked()
{
	for (int i = DriverList.GetItemCount() - 1; i != -1; --i)
		if (DriverList.GetCheckState(i) == TRUE)
			RemoveItem(i, this);
	DriverList.SetSelectionMark(-1);
	Invalidate();
}

void DriverDlg::RemoveAll()
{
	DriverList.DeleteAllItems();
	ctx.DriverVector.clear();
	DeleteDirectory((ctx.PathTemp + L"Drivers").c_str());
	Invalidate();
}

static bool EnumTreeViewChildItems(TreeView* pTreeView, HTREEITEM hItem, std::function<bool(HTREEITEM)> EnumProc)
{
	auto h = pTreeView->GetChild(hItem);
	while (h)
	{
		if (!EnumProc(h))
			return false;
		if (!EnumTreeViewChildItems(pTreeView, h, EnumProc))
			return false;
		h = pTreeView->GetNextSibling(h);
	}
	return true;
}

static void EnumTreeViewItems(TreeView* pTreeView, std::function<bool(HTREEITEM)> EnumProc)
{
	auto hItem = pTreeView->GetRoot();
	while (hItem)
	{
		if (!EnumProc(hItem))
			return;
		if (!EnumTreeViewChildItems(pTreeView, hItem, EnumProc))
			return;
		hItem = pTreeView->GetNextSibling(hItem);
	}
}

static void CALLBACK TimerProc(HWND, UINT, UINT_PTR, DWORD);

class SystemList : public Window
{
	friend void CALLBACK TimerProc(HWND, UINT, UINT_PTR, DWORD);
public:
	const int nPixelPerItem;

	SystemList(Dialog* Parent, int X, int Y, int cx, int cy) : Window(X, Y, cx, cy, nullptr, WS_CHILD | WS_BORDER | WS_VSCROLL, *Parent),
		nPixelPerItem(MulDiv(90, GetDpiForWindow(hWnd), BaseDpi) + GetFontSize() * 4)
	{
		bDoubleBuffer = true;
	}

	void InsertItem(WCHAR cSystemDrive, PCWSTR pszBranch, PCWSTR pszVersionString, PWSTR pszProductName)
	{
		CHAR szPath[] = "X:\\Windows\\Branding\\Basebrd\\basebrd.dll";
		*szPath = static_cast<CHAR>(cSystemDrive);

		auto CheckVersion = [&]()
			{
				VersionStruct Version;
				ParseVersionString(pszVersionString, Version);

				if (Version.dwBuild >= 22000)
				{
					auto p = wcschr(pszProductName, '1');
					if (p && p[1] == '0')
						p[1] = '1';
				}
			};
		HMODULE hModule = LoadLibraryExA(szPath, nullptr, LOAD_LIBRARY_AS_IMAGE_RESOURCE);
		if (hModule)
		{
			bool Bitmap = false;
			IStream* pStream = nullptr;
			HRSRC hRes = FindResourceA(hModule, MAKEINTRESOURCEA(123), "IMAGE");
			if (!hRes)
			{
				Bitmap = true;
				pStream = reinterpret_cast<IStream*>(LoadImageW(hModule, MAKEINTRESOURCEW(123), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_DEFAULTSIZE | LR_LOADTRANSPARENT));
			}
			else
			{
				DWORD dwSize = SizeofResource(hModule, hRes);
				PVOID pData = LockResource(LoadResource(hModule, hRes));
				pStream = SHCreateMemStream(static_cast<const BYTE*>(pData), dwSize);
			}

			hRes = FindResourceA(hModule, MAKEINTRESOURCEA(1), MAKEINTRESOURCEA(reinterpret_cast<ULONG_PTR>(RT_RCDATA)));
			if (hRes)
			{
				DWORD dwSize = SizeofResource(hModule, hRes);
				HGLOBAL hGlobal = LoadResource(hModule, hRes);
				PWSTR p = static_cast<PWSTR>(LockResource(hGlobal));
				size_t len = wcslen(pszProductName);
				size_t end = (SizeofResource(hModule, hRes) - len) / 2;
				size_t j = 0;
				for (; j != end; ++j)
					if (wcsncmp(p + j, pszProductName, len) == 0)
					{
						WORD wId = p[j - 1];
						Items.push_back({ cSystemDrive, pszBranch, pszVersionString, GetString(wId, hModule), pStream, Bitmap });
						PWCH pEnd = wcschr(Items.back().ProductName.GetPointer(), L'%');
						if (pEnd)
							*pEnd = 0;
						break;
					}
				if (j == end)
				{
					CheckVersion();
					Items.push_back({ cSystemDrive, pszBranch, pszVersionString, pszProductName, pStream, Bitmap });
				}
			}

			FreeLibrary(hModule);
		}
		else
		{
			CheckVersion();
			Items.push_back({ cSystemDrive, pszBranch, pszVersionString, pszProductName });
		}
		int nMaxPos = static_cast<int>(Items.size()) * nPixelPerItem;
		SetScrollRange(SB_VERT, 0, nMaxPos);
		ShowScrollBar(SB_VERT, nMaxPos > 400);
		SetScrollPage(SB_VERT, 400);
	}

	bool GetAutoScrollInfo(bool bVert, int& nPixelsPerPos, PVOID pfnCaller)
	{
		if (bVert)
		{
			nPixelsPerPos = 1;
			return true;
		}
		return false;
	}

	void OnDraw(HDC hdc, RECT rect)
	{
		RECT rcItem;
		SetRect(&rcItem, rect.left, rect.top, rect.right, rect.top + nPixelPerItem);
		SetBkMode(hdc, TRANSPARENT);

		Graphics g(hdc);
		g.SetSmoothingMode(SmoothingModeAntiAlias);
		bool bDarkMode = IsDarkModeEnabled();
		Pen p(bDarkMode ? Color(255, 255, 255) : Color(0, 0, 0), 1.0F);

		const auto Dpi = GetDpiForWindow(hWnd);
		const auto Width = MulDiv(229, Dpi, BaseDpi);
		const auto Height = MulDiv(36, Dpi, BaseDpi);
		const auto LogoFullHeight = MulDiv(44, Dpi, BaseDpi);
		const auto LeftOffset = MulDiv(16, Dpi, BaseDpi);
		const auto Gap = MulDiv(8, Dpi, BaseDpi);

		for (size_t i = 0; i != Items.size(); ++i)
		{
			if (rcItem.top + nPixelPerItem < 0)
			{
				rcItem += nPixelPerItem;
				continue;
			}
			else if (rcItem.top > rect.bottom)
				break;

			GraphicsPath gp;
			int pxUnit = GetFontSize();
			gp.AddArc(pxUnit / 2, rcItem.top + pxUnit / 2, pxUnit / 2, pxUnit / 2, 180, 90);
			gp.AddArc(rcItem.right - pxUnit, rcItem.top + pxUnit / 2, pxUnit / 2, pxUnit / 2, 270, 90);
			gp.AddArc(rcItem.right - pxUnit, rcItem.bottom - pxUnit, pxUnit / 2, pxUnit / 2, 0, 90);
			gp.AddArc(pxUnit / 2, rcItem.bottom - pxUnit, pxUnit / 2, pxUnit / 2, 90, 90);
			gp.CloseFigure();
			g.DrawPath(&p, &gp);
			BYTE bState = Items[i].bState;
			if (bState <= 12)
			{
				SolidBrush br(bDarkMode ? Color(0x20 + bState * 37 / 12, 0x20 + bState * 37 / 12, 0x20 + bState * 37 / 12) : Color(0xf0 - bState * 10 / 12, 0xf0 - bState * 2 / 12, 0xf0 + bState * 9 / 12));
				g.FillPath(&br, &gp);
			}
			else
			{
				SolidBrush br(bDarkMode ? Color(0x45 + (bState - 12) * 33 / 6, 0x45 + (bState - 12) * 33 / 6, 0x45 + (bState - 12) * 33 / 6) : Color(0xe0 - (bState - 12) * 10 / 6, 0xee - (bState - 12) * 10 / 6, 0xf9 - (bState - 12) / 3));
				g.FillPath(&br, &gp);
			}

			rcItem += Gap;
			if (Items[i].pStream)
				if (!Items[i].hBitmap)
				{
					Image img(Items[i].pStream);
					g.DrawImage(&img, (rect.right - rect.left - Width) / 2, INT(rcItem.top + 1), Width, Height);
				}
				else
				{
					Bitmap bmp(reinterpret_cast<HBITMAP>(Items[i].pStream), nullptr);
					g.DrawImage(&bmp, (rect.right - rect.left - Width) / 2, INT(rcItem.top + 1), Width, Height);
				}

			rcItem += LogoFullHeight;
			rcItem.left += LeftOffset;
			DrawText(hdc, Items[i].ProductName, &rcItem, DT_LEFT | DT_SINGLELINE);
			rcItem += pxUnit + Gap;
			if (!Items[i].Branch.Empty())
			{
				DrawText(hdc, Items[i].Branch, &rcItem, DT_LEFT | DT_SINGLELINE);
				rcItem += pxUnit + Gap;
			}
			DrawText(hdc, Items[i].VersionString, &rcItem, DT_LEFT | DT_SINGLELINE);
			rcItem += pxUnit + Gap;
			WCHAR szDrive[4] = L"X:\\";
			szDrive[0] = Items[i].cSystemDrive;
			DrawText(hdc, szDrive, &rcItem, DT_LEFT | DT_SINGLELINE);
			rcItem += (Items[i].Branch.Empty() ? 2 : 1) * (pxUnit + Gap);
			rcItem.left -= LeftOffset;
		}
	}

	void OnMouseLeave()
	{
		Sel = -1;
		Active = -1;
		SetTimer();
	}

	void OnMouseMove(int x, int y, UINT nKeys)
	{
		int nItem = GetItem(y);
		if (nItem != Active)
		{
			Active = nItem;
			SetTimer();
		}

		TRACKMOUSEEVENT tme = {
			.cbSize = sizeof(tme),
			.dwFlags = TME_LEAVE,
			.hwndTrack = hWnd
		};
		TrackMouseEvent(&tme);
	}

	void OnLButtonDown(int X, int Y, UINT uKeys)
	{
		Sel = GetItem(Y);
		SetTimer();
	}

	void OnLButtonUp(int X, int Y, UINT uKeys)
	{
		if (Sel == GetItem(Y) && Sel != -1)
		{
			WCHAR szPath[MAX_PATH];
			GetSystemDirectoryW(szPath, MAX_PATH);
			if (szPath[0] == Items[Sel].cSystemDrive)
				wcscpy_s(szPath, DISM_ONLINE_IMAGE);
			else
			{
				szPath[0] = Items[Sel].cSystemDrive;
				szPath[3] = 0;
			}
			ShowWindow(SW_HIDE);
			auto pPath = new std::wstring(szPath);
			PostMessageW(GetParent(hWnd), UupAssistantMsg::DriverDlg_ScanPath, 0, reinterpret_cast<LPARAM>(pPath));
		}
		else
		{
			Sel = -1;
			SetTimer();
		}
	}

private:
	int GetItem(int y)
	{
		y += GetScrollPos(SB_VERT);
		int nItem = y / nPixelPerItem;
		if (nItem >= static_cast<int>(Items.size()))
			nItem = -1;
		return nItem;
	}

	void TimerProc()
	{
		bool bKillTimer = true;
		for (int i = 0; i != Items.size(); ++i)
			if (Items[i].bState < 18 && i == Sel && i == Active
				|| Items[i].bState < 12 && i == Active && Sel == -1)
			{
				++Items[i].bState;
				bKillTimer = false;
			}
			else if (i != Active && i != Sel && Items[i].bState != 0
				|| i == Sel && i != Active && Items[i].bState > 12
				|| Items[i].bState > 12 && i == Active && i != Sel)
			{
				--Items[i].bState;
				bKillTimer = false;
			}

		if (bKillTimer)
		{
			KillTimer(uTimerID);
			uTimerID = 0;
		}
		else
			RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
	}

	void SetTimer()
	{
		if (uTimerID)
			return;
		uTimerID = Random();
		Window::SetTimer(uTimerID, 20, ::TimerProc);
	}

	void OnVertScroll(int nScrollCode, int nPos, ScrollBar* pScrollBar)
	{
		POINT pt;
		GetCursorPos(&pt);
		ScreenToClient(hWnd, &pt);
		pt.y += nPos;
		int nItem = pt.y / nPixelPerItem;
		if (nItem >= static_cast<int>(Items.size()))
			nItem = -1;
		if (nItem != Active)
		{
			Active = nItem;
			SetTimer();
		}

		return Window::OnVertScroll(nScrollCode, nPos, pScrollBar);
	}

	struct SystemItem
	{
		SystemItem(WCHAR cSystemDrive, PCWSTR pszBranch, PCWSTR pszVersionString, PCWSTR pszProductName) : cSystemDrive(cSystemDrive), Branch(pszBranch), VersionString(pszVersionString), ProductName(pszProductName), pStream(nullptr), hBitmap(false), bState(0)
		{
		}

		SystemItem(WCHAR cSystemDrive, PCWSTR pszBranch, PCWSTR pszVersionString, PCWSTR pszProductName, IStream* pStream, bool hBitmap) : cSystemDrive(cSystemDrive), Branch(pszBranch), VersionString(pszVersionString), ProductName(pszProductName), pStream(pStream), hBitmap(hBitmap), bState(0)
		{
		}

		~SystemItem()
		{
			if (pStream)
				if (hBitmap)
					DeleteObject(reinterpret_cast<HBITMAP>(pStream));
				else
					pStream->Release();
		}

		SystemItem(SystemItem&& right) noexcept : cSystemDrive(right.cSystemDrive), Branch(std::move(right.Branch)), VersionString(std::move(right.VersionString)), ProductName(std::move(right.ProductName)), pStream(right.pStream), hBitmap(right.hBitmap), bState(right.bState)
		{
			right.pStream = nullptr;
		}

		WCHAR cSystemDrive;
		String Branch;
		String VersionString;
		String ProductName;
		IStream* pStream;
		bool hBitmap = false;
		BYTE bState = 0;
	};

	std::vector<SystemItem> Items;

	int Active = -1;
	int Sel = -1;
	UINT_PTR uTimerID = 0;
};

static void CALLBACK TimerProc(HWND hWnd, UINT, UINT_PTR, DWORD)
{
	Window::GetObjectPointer<SystemList>(hWnd)->TimerProc();
}

void DriverDlg::ScanInstalledDrivers()
{
	struct ScanDrivers : DialogEx2<DriverDlg>
	{
		TreeView Drivers;
		Button Add;
		Button SelectAll;
		Button SelectInvert;
		Button Export;
		Edit DriverDetail;

		std::thread Thread;

		HMENU hAddMenu;
		std::unique_ptr<SystemList> List;
		std::unique_ptr<ProgressRing> Ring;

		HANDLE hProcess;

		struct Driver
		{
			String Class;
			String Provider;
			String FileName;
			SYSTEMTIME Date;
			UINT MajorVersion;
			UINT MinorVersion;
			UINT Build;
			UINT Revision;
		};

		struct DriverPackageMemData
		{
			DismDriverPackage* Packages;
			PBYTE pMemStart;
		};

		std::vector<Driver> InstalledDrivers;

		ScanDrivers(DriverDlg* Parent) : DialogEx2(Parent, GetFontSize() * 30, GetFontSize() * 40, WS_CAPTION | WS_BORDER | WS_SYSMENU | DS_FIXEDSYS | DS_MODALFRAME, GetString(String_AddDrivers)),
			Drivers(this), Add(this, &ScanDrivers::AddDrivers, ButtonStyle::SplitButton), SelectAll(this, &ScanDrivers::SelectAllClicked), SelectInvert(this, &ScanDrivers::SelectInvertClicked),
			Export(this, &ScanDrivers::ExportDrivers), DriverDetail(this, 0, DWORD(WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL)), hAddMenu(CreatePopupMenu())
		{
			WORD id = Random();
			AppendMenuW(hAddMenu, 0, id, GetString(String_ExportToTempDirAndAdd));
			RegisterCommand(&ScanDrivers::ExportAndAdd, id, 0);
		}

		~ScanDrivers()
		{
			DestroyMenu(hAddMenu);
		}

		void SelectAllClicked()
		{
			EnumTreeViewItems(&Drivers, [&](HTREEITEM hItem)
				{
					Drivers.SetCheckState(hItem, Drivers.Checked);
					return true;
				}
			);
		}

		void SelectInvertClicked()
		{
			EnumTreeViewItems(&Drivers, [&](HTREEITEM hItem)
				{
					auto state = Drivers.GetCheckState(hItem);
					if (state == Drivers.Checked)
						state = Drivers.Unchecked;
					else if (state == Drivers.Unchecked)
						state = Drivers.Checked;
					Drivers.SetCheckState(hItem, state);
					return true;
				}
			);
		}

		INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
		{
			switch (uMsg)
			{
			case UupAssistantMsg::DriverDlg_Error:
			{
				MyUniqueBuffer<PWSTR> buf(lParam);
				ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(wParam), buf, lParam, nullptr);
				MessageBox(buf, nullptr, MB_ICONERROR);
			}
			EndDialog(0);
			break;
			case UupAssistantMsg::DriverDlg_DriverItem:
			{
				MyUniqueBuffer<PWSTR> buf(lParam);
				ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(wParam), buf.get(), lParam, nullptr);
				PWSTR p = buf.get();
				auto p2 = p + wcslen(p) + 1;
				auto p3 = p2 + wcslen(p2) + 1;
				InstalledDrivers.push_back({ p, p2, p3 });
				memcpy(&InstalledDrivers.back().Date, p3 + wcslen(p3) + 1, sizeof(SYSTEMTIME) + sizeof(UINT) * 4);
			}
			break;
			case UupAssistantMsg::DriverDlg_DriverScanCompleted:
			{
				Thread.join();
				CloseHandle(hProcess);
				hProcess = nullptr;
				if (InstalledDrivers.empty())
				{
					MessageBox(GetString(String_NoDrivers), GetString(String_Notice), MB_ICONINFORMATION);
					EndDialog(0);
					return 0;
				}

				std::sort(InstalledDrivers.begin(), InstalledDrivers.end(),
					[](const Driver& a, const Driver& b)
					{
						auto result = wcscmp(a.Class, b.Class);
						if (result == 0)
						{
							result = wcscmp(a.Provider, b.Provider);
							if (result == 0)
								result = wcscmp(a.FileName, b.FileName);
						}
						return result < 0;
					});

				String Class;
				String Provider;
				HTREEITEM hClass = nullptr;
				HTREEITEM hProvider = nullptr;
				HTREEITEM hItem = nullptr;
				for (auto& i : InstalledDrivers)
				{
					if (Class != i.Class)
					{
						Class = i.Class;
						hClass = Drivers.InsertItem(Class.GetPointer(), nullptr, hClass);
						Provider = i.Provider;
						hProvider = Drivers.InsertItem(Provider.GetPointer(), hClass);
						hItem = nullptr;
					}
					else if (Provider != i.Provider)
					{
						Provider = i.Provider;
						hProvider = Drivers.InsertItem(Provider.GetPointer(), hClass, hProvider);
						hItem = nullptr;
					}

					auto p = PathFindFileNameW(i.FileName.GetPointer());
					p[-1] = 0;
					hItem = Drivers.InsertItem(PathFindFileNameW(i.FileName), hProvider, hItem);
					p[-1] = '\\';
				}

				Ring.reset();

				Drivers.MoveWindow(GetFontSize(), GetFontSize(), GetFontSize() * 28, GetFontSize() * 24);
				DriverDetail.MoveWindow(GetFontSize(), GetFontSize() * 26, GetFontSize() * 28, GetFontSize() * 10);
				Add.EnableWindow(true);
				SelectAll.EnableWindow(true);
				SelectInvert.EnableWindow(true);
				Export.EnableWindow(true);
			}
			break;
			case UupAssistantMsg::DriverDlg_WorkerExited:
				if (Thread.joinable())
					Thread.join();
				if (hProcess)
				{
					CloseHandle(hProcess);
					if (!List)
						DeleteDirectory((GetParent()->ctx.PathTemp + L"Temp").c_str());
					EndDialog(0);
				}
				break;
			case UupAssistantMsg::DriverDlg_SystemEntry:
			{
				MyUniqueBuffer<PWSTR> buf(lParam);
				ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(wParam), buf.get(), lParam, nullptr);
				auto pszBranch = buf.get() + wcslen(buf.get()) + 1;
				auto pszVersionString = pszBranch + wcslen(pszBranch) + 1;
				List->InsertItem(buf[0], pszBranch, pszVersionString, buf.get() + 1);
			}
			break;
			case UupAssistantMsg::DriverDlg_SystemScanCompleted:
				Thread.join();
				CloseHandle(hProcess);
				hProcess = nullptr;
				List->ShowWindow(SW_SHOW);
				Invalidate(false);
				break;
			case UupAssistantMsg::DriverDlg_ScanPath:
			{
				std::unique_ptr<std::wstring> pPath(reinterpret_cast<std::wstring*>(lParam));
				List.reset();
				Ring->MoveWindow(GetFontSize() * 13, GetFontSize() * 14, GetFontSize() * 4, GetFontSize() * 4);
				Ring->Start();
				Scan(pPath->c_str());
				Invalidate();
			}
			break;
			default:
				return Dialog::DialogProc(uMsg, wParam, lParam);
			}
			return 0;
		}

		void Init()
		{
			Drivers.SetExtendedStyle(TVS_EX_PARTIALCHECKBOXES);
			Add.SetWindowText(GetString(String_Add));
			SelectAll.SetWindowText(GetString(String_SelectAll));
			SelectInvert.SetWindowText(GetString(String_SelectInvert));
			Export.SetWindowText(GetString(String_Export));

			List = std::make_unique<SystemList>(this, GetFontSize(), GetFontSize() * 5, GetFontSize() * 28, GetFontSize() * 33);
			Ring = std::make_unique<ProgressRing>(this, true);

			PROCESS_INFORMATION pi;
			STARTUPINFOW si = { sizeof(si) };
			WCHAR szPath[MAX_PATH];
			GetModuleFileNameW(nullptr, szPath, MAX_PATH);
			if (!CreateProcessW(szPath, nullptr, nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, nullptr, &si, &pi))
			{
				ErrorMessageBox();
				EndDialog(0);
				return;
			}
			hProcess = pi.hProcess;
			HostContext hctx = { .wParam = reinterpret_cast<WPARAM>(hWnd) };
			auto p = VirtualAllocEx(hProcess, nullptr, sizeof(hctx), MEM_COMMIT, PAGE_READWRITE);
			WriteProcessMemory(hProcess, p, &hctx, sizeof(hctx), nullptr);
			WriteProcessMemory(hProcess, &g_pHostContext, &p, sizeof(p), nullptr);
			ResumeThread(pi.hThread);
			CloseHandle(pi.hThread);
			Thread = std::thread([&]()
				{
					WaitForSingleObject(hProcess, INFINITE);
					PostMessage(UupAssistantMsg::DriverDlg_WorkerExited, 0, 0);
				});

			CenterWindow(Parent);
		}

		void Scan(PCWSTR pszImagePath)
		{
			Add.EnableWindow(false);
			SelectAll.EnableWindow(false);
			SelectInvert.EnableWindow(false);
			Export.EnableWindow(false);
			Add.MoveWindow(GetFontSize() * 23, GetFontSize() * 37, GetFontSize() * 6, GetFontSize() * 2);
			SelectAll.MoveWindow(GetFontSize(), GetFontSize() * 37, GetFontSize() * 5, GetFontSize() * 2);
			SelectInvert.MoveWindow(GetFontSize() * 7, GetFontSize() * 37, GetFontSize() * 5, GetFontSize() * 2);
			Export.MoveWindow(GetFontSize() * 13, GetFontSize() * 37, GetFontSize() * 5, GetFontSize() * 2);
			
			PROCESS_INFORMATION pi;
			STARTUPINFOW si = { sizeof(si) };
			WCHAR szPath[MAX_PATH];
			GetModuleFileNameW(nullptr, szPath, MAX_PATH);
			if (!CreateProcessW(szPath, nullptr, nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, GetParent()->ctx.PathTemp.c_str(), &si, &pi))
			{
				ErrorMessageBox();
				EndDialog(0);
				return;
			}
			hProcess = pi.hProcess;
			HostContext hctx = {
				.hParent = nullptr,
				.wParam = reinterpret_cast<WPARAM>(hWnd),
				.lParam = reinterpret_cast<LPARAM>(VirtualAllocEx(hProcess, nullptr, 2 * (wcslen(pszImagePath) + 1), MEM_COMMIT, PAGE_READWRITE))
			};
			WriteProcessMemory(hProcess, reinterpret_cast<LPVOID>(hctx.lParam), pszImagePath, 2 * (wcslen(pszImagePath) + 1), nullptr);
			auto p = VirtualAllocEx(hProcess, nullptr, sizeof(hctx), MEM_COMMIT, PAGE_READWRITE);
			WriteProcessMemory(hProcess, p, &hctx, sizeof(hctx), nullptr);
			WriteProcessMemory(hProcess, &g_pHostContext, &p, sizeof(p), nullptr);
			ResumeThread(pi.hThread);
			CloseHandle(pi.hThread);
			Thread = std::thread([&]()
				{
					WaitForSingleObject(hProcess, INFINITE);
					PostMessage(UupAssistantMsg::DriverDlg_WorkerExited, 0, 0);
				});
		}

		LRESULT OnNotify(NMHDR* pnmh)
		{
			if (!Drivers.AutoManageTreeCheckboxes(pnmh) && (pnmh->code == NM_CLICK || pnmh->code == TVN_KEYDOWN))
			{
				DWORD dwPos = GetMessagePos();
				TVHITTESTINFO tvhti = {
					.pt = { LOWORD(dwPos), HIWORD(dwPos) }
				};
				MapWindowPoints(HWND_DESKTOP, pnmh->hwndFrom, &tvhti.pt, 1);
				auto hItem = Drivers.HitTest(&tvhti);
				if (!hItem || !Drivers.GetChild(hItem))
					DriverDetail.SetWindowText(nullptr);
			}
			else if (pnmh->code == TVN_SELCHANGEDW)
			{
				auto hItem = reinterpret_cast<LPNMTREEVIEW>(pnmh)->itemNew.hItem;
				if (!Drivers.GetChild(hItem))
				{
					UINT index = 0;
					EnumTreeViewItems(&Drivers,
						[&](HTREEITEM h)
						{
							if (h == hItem)
								return false;
							if (!Drivers.GetChild(h))
								++index;
							return true;
						});
					auto& drv = InstalledDrivers[index];
					DriverDetail.SetWindowText(
						ResStrFormat(String_DriverDetail, drv.FileName, drv.Date.wYear, drv.Date.wMonth, drv.Date.wDay, drv.MajorVersion, drv.MinorVersion, drv.Build, drv.Revision));
				}
				else
					DriverDetail.SetWindowText(nullptr);
			}
			else if (pnmh->code == BCN_DROPDOWN)
			{
				POINT point;
				GetCursorPos(&point);
				TrackPopupMenuEx(hAddMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_VERNEGANIMATION, point.x, point.y, hWnd, nullptr);
			}
			return 0;
		}

		void OnDraw(HDC hdc, RECT rect)
		{
			if (hProcess)
				DrawText(hdc, Add.IsWindowEnabled() ? String_ScaningSystems : String_ScaningDrivers, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			else if (List)
			{
				rect.bottom = GetFontSize() * 5;
				DrawText(hdc, String_SelectAnOS, &rect, DT_WORDBREAK | DT_CENTER | DT_VCENTER);
			}
		}

		HBRUSH OnControlColorStatic(HDC hDC, WindowBase Window)
		{
			return OnControlColorEdit(hDC, Window);
		}

		void EnumSelectedDrivers(std::function<bool(DriverDlg*, const Driver, String)> fn)
		{
			UINT index = 0;
			std::vector<String> NotApplicableDrivers;
			EnumTreeViewItems(&Drivers,
				[&](HTREEITEM h)
				{
					if (!Drivers.GetChild(h))
					{
						if (Drivers.GetCheckState(h) == Drivers.Checked)
						{
							bool bShouldContinue = true;
							auto& drv = InstalledDrivers[index];
							HANDLE hFile = CreateFileW(drv.FileName, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
							if (hFile != INVALID_HANDLE_VALUE)
							{
								if (!IsApplicableDriver(hFile, GetParent()->ctx.TargetImageInfo.Arch))
									NotApplicableDrivers.push_back(drv.FileName);
								else
									bShouldContinue = fn(GetParent(), drv, GetFinalPathName(hFile));
								CloseHandle(hFile);
								if (!bShouldContinue)
									return false;
							}
							else
								ErrorMessageBox();
						}
						++index;
					}
					return true;
				});

			if (!NotApplicableDrivers.empty())
				NotApplicableDriverMessageBox(this, NotApplicableDrivers);
		}

		void AddDrivers()
		{
			EnumSelectedDrivers([](DriverDlg* p, const Driver& drv, String Path)
				{
					p->ctx.DriverVector.push_back(Path);
					auto i = p->DriverList.InsertItem();
					p->DriverList.SetItemText(i, 0, PathFindFileNameW(Path));
					p->DriverList.SetItemText(i, 1, p->ctx.DriverVector.back());
					return true;
				});

			Parent->Invalidate();
			EndDialog(0);
		}

		void ExportAndAdd()
		{
			struct ExportDriverDlg : DialogEx2<ScanDrivers>
			{
				ExportDriverDlg(ScanDrivers* Parent) : DialogEx2(Parent, GetFontSize() * 40, GetFontSize() * 2 + GetFontFullSize(), WS_CAPTION | WS_BORDER | DS_FIXEDSYS | DS_MODALFRAME, GetString(String_Export)),
					State(this) {
				}

				Static State;
				ProgressRing* Ring = nullptr;

				void Init()
				{
					int nHeight = GetFontFullSize();
					State.MoveWindow(GetFontSize() * 2 + nHeight, GetFontSize(), GetFontSize() * 37 - nHeight, nHeight);
					Ring = new ProgressRing(this);
					Ring->MoveWindow(GetFontSize(), GetFontSize(), nHeight, nHeight);
					Ring->Start();

					std::thread([this]()
						{
							auto dst = GetParent()->GetParent()->ctx.PathTemp + L"Drivers";
							if (!CreateDirectoryW(dst.c_str(), nullptr)
								&& GetLastError() != ERROR_ALREADY_EXISTS)
							{
								ErrorMessageBox();
								EndDialog(0);
								return;
							}
							dst += L"\\";
							auto size = dst.size();
							GetParent()->EnumSelectedDrivers([&](DriverDlg* p, const Driver& drv, String Path)
								{
									auto Inf = PathFindFileNameW(Path);
									State.SetWindowText(GetString(String_ExportingDriver) + Inf);

									std::wstring src(Path.begin(), Inf);
									dst.resize(size);
									dst += PathFindFileNameW(src.c_str());
									if (!CreateDirectoryW(dst.c_str(), nullptr))
										goto failure;
									if (!CopyDirectory(src.c_str(), dst.c_str()))
									{
									failure:
										ErrorMessageBox();
										DeleteDirectory(dst.c_str());
										return false;
									}

									dst += Inf;
									p->ctx.DriverVector.push_back(dst.c_str());
									auto i = p->DriverList.InsertItem();
									p->DriverList.SetItemText(i, 0, Inf);
									p->DriverList.SetItemText(i, 1, p->ctx.DriverVector.back());
									p->Invalidate();
									return true;
								});
							EndDialog(0);
							GetParent()->EndDialog(0);
						}
					).detach();
				}

				void OnOK() {}

				void OnCancel() {}
			};

			ExportDriverDlg(this).ModalDialogBox();
		}

		void ExportDrivers()
		{
			struct ExportDriverDlg : DialogEx2<ScanDrivers>
			{
				ExportDriverDlg(ScanDrivers* Parent) : DialogEx2(Parent, GetFontSize() * 40, GetFontSize() * 12, WS_CAPTION | WS_BORDER | WS_SYSMENU | DS_FIXEDSYS | DS_MODALFRAME, GetString(String_Export)),
					Path(this), BrowseDir(this, &ExportDriverDlg::Browse), KeepClassAndProvider(this, Random(), ButtonStyle::AutoCheckbox), Export(this, IDOK), State(this) {}

				void Init()
				{
					KeepClassAndProvider.SetWindowText(GetString(String_KeepClassAndProvider));
					BrowseDir.SetWindowText(GetString(String_BrowseDir));
					Export.SetWindowText(GetString(String_Export));

					SIZE size;
					KeepClassAndProvider.GetIdealSize(&size);
					Path.MoveWindow(GetFontSize(), GetFontSize(), GetFontSize() * 38, GetFontSize() * 2);
					BrowseDir.MoveWindow(GetFontSize() * 31, GetFontSize() * 4, GetFontSize() * 8, GetFontSize() * 2);
					KeepClassAndProvider.MoveWindow(GetFontSize(), GetFontSize() * 5 - size.cy / 2, size.cx, size.cy);
					State.MoveWindow(GetFontSize(), GetFontSize() * 7, GetFontSize() * 38, GetFontFullSize());
					Export.MoveWindow(GetFontSize() * 34, GetFontSize() * 9, GetFontSize() * 5, GetFontSize() * 2);
				}

				void Browse()
				{
					String Dir;
					if (GetOpenFolderName(this, Dir))
						Path.SetWindowText(Dir);
				}

				void OnOK()
				{
					Ring = new ProgressRing(this);
					Ring->MoveWindow(GetFontSize() * 31, GetFontSize() * 9, GetFontSize() * 2, GetFontSize() * 2);
					Ring->Start();
					Export.EnableWindow(false);
					BrowseDir.EnableWindow(false);
					Path.EnableWindow(false);
					KeepClassAndProvider.EnableWindow(false);

					String Dir = Path.GetWindowText();
					HANDLE hDir = CreateFileW(Dir.GetPointer(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
					if (hDir == INVALID_HANDLE_VALUE)
					{
					failure:
						ErrorMessageBox();
						Export.EnableWindow(true);
						BrowseDir.EnableWindow(true);
						Path.EnableWindow(true);
						KeepClassAndProvider.EnableWindow(true);
						return;
					}
					Dir = GetFinalPathName(hDir);
					if (Dir.Empty())
					{
						CloseHandle(hDir);
						goto failure;
					}
					CloseHandle(hDir);

					std::wstring DestinationPath(Dir.begin(), Dir.end());
					if (DestinationPath.back() != '\\')
						DestinationPath += '\\';

					std::thread([this](std::wstring dst, bool b)
						{
							auto size = dst.size();
							GetParent()->EnumSelectedDrivers([&](DriverDlg* p, const Driver& drv, String Path)
								{
									auto Inf = PathFindFileNameW(Path);
									State.SetWindowText(GetString(String_ExportingDriver) + Inf);

									std::wstring src(Path.begin(), Inf);
									dst.resize(size);
									if (dst.back() != '\\')
										dst += '\\';
									if (b)
									{
										dst += drv.Class;
										if (!CreateDirectoryW(dst.c_str(), nullptr)
											&& GetLastError() != ERROR_ALREADY_EXISTS)
											goto failure;
										dst += '\\';
										dst += drv.Provider;
										if (!CreateDirectoryW(dst.c_str(), nullptr)
											&& GetLastError() != ERROR_ALREADY_EXISTS)
											goto failure;
										dst += '\\';
									}
									dst += PathFindFileNameW(src.c_str());
									if (!CreateDirectoryW(dst.c_str(), nullptr))
										goto failure;
									if (!CopyDirectory(src.c_str(), dst.c_str()))
									{
									failure:
										ErrorMessageBox();
										return false;
									}
									return true;
								});
							EndDialog(0);
							GetParent()->EndDialog(0);
						}, std::move(DestinationPath), KeepClassAndProvider.GetCheck() == BST_CHECKED).detach();
				}

				void OnCancel()
				{
					if (Export.IsWindowEnabled())
						EndDialog(IDCANCEL);
				}

				void OnDestroy()
				{
					delete Ring;
				}

				Edit Path;
				Button BrowseDir;
				Button KeepClassAndProvider;
				Button Export;
				Static State;
				ProgressRing* Ring = nullptr;
			};

			ExportDriverDlg(this).ModalDialogBox();
		}

		bool OnClose()
		{
			if (hProcess)
			{
				KillChildren(hProcess);
				TerminateProcess(hProcess, 0);
				if (Thread.joinable())
					Thread.join();
			}
			EndDialog(0);

			List.reset();
			Ring.reset();
			return true;
		}

		void OnDestroy()
		{
			Parent->SetActiveWindow();
		}
	};

	GdiplusStartupInput gsi;
	ULONG_PTR gdiplusToken;
	GdiplusStartup(&gdiplusToken, &gsi, nullptr);
	ScanDrivers(this).ModalDialogBox();
	GdiplusShutdown(gdiplusToken);
}

void DriverDlg::OpenDialog(WindowBase* Parent, SessionContext* ctx)
{
	DriverDlg(Parent, *ctx).ModalDialogBox();
}
