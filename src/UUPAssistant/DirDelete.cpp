#include "pch.h"
#include "misc.h"

#include <winternl.h>

#undef DeleteFile
#undef GetFileAttributes
#undef SetFileAttributes

extern "C"
{
NTSTATUS
NTAPI
NtSetInformationFile(
	_In_ HANDLE FileHandle,
	_Out_ PIO_STATUS_BLOCK IoStatusBlock,
	_In_reads_bytes_(Length) PVOID FileInformation,
	_In_ ULONG Length,
	_In_ FILE_INFORMATION_CLASS FileInformationClass
);

constexpr FILE_INFORMATION_CLASS FileBasicInformation = static_cast<FILE_INFORMATION_CLASS>(4);

NTSTATUS
NTAPI
NtSetSecurityObject(
	_In_ HANDLE Handle,
	_In_ SECURITY_INFORMATION SecurityInformation,
	_In_ PSECURITY_DESCRIPTOR SecurityDescriptor
);


typedef struct _FILE_BASIC_INFORMATION {
	LARGE_INTEGER CreationTime;
	LARGE_INTEGER LastAccessTime;
	LARGE_INTEGER LastWriteTime;
	LARGE_INTEGER ChangeTime;
	ULONG FileAttributes;
} FILE_BASIC_INFORMATION, * PFILE_BASIC_INFORMATION;

NTSTATUS
NTAPI
NtQueryInformationFile(
	_In_ HANDLE FileHandle,
	_Out_ PIO_STATUS_BLOCK IoStatusBlock,
	_Out_writes_bytes_(Length) PVOID FileInformation,
	_In_ ULONG Length,
	_In_ FILE_INFORMATION_CLASS FileInformationClass
);

typedef struct _FILE_DIRECTORY_INFORMATION {
	ULONG NextEntryOffset;
	ULONG FileIndex;
	LARGE_INTEGER CreationTime;
	LARGE_INTEGER LastAccessTime;
	LARGE_INTEGER LastWriteTime;
	LARGE_INTEGER ChangeTime;
	LARGE_INTEGER EndOfFile;
	LARGE_INTEGER AllocationSize;
	ULONG FileAttributes;
	ULONG FileNameLength;
	_Field_size_bytes_(FileNameLength) WCHAR FileName[1];
} FILE_DIRECTORY_INFORMATION, * PFILE_DIRECTORY_INFORMATION;

NTSTATUS
NTAPI
NtQueryDirectoryFile(
	_In_ HANDLE FileHandle,
	_In_opt_ HANDLE Event,
	_In_opt_ PIO_APC_ROUTINE ApcRoutine,
	_In_opt_ PVOID ApcContext,
	_Out_ PIO_STATUS_BLOCK IoStatusBlock,
	_Out_writes_bytes_(Length) PVOID FileInformation,
	_In_ ULONG Length,
	_In_ FILE_INFORMATION_CLASS FileInformationClass,
	_In_ BOOLEAN ReturnSingleEntry,
	_In_opt_ PUNICODE_STRING FileName,
	_In_ BOOLEAN RestartScan
);

NTSTATUS
NTAPI
NtQueryInformationToken(
	_In_ HANDLE TokenHandle,
	_In_ TOKEN_INFORMATION_CLASS TokenInformationClass,
	_Out_writes_bytes_to_opt_(TokenInformationLength, *ReturnLength) PVOID TokenInformation,
	_In_ ULONG TokenInformationLength,
	_Out_ PULONG ReturnLength
);

ULONG
NTAPI
RtlLengthSid(
	_In_ PSID Sid
);

NTSTATUS
NTAPI
RtlCreateAcl(
	_Out_writes_bytes_(AclLength) PACL Acl,
	_In_ ULONG AclLength,
	_In_ ULONG AclRevision
);

NTSTATUS
NTAPI
RtlAddAccessAllowedAce(
	_Inout_ PACL Acl,
	_In_ ULONG AceRevision,
	_In_ ACCESS_MASK AccessMask,
	_In_ PSID Sid
);

NTSTATUS
NTAPI
RtlCreateSecurityDescriptor(
	_Out_ PSECURITY_DESCRIPTOR SecurityDescriptor,
	_In_ ULONG Revision
);

NTSTATUS
NTAPI
RtlSetOwnerSecurityDescriptor(
	_Inout_ PSECURITY_DESCRIPTOR SecurityDescriptor,
	_In_opt_ PSID Owner,
	_In_ BOOLEAN OwnerDefaulted
);

NTSTATUS
NTAPI
RtlSetDaclSecurityDescriptor(
	_Inout_ PSECURITY_DESCRIPTOR SecurityDescriptor,
	_In_ BOOLEAN DaclPresent,
	_In_opt_ PACL Dacl,
	_In_ BOOLEAN DaclDefaulted
);


#define STATUS_SUCCESS                   ((NTSTATUS)0x00000000L)
#define STATUS_NO_MORE_FILES             ((NTSTATUS)0x80000006L)
#define STATUS_ACCESS_DENIED             ((NTSTATUS)0xC0000022L)
#define STATUS_BUFFER_TOO_SMALL          ((NTSTATUS)0xC0000023L)
#define STATUS_FILE_IS_A_DIRECTORY       ((NTSTATUS)0xC00000BAL)
#define STATUS_CANNOT_DELETE		     ((NTSTATUS)0xC0000121L)
}


