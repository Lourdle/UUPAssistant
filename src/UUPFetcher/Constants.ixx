module;
#include "pch.h"
#include <Windows.h>

export module Constants;

export constexpr UINT kMsgExitProcess = WM_USER + 3;

export constexpr UINT kCmdNextId = 0x1234;
export constexpr UINT kCmdNextCode = 0x1234;

export constexpr WORD kDisplaySummaryPageNotifyId = 0x7812;

export constexpr LONG ProgressStepMax = 10000;
export constexpr LONG ProgressStepsPerPercent = ProgressStepMax / 100;

export constexpr PCWSTR UUPDUMP_API_BASE_URL = L"https://api.uupdump.net";
export constexpr PCWSTR UUPDUMP_WEBSITE_BASE_URL = L"https://uupdump.net";
