module;
#include "pch.h"
#include "framework.h"
#include "FetcherMain.h"
#include "resource.h"

#include <thread>

export module Http;

using namespace Lourdle::UIFramework;
using namespace std;

struct CallbackContext
{
	HANDLE hFile;
	volatile DWORD* dwErrCode;
};

static INT_PTR cb(LRDLDLCREATETASK_MESSAGE Message, const LRDLDLCREATETASK_MESSAGE_DATA* pData, CallbackContext* ctx)
{
	switch (Message)
	{
	case LRDLDLCREATETASK_MESSAGE_GETSESSIONINFO:
		return LRDLDLCREATETASK_CALLBACK_RESULT_FETCH_DATA;
	case LRDLDLCREATETASK_MESSAGE_FETCHDATA:
	{
		DWORD written, read;
		constexpr DWORD kIoBufferSize = 4096;
		BYTE szBuf[kIoBufferSize];
		while (ReadFile(pData->FetchData.hReadPipe, szBuf, kIoBufferSize, &read, nullptr))
			if (!WriteFile(ctx->hFile, szBuf, read, &written, nullptr)
				|| written != read)
			{
				*ctx->dwErrCode = GetLastError();
				return LRDLDLCREATETASK_CALLBACK_RESULT_CANCEL;
			}
	}
	return LRDLDLCREATETASK_CALLBACK_RESULT_CANCEL;
	default:
		return LRDLDLCREATETASK_CALLBACK_RESULT_CONTINUE;
	}
}


export void HttpGetData(HANDLE hFile, volatile DWORD* pdwErrCode, PCWSTR pUrl, bool bWait, bool bAutoClose = true, LRDLDLCREATETASK_HTTP_HEADERS* headers = nullptr)
{
	*pdwErrCode = ERROR_SUCCESS;
	CallbackContext* ctx = new CallbackContext
	{
		hFile,
		pdwErrCode
	};

	auto CreateTask = [headers, bAutoClose](PCWSTR url, CallbackContext* ctx)
		{
			LRDLdlCreateTaskEx(url, nullptr, headers, LRDLDL_DEFAULT_RESPONSE_TIMEOUT, reinterpret_cast<LRDLDLCREATETASK_CALLBACK>(cb), ctx);
			if (*ctx->dwErrCode == ERROR_SUCCESS)
			{
				*ctx->dwErrCode = GetLastError();
				if (*ctx->dwErrCode == ERROR_CANCELLED)
					*ctx->dwErrCode = ERROR_SUCCESS;
			}

			if (bAutoClose)
				CloseHandle(ctx->hFile);
			delete ctx;
		};

	if (bWait)
		CreateTask(pUrl, ctx);
	else
		thread(CreateTask, pUrl, ctx).detach();
}
