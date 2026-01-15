#include "pch.h"
#include "UUPAssistant.h"
#include "Resources/resource.h"

#define DismAddProvisionedAppxPackage _DismAddProvisionedAppxPackage
#include <dismapi.h>
#undef _DismAddProvisionedAppxPackage
#include <wimgapi.h>
#include <Shlwapi.h>

#include <format>
#include <filesystem>

using namespace std;
using namespace Lourdle::UIFramework;


// Undocumented DISM API
extern "C"
{

//////////////////////////////////////////////////////////////////////////////
//
// Constants
//
//////////////////////////////////////////////////////////////////////////////

// Clean flags
#define DISM_CLEAN_FLAG_RESET_BASE 0x00000001
#define DISM_CLEAN_FLAG_DEFER 0x00000002

//////////////////////////////////////////////////////////////////////////////
//
// Enums
//
//////////////////////////////////////////////////////////////////////////////

typedef enum _DismCleanType
{
	DismCleanNone = 0,
	DismCleanWindowsUpdate = 1,
	DismCleanServicePack = 2,
	DismCleanComponents = 4
} DismCleanType;

//////////////////////////////////////////////////////////////////////////////
//
// Functions
//
//////////////////////////////////////////////////////////////////////////////

HRESULT WINAPI
_DismSetEdition2(
	_In_ DismSession Session,
	_In_ PCWSTR EditionId,
	_In_opt_ PCWSTR ProductKey,
	_In_opt_ HANDLE CancelEvent,
	_In_opt_ DISM_PROGRESS_CALLBACK Progress,
	_In_opt_ PVOID UserData);

HRESULT WINAPI
_DismCleanImage(
	_In_ DismSession Session,
	_In_ DismCleanType Type,
	_In_ DWORD Flags,
	_In_opt_ HANDLE CancelEvent,
	_In_opt_ DISM_PROGRESS_CALLBACK Progress,
	_In_opt_ PVOID UserData);

HRESULT WINAPI
_DismOptimizeProvisionedAppxPackages(
	_In_ DismSession Session);
}

inline
static HRESULT AddProvisionedAppxPackage(DismSession Session, PCWSTR pszTempDir, const AppxFeature& Feature, bool bFullInstall)
{
	wstring Path = pszTempDir;
	Path += L"AppxFeatures\\";
	Path += Feature.Feature;
	Path += '\\';
	auto StubPackageOption = DismStubPackageOptionNone;
	if (PathFileExistsW((Path + L"AppxMetadata").c_str())
		|| PathFileExistsW((Path + L"Stub").c_str()))
		StubPackageOption = bFullInstall ? DismStubPackageOptionInstallFull : DismStubPackageOptionInstallStub;
	auto License = Path + L"License.xml";
	auto pLicense = License.c_str();
	Path += Feature.Bundle;
	return _DismAddProvisionedAppxPackage(Session, Path.c_str(), nullptr, 0, nullptr, 0, &pLicense, Feature.bHasLicense, !Feature.bHasLicense, nullptr, L"all", StubPackageOption);
}

inline
static HRESULT AddProvisionedAppxPackage(DismSession Session, PCWSTR pszAppPath)
{
	return _DismAddProvisionedAppxPackage(Session, pszAppPath, nullptr, 0, nullptr, 0, nullptr, 0, TRUE, nullptr, nullptr, DismStubPackageOptionNone);
}

struct progctx_t
{
	UINT_PTR Progress;
	bool* pbCancel;
	std::function<void(PCWSTR)> SetString;
};

static wimlib_progress_status progfuc(enum wimlib_progress_msg msg_type,
	union wimlib_progress_info* info,
	progctx_t* progctx)
{
	if (progctx->pbCancel && *progctx->pbCancel)
		return WIMLIB_PROGRESS_STATUS_ABORT;

	if (msg_type == WIMLIB_PROGRESS_MSG_EXTRACT_STREAMS)
	{
		auto progress = static_cast<UINT_PTR>(info->extract.completed_bytes * 100 / info->extract.total_bytes);
		if (progress != progctx->Progress)
		{
			progctx->Progress = progress;
			progctx->SetString(
				ResStrFormat(String_ApplyingImageState, progress));
		}
	}
	return WIMLIB_PROGRESS_STATUS_CONTINUE;
}

