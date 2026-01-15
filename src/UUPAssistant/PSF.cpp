#include "pch.h"
#include "global_features.h"
#include "Xml.h"

#include <msdelta.h>

#include <string>
#include <thread>
#include <atomic>

using namespace std;
using namespace rapidxml;

struct FileInfo
{
	std::wstring name;
	FILETIME time;
	struct Source
	{
		DELTA_FLAG_TYPE type;
		ULONG offset;
		ULONG length;
	}deltaSource;
};

inline
static ULONG GetChildNodeCount(xml_node<>* node)
{
	auto child = node->first_node();
	if (!child)
		return 0;

	ULONG n = 1;
	while (child = child->next_sibling())
		++n;
	return n;
}

template<typename T>
static
bool CheckString(PCSTR pString1, PCSTR pString2, T& Target, T Value)
{
	if (!_strcmpi(pString1, pString2))
		Target = Value;
	else return false;

	return true;
}

inline
static bool ReadXml(DWORD& FileCount, std::unique_ptr<FileInfo[]>& pFileInfo, PSTR pXml)
{
	try
	{
		xml_document<CHAR> doc;
		doc.parse<0>(pXml);
		auto root = doc.first_node();
		if (!root) throw ERROR_BAD_FORMAT;
		auto files = root->first_node("Files");
		if (!files) throw ERROR_NOT_FOUND;

		FileCount = GetChildNodeCount(files);
		auto pFileLists = new FileInfo[FileCount];
		pFileInfo.reset(pFileLists);
		DWORD n = 0;
		for (auto i = files->first_node(); i; i = i->next_sibling())
		{
			FileInfo& fi = pFileLists[n++];
			auto attr = i->first_attribute("name");
			if (!attr) throw ERROR_NOT_FOUND;
			fi.name.resize(attr->value_size());
			for (LONG i = 0; i != static_cast<LONG>(attr->value_size()); ++i)
				fi.name[i] = attr->value()[i];

			attr = i->first_attribute("time");
			*reinterpret_cast<ULONGLONG*>(&fi.time) = strtoull(attr->value(), nullptr, 10);

			auto Source = i->first_node("Delta");
			if (!Source) throw ERROR_NOT_FOUND;
			Source = Source->first_node("Source");
			if (!Source) throw ERROR_NOT_FOUND;

			attr = Source->first_attribute("offset");
			if (!attr) throw ERROR_NOT_FOUND;
			fi.deltaSource.offset = atol(attr->value());

			attr = Source->first_attribute("length");
			if (!attr) throw ERROR_NOT_FOUND;
			fi.deltaSource.length = atol(attr->value());

			attr = Source->first_attribute("type");
			if (!attr) throw ERROR_NOT_FOUND;

			if (!CheckString(attr->value(), "RAW", fi.deltaSource.type, DELTA_FLAG_TYPE(-1))
				&& !CheckString(attr->value(), "PA30", fi.deltaSource.type, DELTA_FLAG_NONE)
				&& !CheckString(attr->value(), "PA19", fi.deltaSource.type, DELTA_APPLY_FLAG_ALLOW_PA19))
				throw ERROR_BAD_FORMAT;
		}
	}
	catch (long ErrCode)
	{
		SetLastError(ErrCode);
		return false;
	}

	return true;
}

