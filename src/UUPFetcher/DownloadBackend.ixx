module;
#include "pch.h"
#include "framework.h"
#include "FetcherMain.h"
#include "resource.h"
#include "global_features.h"
#include "../common/common.h"

#include <Shlwapi.h>
#include <wincrypt.h>

#include <string>
#include <thread>
#include <atomic>
#include <format>
#include <vector>
#include <memory>


using namespace Lourdle::UIFramework;

import Misc;
import UUPdump;
import Constants;

export module DownloadHost;

constexpr int kAppxParallelHosts = 4;
constexpr int kSystemHosts = 1;
constexpr int kMaxPhaseRetries = 3;

constexpr int kSha1HexChars = 40;
constexpr int kSha1Bytes = 20;

constexpr DWORD kVerifyReadBufferBytes = 8u * 1024u * 1024u;
constexpr DWORD kTaskPollIntervalMs = 1000u;

constexpr int kTotalDownloadThreadsBudget = 12;
constexpr int kCreateTaskMaxAttempts = 5;

constexpr int kScrollMarginX = 4;
constexpr int kScrollMarginY = 24;

constexpr size_t kVerifyStatusExtraChars = 32;

constexpr UINT kMsgGetUrlsDone = WM_USER + 2;
constexpr UINT kMsgDownloadWorkersDone = WM_USER + 4;
constexpr UINT kMsgCheckHasDone = WM_USER + 5;
constexpr UINT kMsgVerifyDone = WM_USER + 6;

export struct DownloadBE
{
	HANDLE hProcess;
	HWND hStatic;
	HWND hListView;
	PWSTR pId;
	PWSTR pAppId;
	PWSTR pLangAndEdition;
	PWSTR pSHA1String;
	decltype(FetcherMain::State)* State;
	int nAppxFiles;
	int nSysFiles;
	int nExitCode;
}*g_pDownloadBackend;

static std::atomic<DWORD> g_downloadHostThreadId = 0;

export DWORD GetDownloadHostThreadId()
{
	return g_downloadHostThreadId.load(std::memory_order_relaxed);
}

export bool StartDownloadHost(FetcherMain& main)
{
	WCHAR szPath[MAX_PATH];
	GetModuleFileNameW(NULL, szPath, MAX_PATH);
	STARTUPINFOW si = { sizeof(si) };
	PROCESS_INFORMATION pi;
	if (!CreateProcessW(szPath, nullptr, nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, nullptr, &si, &pi))
		return false;

	auto Fail = [&]() -> bool
		{
			TerminateProcess(pi.hProcess, 0);
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
			return false;
		};

	DownloadBE dbe = {
		.hStatic = main.Status,
		.hListView = main.FileList,

		// Allocate and write UUPid and AppUUPid into the target process
		.pId = reinterpret_cast<PWSTR>(VirtualAllocEx(pi.hProcess, nullptr, sizeof(WCHAR) * (1 + main.uup.UUPid.GetLength()), MEM_COMMIT, PAGE_READWRITE)),
		.pAppId = reinterpret_cast<PWSTR>(VirtualAllocEx(pi.hProcess, nullptr, sizeof(WCHAR) * (1 + main.uup.AppUUPid.GetLength()), MEM_COMMIT, PAGE_READWRITE)),

		.State = &main.State,
		.nAppxFiles = static_cast<int>(main.uup.Apps.size()),
		.nSysFiles = static_cast<int>(main.uup.System.size()),
		.nExitCode = static_cast<int>(Random()) << 16 | static_cast<int>(Random()),
	};
	if (!dbe.pId || !dbe.pAppId)
		return Fail();
	if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), pi.hProcess, &dbe.hProcess, 0, FALSE, DUPLICATE_SAME_ACCESS))
		return Fail();

	if (!WriteProcessMemory(pi.hProcess, dbe.pId, main.uup.UUPid, sizeof(WCHAR) * (1 + main.uup.UUPid.GetLength()), nullptr))
		return Fail();
	if (!WriteProcessMemory(pi.hProcess, dbe.pAppId, main.uup.AppUUPid, sizeof(WCHAR) * (1 + main.uup.AppUUPid.GetLength()), nullptr))
		return Fail();

	// Prepare language and edition string and write it into target process
	std::wstring LangAndEdition = std::format(L"&lang={}&edition={}", main.uup.Language.GetPointer(), main.uup.Editions.GetPointer());
	dbe.pLangAndEdition = reinterpret_cast<PWSTR>(VirtualAllocEx(pi.hProcess, nullptr, sizeof(WCHAR) * (1 + LangAndEdition.size()), MEM_COMMIT, PAGE_READWRITE));
	if (!dbe.pLangAndEdition)
		return Fail();
	if (!WriteProcessMemory(pi.hProcess, dbe.pLangAndEdition, LangAndEdition.c_str(), sizeof(WCHAR) * (1 + LangAndEdition.size()), nullptr))
		return Fail();

	// Allocate and write SHA1 strings for all files
	dbe.pSHA1String = reinterpret_cast<PWSTR>(VirtualAllocEx(pi.hProcess, nullptr, sizeof(WCHAR) * static_cast<SIZE_T>(kSha1HexChars) * (dbe.nAppxFiles + dbe.nSysFiles), MEM_COMMIT, PAGE_READWRITE));
	if (!dbe.pSHA1String)
		return Fail();
	MyUniquePtr<WCHAR> pSHA1Buffer = static_cast<SIZE_T>(kSha1HexChars) * (dbe.nAppxFiles + dbe.nSysFiles);
	for (int i = 0; i != dbe.nAppxFiles; ++i)
		memcpy(pSHA1Buffer + kSha1HexChars * i, main.uup.Apps[i].SHA1, kSha1HexChars * sizeof(WCHAR));
	for (int i = 0; i != dbe.nSysFiles; ++i)
		memcpy(pSHA1Buffer + kSha1HexChars * (i + dbe.nAppxFiles), main.uup.System[i].SHA1, kSha1HexChars * sizeof(WCHAR));
	if (!WriteProcessMemory(pi.hProcess, dbe.pSHA1String, pSHA1Buffer, sizeof(WCHAR) * kSha1HexChars * (dbe.nAppxFiles + dbe.nSysFiles), nullptr))
		return Fail();

	// Allocate space for DownloadBE structure in target process and set global pointer
	DownloadBE* pRemoteBackend = reinterpret_cast<DownloadBE*>(VirtualAllocEx(pi.hProcess, nullptr, sizeof(DownloadBE), MEM_COMMIT, PAGE_READWRITE));
	if (!pRemoteBackend)
		return Fail();
	if (!WriteProcessMemory(pi.hProcess, pRemoteBackend, &dbe, sizeof(DownloadBE), nullptr))
		return Fail();
	if (!WriteProcessMemory(pi.hProcess, &g_pDownloadBackend, &pRemoteBackend, sizeof(g_pDownloadBackend), nullptr))
		return Fail();

	ResumeThread(pi.hThread);
	CloseHandle(pi.hThread);

	// Create a thread to monitor the download process and update UI status accordingly
	std::thread([](FetcherMain& main, HANDLE hProcess, int normalExitCode)
		{
			WaitForSingleObject(hProcess, INFINITE);
			DWORD dwExitCode;
			GetExitCodeProcess(hProcess, &dwExitCode);
			CloseHandle(hProcess);
			main.Status.ShowWindow(SW_HIDE);
			if (dwExitCode != normalExitCode)
			{
				main.State = main.Failed;
				MessageBeep(MB_ICONERROR);
				main.MessageBox(GetString(String_DownloaderClosed), nullptr, MB_ICONERROR);
			}
			else if (main.State == main.Done && main.bWriteToStdOut)
			{
				RECT rect;
				main.GetClientRect(&rect);
				main.OnSize(0, rect.right, rect.bottom, WindowBatchPositioner());
			}
		},
		std::ref(main), pi.hProcess, dbe.nExitCode).detach();

	// Save thread id for UI process to control the download host.
	g_downloadHostThreadId.store(pi.dwThreadId, std::memory_order_relaxed);
	return true;
}

