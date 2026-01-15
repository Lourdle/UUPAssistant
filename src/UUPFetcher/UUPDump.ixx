module;
#include "pch.h"
#include "framework.h"
#include "FetcherMain.h"
#include "resource.h"

#include <nlohmann/json.hpp>

#include <map>

export module UUPdump;

import Http;
import Misc;
import Constants;

using namespace Lourdle::UIFramework;
using namespace std;

inline
static String string_to_String(const string& s)
{
	String result;
	if (s.empty()) return result;
	int size_needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
	result.Resize(size_needed);
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), result.GetPointer(), size_needed);
	return result;
}

inline
static string String_to_string(const String& s)
{
	if (s.Empty()) return string();
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, s, (int)s.GetLength(), NULL, 0, NULL, NULL);
	string result(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, s, (int)s.GetLength(), &result[0], size_needed, NULL, NULL);
	return result;
}

export bool GetFiles(HANDLE hFile, uup_struct::FileList_t& vFiles)
{
	string text;
	char buf[4096];
	DWORD cbRead;
	while (ReadFile(hFile, buf, sizeof(buf), &cbRead, nullptr)
		&& cbRead != 0)
		text.append(buf, cbRead);

	try
	{
		auto j = nlohmann::json::parse(text);
		for (auto& item : j.at("response").at("files").items())
			vFiles.push_back({ string_to_String(item.key()), string_to_String(item.value()["sha1"].get<string>()), strtoll(item.value()["size"].get<string>().c_str(), nullptr, 10) });
		sort(vFiles.begin(), vFiles.end(), [](const auto& lhs, const auto& rhs) { return _wcsicmp(lhs.Name, rhs.Name) < 0; });
		return true;
	}
	catch (const std::exception&)
	{
		return false;
	}
}

export string CombineFileLists(const vector<string>& v)
{
	nlohmann::json j;
	auto& files = j["response"]["files"];
	for (const auto& i : v)
	{
		try
		{
			auto j2 = nlohmann::json::parse(i);
			for (auto& item : j2.at("response").at("files").items())
				files[item.key()] = item.value();
		}
		catch (const std::exception&)
		{
		}
	}
	return j.dump();
}


struct wstriless
{
	bool operator()(const wstring& lhs, const wstring& rhs) const
	{
		return _wcsicmp(lhs.c_str(), rhs.c_str()) < 0;
	}
};

export using FileUrls = map<wstring, wstring, wstriless>;
export bool GetFileUrls(PCWSTR id, PCWSTR pLangAndEdition, FileUrls& m)
{
	wstring url = format(L"{}/get.php?id={}", UUPDUMP_API_BASE_URL, id);

	vector<string> jsons;
	PCWSTR p;
	auto p2 = pLangAndEdition + wcslen(pLangAndEdition);
	while (p2[-1] != L'=')
		--p2;
	url.append(pLangAndEdition, p2);
	auto urlSize = url.size();

	do
	{
		p = wcschr(p2, L';');
		if (!p) p = p2 + wcslen(p2);
		url.resize(urlSize);
		url.append(p2, p - p2);

		string text;
		DWORD dwErrCode = ERROR_SUCCESS;
		for (BYTE nTry = 0; nTry != 5; ++nTry)
		{
			HANDLE hReadPipe, hWritePipe;
			CreatePipe(&hReadPipe, &hWritePipe, nullptr, 0);

			text.clear();
			HttpGetData(hWritePipe, &dwErrCode, url.c_str(), false);
			char buf[4096];
			DWORD cbRead;
			while (ReadFile(hReadPipe, buf, 4096, &cbRead, nullptr))
				text.append(buf, cbRead);
			CloseHandle(hReadPipe);

			if (dwErrCode == ERROR_SUCCESS)
				break;
			else
				Sleep(5000);
		}
		if (dwErrCode != ERROR_SUCCESS)
		{
			SetLastError(dwErrCode);
			return false;
		}
		jsons.push_back(move(text));

		p2 = p + 1;
	} while (*p);


	string text = CombineFileLists(jsons);

	try
	{
		auto j = nlohmann::json::parse(text);
		for (auto& item : j["response"]["files"].items())
		{
			string file = item.key();
			string url = item.value()["url"].get<string>();

			m[wstring(file.begin(), file.end())] = wstring(url.begin(), url.end());
		}
		return true;
	}
	catch (const std::exception&)
	{
		return false;
	}
}
