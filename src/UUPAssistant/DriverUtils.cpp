#include "pch.h"
#include "UUPAssistant.h"
#include <Windows.h>
#include <Shlwapi.h>

#include <string>
#include <functional>
#include <vector>
#include <unordered_set>

using namespace std;

class Stream
{
	HANDLE hFile;
	PSTR pBuffer;
	static constexpr DWORD dwBufferSize = 0x1000;
	DWORD dwBufferPos;
	DWORD dwBufferLen;

	enum : BYTE
	{
		Uninitialized,
		ANSI,
		UTF16LE,
		UTF16BE,
	} DocumentCodePage;

public:
	Stream(HANDLE hFile) : hFile(hFile), pBuffer(new CHAR[dwBufferSize]), dwBufferPos(0), dwBufferLen(0), DocumentCodePage(Uninitialized) {}

	~Stream()
	{
		delete[] pBuffer;
	}

	bool operator>>(string& str)
	{
		str.clear();
		PSTR pBegin = pBuffer + dwBufferPos;
		while (true)
		{
			if (DocumentCodePage == Uninitialized)
			{
				if (!ReadFile(hFile, pBuffer, dwBufferSize, &dwBufferLen, nullptr) || dwBufferLen == 0)
					return false;
				if (pBuffer[0] == char(0xFF) && pBuffer[1] == char(0xFE))
				{
					DocumentCodePage = UTF16LE;
					dwBufferPos = 2;
				}
				else if (pBuffer[0] == char(0xFE) && pBuffer[1] == char(0xFF))
				{
					DocumentCodePage = UTF16BE;
					dwBufferPos = 2;
				}
				else
					DocumentCodePage = ANSI;
				pBegin = pBuffer + dwBufferPos;
				continue;
			}
			if (dwBufferPos >= dwBufferLen)
			{
				if (DocumentCodePage == ANSI)
					str.append(pBegin, pBuffer + dwBufferLen);
				else if (DocumentCodePage == UTF16LE)
					for (auto i = pBegin; i < pBuffer + dwBufferLen; i += 2)
						str.push_back(*i);
				else
					for (auto i = pBegin + 1; i < pBuffer + dwBufferLen; i += 2)
						str.push_back(*i);
				dwBufferPos = 0;
				if (!ReadFile(hFile, pBuffer, dwBufferSize, &dwBufferLen, nullptr) || dwBufferLen == 0)
					return false;
				pBegin = pBuffer;
				continue;
			}

			if (pBuffer[dwBufferPos] == '\n'
				|| pBuffer[dwBufferPos] == '\r')
			{
				if (DocumentCodePage == ANSI)
					str.append(pBegin, pBuffer + dwBufferPos);
				else if (DocumentCodePage == UTF16LE)
					for (auto i = pBegin; i < pBuffer + dwBufferPos; i += 2)
						str.push_back(*i);
				else
					for (auto i = pBegin + 1; i < pBuffer + dwBufferPos; i += 2)
						str.push_back(*i);
				pBegin = pBuffer + dwBufferPos + (DocumentCodePage == UTF16LE ? 2 : 1);
				if (!str.empty())
					return true;
			}

			++dwBufferPos;
		}
	}
};

inline
static HANDLE FilePointerToBegin(HANDLE hFile)
{
	SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);
	return hFile;
}

static bool GetKeyValue(string& line, PSTR& key, PSTR& value)
{
	key = nullptr;
	value = nullptr;

	size_t commentPos = line.find(';');
	if (commentPos != string::npos)
		line.resize(commentPos);

	size_t equalPos = line.find('=');
	if (equalPos == string::npos || equalPos == 0)
		return false;

	line[equalPos] = '\0';
	key = &line[0];

	for (int i = static_cast<int>(equalPos) - 1; i >= 0 && std::isspace(key[i]); --i)
		key[i] = '\0';

	value = &line[equalPos + 1];

	while (*value && std::isspace(*value))
		++value;
	return *value != '\0';
}