struct DownloadState
{
	DownloadState()
	{
		InitializeCriticalSection(&cs);
	}
	~DownloadState()
	{
		DeleteCriticalSection(&cs);
	}

	std::wstring StateString;
	std::wstring ErrorString;
	CRITICAL_SECTION cs;
};

static void DownloadHost(int nhosts, DownloadState* pds, DWORD dwThreadId, HANDLE hEvent,
	FileUrls& urls, std::wstring* perrstr, std::atomic<int>& Current, int nEnd,
	PWSTR pBuffer, LVITEMW* pLVItem, POINT* pPoint,
	PWSTR pRemoteBuffer, LVITEMW* pRemoteLVItem, POINT* pRemotePoint)
{
	if (nhosts <= 0)
		return;

	// Notify DownloadHostMain that this worker is done.
	constexpr UINT kMsgWorkerDone = kMsgDownloadWorkersDone;

	// Retrieve common strings from resource
	auto finishedStr = GetString(String_Finished);
	auto failedStr = GetString(String_Failed);
	auto downloadingStr = GetString(String_Downloading);
	auto speedStr = GetString(String_Speed);

	auto AppendDownloaderLastError = [&](PCWSTR fileName)
		{
			if (!perrstr)
				return;
			*perrstr += fileName;
			*perrstr += L": ";
			if (PWSTR msg = LRDLdlGetErrorMessage())
			{
				*perrstr += msg;
				LRDLdlDelete(msg);
			}
			else *perrstr += L"(unknown error)";
			*perrstr += L"\r\n";
		};

	auto AppendTaskError = [&](PCWSTR fileName, LRDLDLTASK* task, const LRDLDLTASKPROGRESS& progress)
		{
			if (!perrstr)
				return;
			if (task)
			{
				for (BYTE t = 0; t < progress.nThreads; ++t)
				{
					DWORD dwErrCode = ERROR_SUCCESS;
					LRDLdlGetThreadError(task, t, &dwErrCode);
					if (dwErrCode == ERROR_SUCCESS)
						continue;

					*perrstr += fileName;
					*perrstr += L": ";
					if (PWSTR msg = LRDLdlGetErrorMessage(dwErrCode))
					{
						*perrstr += msg;
						LRDLdlDelete(msg);
					}
					else
					{
						*perrstr += L"(unknown error)";
					}
					*perrstr += L"\r\n";
					return;
				}
			}

			AppendDownloaderLastError(fileName);
		};

	// Lambda to get item text from listview
	auto GetItemText = [&](int Index, int SubItem)
		{
			*pLVItem = LVITEMW{
				.iSubItem = SubItem,
				.pszText = pRemoteBuffer,
				.cchTextMax = MAX_PATH
			};
			SendMessageW(g_pDownloadBackend->hListView, LVM_GETITEMTEXT, Index, reinterpret_cast<LPARAM>(pRemoteLVItem));
		};

	// Lambda to set item text in listview
	auto SetItemText = [&](int Index, int SubItem, PCWSTR pText, bool AutoRestoreBuffer = false)
		{
			*pLVItem = LVITEMW{
				.iSubItem = SubItem,
				.pszText = pRemoteBuffer
			};
			if (AutoRestoreBuffer)
			{
				WCHAR szOldBuffer[MAX_PATH];
				wcscpy_s(szOldBuffer, pBuffer);
				wcscpy_s(pBuffer, MAX_PATH, pText);
				SendMessageW(g_pDownloadBackend->hListView, LVM_SETITEMTEXT, Index, reinterpret_cast<LPARAM>(pRemoteLVItem));
				wcscpy_s(pBuffer, MAX_PATH, szOldBuffer);
			}
			else
			{
				wcscpy_s(pBuffer, MAX_PATH, pText);
				SendMessageW(g_pDownloadBackend->hListView, LVM_SETITEMTEXT, Index, reinterpret_cast<LPARAM>(pRemoteLVItem));
			}
		};

	// Process each file from Current index to nEnd
	for (int i = Current++; i < nEnd; i = Current++)
	{
		std::wstring urlPrefix = std::format(L"{}/getfile.php?id={}&file=", UUPDUMP_WEBSITE_BASE_URL,
			i < g_pDownloadBackend->nAppxFiles ? g_pDownloadBackend->pAppId : g_pDownloadBackend->pId);

		bool resume = true;
		GetItemText(i, 1);
		if (finishedStr == pBuffer)
			continue;

		// Check if file exists locally
		GetItemText(i, 0);
		if (GetFileAttributesW(pBuffer) == INVALID_FILE_ATTRIBUTES)
			resume = false;

		// Scroll listview if there is only one host
		if (nhosts == 1)
		{
			ListView_GetItemPosition(g_pDownloadBackend->hListView, i, pRemotePoint);
			ListView_Scroll(g_pDownloadBackend->hListView, pPoint->x - kScrollMarginX, pPoint->y - kScrollMarginY);
		}

		// Update state text
		EnterCriticalSection(&pds->cs);
		pds->StateString = downloadingStr.GetPointer();
		pds->StateString += ' ';
		pds->StateString += pBuffer;
		LeaveCriticalSection(&pds->cs);
		SetItemText(i, 1, downloadingStr, true);
		SetEvent(hEvent);

		// Obtain the correct URL from urls map, else use default URL
		std::wstring url;
		if (auto it = urls.find(pBuffer); it != urls.end())
			url = it->second;
		else
			url = urlPrefix + pBuffer;

		auto CreateTaskForNewDownload = [&](std::wstring& inoutUrl) -> LRDLDLTASK*
			{
				LRDLDLTASK* t = LRDLdlCreateTask(inoutUrl.c_str());
				if (!t && perrstr)
				{
					inoutUrl = urlPrefix + pBuffer;
					for (int attempt = 2; attempt <= kCreateTaskMaxAttempts && !t; ++attempt)
						t = LRDLdlCreateTask(inoutUrl.c_str());
				}
				return t;
			};

		auto ResumeTaskOrFallback = [&](const std::wstring& resumeUrl) -> LRDLDLTASK*
			{
				LRDLDLTASK* t = LRDLdlResumeTask(pBuffer, resumeUrl.c_str());
				if (t && LRDLdlBeginTask(t))
					return t;
				if (t) LRDLdlDeleteTask(t);
				std::wstring fallback = urlPrefix + pBuffer;
				t = LRDLdlResumeTask(pBuffer, fallback.c_str());
				if (t && LRDLdlBeginTask(t))
					return t;
				if (t) LRDLdlDeleteTask(t);
				return nullptr;
			};

		// Create/download or resume
		LRDLDLTASK* task = nullptr;
		if (!resume)
		{
			task = CreateTaskForNewDownload(url);
			if (!task)
			{
				AppendDownloaderLastError(pBuffer);
				SetItemText(i, 1, failedStr);
				continue;
			}
			// Get resource info and update file size info in state text
			LRDLDLRESINFO* resinfo = LRDLdlGetResourceInfo(task);
			if (resinfo)
			{
				WCHAR sz[32];
				StrFormatByteSizeW(resinfo->ullResourceBytes, sz, 32);
				EnterCriticalSection(&pds->cs);
				pds->StateString += std::format(L" (0/{})", sz);
				LeaveCriticalSection(&pds->cs);
				SetEvent(hEvent);
				LRDLdlDelete(resinfo);
			}
			if (!LRDLdlInitializeTask(task, pBuffer, kTotalDownloadThreadsBudget / nhosts, nullptr, true, true) || !LRDLdlBeginTask(task))
			{
				AppendDownloaderLastError(pBuffer);
				LRDLdlDeleteTaskAndFile(task);
				SetItemText(i, 1, failedStr);
				continue;
			}
		}
		else
		{
			task = ResumeTaskOrFallback(url);
			if (!task)
			{
				AppendDownloaderLastError(pBuffer);
				SetItemText(i, 1, failedStr);
				continue;
			}
		}

		// Monitor task progress and update listview with percentage
		LRDLDLTASKPROGRESS progress;
		int lastProgress = -1;
		WCHAR Downloaded[32], Total[32], Speed[32];
		while (!LRDLdlWaitForTask(task, kTaskPollIntervalMs))
		{
			LRDLdlGetTaskProgress(task, &progress);
			StrFormatByteSizeW(progress.ullFetchedBytes, Downloaded, 32);
			StrFormatByteSizeW(progress.ullTotalBytes, Total, 32);
			StrFormatByteSizeW(progress.ullBytesPerSecond, Speed, 32);
			EnterCriticalSection(&pds->cs);
			pds->StateString = std::format(L"{} "
				L"{} ({} / {})"
				L"{}{} {}/s",
				downloadingStr.GetPointer(),
				pBuffer, Downloaded, Total,
				(nhosts == 1 ? L"\r\n" : L" "), speedStr.GetPointer(), Speed);
			LeaveCriticalSection(&pds->cs);
			SetEvent(hEvent);

			int prog = 0;
			if (progress.ullTotalBytes != 0)
				prog = static_cast<int>(progress.ullFetchedBytes * 100 / progress.ullTotalBytes);
			if (prog != lastProgress)
			{
				std::wstring percent = std::format(L"{}%", prog);
				SetItemText(i, 1, percent.c_str(), true);
				lastProgress = prog;
			}
		}

		// Finalize download: update listview status
		LRDLdlGetTaskProgress(task, &progress);
		if (progress.State == LRDLDLSTATE_FINISHED)
			SetItemText(i, 1, finishedStr);
		else
		{
			AppendTaskError(pBuffer, task, progress);
			SetItemText(i, 1, failedStr);
		}
		LRDLdlDeleteTask(task);
	}

	pds->StateString.clear();
	PostThreadMessageW(dwThreadId, kMsgWorkerDone, 0, 0);
}

