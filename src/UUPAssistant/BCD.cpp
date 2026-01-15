#include "pch.h"
#include "BCD.h"
#include "DiskPartTable.h"

#include <aclapi.h>
#include <virtdisk.h>

#include <filesystem>
#include <map>

using namespace Lourdle::UIFramework;


static PWSTR AssignElementKey(WCHAR szSubKey[18], DWORD dwElement)
{
	memcpy(szSubKey, L"Elements\\", 18);
	BYTE n = 0;
	for (DWORD e = dwElement; e != 0; ++n, e /= 16);
	n = 8 - n;
	for (BYTE i = 0; i != n; ++i)
		szSubKey[9 + i] = '0';
	_ultow_s(dwElement, szSubKey + 9 + n, 9 - n, 16);
	return szSubKey;
}

static LSTATUS SetElement(HKEY hKey, DWORD dwElement, DWORD dwType, LPCVOID pData, DWORD cbData)
{
	WCHAR szSubKey[18];
	return RegSetKeyValueW(hKey, AssignElementKey(szSubKey, dwElement), L"Element", dwType, pData, cbData);
}

static LSTATUS GetElement(HKEY hKey, DWORD dwElement, DWORD& dwType, std::vector<BYTE>& data)
{
	WCHAR szSubKey[18];
	DWORD cbData = 0;
	LSTATUS l = RegGetValueW(hKey, AssignElementKey(szSubKey, dwElement), L"Element", RRF_RT_ANY, &dwType, nullptr, &cbData);
	if (l != ERROR_SUCCESS)
		return l;
	data.resize(cbData);
	return RegGetValueW(hKey, szSubKey, L"Element", RRF_RT_ANY, nullptr, data.data(), &cbData);
}

