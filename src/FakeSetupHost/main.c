#include <Windows.h>
#include <winternl.h>

NTSYSAPI NTSTATUS NTAPI NtOpenProcess(
    PHANDLE            ProcessHandle,
    ACCESS_MASK        DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    CLIENT_ID*         ClientId
);

NTSYSAPI NTSTATUS NTAPI NtTerminateProcess(
    HANDLE   ProcessHandle,
    NTSTATUS ExitStatus
);

#define NtCurrentProcess() ((HANDLE)(LONG_PTR)-1)


VOID APIENTRY Win32ProcessStartup(PPEB Peb)
{
    UNREFERENCED_PARAMETER(Peb);

    PROCESS_BASIC_INFORMATION ProcessBasicInfo;
    if (NT_SUCCESS(NtQueryInformationProcess(
        NtCurrentProcess(),
        ProcessBasicInformation,
        &ProcessBasicInfo,
        sizeof(ProcessBasicInfo),
        NULL
    )))
    {
        HANDLE ProcessHandle;
        OBJECT_ATTRIBUTES ObjectAttributes = { sizeof(ObjectAttributes) };
        CLIENT_ID ClientId = { ProcessBasicInfo.Reserved3 };
        if (NT_SUCCESS(NtOpenProcess(&ProcessHandle, PROCESS_TERMINATE, &ObjectAttributes, &ClientId)))
        {
            NtTerminateProcess(ProcessHandle,
#include "ExitCode.inc"
            );
            NtClose(ProcessHandle);
        }
    }
    NtTerminateProcess(NtCurrentProcess(), 0);
}
