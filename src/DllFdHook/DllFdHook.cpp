#include "framework.h"
#include "DllFdHook.h"

extern "C"
{
	NTSTATUS NTAPI LdrFindResource_U(
		PVOID DllHandle,
		PULONG_PTR ResourcePath,
		ULONG Level,
		PVOID* ResourceDataEntry
	);

	NTSTATUS NTAPI LdrAccessResource(
		PVOID DllHandle,
		PIMAGE_RESOURCE_DATA_ENTRY ResourceDataEntry,
		PVOID* ResourceBuffer,
		ULONG* ResourceLength
	);

	NTSTATUS NTAPI RtlFindMessage(
		PVOID BaseAddress,
		ULONG Type,
		ULONG Language,
		ULONG MessageId,
		PVOID* MessageResourceEntry
	);

	NTSTATUS NTAPI RtlLoadString(
		PVOID DllHandle,
		ULONG StringId,
		PCWSTR StringLanguage,
		ULONG Flags,
		PCWSTR* ReturnString,
		PUSHORT ReturnStringLen,
		PWSTR ReturnLanguageName,
		PULONG ReturnLanguageLen
	);

	FARPROC WINAPI GetProcAddressForCaller(
		HMODULE hModule,
		LPCSTR lpProcName,
		PVOID Callback
	);
}

static HMODULE g_hModule, g_hTarget;

static decltype(LdrFindResource_U)* g_pLdrFindResourceTrampoline = LdrFindResource_U;
static decltype(LdrAccessResource)* g_pLdrAccessResourceTrampoline = LdrAccessResource;
static decltype(GetModuleFileNameW)* g_pGetModuleFileNameWTrampoline = reinterpret_cast<decltype(GetModuleFileNameW)*>(GetKernelExport("GetModuleFileNameW"));
static decltype(RtlFindMessage)* g_pRtlFindMessageTrampoline = RtlFindMessage;
static decltype(RtlLoadString)* g_pRtlLoadStringTrampoline = RtlLoadString;
static decltype(GetProcAddress)* g_pGetProcAddressTrampoline = reinterpret_cast<decltype(GetProcAddress)*>(GetKernelExport("GetProcAddress"));
static decltype(GetProcAddressForCaller)* g_pGetProcAddressForCallerTrampoline = reinterpret_cast<decltype(GetProcAddressForCaller)*>(GetProcAddress(GetModuleHandleA("KERNELBASE.dll"), "GetProcAddressForCaller"));

static NTSTATUS NTAPI MyLdrFindResource_U(
	PVOID DllHandle,
	PULONG_PTR ResourcePath,
	ULONG Level,
	PVOID* ResourceDataEntry
)
{
	if (DllHandle == g_hTarget)
		DllHandle = g_hModule;
	return g_pLdrFindResourceTrampoline(DllHandle, ResourcePath, Level, ResourceDataEntry);
}

static NTSTATUS NTAPI MyLdrAccessResource(
	PVOID DllHandle,
	PIMAGE_RESOURCE_DATA_ENTRY ResourceDataEntry,
	PVOID* ResourceBuffer,
	ULONG* ResourceLength
)
{
	if (DllHandle == g_hTarget)
		DllHandle = g_hModule;
	return g_pLdrAccessResourceTrampoline(DllHandle, ResourceDataEntry, ResourceBuffer, ResourceLength);
}

static BOOL WINAPI MyGetModuleFileNameW(
	HMODULE hModule,
	LPWSTR lpFilename,
	DWORD nSize
)
{
	do if (hModule == g_hTarget)
	{
		// Check if the caller is USER32.dll. If so, redirect the module handle to our module.
		// This is likely to fix resource loading issues when USER32 tries to load resources (like icons or strings)
		// from the hooked module.
		HMODULE hUser32 = GetModuleHandleA("USER32.dll");

		PVOID BackTrace;
		if (RtlCaptureStackBackTrace(1, 1, &BackTrace, nullptr) == 0)
			break;

		HMODULE hModuleBackTrace;
		if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(BackTrace), &hModuleBackTrace)
			&& hModuleBackTrace == hUser32)
			hModule = g_hModule;
	} while (false);

	return g_pGetModuleFileNameWTrampoline(hModule, lpFilename, nSize);
}

static NTSTATUS NTAPI MyRtlFindMessage(
	PVOID BaseAddress,
	ULONG Type,
	ULONG Language,
	ULONG MessageId,
	PVOID* MessageResourceEntry
)
{
	if (BaseAddress == g_hTarget)
		BaseAddress = g_hModule;
	return g_pRtlFindMessageTrampoline(BaseAddress, Type, Language, MessageId, MessageResourceEntry);
}

static NTSTATUS NTAPI MyRtlLoadString(
	PVOID DllHandle,
	ULONG StringId,
	PCWSTR StringLanguage,
	ULONG Flags,
	PCWSTR* ReturnString,
	PUSHORT ReturnStringLen,
	PWSTR ReturnLanguageName,
	PULONG ReturnLanguageLen
)
{
	if (DllHandle == g_hTarget)
		DllHandle = g_hModule;
	return g_pRtlLoadStringTrampoline(DllHandle, StringId, StringLanguage, Flags, ReturnString, ReturnStringLen, ReturnLanguageName, ReturnLanguageLen);
}

static FARPROC WINAPI MyGetProcAddress(
	HMODULE hModule,
	LPCSTR lpProcName
)
{
	if (hModule == g_hTarget)
		hModule = g_hModule;
	return g_pGetProcAddressTrampoline(hModule, lpProcName);
}