static NTSTATUS DeleteFileOrDirectoryInternal(POBJECT_ATTRIBUTES ObjectAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor);


static NTSTATUS SetObjectSecurityAndReOpen(HANDLE& FileHandle, POBJECT_ATTRIBUTES ObjectAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, bool IsDirectory)
{
	IO_STATUS_BLOCK IoStatusBlock;
	const ULONG OpenOption = (IsDirectory ? FILE_DIRECTORY_FILE : FILE_NON_DIRECTORY_FILE) | FILE_OPEN_REPARSE_POINT | FILE_OPEN_FOR_BACKUP_INTENT;
	NTSTATUS Status = NtOpenFile(
		&FileHandle,
		WRITE_OWNER,
		ObjectAttributes,
		&IoStatusBlock,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		OpenOption
	);
	if (!NT_SUCCESS(Status))
		return Status;

	Status = NtSetSecurityObject(
		FileHandle,
		OWNER_SECURITY_INFORMATION,
		SecurityDescriptor
	);
	NtClose(FileHandle);
	if (!NT_SUCCESS(Status))
		return Status;

	Status = NtOpenFile(
		&FileHandle,
		WRITE_DAC,
		ObjectAttributes,
		&IoStatusBlock,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		OpenOption
	);
	if (!NT_SUCCESS(Status))
		return Status;

	Status = NtSetSecurityObject(
		FileHandle,
		DACL_SECURITY_INFORMATION,
		SecurityDescriptor
	);
	NtClose(FileHandle);
	if (!NT_SUCCESS(Status))
		return Status;

	return NtOpenFile(
		&FileHandle,
		DELETE | FILE_WRITE_ATTRIBUTES | FILE_READ_ATTRIBUTES | (IsDirectory ? FILE_LIST_DIRECTORY | SYNCHRONIZE : 0),
		ObjectAttributes,
		&IoStatusBlock,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		OpenOption
	);
}

inline
static ULONG GetFileAttributes(HANDLE FileHandle)
{
	IO_STATUS_BLOCK IoStatusBlock;
	FILE_BASIC_INFORMATION BasicInformation{};
	NTSTATUS Status = NtQueryInformationFile(
		FileHandle,
		&IoStatusBlock,
		&BasicInformation,
		sizeof(BasicInformation),
		FileBasicInformation
	);

	if (NT_SUCCESS(Status))
		return BasicInformation.FileAttributes;
	else
		return 0;
}

