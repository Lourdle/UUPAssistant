#include "pch.h"
#include "Resources/resource.h"

#include <Shlwapi.h>
#include <wimgapi.h>

#include <vector>
#include <thread>
#include <format>

using namespace Lourdle::UIFramework;
using namespace std;

constexpr int BUFFER_SIZE = 16 * 1024;
constexpr int MIN_PART_SIZE_MB = 200;
constexpr int MAX_CD_LABEL_LENGTH = 32;

static void AppendText(PCWSTR psz, CreateImageWizard* p)
{
	p->State.SetWindowText(nullptr);
	int len = p->StateDetail.GetWindowTextLength();
	p->StateDetail.SetSel(len, len);
	p->StateDetail.ReplaceSel(psz);
}

static vector<String> GetEditions(CreateImageWizard* p)
{
	vector<String> Editions;
	for (int i = 0; i != p->ctx.TargetImageInfo.UpgradableEditions.size(); ++i)
		if (p->Editions.GetCheckState(i) == BST_CHECKED)
			Editions.push_back(p->ctx.TargetImageInfo.UpgradableEditions[i].c_str());
	return Editions;
}

static void CreateFinalWindowsImage(CreateImageWizard* p, bool bMediaBootEX)
{
	auto Compression = p->Compression.GetCurSel();
	auto Path = p->Path.GetWindowText();
	PCWSTR pszFinalImage;
	if (p->SplitImage.GetCheck())
		pszFinalImage = L"Install.swm";
	else if (p->CreateISO.GetCheck())
		pszFinalImage = Compression == WIM_COMPRESS_LZMS ? L"Media\\sources\\install.esd" : L"Media\\sources\\install.wim";
	else
		pszFinalImage = Path;

	auto OnFinish = [=](bool Succeeded)
		{
			if (p->Cancel)
				p->Next.PostCommand();
			else
			{
				if (!Succeeded && !p->Cancel)
				{
					p->Ring.ShowWindow(SW_HIDE);
					p->State.SetWindowPos(GetFontSize() * 2, GetFontSize() * 57, 0, 0, SWP_NOSIZE);
					String Failed = GetString(String_Failed);
					*find(Failed.begin(), Failed.end(), L'\r') = 0;
					p->State.SetWindowText(Failed);
				}
				p->Cancel |= !Succeeded;
			}
			if (p->CreateWIM.GetCheck() == BST_CHECKED && p->SplitImage.GetCheck() == BST_UNCHECKED)
				p->CanClose = true;
		};
	
	CreateImage(p->ctx, Compression,
		pszFinalImage, p->CreateISO.GetCheck() ? L"Media\\sources\\boot.wim" : nullptr,
		p->Cancel, p->InstallDotNetFx3.GetCheck() == BST_CHECKED,
		std::make_unique<CreateImageContext>(p->State, p->StateDetail, p, false, GetEditions(p)),
		OnFinish);

	if (p->Cancel)
	{
		p->CanClose = true;
		p->Ring.ShowWindow(SW_HIDE);
		return;
	}

	HANDLE hWim = nullptr;
	if (bMediaBootEX)
	{
		hWim = WIMCreateFile(pszFinalImage, GENERIC_READ, OPEN_EXISTING, WIM_FLAG_ALLOW_LZMS, 0, nullptr);
		WIMSetTemporaryPath(hWim, p->ctx.PathTemp.c_str());
		HANDLE hImage = WIMLoadImage(hWim, 1);
		if (hWim && hImage)
		{
			struct WFD : WIM_FIND_DATA
			{
				BYTE szReserved[32];
			}wfd;

			wstring Path = L"\\Windows\\Boot\\Fonts_EX\\*";
			wstring Target = L"Media\\efi\\microsoft\\boot\\fonts\\";
			HANDLE hFind = WIMFindFirstImageFile(hImage, Path.c_str(), &wfd);
			if (hFind)
			{
				auto size = Path.size() - 1;
				do
				{
					Target.resize(31);
					Target += wfd.cFileName;
					PWSTR p = wcsstr(&Target[0], L"_EX");
					if (p) for (PWCH p2 = p + 3; *p = *p2; ++p, ++p2);
					else continue;
					Path.resize(size);
					Path += wfd.cFileName;
					DeleteFileW(Target.c_str());
					WIMExtractImagePath(hImage, Path.c_str(), Target.c_str(), 0);
				} while (WIMFindNextImageFile(hFind, &wfd));
				WIMCloseHandle(hFind);
			}

			Target.resize(25); // back to parent path
			Path.resize(14); // back to parent path
			Path += L"DVD_EX\\EFI\\en-US\\";
			Target += L"efisys.bin";
			Path += L"efisys.bin";
			DeleteFileW(Target.c_str());
			Path.insert(Path.size() - 4, L"_EX");
			WIMExtractImagePath(hImage, Path.c_str(), Target.c_str(), 0);
			Target.insert(Target.size() - 4, L"_noprompt");
			Path.insert(Path.size() - 7, L"_noprompt");
			DeleteFileW(Target.c_str());
			WIMExtractImagePath(hImage, Path.c_str(), Target.c_str(), 0);
			DeleteFileW(L"Media\\bootmgr.efi");
			WIMExtractImagePath(hImage, L"\\Windows\\Boot\\EFI_EX\\bootmgr_EX.efi", L"Media\\bootmgr.efi", 0);
			Target.resize(10);
			Target += L"boot\\";
			if (p->ctx.TargetImageInfo.Arch == PROCESSOR_ARCHITECTURE_ARM64)
				Target += L"bootaa64.efi";
			else if (p->ctx.TargetImageInfo.Arch == PROCESSOR_ARCHITECTURE_INTEL)
				Target += L"bootia32.efi";
			else if (p->ctx.TargetImageInfo.Arch == PROCESSOR_ARCHITECTURE_AMD64)
				Target += L"bootx64.efi";
			DeleteFileW(Target.c_str());
			WIMExtractImagePath(hImage, L"\\Windows\\Boot\\EFI_EX\\bootmgfw_EX.efi", Target.c_str(), 0);
			WIMCloseHandle(hImage);
		}
	}

	if (pszFinalImage == L"Install.swm")
	{
		if (!hWim)
		{
			hWim = WIMCreateFile(L"Install.swm", GENERIC_READ, OPEN_EXISTING, WIM_FLAG_ALLOW_LZMS, 0, nullptr);
			if (!hWim)
			{
				p->ErrorMessageBox();
				OnFinish(false);
				return;
			}
			WIMSetTemporaryPath(hWim, p->ctx.PathTemp.c_str());
		}

		LARGE_INTEGER li = {
			.QuadPart = _wtoll(p->PartSize.GetWindowText()) * 1024 * 1024
		};
		int Compress = p->Compression.GetCurSel();

		AppendText(GetString(String_SplittingImage), p);
		if (!WIMSplitFile(hWim, p->CreateISO.GetCheck() ? L"Media\\sources\\install.swm" : Path.GetPointer(), &li, 0))
		{
			p->ErrorMessageBox();
			WIMCloseHandle(hWim);
			OnFinish(false);
			return;
		}
		AppendText(GetString(String_Succeeded), p);
		WIMCloseHandle(hWim);

		DeleteFileW(L"Install.swm");
	}
	else if (hWim)
		WIMCloseHandle(hWim);

	if (p->CreateISO.GetCheck() == BST_UNCHECKED)
	{
		p->CanClose = true;
		p->Ring.ShowWindow(SW_HIDE);
	}
}

