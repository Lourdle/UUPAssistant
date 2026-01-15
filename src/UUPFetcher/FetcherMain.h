#pragma once
#include <Lourdle.UIFramework.h>
#include <vector>
#include <map>
#include <memory>

struct webview_struct;
struct uup_struct
{
	ULONGLONG id;
	Lourdle::UIFramework::String UUPid;
	Lourdle::UIFramework::String AppUUPid;
	Lourdle::UIFramework::String LocaleName;
	Lourdle::UIFramework::String Editions;
	Lourdle::UIFramework::String Name;
	Lourdle::UIFramework::String Language;
	Lourdle::UIFramework::String EditionFriendlyNames;
	Lourdle::UIFramework::String HtmlVritualEditions;
	Lourdle::UIFramework::String UUPSize;
	Lourdle::UIFramework::String AppSize;
	Lourdle::UIFramework::String SystemSize;
	Lourdle::UIFramework::String Path;
	std::map<Lourdle::UIFramework::String, Lourdle::UIFramework::String> AdditionalEditions;

	struct File
	{
		Lourdle::UIFramework::String Name;
		Lourdle::UIFramework::String SHA1;
		LONGLONG Size;
	};

	using FileList_t = std::vector<File>;

	FileList_t System;
	FileList_t Apps;

	void Reset()
	{
		*this = uup_struct();
	}
};

class FetcherMain :
	public Lourdle::UIFramework::Window
{
public:
	FetcherMain();

	void OnSize(BYTE ResizeType, int nClientWidth, int nClientHeight, Lourdle::UIFramework::WindowBatchPositioner);
	bool GetAutoScrollInfo(bool bVert, int& nPixelsPerPos, PVOID pfnCaller);
	void OnGetMinMaxInfo(PMINMAXINFO pMinMaxInfo);
	void OnClose();
	void OnDestroy();
	void OnDraw(HDC, RECT);
	LRESULT OnNotify(LPNMHDR);
	LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

	void MyCommand();

	void Fetch();
	void ViewTasks();
	void Clean();
	void CreateByUrl();
	void Back();
	void Next();
	void BrowseFile();
	void ViewAppFiles();
	void ViewVirtualEditions();
	void Delete();
	void Redraw();

	void GoToTaskSummary();

	Lourdle::UIFramework::Button FetchButton;
	Lourdle::UIFramework::Button ViewTasksButton;
	Lourdle::UIFramework::Button CleanButton;
	Lourdle::UIFramework::Button CreateTaskByUrlButton;
	Lourdle::UIFramework::Edit EditBox;
	Lourdle::UIFramework::Button BackButton;
	Lourdle::UIFramework::Button NextButton;
	Lourdle::UIFramework::Button Browse;
	Lourdle::UIFramework::ProgressBar Progress;
	Lourdle::UIFramework::ListView FileList;
	Lourdle::UIFramework::Button ViewApps;
	Lourdle::UIFramework::Button ViewAdditionalEditions;
	Lourdle::UIFramework::Button DontDownloadApps;
	Lourdle::UIFramework::Button DeleteButton;
	Lourdle::UIFramework::Static Status;
	bool bWriteToStdOut;

	enum
	{
		Idle,
		WebView,
		GettingInfo,
		SummaryPage,
		TaskList,
		TaskSummary,
		Downloading,
		Done,
		Failed
	} State;

	std::unique_ptr<webview_struct> webview;
	uup_struct uup;
	HICON hBack;
	HICON hCancel;
};
