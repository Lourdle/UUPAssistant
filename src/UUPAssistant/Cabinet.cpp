#include "pch.h"
#include <Shlwapi.h>
#include <fdi.h>


static FNALLOC(fnAlloc)
{
	return HeapAlloc(reinterpret_cast<HANDLE>(_get_heap_handle()), 0, cb);
}

static FNFREE(fnFree)
{
	HeapFree(reinterpret_cast<HANDLE>(_get_heap_handle()), 0, pv);
}

struct OutFile
{
	OutFile(PCSTR pstr, DWORD& dwErrCode) : hFile(INVALID_HANDLE_VALUE), UserHandle(false), dwErrCode(dwErrCode)
	{
		int cch = MultiByteToWideChar(CP_ACP, 0, pstr, -1, nullptr, 0);
		if (cch > 0)
		{
			FileName.resize(cch - 1);
			MultiByteToWideChar(CP_ACP, 0, pstr, -1, FileName.data(), cch);
		}
	}

	~OutFile()
	{
		if (hFile != INVALID_HANDLE_VALUE)
			CloseHandle(hFile);
	}

	UINT Write(PVOID pv, UINT cb)
	{
		DWORD dwBytesWritten;

		if (!WriteFile(hFile, pv, cb, &dwBytesWritten, nullptr))
		{
			dwErrCode = GetLastError();
			dwBytesWritten = -1;
		}

		return dwBytesWritten;
	}

	INT_PTR Mark = 0;
	std::wstring FileName;
	HANDLE hFile;
	bool UserHandle;
	DWORD& dwErrCode;
	FILETIME FileTime;
};


struct StreamWrapper
{
	PBYTE pData;
	LONG Offset;
	DWORD cbData;

	UINT Read(PVOID pv, UINT cb)
	{
		DWORD cbRead = min(cb, cbData - Offset);
		memcpy(pv, pData + Offset, cbRead);
		Offset += cbRead;
		return cbRead;
	}

	LONG Seek(LONG dist, int seektype)
	{
		const auto original = Offset;

		switch (seektype) {
		case SEEK_SET: Offset = dist; break;
		case SEEK_CUR: Offset += dist; break;
		case SEEK_END: Offset = cbData - dist; break;
		}

		return original;
	}
};

static FNOPEN(fnOpen)
{
	return reinterpret_cast<INT_PTR>(new StreamWrapper(*reinterpret_cast<StreamWrapper*>(strtoull(pszFile, nullptr, 10))));
}

static FNCLOSE(fnClose)
{
	if (*reinterpret_cast<INT_PTR*>(hf) == 0)
		delete reinterpret_cast<OutFile*>(hf);
	else
		delete reinterpret_cast<StreamWrapper*>(hf);
	return 0;
}


static DWORD FDIErrToWin32(ERF erf)
{
	switch (erf.erfOper)
	{
	case FDIERROR_NONE:
		return ERROR_SUCCESS;
	case FDIERROR_CABINET_NOT_FOUND:
		return ERROR_FILE_NOT_FOUND;
	case FDIERROR_NOT_A_CABINET:
		return ERROR_BAD_FORMAT;
	case FDIERROR_UNKNOWN_CABINET_VERSION:case FDIERROR_CORRUPT_CABINET:case FDIERROR_BAD_COMPR_TYPE:
		return ERROR_INVALID_DATA;
	case FDIERROR_ALLOC_FAIL:
		return ERROR_OUTOFMEMORY;
	case FDIERROR_USER_ABORT:
		return ERROR_CANCELLED;
	default:
		return ERROR_INVALID_FUNCTION;
	}
}

struct ExpansionContext
{
	CAB_EXPANSION_PROC callback;
	PVOID pv;
	FileCreator fc;
	HANDLE hFile;
	DWORD lastError = ERROR_SUCCESS;
	USHORT cTotalFileCount;
};

