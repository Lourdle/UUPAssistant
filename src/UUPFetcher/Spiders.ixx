module;
#include "pch.h"
#include "framework.h"
#include "FetcherMain.h"
#include "resource.h"
#include "zip.h"
#include "../common/common.h"

#include <winternl.h>
#include <Shlwapi.h>

#include <thread>
#include <sstream>
#include <filesystem>
#include <vector>
#include <string>
#include <string_view>
#include <memory>

import Constants;
import Misc;
import UUPdump;
import Http;

using namespace std;
using namespace Lourdle::UIFramework;
namespace fs = std::filesystem;

export module Spiders;


struct HandleCollection {
	WinHandle download;
	WinHandle get;
	WinHandle get_app;
};

class PathManager {
private:
	fs::path m_workDir;
	fs::path m_cacheDir;

public:
	PathManager(const fs::path& relativePath) 
		: m_workDir(fs::current_path() / relativePath), m_cacheDir(fs::current_path() / "Cache") 
	{}

	~PathManager()
	{
		DWORD dwErrCode = GetLastError();
		MoveDirectory(m_workDir.c_str(), m_cacheDir.c_str());
		RemoveDirectoryRecursive(m_workDir.c_str());
		SetLastError(dwErrCode);
	}

	std::wstring GetFullPath(const fs::path& relativePath) const {
		return (fs::current_path() / relativePath).wstring();
	}
};


// Helper to parse language from cookies
static std::wstring GetLanguageFromCookies(const std::wstring& cookies) {
	auto pos = cookies.find(L"Page-Language=");
	if (pos != std::wstring::npos) {
		auto begin = pos + 14;
		auto end = cookies.find(L';', begin);
		if (end == std::wstring::npos)
			end = cookies.length();
		return cookies.substr(begin, end - begin);
	}
	return {};
}

// Helper to build URLs
struct UrlBuilder {
	static std::wstring BuildDownloadUrl(const std::wstring& id, const std::wstring& lang, const std::wstring& edition) {
		return std::wstring(UUPDUMP_WEBSITE_BASE_URL) + L"/download.php?id=" + id + L"&pack=" + lang + L"&edition=" + edition;
	}
	static std::wstring BuildGetDownloadUrl(const std::wstring& id, const std::wstring& lang, const std::wstring& edition) {
		return std::wstring(UUPDUMP_WEBSITE_BASE_URL) + L"/get.php?id=" + id + L"&pack=" + lang + L"&edition=" + edition;
	}
	static std::wstring BuildGetUrl(const std::wstring& id, const std::wstring& lang, const std::wstring& edition) {
		return std::wstring(UUPDUMP_API_BASE_URL) + L"/get.php?id=" + id + L"&lang=" + lang + L"&edition=" + edition;
	}
	static std::wstring BuildGetAppUrl(const std::wstring& id) {
		return std::wstring(UUPDUMP_API_BASE_URL) + L"/get.php?id=" + id + L"&lang=neutral&edition=app&noLinks=1";
	}
};