static bool IsRemark(string& line)
{
	for (auto i = line.cbegin(); i != line.cend(); ++i)
		if (!std::isspace(*i))
			return *i == ';';
	return false;
}

static bool EnumItems(HANDLE hFile, PCSTR node, function<bool(char*, char*)> callback)
{
	string line;
	Stream stream(FilePointerToBegin(hFile));

	while (stream >> line)
	{
		if (IsRemark(line))
			continue;
		if (line.find(node) == 0)
		{
			if (line.size() > std::strlen(node))
			{
				PCSTR rest = line.c_str() + std::strlen(node);
				bool valid = true;
				for (; *rest; ++rest)
				{
					if (!std::isspace(*rest))
					{
						if (*rest == ';')
							break;
						valid = false;
						break;
					}
				}
				if (!valid)
					continue;
			}

			while (stream >> line)
			{
				if (line[0] == '[' || IsRemark(line))
					continue;
				PSTR key = nullptr;
				PSTR value = nullptr;
				if (GetKeyValue(line, key, value)) {
					if (!callback(key, value))
						return false;
				}
				else
					return false;
			}
		}
	}
	return true;
}

bool IsApplicableDriver(HANDLE hFile, WORD wArch)
{
	bool ret = false;
	EnumItems(hFile, "[Manufacturer]", [&](PSTR pKey, PSTR pValue)
		{
			auto end = pValue + strlen(pValue);
			auto pos = find(pValue, end, ',');
			if (pos == end)
				return false;
			while (!ret)
			{
				for (++pos; pos != end; ++pos)
					if (!isspace(*pos))
						break;
				if (pos == end)
					return false;
				pValue = pos;
				if (wArch == PROCESSOR_ARCHITECTURE_INTEL)
					if (_strnicmp(pValue, "NTx86", 5) == 0)
						ret = true;
				if (wArch == PROCESSOR_ARCHITECTURE_AMD64)
					if (_strnicmp(pValue, "NTamd64", 7) == 0)
						ret = true;
				if (wArch == PROCESSOR_ARCHITECTURE_ARM)
					if (_strnicmp(pValue, "NTarm", 5) == 0)
						ret = true;
				if (wArch == PROCESSOR_ARCHITECTURE_ARM64)
					if (_strnicmp(pValue, "NTarm64", 7) == 0)
						ret = true;

				if (pos == end)
					return false;
				pos = find(pValue, end, ',');
				if (pos == end)
					return false;
			}
			return false;
		});

	if (ret)
		return true;
	std::string Node = "[DefaultInstall]";
	PCSTR pArch = nullptr;
	if (wArch == PROCESSOR_ARCHITECTURE_INTEL)
		pArch = "NTx86";
	if (wArch == PROCESSOR_ARCHITECTURE_AMD64)
		pArch = "NTamd64";
	if (wArch == PROCESSOR_ARCHITECTURE_ARM)
		pArch = "NTarm";
	if (wArch == PROCESSOR_ARCHITECTURE_ARM64)
		pArch = "NTarm64";
	if (pArch)
	{
		Node.insert(Node.end() - 1, '.');
		Node.insert(Node.size() - 1, pArch);
	}
	string str;
	Stream stream(FilePointerToBegin(hFile));

	while (stream >> str)
		if (_strnicmp(str.c_str(), Node.c_str(), Node.size()) == 0)
		{
			if (str.size() != Node.size())
			{
				auto i = str.cbegin() + Node.size();
				for (; i != str.cend(); ++i)
					if (!isspace(*i))
						if (*i == ';')
							return true;
						else
							break;
				continue;
			}
			return true;
		}
	return false;
}

