module;
#include "pch.h"
#include <Windows.h>

export module Constants;

// Centralized WM_USER-based message IDs and small IPC enums.
// Keep values stable: they may be used for cross-thread/cross-process PostMessage.
namespace UupAssistantMsg
{
	export
	{
		// Installer UI
		constexpr UINT Installer_DestroyWindow = WM_USER + 64;
		constexpr UINT Installer_ShowPostInstallDialog = WM_USER + 128;

		// Driver dialog (cross-process callbacks from Bootstrap-hosted worker)
		constexpr UINT DriverDlg_Error = WM_USER + 1;
		constexpr UINT DriverDlg_DriverItem = WM_USER + 2;
		constexpr UINT DriverDlg_DriverScanCompleted = WM_USER + 3;
		constexpr UINT DriverDlg_WorkerExited = WM_USER + 4;
		constexpr UINT DriverDlg_SystemEntry = WM_USER + 5;
		constexpr UINT DriverDlg_SystemScanCompleted = WM_USER + 6;
		constexpr UINT DriverDlg_ScanPath = WM_USER + 7;

		// Upgrade progress
#include "../FakeWimgAPI/MessageIds.h"
		constexpr UINT UpgradeProgress_ProcessExited = WM_USER + 16;
	}
}

// Bootstrap-host action written by child process into parent memory.
export enum class HostAction : BYTE
{
	None = 0,
	Shutdown = 1,
	Reboot = 2,
	Quit = 3,
	WaitForProcess = 4
};

#include "../UUPFetcher/resource.h"
export constexpr UINT String_UUPFetcherId = String_UUPFetcher;

export constexpr ULONGLONG kMinTargetDiskBytes = 20ULL * 1024 * 1024 * 1024;
