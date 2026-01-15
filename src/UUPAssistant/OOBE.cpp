#include "pch.h"
#include "UUPAssistant.h"
#include "Resources/resource.h"
#include "Xml.h"

using namespace std;
using namespace Lourdle::UIFramework;

SettingOOBEDlg::SettingOOBEDlg(WindowBase* p) : Dialog(p, GetFontSize() * 40, GetFontSize() * 38, WS_SYSMENU | WS_CAPTION | WS_BORDER | DS_MODALFRAME | DS_FIXEDSYS, GetString(String_OOBESettings)),
SkipOOBE(this, &SettingOOBEDlg::SkipMachineOOBE, ButtonStyle::Checkbox), EnableAdministrator(this, &SettingOOBEDlg::EnableAdministratorAccount, ButtonStyle::AutoCheckbox),
AdministratorPassword(this, 0, DWORD(WS_BORDER | WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_PASSWORD)), SkipEULA(this, 0, ButtonStyle::AutoCheckbox), SkipNetworkSetup(this, 0, ButtonStyle::AutoCheckbox),
HideOnlineAccountScreens(this, 0, ButtonStyle::AutoCheckbox), ComputerName(this, 0), RegisteredOrganization(this, 0), RegisteredOwner(this, 0),
UserAccounts(this, 0, WS_CHILD | WS_BORDER | WS_VISIBLE | LVS_SINGLESEL), AddUser(this, &SettingOOBEDlg::CreateUser), RemoveUser(this, &SettingOOBEDlg::DeleteUser),
EnableAutoLogon(this, &SettingOOBEDlg::SetAutoLogon, ButtonStyle::Checkbox), AutoLogonUser(this, 0), AutoLogonOnce(this, 0, ButtonStyle::AutoCheckbox)
{
	Settings.AutoLogon = -1;
	Settings.AutoLogonOnce = true;
}