// Helper to download or get from cache
static bool DownloadOrCache(
	const PathManager& pm, 
	const std::wstring& url, 
	const fs::path& relPath, 
	const fs::path& cacheRelPath, 
	WinHandle& outHandle, 
	const std::wstring& cookies,
	const std::wstring& acceptLang,
	bool& outIsCached)
{
	outIsCached = false;
	
	// Check cache
	std::wstring fullCachePath = pm.GetFullPath(cacheRelPath);
	HANDLE hCache = CreateFileW(fullCachePath.c_str(), GENERIC_READ | DELETE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hCache != INVALID_HANDLE_VALUE) {
		if (GetFileSize(hCache, nullptr) > 0) {
			outHandle = hCache;
			outIsCached = true;
			return true;
		}
		CloseHandle(hCache);
	}

	if (url.empty()) return false;

	// Prepare download path
	std::wstring fullPath = pm.GetFullPath(relPath);
	
	// Ensure directory exists
	fs::create_directories(fs::path(fullPath).parent_path());

	HANDLE hFile = CreateFileW(fullPath.c_str(), GENERIC_WRITE | GENERIC_READ | DELETE,
		FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	
	if (hFile == INVALID_HANDLE_VALUE) return false;

	LRDLDLCREATETASK_HTTP_HEADERS headers = {
		cookies.empty() ? 2UL : 3UL,
		{
			{ L"Accept", L"text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7" },
			{ L"Accept-Language", acceptLang.c_str() },
			{ L"Cookie", cookies.c_str() }
		}
	};

	DWORD dwErrCode = ERROR_SUCCESS;
	HttpGetData(hFile, &dwErrCode, url.c_str(), true, false, &headers);
	
	if (dwErrCode != ERROR_SUCCESS) {
		if (dwErrCode == ERROR_BAD_NET_RESP) {
			Sleep(3000);
			HttpGetData(hFile, &dwErrCode, url.c_str(), true, false, &headers);
		}
	}

	if (dwErrCode != ERROR_SUCCESS) {
		DeleteFileOnClose(hFile);
		CloseHandle(hFile);
		SetLastError(dwErrCode);
		return false;
	}

	outHandle = hFile;
	return true;
}

inline
static bool FetchData(wstring& PathStr, PCWSTR UUPid, PCWSTR lang, PCWSTR edition, const wstring& cookies, FetcherMain& main, ULONGLONG id, HandleCollection& handles, String& AppId)
{
	// Create temporary directory
	fs::path currentDir = fs::current_path();
	fs::path tempDir = currentDir / "Temp";
	std::error_code ec;
	fs::create_directory(tempDir, ec);

	fs::path workPath = currentDir / PathStr;
	fs::create_directory(workPath, ec);

	wstring szLang = GetLanguageFromCookies(cookies);
	if (szLang.empty()) {
		WCHAR buf[LOCALE_NAME_MAX_LENGTH];
		GetUserDefaultLocaleName(buf, LOCALE_NAME_MAX_LENGTH);
		szLang = buf;
	}

	PathManager CurDir(PathStr);

	// 1. Download main page (download.php)
	wstring url = UrlBuilder::BuildDownloadUrl(UUPid, lang, edition);
	wstring queryPart = url.substr(url.find(L'?') + 1); // Safer than hardcoded offset

	fs::path downloadCachePath = fs::path("Cache") / szLang / "download.php" / queryPart;
	fs::path downloadWorkPath = fs::relative(workPath / fs::path(szLang) / "download.php" / queryPart, currentDir);

	wstring acceptlang = szLang + L"," + szLang.substr(0, 2) + L";q=0.9";
	if (szLang.substr(0, 2) != L"en")
		acceptlang += L",en;q=0.8,en-GB;q=0.7,en-US;q=0.6";

	if (main.State == FetcherMain::GettingInfo && main.uup.id == id) {
		main.EditBox.SetWindowText(url.c_str());
		main.Progress.StepIt();
	}
	else return true;

	bool isCached = false;
	if (!DownloadOrCache(CurDir, url, downloadWorkPath, downloadCachePath, handles.download, cookies, acceptlang, isCached)) {
		return false;
	}

	if (main.State == FetcherMain::GettingInfo && id == main.uup.id)
		main.Progress.StepIt();
	else return true;

	// 2. Process system files (get.php)
	// This part handles multiple editions separated by ';' or "%3B"
	wstring editionStr = edition;
	for (size_t pos = 0; (pos = editionStr.find(L"%3B", pos)) != wstring::npos; ) {
		editionStr.replace(pos, 3, L";");
	}
	vector<string> jsons;

	// Base URL for get.php
	wstring getUrlBase = format(L"{}/get.php?id={}&lang={}&edition=", UUPDUMP_API_BASE_URL, UUPid, lang);

	// We need to construct a combined path for the cache/file
	// The original code used the first edition for the path, but appended all editions to the URL?
	// Actually, the original code looped and downloaded multiple times into the SAME file handle?
	// No, it downloaded to the same file handle `hFile` sequentially?
	// "HttpGetData(hFile, ...)" appends? No, usually it writes.
	// Ah, `HttpGetData` likely writes to the file.
	// The original code:
	// 1. Create file.
	// 2. Loop editions.
	// 3. HttpGetData(hFile, ...) -> This would overwrite if position is 0, or append if position is current?
	//    If HttpGetData uses WriteFile without setting pointer, it appends.
	//    But then it does: SetFilePointer(hFile, 0...); ReadFile(...); SetFilePointer(0); SetEndOfFile();
	//    It reads the content, adds to `jsons`, then clears the file?
	//    Then finally `CombineFileLists(jsons)` and writes back.

	// Let's replicate the logic but cleaner.

	fs::path getPhpRelPath = fs::path(szLang) / "get.php" / (wstring(UUPid) + L"_" + wstring(lang)); // Simplified path
	// Original path was complex: Cache/Lang/get.php/query_part/edition
	// I'll stick to a unique path based on query.

	wstring fullQuery = L"id=" + wstring(UUPid) + L"&pack=" + wstring(lang) + L"&edition=" + editionStr;
	fs::path getCachePath = fs::path("Cache") / szLang / "get.php" / fullQuery;
	fs::path getWorkPath = fs::path(downloadWorkPath).parent_path().parent_path() / "get.php" / fullQuery;

	// Check if combined result is cached
	if (!DownloadOrCache(CurDir, L"", getWorkPath, getCachePath, handles.get, cookies, acceptlang, isCached)) {
		// Not cached, we need to fetch and combine

		// Create the target file
		fs::create_directories(fs::path(CurDir.GetFullPath(getWorkPath)).parent_path());

		HANDLE hFile = CreateFileW(CurDir.GetFullPath(getWorkPath).c_str(), GENERIC_WRITE | GENERIC_READ | DELETE,
			FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

		if (hFile == INVALID_HANDLE_VALUE) return false;

		// Loop through editions
		wstringstream ss(editionStr);
		wstring segment;
		while (std::getline(ss, segment, L';')) {
			if (segment.empty()) continue;

			wstring currentUrl = getUrlBase + segment + L"&noLinks=1";

			if (main.State == FetcherMain::GettingInfo && id == main.uup.id)
				main.EditBox.SetWindowText(currentUrl.c_str());
			else {
				DeleteFileOnClose(hFile);
				CloseHandle(hFile);
				return true;
			}

			// We need a temp file for each request to avoid messing up the main handle
			// Or we can just read into memory if HttpGetData supports it?
			// Assuming HttpGetData needs a handle.
			// We can reuse hFile, but we need to truncate it each time?
			// The original code reused hFile, read it, then truncated it.

			SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);
			SetEndOfFile(hFile); // Truncate

			DWORD dwErr = ERROR_SUCCESS;
			HttpGetData(hFile, &dwErr, currentUrl.c_str(), true, false);
			if (dwErr != ERROR_SUCCESS) {
				DeleteFileOnClose(hFile);
				CloseHandle(hFile);
				SetLastError(dwErr);
				return false;
			}

			// Read content
			DWORD size = GetFileSize(hFile, nullptr);
			string content;
			content.resize(size);
			SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);
			DWORD read;
			ReadFile(hFile, content.data(), size, &read, nullptr);
			jsons.push_back(move(content));
		}

		// Combine
		string combinedJson = CombineFileLists(jsons);

		// Write back to hFile
		SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);
		SetEndOfFile(hFile);
		DWORD written;
		WriteFile(hFile, combinedJson.data(), static_cast<DWORD>(combinedJson.size()), &written, nullptr);

		handles.get = hFile;
	}

	// 3. Process package data (autodl)
	wstring packUrl = UrlBuilder::BuildGetDownloadUrl(UUPid, lang, edition) + L"&autodl=1";
	wstring packQuery = packUrl.substr(packUrl.find(L'?') + 1);
	fs::path packCachePath = fs::path("Cache") / szLang / "get.php" / packQuery;
	fs::path packWorkPath = getWorkPath.parent_path() / packQuery;

	if (main.State == FetcherMain::GettingInfo && id == main.uup.id) {
		main.EditBox.SetWindowText(packUrl.c_str());
		main.Progress.StepIt();
	}
	else return true;

	WinHandle hPack;
	if (!DownloadOrCache(CurDir, packUrl, packWorkPath, packCachePath, hPack, cookies, acceptlang, isCached)) {
		return false;
	}

	// Read zip
	DWORD dwSize = GetFileSize(hPack, nullptr);
	auto zipData = std::make_unique<char[]>(dwSize);
	SetFilePointer(hPack, 0, nullptr, FILE_BEGIN);
	DWORD dwRead;
	ReadFile(hPack, zipData.get(), dwSize, &dwRead, nullptr);
	hPack.Close(); // Close handle as we have data in memory

	zip_t* zip = zip_stream_open(zipData.get(), dwSize, 0, 'r');
	if (!zip) {
		SetLastError(ERROR_INVALID_DATA);
		return false;
	}

	if (zip_entry_open(zip, "uup_download_windows.cmd") < 0) {
		zip_stream_close(zip);
		SetLastError(ERROR_FILE_NOT_FOUND);
		return false;
	}

	auto len = zip_entry_size(zip);
	string cmd;
	cmd.resize(len);
	zip_entry_noallocread(zip, cmd.data(), len);
	zip_entry_close(zip);
	zip_stream_close(zip);

	// Parse AppId from cmd
	auto pos = cmd.find("edition=app");
	if (pos != string::npos) {
		pos = cmd.rfind("id=", pos);
		if (pos != string::npos) {
			pos += 3;
			auto end = cmd.find('&', pos);
			wstring appIdStr(cmd.c_str() + pos, cmd.c_str() + end);
			AppId = String(appIdStr.c_str()); // Convert to Lourdle String
		}
	}

	if (main.State == FetcherMain::GettingInfo && id == main.uup.id)
		main.Progress.StepIt();
	else return true;

	// 4. Process App ID if found
	if (!AppId.Empty()) {
		wstring appIdW = AppId.GetPointer();
		wstring appUrl = UrlBuilder::BuildGetAppUrl(appIdW);
		fs::path appCachePath = fs::path("Cache") / appIdW;
		fs::path appWorkPath = workPath / appIdW;

		main.EditBox.SetWindowText(appUrl.c_str());

		if (!DownloadOrCache(CurDir, appUrl, appWorkPath, appCachePath, handles.get_app, cookies, acceptlang, isCached)) {
			return false;
		}
	}

	return true;
}