int ApplyWIMImage(WIMStruct* wim, PCWSTR pszPath, int Index, bool* pbCancel, std::function<void(PCWSTR)> AppendText, std::function<void(PCWSTR)> SetString)
{
	AppendText(GetString(String_ImageIndex));
	AppendText((to_wstring(Index) += L". ").c_str());
	AppendText(GetString(String_ApplyingImage));
	AppendText(L"...");

	progctx_t progctx = { UINT_PTR(-1), pbCancel, SetString };
	wimlib_register_progress_function(wim, reinterpret_cast<wimlib_progress_func_t>(progfuc), &progctx);

	int wec = wimlib_extract_image(wim, Index, pszPath, 0);
	wimlib_register_progress_function(wim, nullptr, nullptr);
	SetString(nullptr);
	return wec;
}

static bool ErrorMessage(HRESULT hr, wstring& str, bool* pbDamaged = nullptr)
{
	if (pbDamaged)
		*pbDamaged = false;
	str = GetString(String_Failed).GetPointer();
	str += L"HRESULT: ";
	WCHAR code[11];
	swprintf_s(code, L"0x%08lX", hr);
	str += code;
	str += L"\r\n";
	LPWSTR lpMsgBuf = nullptr;
	FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		nullptr,
		hr,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		reinterpret_cast<LPWSTR>(&lpMsgBuf),
		0, nullptr);
	if (lpMsgBuf)
		str += lpMsgBuf;
	DismString* errstr = nullptr;
	bool ret = true;
	if (SUCCEEDED(DismGetLastErrorMessage(&errstr)) && errstr)
	{
		if (!lpMsgBuf || wcscmp(errstr->Value, lpMsgBuf) != 0)
		{
			str += L"Dism Error Message: ";
			str += errstr->Value;
		}

		if (pbDamaged)
		{
			if (wcsstr(errstr->Value, L".msu") && wcsstr(errstr->Value, L"Unattend.xml"))
			{
				PCWSTR p = wcsstr(errstr->Value, L"\r\n");
				if (p && wcsstr(p + 2, L"\r\n"))
					*pbDamaged = true;
			}
		}
		DismDelete(errstr);
	}
	else
		ret = false;
	if (lpMsgBuf)
		LocalFree(lpMsgBuf);
	return ret;
}


struct CallbackContext
{
	template<class T>
	CallbackContext(DismWrapper& dism, UINT uStringID, T* lParam) : dism(dism), uStringID(uStringID), ProgressPercent(-1), lParam(reinterpret_cast<LPARAM>(lParam)) {}
	CallbackContext(DismWrapper& dism, UINT uStringID, LPARAM lParam) : dism(dism), uStringID(uStringID), ProgressPercent(-1), lParam(lParam) {}
	CallbackContext(DismWrapper& dism, UINT uStringID) : dism(dism), uStringID(uStringID), ProgressPercent(-1), lParam(0) {}

	UINT ProgressPercent;
	LPARAM lParam;
	UINT uStringID;
	DismWrapper& dism;
};

static void CALLBACK ProgressCallback(UINT Current, UINT Total, CallbackContext* ctx)
{
	UINT uCurrentPercent = Current * 100 / Total;
	if (ctx->ProgressPercent != uCurrentPercent)
	{
		ctx->ProgressPercent = uCurrentPercent;
		String Template = GetString(ctx->uStringID);
		auto size = Template.GetLength() + 30;
		if (ctx->uStringID == String_UpgradingEditionState
			|| ctx->uStringID == String_CleaningComponentsState)
			if (Current > Total)
			{
				String Step = ResStrFormat(String_Step, Current / Total + 1);
				if (ctx->uStringID == String_UpgradingEditionState)
					ctx->dism.SetString(
						ResStrFormat(ctx->uStringID, ctx->lParam, Step, ctx->ProgressPercent % 100));
				else
					ctx->dism.SetString(
						ResStrFormat(ctx->uStringID, Step, ctx->ProgressPercent % 100));
			}
			else
			{
				WCHAR null = 0;
				if (ctx->uStringID == String_UpgradingEditionState)
					ctx->dism.SetString(
						ResStrFormat(ctx->uStringID, ctx->lParam, &null, ctx->ProgressPercent));
				else
					ctx->dism.SetString(
						ResStrFormat(ctx->uStringID, &null, ctx->ProgressPercent));
			}
		else if (ctx->lParam == 0)
			ctx->dism.SetString(
				ResStrFormat(ctx->uStringID, ctx->ProgressPercent));
		else if (Current > Total)
			ctx->dism.SetString(
				ResStrFormat(ctx->uStringID, static_cast<ULONG>(HIWORD(ctx->lParam)),
					ResStrFormat(String_Step, Current / Total + 1), ctx->ProgressPercent % 100, static_cast<ULONG>(LOWORD(ctx->lParam))));
		else
		{
			WCHAR null = 0;
			ctx->dism.SetString(
				ResStrFormat(ctx->uStringID, static_cast<ULONG>(HIWORD(ctx->lParam)),
					&null, ctx->ProgressPercent, static_cast<ULONG>(LOWORD(ctx->lParam))));
		}
	}
}

