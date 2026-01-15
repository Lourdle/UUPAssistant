module;
#include "pch.h"
#include "framework.h"
#include "FetcherMain.h"
#include "resource.h"
#include "global_features.h"
#include "../common/common.h"

#include <string>
#include <shellapi.h>

#include <wrl.h>
#include <wil/com.h>
#include <WebView2.h>

using namespace Microsoft::WRL;
using namespace wil;
using namespace std;

import Constants;
import Misc;
import Spiders;

export module WebView;

export struct webview_struct
{
	com_ptr<ICoreWebView2Controller> webviewController;
	com_ptr<ICoreWebView2> webview;
};

export void webviewResize(FetcherMain* p)
{
	if (p->webview->webviewController)
	{
		RECT bounds;
		p->GetClientRect(&bounds);
		bounds.top += Lourdle::UIFramework::GetFontSize() * 4;
		p->webview->webviewController->put_Bounds(bounds);
	}
}

static bool CreateWebView2Environment(FetcherMain* main, PCWSTR WebView2Folder)
{
	return CreateCoreWebView2EnvironmentWithOptions(WebView2Folder, GetAppDataPath(), nullptr,
		Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
			[main](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
				env->CreateCoreWebView2Controller(main->GetHandle(), Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
					[main](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
						if (controller != nullptr)
						{
							main->webview->webviewController = controller;
							main->webview->webviewController->get_CoreWebView2(&main->webview->webview);
						}

						com_ptr<ICoreWebView2Controller2> controller2;
						COREWEBVIEW2_COLOR color = {};
						main->webview->webviewController->QueryInterface(&controller2);
						controller2->put_DefaultBackgroundColor(color);

						webviewResize(main);
						main->BackButton.ShowWindow(SW_SHOW);
						main->webview->webview->Navigate(UUPDUMP_WEBSITE_BASE_URL);

						EventRegistrationToken token;
						main->webview->webview->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
							[main](ICoreWebView2* webview, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
								if (main->State != main->WebView)
									return S_OK;

								unique_cotaskmem_string uri;
								args->get_Uri(&uri);
								main->EditBox.SetWindowText(uri.get());
								if (wcsncmp((wstring(UUPDUMP_WEBSITE_BASE_URL) += L"/download.php?").c_str(), uri.get(), 33) == 0)
								{
									if (!SetCurrentDirectoryW(GetAppDataPath()))
									{
										main->ErrorMessageBox();
										main->BackButton.PostCommand();
										return S_OK;
									}
									com_ptr<ICoreWebView2_2> webview2;
									webview->QueryInterface(&webview2);
									com_ptr<ICoreWebView2CookieManager> cookieManager;
									webview2->get_CookieManager(&cookieManager);
									wstring cookies;
									cookieManager->GetCookies(UUPDUMP_WEBSITE_BASE_URL, Callback<ICoreWebView2GetCookiesCompletedHandler>(
										[main](HRESULT result, ICoreWebView2CookieList* cookieList) -> HRESULT {
											UINT count;
											cookieList->get_Count(&count);
											wstring cookies;
											for (UINT i = 0; i != count; ++i)
											{
												com_ptr<ICoreWebView2Cookie> cookie;
												cookieList->GetValueAtIndex(i, &cookie);
												if (!cookies.empty())
													cookies += L"; ";
												unique_cotaskmem_string name;
												cookie->get_Name(&name);
												cookies += name.get();
												cookies += L"=";
												unique_cotaskmem_string value;
												cookie->get_Value(&value);
												cookies += value.get();
											}
											main->BackButton.SetImageIcon(main->hBack);

											StartSpiderThread(main, cookies);
											return S_OK;
										}).Get());
									main->State = main->GettingInfo;
									args->put_Cancel(TRUE);
									main->webview->webviewController->put_IsVisible(FALSE);
								}
								else
									main->Progress.SetPos(ProgressStepsPerPercent * 80);
								main->BackButton.SetImageIcon(main->hCancel);
								main->Progress.ShowWindow(SW_SHOW);
								return S_OK;
							}).Get(), &token);

						main->webview->webview->add_NavigationCompleted(
							Callback<ICoreWebView2NavigationCompletedEventHandler>(
								[main](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args)
								-> HRESULT {
									if (main->State != main->WebView)
										return S_OK;
									unique_cotaskmem_string uri;
									sender->get_Source(&uri);
									if (wcscmp(uri.get(), L"about:blank") == 0)
									{
										main->webview->webview->GoBack();
										return S_OK;
									}

									main->webview->webviewController->put_IsVisible(TRUE);
									main->EditBox.SetWindowText(uri.get());
									main->BackButton.SetImageIcon(main->hBack);
									main->Progress.SetPos(ProgressStepMax);
									main->PostCommand(main->Progress, 0, kDisplaySummaryPageNotifyId);
									return S_OK;
								})
							.Get(), &token);
						return S_OK;
					}).Get());
				return S_OK;
			}).Get()) == S_OK;
}