static String FindArg(const std::wstring& args, const std::wstring& arg)
{
	std::wstring_view query(args);
	while (!query.empty() && (query.front() == L'?' || query.front() == L'&'))
		query.remove_prefix(1);

	std::wstring_view value;
	size_t pos = 0;
	while (pos <= query.size())
	{
		size_t amp = query.find(L'&', pos);
		std::wstring_view part = (amp == std::wstring_view::npos)
			? query.substr(pos)
			: query.substr(pos, amp - pos);

		if (!part.empty())
		{
			size_t eq = part.find(L'=');
			if (eq != std::wstring_view::npos)
			{
				std::wstring_view key = part.substr(0, eq);
				if (key == std::wstring_view(arg))
				{
					value = part.substr(eq + 1);
					break;
				}
			}
		}

		if (amp == std::wstring_view::npos)
			break;
		pos = amp + 1;
	}

	if (value.empty())
		throw Exception(ERROR_BAD_FORMAT);

	return String(std::wstring(value).c_str());
}

static void ErrorMessageBox(FetcherMain& main)
{
	LPWSTR pErrMsg = LRDLdlGetErrorMessage();
	main.MessageBox(pErrMsg, GetString(String_Failed), MB_ICONERROR);
	LRDLdlDelete(pErrMsg);
}