static HKEY CreateObject(HKEY hKey, PCWSTR guid, LSTATUS& lStatus)
{
	HKEY hObject = nullptr;
	DWORD dwDisposition;
	lStatus = RegCreateKeyExW(hKey, L"Objects", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &hKey, nullptr);
	if (lStatus != ERROR_SUCCESS)
		return hObject;
	lStatus = RegCreateKeyExW(hKey, guid, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &hObject, &dwDisposition);
	RegCloseKey(hKey);
	if (lStatus == ERROR_SUCCESS)
		return hObject;
	if (dwDisposition == REG_OPENED_EXISTING_KEY)
		return hObject;
	if (RegCreateKeyExW(hObject, L"Elements", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
		RegCloseKey(hKey);
	if (RegCreateKeyExW(hObject, L"Description", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
		RegCloseKey(hKey);
	return hObject;
}

static HKEY CreateObject(HKEY hKey, PCWSTR guid, DWORD dwType, LSTATUS& lStatus)
{
	HKEY hObject = CreateObject(hKey, guid, lStatus);
	if (lStatus == ERROR_SUCCESS)
		lStatus = RegSetKeyValueW(hObject, L"Description", L"Type", REG_DWORD, &dwType, sizeof(dwType));
	return hObject;
}


namespace BCD
{
	Object::Object(HKEY hKey) : hObject(hKey)
	{
	}

	Object::Object(Object&& right) : hObject(right.hObject)
	{
		right.hObject = nullptr;
	}

	Object::~Object()
	{
		Close();
	}

	void Object::Close()
	{
		if (hObject)
		{
			RegCloseKey(hObject);
			hObject = nullptr;
		}
	}

	Object& Object::operator=(Object&& right)
	{
		Close();
		this->Object::Object(std::move(right));
		return *this;
	}

	LSTATUS Object::SetDescription(PCWSTR pDescription)
	{
		return SetElement(0x12000004, pDescription, false);
	}

	LSTATUS Object::SetElement(DWORD dwElement, PVOID pvData, DWORD cbData)
	{
		return ::SetElement(hObject, dwElement, REG_BINARY, pvData, cbData);
	}

	LSTATUS Object::SetElement(DWORD dwElement, PCWSTR pString, bool MultiString)
	{
		if (MultiString)
		{
			DWORD len = 0;
			while (*reinterpret_cast<const long*>(pString + len) != 0)
				++len;
			return ::SetElement(hObject, dwElement, REG_MULTI_SZ, pString, len * 2 + 4);
		}
		else
			return ::SetElement(hObject, dwElement, REG_SZ, pString, static_cast<DWORD>(wcslen(pString) + 1) * 2);
	}

	LSTATUS Object::SetPreferedLocale(PCWSTR pLocale)
	{
		return SetElement(0x12000005, pLocale, false);
	}


	LSTATUS Object::SetAppDevice(const DeviceLocator& dl)
	{
		return SetElement(0x11000001, dl.pbData, dl.cbData);
	}

	LSTATUS Object::SetSystemDevice(const DeviceLocator& dl)
	{
		return SetElement(0x21000001, dl.pbData, dl.cbData);
	}

	LSTATUS Object::SetAppPath(PCWSTR pPath)
	{
		return SetElement(0x12000002, pPath, false);
	}

	LSTATUS Object::SetGraphicsHighestMode(bool bHighestMode)
	{
		DWORD dwData = bHighestMode;
		return ::SetElement(hObject, 0x16000054, REG_DWORD, &dwData, dwData);
	}

	static bool IsSpecificObject(HKEY hObject, DWORD dwObjType)
	{
		DWORD dwType, cbData = sizeof(DWORD);
		if (RegGetValueW(hObject, L"Description", L"Type", RRF_RT_REG_DWORD, nullptr, &dwType, &cbData) == ERROR_SUCCESS)
			return dwType == dwObjType;
		return false;
	}

	bool Object::IsAppObject()
	{
		return IsSpecificObject(hObject, 0x10200003);
	}

	bool Object::IsResumeObject()
	{
		return IsSpecificObject(hObject, 0x10200004);
	}

	static bool IsSpecificDevice(HKEY hObject, DWORD dwElement, PBYTE pbLocator, DWORD cbLocator)
	{
		DWORD dwType;
		std::vector<BYTE> data;

		if (GetElement(hObject, dwElement, dwType, data) != ERROR_SUCCESS)
			return false;

		if (dwType == REG_BINARY
			&& data.size() == cbLocator
			&& memcmp(pbLocator, data.data(), cbLocator) != 0)
			return false;
		
		if (dwType != REG_BINARY || data.size() != cbLocator)
			return false;
			
		return memcmp(pbLocator, data.data(), cbLocator) == 0;
	}

	bool Object::IsSpecificAppDevice(const DeviceLocator& dl)
	{
		return IsSpecificDevice(hObject, 0x11000001, dl.pbData, dl.cbData);
	}

	bool Object::IsSpecificSystemDevice(const DeviceLocator& dl)
	{
		return IsSpecificDevice(hObject, 0x21000001, dl.pbData, dl.cbData);
	}

	Lourdle::UIFramework::String Object::GetAppPath()
	{
		DWORD dwType;
		std::vector<BYTE> data;

		if (GetElement(hObject, 0x12000002, dwType, data) != ERROR_SUCCESS)
			return Lourdle::UIFramework::String();

		if (data.empty()) return Lourdle::UIFramework::String();

		data.resize(data.size() + 2);
		Lourdle::UIFramework::String Path = reinterpret_cast<PWSTR>(data.data());
		return Path;
	}

	static DWORD SetKeyDaclRecursive(HKEY hKey, PACL pDacl)
	{
		WCHAR szSubKey[39];
		HKEY hSubKey;
		DWORD i = 0;
		DWORD dw = SetSecurityInfo(hKey, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION, nullptr, nullptr, pDacl, nullptr);
		while (RegEnumKeyW(hKey, i, szSubKey, 39) == ERROR_SUCCESS)
		{
			RegOpenKeyExW(hKey, szSubKey, REG_OPTION_BACKUP_RESTORE, WRITE_DAC | WRITE_OWNER | ACCESS_SYSTEM_SECURITY | KEY_ENUMERATE_SUB_KEYS, &hSubKey);
			DWORD dw2 = SetKeyDaclRecursive(hSubKey, pDacl);
			if (dw2 != ERROR_SUCCESS)
				dw = dw2;
			RegCloseKey(hSubKey);
			++i;
		}
		return dw;
	}

	static bool ShouldElementNeedToExpanded(PWSTR pElement)
	{
		switch (wcstoull(pElement, nullptr, 16))
		{
		case 0x15000011:case 0x15000013:case 0x15000014:case 0x250000f3:case 0x250000f4:case 0x250000f5:case 0x250000c2:case 0x25000020:
			return true;
		default:
			return false;
		}
	}

	static LSTATUS CopyTemplateObject(HKEY hTemplate, PCWSTR pGUID, HKEY hTargetKey)
	{
		HKEY hKey, hTemplateObjects, hObject, hElements;
		WCHAR szKey[9];
		DWORD dwType, cbData;
		LSTATUS lStatus = RegCreateKeyExW(hTargetKey, L"Elements", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &hKey, nullptr);
		if (lStatus != ERROR_SUCCESS)
			return lStatus;
		lStatus = RegOpenKeyExW(hTemplate, L"Objects", 0, KEY_READ | READ_CONTROL, &hTemplateObjects);
		if (lStatus != ERROR_SUCCESS)
		{
			RegCloseKey(hKey);
			return lStatus;
		}
		lStatus = RegOpenKeyExW(hTemplateObjects, pGUID, 0, KEY_READ, &hObject);
		if (lStatus != ERROR_SUCCESS)
		{
			RegCloseKey(hKey);
			RegCloseKey(hTemplateObjects);
			return lStatus;
		}
		lStatus = RegOpenKeyExW(hObject, L"Elements", 0, KEY_READ, &hElements);
		if (lStatus != ERROR_SUCCESS)
		{
			RegCloseKey(hKey);
			RegCloseKey(hObject);
			RegCloseKey(hTemplateObjects);
			return lStatus;
		}
		for (DWORD i = 0; RegEnumKeyW(hElements, i, szKey, 9) == ERROR_SUCCESS; ++i, szKey[0] = 0)
			if (szKey[0] != '4')
			{
				HKEY hSrcKey;
				MyUniqueBuffer<PBYTE> pData;
				bool bExpansion;

				for (WCHAR& i : szKey)
					if (i >= 'A' && i <= 'Z')
						i += 'a' - 'A';

				LSTATUS lStatus2 = RegOpenKeyExW(hElements, szKey, 0, KEY_READ, &hSrcKey);
				if (lStatus2 != ERROR_SUCCESS)
				{
					lStatus = lStatus2;
					continue;
				}
				lStatus2 = RegQueryValueExW(hSrcKey, L"Element", nullptr, &dwType, nullptr, &cbData);
				if (lStatus2 != ERROR_SUCCESS)
				{
					lStatus = lStatus2;
					RegCloseKey(hSrcKey);
					continue;
				}
				bExpansion = cbData < 8 && ShouldElementNeedToExpanded(szKey);
				pData.reset(bExpansion ? 8 : cbData);
				lStatus2 = RegQueryValueExW(hSrcKey, L"Element", nullptr, &dwType, pData, &cbData);
				if (lStatus2 != ERROR_SUCCESS)
				{
					RegCloseKey(hSrcKey);
					lStatus = lStatus2;
					continue;
				}
				if (bExpansion)
				{
					ZeroMemory(pData + cbData, 8 - cbData);
					cbData = 8;
				}
				lStatus2 = RegSetKeyValueW(hKey, szKey, L"Element", dwType, pData, cbData);
				if (lStatus2 != ERROR_SUCCESS)
				{
					RegCloseKey(hSrcKey);
					lStatus = lStatus2;
					continue;
				}
				RegCloseKey(hSrcKey);
			}
		cbData = 4;
		if (RegGetValueW(hObject, L"Description", L"Type", RRF_RT_DWORD, nullptr, &dwType, &cbData) == ERROR_SUCCESS)
			RegSetKeyValueW(hTargetKey, L"Description", L"Type", REG_DWORD, &dwType, 4);
		RegCloseKey(hElements);
		RegCloseKey(hObject);
		RegCloseKey(hTemplateObjects);
		RegCloseKey(hKey);
		return lStatus;
	}

	static LSTATUS InitBCD(HKEY hBCD, HKEY hTemplate)
	{
		LSTATUS lStatus = ERROR_SUCCESS;
		PCWSTR pObjects[] = {
			L"{0ce4991b-e6b3-4b16-b23c-5e0d9250e5d9}", L"{1afa9c49-16ab-4a5c-901b-212802da9460}",
			L"{4636856e-540f-4170-a130-a84776f4c654}", L"{5189b25c-5558-4bf2-bca4-289b11bd29e2}",
			L"{6efb52bf-1766-41db-a6b3-0ee5eff72bd7}", L"{7ea2e1ac-2e61-4728-aaa3-896d9d0a9f0e}",
			L"{7ff607e0-4395-11db-b0de-0800200c9a66}", L"{9dea862c-5cdd-4e70-acc1-f32b344d4795}" };
		for (PCWSTR pObject : pObjects)
		{
			LSTATUS lStatus2;
			HKEY hObject = CreateObject(hBCD, pObject, lStatus2);
			if (lStatus2 != ERROR_SUCCESS)
				lStatus = lStatus2;
			else
			{
				lStatus2 = CopyTemplateObject(hTemplate, pObject, hObject);
				RegCloseKey(hObject);
				if (lStatus2 != ERROR_SUCCESS)
					lStatus = lStatus2;
			}
		}
		return ERROR_SUCCESS;
	}

	BCD::BCD(HKEY hBCD, PCWSTR pBcdTemplate, DWORD& dwError, std::function<void(BCD&)> fn) : hBCD(hBCD), hTemplate(nullptr)
	{
		GUID guid;
		CoCreateGuid(&guid);
		TemplateKeyName = GUID2String(guid);

		dwError = RegLoadKeyW(HKEY_LOCAL_MACHINE, TemplateKeyName, pBcdTemplate);
		if (dwError)
			return;

		if ((dwError = RegOpenKeyExW(HKEY_LOCAL_MACHINE, TemplateKeyName, REG_OPTION_BACKUP_RESTORE, KEY_READ | ACCESS_SYSTEM_SECURITY, &hTemplate)) == ERROR_SUCCESS)
			if (RegEnumKeyW(hBCD, 0, nullptr, 0) == ERROR_NO_MORE_ITEMS)
			{
				dwError = InitBCD(hBCD, hTemplate);
				if (dwError != ERROR_SUCCESS)
					return;
				RegSetKeyValueW(hBCD, L"Description", L"KeyName", REG_SZ, L"BCD00000000", 24);
				DWORD dwData = 1;
				RegSetKeyValueW(hBCD, L"Description", L"System", REG_DWORD, &dwData, 4);
				RegSetKeyValueW(hBCD, L"Description", L"TreatAsSystem", REG_DWORD, &dwData, 4);
				fn(*this);
			}
			else
			{
				HANDLE hToken;
				DWORD dwLength;

				OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken);
				GetTokenInformation(hToken, TokenUser, nullptr, 0, &dwLength);
				MyUniqueBuffer<PTOKEN_USER> pUser(dwLength);
				GetTokenInformation(hToken, TokenUser, pUser, dwLength, &dwLength);
				CloseHandle(hToken);

				EXPLICIT_ACCESSW explicitAccess = {
					.grfAccessPermissions = KEY_ALL_ACCESS,
					.grfAccessMode = SET_ACCESS,
					.grfInheritance = CONTAINER_INHERIT_ACE | OBJECT_INHERIT_ACE,
					.Trustee = {
						.TrusteeForm = TRUSTEE_IS_SID,
						.TrusteeType = TRUSTEE_IS_USER,
						.ptstrName = static_cast<LPWSTR>(pUser->User.Sid)
					}
				};
				PACL pDacl;
				SetEntriesInAclW(1, &explicitAccess, nullptr, &pDacl);

				SetKeyDaclRecursive(hBCD, pDacl);
				LocalFree(pDacl);
			}
		else
		{
			if (hBCD)
				hBCD = nullptr;
			if (hTemplate)
			{
				RegCloseKey(hTemplate);
				hTemplate = nullptr;
				RegUnLoadKeyW(HKEY_LOCAL_MACHINE, TemplateKeyName);
			}
		}
	}

	static DWORD SetKeySecurityRecursive(HKEY hKey, PSECURITY_DESCRIPTOR pSecurityDescriptor)
	{
		WCHAR szSubKey[39];
		HKEY hSubKey;
		DWORD i = 0;
		DWORD dw = RegSetKeySecurity(hKey, BACKUP_SECURITY_INFORMATION, pSecurityDescriptor);
		while (RegEnumKeyW(hKey, i, szSubKey, 39) == ERROR_SUCCESS)
		{
			LSTATUS lStatus = RegOpenKeyExW(hKey, szSubKey, REG_OPTION_BACKUP_RESTORE, WRITE_DAC | WRITE_OWNER | ACCESS_SYSTEM_SECURITY | KEY_ENUMERATE_SUB_KEYS, &hSubKey);
			if (lStatus != ERROR_SUCCESS)
				dw = lStatus;
			lStatus = SetKeySecurityRecursive(hSubKey, pSecurityDescriptor);
			if (lStatus != ERROR_SUCCESS)
				dw = lStatus;
			RegCloseKey(hSubKey);
			++i;
		}
		return dw;
	}

	static DWORD ApplySecurityFromTemplate(HKEY hKey, HKEY hTemplate)
	{
		DWORD cb = 0;
		RegGetKeySecurity(hTemplate, BACKUP_SECURITY_INFORMATION, nullptr, &cb);
		MyUniqueBuffer<PSECURITY_DESCRIPTOR> psd = cb;
		RegGetKeySecurity(hTemplate, BACKUP_SECURITY_INFORMATION, psd, &cb);
		DWORD result = SetKeySecurityRecursive(hKey, psd);
		return result;
	}

	void BCD::Finalize()
	{
		if (hBCD)
		{
			ApplySecurityFromTemplate(hBCD, hTemplate);
			RegFlushKey(hBCD);
			RegCloseKey(hTemplate);
			hBCD = nullptr;
			RegUnLoadKeyW(HKEY_LOCAL_MACHINE, TemplateKeyName);
		}
	}

	BCD::~BCD()
	{
		Finalize();
	}

	void BCD::CreateRecoveryBCD(PCWSTR pPath, PCWSTR pLocale, const DeviceLocator& dl)
	{
		if (GetFileAttributesW(pPath) != INVALID_FILE_ATTRIBUTES)
			return;

		if (RegistryHive hKey(pPath, true); hKey)
		{
			if (InitBCD(hKey, hTemplate) != ERROR_SUCCESS)
				return;
			RegSetKeyValueW(hKey, L"Description", L"KeyName", REG_SZ, L"BCD00000001", 24);

			LSTATUS lStatus;
			Object Obj = CreateObject(hKey, L"{9dea862c-5cdd-4e70-acc1-f32b344d4795}", 0x10100002, lStatus);
			Obj.SetAppPath(L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi");
			Obj.SetPreferedLocale(pLocale);
			Obj.SetAppDevice(dl);
			WCHAR szSubKey[18];
			RegDeleteKeyW(Obj.hObject, AssignElementKey(szSubKey, 0x23000003));
			Obj.~Object();
			ApplySecurityFromTemplate(hKey, hTemplate);
			RegFlushKey(hKey);
		}
		else return;

		std::filesystem::path Path = pPath;
		auto bcd = Path.filename().wstring();
		std::error_code ec;
		auto iter = std::filesystem::directory_iterator(Path.parent_path(), ec);
		if (ec) return;
		for (auto& entry : iter)
			if (auto filename = entry.path().filename().wstring();
				filename.find(bcd) == 0 && filename.size() != bcd.size())
				DeleteFileW(entry.path().c_str());
	}

	static void StringToLower(String& str)
	{
		for (WCHAR& i : str)
			if (i >= 'A' && i <= 'Z')
				i += 'a' - 'A';
	}

	Object BCD::CreateApplicationObject(const GUID& guid)
	{
		GUID ResumeObjGUID = guid;
		String GuidString = GUID2String(guid);
		WCHAR unnamed[40];
		StringToLower(GuidString);
		LSTATUS lStatus;
		Object AppObj = CreateObject(hBCD, GuidString, lStatus);
		if (lStatus != ERROR_SUCCESS)
		{
			SetLastError(lStatus);
			return Object(nullptr);
		}
		lStatus = CopyTemplateObject(hTemplate, L"{b012b84d-c47c-4ed5-b722-c0c42163e569}", AppObj.hObject);
		if (lStatus != ERROR_SUCCESS)
		{
			SetLastError(lStatus);
			return Object(nullptr);
		}
		wcscpy_s(unnamed, L"{6efb52bf-1766-41db-a6b3-0ee5eff72bd7}");
		unnamed[39] = 0;
		AppObj.SetElement(0x14000006, unnamed, true);
		--ResumeObjGUID.Data1;
		GuidString = GUID2String(ResumeObjGUID);
		StringToLower(GuidString);
		AppObj.SetElement(0x23000003, GuidString, false);
		AppObj.SetElement(0x22000002, L"\\Windows", false);
		return AppObj;
	}

	static GUID QueryRamDevice(HKEY hKey, DWORD dwElement)
	{
		DWORD dwType;
		std::vector<BYTE> data;
		GUID guid = {};
		if (GetElement(hKey, dwElement, dwType, data) == ERROR_SUCCESS
			&& dwType == REG_BINARY
			&& data.size() > sizeof(guid))
		{
			memcpy(&guid, data.data(), sizeof(guid));
		}
		return guid;
	}

	LSTATUS BCD::DeleteApplicationObject(PCWSTR pObjectGUID)
	{
		DWORD dwType;
		HKEY hObjects, hObject;
		std::wstring Objects;
		size_t pos;
		LSTATUS lStatus;

		lStatus = RegOpenKeyExW(hBCD, L"Objects", 0, KEY_ALL_ACCESS, &hObjects);
		if (lStatus != ERROR_SUCCESS)
			return lStatus;
		lStatus = RegOpenKeyExW(hObjects, pObjectGUID, 0, KEY_ALL_ACCESS, &hObject);
		if (lStatus != ERROR_SUCCESS)
		{
			RegCloseKey(hObjects);
			return lStatus;
		}

		std::vector<BYTE> oldResumeObjData;
		if (GetElement(hObject, 0x23000003, dwType, oldResumeObjData) == ERROR_SUCCESS
			&& dwType == REG_SZ && !oldResumeObjData.empty())
		{
			if (oldResumeObjData.size() % 2 != 0) oldResumeObjData.push_back(0);
			oldResumeObjData.push_back(0); oldResumeObjData.push_back(0);
			PCWSTR pOldResumeObj = reinterpret_cast<PCWSTR>(oldResumeObjData.data());

			HKEY hResumeObj;
			if (RegOpenKeyExW(hObjects, pOldResumeObj, 0, KEY_ALL_ACCESS, &hResumeObj) == ERROR_SUCCESS)
			{
				RegDeleteTreeW(hResumeObj, nullptr);
				RegCloseKey(hResumeObj);
				RegDeleteKeyW(hObjects, pOldResumeObj);
			}
		}

		std::vector<BYTE> recoveryObjectsData;
		if (GetElement(hObject, 0x14000008, dwType, recoveryObjectsData) == ERROR_SUCCESS
			&& dwType == REG_MULTI_SZ && !recoveryObjectsData.empty())
		{
			std::map<String, std::pair<HKEY, DWORD>> RecoveryObjects, RamDevices;

			if (recoveryObjectsData.size() % 2 != 0) recoveryObjectsData.push_back(0);
			recoveryObjectsData.push_back(0); recoveryObjectsData.push_back(0);

			for (PWSTR p = reinterpret_cast<PWSTR>(recoveryObjectsData.data()); *p; p += wcslen(p) + 1)
			{
				HKEY hRecoveryObj;
				if (RegOpenKeyExW(hObjects, p, 0, KEY_ALL_ACCESS, &hRecoveryObj) == ERROR_SUCCESS)
				{
					RecoveryObjects[p] = std::make_pair(hRecoveryObj, 0);
					GUID guid = QueryRamDevice(hRecoveryObj, 0x11000001);
					if (guid != GUID{})
						RamDevices[GUID2String(guid)] = std::make_pair(nullptr, 0);
					guid = QueryRamDevice(hRecoveryObj, 0x21000001);
					if (guid != GUID{})
						RamDevices[GUID2String(guid)] = std::make_pair(nullptr, 0);
				}
			}

			for (auto& [Key, Value] : RamDevices)
				RegOpenKeyExW(hObjects, Key, 0, KEY_ALL_ACCESS, &Value.first);

			auto CheckIsStringInMap = [&](PCWSTR pszGuidString, decltype(RamDevices)& Map, bool bAddRef = false)
				{
					for (auto& [Key, Value] : Map)
						if (Key.CompareCaseInsensitive(pszGuidString))
						{
							if (bAddRef)
								++Value.second;
							return true;
						}
					return false;
				};
			MyUniquePtr<WCHAR> pszSubKey;
			DWORD cbMaxSubKeyLen;
			if (RegQueryInfoKeyW(hObjects, nullptr, nullptr, nullptr, nullptr, &cbMaxSubKeyLen, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
			{
				pszSubKey.reset(++cbMaxSubKeyLen);
				for (DWORD i = 0, cch = cbMaxSubKeyLen; RegEnumKeyExW(hObjects, i, pszSubKey, &cch, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS; ++i, cch = cbMaxSubKeyLen)
				{
					HKEY hObject;
					DWORD dwObjType, cbData = sizeof(DWORD);
					if (RegOpenKeyExW(hObjects, pszSubKey, 0, KEY_ALL_ACCESS, &hObject) != ERROR_SUCCESS)
						continue;
					if (RegGetValueW(hObject, L"Description", L"Type", RRF_RT_DWORD, nullptr, &dwObjType, &cbData) == ERROR_SUCCESS
						&& 0x10200003 == dwObjType)
					{
						DWORD dwType;
						std::vector<BYTE> data;

						if (_wcsicmp(pszSubKey, pObjectGUID) != 0
							&& !CheckIsStringInMap(pszSubKey, RecoveryObjects)
							&& !CheckIsStringInMap(pszSubKey, RamDevices))
						{
							if (GetElement(hObject, 0x14000008, dwType, data) == ERROR_SUCCESS)
							{
								if (dwType == REG_MULTI_SZ && !data.empty()) {
									if (data.size() % 2 != 0) data.push_back(0);
									data.reserve(data.size() + 2);
									for (PCWSTR p = reinterpret_cast<PCWSTR>(data.data()); *p; p += wcslen(p) + 1)
										CheckIsStringInMap(pszSubKey, RecoveryObjects, true);
								}
							}

							GUID guid = QueryRamDevice(hObject, 0x11000001);
							if (guid != GUID{})
								CheckIsStringInMap(GUID2String(guid), RamDevices, true);
							guid = QueryRamDevice(hObject, 0x21000001);
							if (guid != GUID{})
								CheckIsStringInMap(GUID2String(guid), RamDevices, true);
						}
					}
					RegCloseKey(hObject);
				}
			}

			for (const auto& [Key, Value] : RecoveryObjects)
				if (Value.second == 0)
				{
					RegDeleteTreeW(Value.first, nullptr);
					RegCloseKey(Value.first);
					RegDeleteKeyW(hObjects, Key);
				}
				else
					RegCloseKey(Value.first);

			for (const auto& [Key, Value] : RamDevices)
				if (Value.second == 0)
				{
					RegDeleteTreeW(Value.first, nullptr);
					RegCloseKey(Value.first);
					RegDeleteKeyW(hObjects, Key);
				}
				else
					RegCloseKey(Value.first);
		}

		RegDeleteTreeW(hObject, nullptr);
		RegCloseKey(hObject);
		RegDeleteKeyW(hObjects, pObjectGUID);
		RegCloseKey(hObjects);

		DWORD cbData{};
		lStatus = RegGetValueW(hBCD, L"Objects\\{9dea862c-5cdd-4e70-acc1-f32b344d4795}\\Elements\\24000001", L"Element", RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &cbData);
		if (lStatus != ERROR_SUCCESS)
			return lStatus;
		Objects.resize(cbData / 2 - 1);
		RegGetValueW(hBCD, L"Objects\\{9dea862c-5cdd-4e70-acc1-f32b344d4795}\\Elements\\24000001", L"Element", RRF_RT_REG_MULTI_SZ, nullptr, const_cast<PWSTR>(Objects.c_str()), &cbData);
		pos = Objects.find(pObjectGUID);
		if (pos != std::wstring::npos)
		{
			Objects.erase(pos, wcslen(pObjectGUID) + 1);
			RegSetKeyValueW(hBCD, L"Objects\\{9dea862c-5cdd-4e70-acc1-f32b344d4795}\\Elements\\24000001", L"Element", REG_MULTI_SZ, Objects.c_str(), cbData - static_cast<DWORD>(wcslen(pObjectGUID) + 1) * 2);
		}

		return ERROR_SUCCESS;
	}

	Object BCD::CreateResumeObject(GUID& Guid)
	{
		String AppGuidString = GUID2String(Guid);
		--Guid.Data1;
		String GuidString = GUID2String(Guid);
		StringToLower(GuidString);
		LSTATUS lStatus;
		Object Obj = CreateObject(hBCD, GuidString, 0x10200004, lStatus);
		if (lStatus != ERROR_SUCCESS)
		{
			SetLastError(lStatus);
			return Object(nullptr);
		}
		StringToLower(AppGuidString);
		Object AppObj = CreateObject(hBCD, AppGuidString, lStatus);
		if (lStatus != ERROR_SUCCESS)
		{
			SetLastError(lStatus);
			return Object(nullptr);
		}
		CopyTemplateObject(hTemplate, L"{0c334284-9a41-4de1-99b3-a7e87e8ff07e}", Obj.hObject);
		WCHAR unnamed[40];
		wcscpy_s(unnamed, L"{1afa9c49-16ab-4a5c-901b-212802da9460}");
		unnamed[39] = 0;
		lStatus = Obj.SetElement(0x14000006, unnamed, true);
		if (lStatus != ERROR_SUCCESS)
		{
			SetLastError(lStatus);
			return Object(nullptr);
		}
		DWORD dwType;
		ULONGLONG unnamed2;
		std::vector<BYTE> data;
		if (GetElement(AppObj.hObject, 0x25000008, dwType, data) == ERROR_SUCCESS)
		{
			if (dwType == REG_BINARY && data.size() == 1)
			{
				unnamed2 = data[0];
				::SetElement(AppObj.hObject, 0x25000008, REG_BINARY, &unnamed2, sizeof(unnamed2));
			}
		}
		return Obj;
	}

	Object BCD::CreateMemTestObject()
	{
		LSTATUS lStatus;
		Object Obj = CreateObject(hBCD, L"{b2721d73-1db4-4c62-bf78-c548a880142d}", lStatus);
		if (lStatus == ERROR_SUCCESS)
		{
			lStatus = CopyTemplateObject(hTemplate, L"{b2721d73-1db4-4c62-bf78-c548a880142d}", Obj.hObject);
			if (lStatus != ERROR_SUCCESS)
			{
				SetLastError(lStatus);
				return Object(nullptr);
			}
		}
		else
			SetLastError(lStatus);
		return Obj;
	}

	LSTATUS BCD::AppendDisplayedObject(const GUID& refObjectGUID)
	{
		DWORD cbData = 0;
		LSTATUS lStatus = RegGetValueW(hBCD, L"Objects\\{9dea862c-5cdd-4e70-acc1-f32b344d4795}\\Elements\\24000001", L"Element", RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &cbData);
		std::wstring data(cbData == 0 ? 0 : cbData / 2 - 1, '\0');
		if (lStatus != ERROR_FILE_NOT_FOUND)
		{
			HKEY hKey;
			lStatus = RegCreateKeyExW(hBCD, L"Objects\\{9dea862c-5cdd-4e70-acc1-f32b344d4795}\\Elements\\24000001", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &hKey, nullptr);
			if (lStatus != ERROR_SUCCESS)
				return lStatus;
			RegCloseKey(hKey);
		}
		else
			RegGetValueW(hBCD, L"Objects\\{9dea862c-5cdd-4e70-acc1-f32b344d4795}\\Elements\\24000001", L"Element", RRF_RT_REG_MULTI_SZ, nullptr, &data[0], &cbData);
		String GuidString = GUID2String(refObjectGUID);
		StringToLower(GuidString);
		while (data[0] == '\0' && !data.empty())
			data.erase(0, 1);
		if (!data.empty())
		{
			for (size_t i = data.size() - 1; i > 0; --i)
				if (data[i] == '\0' && data[i - 1] == '\0')
					data.erase(i, 1);
			if (data.back() != L'\0')
				data.push_back(L'\0');
		}
		data += GuidString;
		data.push_back(L'\0');
		return RegSetKeyValueW(hBCD, L"Objects\\{9dea862c-5cdd-4e70-acc1-f32b344d4795}\\Elements\\24000001", L"Element", REG_MULTI_SZ, data.data(), static_cast<DWORD>(data.size() + 1) * 2);
	}

	LSTATUS BCD::SetDefaultObject(const GUID& refObjectGUID)
	{
		LSTATUS lStatus;
		Object Obj = CreateObject(hBCD, L"{9dea862c-5cdd-4e70-acc1-f32b344d4795}", 0x10100002, lStatus);
		if (lStatus != ERROR_SUCCESS)
			return lStatus;
		String GuidString = GUID2String(refObjectGUID);
		StringToLower(GuidString);
		return Obj.SetElement(0x23000003, GuidString, false);
	}

	LSTATUS BCD::SetResumeObject(const GUID& refObjectGUID)
	{
		LSTATUS lStatus;
		Object Obj = CreateObject(hBCD, L"{9dea862c-5cdd-4e70-acc1-f32b344d4795}", 0x10100002, lStatus);
		if (lStatus != ERROR_SUCCESS)
			return lStatus;
		String GuidString = GUID2String(refObjectGUID);
		StringToLower(GuidString);
		return Obj.SetElement(0x23000006, GuidString, false);
	}

	LSTATUS BCD::SetPreferedLocale(PCWSTR pLocale)
	{
		LSTATUS lStatus;
		Object Obj = CreateObject(hBCD, L"{9dea862c-5cdd-4e70-acc1-f32b344d4795}", 0x10100002, lStatus);
		if (lStatus != ERROR_SUCCESS)
			return lStatus;
		return Obj.SetPreferedLocale(pLocale);
	}

	LSTATUS BCD::SetAppDevice(const DeviceLocator& dl)
	{
		LSTATUS lStatus;
		Object Obj = CreateObject(hBCD, L"{9dea862c-5cdd-4e70-acc1-f32b344d4795}", 0x10100002, lStatus);
		if (lStatus != ERROR_SUCCESS)
			return lStatus;
		return Obj.SetAppDevice(dl);
	}

	LSTATUS BCD::SetAppPath(PCWSTR pPath)
	{
		LSTATUS lStatus;
		Object Obj = CreateObject(hBCD, L"{9dea862c-5cdd-4e70-acc1-f32b344d4795}", 0x10100002, lStatus);
		if (lStatus != ERROR_SUCCESS)
			return lStatus;
		return Obj.SetAppPath(pPath);
	}

	void BCD::GetDisplayedObjects(std::vector<Lourdle::UIFramework::String>& v)
	{
		DWORD cbData = 0;
		if (RegGetValueW(hBCD, L"Objects\\{9dea862c-5cdd-4e70-acc1-f32b344d4795}\\Elements\\24000001", L"Element", RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &cbData) != ERROR_SUCCESS)
			return;
		MyUniqueBuffer<PWCHAR> p = cbData;
		RegGetValueW(hBCD, L"Objects\\{9dea862c-5cdd-4e70-acc1-f32b344d4795}\\Elements\\24000001", L"Element", RRF_RT_REG_MULTI_SZ, nullptr, p, &cbData);
		for (PWCH i = p; *i != 0; ++i)
		{
			v.push_back(i);
			do ++i;
			while (*i != 0);
		}
	}

	Object BCD::OpenObject(PCWSTR pGUID)
	{
		HKEY hObjects, hObject = nullptr;
		LSTATUS lStatus = RegCreateKeyExW(hBCD, L"Objects", 0, nullptr, REG_OPTION_BACKUP_RESTORE, KEY_ALL_ACCESS, nullptr, &hObjects, nullptr);
		if (lStatus != ERROR_SUCCESS)
			SetLastError(lStatus);
		else
		{
			lStatus = RegCreateKeyExW(hObjects, pGUID, 0, nullptr, REG_OPTION_BACKUP_RESTORE, KEY_ALL_ACCESS, nullptr, &hObject, nullptr);
			RegCloseKey(hObjects);
			if (lStatus != ERROR_SUCCESS)
				SetLastError(lStatus);
		}
		return hObject;
	}

	inline
		static bool GetDevice(PCWSTR pszFilePath, _PartInfo& pi, _DiskInfo& di, MyUniquePtr<WCHAR>& pszFile)
	{
		HANDLE hFile = CreateFileW(pszFilePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
		if (hFile == INVALID_HANDLE_VALUE)
			throw GetLastError();
		DWORD dwLen = GetFinalPathNameByHandleW(hFile, nullptr, 0, VOLUME_NAME_NT);
		if (dwLen == 0)
		{
			CloseHandle(hFile);
			throw GetLastError();
		}
		pszFile.reset(dwLen + 14);
		constexpr size_t GlobalRootLen = ARRAYSIZE(L"\\\\?\\GLOBALROOT") - 1;
		memcpy(pszFile, L"\\\\?\\GLOBALROOT", GlobalRootLen * sizeof(WCHAR));
		GetFinalPathNameByHandleW(hFile, pszFile + 14, dwLen, VOLUME_NAME_NT);
		pszFile[14 + dwLen - GetFinalPathNameByHandleW(hFile, nullptr, 0, VOLUME_NAME_NONE)] = 0;
		CloseHandle(hFile);

		bool bGPT = GetDriveInfo(pszFile, pi, di);
		auto pPath = pszFile + wcslen(pszFile);
		*pPath = '\\';
		for (size_t i = 0;; ++i)
		{
			pszFile[i] = pPath[i];
			if (pszFile[i] == 0)
				break;
		}
		return bGPT;
	}

	static DWORD GetVirtualStorageType(PCWSTR Path)
	{
		HANDLE hDisk;
		VIRTUAL_STORAGE_TYPE storageType = {};
		OPEN_VIRTUAL_DISK_PARAMETERS openParameters = {};
		openParameters.Version = OPEN_VIRTUAL_DISK_VERSION_2;
		openParameters.Version2.GetInfoOnly = TRUE;
		if (OpenVirtualDisk(&storageType, Path, VIRTUAL_DISK_ACCESS_NONE, OPEN_VIRTUAL_DISK_FLAG_NO_PARENTS, &openParameters, &hDisk) != ERROR_SUCCESS)
			return 0;
		GET_VIRTUAL_DISK_INFO diskInfo;
		diskInfo.Version = GET_VIRTUAL_DISK_INFO_VIRTUAL_STORAGE_TYPE;
		ULONG diskInfoSize = sizeof(GET_VIRTUAL_DISK_INFO);
		if (GetVirtualDiskInformation(hDisk, &diskInfoSize, &diskInfo, nullptr) != ERROR_SUCCESS)
		{
			CloseHandle(hDisk);
			return 0;
		}
		CloseHandle(hDisk);
		return diskInfo.VirtualStorageType.DeviceId;
	}


#pragma pack(push, 1)
	struct DriveInfoGPT
	{
		LONGLONG unnamed1[4] = { 0, 0, 0x06, 0x48 };
		GUID PartitionGUID;
		LONGLONG unnamed2 = 0;
		GUID DiskGUID;
		LONGLONG unnamed3[2] = {};
	};

	struct DriveInfoMBR
	{
		DriveInfoMBR() : unnamed2(0) {}

		LONGLONG unnamed1[4] = { 0, 0, 0x06, 0x48 };
		ULONGLONG unnamed2 : 8;
		ULONGLONG OffsetRemainderTimes2 : 8;
		ULONGLONG OffsetDiv128 : 48;
		LONGLONG unnamed3[2] = { 0, 0x0000000100000000 };
		DWORD dwSignature;
		DWORD unnamed4[7] = {};
	};

	struct GPTVHDDriveInfoGPT
	{
		GPTVHDDriveInfoGPT(PCWSTR pVHDPath)
		{
			VirtualDrive.unnamed1[3] = 0xae;
			VirtualDrive.unnamed2 = 6;
			VirtualDrive.unnamed3[1] = 0x76;
			PhysicalDrive.unnamed1[0] = 0x0000000100000005;
			PhysicalDrive.unnamed1[1] = 0x0000000500000062;

			if (GetVirtualStorageType(pVHDPath) == VIRTUAL_STORAGE_TYPE_DEVICE_VHDX)
			{
				VirtualDrive.unnamed1[3] += 2;
				VirtualDrive.unnamed3[1] += 2;
				PhysicalDrive.unnamed1[1] += 2;
			}
		}

		DriveInfoGPT VirtualDrive;
		DriveInfoGPT PhysicalDrive;
		WCHAR szFilePath[];
	};

	struct MBRVHDDriveInfoGPT
	{
		MBRVHDDriveInfoGPT(PCWSTR pVHDPath)
		{
			VirtualDrive.unnamed1[3] = 0xae;
			VirtualDrive.unnamed3[1] = 0x0000000100000006;
			VirtualDrive.unnamed4[5] = 0x76;
			PhysicalDrive.unnamed1[0] = 0x0000000100000005;
			PhysicalDrive.unnamed1[1] = 0x0000000500000062;

			if (GetVirtualStorageType(pVHDPath) == VIRTUAL_STORAGE_TYPE_DEVICE_VHDX)
			{
				VirtualDrive.unnamed1[3] += 2;
				VirtualDrive.unnamed4[5] += 2;
				PhysicalDrive.unnamed1[1] += 2;
			}
		}

		DriveInfoMBR VirtualDrive;
		DriveInfoGPT PhysicalDrive;
		WCHAR szFilePath[];
	};

	struct GPTVHDDriveInfoMBR
	{
		GPTVHDDriveInfoMBR(PCWSTR pVHDPath)
		{
			VirtualDrive.unnamed1[3] = 0xae;
			VirtualDrive.unnamed2 = 6;
			VirtualDrive.unnamed3[1] = 0x76;
			PhysicalDrive.unnamed1[0] = 0x0000000100000005;
			PhysicalDrive.unnamed1[1] = 0x0000000500000062;

			if (GetVirtualStorageType(pVHDPath) == VIRTUAL_STORAGE_TYPE_DEVICE_VHDX)
			{
				VirtualDrive.unnamed1[3] += 2;
				VirtualDrive.unnamed3[1] += 2;
				PhysicalDrive.unnamed1[1] += 2;
			}
		}

		DriveInfoGPT VirtualDrive;
		DriveInfoMBR PhysicalDrive;
		WCHAR szFilePath[];
	};

	struct MBRVHDDriveInfoMBR
	{
		MBRVHDDriveInfoMBR(PCWSTR pVHDPath)
		{
			VirtualDrive.unnamed1[3] = 0xae;
			VirtualDrive.unnamed3[1] = 0x0000000100000006;
			VirtualDrive.unnamed4[5] = 0x76;
			PhysicalDrive.unnamed1[0] = 0x0000000100000005;
			PhysicalDrive.unnamed1[1] = 0x0000000500000062;
			if (GetVirtualStorageType(pVHDPath) == VIRTUAL_STORAGE_TYPE_DEVICE_VHDX)
			{
				VirtualDrive.unnamed1[3] += 2;
				VirtualDrive.unnamed4[5] += 2;
				PhysicalDrive.unnamed1[1] += 2;
			}
		}

		DriveInfoMBR VirtualDrive;
		DriveInfoMBR PhysicalDrive;
		WCHAR szFilePath[];
	};
#pragma pack(pop)


	DeviceLocator::DeviceLocator(
		const _DiskInfo& DiskInfo,
		const _PartInfo& PartInfo,
		bool bGPT,
		PCWSTR pVHDPath) : pbData(nullptr), cbData(0)
	{
		if (!pVHDPath || *pVHDPath == 0)
		{
			if (bGPT)
			{
				DriveInfoGPT dig;
				dig.DiskGUID = DiskInfo.id;
				dig.PartitionGUID = PartInfo.id;

				pbData = new BYTE[sizeof(dig)];
				memcpy(pbData, &dig, sizeof(dig));
				cbData = sizeof(dig);
			}
			else
			{
				DriveInfoMBR dim;
				dim.dwSignature = DiskInfo.Signature;
				dim.OffsetRemainderTimes2 = PartInfo.ullOffset % 128 * 2;
				dim.OffsetDiv128 = PartInfo.ullOffset / 128;

				pbData = new BYTE[sizeof(dim)];
				memcpy(pbData, &dim, sizeof(dim));
				cbData = sizeof(dim);
			}
			return;
		}

		_PartInfo pi;
		_DiskInfo di;
		MyUniquePtr<WCHAR> pFilePath;
		bool bGPT2 = GetDevice(pVHDPath, pi, di, pFilePath);

		cbData = static_cast<DWORD>(wcslen(pFilePath)) * 2 + 2;
		if (bGPT)
			if (bGPT2)
			{
				cbData += sizeof(GPTVHDDriveInfoGPT);
				pbData = new BYTE[cbData];
				GPTVHDDriveInfoGPT* p = reinterpret_cast<GPTVHDDriveInfoGPT*>(pbData);
				p->GPTVHDDriveInfoGPT::GPTVHDDriveInfoGPT(pVHDPath);
				p->VirtualDrive.DiskGUID = DiskInfo.id;
				p->VirtualDrive.PartitionGUID = PartInfo.id;
				p->PhysicalDrive.DiskGUID = di.id;
				p->PhysicalDrive.PartitionGUID = pi.id;
				wcscpy_s(p->szFilePath, (cbData - sizeof(GPTVHDDriveInfoGPT)) / 2, pFilePath);
			}
			else
			{
				cbData += sizeof(GPTVHDDriveInfoMBR);
				pbData = new BYTE[cbData];
				GPTVHDDriveInfoMBR* p = reinterpret_cast<GPTVHDDriveInfoMBR*>(pbData);
				p->GPTVHDDriveInfoMBR::GPTVHDDriveInfoMBR(pVHDPath);
				p->VirtualDrive.DiskGUID = DiskInfo.id;
				p->VirtualDrive.PartitionGUID = PartInfo.id;
				p->PhysicalDrive.dwSignature = di.Signature;
				p->PhysicalDrive.OffsetRemainderTimes2 = pi.ullOffset % 128 * 2;
				p->PhysicalDrive.OffsetDiv128 = pi.ullOffset / 128;
				wcscpy_s(p->szFilePath, (cbData - sizeof(GPTVHDDriveInfoMBR)) / 2, pFilePath);
			}
		else if (bGPT2)
		{
			cbData += sizeof(MBRVHDDriveInfoGPT);
			pbData = new BYTE[cbData];
			MBRVHDDriveInfoGPT* p = reinterpret_cast<MBRVHDDriveInfoGPT*>(pbData);
			p->MBRVHDDriveInfoGPT::MBRVHDDriveInfoGPT(pVHDPath);
			p->VirtualDrive.dwSignature = DiskInfo.Signature;
			p->VirtualDrive.OffsetRemainderTimes2 = PartInfo.ullOffset % 128 * 2;
			p->VirtualDrive.OffsetDiv128 = PartInfo.ullOffset / 128;
			p->PhysicalDrive.DiskGUID = di.id;
			p->PhysicalDrive.PartitionGUID = pi.id;
			wcscpy_s(p->szFilePath, (cbData - sizeof(MBRVHDDriveInfoGPT)) / 2, pFilePath);
		}
		else
		{
			cbData += sizeof(MBRVHDDriveInfoMBR);
			pbData = new BYTE[cbData];
			MBRVHDDriveInfoMBR* p = reinterpret_cast<MBRVHDDriveInfoMBR*>(pbData);
			p->MBRVHDDriveInfoMBR::MBRVHDDriveInfoMBR(pVHDPath);
			p->VirtualDrive.dwSignature = DiskInfo.Signature;
			p->VirtualDrive.OffsetRemainderTimes2 = PartInfo.ullOffset % 128 * 2;
			p->VirtualDrive.OffsetDiv128 = PartInfo.ullOffset / 128;
			p->PhysicalDrive.dwSignature = di.Signature;
			p->PhysicalDrive.OffsetRemainderTimes2 = pi.ullOffset % 128 * 2;
			p->PhysicalDrive.OffsetDiv128 = pi.ullOffset / 128;
			wcscpy_s(p->szFilePath, (cbData - sizeof(MBRVHDDriveInfoMBR)) / 2, pFilePath);
		}
	}

	DeviceLocator::~DeviceLocator()
	{
		delete[] pbData;
	}
}