static FNFDINOTIFY(fnFDINotify)
{
	switch (fdint)
	{
	case fdintCOPY_FILE:
	{
		auto ctx = reinterpret_cast<ExpansionContext*>(pfdin->pv);
		OutFile* pof = new OutFile(pfdin->psz1, ctx->lastError);
		DosDateTimeToFileTime(pfdin->date, pfdin->time, &pof->FileTime);
		if (ctx->callback)
		{
			ctx->hFile = nullptr;

			if (!ctx->callback(true, pof->FileName.c_str(), ctx->cTotalFileCount, ctx->hFile, ctx->pv))
			{
				delete pof;
				ctx->lastError = ERROR_CANCELLED;
				return -1;
			}
			if (ctx->hFile == INVALID_HANDLE_VALUE)
			{
				delete pof;
				return 0;
			}
			else if (ctx->hFile)
			{
				pof->hFile = ctx->hFile;
				pof->UserHandle = true;
				return reinterpret_cast<INT_PTR>(pof);
			}
		}
		pof->hFile = ctx->fc.Create(pof->FileName.c_str());
		if (pof->hFile == INVALID_HANDLE_VALUE)
		{
			ctx->lastError = GetLastError();
			pof->hFile = INVALID_HANDLE_VALUE;
		}
		return reinterpret_cast<INT_PTR>(pof);
	}
	case fdintCLOSE_FILE_INFO:
	{
		bool ret = true;
		auto ctx = reinterpret_cast<ExpansionContext*>(pfdin->pv);
		auto pof = reinterpret_cast<OutFile*>(pfdin->hf);
		if (ctx->callback)
		{
			ctx->hFile = pof->UserHandle ? reinterpret_cast<HANDLE>(pof->hFile) : nullptr;
			ret = ctx->callback(false, pof->FileName.c_str(), ctx->cTotalFileCount, ctx->hFile, ctx->pv);
			if (pof->UserHandle)
			{
				pof->hFile = INVALID_HANDLE_VALUE;
				delete pof;
				if (!ret)
					ctx->lastError = ERROR_CANCELLED;
				return ret;
			}
		}

		SetFileTime(pof->hFile, nullptr, nullptr, &pof->FileTime);
		delete pof;
		if (!ret)
			ctx->lastError = ERROR_CANCELLED;
		return ret;
	}
	default:
		return 0;
	}
}

template<typename T, typename U>
constexpr
T GetMethodPtr(U addr)
{
	return *reinterpret_cast<T*>(&addr);
}

bool ExpandCabFile(PVOID pvCabData, DWORD cbData, PCWSTR pDestDir, CAB_EXPANSION_PROC pfnExpansionProc, PVOID pvData)
{
	HFDI hFDI;
	ERF erf = {};
	
	hFDI = FDICreate(
		fnAlloc, fnFree,
		fnOpen, GetMethodPtr<PFNREAD>(&StreamWrapper::Read),
		GetMethodPtr<PFNWRITE>(&OutFile::Write), fnClose,
		GetMethodPtr<PFNSEEK>(&StreamWrapper::Seek),
		cpuUNKNOWN, &erf);

	StreamWrapper stream = {
		.pData = reinterpret_cast<PBYTE>(pvCabData),
		.Offset = 0,
		.cbData = cbData
	};

	ExpansionContext context{
		.callback = pfnExpansionProc,
		.pv = pvData,
		.fc = pDestDir
	};
	if (!context.fc)
	{
		FDIDestroy(hFDI);
		return false;
	}

	FDICABINETINFO info;
	auto addr_str = std::to_string(reinterpret_cast<ULONG_PTR>(&stream));
	if (!FDIIsCabinet(hFDI, reinterpret_cast<INT_PTR>(&stream), &info))
	{
		FDIDestroy(hFDI);
		return FALSE;
	}
	else
	{
		context.cTotalFileCount = info.cFiles;
		stream.Offset = 0;
	}

	bool ret = FDICopy(hFDI, const_cast<LPSTR>(addr_str.c_str()), const_cast<LPSTR>(addr_str.c_str() + addr_str.size()), 0, fnFDINotify, nullptr, &context);
	if (context.lastError == ERROR_SUCCESS)
		context.lastError = FDIErrToWin32(erf);

	FDIDestroy(hFDI);
	SetLastError(context.lastError);
	return ret;
}

bool ExpandCabFile(PCWSTR pCabFile, PCWSTR pDestDir, CAB_EXPANSION_PROC pfnExpansionProc, PVOID pvData)
{
	HANDLE hFile = CreateFileW(pCabFile, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	HANDLE hFileMapping = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
	if (!hFileMapping)
	{
		CloseHandle(hFile);
		return false;
	}

	auto pCabData = MapViewOfFile(hFileMapping, FILE_MAP_READ, 0, 0, 0);
	if (!pCabData)
	{
		CloseHandle(hFileMapping);
		CloseHandle(hFile);
		return false;
	}

	auto ret = ExpandCabFile(pCabData, GetFileSize(hFile, nullptr), pDestDir, pfnExpansionProc, pvData);
	UnmapViewOfFile(pCabData);
	CloseHandle(hFileMapping);
	CloseHandle(hFile);
	return ret;
}
