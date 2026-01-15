#include "pch.h"
#include "misc.h"
#include "Resources/resource.h"

#include <Shlwapi.h>

#include <algorithm>
#include <format>

using namespace std;
using namespace Lourdle::UIFramework;

AppDlg::AppDlg(WindowBase* Parent, SessionContext& ctx)
	: DialogEx2(Parent, GetFontSize() * 60, GetFontSize() * 35, WS_CAPTION | WS_SYSMENU | DS_FIXEDSYS | DS_MODALFRAME, GetString(String_InstallApps)),
	ctx(ctx), Browse(this, &AppDlg::BrowseFiles), ViewAppxFeatures(this, &AppDlg::ViewFeatures),
	Remove(this, &AppDlg::RemoveItems), Add(this, &AppDlg::AddFiles), InstallEdge(this, 0, ButtonStyle::AutoCheckbox),
	AppList(this, 0, WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_SINGLESEL), Path(this, 0), hRemoveOptionMenu(CreatePopupMenu())
{
	pFileOpenDialog = CreateFileOpenDialogInstance();
	auto type1 = GetString(String_AppPackage), type2 = GetString(String_AppBundle);
	COMDLG_FILTERSPEC cfs[2] = { { type2, L"*.appxbundle;*.msixbundle" }, { type1, L"*.appx;*.msix" } };
	pFileOpenDialog->SetFileTypes(2, cfs);
	DWORD dwOptions;
	pFileOpenDialog->GetOptions(&dwOptions);
	pFileOpenDialog->SetOptions(dwOptions | FOS_ALLOWMULTISELECT | FOS_NOCHANGEDIR | FOS_NODEREFERENCELINKS);

	WORD id = Random();
	AppendMenuW(hRemoveOptionMenu, MF_STRING, id, GetString(String_RemoveThis));
	RegisterCommand(&AppDlg::RemoveIt, id, 0);
	id = Random();
	AppendMenuW(hRemoveOptionMenu, MF_STRING, id, GetString(String_RemoveSelected));
	RegisterCommand(&AppDlg::RemoveSelectedItems, id, 0);
	id = Random();
	AppendMenuW(hRemoveOptionMenu, MF_STRING, id, GetString(String_RemoveAll));
	RegisterCommand(&AppDlg::RemoveAll, id, 0);
}

AppDlg::~AppDlg()
{
	DestroyMenu(hRemoveOptionMenu);
	pFileOpenDialog->Release();
}

static bool GetApp(PWSTR pFileName, ListView& AppList, vector<String>& AppVector)
{
	if (!PathMatchSpecW(pFileName, L"*.appx")
		&& !PathMatchSpecW(pFileName, L"*.appxbundle")
		&& !PathMatchSpecW(pFileName, L"*.msix")
		&& !PathMatchSpecW(pFileName, L"*.msixbundle"))
	{
		SetLastError(ERROR_BAD_FILE_TYPE);
		return false;
	}
	HANDLE hFile = CreateFileW(pFileName, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;
	String File = GetFinalPathName(hFile);
	CloseHandle(hFile);
	PWSTR p = File.end();
	while (*p != '\\')
		--p;
	int iIndex = AppList.InsertItem();
	AppList.SetItemText(iIndex, 0, p + 1);
	AppList.SetItemText(iIndex, 1, File.GetPointer());
	AppVector.push_back(move(File));
	return true;
}

void AppDlg::AddFiles()
{
	String Name = Path.GetWindowText();
	String Dir;
	for (PWSTR i = Name.end();; --i)
		if (i == Name.begin())
		{
			SetLastError(ERROR_NOT_FOUND);
			ErrorMessageBox();
			return;
		}
		else if (*i == '\\' || *i == '/')
		{
			*i = 0;
			HANDLE hDir = CreateFileW(Name, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
			if (hDir == INVALID_HANDLE_VALUE)
			{
				ErrorMessageBox();
				return;
			}
			Dir = GetFinalPathName(hDir);
			if (Dir.end()[-1] != '\\')
				Dir += '\\';
			CloseHandle(hDir);
			*i = '\\';
			break;
		}

	WIN32_FIND_DATAW wfd;
	HANDLE hFindFile = FindFirstFileW(Name, &wfd);
	if (hFindFile == INVALID_HANDLE_VALUE)
		ErrorMessageBox();
	else
	{
		bool result = false;
		do
		{
			String File = Dir + wfd.cFileName;
			result |= GetApp(File.GetPointer(), AppList, ctx.AppVector);
		} while (FindNextFileW(hFindFile, &wfd));
		FindClose(hFindFile);

		if (!result)
		{
			SetLastError(ERROR_FILE_NOT_FOUND);
			ErrorMessageBox();
			return;
		}
		Path.SetWindowText(nullptr);
		Invalidate();
	}
}

void AppDlg::BrowseFiles()
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
		if (!GetApp(name, AppList, ctx.AppVector))
			ErrorMessageBox();
		CoTaskMemFree(name);
	}
	Invalidate();
	sia->Release();
}