static FARPROC WINAPI MyGetProcAddressForCaller(
	HMODULE hModule,
	LPCSTR lpProcName,
	PVOID Callback
)
{
	if (hModule == g_hTarget)
		hModule = g_hModule;
	return g_pGetProcAddressForCallerTrampoline(hModule, lpProcName, Callback);
}

#include <tlhelp32.h>

#include <vector>

HANDLE* DetourUpdateAllThreads(ULONG* ulThreadCount)
{
	DWORD currentProcessId = GetCurrentProcessId();
	DWORD currentThreadId = GetCurrentThreadId();

	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (hSnapshot == INVALID_HANDLE_VALUE)
	{
		*ulThreadCount = 0;
		return nullptr;
	}

	THREADENTRY32 te;
	te.dwSize = sizeof(te);

	std::vector<HANDLE> hThreads;

	if (Thread32First(hSnapshot, &te))
		do
			if (te.th32OwnerProcessID == currentProcessId && te.th32ThreadID != currentThreadId)
			{
				HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
				if (hThread)
				{
					DetourUpdateThread(hThread);
					hThreads.push_back(hThread);
				}
			}
	while (Thread32Next(hSnapshot, &te));
	CloseHandle(hSnapshot);

	DetourUpdateThread(GetCurrentThread());

	*ulThreadCount = static_cast<DWORD>(hThreads.size());
	HANDLE* Handles = new HANDLE[*ulThreadCount];
	memcpy(Handles, hThreads.data(), *ulThreadCount * sizeof(HANDLE));
	return Handles;
}

void CloseThreadsAndFree(HANDLE* hThreads, ULONG ulThreadCount)
{
	for (ULONG i = 0; i != ulThreadCount; ++i)
		CloseHandle(hThreads[i]);
	delete[] hThreads;
}

void InitializeDllFd(HMODULE hTarget, HMODULE hModule, DWORD dwFlags)
{
	g_hTarget = hTarget;
	g_hModule = hModule;

	DetourTransactionBegin();
	ULONG ulThreadCount;
	HANDLE* hThreads = DetourUpdateAllThreads(&ulThreadCount);

	if (dwFlags & DLLFD_FLAG_FDRES)
	{
		DetourAttach(reinterpret_cast<PVOID*>(&g_pLdrFindResourceTrampoline), MyLdrFindResource_U);
		DetourAttach(reinterpret_cast<PVOID*>(&g_pLdrAccessResourceTrampoline), MyLdrAccessResource);
		DetourAttach(reinterpret_cast<PVOID*>(&g_pGetModuleFileNameWTrampoline), MyGetModuleFileNameW);
	}
	if (dwFlags & DLLFD_FLAG_FDMSG)
		DetourAttach(reinterpret_cast<PVOID*>(&g_pRtlFindMessageTrampoline), MyRtlFindMessage);
	if (dwFlags & DLLFD_FLAG_FDSTR)
		DetourAttach(reinterpret_cast<PVOID*>(&g_pRtlLoadStringTrampoline), MyRtlLoadString);
	if (dwFlags & DLLFD_FLAG_FDPROC)
	{
		DetourAttach(reinterpret_cast<PVOID*>(&g_pGetProcAddressTrampoline), MyGetProcAddress);
		if (g_pGetProcAddressForCallerTrampoline)
			DetourAttach(reinterpret_cast<PVOID*>(&g_pGetProcAddressForCallerTrampoline), MyGetProcAddressForCaller);
	}
	DetourTransactionCommit();
	
	CloseThreadsAndFree(hThreads, ulThreadCount);
}

HMODULE Rollback()
{
	HMODULE hModule = g_hModule;

	DetourTransactionBegin();
	ULONG ulThreadCount;
	HANDLE* hThreads = DetourUpdateAllThreads(&ulThreadCount);

	if (g_pLdrFindResourceTrampoline != LdrFindResource_U)
		DetourDetach(reinterpret_cast<PVOID*>(&g_pLdrFindResourceTrampoline), MyLdrFindResource_U);
	if (g_pLdrAccessResourceTrampoline != LdrAccessResource)
		DetourDetach(reinterpret_cast<PVOID*>(&g_pLdrAccessResourceTrampoline), MyLdrAccessResource);
	if (g_pRtlFindMessageTrampoline != RtlFindMessage)
		DetourDetach(reinterpret_cast<PVOID*>(&g_pRtlFindMessageTrampoline), MyRtlFindMessage);
	if (g_pGetModuleFileNameWTrampoline != reinterpret_cast<decltype(GetModuleFileNameW)*>(GetKernelExport("GetModuleFileNameW")))
		DetourDetach(reinterpret_cast<PVOID*>(&g_pGetModuleFileNameWTrampoline), MyGetModuleFileNameW);
	if (g_pRtlLoadStringTrampoline != RtlLoadString)
		DetourDetach(reinterpret_cast<PVOID*>(&g_pRtlLoadStringTrampoline), MyRtlLoadString);
	if (g_pGetProcAddressTrampoline != reinterpret_cast<decltype(GetProcAddress)*>(GetKernelExport("GetProcAddress")))
		DetourDetach(reinterpret_cast<PVOID*>(&g_pGetProcAddressTrampoline), MyGetProcAddress);
	if (g_pGetProcAddressForCallerTrampoline != reinterpret_cast<decltype(GetProcAddressForCaller)*>(GetProcAddress(GetModuleHandleA("KERNELBASE.dll"), "GetProcAddressForCaller")))
		DetourDetach(reinterpret_cast<PVOID*>(&g_pGetProcAddressForCallerTrampoline), MyGetProcAddressForCaller);
	DetourTransactionCommit();

	CloseThreadsAndFree(hThreads, ulThreadCount);

	g_hModule = nullptr;
	g_hTarget = nullptr;
	return hModule;
}