bool GetAdditionalDrivers(HANDLE hFile, unordered_set<wstring>& InfFiles)
{
	Stream stream(hFile);
	string str;
	while (stream >> str)
		if (str.find("CopyINF") == 0)
		{
			PSTR pKey, pValue;
			if (GetKeyValue(str, pKey, pValue))
			{
				auto end = pValue + strlen(pValue);
				for (auto pos = find(pValue, end, ','); pos != end; pos = find(pValue, end, ','))
				{
					auto p = pos - 1;
					if (p == pValue)
					{
						SetLastError(ERROR_INVALID_DATA);
						return false;
					}
					while (isspace(*p))
						--p;
					InfFiles.insert(wstring(pValue, p + 1));
					pValue = pos + 1;
				}
				InfFiles.insert(wstring(pValue, pValue + strlen(pValue)));
			}
			else
				return false;
		}
	return true;
}

static bool FindDriversRecursive(wstring& DirRoot, wstring& Subdir, WIN32_FIND_DATAW& FindFileData, vector<wstring>& Drivers)
{
	auto len = DirRoot.size();
	DirRoot += Subdir;
	DirRoot += '*';

	HANDLE hFind = FindFirstFileW(DirRoot.c_str(), &FindFileData);
	if (hFind == INVALID_HANDLE_VALUE)
		return false;
	DirRoot.resize(len);
	len = Subdir.size();
	do
	{
		if (wcscmp(FindFileData.cFileName, L".") == 0 || wcscmp(FindFileData.cFileName, L"..") == 0
			|| (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
			continue;
		else if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			Subdir += FindFileData.cFileName;
			Subdir.push_back('\\');
			FindDriversRecursive(DirRoot, Subdir, FindFileData, Drivers);
		}
		else if (PathMatchSpecW(FindFileData.cFileName, L"*.inf"))
		{
			Subdir += FindFileData.cFileName;
			Drivers.push_back(Subdir);
		}
		else
			continue;
		Subdir.resize(len);
	} while (FindNextFileW(hFind, &FindFileData));
	FindClose(hFind);
	return true;
}

bool FindDrivers(PCWSTR pDirectory, WORD wArch, vector<wstring>& Drivers)
{
	Drivers.clear();
	HANDLE hObject = CreateFileW(pDirectory, 0, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (hObject == INVALID_HANDLE_VALUE)
		return false;
	wstring Path = GetFinalPathName(hObject).GetPointer();
	CloseHandle(hObject);

	if (Path.back() != '\\')
		Path.push_back('\\');
	wstring Subdir;
	WIN32_FIND_DATAW FindFileData;
	bool ret = FindDriversRecursive(Path, Subdir, FindFileData, Drivers);
	DWORD dwError = GetLastError();

	unordered_set<wstring> AdditionalDrivers;
	auto len = Path.size();
	for (size_t i = 0; i != Drivers.size(); ++i)
	{
		Path.resize(len);
		Path += Drivers[i];
		unordered_set<wstring> InfFiles;
		hObject = CreateFileW(Path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (hObject == INVALID_HANDLE_VALUE)
			goto SkipThis;

		if (!IsApplicableDriver(hObject, wArch)
			|| !GetAdditionalDrivers(FilePointerToBegin(hObject), InfFiles))
		{
			CloseHandle(hObject);
		SkipThis:
			Drivers.erase(Drivers.begin() + i);
			--i;
			continue;
		}

		CloseHandle(hObject);
		for (auto& j : InfFiles)
		{
			auto pos = Drivers[i].rfind('\\');
			if (pos != wstring::npos)
			{
				auto InfName = Drivers[i].substr(0, pos + 1);
				InfName += j;
				AdditionalDrivers.insert(move(InfName));
			}
			else
				AdditionalDrivers.insert(j);
		}
	}

	for (size_t i = 0; i != Drivers.size(); ++i)
		for (auto j = AdditionalDrivers.begin(); j != AdditionalDrivers.end(); ++j)
			if (_wcsicmp(Drivers[i].c_str(), j->c_str()) == 0)
			{
				Drivers.erase(Drivers.begin() + i);
				--i;
				AdditionalDrivers.erase(j);
				break;
			}

	Path.resize(len);
	for (auto& i : Drivers)
		i.insert(0, Path);

	SetLastError(dwError);
	return ret;
}