void SettingOOBEDlg::Init(LPARAM)
{
	CenterWindow(Parent);
	int pxUnit = GetFontSize();
	SkipOOBE.SetWindowText(GetString(String_SkipOOBE));
	EnableAdministrator.SetWindowText(GetString(String_EnableAdministrator));
	SkipEULA.SetWindowText(GetString(String_SkipEULA));
	SkipNetworkSetup.SetWindowText(GetString(String_SkipNetworkSetup));
	HideOnlineAccountScreens.SetWindowText(GetString(String_HideOnlineAccountScreens));
	AddUser.SetWindowText(GetString(String_AddUser));
	RemoveUser.SetWindowText(GetString(String_RemoveUser));
	AutoLogonOnce.SetWindowText(GetString(String_AutoLogonOnce));
	EnableAutoLogon.SetWindowText(GetString(String_EnableAutoLogon));
	UserAccounts.InsertColumn([pxUnit]()
		{
			String UserName = GetString(String_UserName);
			UserName.end()[-1] = 0;
			return UserName;
		}().GetPointer(), pxUnit * 6, 0);
	UserAccounts.InsertColumn(GetString(String_DisplayName).GetPointer(), pxUnit * 9, 1);
	UserAccounts.InsertColumn(GetString(String_Group).GetPointer(), pxUnit * 9, 2);
	UserAccounts.InsertColumn(GetString(String_Description).GetPointer(), pxUnit * 12, 3);
	for (auto& i : Settings.Users)
	{
		int index = UserAccounts.InsertItem();
		UserAccounts.SetItemText(index, 0, i.Name.GetPointer());
		UserAccounts.SetItemText(index, 1, i.DisplayName.GetPointer());
		UserAccounts.SetItemText(index, 2, i.Group.GetPointer());
		UserAccounts.SetItemText(index, 3, i.Description.GetPointer());
	}
	RegisteredOrganization.SetLimitText(256);
	RegisteredOwner.SetLimitText(256);
	ComputerName.SetLimitText(15);

	SkipOOBE.SetCheck(Settings.SkipOOBE);
	if (Settings.SkipOOBE)
	{
		SkipEULA.EnableWindow(false);
		SkipNetworkSetup.EnableWindow(false);
		HideOnlineAccountScreens.EnableWindow(false);
	}
	if (Settings.AutoLogon == -2)
	{
		EnableAdministrator.SetCheck(BST_CHECKED);
		AdministratorPassword.SetWindowText(Settings.AdministratorPassword);
		EnableAdministratorAccount();
	}
	else
	{
		AdministratorPassword.EnableWindow(false);
		if (Settings.AutoLogon == -1)
		{
			AutoLogonUser.EnableWindow(false);
			AutoLogonOnce.EnableWindow(false);
		}
		else
		{
			EnableAutoLogon.SetCheck(BST_CHECKED);
			for (const auto& i : Settings.Users)
				if (!i.LogonSetPassword)
					AutoLogonUser.AddString(i.Name);
			AutoLogonUser.SetCurSel(Settings.AutoLogon);
		}
	}
	SkipEULA.SetCheck(Settings.HideEULA);
	SkipNetworkSetup.SetCheck(Settings.SkipNetwork);
	HideOnlineAccountScreens.SetCheck(Settings.HideOnlineAccountScreens);
	AutoLogonOnce.SetCheck(Settings.AutoLogonOnce);
	RemoveUser.EnableWindow(false);
	RegisteredOrganization.SetWindowText(Settings.RegisteredOrganization);
	RegisteredOwner.SetWindowText(Settings.RegisteredOwner);
	ComputerName.SetWindowText(Settings.ComputerName);

	SIZE size;
	SkipOOBE.GetIdealSize(&size);
	SkipOOBE.MoveWindow(pxUnit, pxUnit, size.cx, size.cy);
	SkipEULA.GetIdealSize(&size);
	SkipEULA.MoveWindow(pxUnit, pxUnit * 4, size.cx, size.cy);
	SkipNetworkSetup.GetIdealSize(&size);
	SkipNetworkSetup.MoveWindow(pxUnit, pxUnit * 4 + size.cy, size.cx, size.cy);
	HideOnlineAccountScreens.GetIdealSize(&size);
	HideOnlineAccountScreens.MoveWindow(pxUnit, pxUnit * 4 + size.cy * 2, size.cx, size.cy);
	ComputerName.MoveWindow(pxUnit * 11, pxUnit * 10, pxUnit * 8, pxUnit * 2);
	RegisteredOwner.MoveWindow(pxUnit * 11, pxUnit * 14, pxUnit * 8, pxUnit * 2);
	RegisteredOrganization.MoveWindow(pxUnit * 11, pxUnit * 17, pxUnit * 8, pxUnit * 2);
	EnableAdministrator.GetIdealSize(&size);
	EnableAdministrator.MoveWindow(pxUnit * 21, pxUnit, size.cx, size.cy);
	AdministratorPassword.MoveWindow(pxUnit * 28, pxUnit * 4, pxUnit * 8, pxUnit * 2);
	EnableAutoLogon.GetIdealSize(&size);
	EnableAutoLogon.MoveWindow(pxUnit * 21, pxUnit * 9, size.cx, size.cy);
	AutoLogonUser.MoveWindow(pxUnit * 21, pxUnit * 13, pxUnit * 14, 1);
	AutoLogonOnce.GetIdealSize(&size);
	AutoLogonOnce.MoveWindow(pxUnit * 21, pxUnit * 16, size.cx, size.cy);
	AddUser.MoveWindow(pxUnit, pxUnit * 35, pxUnit * 6, pxUnit * 2);
	RemoveUser.MoveWindow(pxUnit * 8, pxUnit * 35, pxUnit * 7, pxUnit * 2);
	UserAccounts.MoveWindow(pxUnit, pxUnit * 26, pxUnit * 38, pxUnit * 8);
}