static void CreateISOThread(CreateImageWizard* p)
{
	auto Error = [&](bool bNextLine = false)
		{
			DWORD dwError = GetLastError();
			LPWSTR pszError = nullptr;
			FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, dwError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&pszError), 0, nullptr);
			p->MessageBox(pszError, nullptr, MB_ICONERROR);
			p->State.SetWindowText(nullptr);
			AppendText(pszError, p);
			LocalFree(pszError);
		};

	p->State.SetWindowText(GetString(String_Preparing));
	CreateDirectoryW(L"Media", nullptr);
	CreateDirectoryW(L"Temp", nullptr);

	HANDLE hWim = WIMCreateFile((p->ctx.PathUUP + p->ctx.TargetImageInfo.SystemESD).c_str(), WIM_GENERIC_READ, OPEN_EXISTING, WIM_FLAG_ALLOW_LZMS, 0, nullptr);
	if (!hWim)
	{
		Error(true);
		p->CanClose = true;
		p->Ring.ShowWindow(SW_HIDE);
		return;
	}
	WIMSetTemporaryPath(hWim, L"Temp");
	HANDLE hImage = WIMLoadImage(hWim, 1);
	if (!hImage)
	{
		WIMCloseHandle(hWim);
		Error(true);
		p->CanClose = true;
		p->Ring.ShowWindow(SW_HIDE);
		return;
	}
	if (!WIMApplyImage(hImage, L"Media", 0))
	{
		Error(true);
		WIMCloseHandle(hImage);
		WIMCloseHandle(hWim);
		p->CanClose = true;
		p->Ring.ShowWindow(SW_HIDE);
		return;
	}
	WIMCloseHandle(hImage);
	WIMCloseHandle(hWim);
	if (p->ctx.bAddSetupUpdate && !p->ctx.SetupUpdate.Empty())
		ExpandCabFile(p->ctx.SetupUpdate, L"Media\\sources", nullptr, nullptr);

	if (p->RemoveHwReq.GetCheck() == BST_CHECKED)
	{
		HANDLE hFile = CreateFileW(L"Media\\sources\\appraiserres.dll", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		CloseHandle(hFile);
	}

	CreateFinalWindowsImage(p, p->BootEX.GetCheck() == BST_CHECKED);

	if (p->Cancel)
	{
		p->CanClose = true;
		p->Ring.ShowWindow(SW_HIDE);
		return;
	}

	if (p->RemoveHwReq.GetCheck() == BST_CHECKED)
	{
		if (!SetCurrentDirectoryW(L"Media\\sources"))
		{
			Error(true);
			p->CanClose = true;
			p->Ring.ShowWindow(SW_HIDE);
			return;
		}

		MoveFileW(L"autorun.dll", L"realautorun.dll");

		UINT File_DllFdHook = File_DllFdHookAmd64;
		UINT File_FakeAutoRun = File_FakeAutoRunAmd64;
		if (p->ctx.TargetImageInfo.Arch == PROCESSOR_ARCHITECTURE_ARM64)
		{
			File_DllFdHook = File_DllFdHookArm64;
			File_FakeAutoRun = File_FakeAutoRunArm64;
		}

		if (!WriteFileResourceToFile(L"autorun.dll", File_FakeAutoRun)
			|| !WriteFileResourceToFile(L"dllfdhook.dll", File_DllFdHook)
			|| !SetCurrentDirectoryW(p->ctx.PathTemp.c_str()))
		{
			Error(true);
			p->CanClose = true;
			p->Ring.ShowWindow(SW_HIDE);
			return;
		}
	}

	HRSRC hResource = FindResourceA(nullptr, MAKEINTRESOURCEA(File_OscdimgCAB), "FILE");
	if (!hResource
		|| !ExpandCabFile(LockResource(LoadResource(nullptr, hResource)), SizeofResource(nullptr, hResource), nullptr))
	{
		Error(true);
		p->CanClose = true;
		p->Ring.ShowWindow(SW_HIDE);
		return;
	}
	int iUefiBootOption = p->UefiBootOption.GetCurSel();

	wstring args = L" -m -o -u2 -udfver102 -l\"";
	for (WCHAR i : p->CDLabel.GetWindowText())
	{
		if (i == L'"')
			args.push_back('\\');
		args.push_back(i);
	}
	args += '"';
	if (p->NoLegacyBoot.GetCheck() == BST_UNCHECKED || iUefiBootOption != 0)
	{
		args += L" -bootdata:";
		args.push_back('0' + (iUefiBootOption != 0 ? 1 : 0) + (p->NoLegacyBoot.GetCheck() == BST_CHECKED ? 0 : 1));
		if (p->NoLegacyBoot.GetCheck() == BST_UNCHECKED)
			args += L"#p0,e,bMedia\\boot\\etfsboot.com";
		if (iUefiBootOption != 0)
		{
			args += L"#pEF,e,bMedia\\efi\\microsoft\\boot\\efisys.bin";
			if (iUefiBootOption == 2)
				args.insert(args.size() - 4, L"_noprompt");
		}
	}

	args += format(L" Media \"{}\"", p->Path.GetWindowText().GetPointer());

	wstring tmpdir;
	DWORD cchName = GetShortPathNameW(p->ctx.PathTemp.c_str(), nullptr, 0);
	tmpdir.resize(cchName);
	GetShortPathNameW(p->ctx.PathTemp.c_str(), const_cast<LPWSTR>(tmpdir.c_str()), cchName + 1);
	if (PathIsUNCW(p->ctx.PathTemp.c_str()))
		tmpdir.erase(2, 6);
	else
		tmpdir.erase(0, 4);
	SetCurrentDirectoryW(tmpdir.c_str());

	SECURITY_ATTRIBUTES sa = {
		.nLength = sizeof(SECURITY_ATTRIBUTES),
		.lpSecurityDescriptor = nullptr,
		.bInheritHandle = TRUE
	};

	STARTUPINFOW si = {
		.cb = sizeof(si),
		.dwFlags = STARTF_USESTDHANDLES
	};

	HANDLE hStdOutReadPipe, hStdErrReadPipe;
	CreatePipe(&hStdOutReadPipe, &si.hStdOutput, &sa, 16 * 1024);
	CreatePipe(&hStdErrReadPipe, &si.hStdError, &sa, 0);

	PROCESS_INFORMATION pi;
	AppendText(GetString(String_RunOscdimg), p);
	AppendText(args.c_str(), p);
	BOOL result = CreateProcessW(L"Oscdimg.exe", const_cast<LPWSTR>(args.c_str()), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
	CloseHandle(si.hStdOutput);
	CloseHandle(si.hStdError);
	if (!result)
	{
		AppendText(L"\r\n", p);
		Error();
		CloseHandle(hStdOutReadPipe);
		CloseHandle(hStdErrReadPipe);
		p->CanClose = true;
		p->Ring.ShowWindow(SW_HIDE);
		return;
	}

	CloseHandle(pi.hThread);

	char s[5] = { '\r' };
	do if (p->Cancel)
	{
		TerminateProcess(pi.hProcess, 0);
		break;
	}
	else if (s[0] == '\r')
	{
		DWORD n = 4, nr = 0;
		do
			if (ReadFile(hStdErrReadPipe, s, n, &nr, nullptr))
				n -= nr;
			else
				goto End;
		while (n + nr != 4);

		for (int i = 0; i != 4; ++i)
			if (s[i] == '%')
			{
				s[i] = 0;
				auto ProgressText = ResStrFormat(String_CreatingISOProgress, strtoul(s, nullptr, 10)); // I used to use the string "s" here with "%s", but that caused issues in newer versions of MSVC.
				p->State.SetWindowText(ProgressText);
			}
	}
	while (ReadFile(hStdErrReadPipe, s, 1, nullptr, nullptr));

End:
	AppendText(L"\r\n", p);
	CloseHandle(hStdErrReadPipe);
	DWORD dwExitCode;
	GetExitCodeProcess(pi.hProcess, &dwExitCode);
	CloseHandle(pi.hProcess);
	if (p->Cancel)
	{
		AppendText(GetString(String_Cancelled), p);
		DeleteFileW(p->Path.GetWindowText());
		CloseHandle(hStdOutReadPipe);
		DeleteDirectory(L"Media");
		p->CanClose = true;
		p->Next.PostCommand();
		p->Ring.ShowWindow(SW_HIDE);
		return;
	}
	else if (dwExitCode != 0)
	{
		wstring text = GetString(String_Failed).GetPointer();
		DWORD n = 0;
		MyUniqueBuffer<PSTR> Buffer = 16 * 1024;
		if (ReadFile(hStdOutReadPipe, Buffer, 16 * 1024, &n, nullptr))
		{
			int cch = MultiByteToWideChar(CP_ACP, 0, Buffer, n, nullptr, 0);
			if (cch > 0)
			{
				MyUniquePtr<WCHAR> WideText = cch;
				MultiByteToWideChar(CP_ACP, 0, Buffer, n, WideText, cch);
				text.append(WideText, cch);
			}
		}
		p->MessageBox(text.c_str(), GetString(String_CreateISO), MB_ICONERROR);
		AppendText(text.c_str(), p);
	}
	else
		AppendText(GetString(String_Succeeded), p);
	CloseHandle(hStdOutReadPipe);
	DeleteDirectory(L"Media");
	p->CanClose = true;
	p->Ring.ShowWindow(SW_HIDE);
}

void CreateImageWizard::Execute()
{
	if (!Next.IsWindowVisible())
	{
		DestroyWindow();
		return;
	}
	HANDLE hFile = CreateFileW(Path.GetWindowText(), 0, 0, nullptr, CREATE_NEW, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		ErrorMessageBox();
		return;
	}
	CloseHandle(hFile);

	if (SplitImage.GetCheck() == BST_CHECKED && _wtol(PartSize.GetWindowText()) < MIN_PART_SIZE_MB)
	{
		MessageBox(GetString(String_NeedLargerPart), GetString(String_Notice), MB_ICONERROR);
		return;
	}

	if (CDLabel.GetWindowTextLength() == 0)
	{
		MessageBox(GetString(String_NoCdLabel), GetString(String_Notice), MB_ICONERROR);
		return;
	}
	else if (CDLabel.GetWindowTextLength() > MAX_CD_LABEL_LENGTH)
	{
		MessageBox(GetString(String_CdLabelTooLong), GetString(String_Notice), MB_ICONERROR);
		return;
	}
	else
	{
		String Label = CDLabel.GetWindowText();
		if (WideCharToMultiByte(CP_ACP, 0, Label.GetPointer(), static_cast<int>(Label.GetLength()), nullptr, 0, nullptr, nullptr) > MAX_CD_LABEL_LENGTH)
		{
			MessageBox(GetString(String_CdLabelTooLong), GetString(String_Notice), MB_ICONERROR);
			return;
		}
	}
	for (int i = 0; i != ctx.TargetImageInfo.UpgradableEditions.size(); ++i)
		if (Editions.GetCheckState(i) == BST_CHECKED)
			goto DisableControls;
	MessageBox(GetString(String_NoEditionSelected), GetString(String_Notice), MB_ICONERROR);
	return;

DisableControls:
	Next.ShowWindow(SW_HIDE);
	BootEX.EnableWindow(false);
	InstallDotNetFx3.EnableWindow(false);
	NoLegacyBoot.EnableWindow(false);
	SplitImage.EnableWindow(false);
	Compression.EnableWindow(false);
	UefiBootOption.EnableWindow(false);
	Browse.EnableWindow(false);
	Editions.EnableWindow(false);
	CDLabel.EnableWindow(false);
	PartSize.EnableWindow(false);
	Path.EnableWindow(false);
	CreateISO.EnableWindow(false);
	CreateWIM.EnableWindow(false);
	RemoveHwReq.EnableWindow(false);

	if (!AdjustPrivileges({ SE_SECURITY_NAME, SE_TAKE_OWNERSHIP_NAME }))
		ErrorMessageBox();

	Ring.MoveWindow(GetFontSize() * 2, GetFontSize() * 57, GetFontSize() * 2, GetFontSize() * 2);
	Ring.Start();

	SetCurrentDirectoryW(ctx.PathTemp.c_str());
	CreateDirectoryW(L"Temp", nullptr);
	CanClose = false;

	if (CreateISO.GetCheck() == BST_CHECKED)
		thread(CreateISOThread, this).detach();
	else
		thread(CreateFinalWindowsImage, this, false).detach();
}
