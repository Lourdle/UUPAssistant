#pragma once

#ifndef LOURDLE_DOWNLOADERAPI_H
#define LOURDLE_DOWNLOADERAPI_H

#include <Windows.h>

#define LOURDLE_DOWNLOADER_API __declspec(dllimport)

#ifdef __cplusplus
extern "C" {
#else
#include <stdbool.h>
#endif

typedef struct _LRDLDLTASK LRDLDLTASK;

#define LRDLDL_UNKNOWN_FILE_SIZE									((ULONGLONG)-1)
#define LRDLDL_DEFAULT_RESPONSE_TIMEOUT								30000

LOURDLE_DOWNLOADER_API
VOID
LRDLdlDelete(
	_In_ PVOID pMem
);

typedef enum _LRDLDLSTATE
{
	LRDLDLSTATE_UNInitializeD,
	LRDLDLSTATE_PENDING,
	LRDLDLSTATE_DOWNLOADING,
	LRDLDLSTATE_STOPPED,
	LRDLDLSTATE_FINISHED,
	LRDLDLSTATE_FAILED
}LRDLDLSTATE;

LOURDLE_DOWNLOADER_API _Maybenull_ LRDLDLTASK* LRDLdlCreateTask(
	_In_ PCWSTR pResUrl
);

typedef enum _LRDLDLCREATETASK_MESSAGE
{
	LRDLDLCREATETASK_MESSAGE_CONNECTING,
	LRDLDLCREATETASK_MESSAGE_CONNECTION_FAILED,
	LRDLDLCREATETASK_MESSAGE_SENDINGREQUEST,
	LRDLDLCREATETASK_MESSAGE_SENDINGREQUEST_FAILED,
	LRDLDLCREATETASK_MESSAGE_RECEIVINGRESPONSE,
	LRDLDLCREATETASK_MESSAGE_RECEIVINGRESPONSE_FAILED,
	LRDLDLCREATETASK_MESSAGE_RECEIVEDRESPONSE,
	LRDLDLCREATETASK_MESSAGE_GETSESSIONINFO,
	LRDLDLCREATETASK_MESSAGE_GETURL,
	LRDLDLCREATETASK_MESSAGE_GETCONTENTTYPE,
	LRDLDLCREATETASK_MESSAGE_QUERYCONTENTLENGTH,
	LRDLDLCREATETASK_MESSAGE_QUERYHEADER,
	LRDLDLCREATETASK_MESSAGE_GETRAWHEADER,
	LRDLDLCREATETASK_MESSAGE_FETCHDATA,
	LRDLDLCREATETASK_MESSAGE_FETCHINGDATA_FAILED
}LRDLDLCREATETASK_MESSAGE;

typedef union _LRDLDLCREATETASK_MESSAGE_DATA
{
	struct
	{
		DWORD dwErrorCode;
	} ConnectionFailed;

	struct
	{
		DWORD dwErrorCode;
	} SendingRequestFailed;

	struct
	{
		DWORD dwErrorCode;
	} ReceivingResponseFailed;

	struct
	{
		DWORD dwStatusCode;
		PCWSTR pStatusText;
	} ReceivedResponse;

	struct
	{
		PCWSTR pUrl;
	} GetUrl;

	struct
	{
		PCWSTR pContentType;
	} GetContentType;

	struct
	{
		ULONGLONG ullContentLength;
	} QueryContentLength;

	struct
	{
		PWSTR pHeaderName;
		PCWSTR pHeaderValue;
	} QueryHeader;

	struct
	{
		PCWSTR pHeader;
	} GetRawHeader;

	struct
	{
		HANDLE hReadPipe;
	} FetchData;

	struct
	{
		DWORD dwErrorCode;
	} FetchDataFailed;
}LRDLDLCREATETASK_MESSAGE_DATA;

#define LRDLDLCREATETASK_CALLBACK_RESULT_CANCEL						((INT_PTR)(FALSE))
#define LRDLDLCREATETASK_CALLBACK_RESULT_CONTINUE					((INT_PTR)(TRUE))
#define LRDLDLCREATETASK_CALLBACK_RESULT_CONTINUE_ANYWAY			(LRDLDLCREATETASK_CALLBACK_RESULT_CONTINUE + 1)
#define LRDLDLCREATETASK_CALLBACK_RESULT_GET_URL					((INT_PTR)(3))
#define LRDLDLCREATETASK_CALLBACK_RESULT_GET_CONTENT_TYPE			((INT_PTR)(4))
#define LRDLDLCREATETASK_CALLBACK_RESULT_QUERY_HEADER(pName)		((INT_PTR)(pName))
#define LRDLDLCREATETASK_CALLBACK_RESULT_QUERY_CONTENT_LENGTH		((INT_PTR)(-1))
#define LRDLDLCREATETASK_CALLBACK_RESULT_GET_RAW_HEADER				((INT_PTR)(-2))
#define LRDLDLCREATETASK_CALLBACK_RESULT_FETCH_DATA					((INT_PTR)(-3))

typedef INT_PTR(*LRDLDLCREATETASK_CALLBACK)(LRDLDLCREATETASK_MESSAGE Message, const LRDLDLCREATETASK_MESSAGE_DATA* pData, PVOID);

typedef struct _LRDLDLCREATETASK_HTTP_HEADERS
{
	DWORD dwHeaderCount;
	struct
	{
		PCWSTR pName;
		PCWSTR pValue;
	} Headers[];
} LRDLDLCREATETASK_HTTP_HEADERS;

LOURDLE_DOWNLOADER_API
_Maybenull_
LRDLDLTASK*
LRDLdlCreateTaskEx(
	_In_ PCWSTR pResUrl,
	_In_opt_ PCWSTR pUserAgent,
	_In_opt_ const LRDLDLCREATETASK_HTTP_HEADERS* pHeaders,
	_In_opt_ int nTimeout,
	_In_opt_ LRDLDLCREATETASK_CALLBACK pfnCallback,
	_In_opt_ PVOID pvData
);

typedef struct _LRDLDLREESINFO
{
	PCWSTR pUrl;
	PCWSTR pOriginalUrl;
	PCWSTR pETag;
	ULONGLONG ullResourceBytes;
	bool bPartialContentSupported;
} LRDLDLRESINFO;

LOURDLE_DOWNLOADER_API _Maybenull_ LRDLDLRESINFO* LRDLdlGetResourceInfo(
	_In_ LRDLDLTASK* pTask
);

typedef struct _LRDLDLTHREADTASKRANGE
{
	ULONGLONG ullBegin;
	ULONGLONG ullEnd;
}LRDLDLTHREADTASKRANGE;

LOURDLE_DOWNLOADER_API
_Must_inspect_result_
BYTE
LRDLdlInitializeTask(
	_In_ LRDLDLTASK* pTask,
	_In_ PCWSTR pFileName,
	_In_ BYTE nThreads,
	_In_opt_ const LRDLDLTHREADTASKRANGE* pszThreadTaskRanges,
	_In_ bool bSpaceAllocation,
	_In_ bool bAllowResumeOfBrokenTransfer
);

LOURDLE_DOWNLOADER_API
_Must_inspect_result_
BYTE
LRDLdlInitializeTask2(
	_In_ LRDLDLTASK* pTask,
	_In_opt_ PCWSTR pOutDir,
	_In_opt_ PCWSTR pFileName,
	_In_ BYTE nThreads,
	_In_opt_ const LRDLDLTHREADTASKRANGE* pszThreadTaskRanges,
	_In_ bool bSpaceAllocation,
	_In_ bool bAllowResumeOfBrokenTransfer
);


typedef struct _LRDLDLTHREADPROGRESS
{
	LRDLDLTHREADTASKRANGE TaskRange;
	ULONGLONG ullFetchedBytes;
}LRDLDLTHREADPROGRESS;

LOURDLE_DOWNLOADER_API
bool
LRDLdlGetThreadProgress(
	_In_ LRDLDLTASK* pTask,
	_In_ BYTE Index,
	_Out_ LRDLDLTHREADPROGRESS* pThreadProgress,
	_Out_opt_ LRDLDLSTATE* pThreadState
#ifdef __cplusplus
	= nullptr
#endif
);

LOURDLE_DOWNLOADER_API
bool
LRDLdlGetThreadError(
	_In_ LRDLDLTASK* pTask,
	_In_ BYTE Index,
	_Out_ PDWORD pdwErrorCode
);


typedef struct _LRDLDLTASKINFO
{
	LRDLDLRESINFO ResourceInfo;
	PCWSTR pLocalFileName;
	PCWSTR pUserAgent;
	PCWSTR pHeaders;
	int nTimeout;
	BYTE nThreads;
	const LRDLDLTHREADPROGRESS* pszThreadProgress;
}LRDLDLTASKINFO;

typedef VOID LRDLDLTASKMETADATA;

LOURDLE_DOWNLOADER_API
bool
LRDLdlGetTaskInfo(
	_In_ PCWSTR pFileName,
	_Out_ LRDLDLTASKINFO* pTaskInfo,
	_Out_ LRDLDLTASKMETADATA** ppTaskMetadata
);

LOURDLE_DOWNLOADER_API
bool
LRDLdlGetTaskInfoFromMetadata(
	_In_ const LRDLDLTASKMETADATA* pTaskMetadata,
	_Out_ LRDLDLTASKINFO* pTaskInfo
);

LOURDLE_DOWNLOADER_API
_Maybenull_
LRDLDLTASKMETADATA*
LRDLdlGetTaskMetadata(
	_In_ LRDLDLTASK* pTask,
	_Out_ PDWORD pcbData
);

LOURDLE_DOWNLOADER_API
DWORD
LRDLdlGetSizeOfTaskMetadata(
	_In_ LRDLDLTASK* pTask
);

LOURDLE_DOWNLOADER_API
_Maybenull_
LRDLDLTASK*
LRDLdlResumeTask(
	_In_ PCWSTR pLocalFileName,
	_In_opt_ PCWSTR pUrl
#ifdef __cplusplus
	= nullptr
#endif
);

LOURDLE_DOWNLOADER_API
_Maybenull_
LRDLDLTASK*
LRDLdlResumeTask2(
	_In_ const LRDLDLTASKMETADATA* pTaskMetadata,
#ifdef __cplusplus
	_In_opt_ PCWSTR pLocalFileName = nullptr,
	_In_opt_ PCWSTR pUrl = nullptr
#else
	_In_opt PCWSTR pLocalFileName,
	_In_opt PCWSTR pUrl
#endif
);

typedef struct _LRDLDLTASKPROGRESS
{
	ULONGLONG ullTotalBytes;
	ULONGLONG ullFetchedBytes;
	ULONGLONG ullBytesPerSecond;
	LRDLDLSTATE State;
	BYTE nThreads;
	BYTE nActiveThreads;
}LRDLDLTASKPROGRESS;

LOURDLE_DOWNLOADER_API
bool
LRDLdlGetTaskProgress(
	_In_ LRDLDLTASK* pTask,
	_Out_ LRDLDLTASKPROGRESS* pProgress
);

LOURDLE_DOWNLOADER_API
BYTE
LRDLdlRetryTask(
	_In_ LRDLDLTASK* pTask,
#ifdef __cplusplus
	_Out_opt_ PBYTE pnPreviousFailedThreads = nullptr,
	_Out_opt_ PBYTE pnPreviousStoppedThreads = nullptr
#else
	_Out_opt_ PBYTE pnPreviousFailedThreads,
	_Out_opt_ PBYTE pnPreviousStoppedThreads
#endif
);

LOURDLE_DOWNLOADER_API
bool
LRDLdlBeginTask(
	_In_ LRDLDLTASK* pTask
);

LOURDLE_DOWNLOADER_API
bool
LRDLdlWaitForTask(
	_In_ LRDLDLTASK* pTask,
	_In_ DWORD dwTimeout,
#ifdef __cplusplus
	_In_opt_ HANDLE hObject = nullptr,
	_Out_opt_ LRDLDLSTATE* pState = nullptr
#else
	_In_opt_ HANDLE hObject,
	_Out_opt_ LRDLDLSTATE* pState
#endif
);

LOURDLE_DOWNLOADER_API
bool
LRDLdlStopTask(
	_In_ LRDLDLTASK* pTask
);

LOURDLE_DOWNLOADER_API
bool
LRDLdlDeleteTask(
	_In_ LRDLDLTASK* pTask
);

LOURDLE_DOWNLOADER_API
bool
LRDLdlDeleteTaskAndFile(
	_In_ LRDLDLTASK* pTask
);

LOURDLE_DOWNLOADER_API
PWSTR
LRDLdlGetErrorMessage(
	_In_ DWORD dwErrCode
#ifdef __cplusplus
	= GetLastError()
#endif
);

#ifdef __cplusplus
}
#endif

#endif