void SettingOOBEDlg::OnDraw(HDC hdc, RECT rect)
{
	rect.left = GetFontSize();
	rect.right -= rect.left;
	rect.top = rect.left * 22;
	DrawText(hdc, String_AddUsers, &rect, DT_WORDBREAK);
	rect.top = rect.left * 10;
	rect.bottom = rect.left * 12;
	DrawText(hdc, String_ComputerName, &rect, DT_VCENTER | DT_SINGLELINE);
	rect += rect.left * 4;
	DrawText(hdc, String_RegisteredOwner, &rect, DT_VCENTER | DT_SINGLELINE);
	rect += rect.left * 3;
	DrawText(hdc, String_RegisteredOrganization, &rect, DT_VCENTER | DT_SINGLELINE);
	rect -= rect.left * 13;
	rect >>= rect.left * 20;
	if (EnableAdministrator.GetCheck() == BST_UNCHECKED)
		SetTextColor(hdc, RGB(0x6D, 0x6D, 0x6D));
	DrawText(hdc, String_AdministratorPassword, &rect, DT_VCENTER | DT_SINGLELINE);
	SetTextColor(hdc, DarkTextColor);
	rect += GetFontSize() * 7;
	DrawText(hdc, String_User, &rect, DT_SINGLELINE | DT_VCENTER);
}

LRESULT SettingOOBEDlg::OnNotify(LPNMHDR lpnmhdr)
{
	if (lpnmhdr->hwndFrom == UserAccounts && lpnmhdr->code == LVN_ITEMCHANGED)
	{
		int sel = reinterpret_cast<LPNMLISTVIEW>(lpnmhdr)->iItem;
		if (sel == CTL_ERR)
			RemoveUser.EnableWindow(false);
		if (reinterpret_cast<LPNMLISTVIEW>(lpnmhdr)->uNewState & LVIS_SELECTED)
			RemoveUser.EnableWindow(true);
	}

	return 0;
}

void SettingOOBEDlg::OnCancel()
{
	OnOK();
}


static String Base64Encode(LPCVOID pvData, size_t cbData)
{
	constexpr const char* base64_chars =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789+/";

	auto data = reinterpret_cast<LPCBYTE>(pvData);
	std::wstring out;
	BYTE i = 0;
	BYTE array3[3];
	BYTE array4[4];

	do
	{
		array3[i++] = *(data++);
		if (i == 3)
		{
			array4[0] = (array3[0] & 0xfc) >> 2;
			array4[1] = ((array3[0] & 0x03) << 4) + ((array3[1] & 0xf0) >> 4);
			array4[2] = ((array3[1] & 0x0f) << 2) + ((array3[2] & 0xc0) >> 6);
			array4[3] = array3[2] & 0x3f;

			for (i = 0; i < 4; ++i)
				out += base64_chars[array4[i]];
			i = 0;
		}
	} while (--cbData);

	if (i)
	{
		for (int j = i; j < 3; ++j)
			array3[j] = '\0';

		array4[0] = (array3[0] & 0xfc) >> 2;
		array4[1] = ((array3[0] & 0x03) << 4) + ((array3[1] & 0xf0) >> 4);
		array4[2] = ((array3[1] & 0x0f) << 2) + ((array3[2] & 0xc0) >> 6);
		array4[3] = array3[2] & 0x3f;

		for (int j = 0; j < i + 1; ++j)
			out += base64_chars[array4[j]];
		while ((i++ < 3))
			out += '=';
	}

	return out;
}

void SettingOOBEDlg::OnOK()
{
	Settings.SkipOOBE = SkipOOBE.GetCheck();
	Settings.HideEULA = SkipEULA.GetCheck();
	Settings.SkipNetwork = SkipNetworkSetup.GetCheck();
	Settings.HideOnlineAccountScreens = HideOnlineAccountScreens.GetCheck();
	Settings.AutoLogonOnce = AutoLogonOnce.GetCheck();
	if (EnableAdministrator.GetCheck())
	{
		Settings.AutoLogon = -2;
		Settings.AdministratorPassword = AdministratorPassword.GetWindowText();
	}
	else
	{
		Settings.AdministratorPassword[0] = 0;
		Settings.AutoLogon = AutoLogonUser.GetCurSel();
	}
	String ComputerName = this->ComputerName.GetWindowText();
	if (WideCharToMultiByte(CP_ACP, 0, ComputerName, -1, nullptr, 0, nullptr, nullptr) > 16)
		ComputerName[0] = '{';
	for (WCHAR i : ComputerName)
		switch (i)
		{
		case '{': case ' | ': case '}': case '~': case '[': case ']': case '^': case '\'': case ':': case ';': case '<': case '=': case '>': case '?': case '@': case '!': case '"': case '#': case '$': case '%': case '`': case '(': case ')': case '+': case '/': case '.': case ',': case '*': case '&': case ' ':
			MessageBox(GetString(String_InvalidComputerName), GetString(String_Notice), MB_ICONWARNING);
			return;
		}
	Settings.ComputerName = move(ComputerName);
	Settings.RegisteredOrganization = RegisteredOrganization.GetWindowText();
	Settings.RegisteredOwner = RegisteredOwner.GetWindowText();
	EndDialog(0);
}

