#include "pch.h"
#include "UUPAssistant.h"
#include "Resources/resource.h"
#include "Xml.h"

#include <Shlwapi.h>
#include <AclAPI.h>
#include <wimgapi.h>
#include <wil/com.h>

#include <thread>
#include <vector>
#include <unordered_map>
#include <format>
#include <filesystem>

using namespace Lourdle::UIFramework;
using namespace std;
using namespace filesystem;
using namespace rapidxml;
using namespace wil;

namespace
{
	inline bool IsWimHandleValid(HANDLE h)
	{
		return h && h != INVALID_HANDLE_VALUE;
	}

	constexpr DWORD kFakeSetupHostSuccessExitCode =
#include "../FakeSetupHost/ExitCode.inc"
		+ 0; // + 0 to prevent IntelliSense error

	constexpr UINT kProgressMax = 10000;
	constexpr UINT kProgressStage1Percent = 20;
	constexpr UINT kProgressStageGapPercent = 5;
	constexpr UINT kProgressStage2Percent = 75;
	constexpr UINT kProgressPerPercent = kProgressMax / 100;
	constexpr UINT kProgressStage1Max = kProgressStage1Percent * kProgressPerPercent;
	constexpr UINT kProgressStage2Max = kProgressStage2Percent * kProgressPerPercent;
	constexpr UINT kProgressStage2Base = (kProgressStage1Percent + kProgressStageGapPercent) * kProgressPerPercent;

	constexpr DWORD MOSETUP_E_COMPAT_SCANONLY = 0xC1900210;

	inline void WimCloseHandleIfValid(HANDLE& h)
	{
		if (IsWimHandleValid(h))
			WIMCloseHandle(h);
		h = nullptr;
	}
}

void InPlaceSetupWizard::SwitchInstallationMethod()
{
	bool bChecked = SynergisticInstallation.GetCheck() == BST_CHECKED;
	if (!bChecked)
	{
		HANDLE hWim = WIMCreateFile((ctx.PathUUP + ctx.TargetImageInfo.SystemESD).c_str(), WIM_GENERIC_READ, OPEN_EXISTING, WIM_FLAG_ALLOW_LZMS, 0, nullptr);
		if (!IsWimHandleValid(hWim) || !WIMSetTemporaryPath(hWim, ctx.PathTemp.c_str()))
		{
			WimCloseHandleIfValid(hWim);
			MessageBox(GetString(String_SynergisticInstallationIsNotAvaliable), GetString(String_Notice), MB_ICONERROR);
			goto CheckAdvancedOptions;
		}

		HANDLE hImage = WIMLoadImage(hWim, 1);
		struct WFD : WIM_FIND_DATA
		{
			BYTE ReservedBytes[32];
		} wfd;
		HANDLE hFindFile = nullptr;
		if (IsWimHandleValid(hImage))
			hFindFile = WIMFindFirstImageFile(hImage, L"\\sources\\wimgapi.dll", &wfd);
		WimCloseHandleIfValid(hImage);
		WIMCloseHandle(hWim);
		if (!IsWimHandleValid(hFindFile))
		{
			MessageBox(GetString(String_SynergisticInstallationIsNotAvaliable), GetString(String_Notice), MB_ICONERROR);
			goto CheckAdvancedOptions;
		}
		else
		{
			WIMCloseHandle(hFindFile);
			SynergisticInstallation.SetCheck(bChecked ? BST_UNCHECKED : BST_CHECKED);
			MessageBox(GetString(String_SynergisticInstallationNotice), GetString(String_Notice), MB_ICONINFORMATION);
		}
	}
	else
		SynergisticInstallation.SetCheck(bChecked ? BST_UNCHECKED : BST_CHECKED);

CheckAdvancedOptions:
	if (SynergisticInstallation.GetCheck() == BST_CHECKED)
	{
		InstallUpdates.EnableWindow(true);
		InstallAppxes.EnableWindow(true);
		AddDrivers.EnableWindow(true);
	}
	else
	{
		InstallUpdates.EnableWindow(ctx.bAdvancedOptionsAvaliable);
		InstallAppxes.EnableWindow(ctx.bAdvancedOptionsAvaliable);
		AddDrivers.EnableWindow(ctx.bAdvancedOptionsAvaliable);
	}
}

InPlaceSetupWizard::InPlaceSetupWizard(SessionContext& refSessionContext) : Window(GetFontSize() * 36, GetFontSize() * 36, GetString(String_InplaceInstallation), WS_BORDER | WS_SYSMENU | WS_MINIMIZEBOX | WS_CAPTION | WS_CLIPCHILDREN),
InstallUpdates(this, ButtonStyle::CommandLink, Random(), UpdateDlg::OpenDialog, this, &ctx),
InstallAppxes(this, ButtonStyle::CommandLink, Random(), AppDlg::OpenDialog, this, &ctx),
AddDrivers(this, ButtonStyle::CommandLink, Random(), DriverDlg::OpenDialog, this, &ctx),
PreventUpdating(this, 0, ButtonStyle::AutoCheckbox), Next(this, &InPlaceSetupWizard::Execute), ctx(refSessionContext), NoReboot(this, 0, ButtonStyle::AutoCheckbox),
SynergisticInstallation(this, &InPlaceSetupWizard::SwitchInstallationMethod, ButtonStyle::Checkbox), RemoveHwReq(this, 0, ButtonStyle::AutoCheckbox), hProcess(nullptr)
{
	InstallUpdates.SetWindowText(GetString(String_InstallUpdates));
	InstallAppxes.SetWindowText(GetString(String_InstallApps));
	AddDrivers.SetWindowText(GetString(String_AddDrivers));
	PreventUpdating.SetWindowText(GetString(String_PreventUpdating));
	Next.SetWindowText(GetString(String_Next));
	NoReboot.SetWindowText(GetString(String_NoReboot));
	SynergisticInstallation.SetWindowText(GetString(String_SynergisticInstallation));
	RemoveHwReq.SetWindowText(GetString(String_RemoveHwReq));

	SIZE size;
	InstallUpdates.GetIdealSize(&size);
	InstallUpdates.MoveWindow(GetFontSize() * 9, GetFontSize() * 11, GetFontSize() * 18, size.cy);
	InstallAppxes.MoveWindow(GetFontSize() * 9, GetFontSize() * 15, GetFontSize() * 18, size.cy);
	AddDrivers.MoveWindow(GetFontSize() * 9, GetFontSize() * 19, GetFontSize() * 18, size.cy);
	Next.MoveWindow(GetFontSize() * 15, GetFontSize() * 33, GetFontSize() * 6, GetFontSize() * 2);
	PreventUpdating.GetIdealSize(&size);
	PreventUpdating.SetCheck(BST_CHECKED);
	PreventUpdating.MoveWindow(GetFontSize() * 6, GetFontSize() * 23, size.cx, size.cy);
	NoReboot.GetIdealSize(&size);
	NoReboot.MoveWindow(GetFontSize() * 6, GetFontSize() * 23 + size.cy + 2, size.cx, size.cy);
	SynergisticInstallation.GetIdealSize(&size);
	SynergisticInstallation.MoveWindow(GetFontSize() * 6, GetFontSize() * 24 + size.cy * 2 + 3, size.cx, size.cy);
	RemoveHwReq.GetIdealSize(&size);
	RemoveHwReq.MoveWindow(GetFontSize() * 6, GetFontSize() * 24 + size.cy * 3 + 4, size.cx, size.cy);
	if (ctx.TargetImageInfo.Name.find(L"Windows 11") == wstring::npos)
		RemoveHwReq.EnableWindow(false);

	SynergisticInstallation.PostCommand();
}

void InPlaceSetupWizard::OnDraw(HDC hdc, RECT rect)
{
	rect.top = GetFontSize();
	rect.left += rect.top;
	rect.right -= rect.top;
	DrawText(hdc, String_CustomizeSystemUpgrade, &rect, DT_WORDBREAK);
}

void InPlaceSetupWizard::OnDestroy()
{
	PostQuitMessage(0);
}

void InPlaceSetupWizard::OnSize(BYTE type, int nClientWidth, int nClientHeight, WindowBatchPositioner)
{
	SetProcessEfficiencyMode(type == SIZE_MINIMIZED);
}

static void GetWindowsBt(WCHAR szPath[MAX_PATH])
{
	GetSystemDirectoryW(szPath, MAX_PATH);
	szPath[3] = 0;
	wcscat_s(szPath, MAX_PATH, L"$WINDOWS.~BT");
}