DismWrapper::DismWrapper(PCWSTR ImagePath, PCWSTR TempPath, bool* pbCancel, std::function<void(PCWSTR)> AppendText)
	: Session(0), ImagePath(ImagePath), TempPath(TempPath), pbCancel(pbCancel), AppendText(AppendText)
{
	DismInitialize(DismLogErrors, L"DismLog.txt", TempPath);
	AppendText(GetString(String_OpenDismSession));
	if (OpenSession())
		AppendText(GetString(String_Succeeded));
}

DismWrapper::~DismWrapper()
{
	CloseSession();
	DismShutdown();
}


bool DismWrapper::OpenSession()
{
	HRESULT hr = DismOpenSession(ImagePath, nullptr, nullptr, &Session);
	if (FAILED(hr))
	{
		AppendText(GetString(String_Failed));
		wstring msg;
		ErrorMessage(hr, msg);
		AppendText(msg.c_str());
		return false;
	}
	return true;
}

void DismWrapper::CloseSession(bool bNoPrompt)
{
	if (Session == 0)
		return;
	if (!bNoPrompt)
		AppendText(GetString(String_CloseDismSession));
	DismCloseSession(Session);
	Session = 0;
}

bool DismWrapper::EnableDotNetFx3(PCWSTR SourcePath)
{
	AppendText(ResStrFormat(String_EnableFeature, L"NetFx3"));

	CallbackContext ctx(*this, String_InstallingDotNetFx3);
	HRESULT hr = DismEnableFeature(Session, L"NetFx3", nullptr, DismPackageNone, TRUE, &SourcePath, 1, FALSE, nullptr, reinterpret_cast<DISM_PROGRESS_CALLBACK>(ProgressCallback), &ctx);
	SetString(nullptr);

	if (pbCancel && *pbCancel)
	{
		SetString(nullptr);
		AppendText(GetString(String_Cancelled));
		CloseSession();
		return false;
	}
	else if (FAILED(hr))
	{
		wstring msg;
		ErrorMessage(hr, msg);
		AppendText(msg.c_str());
	}
	else
		AppendText(GetString(String_Succeeded));
	return true;
}

bool DismWrapper::SetEdition(PCWSTR EditionId, bool bNoPrompt)
{
	if (!bNoPrompt)
		AppendText(GetString(String_UpgradingEdition));

	CallbackContext ctx(*this, String_UpgradingEditionState, EditionId);
	HRESULT hr = _DismSetEdition2(Session, EditionId, nullptr, nullptr, reinterpret_cast<DISM_PROGRESS_CALLBACK>(ProgressCallback), &ctx);
	SetString(nullptr);

	wstring str;
	if (FAILED(hr))
	{
		ErrorMessage(hr, str);
		AppendText(str.c_str());
		return false;
	}

	return true;
}