void SettingOOBEDlg::EnableAdministratorAccount()
{
	if (EnableAdministrator.GetCheck() == BST_CHECKED)
	{
		EnableAutoLogon.SetCheck(BST_CHECKED);
		EnableAutoLogon.EnableWindow(false);
		AdministratorPassword.EnableWindow(true);
		AutoLogonUser.InsertString(L"Administrator", 0);
		AutoLogonUser.SetCurSel(0);
		AutoLogonUser.EnableWindow(false);
		AutoLogonOnce.EnableWindow(true);
	}
	else
	{
		EnableAutoLogon.EnableWindow(true);
		SetAutoLogon();
		AdministratorPassword.EnableWindow(false);
		if (SkipOOBE.GetCheck() && Settings.Users.empty())
			SkipMachineOOBE();
	}
	Invalidate(false);
}

void SettingOOBEDlg::SkipMachineOOBE()
{
	if (SkipOOBE.GetCheck() == BST_UNCHECKED)
		if (Settings.Users.empty() && EnableAdministrator.GetCheck() == BST_UNCHECKED)
			MessageBox(GetString(String_NoUsers), GetString(String_Notice), MB_ICONWARNING);
		else
		{
			SkipOOBE.SetCheck(BST_CHECKED);
			SkipEULA.SetCheck(BST_CHECKED);
			SkipEULA.EnableWindow(false);
			SkipNetworkSetup.SetCheck(BST_CHECKED);
			SkipNetworkSetup.EnableWindow(false);
			HideOnlineAccountScreens.SetCheck(BST_CHECKED);
			HideOnlineAccountScreens.EnableWindow(false);
		}
	else
	{
		SkipOOBE.SetCheck(BST_UNCHECKED);
		SkipEULA.EnableWindow(TRUE);
		SkipNetworkSetup.EnableWindow(TRUE);
		HideOnlineAccountScreens.EnableWindow(TRUE);
	}
}