static void HandleFailure(FetcherMain& main, ULONGLONG id, HANDLE hFileToDelete)
{
	if (main.State == FetcherMain::GettingInfo && main.uup.id == id)
	{
		main.ErrorMessageBox();
		main.BackButton.PostCommand();
		if (hFileToDelete != nullptr && hFileToDelete != INVALID_HANDLE_VALUE)
			DeleteFileOnClose(hFileToDelete);
	}
}

static wstring ReadTextFile(HANDLE hFile)
{
	LARGE_INTEGER size;
	if (!GetFileSizeEx(hFile, &size) || size.QuadPart <= 0)
		return {};

	std::string buffer;
	buffer.resize(static_cast<size_t>(size.QuadPart));

	LARGE_INTEGER zero = {
		.QuadPart = 0
	};
	SetFilePointerEx(hFile, zero, nullptr, FILE_BEGIN);

	size_t totalRead = 0;
	while (totalRead < buffer.size())
	{
		DWORD chunk = static_cast<DWORD>(min<size_t>(buffer.size() - totalRead, 1u << 20));
		DWORD read = 0;
		if (!ReadFile(hFile, buffer.data() + totalRead, chunk, &read, nullptr))
			return {};
		if (read == 0)
			break;
		totalRead += read;
	}
	buffer.resize(totalRead);
	if (buffer.empty())
		return {};

	int len = MultiByteToWideChar(CP_UTF8, 0, buffer.data(), static_cast<int>(buffer.size()), nullptr, 0);
	if (len <= 0)
		return {};
	wstring result;
	result.resize(len);
	MultiByteToWideChar(CP_UTF8, 0, buffer.data(), static_cast<int>(buffer.size()), result.data(), len);
	return result;
}

