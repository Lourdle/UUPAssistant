#pragma once

#include <Windows.h>
#include <Lourdle.UIFramework.h>

#include <vector>
#include <functional>

#include "DiskPartTable.h"

namespace BCD
{
	class BCD;
	class Object;

	class DeviceLocator
	{
		PBYTE pbData;
		DWORD cbData;

		friend class Object;
	public:
		DeviceLocator(
			const _DiskInfo& DiskInfo,
			const _PartInfo& PartInfo,
			bool bGPT,
			PCWSTR pVHDPath = nullptr);
		DeviceLocator() = delete;
		DeviceLocator(const DeviceLocator&) = delete;
		DeviceLocator(DeviceLocator&&) = delete;
		~DeviceLocator();
	};

	class Object
	{
		HKEY hObject;
		friend class BCD;
	public:
		Object(HKEY);
		Object(const Object&) = delete;
		Object(Object&&);
		~Object();

		void Close();

		operator bool() { return hObject != nullptr; }

		Object& operator=(Object&&);

		LSTATUS SetDescription(PCWSTR pDescription);
		LSTATUS SetElement(DWORD dwElement, PVOID pvData, DWORD cbData);
		LSTATUS SetElement(DWORD dwElement, PCWSTR pString, bool MultiString);
		LSTATUS SetPreferedLocale(PCWSTR pLocale);
		LSTATUS SetAppDevice(const DeviceLocator&);
		LSTATUS SetSystemDevice(const DeviceLocator&);
		LSTATUS SetAppPath(PCWSTR pPath);
		LSTATUS SetGraphicsHighestMode(bool bHighestMode);
		bool IsAppObject();
		bool IsResumeObject();
		bool IsSpecificAppDevice(const DeviceLocator&);
		bool IsSpecificSystemDevice(const DeviceLocator&);
		Lourdle::UIFramework::String GetAppPath();
	};

	class BCD
	{
		HKEY hBCD;
		HKEY hTemplate;
		Lourdle::UIFramework::String TemplateKeyName;
	public:
		BCD(HKEY hBCD, PCWSTR pBcdTemplate, DWORD& dwErrorCode, std::function<void(BCD&)> fnNewBcdInitProc);
		BCD(const BCD&) = delete;
		~BCD();

		void Finalize();

		void CreateRecoveryBCD(PCWSTR pPath, PCWSTR pLocale, const DeviceLocator&);
		Object CreateApplicationObject(const GUID& refObjectGUID);
		LSTATUS DeleteApplicationObject(PCWSTR pObjectGUID);
		Object CreateResumeObject(GUID& refAppObjectGUID);
		Object CreateMemTestObject();
		LSTATUS AppendDisplayedObject(const GUID& refObjectGUID);
		LSTATUS SetDefaultObject(const GUID& refObjectGUID);
		LSTATUS SetResumeObject(const GUID& refObjectGUID);
		LSTATUS SetPreferedLocale(PCWSTR pLocale);
		LSTATUS SetAppDevice(const DeviceLocator&);
		LSTATUS SetAppPath(PCWSTR pPath);
		void GetDisplayedObjects(std::vector<Lourdle::UIFramework::String>&);
		Object OpenObject(PCWSTR pGUID);
	};
}