void SettingOOBEDlg::CreateUser()
{
	struct UserDlg : Dialog
	{
		UserDlg(WindowBase* p, decltype(OOBESettingsStruct::Users)& settings)
			: Dialog(p, GetFontSize() * 20, GetFontSize() * 34, WS_SYSMENU | WS_CAPTION | WS_BORDER | DS_MODALFRAME, GetString(String_AddUser)),
			Settings(settings), UserName(this, 0), Password(this, 0, DWORD(WS_BORDER | WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_PASSWORD)), Password2(this, 0, DWORD(WS_BORDER | WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_PASSWORD)),
			DisplayName(this, 0), Description(this, 0, DWORD(WS_BORDER | WS_VISIBLE | WS_CHILD | ES_AUTOVSCROLL | ES_MULTILINE)), Type(this, 0), Okay(this, IDOK),
			LogonSetPassword(this, &UserDlg::LogonSetPsd, ButtonStyle::AutoCheckbox)
		{
		}

		decltype(OOBESettingsStruct::Users)& Settings;
		Edit UserName;
		Edit Password;
		Edit Password2;
		Edit DisplayName;
		Edit Description;
		Button LogonSetPassword;
		Button Okay;
		ComboBox Type;

		void Init(LPARAM)
		{
			int pxUnit = GetFontSize();
			CenterWindow(Parent);
			Type.AddString(GetString(String_Administrator));
			Type.AddString(GetString(String_StandardUser));
			Type.SetCurSel(0);
			Okay.SetWindowText(GetString(String_Okay));
			UserName.SetLimitText(20);
			Password.SetLimitText(128);
			Password2.SetLimitText(128);
			DisplayName.SetLimitText(256);
			Description.SetLimitText(156);
			LogonSetPassword.SetWindowText(GetString(String_LogonSetPassword));
			SIZE size;
			LogonSetPassword.GetIdealSize(&size);

			UserName.MoveWindow(pxUnit, pxUnit * 2, pxUnit * 18, pxUnit * 2);
			DisplayName.MoveWindow(pxUnit, pxUnit * 6, pxUnit * 18, pxUnit * 2);
			Description.MoveWindow(pxUnit, pxUnit * 10, pxUnit * 18, pxUnit * 5);
			LogonSetPassword.MoveWindow(pxUnit, pxUnit * 18 - size.cy - pxUnit / 2, size.cx, size.cy);
			Password.MoveWindow(pxUnit, pxUnit * 19, pxUnit * 18, pxUnit * 2);
			Password2.MoveWindow(pxUnit, pxUnit * 23, pxUnit * 18, pxUnit * 2);
			Type.MoveWindow(pxUnit, pxUnit * 27, pxUnit * 18, 1);
			Okay.MoveWindow(pxUnit * 7, pxUnit * 31, pxUnit * 6, pxUnit * 2);
		}

		void LogonSetPsd()
		{
			if (LogonSetPassword.GetCheck())
			{
				Password.EnableWindow(false);
				Password2.EnableWindow(false);
			}
			else
			{
				Password.EnableWindow(true);
				Password2.EnableWindow(true);
			}
			Invalidate(false);
		}

		void OnDraw(HDC hdc, RECT rect)
		{
			rect.left = GetFontSize();
			rect.top = rect.left / 2;
			rect.bottom = rect.left * 2;
			DrawText(hdc, String_UserName, &rect, DT_SINGLELINE | DT_VCENTER);
			rect += 4 * rect.left;
			DrawText(hdc, String_DisplayName, &rect, DT_SINGLELINE | DT_VCENTER);
			rect += 4 * rect.left;
			DrawText(hdc, String_Description, &rect, DT_SINGLELINE | DT_VCENTER);
			rect += 9 * rect.left;
			if (LogonSetPassword.GetCheck() == BST_CHECKED)
				SetTextColor(hdc, RGB(0x6D, 0x6D, 0x6D));
			DrawText(hdc, String_Password, &rect, DT_SINGLELINE | DT_VCENTER);
			rect += 4 * rect.left;
			DrawText(hdc, String_RetypePassword, &rect, DT_SINGLELINE | DT_VCENTER);
			SetTextColor(hdc, DarkTextColor);
			rect += 4 * rect.left;
			DrawText(hdc, String_UserAccountType, &rect, DT_SINGLELINE | DT_VCENTER);
		}

		void OnOK()
		{
			String Name = UserName.GetWindowText();
			if (Name.Empty())
			{
				MessageBox(GetString(String_InvalidUserName), GetString(String_Notice), MB_ICONERROR);
				return;
			}
			for (WCHAR i : Name)
				switch (i)
				{
				case '/': case '\\': case '[': case ']': case ':': case '|': case '<': case '>': case '+': case '=': case ';': case ',': case '?': case '*': case '%': case '.':
					MessageBox(GetString(String_InvalidUserName), GetString(String_Notice), MB_ICONERROR);
					return;
				default:
					continue;
				}

			String Password;
			if (LogonSetPassword.GetCheck() == BST_UNCHECKED)
			{
				Password = this->Password.GetWindowText();
				if (!(Password == Password2.GetWindowText()))
				{
					MessageBox(GetString(String_DifferentPasswords), GetString(String_Notice), MB_ICONWARNING);
					return;
				}

				Password += L"Password";
				Password = Base64Encode(Password.GetPointer(), Password.GetLength() * sizeof(WCHAR));
			}
			for (const auto& i : Settings)
				if (i.Name.CompareCaseInsensitive(Name))
				{
					MessageBox(GetString(String_UserExisting), GetString(String_Notice), MB_ICONWARNING);
					return;
				}
			Settings.push_back({ move(Name),  Type.GetCurSel() == 1 ? String(L"Users") : String(L"Administrators"), LogonSetPassword.GetCheck() == BST_CHECKED ? String() : move(Password), Description.GetWindowText(), DisplayName.GetWindowText(), LogonSetPassword.GetCheck() == BST_CHECKED });
			EndDialog(1);
		}
	};
	UserDlg User(this, Settings.Users);
	if (User.ModalDialogBox(0) == IDOK)
	{
		int i = UserAccounts.InsertItem();
		auto& UserInfo = *User.Settings.rbegin();
		UserAccounts.SetItemText(i, 0, UserInfo.Name.GetPointer());
		UserAccounts.SetItemText(i, 1, UserInfo.DisplayName.GetPointer());
		UserAccounts.SetItemText(i, 2, UserInfo.Group.GetPointer());
		UserAccounts.SetItemText(i, 3, UserInfo.Description.GetPointer());
		if (EnableAutoLogon.GetCheck() == BST_CHECKED && !UserInfo.LogonSetPassword)
			AutoLogonUser.AddString(UserInfo.Name);
	}
}