static bool MergeDirectory(PCWSTR pSourceDir, PCWSTR pTargetDir)
{
	error_code ec;
	auto source = filesystem::path(pSourceDir);
	auto iter = filesystem::directory_iterator(source, ec);
	if (ec) return false;

	for (auto& entry : iter)
	{
		auto filename = entry.path().filename();
		auto target = filesystem::path(pTargetDir) / filename;
		if (entry.is_directory())
		{
			if (!CreateDirectoryW(target.c_str(), nullptr)
				&& GetLastError() != ERROR_ALREADY_EXISTS)
				return false;
			else if (!MergeDirectory((source / filename).c_str(), target.c_str()))
				return false;
		}
		else if (!MoveFileExW((source / filename).c_str(), target.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING))
			return false;
	}

	return RemoveDirectoryW(pSourceDir);
}


struct PreparationDialog;

static void PreparationThread(SessionContext& ctx, Dialog* pDlg, ITaskbarList4* pTaskbarList, ProgressBar* pProgress, bool bMediaSetup, bool*);
static void RunSetup(PreparationDialog* pDlg, bool bPreventUpdating, bool bNoReboot);

struct UpgradeSourceCapabilities
{
	bool dataOnly = false;
	bool fullUpgrade = false;
};

using UpgradeSourceMap_t = unordered_map<wstring, UpgradeSourceCapabilities>;
using UpgradeMaps_t = unordered_map<wstring, UpgradeSourceMap_t>;
static UpgradeMaps_t GetUpgradeMaps(SessionContext& ctx)
{
	UpgradeMaps_t UpgradeMap;

	try
	{
		WIMStruct* wim;
		string text;
		SetCurrentDirectoryW(ctx.PathUUP.c_str());
		int code = wimlib_open_wim(ctx.TargetImageInfo.SystemESD.c_str(), 0, &wim);
		if (code == 0)
		{
			SetCurrentDirectoryW(ctx.PathTemp.c_str());
			SetReferenceFiles(wim);

			PCWSTR pszFileName = L"\\Windows\\servicing\\Editions\\UpgradeMatrix.xml";
			code = wimlib_extract_paths(wim, 3, ctx.PathTemp.c_str(), &pszFileName, 1, WIMLIB_EXTRACT_FLAG_NO_PRESERVE_DIR_STRUCTURE);
			if (code != 0) goto wimlib_failure;
			wimlib_free(wim);

			HANDLE hFile = CreateFileW(L"UpgradeMatrix.xml", GENERIC_READ | DELETE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (hFile != INVALID_HANDLE_VALUE)
			{
				ReadText(hFile, text, GetFileSize(hFile, nullptr));
				DeleteFileOnClose(hFile);
				CloseHandle(hFile);
			}
		}
		else
		{
		wimlib_failure:
			auto error_string = wimlib_get_error_string(static_cast<wimlib_error_code>(code));
			throw runtime_error(string(error_string, error_string + wcslen(error_string)));
		}

		rapidxml::xml_document<> doc;
		doc.parse<0>(const_cast<char*>(text.c_str()));
		auto pRoot = doc.first_node();
		if (!pRoot)
			throw rapidxml::parse_error("UpgradeMatrix.xml", &pRoot);

		// First, we find the correct version range for the current OS version.
		string versionRange;
		string anyRange;
		VersionStruct Version;
		GetNtKernelVersion(Version);
		if (auto ranges = pRoot->first_node("VersionRanges"); ranges)
			for (auto range = ranges->first_node("Range"); range; range = range->next_sibling("Range"))
			{
				auto name = range->first_attribute("name");
				auto minVersion = range->first_attribute("minVersion");
				auto maxVersion = range->first_attribute("maxVersion");
				if (!name || !minVersion || !maxVersion)
					continue;

				auto ParseVersionString = [](rapidxml::xml_base<char>* attr, VersionStruct& version)
					{
						auto value = attr->value();
						auto size = attr->value_size();
						MyUniquePtr<WCHAR> Buffer(size + 1);
						for (size_t i = 0; i != size; ++i)
							Buffer[i] = value[i];
						Buffer[size] = 0;
						::ParseVersionString(Buffer, version);
					};

				if (minVersion->value_size() == 7
					&& maxVersion->value_size() == 7
					&& memcmp(minVersion->value(), maxVersion->value(), minVersion->value_size()) == 0
					&& memcmp(minVersion->value(), "*.*.*.*", 7) == 0)
				{
					anyRange = string(name->value(), name->value_size());
					if (versionRange.empty()) continue;
					else break;
				}

				VersionStruct MinVersion, MaxVersion;
				ParseVersionString(minVersion, MinVersion);
				ParseVersionString(maxVersion, MaxVersion);

				if (MinVersion <= Version && Version <= MaxVersion)
				{
					versionRange = string(name->value(), name->value_size());
					if (anyRange.empty()) continue;
					else break;
				}
			}

		for (auto target = pRoot->first_node("TargetEdition"); target; target = target->next_sibling("TargetEdition"))
		{
			auto TargetEditionId = target->first_attribute("ID");
			if (!TargetEditionId) continue;
			wstring EditionID(TargetEditionId->value(), TargetEditionId->value() + TargetEditionId->value_size());

			for (auto source = target->first_node("SourceEdition"); source; source = source->next_sibling("SourceEdition"))
			{
				auto range = source->first_attribute("versionRange");
				if (!range) continue;

				auto IsInRange = [](rapidxml::xml_base<char>* attr, string& range)
					{
						if (attr->value_size() != range.size()) return false;
						return memcmp(attr->value(), range.c_str(), range.size()) == 0;
					};
				if (!IsInRange(range, anyRange)
					&& !IsInRange(range, versionRange))
					continue;

				auto GetBool = [](rapidxml::xml_base<char>* attr)
					{
						return attr->value_size() == 4 && memcmp(attr->value(), "true", 4) == 0;
					};

				auto ID = source->first_attribute("ID");
				auto dataOnly = source->first_attribute("dataOnly");
				auto fullUpgrade = source->first_attribute("fullUpgrade");
				if (!ID || !dataOnly || !fullUpgrade) continue;

				auto& [bDataOnly, bFullUpgrade] = UpgradeMap[EditionID][wstring(ID->value(), ID->value() + ID->value_size())];
				bDataOnly |= GetBool(dataOnly);
				bFullUpgrade |= GetBool(fullUpgrade);
			}
		}
	}
	catch (exception&) {}
	return UpgradeMap;
}

static bool CheckUpgradeMaps(const UpgradeMaps_t& UpgradeMap, const wstring& TargetEditionId, const wstring& SourceEditionId, bool& dataOnly, bool& fullUpgrade)
{
	auto CheckSourceMap = [&](const UpgradeSourceMap_t& Map)
		{
			auto it = Map.find(SourceEditionId);
			if (it == Map.end())
				it = Map.find(L"*");
			if (it != Map.end())
			{
				dataOnly = it->second.dataOnly;
				fullUpgrade = it->second.fullUpgrade;
				return true;
			}
			return false;
		};

	auto it = UpgradeMap.find(TargetEditionId);
	if (it != UpgradeMap.end() && CheckSourceMap(it->second))
		return true;
	it = UpgradeMap.find(L"*");
	if (it != UpgradeMap.end() && CheckSourceMap(it->second))
		return true;
	return false;
}

static bool IsDowngrade(InPlaceSetupWizard* p)
{
	VersionStruct versionCurrent;
	GetNtKernelVersion(versionCurrent);

	VersionStruct versionTarget;
	ParseVersionString(p->ctx.TargetImageInfo.Version.c_str(), versionTarget);
	versionCurrent.dwSpBuild = 0;

	if (versionCurrent > versionTarget)
		return true;
	return false;
}

class PreparationDialogResult
{
	// Explicit INT_PTR constants so callers can pass directly to EndDialog()
	// and switch on them without casts.
public:
	static constexpr INT_PTR CleanupWindowsBt = 0;
	static constexpr INT_PTR CanceledOrFailed = 1;
	static constexpr INT_PTR LaunchedSetupAndExit = 2;
};

static constexpr int kPreparationEditionComboId = 1234;

struct PreparationDialog : DialogEx3<InPlaceSetupWizard, PHANDLE, PHANDLE>
{
	Button Next;
	ComboBox Edition;
	ProgressBar Progress;
	com_ptr<ITaskbarList4> pTaskbarList;
	PHANDLE phProcess, phFile;

	wstring EditionID;
	UpgradeMaps_t UpgradeMaps;
	UINT uResId = 0;
	bool Cancel = false;


	PreparationDialog(InPlaceSetupWizard* pParent) : DialogEx3(pParent, GetFontSize() * 30, GetFontSize() * 22, WS_CAPTION | WS_SYSMENU | DS_FIXEDSYS | DS_MODALFRAME, GetString(String_SynergisticInstallation)), pTaskbarList(nullptr),
		Next(this, IDOK), Edition(this, kPreparationEditionComboId), Progress(this),
		EditionID(GetVersionValue(L"SOFTWARE", L"EditionID")), UpgradeMaps(GetUpgradeMaps(GetParent()->ctx))
	{
		if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pTaskbarList))))
			pTaskbarList->HrInit();
	}

	void Init(PHANDLE phProcess, PHANDLE phFile)
	{
		this->phProcess = phProcess;
		this->phFile = phFile;
		CenterWindow(Parent);

		Next.EnableWindow(false);
		Next.SetWindowText(GetString(String_Next));
		Next.MoveWindow(GetFontSize() * 12, GetFontSize() * 17, GetFontSize() * 6, GetFontSize() * 2);

		for (const auto& i : GetParent()->ctx.TargetImageInfo.UpgradableEditions)
			Edition.AddString(i.c_str());
		Edition.SetCurSel(Edition.FindString(GetParent()->ctx.TargetImageInfo.Edition.c_str()));

		ChangeEdition();
		if (uResId == String_CantKeepDataAndSettings)
		{
			wstring* target = nullptr;
			for (auto& i : GetParent()->ctx.TargetImageInfo.UpgradableEditions)
				if (EditionID == i.c_str())
				{
					target = &i;
					break;
				}
			if (target)
			{
				Edition.SetCurSel(Edition.FindString(target->c_str()));
				uResId = 0;
			}
		}

		Edition.MoveWindow(GetFontSize() * 7, GetFontSize() * 10, GetFontSize() * 16, GetFontSize() * 2);
		Progress.MoveWindow(0, GetFontSize() * 21, GetFontSize() * 30, GetFontSize() * 1);
		Progress.SetRange(0, kProgressMax);

		if (IsDowngrade(GetParent()))
			uResId = String_CleanDowngrade;
		else
			RegisterCommand(&PreparationDialog::ChangeEdition, kPreparationEditionComboId, CBN_SELCHANGE);

		DispatchAllMessages();
		if (pTaskbarList)
			pTaskbarList->SetProgressState(GetParent()->GetHandle(), TBPF_NORMAL);
		thread(PreparationThread, ref(GetParent()->ctx), this, pTaskbarList.get(), &Progress, false, &Cancel).detach();
	}

	void ChangeEdition()
	{
		bool fullUpgrade = false, dataOnly;
		wstring CurrentSelection = Edition.GetWindowText().GetPointer();
		CheckUpgradeMaps(UpgradeMaps, CurrentSelection, EditionID, dataOnly, fullUpgrade);

		uResId = fullUpgrade ? 0 : String_CantKeepDataAndSettings;
		RECT rc;
		GetClientRect(&rc);
		rc.bottom -= GetFontSize();
		rc.top = rc.bottom - GetFontSize() * 2;
		RedrawWindow(&rc, nullptr, RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW);
	}

	void OnDraw(HDC hdc, RECT rect)
	{
		rect.top = GetFontSize();
		rect.left += rect.top;
		rect.right -= rect.left;
		DrawText(hdc, String_SwitchingEditionForUpgrading, &rect, DT_WORDBREAK);
		rect.bottom -= rect.top * 3 / 2;
		rect.right += rect.left;
		rect.left = 0;
		DrawText(hdc, uResId, &rect, DT_BOTTOM | DT_SINGLELINE);

		if (!Next.IsWindowVisible())
		{
			rect.top += 16 * GetFontSize();
			rect.bottom = rect.top + GetFontSize() * 2;
			DrawText(hdc, String_Preparing, &rect, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
		}
	}

	void OnOK()
	{
		RemoveWindowStyle(WS_SYSMENU);
		Next.ShowWindow(SW_HIDE);
		Edition.EnableWindow(false);
		Invalidate();
		Progress.SetMarqueeProgressBar(true);
		if (pTaskbarList)
			pTaskbarList->SetProgressState(GetParent()->GetHandle(), TBPF_INDETERMINATE);
		thread(RunSetup, this, GetParent()->PreventUpdating.GetCheck() == BST_CHECKED, GetParent()->NoReboot.GetCheck() == BST_CHECKED).detach();
	}

	bool OnClose()
	{
		Cancel = true;
		if (Next.IsWindowEnabled() && Next.IsWindowVisible())
			EndDialog(PreparationDialogResult::CanceledOrFailed);
		return true;
	}
};