static std::wstring ExtractArgsFromUrlOrQuery(const std::wstring& text)
{
	std::wstring_view s(text);
	if (auto q = s.find(L'?'); q != std::wstring_view::npos)
		s.remove_prefix(q + 1);

	while (!s.empty() && (s.front() == L'?' || s.front() == L'&'))
		s.remove_prefix(1);

	// If the user pasted a URL without '?', fall back to the first "id=".
	if (auto idPos = s.find(L"id="); idPos != std::wstring_view::npos)
		s.remove_prefix(idPos);

	return std::wstring(s);
}

static bool GetNextLine(std::wstringstream& ss, std::wstring& line) {
	while (std::getline(ss, line)) {
		if (!line.empty()) {
			if (line.back() == L'\r') line.pop_back();
			if (!line.empty()) return true;
		}
	}
	return false;
}

static bool ParseHtmlResponse(std::wstringstream& ifs, uup_struct& uup, FetcherMain& main, ULONGLONG id)
{
	std::wstring str;
	SetLastError(ERROR_SUCCESS);
	bool bHeadersDone = false;
	bool bVirtualEditionsFound = false;

	// Reset stream to beginning
	ifs.clear();
	ifs.seekg(0);

	while (GetNextLine(ifs, str))
	{
		// Process additional editions list
		if (str.find(L"additional-editions-list") != std::wstring::npos)
		{
			int n = 1;
			String Edition, EditionFriendlyName;
			while (n > 0 && GetNextLine(ifs, str))
			{
				if (str.find(L"</div>") != std::wstring::npos)
					--n;
				else if (str.find(L"<div") != std::wstring::npos)
					++n;
				
				if (n == 0) break;

				// Parse value=
				auto pos = str.find(L"value=");
				if (pos != std::wstring::npos)
				{
					pos += 7;
					auto end = str.find(L'"', pos);
					if (end != std::wstring::npos)
						Edition = String(str.substr(pos, end - pos).c_str());
				}
				// Parse <label>
				if (!Edition.Empty() && (pos = str.find(L"<label>")) != std::wstring::npos)
				{
					pos += 7;
					auto end = str.find(L"</label>", pos);
					if (end != std::wstring::npos)
						EditionFriendlyName = String(str.substr(pos, end - pos).c_str());
				}
				if (!Edition.Empty() && !EditionFriendlyName.Empty())
				{
					uup.AdditionalEditions.insert({ move(Edition), move(EditionFriendlyName) });
					Edition = String();
					EditionFriendlyName = String();
				}
			}
		}
		// Process sub-headers (e.g., Name, Friendly Name, LocaleName)
		else if (!bHeadersDone && str.find(L"<div class=\"sub header\">") != std::wstring::npos)
		{
			auto pos = str.find(L"Windows");
			if (pos != std::wstring::npos)
			{
				auto end = str.find(L"</div>", pos);
				if (end != std::wstring::npos)
					uup.Name = String(str.substr(pos, end - pos).c_str());
				
				while (GetNextLine(ifs, str))
				{
					if ((pos = str.find(L"<div class=\"sub header\">")) != std::wstring::npos)
					{
						if (str.find(L"Windows", pos + 24) == pos + 24)
						{
							pos += 24;
							end = str.find(L"</div>", pos);
							if (end != std::wstring::npos)
								uup.EditionFriendlyNames = String(str.substr(pos, end - pos).c_str());
						}
						else if ((str.length() > pos + 25) && 
								((str[pos + 24] >= L'0' && str[pos + 24] <= L'9') ||
								 (str[pos + 25] >= L'0' && str[pos + 25] <= L'9')))
						{
							bHeadersDone = true;
							break;
						}
						else
						{
							end = str.find(L"</div>", pos);
							if (end != std::wstring::npos)
								uup.LocaleName = String(str.substr(pos + 24, end - (pos + 24)).c_str());
						}
					}
				}
			}
		}
		// Process virtual editions
		else if (str.find(L"virtual-editions") != std::wstring::npos)
		{
			while (GetNextLine(ifs, str))
			{
				if (str.find(L"content") != std::wstring::npos)
				{
					GetNextLine(ifs, str);
					std::wstring html = L"<!DOCTYPE html><html><head><style type=\"text/css\">\r\n"
						L"body {\r\nbackground-color: Canvas;\r\ncolor: CanvasText;\r\n"
						L"color-scheme: light dark;\r\n}\r\n</style></head><body>";
					
					int n = 1;
					while (n > 0 && GetNextLine(ifs, str))
					{
						// Handle nested divs
						size_t pos = 0;
						while ((pos = str.find(L"<div", pos)) != std::wstring::npos) {
							n++;
							pos += 4;
						}
						pos = 0;
						while ((pos = str.find(L"</div>", pos)) != std::wstring::npos) {
							n--;
							pos += 6;
						}

						if (n <= 0) // Closed the content div
						{
							html += L"</body></html>";
							uup.HtmlVritualEditions = html.c_str();
							bVirtualEditionsFound = true;
							if (main.State == FetcherMain::GettingInfo && main.uup.id == id)
								main.Progress.DeltaPos(4 * ProgressStepsPerPercent);
							break;
						}
						html += str;
					}
					break; // Found content
				}
			}
		}
	}

	if (GetLastError() == ERROR_SUCCESS && uup.Name.Empty())
		SetLastError(ERROR_INVALID_DATA);
	
	return !uup.Name.Empty();
}