void SettingOOBEDlg::DeleteUser()
{
	int sel = UserAccounts.GetSelectionMark();
	auto& v = Settings.Users;
	if (EnableAdministrator.GetCheck() == BST_UNCHECKED && EnableAutoLogon.GetCheck() == BST_CHECKED)
		if (AutoLogonUser.GetWindowText() == UserAccounts.GetItemText(sel, 0))
		{
			AutoLogonUser.DeleteString(AutoLogonUser.GetCurSel());
			AutoLogonUser.SetCurSel(0);
		}
		else if (!v[sel].LogonSetPassword)
			for (int i = 0, j = 0;; ++i)
				if (j == sel)
				{
					if (AutoLogonUser.DeleteString(i) != CTL_ERR)
						AutoLogonUser.SetCurSel(0);
					break;
				}
				else if (!v[i].LogonSetPassword)
					++j;
	v.erase(sel + v.cbegin());
	UserAccounts.DeleteItem(sel);
	RemoveUser.EnableWindow(false);
	if (EnableAdministrator.GetCheck() == BST_UNCHECKED && EnableAutoLogon.GetCheck() == BST_CHECKED)
	{
		for (const auto& i : v)
			if (!i.LogonSetPassword)
				return;
		if (SkipOOBE.GetCheck() == BST_CHECKED && v.empty())
			SkipMachineOOBE();
		SetAutoLogon();
	}
}

void SettingOOBEDlg::SetAutoLogon()
{
	if (EnableAutoLogon.GetCheck() == BST_CHECKED)
	{
		EnableAutoLogon.SetCheck(BST_UNCHECKED);
		AutoLogonUser.EnableWindow(false);

		while (AutoLogonUser.DeleteString(0) != CTL_ERR);
		AutoLogonUser.SetCurSel(-1);
		AutoLogonOnce.EnableWindow(false);
	}
	else
	{
		const auto& Users = Settings.Users;
		for (const auto& i : Users)
			if (!i.LogonSetPassword)
				goto SetComboBox;

		MessageBox(GetString(String_NoUserToLogon), GetString(String_Notice), MB_ICONWARNING);
		return;

	SetComboBox:
		EnableAutoLogon.SetCheck(BST_CHECKED);
		for (const auto& i : Users)
			if (!i.LogonSetPassword)
				AutoLogonUser.AddString(i.Name);
		AutoLogonUser.EnableWindow(true);
		AutoLogonUser.SetCurSel(0);
		AutoLogonOnce.EnableWindow(true);
	}
}


static rapidxml::xml_node<>* NewComponentNode(rapidxml::xml_document<>& doc, WORD arch)
{
	auto component = doc.allocate_node(rapidxml::node_element, "component");
	component->append_attribute(doc.allocate_attribute("name", "Microsoft-Windows-Shell-Setup"));
	component->append_attribute(doc.allocate_attribute("publicKeyToken", "31bf3856ad364e35"));
	component->append_attribute(doc.allocate_attribute("language", "neutral"));
	component->append_attribute(doc.allocate_attribute("versionScope", "nonSxS"));

	switch (arch)
	{
	case PROCESSOR_ARCHITECTURE_AMD64:
		component->append_attribute(doc.allocate_attribute("processorArchitecture", "amd64"));
		break;
	case PROCESSOR_ARCHITECTURE_INTEL:
		component->append_attribute(doc.allocate_attribute("processorArchitecture", "x86"));
		break;
	case PROCESSOR_ARCHITECTURE_ARM64:
		component->append_attribute(doc.allocate_attribute("processorArchitecture", "arm64"));
		break;
	}
	return component;
}