struct ProgressContext
{
	ProgressBar* Progress;
	ITaskbarList4* pTaskbarList;
	HWND hWnd;
	bool& Cancel;

	void SetProgress(UINT uCurrent)
	{
		Progress->SetPos(uCurrent);
		if (pTaskbarList)
			pTaskbarList->SetProgressValue(hWnd, uCurrent, kProgressMax);
	}
};

static DWORD CALLBACK MyWIMMessageCallback20(DWORD dwMessageId, WPARAM wParam, LPARAM lParam, ProgressContext* ctx)
{
	if (dwMessageId == WIM_MSG_PROGRESS)
		ctx->SetProgress(static_cast<UINT>(wParam) * kProgressStage1Max / 100);
	else if (ctx->Cancel)
		return WIM_MSG_ABORT_IMAGE;
	return WIM_MSG_SUCCESS;
}

static DWORD CALLBACK MyWIMMessageCallback75(DWORD dwMessageId, WPARAM wParam, LPARAM lParam, ProgressContext* ctx)
{
	if (dwMessageId == WIM_MSG_PROGRESS)
		ctx->SetProgress(kProgressStage2Base + static_cast<UINT>(wParam) * kProgressStage2Max / 100);
	else if (ctx->Cancel)
		return WIM_MSG_ABORT_IMAGE;
	return WIM_MSG_SUCCESS;
}