static NTSTATUS SetFileAttributes(HANDLE FileHandle, ULONG Attributes)
{
	IO_STATUS_BLOCK IoStatusBlock;
	FILE_BASIC_INFORMATION BasicInformation = { .FileAttributes = Attributes };

	return NtSetInformationFile(
		FileHandle,
		&IoStatusBlock,
		&BasicInformation,
		sizeof(BasicInformation),
		FileBasicInformation
	);
}

static NTSTATUS DeleteDirectoryRecursively(POBJECT_ATTRIBUTES ObjectAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor)
{
	HANDLE FileHandle;
	IO_STATUS_BLOCK IoStatusBlock;
	NTSTATUS Status = NtOpenFile(
		&FileHandle,
		DELETE | SYNCHRONIZE | FILE_LIST_DIRECTORY | FILE_WRITE_ATTRIBUTES | FILE_READ_ATTRIBUTES,
		ObjectAttributes,
		&IoStatusBlock,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		FILE_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT | FILE_OPEN_FOR_BACKUP_INTENT
	);

	if (Status == STATUS_ACCESS_DENIED && SecurityDescriptor)
		Status = SetObjectSecurityAndReOpen(FileHandle, ObjectAttributes, SecurityDescriptor, true);

	if (!NT_SUCCESS(Status))
		return Status;

	constexpr auto BufferSize = 16 * 1024;
	MyUniqueBuffer<PFILE_DIRECTORY_INFORMATION> Buffer = BufferSize;
	NTSTATUS LastFailure = Status;
	PFILE_DIRECTORY_INFORMATION FileDirectoryInfo = nullptr;
	if (!(GetFileAttributes(FileHandle) & FILE_ATTRIBUTE_REPARSE_POINT))
		for (;;)
		{
			Status = NtQueryDirectoryFile(
				FileHandle,
				nullptr,
				nullptr,
				nullptr,
				&IoStatusBlock,
				Buffer,
				BufferSize,
				FileDirectoryInformation,
				FALSE,
				nullptr,
				!FileDirectoryInfo
			);

			if (Status == STATUS_NO_MORE_FILES)
				break;
			else if (!NT_SUCCESS(Status))
			{
				LastFailure = Status;
				break;
			}
			else if (Status == STATUS_PENDING)
			{
				WaitForSingleObject(FileHandle, INFINITE);
				if (IoStatusBlock.Status == STATUS_NO_MORE_FILES)
					break;
				else if (!NT_SUCCESS(IoStatusBlock.Status))
				{
					LastFailure = IoStatusBlock.Status;
					break;
				}
			}

			for (FileDirectoryInfo = Buffer;;)
			{
				if (!(FileDirectoryInfo->FileNameLength == 2 && FileDirectoryInfo->FileName[0] == L'.'
					|| FileDirectoryInfo->FileNameLength == 4 && FileDirectoryInfo->FileName[0] == L'.' && FileDirectoryInfo->FileName[1] == L'.'))
				{
					UNICODE_STRING FileName;
					FileName.Length = static_cast<USHORT>(FileDirectoryInfo->FileNameLength + sizeof(WCHAR) + ObjectAttributes->ObjectName->Length);
					FileName.MaximumLength = FileName.Length;
					FileName.Buffer = new WCHAR[FileName.Length / sizeof(WCHAR)];
					if (FileName.Buffer)
					{
						RtlCopyMemory(FileName.Buffer, ObjectAttributes->ObjectName->Buffer, ObjectAttributes->ObjectName->Length);
						FileName.Buffer[ObjectAttributes->ObjectName->Length / sizeof(WCHAR)] = L'\\';
						RtlCopyMemory(FileName.Buffer + ObjectAttributes->ObjectName->Length / sizeof(WCHAR) + 1, FileDirectoryInfo->FileName, FileDirectoryInfo->FileNameLength);

						OBJECT_ATTRIBUTES InternalObjectAttributes;
						InitializeObjectAttributes(&InternalObjectAttributes, &FileName, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
						Status = DeleteFileOrDirectoryInternal(&InternalObjectAttributes, SecurityDescriptor);

						if (!NT_SUCCESS(Status))
							LastFailure = Status;
						delete[] FileName.Buffer;
					}
				}

				if (FileDirectoryInfo->NextEntryOffset == 0)
					break;
				FileDirectoryInfo = reinterpret_cast<PFILE_DIRECTORY_INFORMATION>(reinterpret_cast<PCHAR>(FileDirectoryInfo) + FileDirectoryInfo->NextEntryOffset);
			}
		}

	if (NT_SUCCESS(LastFailure))
	{
		LastFailure = DeleteFileOnClose(FileHandle);
		if (!NT_SUCCESS(LastFailure))
		{
			LastFailure = SetFileAttributes(FileHandle, FILE_ATTRIBUTE_DIRECTORY);

			if (NT_SUCCESS(LastFailure))
				LastFailure = DeleteFileOnClose(FileHandle);
		}
	}

	NtClose(FileHandle);
	return LastFailure;
}

static NTSTATUS DeleteFileOrDirectoryInternal(POBJECT_ATTRIBUTES ObjectAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor)
{
	HANDLE FileHandle;
	IO_STATUS_BLOCK IoStatusBlock;
	NTSTATUS Status = NtOpenFile(
		&FileHandle,
		DELETE | FILE_WRITE_ATTRIBUTES,
		ObjectAttributes,
		&IoStatusBlock,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		FILE_NON_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT | FILE_OPEN_FOR_BACKUP_INTENT
	);

	if (Status == STATUS_ACCESS_DENIED && SecurityDescriptor)
		Status = SetObjectSecurityAndReOpen(FileHandle, ObjectAttributes, SecurityDescriptor, false);

	if (NT_SUCCESS(Status))
	{
		NTSTATUS Status2 = SetFileAttributes(FileHandle, FILE_ATTRIBUTE_NORMAL);

		if (!NT_SUCCESS(Status2))
			Status = Status2;

		Status2 = DeleteFileOnClose(FileHandle);
		NtClose(FileHandle);

		if (!NT_SUCCESS(Status2))
		{
			constexpr const WCHAR GlobalRootPrefix[] = L"\\\\?\\GLOBALROOT";
			MyUniqueBuffer<PWCHAR> FileName = ObjectAttributes->ObjectName->Length + sizeof(GlobalRootPrefix);
			RtlCopyMemory(FileName, GlobalRootPrefix, sizeof(GlobalRootPrefix) - sizeof(WCHAR));
			RtlCopyMemory(FileName + ARRAYSIZE(GlobalRootPrefix) - 1, ObjectAttributes->ObjectName->Buffer, ObjectAttributes->ObjectName->Length);
			FileName[ObjectAttributes->ObjectName->Length / sizeof(WCHAR) + ARRAYSIZE(GlobalRootPrefix) - 1] = L'\0';
			if (DeleteFileW(FileName))
				Status2 = STATUS_SUCCESS;
		}
		return NT_SUCCESS(Status2) ? Status : Status2;
	}
	else if (Status == STATUS_FILE_IS_A_DIRECTORY)
		return DeleteDirectoryRecursively(ObjectAttributes, SecurityDescriptor);
	else
		return Status;
}

static NTSTATUS DeleteFileOrDirectory(POBJECT_ATTRIBUTES ObjectAttributes, BOOLEAN ForceDelete)
{
	if (ForceDelete)
	{
		HANDLE TokenHandle;
		NTSTATUS Status = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &TokenHandle);
		if (!NT_SUCCESS(Status))
			return Status;

		ULONG TokenUserLength = 0;
		Status = NtQueryInformationToken(TokenHandle, TokenUser, nullptr, 0, &TokenUserLength);
		if (Status != STATUS_BUFFER_TOO_SMALL)
			return Status;

		MyUniqueBuffer<PTOKEN_USER> TokenUserInfo(TokenUserLength);
		if (!TokenUserInfo)
			return STATUS_NO_MEMORY;
		Status = NtQueryInformationToken(TokenHandle, TokenUser, TokenUserInfo.get(), TokenUserLength, &TokenUserLength);
		if (NT_SUCCESS(Status))
		{
			const ULONG AclSize = sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) - sizeof(ACCESS_ALLOWED_ACE::SidStart) + RtlLengthSid(TokenUserInfo->User.Sid);
			MyUniqueBuffer<PACL> Acl(AclSize);
			if (!Acl)
				return STATUS_NO_MEMORY;
			Status = RtlCreateAcl(Acl, AclSize, ACL_REVISION);
			if (NT_SUCCESS(Status))
				Status = RtlAddAccessAllowedAce(Acl, ACL_REVISION, FILE_ALL_ACCESS, TokenUserInfo->User.Sid);
			if (!NT_SUCCESS(Status))
				return Status;

			SECURITY_DESCRIPTOR SecurityDescriptor;
			Status = RtlCreateSecurityDescriptor(&SecurityDescriptor, SECURITY_DESCRIPTOR_REVISION);
			if (NT_SUCCESS(Status))
				Status = RtlSetOwnerSecurityDescriptor(&SecurityDescriptor, TokenUserInfo->User.Sid, FALSE);
			if (NT_SUCCESS(Status))
				Status = RtlSetDaclSecurityDescriptor(&SecurityDescriptor, TRUE, Acl, FALSE);

			if (NT_SUCCESS(Status))
				Status = DeleteFileOrDirectoryInternal(ObjectAttributes, &SecurityDescriptor);
			else
			{
				NTSTATUS Status2 = DeleteFileOrDirectoryInternal(ObjectAttributes, nullptr);
				if (!NT_SUCCESS(Status2))
					Status = Status2;
			}
		}

		return Status;
	}
	else
		return DeleteFileOrDirectoryInternal(ObjectAttributes, nullptr);
}