static bool ProcessUpdate(DismWrapper& dism, const Update& update, const SessionContext& ctx, size_t index, bool& Cancel, std::function<bool()> ReopenDismSession)
{
	if (Cancel) return false;

	auto AppendError = [&]()
	{
		LPWSTR lpErrMsg;
		FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
			nullptr,
			GetLastError(),
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			reinterpret_cast<LPWSTR>(&lpErrMsg),
			0, nullptr);
		dism.AppendText(lpErrMsg);
		LocalFree(lpErrMsg);
	};

	TempDir dir(ctx.PathTemp.c_str());
	if (!update.PSF.Empty())
	{
		bool wim = PathMatchSpecW(update.UpdateFile, L"*.wim");

		std::wstring text = GetString(wim ? String_ProcessingFile : String_ExpandingCab).GetPointer();
		if (!wim)
			text += L": ";
		text += update.UpdateFile;
		text += L"...";
		dism.AppendText(text.c_str());
		if (wim)
		{
			WIMStruct* wim;
			auto code = wimlib_open_wim(update.UpdateFile, 0, &wim);
			if (code != 0)
			{
				dism.AppendText(wimlib_get_error_string(static_cast<wimlib_error_code>(code)));
				return true;
			}
			code = wimlib_extract_image(wim, 1, dir, 0);
			wimlib_free(wim);
			if (code != 0)
			{
				dism.AppendText(wimlib_get_error_string(static_cast<wimlib_error_code>(code)));
				return true;
			}
		}
		else if (!ExpandCabFile(update.UpdateFile, dir, nullptr, nullptr))
		{
			AppendError();
			return true;
		}

		if (Cancel)
			return false;

		text = format(L"\r\n{}: {}...", GetString(String_ExpandingPSF).GetPointer(), update.PSF.begin());
		dism.AppendText(text.c_str());
		if (!ExpandPSF(update.PSF, (dir / L"express.psf.cix.xml").c_str(), dir))
		{
			AppendError();
			return true;
		}
		if (Cancel)
			return false;

		text = format(L"{}{}{}...", GetString(String_Succeeded).GetPointer(), GetString(String_AddPackage).GetPointer(), dir.operator const WCHAR *());
		dism.AppendText(text.c_str());

		CallbackContext cbctx(dism, String_InstallingUpdateState, MAKELPARAM(ctx.UpdateVector.size(), index + 1));
		HRESULT hr = DismAddPackage(dism.Session, dir, FALSE, TRUE, nullptr, reinterpret_cast<DISM_PROGRESS_CALLBACK>(ProgressCallback), &cbctx);
		dism.SetString(nullptr);

		if (FAILED(hr))
		{
			if (Cancel)
				return false;

			if (ErrorMessage(hr, text))
			{
				dism.AppendText(text.c_str());
				if (cbctx.ProgressPercent > 100 && !ReopenDismSession())
					return false;
			}
			else
			{
				dism.AppendText(GetString(String_FailedButNoReasons));
				if (!ReopenDismSession())
					return false;
			}
		}
		else if (cbctx.ProgressPercent > 100)
		{
			if (!ReopenDismSession())
				return false;
		}
		else
			dism.AppendText(GetString(String_Succeeded));
		return true;
	}
	else
	{
		CallbackContext cbctx(dism, String_InstallingUpdateState, MAKELPARAM(ctx.UpdateVector.size(), index + 1));
		wstring text = GetString(String_AddPackage).GetPointer();
		text += update.UpdateFile;
		text += L"...";
		dism.AppendText(text.c_str());

		auto Link = dir / PathFindFileNameW(update.UpdateFile);
		PCWSTR Package = Link.c_str();
		if (!CreateSymbolicLinkW(Link.c_str(), update.UpdateFile, 0))
			Package = update.UpdateFile;
		HRESULT hr = DismAddPackage(dism.Session, Package, FALSE, TRUE, nullptr, reinterpret_cast<DISM_PROGRESS_CALLBACK>(ProgressCallback), &cbctx);
		dism.SetString(nullptr);

		if (FAILED(hr))
		{
			if (Cancel)
				return false;

			bool bDamaged;
			if (ErrorMessage(hr, text, &bDamaged))
			{
				dism.AppendText(text.c_str());

				if (bDamaged)
				{
					PWSTR pFile = PathFindFileNameW(update.UpdateFile);
					bDamaged = dism.MessageBox(
						ResStrFormat(String_SystemDestroyed, pFile),
						nullptr, MB_ICONERROR | MB_YESNO) == IDYES;
					if (bDamaged)
						return false;
				}

				if (cbctx.ProgressPercent > 100 && !ReopenDismSession())
					return false;
			}
			else
			{
				text += GetString(String_FailedButNoReasons);
				if (!ReopenDismSession())
					return false;
			}
		}
		else if (cbctx.ProgressPercent > 100)
		{
			if (!ReopenDismSession())
				return false;
		}
		else
			dism.AppendText(GetString(String_Succeeded));
	}
	return true;
}

