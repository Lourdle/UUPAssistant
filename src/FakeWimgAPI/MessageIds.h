#pragma once

#include <Windows.h>

constexpr UINT UpgradeProgress_StartExportReImg = WM_USER + 1;
constexpr UINT UpgradeProgress_CreateFakeInstallEsd = WM_USER + 2;
constexpr UINT UpgradeProgress_ConfigureSafeOSSucceeded = WM_USER + 3;
constexpr UINT UpgradeProgress_ConfiguringSafeOS = WM_USER + 4;
constexpr UINT UpgradeProgress_CancelRequested = WM_USER + 5;
constexpr UINT UpgradeProgress_StartApplyImage = WM_USER + 6;
constexpr UINT UpgradeProgress_StartSafeOSDU = WM_USER + 7;
constexpr UINT UpgradeProgress_StartDismAndEdge = WM_USER + 8;

namespace CancelInstallation
{
	constexpr WPARAM wParam = 12345678;
	constexpr LPARAM lParam = 9101112;
}