inline
static HANDLE ProcessFileLists(HandleCollection& handles, uup_struct& uup, FetcherMain& main, ULONGLONG id)
{
	WCHAR szSize[10] = { 0 };

	// Process system file list
	SetFilePointer(handles.get, 0, nullptr, FILE_BEGIN);
	if (!GetFiles(handles.get, uup.System))
		return handles.get;
	LONGLONG sizeSum = 0;
	auto it = uup.System.end();
	for (auto i = uup.System.begin(); i != uup.System.end(); ++i)
		if (PathMatchSpecW(i->Name, L"*.AggregatedMetadata.cab"))
			it = i;
		else
			sizeSum += i->Size;
	if (it != uup.System.end())
		uup.System.erase(it);
	StrFormatByteSizeW(sizeSum, szSize, 10);
	uup.SystemSize = szSize;
	main.Progress.DeltaPos(8 * ProgressStepsPerPercent);

	// Process application file list
	if (!handles.get_app || GetFileSize(handles.get_app, nullptr) == 0)
		return nullptr;  // No application data, finish processing
	SetFilePointer(handles.get_app, 0, nullptr, FILE_BEGIN);
	if (GetFiles(handles.get_app, uup.Apps))
	{
		sizeSum = 0;
		for (const auto& file : uup.Apps)
			sizeSum += file.Size;
		StrFormatByteSizeW(sizeSum, szSize, 10);
		uup.AppSize = szSize;
		return nullptr;
	}
	return handles.get_app;
}