BYTE DismWrapper::AddUpdates(const SessionContext& ctx)
{
	bool False = false;
	bool& Cancel = pbCancel ? *pbCancel : False;

	auto ReopenDismSession = [&]()
		{
			AppendText(GetString(String_ReopenDismSession));
			CloseSession(true);
			if (OpenSession())
			{
				AppendText(GetString(String_Succeeded));
				return true;
			}
			return false;
		};

	SetString(nullptr);

	for (auto i = ctx.UpdateVector.cbegin(); i != ctx.UpdateVector.cend(); ++i)
	{
		if (Cancel)
			goto Cancelled;

		if (!ProcessUpdate(*this, *i, ctx, i - ctx.UpdateVector.cbegin(), Cancel, ReopenDismSession))
		{
			if (Session == 0) return FALSE;
			goto Interrupted;
		}
	}

	SetString(nullptr);
	if (ctx.bAddEnablementPackage && !ctx.EnablementPackage.Empty())
	{
		wstring text = GetString(String_AddPackage).GetPointer();
		text += ctx.EnablementPackage;
		text += L"...";
		AppendText(text.c_str());
		HRESULT hr = DismAddPackage(Session, ctx.EnablementPackage, FALSE, TRUE, nullptr, nullptr, nullptr);
		SetString(nullptr);

		if (FAILED(hr))
		{
			if (Cancel)
				goto Interrupted;
			if (ErrorMessage(hr, text))
				AppendText(text.c_str());
			else
			{
				text += GetString(String_FailedButNoReasons);
				AppendText(text.c_str());
			}
		}
		else
			AppendText(GetString(String_Succeeded));
	}

	if (ctx.bCleanComponentStore)
	{
		CallbackContext ctx(*this, String_CleaningComponentsState);
		AppendText(GetString(String_CleaningComponents));
		HRESULT hr = _DismCleanImage(Session, DismCleanComponents, 0, nullptr, reinterpret_cast<DISM_PROGRESS_CALLBACK>(ProgressCallback), &ctx);
		SetString(nullptr);

		if (FAILED(hr))
		{
			if (Cancel)
				goto Interrupted;

			wstring text;
			ErrorMessage(hr, text);
			AppendText(text.c_str());
		}
		else
			AppendText(GetString(String_Succeeded));
	}
	return TRUE;

Interrupted:
	AppendText(GetString(String_Cancelled));
Cancelled:
	SetString(nullptr);
	CloseSession();
	return -1;
}

