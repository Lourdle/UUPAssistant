#ifdef DLLFDHOOK_EXPORTS
#define DLLFDHOOK_API __declspec(dllexport)
#else
#define DLLFDHOOK_API __declspec(dllimport)
#endif

#include <Windows.h>

#ifdef __cplusplus
EXTERN_C_START
#endif

#define DLLFD_FLAG_FDRES 0x00000001UL
#define DLLFD_FLAG_FDMSG 0x00000002UL
#define DLLFD_FLAG_FDSTR 0x00000004UL
#define DLLFD_FLAG_FDPROC 0x00000008UL
#define DLLFD_FLAG_ALL (DLLFD_FLAG_FDRES | DLLFD_FLAG_FDMSG | DLLFD_FLAG_FDSTR | DLLFD_FLAG_FDPROC)

DLLFDHOOK_API HANDLE* DetourUpdateAllThreads(ULONG* ulThreadCount);
DLLFDHOOK_API void CloseThreadsAndFree(HANDLE* hThreads, ULONG ulThreadCount);

DLLFDHOOK_API void InitializeDllFd(HMODULE hTarget, HMODULE hModule, DWORD dwFlags);

DLLFDHOOK_API HMODULE Rollback();

static PVOID GetKernelExport(const char* pszName)
{
	HMODULE hModule = GetModuleHandleA("KERNELBASE.dll");
	if (!hModule)
		hModule = GetModuleHandleA("KERNEL32.dll");
	return GetProcAddress(hModule, pszName);
}

#ifdef __cplusplus
EXTERN_C_END
#endif