inline
static void Finalize(FetcherMain& main, uup_struct&& uup)
{
	// Aggregate total size
	LONGLONG totalSize = 0;
	for (const auto& file : uup.System)
		totalSize += file.Size;
	for (const auto& file : uup.Apps)
		totalSize += file.Size;
	WCHAR szTotal[10];
	StrFormatByteSizeW(totalSize, szTotal, 10);
	uup.UUPSize = szTotal;

	main.uup = move(uup);
	main.SendCommand(main.Progress, 0, kDisplaySummaryPageNotifyId);
	main.RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE);
	main.State = FetcherMain::SummaryPage;
	main.PostCommand(main.Progress, 0, kDisplaySummaryPageNotifyId);
}

export void StartSpiderThread(FetcherMain* main, const wstring& cookies)
{
	ULONGLONG id = Random64();
	main->uup.id = id;
	main->Progress.SetPos(0);

	// Accept either a full URL or a raw query string.
	std::wstring argsStr = ExtractArgsFromUrlOrQuery(main->EditBox.GetWindowText().GetPointer());

	std::thread([id](FetcherMain& main, std::wstring cookies, String args)
		{
			// Construct working directory path
			std::wstring Path = L"Temp\\" + std::to_wstring(id);
			HANDLE hFileToDelete = INVALID_HANDLE_VALUE;
			HandleCollection handles;

			uup_struct uup = { .id = id };
			try
			{
				uup.UUPid = FindArg(args.GetPointer(), L"id");
				uup.Language = FindArg(args.GetPointer(), L"pack");
				uup.Editions = FindArg(args.GetPointer(), L"edition");
			}
			catch (Exception& e)
			{
				SetLastError(e.dwSysErrCode);
				HandleFailure(main, id, hFileToDelete);
				return;
			}

			// Get basic data
			if (!FetchData(Path, uup.UUPid, uup.Language, uup.Editions, cookies, main, id, handles, uup.AppUUPid))
			{
				if (main.State == FetcherMain::GettingInfo && main.uup.id == id)
				{
					ErrorMessageBox(main);
					main.BackButton.PostCommand();
				}
				return;
			}

			// Ensure current state for further processing
			if (main.State == FetcherMain::GettingInfo && main.uup.id == id)
			{
				main.EditBox.MoveWindow(0, 0, 0, 0);
				
				wstring content = ReadTextFile(handles.download);
				if (content.empty())
				{
					HandleFailure(main, id, hFileToDelete);
					return;
				}
				std::wstringstream ifs(content);
				hFileToDelete = handles.download;

				// Parse additional editions, sub-headers, and virtual editions
				if (!ParseHtmlResponse(ifs, uup, main, id))
				{
					HandleFailure(main, id, hFileToDelete);
					return;
				}

				if (main.State == FetcherMain::GettingInfo && main.uup.id == id)
					main.Progress.DeltaPos(4 * ProgressStepsPerPercent);

				// Process file list data
				hFileToDelete = ProcessFileLists(handles, uup, main, id);
				if (hFileToDelete)
				{
					HandleFailure(main, id, hFileToDelete);
					return;
				}

				if (main.State == FetcherMain::GettingInfo && main.uup.id == id)
				{
					Finalize(main, move(uup));
					return;
				}
				else
					return;
			}
		}, std::ref(*main), cookies, String(argsStr.c_str())).detach();
}
