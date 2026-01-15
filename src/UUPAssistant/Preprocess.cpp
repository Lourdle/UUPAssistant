#include "pch.h"
#include "misc.h"
#include "Resources/resource.h"
#include "global_features.h"
#include "Xml.h"
#include "../UUPFetcher/ExitCode.h"

#include <ShlObj.h>
#include <wimlib.h>
#include <Shlwapi.h>

#include <filesystem>
#include <thread>
#include <format>

using namespace Lourdle::UIFramework;
using namespace std;
using namespace filesystem;
using namespace rapidxml;

import CheckMiniNt;
import Constants;

constexpr UINT SetTaskbarProgressMsg = 0x7A59U;

static std::wstring EllipsizeForUi(std::wstring_view text)
{
	constexpr size_t kMaxLen = 40;
	constexpr size_t kHeadLen = 30;
	constexpr size_t kTailLen = 8;

	if (text.size() <= kMaxLen)
		return std::wstring(text);

	std::wstring result(text.begin(), text.begin() + kHeadLen);
	result += L"...";
	result.append(text.end() - kTailLen, text.end());
	return result;
}

DirSelection::DirSelection(SessionContext& ctx) : ctx(ctx),
Window(GetFontSize() * 40, GetFontSize() * 14, GetString(String_DirChoice), WS_CLIPCHILDREN | WS_BORDER | WS_SYSMENU | WS_MINIMIZEBOX | WS_CAPTION),
ButtonBrowse(this, &DirSelection::Browse, ButtonStyle::Text), ButtonMain(this, &DirSelection::MainButtonClicked, ButtonStyle::DefPushButton),
Path(this), Target(this), Purpose(this), AdditionEditions(this),
ProcessingProgress(this), FetchUUP(this, &DirSelection::Fetch, ButtonStyle::CommandLink),
ComboBoxSel(CB_ERR), CurrentFileProgress(0), wProcessed(0), wTotal(0), TotalProgress(0), pTaskbar(nullptr), State(UUP),
hEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr))
{
	bDoubleBuffer = true;
	RegisterCommand(&DirSelection::OnSwitchEdition, Target, Target.GetControlID(), CBN_SELCHANGE);

	ButtonMain.SetWindowText(GetString(State == ProcessingCab || State == ProcessingEsd || State == Cleaning ? String_Cancel : String_Next));
	ButtonBrowse.SetWindowText(GetString(String_Browse));
	Purpose.AddString(GetString(String_CreateImage));
	Purpose.AddString(GetString(String_InplaceInstallation));
	Purpose.AddString(GetString(String_InstallWindows));
	SetWindowText(GetString(String_DirChoice));

	const int nFontHeight = GetFontSize(hFont);
	const int nWidth = nFontHeight * 40;
	Path.MoveWindow(nFontHeight, nFontHeight * 2, nWidth - nFontHeight * 2, nFontHeight * 2);
	ButtonMain.MoveWindow(nWidth / 2 - nFontHeight * 3, nFontHeight * 10, nFontHeight * 6, nFontHeight * 2);
	ButtonBrowse.MoveWindow(nWidth - nFontHeight * 6, nFontHeight * 5, nFontHeight * 5, nFontHeight * 2);
	Purpose.SetCurSel(0);
	ProcessingProgress.SetRange(0, 10000);

	HMODULE hModule = LoadLibraryExA("UUPFetcher.exe", nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
	if (hModule)
	{
		FetchUUP.SetWindowText(GetString(String_UUPFetcherId, hModule));
		SIZE size;
		FetchUUP.GetIdealSize(&size);
		FetchUUP.MoveWindow(4, nFontHeight * 21 / 2, nFontHeight * 12, size.cy);
	}
}

DirSelection::~DirSelection()
{
	CloseHandle(hEvent);
}

LRESULT DirSelection::WindowProc(UINT Msg, WPARAM wParam, LPARAM lParam)
{
	if (Msg == SetTaskbarProgressMsg)
	{
		if (!pTaskbar
			&& CoCreateInstance(
				CLSID_TaskbarList,
				0,
				CLSCTX_INPROC_SERVER,
				IID_PPV_ARGS(&pTaskbar)) == S_OK)
			pTaskbar->HrInit();
		if (pTaskbar)
			pTaskbar->SetProgressValue(GetHandle(), wParam, lParam);
		return 0;
	}

	return Window::WindowProc(Msg, wParam, lParam);
}

constexpr PCWSTR ArchCodeToStr(WORD wArchitecture)
{
	switch (wArchitecture)
	{
	case PROCESSOR_ARCHITECTURE_AMD64:
		return L"x64";
		break;
	case PROCESSOR_ARCHITECTURE_INTEL:
		return L"x86";
		break;
	case PROCESSOR_ARCHITECTURE_ARM64:
		return L"ARM64";
		break;
	default:
		return L"Unknown";
	}
}

void DirSelection::OnDraw(HDC hdc, RECT rect)
{
	const int FontHeight = GetFontSize(hFont);
	rect.bottom -= FontHeight;
	rect.left += FontHeight;
	rect.right -= FontHeight;

	DrawText(hdc, State == UUP || State == Scaning ? String_UUPDir : String_TempDir, &rect, DT_SINGLELINE);

	if (State == Temp || State == ProcessingCab || State == ProcessingEsd || State == Cleaning)
	{
		rect.top += FontHeight * 4;
		rect.right = FontHeight * 33;
		DrawText(hdc, String_TempDirDescription, &rect, DT_WORDBREAK);
		rect.right = FontHeight * 40;
		rect.top += FontHeight * 7;
		DrawText(hdc, String_Edition, &rect, DT_SINGLELINE);
		rect.left = rect.right / 5 * 3 - FontHeight;
		DrawText(hdc, String_Purpose, &rect, DT_SINGLELINE);
		rect.left = FontHeight;
		if (ComboBoxSel == CB_ERR)
		{
			rect.left += 100;
			rect.right -= 100;
			rect.top += FontHeight * 4;
			rect.bottom = 180 + FontHeight * 18;
			DrawText(hdc, String_SelectEdition, &rect, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
			return;
		}

		rect.top += FontHeight * 9 / 2;
		const auto& i = ImageDetail[ComboBoxSel];
		DrawText(hdc, ResStrFormat(String_ImageName, i.Name.c_str()), &rect, DT_SINGLELINE);
		rect.top += FontHeight * 3 / 2;
		DrawText(hdc, ResStrFormat(String_Version, i.Version.c_str()), &rect, DT_SINGLELINE);
		rect.top += FontHeight * 3 / 2;
		DrawText(hdc, ResStrFormat(String_SystemLanguage, i.Lang.c_str()), &rect, DT_SINGLELINE);
		rect.top += FontHeight * 3 / 2;
		DrawText(hdc, ResStrFormat(String_Architecture, ArchCodeToStr(i.Arch)), &rect, DT_SINGLELINE);
		rect.top += FontHeight * 3 / 2;
		DrawText(hdc, ResStrFormat(String_ESDFile, i.SystemESD.c_str()), &rect, DT_SINGLELINE);
		rect.top += FontHeight * 3 / 2;
		DrawText(hdc, String_AdditionEditions, &rect, DT_SINGLELINE);
		if (i.Branch.size())
		{
			rect.top += FontHeight * 8;
			DrawText(hdc, ResStrFormat(String_Branch, i.Branch.c_str()), &rect, DT_SINGLELINE);
		}
		if (State != Temp)
		{
			rect.top += FontHeight * (i.Branch.size() ? 2 : 10);
			rect.bottom -= FontHeight * 6;
			rect.left -= FontHeight;
			if (State == Cleaning)
			{
				DrawText(hdc, String_Cleaning, &rect, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
				return;
			}
			else if (State == ProcessingEsd)
				DrawText(hdc, ResStrFormat(String_CapturingESD, wProcessed, wTotal), &rect, DT_SINGLELINE | DT_CENTER);
			else
			{
				auto text = GetString(String_ExpandingCab) + format(L" ({}/{})", wProcessed, wTotal);
				DrawText(hdc, text, &rect, DT_SINGLELINE | DT_CENTER);
			}
			rect.top += FontHeight * 3 / 2;
			auto Progress = format(L" ({}%)", int(CurrentFileProgress));
			DrawText(hdc, GetString(String_ProcessingFile) + this->String + Progress, &rect, DT_SINGLELINE | DT_CENTER);
		}
	}
	else if (State == Scaning)
	{
		rect.top = FontHeight * 10;
		rect.bottom = rect.top + FontHeight * 2;
		DrawText(hdc, String_Wait, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
}

inline
static bool ScanCabs(DirSelection& Wnd, vector<wstring>& Packages)
{
	if (!SetCurrentDirectoryW(Wnd.ctx.PathUUP.c_str()))
	{
		Wnd.ErrorMessageBox();
		Wnd.SendMessage(WM_COMMAND, 2, 0);
		return false;
	}

	WIN32_FIND_DATAW wfd;
	HANDLE hFindFile = FindFirstFileW(L"*.cab", &wfd);
	auto TargetEditionID = Wnd.Target.GetWindowText() + '-';
	do
	{
		Packages.push_back(wfd.cFileName);
		if (_wcsnicmp(Packages.crbegin()->c_str(), L"SSU-", 4) == 0
			|| PathMatchSpecW(Packages.crbegin()->c_str(), L"Windows*.?-KB*-*")
			|| Packages.crbegin()->find(L"DesktopDeployment") != wstring::npos
			|| Packages.crbegin()->find(L".AggregatedMetadata") != wstring::npos
			|| Packages.crbegin()->find(L"EditionSpecific") != wstring::npos && Packages.crbegin()->find(TargetEditionID) == wstring::npos)
			Packages.pop_back();
	} while (FindNextFileW(hFindFile, &wfd));
	FindClose(hFindFile);

	return true;
}

static enum wimlib_progress_status WimProgress
(enum wimlib_progress_msg msg_type,
	union wimlib_progress_info* info,
	DirSelection* p)
{
	if (WaitForSingleObject(p->hEvent, 0) == WAIT_OBJECT_0)
		return WIMLIB_PROGRESS_STATUS_ABORT;

	if (msg_type == WIMLIB_PROGRESS_MSG_WRITE_STREAMS)
	{
		BYTE Progress = static_cast<BYTE>(info->write_streams.completed_bytes * 100 / info->write_streams.total_bytes);
		if (p->CurrentFileProgress != Progress)
		{
			p->CurrentFileProgress = Progress;
			BYTE TotalProgress = static_cast<BYTE>((static_cast<DWORD>(p->wProcessed * 2 - 1) * 100 + Progress) * 100 / (static_cast<WORD>(p->wTotal) * 200));
			if (TotalProgress > p->TotalProgress)
			{
				p->TotalProgress = TotalProgress;
				p->SetTaskbarProgress(TotalProgress, 100);
				p->ProcessingProgress.SetPos(TotalProgress * 100);
				SetWindowText(p->GetHandle(), ResStrFormat(String_ProcessingRefESDs, TotalProgress));
			}
			p->Invalidate(false);
		}
	}
	return WIMLIB_PROGRESS_STATUS_CONTINUE;
}

static MyUniquePtr<CHAR> GetUpdateName(HANDLE hFile)
{
	string xml;
	if (!ReadText(hFile, xml, GetFileSize(hFile, nullptr)))
		return MyUniquePtr<CHAR>();

	xml_document<CHAR> doc;
	try
	{
		try
		{
			doc.parse<0>(const_cast<PSTR>(xml.c_str()));
		}
		catch (parse_error&)
		{
			throw ERROR_INVALID_DATA;
		}
		auto assembly = doc.first_node();
		if (!assembly) ERROR_BAD_FORMAT;

		auto attr = assembly->first_attribute("displayName");
		if (!attr || _stricmp(attr->value(), "default") == 0)
		{
			auto assemblyIdentity = assembly->first_node("assemblyIdentity");
			auto package = assembly->first_node("package");
			if (!assemblyIdentity || !package) throw ERROR_NOT_FOUND;

			attr = assemblyIdentity->first_attribute("name");
			if (!attr) throw ERROR_NOT_FOUND;
		}

		MyUniquePtr<CHAR> pszName(attr->value_size() + 1);
		strcpy_s(pszName, attr->value_size() + 1, attr->value());
		return pszName;
	}
	catch (long ErrCode)
	{
		SetLastError(ErrCode);
		return MyUniquePtr<CHAR>();
	}
}

static wstring GetWimlibErrorMessage(int c)
{
	return format(L"Wimlib Error: {}\r\n{}", c, wimlib_get_error_string(static_cast<wimlib_error_code>(c)));
}

static void ProcessingThread(DirSelection& Wnd)
{
	auto& TargetImageInfo = Wnd.ImageDetail[Wnd.Target.GetCurSel()];
	if (!SetCurrentDirectoryW(Wnd.ctx.PathTemp.c_str()))
	{
	cdfailure:
		Wnd.ErrorMessageBox();
		Wnd.SendMessage(WM_COMMAND, 2, 0);
		return;
	}
	GetAppxFeatures(Wnd.ctx);

	AdjustPrivileges({ SE_RESTORE_NAME, SE_BACKUP_NAME });

	if (!CreateDirectoryW(L"RefESDs", nullptr))
	{
	cleantmp:
		WCHAR szPath[MAX_PATH];
		GetSystemDirectoryW(szPath, MAX_PATH);
		SetCurrentDirectoryW(szPath);
		DeleteDirectory(Wnd.ctx.PathTemp.c_str());
		goto cdfailure;
	}

	vector<wstring> Packages;
	if (!ScanCabs(Wnd, Packages))
		goto cleantmp;

	WIN32_FIND_DATAW wfd;
	HANDLE hFindFile = FindFirstFileW(L"*.esd", &wfd);
	if (hFindFile != INVALID_HANDLE_VALUE)
	{
		wstring Link = Wnd.ctx.PathTemp;
		Link += L"RefESDs";
		Link += L"\\";
		wstring Target = Wnd.ctx.PathUUP;
		auto LinkSize = Link.size();
		auto TargetSize = Target.size();
		do
		{
			if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				continue;
			if (wcsstr(wfd.cFileName, L"Edition")
				&& wcsstr(wfd.cFileName, TargetImageInfo.Edition.c_str()))
				goto mklink;
			else
			{
				WIMStruct* wim;
				if (wimlib_open_wim(wfd.cFileName, 0, &wim) != 0)
					continue;
				wimlib_wim_info wi;
				wimlib_get_wim_info(wim, &wi);
				if (wi.image_count != 1)
				{
					wimlib_free(wim);
					continue;
				}
				wimlib_free(wim);
			}

		mklink:
			Link.resize(LinkSize);
			Link += wfd.cFileName;
			Target.resize(TargetSize);
			Target += wfd.cFileName;
			if (!CreateSymbolicLinkW(Link.c_str(), Target.c_str(), 0))
			{
				Wnd.ErrorMessageBox();
				CloseHandle(hFindFile);
				ExitProcess(HRESULT_FROM_WIN32(GetLastError()));
			}
		} while (FindNextFileW(hFindFile, &wfd));
		FindClose(hFindFile);
	}

	if (!SetCurrentDirectoryW(Wnd.ctx.PathUUP.c_str()))
		goto cleantmp;
	WIMStruct* wim;
	int ret = wimlib_open_wim(TargetImageInfo.SystemESD.c_str(), 0, &wim);
	if (ret != 0)
	{
		auto msg = GetWimlibErrorMessage(ret);
		Wnd.MessageBox(msg.c_str(), nullptr, MB_ICONERROR);

		WCHAR szPath[MAX_PATH];
		GetSystemDirectoryW(szPath, MAX_PATH);
		SetCurrentDirectoryW(szPath);
		DeleteDirectory(Wnd.ctx.PathTemp.c_str());
		ExitProcess(E_UNEXPECTED);
	}
	else
	{
		PCWSTR FontPath = L"\\boot\\fonts\\segoe_slboot.ttf";
		wimlib_extract_paths(wim, 1, Wnd.ctx.PathTemp.c_str(), &FontPath, 1, WIMLIB_EXTRACT_FLAG_NO_PRESERVE_DIR_STRUCTURE);
		wimlib_free(wim);

		auto FontFile = Wnd.ctx.PathTemp + L"segoe_slboot.ttf";
		AddFontResourceExW(FontFile.c_str(), FR_PRIVATE | FR_NOT_ENUM, nullptr);
		DeleteFileW(FontFile.c_str());
	}

	if (!Packages.empty())
	{
		Wnd.ButtonMain.ShowWindow(SW_SHOW);
		Wnd.ProcessingProgress.SetMarqueeProgressBar(false);
		Wnd.ProcessingProgress.SetState(ProgressBar::Normal);
		Wnd.ProcessingProgress.ShowWindow(SW_SHOW);
		Wnd.SetTaskbarState(Wnd.Normal);
		Wnd.SetTaskbarProgress(0, 100);
		Wnd.wTotal = static_cast<WORD>(Packages.size());
		auto tempCabExtractDir = Wnd.ctx.PathTemp + L"Temp";

		for (const auto& i : Packages)
		{
			Wnd.CurrentFileProgress = 0;
			++Wnd.wProcessed;
			Wnd.State = Wnd.ProcessingCab;
			Wnd.Invalidate(false);
			Wnd.String = EllipsizeForUi(i);

			if (!SetCurrentDirectoryW(Wnd.ctx.PathUUP.c_str())
				|| !CreateDirectoryW(tempCabExtractDir.c_str(), nullptr))
				goto cleantmp;
			USHORT cComplitedFiles = 0;
			if (!ExpandCabFile(i.c_str(), tempCabExtractDir.c_str(), [&](bool Open, PCWSTR pszFileName, USHORT cTotalFileCount, HANDLE& hFile)
				{
					if (!Open)
					{
						++cComplitedFiles;
						auto Progress = static_cast<BYTE>((static_cast<DWORD>(cComplitedFiles) * 100 / cTotalFileCount));
						if (Progress != Wnd.CurrentFileProgress)
						{
							Wnd.CurrentFileProgress = Progress;
							BYTE TotalProgress = static_cast<BYTE>((static_cast<DWORD>(Wnd.wProcessed * 2 - 2) * 100 + Progress) * 100 / (static_cast<WORD>(Wnd.wTotal) * 200));
							if (TotalProgress > Wnd.TotalProgress)
							{
								Wnd.TotalProgress = TotalProgress;
								Wnd.SetTaskbarProgress(TotalProgress, 100);
								Wnd.ProcessingProgress.SetPos(TotalProgress * 100);
							}
							Wnd.Invalidate(false);
						}
					}

					return TRUE;
				}))
			{
				DWORD dwError = GetLastError();
				SetLastError(dwError);
				Wnd.ErrorMessageBox();
				ExitProcess(HRESULT_FROM_WIN32(GetLastError()));
			}
			else if (!SetCurrentDirectoryW(Wnd.ctx.PathTemp.c_str()))
				goto cdfailure;

			wstring ESD = i;
			PathRenameExtensionW(ESD.data(), L".esd");
			PathRenameExtensionW(Wnd.String.data(), L".esd");

			Wnd.State = Wnd.ProcessingEsd;
			Wnd.CurrentFileProgress = 0;
			Wnd.Invalidate(false);
			try
			{
				ret = wimlib_create_new_wim(WIMLIB_COMPRESSION_TYPE_LZMS, &wim);
				if (ret) throw ret;
				ret = wimlib_add_image(wim, L"Temp", L"Edition Package", nullptr, 0);
				if (ret) throw ret;
				wimlib_set_image_descripton(wim, 1, L"Edition Package");
				if (!SetCurrentDirectoryW(L"RefESDs"))
					goto cleantmp;
				wimlib_register_progress_function(wim,
					reinterpret_cast<wimlib_progress_func_t>(WimProgress), &Wnd);
				ret = wimlib_write(wim, ESD.c_str(), 1, 0, 0);
				if (ret) throw ret;
				wimlib_free(wim);

				DeleteDirectory(tempCabExtractDir.c_str());
				Wnd.State = Wnd.Cleaning;
				Wnd.Invalidate(false);
			}
			catch (int ret)
			{
				wimlib_free(wim);
				SetCurrentDirectoryW(Wnd.ctx.PathUUP.c_str());
				UINT uError = E_UNEXPECTED;
				if (ret == WIMLIB_ERR_ABORTED_BY_PROGRESS)
				{
					SetLastError(ERROR_CANCELLED);
					Wnd.ErrorMessageBox();
					uError = HRESULT_FROM_WIN32(ERROR_CANCELLED);
				}
				else
				{
					auto msg = GetWimlibErrorMessage(ret);
					Wnd.MessageBox(msg.c_str(), nullptr, MB_ICONERROR);
				}

				WCHAR szPath[MAX_PATH];
				GetSystemDirectoryW(szPath, MAX_PATH);
				SetCurrentDirectoryW(szPath);
				DeleteDirectory(Wnd.ctx.PathTemp.c_str());
				ExitProcess(uError);
			}
		}

		auto& UpdateVector = Wnd.ctx.UpdateVector;
		for (size_t i = 0; i != UpdateVector.size();)
		{
			auto iter = UpdateVector.begin() + i;
			if (!PathMatchSpecW(iter->UpdateFile, L"*.cab"))
			{
				++i;
				continue;
			}

			HANDLE hWritePipe = INVALID_HANDLE_VALUE;
			HANDLE hReadPipe = INVALID_HANDLE_VALUE;

			if (ExpandCabFile(iter->UpdateFile, nullptr,
				[&](bool Open, PCWSTR pszFileName, USHORT cTotalFileCount, HANDLE& hFile) -> BOOL
				{
					if (Open)
					{
						hFile = INVALID_HANDLE_VALUE;
						if (_wcsicmp(pszFileName, L"update.mum") == 0)
						{
							CreatePipe(&hReadPipe, &hWritePipe, nullptr, 4 * 1024 * 1024);
							hFile = hWritePipe;
							return TRUE;
						}
						else if (_wcsicmp(pszFileName, L"SetupCore.dll") == 0)
						{
							Wnd.ctx.SetupUpdate = move(iter->UpdateFile);
							Wnd.ctx.bAddSetupUpdate = true;
							iter = Wnd.ctx.UpdateVector.erase(iter);
							return FALSE;
						}
						return TRUE;
					}
					else
					{
						CloseHandle(hWritePipe);
						hWritePipe = INVALID_HANDLE_VALUE;
						auto pUpdateName = GetUpdateName(hReadPipe);
						CloseHandle(hReadPipe);
						hReadPipe = INVALID_HANDLE_VALUE;
						if (pUpdateName)
						{
							if (_stricmp(pUpdateName, "Package_for_SafeOSDU") == 0)
							{
								Wnd.ctx.SafeOSUpdate = move(iter->UpdateFile);
								Wnd.ctx.bAddSafeOSUpdate = true;
								iter = Wnd.ctx.UpdateVector.erase(iter);
							}
							else if (strstr(pUpdateName, "Enablement"))
							{
								Wnd.ctx.EnablementPackage = move(iter->UpdateFile);
								Wnd.ctx.bAddEnablementPackage = true;
								iter = Wnd.ctx.UpdateVector.erase(iter);
							}
							else ++i;
						}
						else ++i;

						return FALSE;
					}
				})) ++i;
			else if (GetLastError() != ERROR_CANCELLED)
				Wnd.ctx.UpdateVector.erase(iter);

			if (hWritePipe != INVALID_HANDLE_VALUE)
				CloseHandle(hWritePipe);
			if (hReadPipe != INVALID_HANDLE_VALUE)
				CloseHandle(hReadPipe);
		}
	}

	Wnd.State = DirSelection::AllDone;
	Wnd.PostMessage(WM_CLOSE, 0, 0);
	HMODULE hModule = GetModuleHandleA("UUPFetcher.exe");
	if (hModule)
		FreeLibrary(hModule);
}

inline
static wstring GetCurrentSystemEditionID()
{
	return GetVersionValue(L"SOFTWARE", L"EditionID");
}

void DirSelection::OnSwitchEdition()
{
	int ComboBoxSel = Target.GetCurSel();
	if (ComboBoxSel == CB_ERR)
		return;
	if (ComboBoxSel == this->ComboBoxSel)
		return;
	this->ComboBoxSel = ComboBoxSel;

	for (auto i = AdditionEditions.GetCount(); i != 0; --i)
		AdditionEditions.DeleteString(0);
	for (const auto& i : ImageDetail[ComboBoxSel].UpgradableEditions)
		AdditionEditions.InsertString(i.c_str(), -1);
	AdditionEditions.ShowWindow(SW_SHOW);
	RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
}

void DirSelection::OnClose()
{
	switch (State)
	{
	case ProcessingEsd:case ProcessingCab:case Cleaning:
		MainButtonClicked();
		return;
	}
	DestroyWindow();
}

void DirSelection::OnSize(BYTE type, int nClientWidth, int nClientHeight, WindowBatchPositioner)
{
	if (State == ProcessingCab || State == ProcessingEsd || State == Cleaning)
		return;
	SetProcessEfficiencyMode(type != SIZE_RESTORED);
}

void DirSelection::Browse()
{
	Lourdle::UIFramework::String dir;
	if (GetOpenFolderName(this, dir))
		Path.SetWindowText(dir);
}


struct AnimationData
{
	int animationStep = 0;
	const int stepSize = 12;
	int totalSteps = 30 * GetFontSize();
	RECT initialRect;
};

void DirSelection::OnDestroy()
{
	AnimationData* pData = reinterpret_cast<AnimationData*>(GetPropW(hWnd, L"AnimData"));
	if (pData)
		delete pData;

	if (pTaskbar)
		pTaskbar->Release();
	if (State == AllDone)
	{
		PostQuitMessage(1 + Purpose.GetCurSel());
		ctx.TargetImageInfo = move(ImageDetail[Target.GetCurSel()]);
		return;
	}
	else PostQuitMessage(0);
}

static void CALLBACK OnTimer(HWND hWnd, UINT uMsg, AnimationData* data, DWORD dwTime)
{
	int newHeight = (data->initialRect.bottom - data->initialRect.top) + data->stepSize;
	int newTop = data->initialRect.top - (data->stepSize / 2);
	data->animationStep += data->stepSize;

	if (data->animationStep >= data->totalSteps)
	{
		newHeight -= data->animationStep - data->totalSteps;
		newTop += (data->animationStep - data->totalSteps) / 2;
	}

	SetWindowPos(hWnd, nullptr, data->initialRect.left, newTop, data->initialRect.right - data->initialRect.left, newHeight, 0);
	RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);

	if (data->animationStep >= data->totalSteps)
	{
		KillTimer(hWnd, reinterpret_cast<UINT_PTR>(data));
		RemovePropW(hWnd, L"AnimData");
		delete data;
	}
	else
	{
		data->initialRect.top = newTop;
		data->initialRect.bottom = newTop + newHeight;
	}
}

static void StartExpandAnimation(HWND hWnd)
{
	AnimationData* data = new AnimationData;
	if (GetWindowRect(hWnd, &data->initialRect))
	{
		data->animationStep = 0;
		SetPropW(hWnd, L"AnimData", data);
		SetTimer(hWnd, reinterpret_cast<UINT_PTR>(data), USER_TIMER_MINIMUM, reinterpret_cast<TIMERPROC>(OnTimer));
	}
	else delete data;
}

void DirSelection::MainButtonClicked()
{
	if (State == Temp)
	{
		if (Target.GetCurSel() == CB_ERR)
		{
			MessageBox(GetString(String_NoEditionSelected), nullptr, MB_ICONERROR);
			return;
		}

		const auto& refImageDetail = ImageDetail[Target.GetCurSel()];
		if (Purpose.GetCurSel() == 1)
		{
			if (IsMiniNtBoot())
			{
				SetLastError(ERROR_NOT_SUPPORTED);
				ErrorMessageBox();
				return;
			}

			WCHAR szPath[MAX_PATH];
			GetSystemDirectoryW(szPath, MAX_PATH);
			ULARGE_INTEGER FreeBytesAvailable;
			if (!GetDiskFreeSpaceExW(szPath, &FreeBytesAvailable, nullptr, nullptr))
			{
				ErrorMessageBox();
				return;
			}
			else if (FreeBytesAvailable.QuadPart < kMinTargetDiskBytes)
			{
				wcscpy_s(szPath + 3, MAX_PATH - 3, L"$WINDOWS.~BT");
				ULONGLONG ullSize = GetDirectorySize(szPath);
				if (ullSize == -1
					|| FreeBytesAvailable.QuadPart + ullSize < kMinTargetDiskBytes)
				{
					wcscpy_s(szPath + 3, MAX_PATH - 3, L"Windows.old");
					ULONGLONG ullSize2 = GetDirectorySize(szPath);
					if (ullSize2 == -1
						|| FreeBytesAvailable.QuadPart + ullSize + ullSize2 < kMinTargetDiskBytes)
					{
						MessageBox(GetString(String_NotEnoughFreeSpace), nullptr, MB_ICONERROR);
						return;
					}
				}
			}

			wstring EditionID = GetCurrentSystemEditionID();
			bool bFound = false;
			for (const auto& i : refImageDetail.UpgradableEditions)
				if (i == EditionID)
				{
					bFound = true;
					break;
				}
			if (!bFound
				&& MessageBox(GetString(String_NonupgradableEdition), nullptr, MB_ICONQUESTION | MB_YESNO) == IDNO)
				return;

			SYSTEM_INFO si;
			GetNativeSystemInfo(&si);
			if (si.wProcessorArchitecture != refImageDetail.Arch)
			{
				MessageBox(GetString(String_DifferentArchitecture), nullptr, MB_ICONERROR);
				return;
			}

			VersionStruct versionCurrent;
			GetNtKernelVersion(versionCurrent);

			VersionStruct versionTarget;
			ParseVersionString(refImageDetail.Version.c_str(), versionTarget);

			if (versionTarget <= versionCurrent)
			{
				auto [dwMajor, dwMinor, dwBuild, dwSpBuild] = versionCurrent;
				wstring text = ResStrFormat(String_HigherVersion, dwMajor, dwMinor, dwBuild, dwSpBuild, refImageDetail.Version.c_str()).GetPointer();
				if (versionTarget.dwBuild == dwBuild)
					text[text.rfind(L"\r\n\r\n")] = '\0';
				if (MessageBox(text.c_str(), GetString(String_Notice), MB_ICONQUESTION | MB_YESNO) == IDNO)
					return;
			}
		}


		if (refImageDetail.Arch != PROCESSOR_ARCHITECTURE_AMD64 && refImageDetail.Arch != PROCESSOR_ARCHITECTURE_INTEL && refImageDetail.Arch != PROCESSOR_ARCHITECTURE_ARM64)
		{
			SetLastError(ERROR_NOT_SUPPORTED);
			ErrorMessageBox();
			return;
		}
		ctx.PathTemp = Path.GetWindowText();
		HANDLE hFile = CreateFileW(ctx.PathTemp.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			ErrorMessageBox();
			return;
		}
		else if (!(GetFileAttributesByHandle(hFile) & FILE_ATTRIBUTE_DIRECTORY))
		{
			SetLastError(ERROR_DIRECTORY);
			ErrorMessageBox();
			CloseHandle(hFile);
			return;
		}
		else
		{
			ctx.PathTemp = GetFinalPathName(hFile);
			if (Purpose.GetCurSel() != 1)
			{
				WCHAR szFilesystem[32];
				if (!GetVolumeInformationByHandleW(hFile, nullptr, 0, nullptr, 0, nullptr, szFilesystem, 32))
				{
					CloseHandle(hFile);
					ErrorMessageBox();
					return;
				}
				CloseHandle(hFile);
				if (wcscmp(szFilesystem, L"NTFS") != 0 && Purpose.GetCurSel() == 1)
				{
					if (MessageBox(
						ResStrFormat(String_VolumeNotNTFS, szFilesystem),
						GetString(String_Notice), MB_ICONQUESTION | MB_YESNO) == IDYES)
						ctx.bAdvancedOptionsAvaliable = false;
					else
						return;
				}
			}
			else
				CloseHandle(hFile);

			GUID guid;
			CoCreateGuid(&guid);
			if (*ctx.PathTemp.crbegin() != '\\')
				ctx.PathTemp += '\\';
			ctx.PathTemp += GUID2String(guid);
			if (!CreateDirectoryW(ctx.PathTemp.c_str(), nullptr))
			{
				ErrorMessageBox();
				return;
			}
			SetFileAttributesW(ctx.PathTemp.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
			ctx.PathTemp += '\\';
			PVOID p = VirtualAllocEx(g_pHostContext->hParent, nullptr, ctx.PathTemp.size() * 2 + 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
			WriteProcessMemory(g_pHostContext->hParent, p, ctx.PathTemp.c_str(), ctx.PathTemp.size() * 2 + 2, nullptr);
			WriteProcessMemory(g_pHostContext->hParent, reinterpret_cast<LPVOID>(g_pHostContext->wParam), &p, sizeof(PVOID), nullptr);
		}

		Path.EnableWindow(false);
		ButtonBrowse.EnableWindow(false);
		Target.EnableWindow(false);
		Purpose.EnableWindow(false);
		ButtonMain.SetWindowText(GetString(String_Cancel));
		ButtonMain.ShowWindow(SW_HIDE);
		const int cFontHeight = GetFontSize(hFont);
		ProcessingProgress.MoveWindow(cFontHeight, cFontHeight * 37, cFontHeight * 38, cFontHeight * 3 / 2);
		ProcessingProgress.SetMarqueeProgressBar();
		ProcessingProgress.ShowWindow(SW_SHOW);
		SetTaskbarState(Indeterminate);
		State = ScaningCabs;
		SetWindowText(GetString(String_Wait));
		thread(ProcessingThread, ref(*this)).detach();
	}
	else if (State == UUP)
	{
		State = Scaning;

		SetTaskbarState(Indeterminate);
		DestroyCursor(hCursor);
		hCursor = LoadCursorW(nullptr, IDC_WAIT);
		SetCursor(hCursor);

		ButtonMain.ShowWindow(SW_HIDE);
		ButtonBrowse.EnableWindow(false);
		Invalidate(false);

		ctx.PathUUP = Path.GetWindowText();

		auto Reset = [&](DirSelection* p)
		{
			p->State = UUP;
			p->ButtonMain.ShowWindow(SW_SHOW);
			p->ButtonBrowse.EnableWindow(true);
			DestroyCursor(p->hCursor);
			p->hCursor = LoadCursorW(nullptr, IDC_ARROW);
			SetCursor(p->hCursor);
			p->Invalidate(false);
			p->SetTaskbarState(NoProgress);
		};

		if (!SetCurrentDirectoryW(ctx.PathUUP.c_str()))
		{
			ErrorMessageBox();
			Reset(this);
		}
		else
		{
			HANDLE hDir = CreateFileW(ctx.PathUUP.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
			if (hDir == INVALID_HANDLE_VALUE)
			{
				ErrorMessageBox();
				Reset(this);
				return;
			}
			ctx.PathUUP = GetFinalPathName(hDir);
			if (*ctx.PathUUP.crbegin() != '\\')
				ctx.PathUUP += '\\';
			CloseHandle(hDir);
			auto Scan = [&](DirSelection* p) {
				std::error_code ec;
				auto iter = directory_iterator(p->ctx.PathUUP, ec);
				if (ec)
				{
					ErrorMessageBox();
					Reset(p);
					return;
				}

				WIMStruct* wim = nullptr;
				p->ctx.bInstallEdge = false;

				try
				{
					wimlib_wim_info wwi;
					const wimlib_tchar* ProductName;
					const Lourdle::UIFramework::String Path = ctx.PathUUP.c_str();

					for (auto& entry : iter) if (entry.is_regular_file())
					{
						auto name = entry.path().filename();
						auto extension = name.extension();
						if (_wcsicmp(extension.c_str(), L".esd") == 0)
						{
							auto WIMLIB_ACTION = [&](int Code)
								{
									if (Code != WIMLIB_ERR_SUCCESS)
									{
										if (wim)
											wimlib_free(wim);
										throw Code;
									}
								};
							WIMLIB_ACTION(wimlib_open_wim(name.c_str(), 0, &wim));
							WIMLIB_ACTION(wimlib_get_wim_info(wim, &wwi));
							if (wwi.image_count == 3
								&& wcscmp(wimlib_get_image_name(wim, 1), L"Windows Setup Media") == 0
								&& wcsncmp(wimlib_get_image_name(wim, 2), L"Microsoft Windows Recovery Environment", 38) == 0
								&& (ProductName = wimlib_get_image_property(wim, 3, L"WINDOWS/PRODUCTNAME"))
								&& wcscmp(ProductName, L"Microsoft® Windows® Operating System") == 0)
							{
								ImageDetail.resize(ImageDetail.size() + 1);
								ImageInfo& refImageInfo = *ImageDetail.rbegin();
								refImageInfo.SystemESD = std::move(name);
								refImageInfo.Name = wimlib_get_image_name(wim, 3);
								refImageInfo.Edition = wimlib_get_image_property(wim, 3, L"WINDOWS/EDITIONID");
								refImageInfo.Lang = wimlib_get_image_property(wim, 3, L"WINDOWS/LANGUAGES/DEFAULT");
								refImageInfo.Arch = static_cast<WORD>(_wtoi(wimlib_get_image_property(wim, 3, L"WINDOWS/ARCH")));

								const auto Branch = wimlib_get_image_property(wim, 3, L"WINDOWS/VERSION/BRANCH");
								if (Branch)
									refImageInfo.Branch = Branch;

								const auto Major = wimlib_get_image_property(wim, 3, L"WINDOWS/VERSION/MAJOR"),
									Minor = wimlib_get_image_property(wim, 3, L"WINDOWS/VERSION/MINOR"),
									Build = wimlib_get_image_property(wim, 3, L"WINDOWS/VERSION/BUILD"),
									SpBuild = wimlib_get_image_property(wim, 3, L"WINDOWS/VERSION/SPBUILD");

								refImageInfo.Version = format(L"{}.{}.{}.{}", Major, Minor, Build, SpBuild);

								WIMLIB_ACTION(wimlib_iterate_dir_tree(wim, 3, L"Windows\\servicing\\Editions", WIMLIB_ITERATE_DIR_TREE_FLAG_CHILDREN,
									[](const struct wimlib_dir_entry* dentry, void* user_ctx)
									{
										auto len = wcslen(dentry->filename) - 11;
										if (len > 0 && _wcsicmp(dentry->filename + len, L"Edition.xml") == 0)
											reinterpret_cast<vector<wstring>*>(user_ctx)->push_back(wstring(dentry->filename, len));
										return 0;
									}, &refImageInfo.UpgradableEditions));
								sort(refImageInfo.UpgradableEditions.begin(), refImageInfo.UpgradableEditions.end());

								WIMLIB_ACTION(wimlib_iterate_dir_tree(wim, 3, L"Windows\\System32\\spp\\tokens\\skus", WIMLIB_ITERATE_DIR_TREE_FLAG_CHILDREN,
									[](const struct wimlib_dir_entry* dentry, void* user_ctx)
									{
										reinterpret_cast<vector<wstring>*>(user_ctx)->push_back(dentry->filename);
										return 0;
									}, &refImageInfo.AdditionalEditions));
								for (size_t i = 0; i != refImageInfo.AdditionalEditions.size();)
								{
									bool bFound = false;
									for (auto& j : refImageInfo.UpgradableEditions)
										if (refImageInfo.AdditionalEditions[i] == j)
										{
											bFound = true;
											break;
										}
									if (bFound)
										++i;
									else
										refImageInfo.AdditionalEditions.erase(refImageInfo.AdditionalEditions.begin() + i);
								}
								refImageInfo.AdditionalEditions.shrink_to_fit();

								WIMLIB_ACTION(wimlib_iterate_dir_tree(wim, 3, L"Windows\\Boot", WIMLIB_ITERATE_DIR_TREE_FLAG_CHILDREN,
									[](const struct wimlib_dir_entry* dentry, void* user_ctx)
									{
										if (_wcsicmp(dentry->filename, L"PCAT") == 0)
											reinterpret_cast<ImageInfo*>(user_ctx)->SupportLegacyBIOS = 1;
										else if (_wcsicmp(dentry->filename, L"EFI") == 0)
											reinterpret_cast<ImageInfo*>(user_ctx)->SupportEFI = 1;
										else if (_wcsicmp(dentry->filename, L"EFI_EX") == 0)
											reinterpret_cast<ImageInfo*>(user_ctx)->bHasBootEX = 1;
										return 0;
									}, &refImageInfo));
							}

							wimlib_free(wim);
						}
						else if (_wcsicmp(extension.c_str(), L".appx") == 0
							|| _wcsicmp(extension.c_str(), L".appxbundle") == 0
							|| _wcsicmp(extension.c_str(), L".msix") == 0
							|| _wcsicmp(extension.c_str(), L".msixbundle") == 0)
							p->ctx.AppVector.push_back(entry.path().c_str());
						else if (PathMatchSpecW(name.c_str(), L"SSU-*.cab")
							|| PathMatchSpecW(name.c_str(), L"Windows*.?-KB*-*.msu"))
							p->ctx.UpdateVector.push_back(Update{ .UpdateFile = entry.path().c_str() });
						else if (PathMatchSpecW(name.c_str(), L"Windows*.?-KB*-*.cab")
							|| PathMatchSpecW(name.c_str(), L"Windows*.?-KB*-*.wim")) {
							auto UpdateName = name.wstring();
							auto pos = UpdateName.find(L'-');
							pos = UpdateName.find(L'-', pos + 1) + 1;

							auto i = UpdateName.c_str() + pos;
							while (isalpha(*i) || isdigit(*i))
								++i;
							pos = i - UpdateName.c_str();
							UpdateName.replace(pos, wstring::npos, L"*.psf");
							UpdateName = entry.path().parent_path() / UpdateName;

							WIN32_FIND_DATAW wfd;
							HANDLE hFindFile = FindFirstFileW(UpdateName.c_str(), &wfd);
							Update Package{ .UpdateFile = entry.path().c_str() };
							if (hFindFile != INVALID_HANDLE_VALUE)
							{
								FindClose(hFindFile);
								Package.PSF = entry.path().parent_path() / wfd.cFileName;
							}
							p->ctx.UpdateVector.push_back(move(Package));
						}
						else if (_wcsicmp(name.c_str(), L"Edge.wim") == 0)
							p->ctx.bInstallEdge = true;
					}

					ImageDetail.shrink_to_fit();
				}
				catch (int c)
				{
					auto msg = GetWimlibErrorMessage(c);
					MessageBox(msg.c_str(), nullptr, MB_ICONERROR);
					ImageDetail.clear();
					Reset(p);
					return;
				}

				if (State == Scaning)
					ButtonMain.PostCommand();
			};

			thread Thread(Scan, this);

			Thread.detach();
			FetchUUP.ShowWindow(SW_HIDE);
		}
	}
	else if (State == Scaning)
	{
		if (!ImageDetail.empty())
		{
			State = Temp;

			const int nFontHeight = GetFontSize(hFont);
			const int nWidth = nFontHeight * 40;
			ButtonMain.MoveWindow(nWidth / 2 - nFontHeight * 3, nFontHeight * 40, nFontHeight * 6, nFontHeight * 2);
			ButtonMain.ShowWindow(SW_SHOW);

			AdditionEditions.MoveWindow(nWidth / 20, nFontHeight * 25, nWidth / 10 * 9, nFontHeight * 6);
			if (ImageDetail.size() != 1)
				AdditionEditions.ShowWindow(SW_HIDE);

			Target.MoveWindow(nWidth / 40, nFontHeight * 13, nWidth / 5 * 2, 1);
			Purpose.MoveWindow(nWidth / 40 * 39 - nWidth / 5 * 2, nFontHeight * 13, nWidth / 5 * 2, 1);

			for (const auto& i : ImageDetail)
				Target.AddString(i.Edition.c_str());

			if (ImageDetail.size() == 1)
			{
				Target.SetCurSel(0);
				OnSwitchEdition();
			}

			DestroyCursor(hCursor);
			hCursor = LoadCursorW(nullptr, IDC_ARROW);
			SetCursor(hCursor);
			SetTaskbarState(NoProgress);

			ButtonMain.ShowWindow(SW_SHOW);
			ButtonBrowse.EnableWindow(true);

			StartExpandAnimation(hWnd);
		}
		else
		{
			DestroyCursor(hCursor);
			hCursor = LoadCursorW(nullptr, IDC_ARROW);
			SetCursor(hCursor);
			SetLastError(ERROR_FILE_NOT_FOUND);
			ErrorMessageBox();
			ButtonMain.ShowWindow(SW_SHOW);
			ButtonBrowse.EnableWindow(true);
			if (GetModuleHandleA("UUPFetcher.exe"))
				FetchUUP.ShowWindow(SW_SHOW);
			State = UUP;
			SetTaskbarState(NoProgress);
			Invalidate(false);
		}
	}
	else if (State == ProcessingCab || State == ProcessingEsd || State == ScaningCabs || State == Cleaning)
	{
		if (State == ScaningCabs)
		{
			DestroyCursor(hCursor);
			hCursor = LoadCursorW(nullptr, IDC_ARROW);
			SetCursor(hCursor);
			ResetEvent(hEvent);
			State = Temp;
			SetWindowText(GetString(String_DirChoice));
			ButtonMain.SetWindowText(GetString(String_Next));
			ButtonMain.ShowWindow(SW_SHOW);
			ButtonMain.EnableWindow(true);
			ButtonBrowse.EnableWindow(true);
			Target.EnableWindow(true);
			Path.EnableWindow(true);
			ProcessingProgress.ShowWindow(SW_HIDE);
			SetTaskbarState(NoProgress);
		}
		else if (WaitForSingleObject(hEvent, 0) == WAIT_TIMEOUT)
		{
			SetEvent(hEvent);
			DestroyCursor(hCursor);
			hCursor = LoadCursorW(nullptr, IDC_WAIT);
			SetCursor(hCursor);
			ButtonMain.EnableWindow(false);
		}
	}
}

void DirSelection::Fetch()
{
	HMODULE hModule = GetModuleHandleA("UUPFetcher.exe");
	WCHAR szPath[MAX_PATH];
	GetModuleFileNameW(hModule, szPath, MAX_PATH);
	STARTUPINFOW si = {
		.cb = sizeof(si),
		.dwFlags = STARTF_USESTDHANDLES
	};
	HANDLE hReadPipe;
	SECURITY_ATTRIBUTES sa = {
		.nLength = sizeof(SECURITY_ATTRIBUTES),
		.bInheritHandle = TRUE
	};
	CreatePipe(&hReadPipe, &si.hStdOutput, &sa, 0);
	PROCESS_INFORMATION pi;
	WCHAR szArg[] = L" /WritePathToStdOut";
	if (CreateProcessW(szPath, szArg, nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi))
	{
		CloseHandle(si.hStdOutput);
		ShowWindow(SW_HIDE);
		CloseHandle(pi.hThread);

		SetProcessEfficiencyMode(true);
		WaitForSingleObject(pi.hProcess, INFINITE);
		SetProcessEfficiencyMode(false);

		DWORD dwCode;
		GetExitCodeProcess(pi.hProcess, &dwCode);
		CloseHandle(pi.hProcess);
		ShowWindow(SW_SHOWNORMAL);

		if (dwCode == kExitCodePathWrittenToStdout)
		{
			DWORD cbData = 0;
			PeekNamedPipe(hReadPipe, nullptr, 0, nullptr, &cbData, nullptr);
			MyUniqueBuffer<PWSTR> PathName = cbData;
			ReadFile(hReadPipe, PathName, cbData, nullptr, nullptr);
			Path.SetWindowText(PathName);
			MainButtonClicked();
		}
	}
	else
	{
		ErrorMessageBox();
		CloseHandle(si.hStdOutput);
	}
	CloseHandle(hReadPipe);
}

void DirSelection::SetTaskbarState(TaskbarState State)
{
	if (!pTaskbar
		&& CoCreateInstance(
			CLSID_TaskbarList,
			0,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&pTaskbar)) == S_OK)
		pTaskbar->HrInit();
	if (pTaskbar)
		pTaskbar->SetProgressState(GetHandle(), static_cast<TBPFLAG>(State));
}

void DirSelection::SetTaskbarProgress(ULONGLONG ullComplited, ULONGLONG ullTotal)
{
	if (!pTaskbar
		&& CoCreateInstance(
			CLSID_TaskbarList,
			0,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&pTaskbar)) == S_OK)
		pTaskbar->HrInit();
	if (pTaskbar)
		SendMessage(SetTaskbarProgressMsg, ullComplited, ullTotal);
}