inline
static UNICODE_STRING GetNtFileName(PCWSTR pPath)
{
	HANDLE hDir = CreateFileW(pPath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (hDir == INVALID_HANDLE_VALUE)
		return {};
	if (!(GetFileAttributes(hDir) & FILE_ATTRIBUTE_DIRECTORY))
	{
		CloseHandle(hDir);
		SetLastError(ERROR_DIRECTORY);
		return {};
	}
	DWORD dwLen = GetFinalPathNameByHandleW(hDir, nullptr, 0, VOLUME_NAME_NT);
	if (dwLen == 0)
	{
		CloseHandle(hDir);
		return {};
	}

	UNICODE_STRING UnicodeString{
		.MaximumLength = static_cast<USHORT>(dwLen * sizeof(WCHAR)),
		.Buffer = new WCHAR[dwLen]
	};
	GetFinalPathNameByHandleW(hDir, UnicodeString.Buffer, dwLen, VOLUME_NAME_NT);
	CloseHandle(hDir);
	UnicodeString.Length = UnicodeString.MaximumLength - sizeof(WCHAR);
	return UnicodeString;
}

static bool DeleteDirectoryInternal(PCWSTR pPath, bool Force)
{
	UNICODE_STRING NtName = GetNtFileName(pPath);
	if (NtName.Buffer == nullptr)
		return false;

	OBJECT_ATTRIBUTES ObjectAttributes;
	InitializeObjectAttributes(&ObjectAttributes, &NtName, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
	NTSTATUS Status = DeleteFileOrDirectory(&ObjectAttributes, Force);
	delete[] NtName.Buffer;
	if (NT_SUCCESS(Status))
		return true;
	else
	{
		SetLastError(RtlNtStatusToDosError(Status));
		return false;
	}
}

bool DeleteDirectory(PCWSTR pPath)
{
	return DeleteDirectoryInternal(pPath, false);
}

bool ForceDeleteDirectory(PCWSTR pPath)
{
	return DeleteDirectoryInternal(pPath, true);
}