static void Verify(DWORD dwThreadId, std::atomic_int& n, HANDLE hEvent)
{
	n = 0;
	std::atomic_bool hasBadFile = false;
	const int totalFiles = g_pDownloadBackend->nAppxFiles + g_pDownloadBackend->nSysFiles;

	auto Failed = GetString(String_Failed);
	auto failedStr = Failed.GetPointer();
	auto failedStrLen = sizeof(WCHAR) * (Failed.GetLength() + 1);

#ifdef _USE_OMP
#pragma omp parallel
#else
	std::atomic_int currentIndex = 0;
	auto VerifyThread = [&, hEvent, totalFiles, failedStr, failedStrLen]()
#endif
		{
			WCHAR szBuffer[MAX_PATH];
			PWSTR pRemoteBuffer = reinterpret_cast<PWSTR>(VirtualAllocEx(g_pDownloadBackend->hProcess, nullptr, sizeof(szBuffer), MEM_COMMIT, PAGE_READWRITE));
			LVITEMW* pRemoteLVItem = reinterpret_cast<LVITEMW*>(VirtualAllocEx(g_pDownloadBackend->hProcess, nullptr, sizeof(LVITEMW), MEM_COMMIT, PAGE_READWRITE));
			bool remoteIoOk = true;

			// Reuse buffer within the thread to reduce allocations.
			std::unique_ptr<BYTE[]> readBuffer;
			constexpr DWORD bufferLength = kVerifyReadBufferBytes;
			LVITEMW lvi = {
				.pszText = pRemoteBuffer,
				.cchTextMax = MAX_PATH
			};

			auto GetItemTextRemote = [&](int itemIndex, int subItemIndex) -> bool
				{
					lvi.iSubItem = subItemIndex;
					if (!WriteProcessMemory(g_pDownloadBackend->hProcess, pRemoteLVItem, &lvi, sizeof(LVITEMW), nullptr))
						return remoteIoOk = false;

					LRESULT chars = SendMessageW(g_pDownloadBackend->hListView, LVM_GETITEMTEXT, itemIndex, reinterpret_cast<LPARAM>(pRemoteLVItem));
					if (chars <= 0)
						return remoteIoOk = false;

					if (!ReadProcessMemory(g_pDownloadBackend->hProcess, pRemoteBuffer, szBuffer, sizeof(szBuffer), nullptr))
						return remoteIoOk = false;

					return true;
				};

			auto SetItemTextRemote = [&](int itemIndex, int subItemIndex, PCWSTR text) -> bool
				{
					if (!text)
						text = L"";

					const size_t maxChars = MAX_PATH - 1;
					const size_t len = wcsnlen(text, maxChars);
					wmemcpy(szBuffer, text, len);
					szBuffer[len] = L'\0';
					if (!WriteProcessMemory(g_pDownloadBackend->hProcess, pRemoteBuffer, szBuffer, sizeof(WCHAR) * (len + 1), nullptr))
						return remoteIoOk = false;

					lvi.iSubItem = subItemIndex;
					if (!WriteProcessMemory(g_pDownloadBackend->hProcess, pRemoteLVItem, &lvi, sizeof(LVITEMW), nullptr))
						return remoteIoOk = false;

					LRESULT ok = SendMessageW(g_pDownloadBackend->hListView, LVM_SETITEMTEXT, itemIndex, reinterpret_cast<LPARAM>(pRemoteLVItem));
					if (!ok)
						return remoteIoOk = false;

					return true;
				};
			HCRYPTPROV hCryptProv = 0;
			if (!pRemoteBuffer || !pRemoteLVItem)
			{
				hasBadFile = true;
				goto Cleanup;
			}
			if (!CryptAcquireContextW(&hCryptProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
			{
				// Can't verify, mark all remaining as failed.
				hasBadFile = true;
				goto Cleanup;
			}

			readBuffer = std::make_unique<BYTE[]>(bufferLength);

#ifdef _USE_OMP
#pragma omp for schedule(dynamic)
			for (int i = 0; i < totalFiles; ++i)
#else
			for (int i = currentIndex++; i < totalFiles; i = currentIndex++)
#endif
			{
				if (!remoteIoOk)
					break;

				if (!GetItemTextRemote(i, 0))
				{
					hasBadFile = true;
					break;
				}

				WCHAR filePath[MAX_PATH];
				wcscpy_s(filePath, szBuffer);

				// Open file for verification
				WinHandle hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (hFile == INVALID_HANDLE_VALUE)
				{
				FileIoFailure:
					if (!SetItemTextRemote(i, 1, failedStr))
					{
						hasBadFile = true;
						break;
					}
					++n;
					SetEvent(hEvent);
					hasBadFile = true;
					continue;
				}
				HCRYPTHASH hCryptHash;
				if (!CryptCreateHash(hCryptProv, CALG_SHA1, 0, 0, &hCryptHash))
					goto FileIoFailure;

				SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);
				for (;;)
				{
					DWORD dwRead = 0;
					if (!ReadFile(hFile, readBuffer.get(), bufferLength, &dwRead, nullptr))
					{
						CryptDestroyHash(hCryptHash);
						goto FileIoFailure;
					}
					if (dwRead == 0)
						break;
					if (!CryptHashData(hCryptHash, readBuffer.get(), dwRead, 0))
					{
						CryptDestroyHash(hCryptHash);
						goto FileIoFailure;
					}
				}

				BYTE hash[kSha1Bytes];
				DWORD cbHash = sizeof(hash);
				if (!CryptGetHashParam(hCryptHash, HP_HASHVAL, hash, &cbHash, 0))
				{
					CryptDestroyHash(hCryptHash);
					goto FileIoFailure;
				}
				CryptDestroyHash(hCryptHash);

				WCHAR szSHA1[kSha1HexChars + 1];
				for (int j = 0; j != kSha1Bytes; ++j)
					swprintf_s(szSHA1 + 2 * j, 3, L"%02X", hash[j]);
				if (_wcsnicmp(szSHA1, g_pDownloadBackend->pSHA1String + kSha1HexChars * i, kSha1HexChars) != 0)
				{
					if (!SetItemTextRemote(i, 1, failedStr))
					{
						hasBadFile = true;
						break;
					}
					hasBadFile = true;
					DeleteFileOnClose(hFile);
				}

				++n;
				SetEvent(hEvent);
			}

			CryptReleaseContext(hCryptProv, 0);
		Cleanup:
			if (pRemoteBuffer)
				VirtualFreeEx(g_pDownloadBackend->hProcess, pRemoteBuffer, 0, MEM_RELEASE);
			if (pRemoteLVItem)
				VirtualFreeEx(g_pDownloadBackend->hProcess, pRemoteLVItem, 0, MEM_RELEASE);
		};

#ifndef _USE_OMP
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	std::vector<std::thread> threads;
	threads.reserve(sysInfo.dwNumberOfProcessors);
	for (int i = 0; i != static_cast<int>(sysInfo.dwNumberOfProcessors); ++i)
	{
		threads.emplace_back(VerifyThread);
		SetThreadIdealProcessor(threads.back().native_handle(), i);
	}
	for (auto& t : threads)
		t.join();
#endif

	PostThreadMessageW(dwThreadId, kMsgVerifyDone, hasBadFile, 0);
}

static void ErrorBox(HWND hWnd, PCWSTR pErrText)
{
	// Define a dialog structure for displaying error messages
	struct ErrorBoxStruct : Dialog
	{
		ErrorBoxStruct(WindowBase* Parent)
			: Dialog(Parent, GetFontSize() * 40, GetFontSize() * 20,
				DS_MODALFRAME | DS_FIXEDSYS | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX,
				GetString(String_DownloadError)),
			ErrorText(this, 0, true)
		{
		}

		void Init(LPARAM pText)
		{
			ErrorText.AddWindowStyle(ES_READONLY);
			RECT rect;
			GetClientRect(&rect);
			ErrorText.MoveWindow(0, 0, rect.right, rect.bottom);
			ErrorText.SetWindowText(reinterpret_cast<PCWSTR>(pText));
			MessageBeep(MB_ICONERROR);
		}

		void OnSize(BYTE, int w, int h, WindowBatchPositioner wbp)
		{
			wbp.MoveWindow(ErrorText, 0, 0, w, h, TRUE);
		}

		Edit ErrorText;
	};

	UIFrameworkInit();
	WindowBase parentWnd(hWnd);
	ErrorBoxStruct(&parentWnd).ModalDialogBox(reinterpret_cast<LPARAM>(pErrText));
	UIFrameworkUninit();
}

static void StatusThread(HANDLE hEvent, volatile bool* pStop, HWND hStatic, DownloadState* states, int count)
{
	while (!*pStop)
	{
		std::wstring statusText;
		for (int i = 0; i < count; ++i)
		{
			EnterCriticalSection(&states[i].cs);
			statusText += states[i].StateString;
			statusText += L"\r\n";
			LeaveCriticalSection(&states[i].cs);
		}
		SendMessageW(hStatic, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(statusText.c_str()));
		WaitForSingleObject(hEvent, INFINITE);
	}
}


constexpr UINT_PTR kSharedMemorySignature = 0114514;

constexpr UINT kMsgOpenSharedMemory = WM_USER + 114;
constexpr UINT kMsgCloseSharedMemory = WM_USER + 514;

LRESULT FetcherMain::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case kMsgCloseSharedMemory:
		UnmapViewOfFile(reinterpret_cast<PVOID>(lParam));
		CloseHandle(reinterpret_cast<HANDLE>(wParam));
		break;
	default:
		return Window::WindowProc(uMsg, wParam, lParam);
	case kMsgOpenSharedMemory:
		PVOID pMem = MapViewOfFile(reinterpret_cast<HANDLE>(wParam), FILE_MAP_ALL_ACCESS, 0, 0, 0);
		if (pMem)
		{
			*reinterpret_cast<PVOID*>(pMem) = pMem;
			reinterpret_cast<UINT_PTR*>(pMem)[1] = kSharedMemorySignature;
		}
	}
	return 0;
}