static void SetNodeValue(rapidxml::xml_document<>& doc, rapidxml::xml_node<>* node, PCWSTR value)
{
	int len = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
	MyUniquePtr<CHAR> Value = len;
	WideCharToMultiByte(CP_UTF8, 0, value, -1, Value, len, nullptr, nullptr);
	node->value(doc.allocate_string(Value));
}

string SettingOOBEDlg::OOBESettingsStruct::ToUnattendXml(const SessionContext& ctx)
{
	string Unattend;
	if (AutoLogon != -1
		|| !ComputerName.Empty()
		|| HideEULA
		|| HideOnlineAccountScreens
		|| SkipNetwork
		|| SkipOOBE
		|| !RegisteredOrganization.Empty()
		|| !RegisteredOwner.Empty()
		|| !Users.empty())
	{
		rapidxml::xml_document<> doc;
		auto node = doc.allocate_node(rapidxml::node_declaration);
		node->append_attribute(doc.allocate_attribute("version", "1.0"));
		node->append_attribute(doc.allocate_attribute("encoding", "utf-8"));
		doc.append_node(node);

		auto root = doc.allocate_node(rapidxml::node_element, "unattend");
		root->append_attribute(doc.allocate_attribute("xmlns", "urn:schemas-microsoft-com:unattend"));
		root->append_attribute(doc.allocate_attribute("xmlns:wcm", "http://schemas.microsoft.com/WMIConfig/2002/State"));

		node = doc.allocate_node(rapidxml::node_element, "settings");

		if (!ComputerName.Empty())
		{
			node->append_attribute(doc.allocate_attribute("pass", "specialize"));
			auto component = NewComponentNode(doc, ctx.TargetImageInfo.Arch);
			auto ComputerName = doc.allocate_node(rapidxml::node_element, "ComputerName");
			SetNodeValue(doc, ComputerName, this->ComputerName);
			component->append_node(ComputerName);
			node->append_node(component);

			root->append_node(node);
			node = doc.allocate_node(rapidxml::node_element, "settings");
		}

		node->append_attribute(doc.allocate_attribute("pass", "oobeSystem"));
		auto component = NewComponentNode(doc, ctx.TargetImageInfo.Arch);

		if (SkipOOBE
			|| HideEULA
			|| SkipNetwork
			|| HideOnlineAccountScreens)
		{
			auto OOBE = doc.allocate_node(rapidxml::node_element, "OOBE");
			if (SkipOOBE)
				OOBE->append_node(doc.allocate_node(rapidxml::node_element, "SkipMachineOOBE", "true"));
			else
			{
				if (HideEULA)
					OOBE->append_node(doc.allocate_node(rapidxml::node_element, "HideEULAPage", "true"));
				if (HideOnlineAccountScreens)
					OOBE->append_node(doc.allocate_node(rapidxml::node_element, "HideOnlineAccountScreens", "true"));
				if (SkipNetwork)
					OOBE->append_node(doc.allocate_node(rapidxml::node_element, "HideWirelessSetupInOOBE", "true"));
			}
			component->append_node(OOBE);
		}
		if (AutoLogon != -1)
		{
			auto AutoLogon = doc.allocate_node(rapidxml::node_element, "AutoLogon");
			auto Enabled = doc.allocate_node(rapidxml::node_element, "Enabled");
			Enabled->value("true");
			AutoLogon->append_node(Enabled);

			auto Username = doc.allocate_node(rapidxml::node_element, "Username");
			if (this->AutoLogon == -2)
				Username->value("Administrator");
			else
				for (int i = 0, j = 0;; ++i)
					if (j == this->AutoLogon)
					{
						this->AutoLogon = i;
						SetNodeValue(doc, Username, Users[i].Name);
						break;
					}
					else if (!Users[i].LogonSetPassword)
						++j;
			AutoLogon->append_node(Username);

			auto Password = doc.allocate_node(rapidxml::node_element, "Password");
			auto PlainText = doc.allocate_node(rapidxml::node_element, "PlainText");
			PlainText->value("false");
			Password->append_node(PlainText);
			auto Value = doc.allocate_node(rapidxml::node_element, "Value");
			if (this->AutoLogon == -2)
			{
				auto Password = AdministratorPassword + L"Password";
				Password = Base64Encode(Password.GetPointer(), Password.GetLength() * sizeof(WCHAR));
				SetNodeValue(doc, Value, Password);
			}
			else
				SetNodeValue(doc, Value, Users[this->AutoLogon].Password);
			Password->append_node(Value);
			AutoLogon->append_node(Password);

			if (AutoLogonOnce)
			{
				auto LogonCount = doc.allocate_node(rapidxml::node_element, "LogonCount");
				LogonCount->value("1");
				AutoLogon->append_node(LogonCount);
			}

			component->append_node(AutoLogon);
		}
		if (!Users.empty() || AutoLogon == -2)
		{
			auto UserAccounts = doc.allocate_node(rapidxml::node_element, "UserAccounts");
			if (AutoLogon == -2)
			{
				auto AdministratorPassword = doc.allocate_node(rapidxml::node_element, "AdministratorPassword");
				auto PlainText = doc.allocate_node(rapidxml::node_element, "PlainText");
				PlainText->value("false");
				AdministratorPassword->append_node(PlainText);
				auto Value = doc.allocate_node(rapidxml::node_element, "Value");

				auto Password = this->AdministratorPassword + L"AdministratorPassword";
				Password = Base64Encode(Password.GetPointer(), Password.GetLength() * sizeof(WCHAR));
				SetNodeValue(doc, Value, Password);
				AdministratorPassword->append_node(Value);
				UserAccounts->append_node(AdministratorPassword);
			}

			if (!Users.empty())
			{
				auto LocalAccounts = doc.allocate_node(rapidxml::node_element, "LocalAccounts");
				for (const auto& i : Users)
				{
					auto LocalAccount = doc.allocate_node(rapidxml::node_element, "LocalAccount");
					LocalAccount->append_attribute(doc.allocate_attribute("wcm:action", "add"));
					auto Password = doc.allocate_node(rapidxml::node_element, "Password");
					if (!i.LogonSetPassword)
					{
						auto PlainText = doc.allocate_node(rapidxml::node_element, "PlainText");
						PlainText->value("false");
						Password->append_node(PlainText);
						auto Value = doc.allocate_node(rapidxml::node_element, "Value");
						SetNodeValue(doc, Value, i.Password);
						Password->append_node(Value);
					}
					LocalAccount->append_node(Password);
					auto Group = doc.allocate_node(rapidxml::node_element, "Group");
					SetNodeValue(doc, Group, i.Group);
					LocalAccount->append_node(Group);
					auto Name = doc.allocate_node(rapidxml::node_element, "Name");
					SetNodeValue(doc, Name, i.Name);
					LocalAccount->append_node(Name);
					if (!i.Description.Empty())
					{
						auto Description = doc.allocate_node(rapidxml::node_element, "Description");
						SetNodeValue(doc, Description, i.Description);
						LocalAccount->append_node(Description);
					}
					if (!i.DisplayName.Empty())
					{
						auto DisplayName = doc.allocate_node(rapidxml::node_element, "DisplayName");
						SetNodeValue(doc, DisplayName, i.DisplayName);
						LocalAccount->append_node(DisplayName);
					}
					LocalAccounts->append_node(LocalAccount);
				}
				UserAccounts->append_node(LocalAccounts);
			}

			component->append_node(UserAccounts);
		}
		if (!RegisteredOrganization.Empty())
		{
			auto RegisteredOrganization = doc.allocate_node(rapidxml::node_element, "RegisteredOrganization");
			SetNodeValue(doc, RegisteredOrganization, this->RegisteredOrganization);
			component->append_node(RegisteredOrganization);
		}
		if (!RegisteredOwner.Empty())
		{
			auto RegisteredOwner = doc.allocate_node(rapidxml::node_element, "RegisteredOwner");
			SetNodeValue(doc, RegisteredOwner, this->RegisteredOwner);
			component->append_node(RegisteredOwner);
		}

		node->append_node(component);
		root->append_node(node);
		doc.append_node(root);

		Unattend = PrintXml(doc);
	}

	return Unattend;
}