bool DismWrapper::AddApps(SessionContext& ctx)
{
	int nAppxFeatures = 0;
	for (auto& i : ctx.AppxFeatures)
		if (i.bInstall)
			++nAppxFeatures;
	if (ctx.AppVector.size() + nAppxFeatures)
	{
		AppendText(GetString(String_ReopenDismSession));
		CloseSession(true);

		wstring tmpdir;
		DWORD cchName = GetShortPathNameW(ctx.PathTemp.c_str(), nullptr, 0);
		tmpdir.resize(cchName);
		GetShortPathNameW(ctx.PathTemp.c_str(), const_cast<LPWSTR>(tmpdir.c_str()), cchName + 1);
		if (PathIsUNCW(ctx.PathTemp.c_str()))
			// Remove "?\UNC\" from "\\?\UNC\..." to convert to "\\..."
			tmpdir.erase(2, 6);
		else
			// Remove "\\?\" prefix
			tmpdir.erase(0, 4);
		if (ImagePath == L"Mount")
			SetCurrentDirectoryW(tmpdir.c_str());
		DismInitialize(DismLogErrors, L"DismLog.txt", TempPath == ctx.PathTemp.c_str() ? tmpdir.c_str() : TempPath);
		HRESULT hr = DismOpenSession(ImagePath, nullptr, nullptr, &Session);
		if (FAILED(hr))
		{
			wstring msg;
			ErrorMessage(hr, msg);
			AppendText(msg.c_str());
			Session = 0;
			return false;
		}
		AppendText(GetString(String_Succeeded));

		int n = 0;
		function<bool(AppxFeature&)> InstallAppxFeature;
		InstallAppxFeature = [&](AppxFeature& f)
			{
				if (pbCancel && *pbCancel)
					return false;
				if (f.bInstalled)
					return true;
				for (auto& i : f.Dependencies)
					if (!i->bInstalled && !InstallAppxFeature(*i))
						return false;

				++n;
				f.bInstalled = true;
				wstring msg = GetString(String_InstallingAppx).GetPointer();
				msg += f.Bundle;
				msg += L"...";
				AppendText(msg.c_str());
				SetString(ResStrFormat(String_InstallingAppxFeature, n, nAppxFeatures));
				HRESULT hr = AddProvisionedAppxPackage(Session, ctx.PathTemp.c_str(), f, ctx.bStubOptionInstallFull);
				if (pbCancel && *pbCancel)
					return false;
				if (FAILED(hr))
				{
					ErrorMessage(hr, msg);
					AppendText(msg.c_str());
				}
				else
					AppendText(GetString(String_Succeeded));
				return true;
			};
		for (auto& i : ctx.AppxFeatures)
			if (i.bInstall && !InstallAppxFeature(i))
				goto Interrupted;

		for (auto i = ctx.AppVector.begin(); i != ctx.AppVector.end(); ++i)
		{
			wstring msg = GetString(String_InstallingAppx).GetPointer();
			msg += PathFindFileNameW(*i);
			msg += L"...";
			AppendText(msg.c_str());
			SetString(ResStrFormat(String_InstallingAppState, static_cast<UINT>(i - ctx.AppVector.cbegin() + 1), static_cast<UINT>(ctx.AppVector.size())));
			hr = AddProvisionedAppxPackage(Session, *i);
			if (pbCancel && *pbCancel)
				goto Interrupted;
			if (FAILED(hr))
			{
				ErrorMessage(hr, msg);
				AppendText(msg.c_str());
			}
			else
				AppendText(GetString(String_Succeeded));
		}

		SetString(nullptr);
		_DismOptimizeProvisionedAppxPackages(Session);
		if (pbCancel && *pbCancel)
			goto Interrupted;
	}
	return true;

Interrupted:
	SetString(nullptr);
	AppendText(GetString(String_Cancelled));
	CloseSession();
	return false;
}

bool DismWrapper::AddSinglePackage(PCWSTR PackagePath)
{
	HRESULT hr = DismAddPackage(Session, PackagePath, TRUE, FALSE, nullptr, nullptr, nullptr);
	if (FAILED(hr))
	{
		if (pbCancel && *pbCancel)
			AppendText(GetString(String_Cancelled));
		else
		{
			wstring text;
			ErrorMessage(hr, text);
			AppendText(text.c_str());
		}
		CloseSession();
		AppendText(GetString(String_Failed));
		return false;
	}
	return true;
}

bool DismWrapper::AddDrivers(const SessionContext& ctx)
{
	if (ctx.DriverVector.empty())
		return true;

	for (auto i = ctx.DriverVector.cbegin(); i != ctx.DriverVector.cend(); ++i)
	{
		SetString(
			ResStrFormat(String_AddingDriverState, static_cast<UINT>(i - ctx.DriverVector.cbegin() + 1), static_cast<UINT>(ctx.DriverVector.size()))
		);
		wstring msg = GetString(String_AddingDriver).GetPointer();
		msg += *i;
		msg += L"...";
		AppendText(msg.c_str());
		HRESULT hr = DismAddDriver(Session, *i, ctx.bForceUnsigned);
		if (FAILED(hr))
		{
			if (pbCancel && *pbCancel)
			{
				SetString(nullptr);
				AppendText(GetString(String_Cancelled));
				AppendText(GetString(String_CloseDismSession));
				CloseSession();
				return false;
			}
			ErrorMessage(hr, msg);
			AppendText(msg.c_str());
		}
		else
			AppendText(GetString(String_Succeeded));
	}

	SetString(nullptr);
	DeleteDirectory((ctx.PathTemp + L"Drivers").c_str());
	return true;
}