void AppDlg::RemoveIt()
{
	int sel = AppList.GetSelectionMark();
	if (sel == CTL_ERR)
		return;

	AppList.DeleteItem(sel);
	ctx.AppVector.erase(ctx.AppVector.cbegin() + sel);
	AppList.SetSelectionMark(-1);
	Invalidate();
}

void AppDlg::RemoveSelectedItems()
{
	for (int i = AppList.GetItemCount() - 1; i != -1; --i)
		if (AppList.GetCheckState(i) == TRUE)
		{
			AppList.DeleteItem(i);
			ctx.AppVector.erase(ctx.AppVector.cbegin() + i);
		}
	AppList.SetSelectionMark(-1);
	Invalidate();
}

void AppDlg::RemoveAll()
{
	AppList.DeleteAllItems();
	ctx.AppVector.clear();
	Invalidate();
}

void AppDlg::RemoveItems()
{
	int sel = AppList.GetSelectionMark();
	MENUITEMINFOW mii = { sizeof(mii) };
	mii.fMask = MIIM_STATE;
	mii.fState = sel == CTL_ERR ? MFS_DISABLED : MFS_ENABLED;
	SetMenuItemInfoW(hRemoveOptionMenu, 0, TRUE, &mii);
	POINT point;
	GetCursorPos(&point);
	TrackPopupMenuEx(hRemoveOptionMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_VERNEGANIMATION, point.x, point.y, hWnd, nullptr);
}

bool AppDlg::OnClose()
{
	ctx.bInstallEdge = InstallEdge.GetCheck();
	return false;
}

struct AppxFeatureDlg : DialogEx2<AppDlg>
{
	AppxFeatureDlg(AppDlg* Parent);

	ListView AppxFeatureList;
	Button StubOption;
	Tooltips StubOptionTip;

	bool bPreventCheck = false;

	void Init();
	bool OnClose();
	void OnDraw(HDC hdc, RECT rect);
	LRESULT OnNotify(LPNMHDR pnmhdr);
};

AppxFeatureDlg::AppxFeatureDlg(AppDlg* Parent) : DialogEx2(Parent, GetFontSize() * 40, GetFontSize() * 40, WS_CAPTION | WS_BORDER | WS_SYSMENU | DS_FIXEDSYS | DS_MODALFRAME, GetString(String_ViewAppxFeatures)),
bPreventCheck(!Parent->Remove.IsWindowEnabled()), AppxFeatureList(this, 0, WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_SINGLESEL), StubOption(this, 0, !Parent->Remove.IsWindowEnabled() ? ButtonStyle::Checkbox : ButtonStyle::AutoCheckbox)
{
}