struct SharedMemory
{
	SharedMemory(HANDLE hProcess, HWND hWnd, DWORD dwSize) : hWnd(nullptr)
	{
		hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, dwSize, nullptr);
		if (!hMap) return;

		pMem = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
		if (!pMem)
		{
			CloseHandle(hMap);
			return;
		}

		if (!DuplicateHandle(GetCurrentProcess(), hMap, hProcess, &hMapRemote, 0, FALSE, DUPLICATE_SAME_ACCESS))
		{
			UnmapViewOfFile(pMem);
			CloseHandle(hMap);
			return;
		}


		reinterpret_cast<UINT_PTR*>(pMem)[1] = 0;
		SendMessageW(hWnd, kMsgOpenSharedMemory, reinterpret_cast<WPARAM>(hMapRemote), 0);
		if (reinterpret_cast<UINT_PTR*>(pMem)[1] == kSharedMemorySignature)
		{
			pMemRemote = *reinterpret_cast<PVOID*>(pMem);
			this->hWnd = hWnd;
		}
		else
		{
			// Close remote mapping and event handles
			if (DuplicateHandle(hProcess, hMapRemote, GetCurrentProcess(), &hMapRemote, 0, FALSE, DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS))
				CloseHandle(hMapRemote);

			UnmapViewOfFile(pMem);
			CloseHandle(hMap);
		}
	}

	~SharedMemory()
	{
		if (hWnd)
		{
			SendMessageW(hWnd, kMsgCloseSharedMemory, reinterpret_cast<WPARAM>(hMapRemote), reinterpret_cast<LPARAM>(pMemRemote));
			UnmapViewOfFile(pMem);
			CloseHandle(hMap);
		}
	}

	HWND hWnd = nullptr;
	HANDLE hMap = nullptr;
	HANDLE hMapRemote = nullptr;
	LPVOID pMem = nullptr;
	LPVOID pMemRemote = nullptr;
};

