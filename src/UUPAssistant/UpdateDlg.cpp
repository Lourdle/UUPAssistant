#include "pch.h"
#include "misc.h"
#include "UUPAssistant.h"
#include "Resources/resource.h"

#include <Shlwapi.h>

#include <string>

using namespace std;
using namespace Lourdle::UIFramework;

struct SetPSFDlg : Dialog
{
	SetPSFDlg(UpdateDlg* Parent);
	~SetPSFDlg();
	Edit Path;
	Button Okay;
	Button Browse;
	String File;
	IFileOpenDialog* pFileOpenDialog;

	void Init(LPARAM);
	void OnOK();
	void BrowseFile();
};

SetPSFDlg::SetPSFDlg(UpdateDlg* Parent) :
	Dialog(Parent, GetFontSize(nullptr) * 40, GetFontSize(nullptr) * 10, WS_BORDER | WS_SYSMENU | WS_CAPTION | DS_MODALFRAME | DS_FIXEDSYS, GetString(String_SetPSF)),
	Okay(this, IDOK, ButtonStyle::Text),
	Browse(this, &SetPSFDlg::BrowseFile, ButtonStyle::Text),
	Path(this, 0)
{
	pFileOpenDialog = CreateFileOpenDialogInstance();
	COMDLG_FILTERSPEC cf = { L"PSF Files", L"*.psf" };
	pFileOpenDialog->SetFileTypes(1, &cf);
	DWORD dw;
	pFileOpenDialog->GetOptions(&dw);
	pFileOpenDialog->SetOptions(dw | FOS_NODEREFERENCELINKS);
}

SetPSFDlg::~SetPSFDlg()
{
	pFileOpenDialog->Release();
}

void SetPSFDlg::Init(LPARAM lParam)
{
	CenterWindow(Parent);
	int pxUnit = GetFontSize(nullptr);

	Browse.SetWindowText(GetString(String_Browse));
	Okay.SetWindowText(GetString(String_Okay));

	Path.MoveWindow(pxUnit, pxUnit, pxUnit * 38, pxUnit * 2);
	Path.SetWindowText(reinterpret_cast<LPCWSTR>(lParam));
	Browse.MoveWindow(pxUnit * 33, pxUnit * 4, pxUnit * 6, pxUnit * 2);
	Okay.MoveWindow(pxUnit * 17, pxUnit * 7, pxUnit * 6, pxUnit * 2);
}


