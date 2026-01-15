module;
#include <Windows.h>
#include "MyRaii.h"

export module CheckMiniNt;

export bool IsMiniNtBoot()
{
	DWORD cbData = 0;
	if (RegGetValueA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control", "SystemStartOptions", RRF_RT_REG_SZ, nullptr, nullptr, &cbData) == ERROR_SUCCESS)
	{
		MyUniqueBuffer<PSTR> szData = cbData;
		RegGetValueA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control", "SystemStartOptions", RRF_RT_REG_SZ, nullptr, szData, &cbData);
		for (DWORD i = 0; i != cbData; ++i)
			if (szData[i] >= 'a' && szData[i] <= 'z')
				szData[i] += 'A' - 'a';
		if (strstr(szData, " MININT "))
			return true;
	}
	return false;
}
