#include "pch.h"
#include "framework.h"
#include "FakeWimgAPI.h"
#include "MessageIds.h"
#include <CommCtrl.h>

static bool g_bCancel = false;

static bool IsButton(HWND hWnd)
{
	WCHAR szClassName[MAX_PATH];
	if (GetClassNameW(hWnd, szClassName, MAX_PATH) == 0)
		return false;
	return _wcsicmp(szClassName, L"Button") == 0;
}

static BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam)
{
	if (IsButton(hWnd))
	{
		*reinterpret_cast<HWND*>(lParam) = hWnd;
		return FALSE;
	}
	return TRUE;
}

static void PostCommand(HWND hWnd)
{
	PostMessageW(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(GetWindowLongW(hWnd, GWL_ID), BN_CLICKED), reinterpret_cast<LPARAM>(hWnd));
}

static LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, CWPRETSTRUCT* pMsg)
{
	if (pMsg->message == WM_INITDIALOG && GetParent(pMsg->hwnd) && g_bCancel)
		PostCommand(GetDlgItem(pMsg->hwnd, IDYES));
	else if (pMsg->message == WM_COMMAND)
	{
		if (GetParent(pMsg->hwnd) == GetWindow(g_hWnd, GW_OWNER) && IsButton(reinterpret_cast<HWND>(pMsg->lParam)))
		{
			if (HIWORD(pMsg->wParam) == BN_CLICKED && LOWORD(pMsg->wParam) == IDYES)
				SendMessageW(g_hWnd, UpgradeProgress_CancelRequested, 0, 0);
		}
		else if (pMsg->wParam == CancelInstallation::wParam && pMsg->lParam == CancelInstallation::lParam)
		{
			HWND hWnd = nullptr;
			EnumChildWindows(pMsg->hwnd, EnumWindowsProc, reinterpret_cast<LPARAM>(&hWnd));
			if (hWnd)
			{
				PostCommand(hWnd);
				g_bCancel = true;
			}
		}
	}

	return CallNextHookEx(nullptr, nCode, wParam, reinterpret_cast<LPARAM>(pMsg));
}

static BOOL CALLBACK EnumWindowsProc2(HWND hWnd, LPARAM lParam)
{
	DWORD pid;
	GetWindowThreadProcessId(hWnd, &pid);
	if (pid == GetCurrentProcessId())
	{
		*reinterpret_cast<HWND*>(lParam) = hWnd;
		return FALSE;
	}
	return TRUE;
}

HWND FindWindowCurrentProcess()
{
	HWND hWnd = nullptr;
	EnumWindows(EnumWindowsProc2, reinterpret_cast<LPARAM>(&hWnd));
	return hWnd;
}

void HookWindows()
{
	HWND hWnd = FindWindowCurrentProcess();
	if (hWnd)
	{
		HMODULE hModule = nullptr;
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&HookProc), &hModule);
		SetWindowsHookExW(WH_CALLWNDPROCRET, reinterpret_cast<HOOKPROC>(HookProc), nullptr, GetWindowThreadProcessId(hWnd, nullptr));
	}
}
