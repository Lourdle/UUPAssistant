#pragma once

#include <Windows.h>
#include <ShlObj.h>
#include <winternl.h>

#include <Lourdle.UIFramework.h>

#include "MyRaii.h"

void SetProcessEfficiencyMode(bool bEnable);

IFileOpenDialog* CreateFileOpenDialogInstance();
bool GetOpenFolderName(Lourdle::UIFramework::WindowBase* pOwner, Lourdle::UIFramework::String& refString);

bool CopyDirectory(PCWSTR pSourceDirectory, PCWSTR pDestinationDirectory);
ULONGLONG GetDirectorySize(PCWSTR pPath);

NTSTATUS DeleteFileOnClose(HANDLE FileHandle);