void AppxFeatureDlg::Init()
{
	AppxFeatureList.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_CHECKBOXES);
	CenterWindow(Parent);

	int pxUnit = GetFontSize();
	auto& v = GetParent()->ctx.AppxFeatures;
	AppxFeatureList.InsertColumn(L"Feature", pxUnit * 34, 0);
	for (auto& i : v)
	{
		int index = AppxFeatureList.InsertItem();
		AppxFeatureList.SetItemText(index, 0, i.Feature.c_str());
		if (i.bInstall)
			AppxFeatureList.SetCheckState(index, true);
	}

	AppxFeatureList.MoveWindow(pxUnit * 2, pxUnit * 5, pxUnit * 36, pxUnit * 31);

	StubOption.SetWindowText(GetString(String_StubOptionInstallFull));
	StubOption.SetCheck(GetParent()->ctx.bStubOptionInstallFull);
	SIZE size;
	StubOption.GetIdealSize(&size);
	StubOption.MoveWindow(pxUnit * 39 - size.cx, pxUnit * 38 - size.cy / 2, size.cx, size.cy);

	String TipText = GetString(String_StubOptionTip);
	TTTOOLINFOW ti = {
		.cbSize=sizeof(ti),
		.uFlags = TTF_SUBCLASS | TTF_IDISHWND,
		.hwnd = StubOption,
		.lpszText = TipText.GetPointer()
	};
	ti.uId = reinterpret_cast<UINT_PTR>(ti.hwnd);
	StubOptionTip.AddTool(&ti);
	StubOptionTip.SetMaxTipWidth(pxUnit * 30);
}

void AppxFeatureDlg::OnDraw(HDC hdc, RECT rect)
{
	rect.left = GetFontSize();
	rect.right -= rect.left;
	rect.bottom = GetFontSize() * 5;
	DrawText(hdc, GetString(String_InstallingAppxFeatures), -1, &rect, DT_WORDBREAK | DT_VCENTER);
}

LRESULT AppxFeatureDlg::OnNotify(LPNMHDR pnmhdr)
{
	auto pnm = reinterpret_cast<NMLISTVIEW*>(pnmhdr);
	if (pnmhdr->hwndFrom == AppxFeatureList)
		if (pnmhdr->code == LVN_ITEMCHANGING)
		{
			if (bPreventCheck)
				return TRUE;

			auto& AppxFeatures = GetParent()->ctx.AppxFeatures;
			constexpr UINT kStateUnchecked = INDEXTOSTATEIMAGEMASK(1);
			constexpr UINT kStateChecked = INDEXTOSTATEIMAGEMASK(2);
			const UINT oldStateImage = pnm->uOldState & LVIS_STATEIMAGEMASK;
			const UINT newStateImage = pnm->uNewState & LVIS_STATEIMAGEMASK;

			auto CollectDependentsToDisable = [](const AppxFeature* target, const vector<AppxFeature>& features)
				-> vector<int>
			{
				vector<int> indices;
				vector<const AppxFeature*> stack;
				stack.push_back(target);

				vector<char> marked(features.size(), false);
				while (!stack.empty())
				{
					const AppxFeature* current = stack.back();
					stack.pop_back();

					for (int i = 0; i != static_cast<int>(features.size()); ++i)
					{
						if (marked[i] || !features[i].bInstall)
							continue;
						for (const auto* dep : features[i].Dependencies)
							if (dep == current)
							{
								marked[i] = true;
								indices.push_back(i);
								stack.push_back(&features[i]);
								break;
							}
					}
				}

				sort(indices.begin(), indices.end());
				return indices;
			};

			if (oldStateImage == kStateChecked && newStateImage == kStateUnchecked)
			{
				vector<int> dependents = CollectDependentsToDisable(&AppxFeatures[pnm->iItem], AppxFeatures);

				if (!dependents.empty())
				{
					wstring text = GetString(String_WillDisableOthers).GetPointer();
					for (int i : dependents)
						text += format(L"\r\n    ●{}", AppxFeatures[i].Feature);

					if (MessageBox(text.c_str(), GetString(String_Notice), MB_ICONINFORMATION | MB_YESNO) == IDNO)
						return TRUE;
					for (int i : dependents)
					{
						AppxFeatureList.SetCheckState(i, false);
						AppxFeatures[i].bInstall = false;
					}
				}
				AppxFeatures[pnm->iItem].bInstall = false;
			}
			else if (oldStateImage == kStateUnchecked && newStateImage == kStateChecked)
			{
				for (int i = 0; i != AppxFeatures.size(); ++i)
					for (auto j : AppxFeatures[pnm->iItem].Dependencies)
						if (j == &AppxFeatures[i])
						{
							AppxFeatureList.SetCheckState(i, true);
							AppxFeatures[i].bInstall = true;
							break;
						}
				AppxFeatures[pnm->iItem].bInstall = true;
			}
		}
		else if (pnmhdr->code == LVN_ITEMCHANGED && pnm->iItem >= 0)
			AppxFeatureList.SetCheckState(pnm->iItem, GetParent()->ctx.AppxFeatures[pnm->iItem].bInstall);

	return 0;
}

