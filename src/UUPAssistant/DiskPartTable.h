#pragma once

#ifndef DISKPARTTABLE_H
#define DISKPARTTABLE_H
#include <Windows.h>

#pragma pack(push, 1)
struct DiskPartition_t
{
	BYTE State;
	BYTE StartingHead;
	WORD StartingSector : 6;
	WORD StartingCylinder : 10;
	BYTE Flag;
	BYTE EndingHead;
	WORD EndingSector : 6;
	WORD EndingCylinder : 10;
	DWORD StartingLBA;
	DWORD TotalSectors;
};

struct MBR_t
{
	BYTE Bootloader[440];
	DWORD dwSignature;
	WORD wReserved;
	DiskPartition_t Partitions[4];
	BYTE BootableSignarure[2];
};

struct GPTHeader
{
	CHAR Signature[8];
	DWORD dwVersion;
	DWORD cbHeader;
	DWORD HeaderCRCChecksum;
	DWORD dwReserved;
	ULONGLONG ullPrimaryHeaderLBA;
	ULONGLONG ullBackupHeaderLBA;
	ULONGLONG ullPartitionStartingLBA;
	ULONGLONG ullPartitionEndingLBA;
	GUID DiskGUID;
	ULONGLONG ullPartitionTableStartingLBA;
	DWORD nMaxNumberOfPartitions;
	DWORD cbPartitionEntry;
	DWORD TableCRCChecksum;
};

struct PartitionEntry
{
	GUID TypeGUID;
	GUID PartitionGUID;
	ULONGLONG ullStartingLBA;
	ULONGLONG ullEndingLBA;
	ULONGLONG ullAttributes;
	WCHAR Name[36];
};

#pragma pack(pop)


constexpr GUID ESP = { 0xc12a7328, 0xf81f, 0x11d2, { 0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b } };
constexpr GUID BDP = { 0xebd0a0a2, 0xb9e5, 0x4433, { 0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7 } };
constexpr GUID MSReserved = { 0xe3c9e316, 0x0b5c, 0x4db8, { 0x81, 0x7d, 0xf9, 0x2d, 0xf0, 0x02, 0x15, 0xae } };
constexpr GUID MSRecovery = { 0xde94bba4, 0x06d1, 0x4d40, { 0xa1, 0x6a, 0xbf, 0xd5, 0x01, 0x79, 0xd6, 0xac } };


union _PartInfo
{
	GUID id;
	ULONGLONG ullOffset;
};
union _DiskInfo
{
	GUID id;
	DWORD Signature;
};

bool GetDriveInfo(PCWSTR pPath, _PartInfo& PartInfo, _DiskInfo& DiskInfo);

#endif