bool ExpandPSF(PCWSTR psf, PCWSTR xml, PCWSTR outdir)
{
	const BYTE Head[] =
	{ 'P', 'S', 'T', 'R', 'E', 'A', 'M', 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 };

	HANDLE hFile = CreateFileW(xml, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	string Xml;
	bool ret = ReadText(hFile, Xml, GetFileSize(hFile, nullptr));
	CloseHandle(hFile);
	if (!ret)
		return false;

	hFile = CreateFileW(psf, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	HANDLE hFileMapping = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
	if (!hFileMapping)
	{
		CloseHandle(hFile);
		return false;
	}

	auto pvData = MapViewOfFile(hFileMapping, FILE_MAP_READ, 0, 0, 0);
	if (!pvData
		|| memcmp(pvData, Head, 16))
	{
		if (pvData)
			UnmapViewOfFile(pvData);
		else
			SetLastError(ERROR_BAD_FORMAT);
		CloseHandle(hFileMapping);
		CloseHandle(hFile);
		return false;
	}

	DWORD dwFileCount;
	std::unique_ptr<FileInfo[]> pFiles;
	ret = ReadXml(dwFileCount, pFiles, const_cast<PSTR>(Xml.c_str()));
	if (!ret)
	{
		UnmapViewOfFile(pvData);
		CloseHandle(hFileMapping);
		CloseHandle(hFile);
		return false;
	}

	atomic<bool> cancel = false;
	atomic<DWORD> dwErrCode = ERROR_SUCCESS;

#ifndef _USE_OMP
	atomic<ULONG> ItemToProcess = 0;
	auto Thread =
#endif
	[&](FileInfo* Files, DWORD FileCount, FileCreator fc)
		{
#ifdef _USE_OMP
#pragma omp parallel for schedule(dynamic)
			for (LONGLONG i = 0; i < FileCount; ++i)
#else
			for (DWORD i = ItemToProcess++; i < FileCount && !cancel; i = ItemToProcess++)
#endif
			{
				HANDLE hFile = fc.Create(Files[i].name.c_str());
				if (hFile == INVALID_HANDLE_VALUE)
				{
					dwErrCode = GetLastError();
					cancel = true;
					break;
				}

				if (Files[i].deltaSource.type == -1)
				{
					DWORD dwWritten;
					if (!WriteFile(hFile, reinterpret_cast<PBYTE>(pvData) + Files[i].deltaSource.offset, Files[i].deltaSource.length, &dwWritten, nullptr)
						|| dwWritten != Files[i].deltaSource.length)
					{
						dwErrCode = GetLastError();
						CloseHandle(hFile);
						cancel = true;
						break;
					}
				}
				else
				{
					DELTA_INPUT di = {
						.lpcStart = reinterpret_cast<PBYTE>(pvData) + Files[i].deltaSource.offset,
						.uSize = Files[i].deltaSource.length,
						.Editable = FALSE
					};

					DELTA_OUTPUT DO;
					if (!ApplyDeltaB(Files[i].deltaSource.type, {}, di, &DO))
					{
						dwErrCode = GetLastError();
						CloseHandle(hFile);
						cancel = true;
						break;
					}

					DWORD dwWritten;
					if (!WriteFile(hFile, DO.lpStart, static_cast<DWORD>(DO.uSize), &dwWritten, nullptr)
						|| dwWritten != static_cast<DWORD>(DO.uSize))
					{
						dwErrCode = GetLastError();
						CloseHandle(hFile);
						cancel = true;
						DeltaFree(DO.lpStart);
						break;
					}
					DeltaFree(DO.lpStart);
				}

				SetFileTime(hFile, nullptr, nullptr, &Files[i].time);
				CloseHandle(hFile);
			}
		}
#ifdef _USE_OMP
	(pFiles.get(), dwFileCount, outdir);
#else
	;

	SYSTEM_INFO si;
	GetSystemInfo(&si);
	DWORD n = si.dwNumberOfProcessors;

	thread* threads = new thread[n];
	FileCreator fc(outdir);
	for (DWORD i = 0; i != n; ++i)
	{
		threads[i] = thread(Thread, pFiles.get(), dwFileCount, fc);
		SetThreadIdealProcessor(threads[i].native_handle(), i);
	}
	for (DWORD i = 0; i != n; ++i)
		threads[i].join();
	delete[] threads;
#endif

	UnmapViewOfFile(pvData);
	CloseHandle(hFileMapping);
	CloseHandle(hFile);

	if (dwErrCode == ERROR_SUCCESS)
		return true;
	SetLastError(dwErrCode);
	return false;
}