static bool DeployMediaSetup(PCWSTR pszBtPath, PCWSTR pszMediaPath)
{
	HANDLE hFile = CreateFileW(L"SetupHost.exe", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;
	DWORD dwFileSize = GetFileSize(hFile, nullptr);
	MyUniqueBuffer<PVOID> OriginalSetupHost = dwFileSize;
	if (!ReadFile(hFile, OriginalSetupHost, dwFileSize, nullptr, nullptr))
	{
		CloseHandle(hFile);
		return false;
	}

	SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);
	HRSRC hResInfo = FindResourceA(nullptr, MAKEINTRESOURCEA(File_FakeSetupHost), "FILE");
	if (!hResInfo
		|| !WriteFile(hFile, LockResource(LoadResource(nullptr, hResInfo)), SizeofResource(nullptr, hResInfo), nullptr, nullptr))
	{
		CloseHandle(hFile);
		return false;
	}
	SetEndOfFile(hFile);
	CloseHandle(hFile);

	PROCESS_INFORMATION pi;
	STARTUPINFO si = { sizeof(si) };
	if (!CreateProcessW(L"SetupPrep.exe", nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
		return false;
	else
		CloseHandle(pi.hThread);
	WaitForSingleObject(pi.hProcess, INFINITE);

	DWORD dwExitCode;
	GetExitCodeProcess(pi.hProcess, &dwExitCode);
	CloseHandle(pi.hProcess);

	hFile = INVALID_HANDLE_VALUE;
	if (dwExitCode != kFakeSetupHostSuccessExitCode
		|| !SetCurrentDirectoryW(pszMediaPath)
		|| !MergeDirectory(L"Media", pszBtPath)
		|| !SetCurrentDirectoryW(pszBtPath)
		|| !SetCurrentDirectoryW(L"sources")
		|| (hFile = CreateFileW(L"SetupHost.exe", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)) == INVALID_HANDLE_VALUE)
	{
		if (dwExitCode != kFakeSetupHostSuccessExitCode)
			SetLastError(ERROR_PROCESS_ABORTED);
		return false;
	}

	DWORD dwWritten;
	WriteFile(hFile, OriginalSetupHost, dwFileSize, &dwWritten, nullptr);
	CloseHandle(hFile);

	return dwWritten == dwFileSize && RemoveDirectoryW(pszMediaPath);
}

static bool MoveFileAndMui(PCWSTR pszSource, PCWSTR pszDestination)
{
	if (!MoveFileW(pszSource, pszDestination))
		return false;

	std::error_code ec;
	path WorkingDir = current_path(ec);
	if (ec) return true;

	wstring SourceMuiFile = wstring(pszSource) += L".mui";
	wstring DestinationMuiFile = wstring(pszDestination) += L".mui";

	directory_iterator iter(WorkingDir, ec);
	if (ec) return true;
	for (const auto& entry : iter)
		if (entry.is_directory())
		{
			auto SourceMuiPath = entry.path() / SourceMuiFile;
			if (!exists(SourceMuiPath))
				continue;
			MoveFileW(SourceMuiPath.c_str(), (entry.path() / DestinationMuiFile).c_str());
		}

	return true;
}

static void ApplyBypasses()
{
	VersionStruct version;
	// Windows 11 24H2 greater
	if (!GetFileVersion(L"appraiserres.dll", version)
		|| version.dwBuild >= 26100)
	{
		RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\CompatMarkers");
		RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Shared");
		RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\TargetVersionUpgradeExperienceIndicators");

		HKEY hKey;
		if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\HwReqChk", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
		{
			constexpr WCHAR lpBypasses[] = L"SQ_SecureBootCapable=TRUE\0SQ_SecureBootEnabled=TRUE\0SQ_TpmVersion=2\0SQ_RamMB=8192\0";
			RegSetValueExW(hKey, L"HwReqChkVars", 0, REG_MULTI_SZ, reinterpret_cast<const BYTE*>(lpBypasses), sizeof(lpBypasses));
			RegCloseKey(hKey);
		}

		if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\Setup\\MoSetup", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
		{
			DWORD dwTrue = 1;
			RegSetValueExW(hKey, L"AllowUpgradesWithUnsupportedTPMOrCPU", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dwTrue), sizeof(dwTrue));
			RegCloseKey(hKey);
		}
	}

	HANDLE hFile = CreateFileW(L"appraiserres.dll", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	CloseHandle(hFile);
}

static void PreparationThread(SessionContext& ctx, Dialog* pDlg, ITaskbarList4* pTaskbarList, ProgressBar* pProgress, bool bMediaSetup, bool* Cancel)
{
	WCHAR szPath[MAX_PATH], szMediaPath[MAX_PATH];
	GetWindowsBt(szPath);
	GUID guid;
	CoCreateGuid(&guid);
	wcsncpy_s(szMediaPath, szPath, 3);
	wcscat_s(szMediaPath, GUID2String(guid));

	ProgressContext pctx{ pProgress, pTaskbarList, static_cast<PreparationDialog*>(pDlg)->GetParent()->GetHandle(), *Cancel };

	auto EndCanceledOrFailed = [&]()
		{
			pDlg->EndDialog(PreparationDialogResult::CanceledOrFailed);
		};

	auto ShowErrorUnlessCanceled = [&]()
		{
			if (!*Cancel)
				pDlg->ErrorMessageBox();
		};

	// Early failure cleanup: only remove the GUID staging directory, do not touch $WINDOWS.~BT.
	auto CleanupStaging = [&]()
		{
			WCHAR szSystemPath[MAX_PATH];
			GetSystemDirectoryW(szSystemPath, MAX_PATH);
			SetCurrentDirectoryW(szSystemPath);
			DeleteDirectory(szMediaPath);
			if (pTaskbarList)
				pTaskbarList->SetProgressState(pctx.hWnd, TBPF_NOPROGRESS);
		};

	auto Cleanup = [&]()
		{
			CleanupStaging();
			ForceDeleteDirectory(szPath);
		};

	if (!SetCurrentDirectoryW(ctx.PathUUP.c_str()))
	{
		pDlg->ErrorMessageBox();
		EndCanceledOrFailed();
		return;
	}

	HANDLE hWim = WIMCreateFile(ctx.TargetImageInfo.SystemESD.c_str(), WIM_GENERIC_READ, OPEN_EXISTING, WIM_FLAG_ALLOW_LZMS, 0, nullptr);
	if (!hWim
		|| !WIMSetTemporaryPath(hWim, ctx.PathTemp.c_str())
		|| !CreateDirectoryW(szMediaPath, nullptr)
		|| !SetFileAttributesW(szMediaPath, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)
		|| !SetCurrentDirectoryW(szMediaPath)
		|| !CreateDirectoryW(L"Media", nullptr))
	{
		ShowErrorUnlessCanceled();
		WimCloseHandleIfValid(hWim);
		CleanupStaging();
		EndCanceledOrFailed();
		return;
	}

	HANDLE hImage = WIMLoadImage(hWim, 1);
	WIMRegisterMessageCallback(hWim, reinterpret_cast<FARPROC>(MyWIMMessageCallback20), &pctx);
	if (!hImage
		|| !WIMApplyImage(hImage, L"Media", 0)
		|| !SetCurrentDirectoryW(L"Media")
		|| !SetCurrentDirectoryW(L"sources"))
	{
		ShowErrorUnlessCanceled();
		WimCloseHandleIfValid(hImage);
		WIMCloseHandle(hWim);
		Cleanup();
		EndCanceledOrFailed();
		return;
	}
	WIMCloseHandle(hImage);
	pctx.SetProgress(kProgressStage1Max);

	if (!bMediaSetup)
	{
		if (!WriteFileResourceToFile(L"Install.esd", File_EmptyESD))
		{
			ShowErrorUnlessCanceled();
			WIMCloseHandle(hWim);
			Cleanup();
			EndCanceledOrFailed();
			return;
		}

		auto WimlibErrorBox = [&](int c)
			{
				wstring txt = L"Wimlib Error: %d";
				txt += to_wstring(c);
				txt += '\n';
				txt += wimlib_get_error_string(static_cast<wimlib_error_code>(c));
				pDlg->MessageBox(txt.c_str(), nullptr, MB_ICONERROR);
			};

		pctx.SetProgress(kProgressStage1Max + 2 * kProgressPerPercent);
		if (!DeployMediaSetup(szPath, szMediaPath))
		{
			ShowErrorUnlessCanceled();
			WIMCloseHandle(hWim);
			Cleanup();
			EndCanceledOrFailed();
			return;
		}

#ifdef _AMD64_
		constexpr auto File_DllFdHook = File_DllFdHookAmd64;
#elif defined(_ARM64_)
		constexpr auto File_DllFdHook = File_DllFdHookArm64;
#else
#error "Unsupported architecture"
#endif
		if (!MoveFileAndMui(L"wimgapi.dll", L"realwimgapi.dll")
			|| !WriteFileResourceToFile(L"wimgapi.dll", File_FakeWimgAPI)
			|| !WriteFileResourceToFile(L"dllfdhook.dll", File_DllFdHook))
		{
			if (pctx.pTaskbarList)
				pctx.pTaskbarList->SetProgressState(pctx.hWnd, TBPF_ERROR);
			ShowErrorUnlessCanceled();
			WIMCloseHandle(hWim);
			Cleanup();
			EndCanceledOrFailed();
			return;
		}
	}

	if (!ctx.SetupUpdate.Empty()
		&& ctx.bAddSetupUpdate
		&& !ExpandCabFile(ctx.SetupUpdate.GetPointer(), nullptr, nullptr, nullptr))
	{
		if (pctx.pTaskbarList)
			pctx.pTaskbarList->SetProgressState(pctx.hWnd, TBPF_ERROR);
		ShowErrorUnlessCanceled();
		WIMCloseHandle(hWim);
		Cleanup();
		EndCanceledOrFailed();
		return;
	}
	pctx.SetProgress(kProgressStage2Base);

	SetReferenceFiles(hWim, ctx.PathTemp);
	HANDLE hWim2 = WIMCreateFile(L"Install.esd", WIM_GENERIC_READ | GENERIC_WRITE, CREATE_ALWAYS, WIM_FLAG_ALLOW_LZMS, WIM_COMPRESS_LZMS, nullptr);
	if (hWim2)
		WIMRegisterMessageCallback(hWim2, reinterpret_cast<FARPROC>(MyWIMMessageCallback75), &pctx);
	hImage = nullptr;
	if (!hWim2
		|| !WIMSetTemporaryPath(hWim2, ctx.PathTemp.c_str())
		|| !(hImage = WIMLoadImage(hWim, 3))
		|| !WIMExportImage(hImage, hWim2, 0))
	{
		if (pctx.pTaskbarList)
			pctx.pTaskbarList->SetProgressState(pctx.hWnd, TBPF_ERROR);
		ShowErrorUnlessCanceled();
		WimCloseHandleIfValid(hImage);
		WIMCloseHandle(hWim);
		WimCloseHandleIfValid(hWim2);
		Cleanup();
		EndCanceledOrFailed();
		return;
	}
	pctx.SetProgress(kProgressMax);
	WIMCloseHandle(hImage);
	WIMCloseHandle(hWim2);
	WIMCloseHandle(hWim);
	if (!bMediaSetup)
		static_cast<PreparationDialog*>(pDlg)->Next.EnableWindow(true);
	else
	{
		if (static_cast<DialogEx2<InPlaceSetupWizard>*>(pDlg)->GetParent()->RemoveHwReq.GetCheck() == BST_CHECKED)
			ApplyBypasses();
		EnableWindow(GetDlgItem(pDlg->GetHandle(), IDOK), TRUE);
		wcscpy_s(reinterpret_cast<PWSTR>(Cancel) - MAX_PATH, MAX_PATH, szMediaPath);
		Cancel[1] = true; // CanClose = true
	}
}

static bool CreateProcessInParentsName(PCWSTR pszPath, const wstring& CommandLine, HANDLE& hProcess)
{
	PROCESS_INFORMATION pi;

	struct SIX : STARTUPINFOEXW
	{
		SIX() : STARTUPINFOEXW{ .StartupInfo = { sizeof(STARTUPINFOEXW) } }
		{
			SIZE_T Size = 0;
			InitializeProcThreadAttributeList(nullptr, 1, 0, &Size);
			lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(malloc(Size));
			if (!InitializeProcThreadAttributeList(lpAttributeList, 1, 0, &Size))
			{
				free(lpAttributeList);
				lpAttributeList = nullptr;
			}
		}

		~SIX()
		{
			if (lpAttributeList)
			{
				DeleteProcThreadAttributeList(lpAttributeList);
				free(lpAttributeList);
			}
		}
	}si;

	if (!si.lpAttributeList)
		return false;

	if (!UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PARENT_PROCESS, &g_pHostContext->hParent, sizeof(hProcess), nullptr, nullptr))
		return false;

	auto result = CreateProcessW(pszPath, const_cast<PWSTR>(CommandLine.c_str()), nullptr, nullptr, FALSE, EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED, nullptr, nullptr, &si.StartupInfo, &pi);
	if (result)
	{
		ResumeThread(pi.hThread);
		CloseHandle(pi.hThread);
		hProcess = pi.hProcess;
	}

	return result;
}

static void RunSetup(PreparationDialog* pDlg, bool bPreventUpdating, bool bNoReboot)
{
	auto EditionID = pDlg->Edition.GetWindowText();

	WIMStruct* wim;
	wimlib_open_wim(L"Install.esd", 0, &wim);
	wstring src = L"\\Windows\\System32\\Licenses\\neutral\\_Default\\";
	wstring dst = src;
	src += pDlg->GetParent()->ctx.TargetImageInfo.Edition;
	dst += EditionID;
	if (src != dst)
	{
		wimlib_set_image_property(wim, 1, L"WINDOWS/EDITIONID", EditionID);
		wstring name = wimlib_get_image_name(wim, 1);
		for (auto i = name.rbegin(); i != name.rend(); ++i)
			if (*i >= '0' && *i <= '9')
			{
				if (i != name.rbegin())
					i[-1] = 0;
				break;
			}
		wimlib_set_image_name(wim, 1, name.c_str());
		if (wimlib_rename_path(wim, 1, src.c_str(), dst.c_str()) == 0)
		{
			HANDLE hFile = CreateFileW(L".edition", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			WriteFile(hFile, EditionID, static_cast<DWORD>(EditionID.GetLength() * sizeof(WCHAR)), nullptr, nullptr);
			CloseHandle(hFile);
		}
		wimlib_overwrite(wim, WIMLIB_WRITE_FLAG_NO_CHECK_INTEGRITY, 0);
	}
	wimlib_free(wim);

	HANDLE hFile = CreateFileW(L".uupassistant_data_exchange", GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, 0, nullptr);

	WCHAR szPath[MAX_PATH];
	MoveFileW(L"SetupPlatform.exe", L"SetupPlatform.exe");
	GetWindowsBt(szPath);

	if (pDlg->GetParent()->RemoveHwReq.GetCheck() == BST_CHECKED)
		ApplyBypasses();

	wstring CommandLine = format(L" /Install /Media  /InstallFile \"{}\\Sources\\Install.esd\" /MediaPath \"{}\""
#ifdef _DEBUG
		L" /DiagnosticPrompt Enable"
#endif // _DEBUG
		, szPath, szPath);
	if (bPreventUpdating)
		CommandLine += L" /DynamicUpdate Disable";
	if (bNoReboot)
		CommandLine += L" /NoReboot";

	auto& AdditionalEditions = pDlg->GetParent()->ctx.TargetImageInfo.AdditionalEditions;
	if (find(AdditionalEditions.begin(), AdditionalEditions.end(), EditionID.GetPointer()) == AdditionalEditions.end())
		CommandLine += L" /EULA accept";

	if (!CreateProcessInParentsName(L"SetupHost.exe", CommandLine, *pDlg->phProcess))
	{
		pDlg->ErrorMessageBox();
		pDlg->EndDialog(PreparationDialogResult::CanceledOrFailed);
		CloseHandle(hFile);
		return;
	}
	*pDlg->phFile = hFile;

	GUID guid;
	CoCreateGuid(&guid);
	auto guidString = GUID2String(guid);
	WriteFile(hFile, guidString, static_cast<DWORD>(guidString.GetLength() * sizeof(WCHAR)), nullptr, nullptr);
	FlushFileBuffers(hFile);
	pDlg->EndDialog(PreparationDialogResult::LaunchedSetupAndExit);
	DeleteDirectory((pDlg->GetParent()->ctx.PathTemp + L"RefESDs").c_str());
}

struct MediaSetupWizard;

static void CompactibilityCheck(MediaSetupWizard*);

#pragma pack(push, 1)
struct MediaSetupWizard : DialogEx2<InPlaceSetupWizard>
{
	MediaSetupWizard(InPlaceSetupWizard* Parent) : DialogEx2(Parent, GetFontSize() * 30, GetFontSize() * 20, WS_CAPTION | WS_SYSMENU | DS_FIXEDSYS | DS_MODALFRAME, GetString(String_InstallWindows)),
		UpgradeMaps(GetUpgradeMaps(GetParent()->ctx)), EditionID(GetVersionValue(L"SOFTWARE", L"EditionID")),
		pTaskbarList(nullptr), IgnoreWarnings(this, 0, ButtonStyle::AutoCheckbox), SkipCompatibilityCheck(this, &MediaSetupWizard::SkipCompatScan, ButtonStyle::AutoCheckbox),
		Edition(this), Next(this, IDOK), Progress(this), StateDetail(this, 0, true),
		Upgrade(this, &MediaSetupWizard::MigChoiceChanged, ButtonStyle::AutoRadioButton), DataOnly(this, &MediaSetupWizard::MigChoiceChanged, ButtonStyle::AutoRadioButton), Clean(this, &MediaSetupWizard::MigChoiceChanged, ButtonStyle::AutoRadioButton),
		SettingOOBE(this), SetOOBE(this, ButtonStyle::CommandLink, Random(), &Dialog::ModalDialogBox, &SettingOOBE, 0), InstallDotNetFx3(this, Random(), ButtonStyle::AutoCheckbox), State(this)
	{
		if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pTaskbarList))))
			pTaskbarList->HrInit();
	}

	void MigChoiceChanged()
	{
		if (Upgrade.GetCheck() == BST_CHECKED)
		{
			SetOOBE.EnableWindow(false);
			InstallDotNetFx3.SetCheck(BST_UNCHECKED);
			InstallDotNetFx3.EnableWindow(false);
		}
		else if (DataOnly.GetCheck() == BST_CHECKED)
		{
			SetOOBE.EnableWindow(false);
			InstallDotNetFx3.EnableWindow(true);
		}
		else
		{
			SetOOBE.EnableWindow(true);
			InstallDotNetFx3.EnableWindow(true);
		}
	}

	bool OnClose()
	{
		if (CanClose)
			EndDialog(PreparationDialogResult::CanceledOrFailed);
		else
			Cancel = true;
		return true;
	}

	void SkipCompatScan()
	{
		if (SkipCompatibilityCheck.GetCheck() == BST_CHECKED)
		{
			IgnoreWarnings.SetCheck(BST_CHECKED);
			IgnoreWarnings.EnableWindow(false);
		}
		else
			IgnoreWarnings.EnableWindow(true);
	}

	void OnOK()
	{
		CanClose = false;
		if (Edition.IsWindowVisible())
		{
			Next.ShowWindow(SW_HIDE);
			IgnoreWarnings.EnableWindow(false);
			Edition.EnableWindow(false);
			SkipCompatibilityCheck.EnableWindow(false);
			Progress.SetMarqueeProgressBar();
			if (pTaskbarList)
				pTaskbarList->SetProgressState(GetParent()->GetHandle(), TBPF_INDETERMINATE);

			if (IsDowngrade(GetParent()))
			{
				Upgrade.EnableWindow(false);
				DataOnly.EnableWindow(false);
			}
			else
			{
				bool dataOnly = false, fullUpgrade = false;
				CheckUpgradeMaps(UpgradeMaps, Edition.GetWindowText().GetPointer(), EditionID, dataOnly, fullUpgrade);
				DataOnly.EnableWindow(dataOnly);
				Upgrade.EnableWindow(fullUpgrade);
			}
			Invalidate();
			thread(CompactibilityCheck, this).detach();
		}
		else
		{
			Next.ShowWindow(SW_HIDE);
			Upgrade.EnableWindow(false);
			DataOnly.EnableWindow(false);
			Clean.EnableWindow(false);
			SetOOBE.EnableWindow(false);
			InstallDotNetFx3.EnableWindow(false);
			Progress.SetMarqueeProgressBar();
			if (pTaskbarList)
				pTaskbarList->SetProgressState(GetParent()->GetHandle(), TBPF_INDETERMINATE);
			Invalidate();
			thread(CompactibilityCheck, this).detach();
		}
	}

	void Init()
	{
		CenterWindow(Parent);
		Upgrade.SetWindowText(GetString(String_KeepDataAndApps));
		DataOnly.SetWindowText(GetString(String_DataOnly));
		Clean.SetWindowText(GetString(String_CleanInstallation));
		SkipCompatibilityCheck.SetWindowText(GetString(String_SkipCompatScan));
		IgnoreWarnings.SetWindowText(GetString(String_IgnoreWarnings));
		SetOOBE.SetWindowText(GetString(String_OOBESettings));
		InstallDotNetFx3.SetWindowText(GetString(String_InstallDotNetFx3));
		Progress.SetRange(0, kProgressMax);
		StateDetail.ShowWindow(SW_HIDE);
		StateDetail.MoveWindow(0, GetFontSize() * 4, GetFontSize() * 30, GetFontSize() * 15);
		StateDetail.SetReadOnly(true);
		StateDetail.SetLimitText(-1);

		for (auto& i : GetParent()->ctx.TargetImageInfo.UpgradableEditions)
			Edition.AddString(i.c_str());

		if (find(GetParent()->ctx.TargetImageInfo.AdditionalEditions.begin(),
			GetParent()->ctx.TargetImageInfo.AdditionalEditions.end(),
			EditionID) == GetParent()->ctx.TargetImageInfo.AdditionalEditions.end())
		{
			int index = Edition.FindString(EditionID.c_str());
			if (index != -1)
				Edition.SetCurSel(index);
			else
				Edition.SetCurSel(Edition.FindString(GetParent()->ctx.TargetImageInfo.Edition.c_str()));
		}
		else
			Edition.SetCurSel(Edition.FindString(GetParent()->ctx.TargetImageInfo.Edition.c_str()));
		Progress.MoveWindow(0, GetFontSize() * 19, GetFontSize() * 30, GetFontSize());
		Edition.MoveWindow(GetFontSize() * 7, GetFontSize() * 10, GetFontSize() * 16, GetFontSize() * 2);
		Next.SetWindowText(GetString(String_Next));
		Next.EnableWindow(false);
		Next.MoveWindow(GetFontSize() * 23, GetFontSize() * 16, GetFontSize() * 6, GetFontSize() * 2);
		SIZE size;
		SkipCompatibilityCheck.GetIdealSize(&size);
		SkipCompatibilityCheck.MoveWindow(GetFontSize(), GetFontSize() * 19 - size.cy, size.cx, size.cy);
		IgnoreWarnings.GetIdealSize(&size);
		IgnoreWarnings.MoveWindow(GetFontSize(), GetFontSize() * 19 - static_cast<int>(size.cy * 2.2), size.cx, size.cy);

		if (pTaskbarList)
			pTaskbarList->SetProgressState(GetParent()->GetHandle(), TBPF_NORMAL);
		thread(PreparationThread, ref(GetParent()->ctx), this, pTaskbarList.get(), &Progress, true, &Cancel).detach();
	}

	void OnDraw(HDC hdc, RECT rect)
	{
		if (!StateDetail.IsWindowVisible())
		{
			if (Next.IsWindowVisible())
			{
				if (Edition.IsWindowVisible())
				{
					rect.top = GetFontSize();
					rect.right -= rect.left = rect.top;
					DrawText(hdc, String_MediaSetupNotice, &rect, DT_WORDBREAK);
				}
			}
			else if (SkipCompatibilityCheck.GetCheck() == BST_UNCHECKED)
			{
				rect.top += 13 * GetFontSize();
				rect.bottom = rect.top + GetFontSize() * 2;
				DrawText(hdc, String_RunningCompactibilityScans, &rect, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
			}
		}
	}

	UpgradeMaps_t UpgradeMaps;
	wstring EditionID;
	com_ptr<ITaskbarList4> pTaskbarList;

	Edit StateDetail;
	Static State;
	Button SkipCompatibilityCheck;
	Button IgnoreWarnings;
	ComboBox Edition;
	Button Next;
	ProgressBar Progress;
	Button Upgrade;
	Button DataOnly;
	Button Clean;
	Button InstallDotNetFx3;
	Button SetOOBE;
	SettingOOBEDlg SettingOOBE;

	WCHAR szPath[MAX_PATH] = {};
	bool Cancel = false;
	bool CanClose = false;
};
#pragma pack(pop)