bool AppxFeatureDlg::OnClose()
{
	GetParent()->ctx.bStubOptionInstallFull = StubOption.GetCheck() == BST_CHECKED;
	return false;
}

void AppDlg::ViewFeatures()
{
	AppxFeatureDlg(this).ModalDialogBox();
	Invalidate();
}

void AppDlg::Init()
{
	CenterWindow(Parent);

	AppList.AddExtendedListViewStyle(LVS_EX_CHECKBOXES);
	AppList.InsertColumn(GetString(String_File).GetPointer(), GetFontSize() * 48, 0);
	AppList.InsertColumn(GetString(String_Path).GetPointer(), GetFontSize() * 100, 1);
	for (int i = 0; i != ctx.AppVector.size(); ++i)
	{
		AppList.InsertItem();
		PWSTR pFileName = ctx.AppVector[i].GetPointer();
		AppList.SetItemText(i, 1, pFileName);
		for (pFileName = pFileName + wcslen(pFileName) - 1; *pFileName != '\\'; --pFileName)
			if (pFileName == ctx.AppVector[i].GetPointer())
			{
				--pFileName;
				break;
			}
		AppList.SetItemText(i, 0, pFileName + 1);
	}

	if (!PathFileExistsW((ctx.PathUUP + L"Edge.wim").c_str()))
	{
		ctx.bInstallEdge = false;
		InstallEdge.EnableWindow(false);
	}
	else
		InstallEdge.SetCheck(ctx.bInstallEdge);
	Browse.SetWindowText(GetString(String_Browse));
	Add.SetWindowText(GetString(String_Add));
	Remove.SetWindowText(GetString(String_Remove));
	ViewAppxFeatures.SetWindowText(GetString(String_ViewAppxFeatures));
	InstallEdge.SetWindowText(GetString(String_InstallEdge));

	int pxUnit = GetFontSize();
	SIZE size;
	InstallEdge.GetIdealSize(&size);
	InstallEdge.MoveWindow(pxUnit * 59 - size.cx, pxUnit * 33 - size.cy / 2, size.cx, size.cy);
	AppList.MoveWindow(pxUnit, pxUnit, pxUnit * 58, pxUnit * 20);
	Path.MoveWindow(pxUnit, pxUnit * 29, pxUnit * 52, pxUnit * 2);
	Browse.MoveWindow(pxUnit * 54, pxUnit * 29, pxUnit * 5, pxUnit * 2);
	Add.MoveWindow(pxUnit, pxUnit * 32, pxUnit * 5, pxUnit * 2);
	Remove.MoveWindow(pxUnit * 54, pxUnit * 22, pxUnit * 5, pxUnit * 2);
	ViewAppxFeatures.MoveWindow(pxUnit, pxUnit * 22, pxUnit * 12, pxUnit * 2);
}

void AppDlg::OnDraw(HDC hdc, RECT rect)
{
	rect.left = GetFontSize();
	rect.top = rect.left * 25;
	rect.right -= rect.left;

	int n = 0;
	for (auto& i : ctx.AppxFeatures)
		if (i.bInstall)
			++n;

	DrawText(hdc,
		ResStrFormat(String_AddApps, static_cast<int>(ctx.AppVector.size()), static_cast<int>(n)),
		-1, &rect, DT_WORDBREAK);
}

void AppDlg::OpenDialog(WindowBase* Parent, SessionContext* ctx)
{
	AppDlg(Parent, *ctx).ModalDialogBox();
}