using namespace Lourdle::UIFramework;
struct GetWebView2Folder : Dialog
{
	GetWebView2Folder(FetcherMain* p, String& Path) : Dialog(p, GetFontSize() * 45, GetFontSize() * 10, WS_CAPTION | WS_BORDER | WS_SYSMENU | DS_FIXEDSYS | DS_MODALFRAME, L"WebView2 folder"),
		Path(Path), BrowseButton(this, &GetWebView2Folder::Browse), OKButton(this, &GetWebView2Folder::OK), FolderEdit(this, 0)
	{
	}

	void Init(LPARAM)
	{
		int pxUnit = GetFontSize();
		OKButton.SetWindowText(GetString(String_Next));
		BrowseButton.SetWindowText(GetString(String_Browse));
		FolderEdit.SetWindowText(Path);
		OKButton.MoveWindow(pxUnit * 40, pxUnit * 7, pxUnit * 4, pxUnit * 2);
		FolderEdit.MoveWindow(pxUnit, pxUnit, pxUnit * 43, pxUnit * 2);
		BrowseButton.MoveWindow(pxUnit * 40, pxUnit * 4, pxUnit * 4, pxUnit * 2);
	}

	Button BrowseButton;
	Button OKButton;
	Edit FolderEdit;

	String& Path;

	void Browse()
	{
		OleInitialize(nullptr);
		String Path;
		if (GetOpenFolderName(this, Path))
			FolderEdit.SetWindowText(Path);
		OleUninitialize();
	}

	void OK()
	{
		Path = FolderEdit.GetWindowText();
		EndDialog(1);
	}
};

static bool TryCreateWebView2Environment(FetcherMain* main, String* WebView2Folder)
{
	if (!CreateWebView2Environment(main, WebView2Folder ? *WebView2Folder : nullptr))
		switch (main->MessageBox(GetString(String_UnableToCreateEnv), nullptr, MB_ICONERROR | MB_YESNOCANCEL))
		{
		case IDYES:
			if (String path; GetWebView2Folder(main, WebView2Folder ? *WebView2Folder : path).ModalDialogBox(0))
				return TryCreateWebView2Environment(main, WebView2Folder ? WebView2Folder : &path);
			else
				return false;
		case IDNO:
			ShellExecuteA(nullptr, "open", "https://developer.microsoft.com/microsoft-edge/webview2#download", nullptr, nullptr, SW_SHOWNORMAL);
		default:
			main->CreateTaskByUrlButton.PostCommand();
			return false;
		}

	return true;
}

void FetcherMain::Fetch()
{
	uup = uup_struct();
	webview.reset(new webview_struct);

	if (!TryCreateWebView2Environment(this, nullptr))
	{
		webview.reset();
		return;
	}

	FetchButton.ShowWindow(SW_HIDE);
	ViewTasksButton.ShowWindow(SW_HIDE);
	CleanButton.ShowWindow(SW_HIDE);
	CreateTaskByUrlButton.ShowWindow(SW_HIDE);
	RECT rect;
	GetClientRect(&rect);
	int pxUnit = Lourdle::UIFramework::GetFontSize();
	EditBox.MoveWindow(pxUnit * 7 / 2, pxUnit, rect.right - pxUnit * 7 / 2, pxUnit * 2);
	EditBox.SetReadOnly(true);
	Progress.ShowWindow(SW_HIDE);
	Progress.MoveWindow(0, pxUnit * 4 - pxUnit / 4, rect.right, pxUnit / 4);
	State = WebView;
	Invalidate();
}