static enum wimlib_progress_status
wimlib_progress_func(enum wimlib_progress_msg msg_type,
	union wimlib_progress_info* info,
	void* progctx)
{
	if (*reinterpret_cast<bool*>(progctx))
		return WIMLIB_PROGRESS_STATUS_ABORT;
	return WIMLIB_PROGRESS_STATUS_CONTINUE;
}

static void CompactibilityCheck(MediaSetupWizard* p)
{
	if (p->Edition.IsWindowVisible())
	{
		auto EditionID = p->Edition.GetWindowText();
		if (find(p->GetParent()->ctx.TargetImageInfo.UpgradableEditions.begin(),
			p->GetParent()->ctx.TargetImageInfo.UpgradableEditions.end(),
			EditionID.GetPointer()) == p->GetParent()->ctx.TargetImageInfo.UpgradableEditions.end())
			p->Upgrade.EnableWindow(false);

		HANDLE hFile = CreateFileW(L"EI.cfg", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		WriteFile(hFile, "[EditionID]\r\n", 13, nullptr, nullptr);
		WriteFile(hFile, string(EditionID.begin(), EditionID.end()).c_str(), static_cast<DWORD>(EditionID.GetLength()), nullptr, nullptr);
		CloseHandle(hFile);

		if (p->SkipCompatibilityCheck.GetCheck() == BST_UNCHECKED)
		{
			wstring src = L"\\Windows\\System32\\Licenses\\neutral\\_Default\\";
			wstring dst = src;
			src += p->GetParent()->ctx.TargetImageInfo.Edition;
			dst += EditionID;

			auto ModifyLicense = [&]()
				{
					if (src != dst)
					{
						WIMStruct* wim;
						wimlib_open_wim(L"Install.esd", 0, &wim);
						wimlib_set_image_property(wim, 1, L"WINDOWS/EDITIONID", PathFindFileNameW(dst.c_str()));
						wimlib_rename_path(wim, 1, src.c_str(), dst.c_str());
						wimlib_register_progress_function(wim, wimlib_progress_func, &p->Cancel);
						wimlib_overwrite(wim, WIMLIB_WRITE_FLAG_NO_CHECK_INTEGRITY, 0);
						wimlib_free(wim);
					}
				};

			ModifyLicense();

			if (p->Cancel)
			{
				p->EndDialog(PreparationDialogResult::CanceledOrFailed);
				if (p->pTaskbarList)
					p->pTaskbarList->SetProgressState(p->GetParent()->GetHandle(), TBPF_NOPROGRESS);
				return;
			}

			STARTUPINFOW si = { sizeof(si) };
			PROCESS_INFORMATION pi;
			wstring CmdLine = L" /DynamicUpdate Disable /Auto Clean /EULA accept /Compat ScanOnly /Quiet";
			if (p->IgnoreWarnings.GetCheck() == BST_CHECKED)
				CmdLine += L" /Compat IgnoreWarning";
			if (!CreateProcessW(L"SetupPrep.exe", const_cast<PWSTR>(CmdLine.c_str()), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
			{
				swap(src, dst);
				ModifyLicense();
				p->ErrorMessageBox();
				p->Next.ShowWindow(SW_SHOW);
				p->Edition.EnableWindow(true);
				p->CanClose = true;
				p->IgnoreWarnings.EnableWindow(true);
				p->SkipCompatibilityCheck.EnableWindow(true);
				if (p->pTaskbarList)
					p->pTaskbarList->SetProgressState(p->GetParent()->GetHandle(), TBPF_NOPROGRESS);
				return;
			}

			CloseHandle(pi.hThread);
			while (WaitForSingleObject(pi.hProcess, 50) == WAIT_TIMEOUT)
				if (p->Cancel)
				{
					KillChildren(pi.hProcess);
					TerminateProcess(pi.hProcess, 0);
					CloseHandle(pi.hProcess);
					p->EndDialog(PreparationDialogResult::CanceledOrFailed);
					if (p->pTaskbarList)
						p->pTaskbarList->SetProgressState(p->GetParent()->GetHandle(), TBPF_NOPROGRESS);
					return;
				}
			DWORD dwExitCode;
			GetExitCodeProcess(pi.hProcess, &dwExitCode);
			CloseHandle(pi.hProcess);

			if (dwExitCode != MOSETUP_E_COMPAT_SCANONLY)
			{
				HMODULE hModule = LoadLibraryExA("SetupCore.dll", nullptr, LOAD_LIBRARY_AS_IMAGE_RESOURCE);
				LPWSTR lpBuffer = nullptr;
				FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM, hModule, dwExitCode, 0, reinterpret_cast<LPWSTR>(&lpBuffer), 0, nullptr);
				wstring txt = lpBuffer;
				LocalFree(lpBuffer);
				txt += GetString(String_ViewSetupReport);
				if (p->MessageBox(txt.c_str(), nullptr, MB_ICONERROR | MB_YESNO) == IDYES)
				{
					CmdLine.erase(CmdLine.find(L"/Quiet"), 6);
					CreateProcessW(L"SetupPrep.exe", const_cast<PWSTR>(CmdLine.c_str()), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
					CloseHandle(pi.hThread);
					WaitForSingleObject(pi.hProcess, INFINITE);
					DWORD dwExitCode2;
					GetExitCodeProcess(pi.hProcess, &dwExitCode2);
					if (dwExitCode == MOSETUP_E_COMPAT_SCANONLY)
					{
						FreeLibrary(hModule);
						goto NextPage;
					}
					else if (dwExitCode != dwExitCode2)
					{
						FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM, hModule, dwExitCode2, 0, reinterpret_cast<LPWSTR>(&lpBuffer), 0, nullptr);
						p->MessageBox(lpBuffer, nullptr, MB_ICONERROR);
						LocalFree(lpBuffer);
					}
					CloseHandle(pi.hProcess);
				}
				FreeLibrary(hModule);
				p->Edition.EnableWindow(true);
				if (p->pTaskbarList)
					p->pTaskbarList->SetProgressState(p->GetParent()->GetHandle(), TBPF_NOPROGRESS);
				p->Next.EnableWindow(false);
				p->Next.ShowWindow(SW_SHOW);
				p->Invalidate();
				swap(src, dst);
				ModifyLicense();
				p->Progress.SetMarqueeProgressBar(false);
				p->Progress.SetPos(kProgressMax);
				p->Next.EnableWindow(true);
				p->SkipCompatibilityCheck.EnableWindow(true);
				p->IgnoreWarnings.EnableWindow(true);
				p->CanClose = true;
				return;
			}
		}
		else
		{
			p->Next.ShowWindow(SW_SHOW);
			p->Edition.ShowWindow(SW_HIDE);
		}

	NextPage:
		SIZE size;
		p->Upgrade.GetIdealSize(&size);
		p->Upgrade.MoveWindow(GetFontSize() * 4, GetFontSize() * 2, size.cx, size.cy);
		p->DataOnly.GetIdealSize(&size);
		p->DataOnly.MoveWindow(GetFontSize() * 4, GetFontSize() * 4, size.cx, size.cy);
		p->Clean.GetIdealSize(&size);
		p->Clean.MoveWindow(GetFontSize() * 4, GetFontSize() * 6, size.cx, size.cy);
		p->InstallDotNetFx3.GetIdealSize(&size);
		p->InstallDotNetFx3.MoveWindow(GetFontSize(), GetFontSize() * 12, size.cx, size.cy);
		p->SetOOBE.GetIdealSize(&size);
		p->SetOOBE.MoveWindow(GetFontSize(), GetFontSize() * 15, GetFontSize() * 20, size.cy);
		p->SkipCompatibilityCheck.ShowWindow(SW_HIDE);
		p->Next.ShowWindow(SW_SHOW);
		p->Edition.ShowWindow(SW_HIDE);
		p->IgnoreWarnings.ShowWindow(SW_HIDE);
		p->SkipCompatibilityCheck.ShowWindow(SW_HIDE);

		if (p->Upgrade.IsWindowEnabled())
			p->Upgrade.SetCheck(BST_CHECKED);
		else if (p->DataOnly.IsWindowEnabled())
			p->DataOnly.SetCheck(BST_CHECKED);
		else
			p->Clean.SetCheck(BST_CHECKED);
		p->MigChoiceChanged();

		p->Progress.SetMarqueeProgressBar(false);
		p->Progress.SetPos(kProgressMax);
		p->CanClose = true;
		if (p->pTaskbarList)
			p->pTaskbarList->SetProgressState(p->GetParent()->GetHandle(), TBPF_NOPROGRESS);
		p->Invalidate();
	}
	else if (p->Clean.GetCheck() == BST_UNCHECKED)
	{
		if (p->SkipCompatibilityCheck.GetCheck() == BST_CHECKED)
			goto Execute;

		STARTUPINFOW si = { sizeof(si) };
		PROCESS_INFORMATION pi;
		WCHAR szCmdLine[98] = L" /DynamicUpdate Disable /EULA accept /Compat ScanOnly /Auto ";
		if (p->Upgrade.GetCheck() == BST_CHECKED)
			wcscat_s(szCmdLine, L"Upgrade");
		else if (p->DataOnly.GetCheck() == BST_CHECKED)
			wcscat_s(szCmdLine, L"DataOnly");
		wcscat_s(szCmdLine, L" /Quiet");
		if (p->IgnoreWarnings.GetCheck() == BST_CHECKED)
			wcscat_s(szCmdLine, L" /Compat IgnoreWarning");
		if (!CreateProcessW(L"SetupPrep.exe", szCmdLine, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
		{
			p->ErrorMessageBox();
			p->EndDialog(PreparationDialogResult::CanceledOrFailed);
			p->CanClose = true;
			if (p->pTaskbarList)
				p->pTaskbarList->SetProgressState(p->GetParent()->GetHandle(), TBPF_NOPROGRESS);
			return;
		}

		CloseHandle(pi.hThread);
		while (WaitForSingleObject(pi.hProcess, 50) == WAIT_TIMEOUT)
			if (p->Cancel)
			{
				KillChildren(pi.hProcess);
				TerminateProcess(pi.hProcess, 0);
				CloseHandle(pi.hProcess);
				p->EndDialog(PreparationDialogResult::CanceledOrFailed);
				if (p->pTaskbarList)
					p->pTaskbarList->SetProgressState(p->GetParent()->GetHandle(), TBPF_NOPROGRESS);
				return;
			}
		DWORD dwExitCode;
		GetExitCodeProcess(pi.hProcess, &dwExitCode);
		CloseHandle(pi.hProcess);

		if (dwExitCode != MOSETUP_E_COMPAT_SCANONLY)
		{
			HMODULE hModule = LoadLibraryExA("SetupCore.dll", nullptr, LOAD_LIBRARY_AS_IMAGE_RESOURCE);
			LPWSTR lpBuffer = nullptr;
			FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM, hModule, dwExitCode, 0, reinterpret_cast<LPWSTR>(&lpBuffer), 0, nullptr);
			wstring txt = lpBuffer;
			LocalFree(lpBuffer);
			txt += GetString(String_ViewSetupReport);
			if (p->MessageBox(txt.c_str(), nullptr, MB_ICONERROR | MB_YESNO) == IDYES)
			{
				wcsrchr(szCmdLine, L'/')[-1] = 0;
				CreateProcessW(L"SetupPrep.exe", szCmdLine, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
				CloseHandle(pi.hThread);
				WaitForSingleObject(pi.hProcess, INFINITE);
				DWORD dwExitCode2;
				GetExitCodeProcess(pi.hProcess, &dwExitCode2);
				CloseHandle(pi.hProcess);
				if (dwExitCode2 == MOSETUP_E_COMPAT_SCANONLY)
				{
					FreeLibrary(hModule);
					goto Execute;
				}
				else if (dwExitCode != dwExitCode2)
				{
					FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM, hModule, dwExitCode2, 0, reinterpret_cast<LPWSTR>(&lpBuffer), 0, nullptr);
					p->MessageBox(lpBuffer, nullptr, MB_ICONERROR);
					LocalFree(lpBuffer);
				}
			}
			FreeLibrary(hModule);

			if (p->Upgrade.GetCheck() == BST_CHECKED)
			{
				p->DataOnly.EnableWindow(true);
				p->Clean.EnableWindow(true);
				p->DataOnly.SetCheck(BST_CHECKED);
				p->Upgrade.SetCheck(BST_UNCHECKED);
			}
			else if (p->DataOnly.GetCheck() == BST_CHECKED)
			{
				p->Clean.EnableWindow(true);
				p->Clean.SetCheck(BST_CHECKED);
				p->DataOnly.SetCheck(BST_UNCHECKED);
			}
			p->Next.ShowWindow(SW_SHOW);
			p->MigChoiceChanged();
			p->Invalidate();
			p->Progress.SetMarqueeProgressBar(false);
			p->Progress.SetPos(kProgressMax);
			p->CanClose = true;
			if (p->pTaskbarList)
				p->pTaskbarList->SetProgressState(p->GetParent()->GetHandle(), TBPF_NOPROGRESS);
		}
		else
			goto Execute;
	}
	else
	{
	Execute:
		DeleteFileW(L"Install.esd");
		if (p->pTaskbarList)
			p->pTaskbarList->SetProgressState(p->GetParent()->GetHandle(), TBPF_INDETERMINATE);
		p->Progress.SetMarqueeProgressBar();
		p->Upgrade.ShowWindow(SW_HIDE);
		p->DataOnly.ShowWindow(SW_HIDE);
		p->Clean.ShowWindow(SW_HIDE);
		p->StateDetail.ShowWindow(SW_SHOW);
		p->SetOOBE.ShowWindow(SW_HIDE);
		p->InstallDotNetFx3.ShowWindow(SW_HIDE);

		WCHAR szPath[MAX_PATH];
		GetCurrentDirectoryW(MAX_PATH, szPath);
		SetCurrentDirectoryW(p->GetParent()->ctx.PathTemp.c_str());
		CreateDirectoryW(L"Temp", nullptr);
		wcscat_s(szPath, L"\\install.wim");
		if (p->GetParent()->ctx.bInstallEdge && p->Upgrade.GetCheck() == BST_CHECKED)
		{
			wstring text = GetString(String_CheckingEdgeVersions).GetPointer();
			p->StateDetail.SetWindowText(text.c_str());
			wstring CurrentEdgeVersion, CurrentWebView2Version, ImageEdgeVersion, ImageWebView2Version;
			bool bInst = CheckWhetherNeedToInstallMicrosoftEdge((p->GetParent()->ctx.PathUUP + L"Edge.wim").c_str(), p->GetParent()->ctx.PathTemp.c_str(), CurrentEdgeVersion, CurrentWebView2Version, ImageEdgeVersion, ImageWebView2Version);
			if (ImageEdgeVersion.empty() || ImageWebView2Version.empty() || ImageEdgeVersion.empty() || ImageWebView2Version.empty())
			{
				LPWSTR lpBuffer = nullptr;
				FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, nullptr, GetLastError(), 0, reinterpret_cast<LPWSTR>(&lpBuffer), 0, nullptr);
				text += lpBuffer;
				LocalFree(lpBuffer);
			}
			else
			{
				text += GetString(String_Succeeded);
				text += ResStrFormat(String_EdgeVersionStrings, CurrentEdgeVersion.c_str(), CurrentWebView2Version.c_str(), ImageEdgeVersion.c_str(), ImageWebView2Version.c_str());
				if (!bInst)
					p->GetParent()->ctx.bInstallEdge = false;
			}
			p->StateDetail.SetWindowText(text.c_str());
		}

		int nHeight = GetFontFullSize();
		p->State.MoveWindow(0, GetFontSize() * 2 - nHeight / 2, GetFontSize() * 30, nHeight);

		CreateImage(p->GetParent()->ctx, WIM_COMPRESS_LZX, szPath, nullptr, p->Cancel, false,
			std::make_unique<CreateImageContext>(p->State, p->StateDetail,
				p, true, vector<String>(1, p->Edition.GetWindowText())),
			[](bool succeeded) {});

		if (GetFileAttributesW(szPath) != INVALID_FILE_ATTRIBUTES)
		{
			WCHAR szMediaPath[MAX_PATH];
			GetWindowsBt(szMediaPath);
			szPath[55] = 0;
			SetCurrentDirectoryW(szPath);
			szPath[41] = 0;
			if (!DeployMediaSetup(szMediaPath, szPath))
				p->ErrorMessageBox();

			PCWSTR InstallType;
			if (p->Upgrade.GetCheck() == BST_CHECKED)
				InstallType = L"Upgrade";
			else if (p->DataOnly.GetCheck() == BST_CHECKED)
				InstallType = L"DataOnly";
			else
				InstallType = L"Clean";

			wstring CommandLine = format(L" /Install /Media  /InstallFile \"{}\\Sources\\install.wim\" /MediaPath \"{}\""
#ifdef _DEBUG
				L" /DiagnosticPrompt Enable"
#endif // _DEBUG
				L" /EULA accept /Auto {}"
				, szMediaPath, szMediaPath, InstallType);

			if (p->GetParent()->PreventUpdating.GetCheck() == BST_CHECKED)
				CommandLine += L" /DynamicUpdate Disable";
			if (p->GetParent()->NoReboot.GetCheck() == BST_CHECKED)
				CommandLine += L" /NoReboot";
			if (p->IgnoreWarnings.GetCheck() == BST_CHECKED)
				CommandLine += L" /Compat IgnoreWarning";

			auto Unattend = p->SettingOOBE.Settings.ToUnattendXml(p->GetParent()->ctx);
			if (!Unattend.empty() && CreateDirectoryW(L"Panther", nullptr))
			{
				HANDLE hFile = CreateFileW(L"Panther\\Unattend.xml", GENERIC_WRITE | DELETE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (hFile != INVALID_HANDLE_VALUE)
				{
					bool result = WriteFile(hFile, Unattend.c_str(), static_cast<DWORD>(Unattend.length()), nullptr, nullptr);
					if (!result)
					{
						p->ErrorMessageBox();
						DeleteFileOnClose(hFile);
					}
					CloseHandle(hFile);
				}
			}

			if (!CreateProcessInParentsName(L"SetupHost.exe", CommandLine, p->GetParent()->hProcess))
				p->ErrorMessageBox();
			else
				p->EndDialog(PreparationDialogResult::CleanupWindowsBt);
		}
		else
		{
			if (p->pTaskbarList)
				p->pTaskbarList->SetProgressState(p->GetParent()->GetHandle(), TBPF_NOPROGRESS);
			if (p->Cancel)
			{
				p->EndDialog(PreparationDialogResult::CanceledOrFailed);
				p->GetParent()->PostMessage(WM_CLOSE, 0, 0);
			}
			else
				p->CanClose = true;
		}
	}
}

void InPlaceSetupWizard::Execute()
{
	if (SynergisticInstallation.GetCheck() == BST_CHECKED)
		switch (PreparationDialog(this).ModalDialogBox(&hProcess, &hFile))
		{
		case PreparationDialogResult::LaunchedSetupAndExit:
			DestroyWindow();
			PostQuitMessage(0);
			break;
		case PreparationDialogResult::CanceledOrFailed:
			break;
		default:
			WCHAR szPath[MAX_PATH];
			GetWindowsBt(szPath);
			ForceDeleteDirectory(szPath);
		}
	else
	{
		MediaSetupWizard msw(this);
		if (msw.ModalDialogBox())
		{
			SetCurrentDirectoryW(ctx.PathTemp.c_str());
			if (msw.szPath[0] != 0)
				DeleteDirectory(msw.szPath);
			GetWindowsBt(msw.szPath);
			ForceDeleteDirectory(msw.szPath);
		}
		else
		{
			DestroyWindow();
			PostQuitMessage(0);
			hFile = INVALID_HANDLE_VALUE;
		}
	}
}