void SetPSFDlg::OnOK()
{
	String* str = new String(Path.GetWindowText());
	if (str->Empty())
	{
		EndDialog(reinterpret_cast<INT_PTR>(str));
		return;
	}
	HANDLE hFile = CreateFileW(str->GetPointer(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		ErrorMessageBox();
		delete str;
		return;
	}
	if (GetFileAttributesByHandle(hFile) & FILE_ATTRIBUTE_DIRECTORY)
	{
		SetLastError(ERROR_DIRECTORY_NOT_SUPPORTED);
		delete str;
		return;
	}
	str->operator=(GetFinalPathName(hFile));
	CloseHandle(hFile);
	EndDialog(reinterpret_cast<INT_PTR>(str));
}

void SetPSFDlg::BrowseFile()
{
	if (FAILED(pFileOpenDialog->Show(hWnd)))
		return;
	IShellItem* si;
	pFileOpenDialog->GetResult(&si);
	PWSTR name;
	si->GetDisplayName(SIGDN_DESKTOPABSOLUTEEDITING, &name);
	Path.SetWindowText(name);
	CoTaskMemFree(name);
	si->Release();
	return;
}

UpdateDlg::UpdateDlg(WindowBase* Parent, SessionContext& ctx) : DialogEx2(Parent, GetFontSize(nullptr) * 45, GetFontSize(nullptr) * 50, WS_CAPTION | WS_SYSMENU | WS_BORDER | DS_MODALFRAME | DS_FIXEDSYS, GetString(String_InstallUpdates)),
UpdateList(this, 0, WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_SINGLESEL), Remove(this, &UpdateDlg::RemoveItem, ButtonStyle::Text),
Path(this, 0), hItemMenu(CreatePopupMenu()), hRemoveMenu(CreatePopupMenu()), FinalPath(this, 0, DWORD(WS_VISIBLE | WS_CHILD | ES_MULTILINE)),
Browse(this, &UpdateDlg::BrowseFiles, ButtonStyle::Text), BrowseDir(this, &UpdateDlg::BrowseDirectory, ButtonStyle::Text),
Add(this, &UpdateDlg::AddItem, ButtonStyle::Text), ViewOtherUpdates(this, &UpdateDlg::OpenOtherUpdateDlg, ButtonStyle::Text), CleanComponents(this, &UpdateDlg::SwitchCleanComponents, ButtonStyle::Checkbox),
ctx(ctx)
{
	WORD id = Random();
	AppendMenuW(hItemMenu, MF_STRING, id, GetString(String_SetPSF));
	RegisterCommand(&UpdateDlg::SetUpdatePSF, id, 0);
	id = Random();
	AppendMenuW(hItemMenu, MF_STRING, id, GetString(String_RemoveThis));
	AppendMenuW(hRemoveMenu, MF_STRING, id, GetString(String_RemoveThis));
	RegisterCommand(&UpdateDlg::RemoveIt, id, 0);
	id = Random();
	AppendMenuW(hRemoveMenu, MF_STRING, id, GetString(String_RemoveSelected));
	RegisterCommand(&UpdateDlg::RemoveSelectedItems, id, 0);
	id = Random();
	AppendMenuW(hRemoveMenu, MF_STRING, id, GetString(String_RemoveAll));
	RegisterCommand(&UpdateDlg::RemoveAll, id, 0);

	pFileOpenDialog = CreateFileOpenDialogInstance();
	auto type = GetString(String_UpdateFile);
	COMDLG_FILTERSPEC cf = { type, L"*.cab;*.msu;*.wim" };
	pFileOpenDialog->SetFileTypes(1, &cf);
	DWORD dw;
	pFileOpenDialog->GetOptions(&dw);
	pFileOpenDialog->SetOptions(dw | FOS_ALLOWMULTISELECT | FOS_NOCHANGEDIR | FOS_NODEREFERENCELINKS);
}

UpdateDlg::~UpdateDlg()
{
	DestroyMenu(hRemoveMenu);
	DestroyMenu(hItemMenu);

	pFileOpenDialog->Release();
}

void UpdateDlg::Init()
{
	CenterWindow(Parent);
	int pxUnit = GetFontSize();

	UpdateList.InsertColumn(GetString(String_File).GetPointer(), pxUnit * 43 - 17, 0);
	UpdateList.AddExtendedListViewStyle(LVS_EX_CHECKBOXES);
	Remove.SetWindowText(GetString(String_Remove));
	Browse.SetWindowText(GetString(String_Browse));
	BrowseDir.SetWindowText(GetString(String_BrowseDir));
	Add.SetWindowText(GetString(String_Add));
	ViewOtherUpdates.SetWindowText(GetString(String_ViewOtherUpdates));
	CleanComponents.SetWindowText(GetString(String_CleanComponents));

	UpdateList.MoveWindow(pxUnit, pxUnit, pxUnit * 43, pxUnit * 20);
	Remove.MoveWindow(pxUnit, pxUnit * 22, pxUnit * 5, pxUnit * 2);

	FinalPath.MoveWindow(pxUnit, pxUnit * 25, pxUnit * 33, pxUnit * 9);
	Path.MoveWindow(pxUnit * 1, pxUnit * 44, pxUnit * 43, pxUnit * 2);
	Browse.MoveWindow(pxUnit * 31, pxUnit * 47, pxUnit * 5, pxUnit * 2);
	BrowseDir.MoveWindow(pxUnit * 37, pxUnit * 47, pxUnit * 7, pxUnit * 2);
	Add.MoveWindow(pxUnit * 1, pxUnit * 47, pxUnit * 5, pxUnit * 2);
	if (!ctx.SetupUpdate.Empty() || !ctx.SafeOSUpdate.Empty())
		ViewOtherUpdates.MoveWindow(pxUnit * 7, pxUnit * 22, pxUnit * 8, pxUnit * 2);

	SIZE size;
	CleanComponents.GetIdealSize(&size);
	CleanComponents.MoveWindow(pxUnit * 44 - size.cx, pxUnit * 23 - size.cy / 2, size.cx, size.cy);

	for (int i = 0; i != ctx.UpdateVector.size(); ++i)
	{
		UpdateList.InsertItem();
		PWSTR file = ctx.UpdateVector[i].UpdateFile.GetPointer();
		for (file += ctx.UpdateVector[i].UpdateFile.GetLength(); *file != '\\'; --file)
			if (file == ctx.UpdateVector[i].UpdateFile.GetPointer())
			{
				--file;
				break;
			}
		UpdateList.SetItemText(i, 0, ++file);
	}

	CleanComponents.SetCheck(ctx.bCleanComponentStore ? BST_CHECKED : BST_UNCHECKED);
}

void UpdateDlg::OnDraw(HDC hdc, RECT rect)
{
	rect.top = GetFontSize(nullptr) * 35;
	rect.left = rect.top / 35;
	rect.right -= rect.left;
	rect.bottom = rect.left * 43;
	DrawText(hdc, ResStrFormat(String_AddUpdates, ctx.UpdateVector.size()), -1, &rect, DT_WORDBREAK | DT_BOTTOM);
}

LRESULT UpdateDlg::OnNotify(LPNMHDR pnmhdr)
{
	if (pnmhdr->hwndFrom == UpdateList.GetHandle() && pnmhdr->code == NM_RCLICK)
	{
		int sel = reinterpret_cast<NMITEMACTIVATE*>(pnmhdr)->iItem;
		MENUITEMINFOW mii = {
			.cbSize = sizeof(mii),
			.fMask = MIIM_STATE,
			.fState = UINT(sel == CTL_ERR ? MFS_DISABLED : MFS_ENABLED)
		};
		SetMenuItemInfoW(hItemMenu, 1, TRUE, &mii);
		if (mii.fState == MFS_ENABLED && !PathMatchSpecW(ctx.UpdateVector[sel].UpdateFile, L"*.cab") && !PathMatchSpecW(ctx.UpdateVector[sel].UpdateFile, L"*.wim"))
			mii.fState = MFS_DISABLED;
		SetMenuItemInfoW(hItemMenu, 0, TRUE, &mii);
		POINT point;
		GetCursorPos(&point);
		TrackPopupMenuEx(hItemMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_VERNEGANIMATION, point.x, point.y, hWnd, nullptr);
	}
	else if (pnmhdr->hwndFrom == UpdateList.GetHandle() && pnmhdr->code == LVN_ITEMCHANGED)
	{
		int sel = reinterpret_cast<LPNMLISTVIEW>(pnmhdr)->iItem;
		if (reinterpret_cast<LPNMLISTVIEW>(pnmhdr)->uNewState & LVIS_SELECTED)
			if (sel == CTL_ERR)
				FinalPath.SetWindowText(nullptr);
			else
				FinalPath.SetWindowText(
					ResStrFormat(String_UpdatePathDetail, ctx.UpdateVector[sel].UpdateFile.GetPointer(), ctx.UpdateVector[sel].PSF.Empty() ? nullptr : ctx.UpdateVector[sel].PSF.GetPointer()));
	}

	return 0;
}

void UpdateDlg::RemoveItem()
{
	int sel = UpdateList.GetSelectionMark();
	MENUITEMINFOW mii = {
		.cbSize = sizeof(mii),
		.fMask = MIIM_STATE,
		.fState = UINT(sel == CTL_ERR ? MFS_DISABLED : MFS_ENABLED)
	};
	SetMenuItemInfoW(hRemoveMenu, 0, TRUE, &mii);
	POINT point;
	GetCursorPos(&point);
	TrackPopupMenuEx(hRemoveMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_VERNEGANIMATION, point.x, point.y, hWnd, nullptr);
}

void UpdateDlg::RemoveIt()
{
	int sel = UpdateList.GetSelectionMark();
	if (sel != CTL_ERR)
	{
		UpdateList.DeleteItem(sel);
		ctx.UpdateVector.erase(ctx.UpdateVector.cbegin() + sel);
		FinalPath.SetWindowText(nullptr);
		UpdateList.SetSelectionMark(-1);
		Invalidate();
	}
}

void UpdateDlg::RemoveSelectedItems()
{
	for (int i = UpdateList.GetItemCount() - 1; i != -1; --i)
		if (UpdateList.GetCheckState(i) == TRUE)
		{
			ctx.UpdateVector.erase(ctx.UpdateVector.cbegin() + i);
			UpdateList.DeleteItem(i);
		}
	UpdateList.SetSelectionMark(-1);
	Invalidate();
}

void UpdateDlg::RemoveAll()
{
	UpdateList.DeleteAllItems();
	ctx.UpdateVector.clear();
	FinalPath.SetWindowText(nullptr);
	Invalidate();
}

void UpdateDlg::SetUpdatePSF()
{
	int sel = UpdateList.GetSelectionMark();
	if (sel == CTL_ERR)
		return;

	String* str = reinterpret_cast<String*>(SetPSFDlg(this).ModalDialogBox(reinterpret_cast<LPARAM>(ctx.UpdateVector[sel].PSF.GetPointer())));
	if (str > reinterpret_cast<String*>(IDCANCEL))
	{
		ctx.UpdateVector[sel].PSF = move(*str);
		delete str;
		FinalPath.SetWindowText(
			ResStrFormat(String_UpdatePathDetail, ctx.UpdateVector[sel].UpdateFile.GetPointer(), ctx.UpdateVector[sel].PSF.Empty() ? nullptr : ctx.UpdateVector[sel].PSF.GetPointer()));
	}
}

static bool GetUpdate(PWSTR pFile, UpdateDlg* p)
{
	auto& v = p->ctx.UpdateVector;
	if ([pFile]()
		{
			bool ret = false;
			auto i = pFile + wcslen(pFile);
			while (*i != '\\' && *i != '/' && i != pFile)
			{
				--i;
				if (*i == '?' || *i == '*')
					ret = true;
			}
			if (pFile == i)
				return ret;
			*i = 0;
			SetCurrentDirectoryW(pFile);
			*i = '\\';
			return ret;
		}
		())
	{
		bool ret = false;
		WIN32_FIND_DATAW wfd;
		HANDLE hFindFile = FindFirstFileW(pFile, &wfd);
		if (hFindFile == INVALID_HANDLE_VALUE)
			return false;
		do
		{
			if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY
				|| !PathMatchSpecW(wfd.cFileName, L"*.cab") && !PathMatchSpecW(wfd.cFileName, L"*.msu"))
				continue;

			HANDLE hFile = CreateFileW(wfd.cFileName, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
			if (hFile == INVALID_HANDLE_VALUE)
				continue;
			String str = GetFinalPathName(hFile);
			CloseHandle(hFile);
			PWSTR file = str.GetPointer() + str.GetLength();
			while (*file != '\\')
				--file;
			v.resize(v.size() + 1);
			v.rbegin()->UpdateFile = move(str);
			p->UpdateList.SetItemText(p->UpdateList.InsertItem(), 0, ++file);
			ret = true;
		} while (FindNextFileW(hFindFile, &wfd));
		FindClose(hFindFile);
		return ret;
	}

	HANDLE hFile = CreateFileW(pFile, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	String str = GetFinalPathName(hFile);
	if (GetFileAttributesByHandle(hFile) & FILE_ATTRIBUTE_DIRECTORY)
	{
		CloseHandle(hFile);
		bool ret = false;
		wstring path = str.operator PCWSTR();
		path += '\\';
		path += L"update.mum";
		DWORD dwAttr = GetFileAttributesW(path.c_str());
		if (dwAttr == INVALID_FILE_ATTRIBUTES)
		{
			path.resize(path.size() - 10);
			path += L"*.cab";
			WIN32_FIND_DATAW wfd;
			auto Find = [&]()
				{
					HANDLE hFindFile = FindFirstFileW(path.c_str(), &wfd);
					if (hFindFile == INVALID_HANDLE_VALUE)
						return false;
					else
					{
						do
						{
							if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
								continue;
							v.resize(v.size() + 1);
							auto& ref = *v.rbegin();
							ref.UpdateFile = str.operator PCWSTR();
							ref.UpdateFile += '\\';
							ref.UpdateFile += wfd.cFileName;
							p->UpdateList.SetItemText(p->UpdateList.InsertItem(), 0, wfd.cFileName);
						} while (FindNextFileW(hFindFile, &wfd));
						FindClose(hFindFile);
					}
					return true;
				};
			ret |= Find();
			path += L"*.msu";
			ret |= Find();
			path.resize(path.size() - 5);
		}
		SetLastError(ret ? ERROR_SUCCESS : ERROR_FILE_NOT_FOUND);
		return ret;
	}

	CloseHandle(hFile);
	PWSTR file = str.GetPointer() + str.GetLength();
	while (*file != '\\')
		--file;
	v.resize(v.size() + 1);
	v.rbegin()->UpdateFile = move(str);
	p->UpdateList.SetItemText(p->UpdateList.InsertItem(), 0, ++file);
	return true;
}

void UpdateDlg::BrowseFiles()
{
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
	for (DWORD i = 0; i != dw; ++i)
	{
		sia->GetItemAt(i, &si);
		PWSTR name;
		si->GetDisplayName(SIGDN_DESKTOPABSOLUTEEDITING, &name);
		si->Release();
		GetUpdate(name, this);
		CoTaskMemFree(name);
	}
	sia->Release();
	Invalidate();
}

void UpdateDlg::BrowseDirectory()
{
	String Dir;
	if (GetOpenFolderName(this, Dir))
		Path.SetWindowText(Dir);
}

void UpdateDlg::AddItem()
{
	if (GetUpdate(Path.GetWindowText().GetPointer(), this))
	{
		Path.SetWindowText(nullptr);
		Invalidate();
	}
	else
		ErrorMessageBox();
}

void UpdateDlg::OpenOtherUpdateDlg()
{
	struct OtherUpdateDlg : DialogEx2<UpdateDlg>
	{
		OtherUpdateDlg(UpdateDlg* Parent) : DialogEx2(Parent, GetFontSize(nullptr) * 50, GetFontSize(nullptr) * 10, WS_BORDER | WS_SYSMENU | WS_CAPTION | DS_MODALFRAME | DS_FIXEDSYS | WS_SIZEBOX, GetString(String_ViewOtherUpdates)),
			List(this, 0, WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL)
		{
		}

		void Init()
		{
			CenterWindow(Parent);
			int pxUnit = GetFontSize(nullptr);

			List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_CHECKBOXES);
			List.InsertColumn(GetString(String_File).GetPointer(), pxUnit * 31, 0);
			List.InsertColumn(GetString(String_Type).GetPointer(), pxUnit * 17, 1);
			auto& ctx = GetParent()->ctx;
			if (!ctx.SafeOSUpdate.Empty())
			{
				List.InsertItem();
				List.SetItemText(0, 0, ctx.SafeOSUpdate.GetPointer());
				List.SetItemText(0, 1, L"SafeOS Update");
				List.SetCheckState(0, ctx.bAddSafeOSUpdate);
			}
			if (!ctx.SetupUpdate.Empty())
			{
				int i = List.InsertItem();
				List.SetItemText(i, 0, ctx.SetupUpdate.GetPointer());
				List.SetItemText(i, 1, L"Setup Update");
				List.SetCheckState(i, ctx.bAddSetupUpdate);
			}
			if (!ctx.EnablementPackage.Empty())
			{
				int i = List.InsertItem();
				List.SetItemText(i, 0, ctx.EnablementPackage.GetPointer());
				List.SetItemText(i, 1, L"Feature Enablement Package");
				List.SetCheckState(i, ctx.bAddEnablementPackage);
			}

			List.EnableWindow(GetParent()->Add.IsWindowEnabled());
		}

		void OnSize(BYTE ResizeType, int nClientWidth, int nClientHeight, WindowBatchPositioner wbp)
		{
			wbp.MoveWindow(List, 0, 0, nClientWidth, nClientHeight);
		}

		bool OnClose()
		{
			auto nCount = List.GetItemCount();
			auto& ctx = GetParent()->ctx;
			for (int i = 0; i != nCount; ++i)
			{
				auto Text = List.GetItemText(i, 1);
				if (Text == L"SafeOS Update")
					ctx.bAddSafeOSUpdate = List.GetCheckState(i);
				else if (Text == L"Setup Update")
					ctx.bAddSetupUpdate = List.GetCheckState(i);
				else if (Text == L"Feature Enablement Package")
					ctx.bAddEnablementPackage = List.GetCheckState(i);
			}
			return false;
		}

		void OnOK()
		{
			OnClose();
			Dialog::OnOK();
		}

		void OnCancel()
		{
			OnClose();
			Dialog::OnCancel();
		}

		ListView List;
	};

	OtherUpdateDlg(this).ModalDialogBox();
}

void UpdateDlg::SwitchCleanComponents()
{
	int Check = CleanComponents.GetCheck();
	if (Check == BST_UNCHECKED
		&& MessageBox(GetString(String_CleanComponentsWarning), GetString(String_Notice), MB_ICONWARNING | MB_YESNO) == IDNO)
		return;
	ctx.bCleanComponentStore = Check == BST_UNCHECKED;
	CleanComponents.SetCheck(Check == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED);
}

void UpdateDlg::OpenDialog(WindowBase* Parent, SessionContext* ctx)
{
	UpdateDlg(Parent, *ctx).ModalDialogBox();
}