export bool DownloadHostMain()
{
	MSG msg;
	HWND hWnd = GetParent(g_pDownloadBackend->hListView);
	HANDLE hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	FileUrls urls;

	auto WaitForDoneMessage = [&](UINT doneMsg, MSG& outMsg) -> bool
		{
			while (GetMessageW(&outMsg, nullptr, 0, 0))
			{
				if (outMsg.message == doneMsg)
					return true;
				if (outMsg.message == kMsgExitProcess)
					ExitProcess(g_pDownloadBackend->nExitCode);
			}
			outMsg = {};
			return false;
		};

	auto WaitForDoneMessages = [&](UINT doneMsg, int countNeeded) -> bool
		{
			int count = 0;
			MSG m;
			while (GetMessageW(&m, nullptr, 0, 0))
			{
				if (m.message == doneMsg)
				{
					if (++count == countNeeded)
						return true;
					continue;
				}
				if (m.message == kMsgExitProcess)
					ExitProcess(g_pDownloadBackend->nExitCode);
			}
			return false;
		};

	// Lambda to fetch file URLs in a separate thread
	auto GetUrls = [&urls, &WaitForDoneMessage, hWnd](PCWSTR pLangAndEdition) -> bool
		{
			urls.clear();
			auto GetUrlsThread = [&urls](PCWSTR pLangAndEdition, std::atomic<DWORD>& dwErrCode, DWORD dwThreadId)
				{
					dwErrCode = GetFileUrls(pLangAndEdition == L"&lang=neutral&edition=app" ? g_pDownloadBackend->pAppId : g_pDownloadBackend->pId, pLangAndEdition, urls)
						? ERROR_SUCCESS : GetLastError();
					PostThreadMessageW(dwThreadId, kMsgGetUrlsDone, 0, 0);
				};

			std::atomic<DWORD> dwErrCode;
			std::thread(GetUrlsThread, pLangAndEdition, std::ref(dwErrCode), GetCurrentThreadId()).detach();
			SendMessageW(g_pDownloadBackend->hStatic, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(GetString(String_GetFileUrl).GetPointer()));
			MSG m;
			if (!WaitForDoneMessage(kMsgGetUrlsDone, m))
				return false;
			SetLastError(dwErrCode);
			return dwErrCode == ERROR_SUCCESS;
		};

	// Lambda to check whether files are all finished download
	auto CheckHasDone = [&WaitForDoneMessage](int startIndex, int itemCount) -> bool
		{
			std::atomic_bool allDone = true;
			std::thread([=, &allDone](DWORD dwThreadId)
				{
					WCHAR szBuffer[MAX_PATH];
					PWSTR pRemoteBuffer = reinterpret_cast<PWSTR>(VirtualAllocEx(g_pDownloadBackend->hProcess, nullptr, sizeof(szBuffer), MEM_COMMIT, PAGE_READWRITE));
					LVITEMW* pRemoteLVItem = reinterpret_cast<LVITEMW*>(VirtualAllocEx(g_pDownloadBackend->hProcess, nullptr, sizeof(LVITEMW), MEM_COMMIT, PAGE_READWRITE));
					auto finishedStr = GetString(String_Finished);

					auto CleanupAndSignal = [&](bool done) -> void
						{
							if (pRemoteBuffer)
								VirtualFreeEx(g_pDownloadBackend->hProcess, pRemoteBuffer, 0, MEM_RELEASE);
							if (pRemoteLVItem)
								VirtualFreeEx(g_pDownloadBackend->hProcess, pRemoteLVItem, 0, MEM_RELEASE);
							allDone.store(done, std::memory_order_relaxed);
							PostThreadMessageW(dwThreadId, kMsgCheckHasDone, 0, 0);
						};

					if (!pRemoteBuffer || !pRemoteLVItem)
						return CleanupAndSignal(false);

					for (int i = startIndex; i != startIndex + itemCount; ++i)
					{
						LVITEMW lvi = {
							.iSubItem = 1,
							.pszText = pRemoteBuffer,
							.cchTextMax = MAX_PATH
						};
						if (!WriteProcessMemory(g_pDownloadBackend->hProcess, pRemoteLVItem, &lvi, sizeof(LVITEMW), nullptr))
							return CleanupAndSignal(false);
						SendMessageW(g_pDownloadBackend->hListView, LVM_GETITEMTEXT, i, reinterpret_cast<LPARAM>(pRemoteLVItem));
						if (!ReadProcessMemory(g_pDownloadBackend->hProcess, pRemoteBuffer, szBuffer, sizeof(szBuffer), nullptr))
							return CleanupAndSignal(false);

						if (finishedStr != szBuffer)
							return CleanupAndSignal(false);
					}
					CleanupAndSignal(true);
				}, GetCurrentThreadId()).detach();

			MSG m;
			if (!WaitForDoneMessage(kMsgCheckHasDone, m))
				return false;
			return allDone.load(std::memory_order_relaxed);
		};

	std::wstring ErrorString;
	DownloadState states[4];

	auto FailPhase = [&](PCWSTR pErrText) -> bool
		{
			ErrorBox(hWnd, pErrText);
			decltype(FetcherMain::State) state = FetcherMain::Failed;
			WriteProcessMemory(g_pDownloadBackend->hProcess, g_pDownloadBackend->State, &state, sizeof(decltype(FetcherMain::State)), nullptr);
			CloseHandle(hEvent);
			return false;
		};

	auto RunDownloadPhase = [&](int startIndex, int itemCount, PCWSTR pLangAndEdition, int nHosts, PBYTE pBuffer, PBYTE pRemoteBuffer) -> bool
		{
			if (itemCount <= 0)
				return true;

			for (int ntried = 1; ntried <= kMaxPhaseRetries; ++ntried)
			{
				if (CheckHasDone(startIndex, itemCount))
					break;
				if (!GetUrls(pLangAndEdition))
				{
					auto errMsg = LRDLdlGetErrorMessage();
					bool ok = FailPhase(errMsg);
					LRDLdlDelete(errMsg);
					return ok;
				}
				ResetEvent(hEvent);

				std::atomic<int> currentIndex = startIndex;
				const int nEnd = startIndex + itemCount;
				const int stateCount = nHosts;
				long long offset = 0;

				for (int i = 0; i < stateCount; ++i)
				{
					std::thread(DownloadHost, nHosts, states + i, GetCurrentThreadId(), hEvent, std::ref(urls),
						ntried == 3 ? &states[i].ErrorString : nullptr, std::ref(currentIndex), nEnd,
						reinterpret_cast<PWSTR>(pBuffer + offset), reinterpret_cast<LPLVITEMW>(pBuffer + offset + sizeof(WCHAR) * MAX_PATH),
						reinterpret_cast<PPOINT>(pBuffer + offset + sizeof(WCHAR) * MAX_PATH + sizeof(LVITEMW)),
						reinterpret_cast<PWSTR>(pRemoteBuffer + offset), reinterpret_cast<LPLVITEMW>(pRemoteBuffer + offset + sizeof(WCHAR) * MAX_PATH),
						reinterpret_cast<PPOINT>(pRemoteBuffer + offset + sizeof(WCHAR) * MAX_PATH + sizeof(LVITEMW))).detach();
					offset += sizeof(WCHAR) * MAX_PATH + sizeof(LVITEMW) + sizeof(POINT);
				}

				volatile bool stopStatusThread = false;
				std::thread statusThread(StatusThread, hEvent, &stopStatusThread, g_pDownloadBackend->hStatic, states, stateCount);
				WaitForDoneMessages(kMsgDownloadWorkersDone, stateCount);
				stopStatusThread = true;
				SetEvent(hEvent);
				statusThread.join();
			}

			for (auto& state : states)
			{
				ErrorString += state.ErrorString;
				state.ErrorString.clear();
			}
			return true;
		};

	SharedMemory mem(g_pDownloadBackend->hProcess,
		GetParent(g_pDownloadBackend->hListView),
		static_cast<DWORD>(max(kSystemHosts, kAppxParallelHosts) * (sizeof(LVITEMW) + sizeof(WCHAR) * MAX_PATH + sizeof(POINT))));
	if (!mem.hWnd)
		return false;

	// Appx files: parallel.
	if (!RunDownloadPhase(0, g_pDownloadBackend->nAppxFiles, L"&lang=neutral&edition=app", kAppxParallelHosts, reinterpret_cast<PBYTE>(mem.pMem), reinterpret_cast<PBYTE>(mem.pMemRemote)))
		return false;

	// System files: sequential.
	if (!RunDownloadPhase(g_pDownloadBackend->nAppxFiles, g_pDownloadBackend->nSysFiles, g_pDownloadBackend->pLangAndEdition, kSystemHosts, reinterpret_cast<PBYTE>(mem.pMem), reinterpret_cast<PBYTE>(mem.pMemRemote)))
		return false;
	if (!ErrorString.empty())
	{
		decltype(FetcherMain::State) state = FetcherMain::Failed;
		WriteProcessMemory(g_pDownloadBackend->hProcess, g_pDownloadBackend->State, &state, sizeof(decltype(FetcherMain::State)), nullptr);
		ErrorBox(hWnd, ErrorString.c_str());
		InvalidateRect(hWnd, nullptr, FALSE);
		PostMessageW(hWnd, WM_COMMAND, MAKEWPARAM(kCmdNextId, kCmdNextCode), 0);
		CloseHandle(hEvent);
		return false;
	}

	// Start file verification process in separate thread
	std::atomic_int verifiedCount = 0;
	std::thread(Verify, GetCurrentThreadId(), std::ref(verifiedCount), hEvent).detach();
	volatile bool stopStatusThread = false;
	std::thread statusThread([&verifiedCount](HANDLE hEvent, volatile bool* pStop, HWND hStatic)
		{
			auto verifyingStr = GetString(String_Verifying);
			auto len = verifyingStr.GetLength() + kVerifyStatusExtraChars;
			auto buffer = std::make_unique<WCHAR[]>(len);
			while (!*pStop)
			{
				int verified = verifiedCount.load();
				swprintf_s(buffer.get(), len, verifyingStr.GetPointer(), verified, g_pDownloadBackend->nAppxFiles + g_pDownloadBackend->nSysFiles);
				SendMessageW(hStatic, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer.get()));
				WaitForSingleObject(hEvent, INFINITE);
			}
		}, hEvent, &stopStatusThread, g_pDownloadBackend->hStatic);
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
	WaitForDoneMessage(kMsgVerifyDone, msg);
	stopStatusThread = true;
	SetEvent(hEvent);
	statusThread.join();
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
	if (!msg.wParam)
	{
		decltype(FetcherMain::State) state = FetcherMain::Done;
		WriteProcessMemory(g_pDownloadBackend->hProcess, g_pDownloadBackend->State, &state, sizeof(decltype(FetcherMain::State)), nullptr);
	}
	InvalidateRect(hWnd, nullptr, FALSE);
	CloseHandle(hEvent);
	return msg.wParam;
}
