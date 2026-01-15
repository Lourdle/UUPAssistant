#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include <memory>
#include <functional>

#include <Windows.h>
#include <Lourdle.UIFramework.h>


constexpr COLORREF kSecondaryTextColor = 0xA6A6A6;

extern struct HostContext
{
	HANDLE hParent;
	WPARAM wParam;
	LPARAM lParam;
}*g_pHostContext;

class ProgressRing : public Lourdle::UIFramework::Window
{
public:
	ProgressRing(WindowBase* Parent, bool Legacy = false);

	void Init();
	void Start();
	void Stop();

	void OnDraw(HDC hdc, RECT rect);
	void OnSize(BYTE type, int nClientWidth, int nClientHeight, Lourdle::UIFramework::WindowBatchPositioner);
	void OnShowWindow(bool bIsBeingShown, int Status);
	void OnThemeChanged();

	COLORREF Color;
	WCHAR Code;
private:
	bool Legacy;
};

struct ImageInfo
{
	std::wstring Name;
	std::wstring Edition;
	std::vector<std::wstring> UpgradableEditions;
	std::vector<std::wstring> AdditionalEditions;
	std::wstring Lang;
	std::wstring Branch;
	std::wstring Version;
	std::wstring SystemESD;

	WORD Arch;
	BYTE SupportEFI : 1;
	BYTE SupportLegacyBIOS : 1;
	BYTE bHasBootEX : 1;
};

struct Update
{
	Lourdle::UIFramework::String UpdateFile;
	Lourdle::UIFramework::String PSF;
};
struct AppxFeature
{
	std::wstring Feature;
	Lourdle::UIFramework::String Bundle;
	std::vector<AppxFeature*> Dependencies;
	BYTE bInstall : 1;
	BYTE bHasLicense : 1;
	BYTE bInstalled : 1;
};

struct SessionContext
{
	std::wstring PathUUP, PathTemp;
	ImageInfo TargetImageInfo;

	std::vector<AppxFeature> AppxFeatures;
	std::vector<Lourdle::UIFramework::String> AppVector;
	std::vector<Update> UpdateVector;
	std::vector<Lourdle::UIFramework::String> DriverVector;
	Lourdle::UIFramework::String SetupUpdate;
	Lourdle::UIFramework::String SafeOSUpdate;
	Lourdle::UIFramework::String EnablementPackage;

	UINT bForceUnsigned : 1;
	UINT bInstallEdge : 1;
	UINT bAddSetupUpdate : 1;
	UINT bAddSafeOSUpdate : 1;
	UINT bAddEnablementPackage : 1;
	UINT bCleanComponentStore : 1;
	UINT bStubOptionInstallFull : 1;
	UINT bAdvancedOptionsAvaliable : 1;
};

struct LetterInfo
{
	union
	{
		GUID id;
		struct
		{
			DWORD Signature;
			ULONGLONG ullOffset;
		}MBR;
	};
	CHAR cLetter;
	UINT ByGUID : 1;
};

struct VMSetting
{
	ULONGLONG Letter : 16;
	ULONGLONG ISize : 24;
	ULONGLONG MSize : 24;
};

struct RegKey
{
	BYTE SYSTEM : 1;
	PCWSTR SubKey;
	Lourdle::UIFramework::String ValueName;
	std::unique_ptr<BYTE[]> Data;
	DWORD Size;
	DWORD Type;
};

struct AppDlg : Lourdle::UIFramework::DialogEx2<Lourdle::UIFramework::WindowBase>
{
	AppDlg(Lourdle::UIFramework::WindowBase* Parent, SessionContext& ctx);
	~AppDlg();

	SessionContext& ctx;

	Lourdle::UIFramework::Button Add;
	Lourdle::UIFramework::Button Browse;
	Lourdle::UIFramework::Button Remove;
	Lourdle::UIFramework::Button ViewAppxFeatures;
	Lourdle::UIFramework::Button InstallEdge;
	HMENU hRemoveOptionMenu;
	Lourdle::UIFramework::Edit Path;
	Lourdle::UIFramework::ListView AppList;

	IFileOpenDialog* pFileOpenDialog;

	bool OnClose();
	void AddFiles();
	void BrowseFiles();
	void RemoveIt();
	void RemoveSelectedItems();
	void RemoveAll();
	void RemoveItems();
	void ViewFeatures();
	void OnDraw(HDC, RECT);

	void Init();

	static void OpenDialog(Lourdle::UIFramework::WindowBase* Parent, SessionContext* ctx);
};

struct UpdateDlg : Lourdle::UIFramework::DialogEx2<Lourdle::UIFramework::WindowBase>
{
	UpdateDlg(Lourdle::UIFramework::WindowBase* Parent, SessionContext& ctx);
	~UpdateDlg();

	Lourdle::UIFramework::ListView UpdateList;
	Lourdle::UIFramework::Button Remove;
	HMENU hItemMenu;
	HMENU hRemoveMenu;
	IFileOpenDialog* pFileOpenDialog;
	Lourdle::UIFramework::Edit Path;
	Lourdle::UIFramework::Edit FinalPath;
	Lourdle::UIFramework::Button Browse;
	Lourdle::UIFramework::Button BrowseDir;
	Lourdle::UIFramework::Button Add;
	Lourdle::UIFramework::Button ViewOtherUpdates;
	Lourdle::UIFramework::Button CleanComponents;
	SessionContext& ctx;

	void Init();
	void OnDraw(HDC, RECT);
	LRESULT OnNotify(LPNMHDR);

	void RemoveItem();
	void RemoveIt();
	void RemoveSelectedItems();
	void RemoveAll();
	void SetUpdatePSF();
	void BrowseFiles();
	void BrowseDirectory();
	void AddItem();
	void OpenOtherUpdateDlg();
	void SwitchCleanComponents();

	static void OpenDialog(Lourdle::UIFramework::WindowBase* Parent, SessionContext* ctx);
};

struct DriverDlg : Lourdle::UIFramework::DialogEx2<Lourdle::UIFramework::WindowBase>
{
	DriverDlg(Lourdle::UIFramework::WindowBase* Parent, SessionContext& ctx);
	~DriverDlg();

	SessionContext& ctx;

	Lourdle::UIFramework::ListView DriverList;
	Lourdle::UIFramework::Button BrowseDir;
	Lourdle::UIFramework::Button BrowseFiles;
	Lourdle::UIFramework::Edit Path;
	Lourdle::UIFramework::Button Add;
	Lourdle::UIFramework::Button Recurse;
	Lourdle::UIFramework::Button ForceUnsigned;
	Lourdle::UIFramework::Button Remove;
	Lourdle::UIFramework::Button AddDriverFromInstalledOS;
	std::unique_ptr<ProgressRing> Ring;

	HMENU hRemoveOptionMenu;

	void Browse();
	void FileBrowse();
	void AddItems();
	void PopMenu();
	void RemoveIt();
	void RemoveChecked();
	void RemoveAll();
	void ScanInstalledDrivers();

	void Init();
	void OnDraw(HDC, RECT);
	bool OnClose();
	void OnDestroy();

	static void OpenDialog(WindowBase* Parent, SessionContext* ctx);
};


struct SettingOOBEDlg : Lourdle::UIFramework::Dialog
{
	SettingOOBEDlg(Lourdle::UIFramework::WindowBase*);

	struct OOBESettingsStruct
	{
		Lourdle::UIFramework::String AdministratorPassword;
		Lourdle::UIFramework::String ComputerName;
		Lourdle::UIFramework::String RegisteredOwner;
		Lourdle::UIFramework::String RegisteredOrganization;

		int AutoLogon;
		BYTE SkipOOBE : 1;
		BYTE HideEULA : 1;
		BYTE SkipNetwork : 1;
		BYTE HideOnlineAccountScreens : 1;
		BYTE AutoLogonOnce : 1;

		struct User
		{
			Lourdle::UIFramework::String Name;
			Lourdle::UIFramework::String Group;
			Lourdle::UIFramework::String Password;
			Lourdle::UIFramework::String Description;
			Lourdle::UIFramework::String DisplayName;
			bool LogonSetPassword;
		};

		std::vector<User>Users;

		std::string ToUnattendXml(const SessionContext& ctx);
	} Settings = {};

	void Init(LPARAM);
	void OnDraw(HDC, RECT);
	LRESULT OnNotify(LPNMHDR);
	void OnCancel();
	void OnOK();

	Lourdle::UIFramework::Button SkipOOBE;
	Lourdle::UIFramework::Button EnableAdministrator;
	Lourdle::UIFramework::Edit AdministratorPassword;
	Lourdle::UIFramework::Button SkipEULA;
	Lourdle::UIFramework::Button SkipNetworkSetup;
	Lourdle::UIFramework::Button HideOnlineAccountScreens;
	Lourdle::UIFramework::Edit ComputerName;
	Lourdle::UIFramework::Edit RegisteredOwner;
	Lourdle::UIFramework::Edit RegisteredOrganization;
	Lourdle::UIFramework::ListView UserAccounts;
	Lourdle::UIFramework::Button AddUser;
	Lourdle::UIFramework::Button RemoveUser;
	Lourdle::UIFramework::Button EnableAutoLogon;
	Lourdle::UIFramework::ComboBox AutoLogonUser;
	Lourdle::UIFramework::Button AutoLogonOnce;

	void EnableAdministratorAccount();
	void SkipMachineOOBE();
	void CreateUser();
	void DeleteUser();
	void SetAutoLogon();
};

struct ITaskbarList4;
class DirSelection
	: public Lourdle::UIFramework::Window
{
public:
	DirSelection(SessionContext& ctx);
	~DirSelection();

	virtual LRESULT WindowProc(UINT Msg, WPARAM wParam, LPARAM lParam);
	void OnDraw(HDC, RECT);
	void OnClose();
	void OnDestroy();
	void OnSize(BYTE type, int nClientWidth, int nClientHeight, Lourdle::UIFramework::WindowBatchPositioner);

	Lourdle::UIFramework::Button ButtonMain;
	Lourdle::UIFramework::Button ButtonBrowse;
	Lourdle::UIFramework::Button FetchUUP;
	Lourdle::UIFramework::Edit Path;
	Lourdle::UIFramework::ComboBox Target;
	Lourdle::UIFramework::ComboBox Purpose;
	Lourdle::UIFramework::ListBox AdditionEditions;
	Lourdle::UIFramework::ProgressBar ProcessingProgress;

	void Browse();
	void MainButtonClicked();
	void Fetch();
	void OnSwitchEdition();

	std::vector<ImageInfo> ImageDetail;
	int ComboBoxSel;
	enum State : char
	{
		UUP,
		Scaning,
		Temp,
		ScaningCabs,
		ProcessingCab,
		ProcessingEsd,
		Cleaning,
		AllDone,
	}State;

	::std::wstring String;
	BYTE CurrentFileProgress, TotalProgress;
	WORD wProcessed, wTotal;
	HANDLE hEvent;

	enum TaskbarState : char
	{
		NoProgress = 0,
		Indeterminate = 0x1,
		Normal = 0x2,
		Error = 0x4,
		Paused = 0x8
	};

	SessionContext& ctx;
	void SetTaskbarState(TaskbarState State);
	void SetTaskbarProgress(ULONGLONG ullComplited, ULONGLONG ullTotal);
private:
	UINT TBBCM;
	ITaskbarList4* pTaskbar;
};

class InstallationWizard : public Lourdle::UIFramework::Window
{
public:
	InstallationWizard(SessionContext&);
	SessionContext& ctx;

	void OnDraw(HDC, RECT);
	void OnDestroy();
	LRESULT OnNotify(LPNMHDR);
	DWORD OnDeviceChanged(WORD, LPCVOID);
	void OnClose();
	HBRUSH OnControlColorStatic(HDC hDC, Lourdle::UIFramework::WindowBase Window);
	void OnSize(BYTE type, int nClientWidth, int nClientHeight, Lourdle::UIFramework::WindowBatchPositioner);

	void EditTextChanged();
	void RefreshInfo();
	void Continue();
	void DontBootButtonClicked();
	void SwitchExtraBootOption();
	void OpenSetESPsDlg();
	void OpenSetActivePartsDlg();
	void OpenSetBootRecordsDlg();
	void OpenSetRecPartsDlg();
	void SetFormatOptions();
	void SwitchInstallationMethod();
	void SwitchBootMode();
	void SetBootPartList();
	void EnableSetBootPart(bool bEnable = true);

	enum : UINT
	{
		Msg_Quit = 0x50E4,
		Msg_Refresh,
		Msg_SetESP,
		Msg_SetRec,
		Msg_Pause,
		Msg_Continue,
		Msg_Format
	};

	DWORD idThread;
	struct PartVolInfo
	{
		struct DiskInfo
		{
			union
			{
				GUID id;
				DWORD Signature;
			};
			Lourdle::UIFramework::String Name;
			Lourdle::UIFramework::String FriendlyName;
			Lourdle::UIFramework::String AdaptorName;
			Lourdle::UIFramework::String DevicePath;
			ULONGLONG ullSize;
			ULONG ulBytesPerSector;
			Lourdle::UIFramework::String VHDPath;
		};
		std::shared_ptr<DiskInfo> pDiskInfo;
		ULONG Number;
		ULONGLONG ullOffset;
		ULONGLONG ullSize;
		ULONGLONG ullFreeSpace;
		WCHAR DosDeviceLetter;
		PCWSTR Filesystem;
		Lourdle::UIFramework::String Label;
		union
		{
			struct
			{
				GUID Type;
				GUID id;
				WCHAR PartName[37];
			}GPT;

			struct
			{
				BYTE Type;
				bool bootIndicator;
				bool Primary;
			}MBR;
		}PartTableInfo;
		BYTE GPT : 1;
		BYTE Removable : 1;
		BYTE System : 1;
		BYTE Page : 1;
	};

	struct FormatOptionsStruct
	{
		Lourdle::UIFramework::String Label;
		BYTE bForce : 1;
		BYTE bQuick : 1;
		BYTE ReFS : 1;
	}FormatOptions = {};


	std::vector<PartVolInfo> PartInfoVector;

	struct LetterDlg : Lourdle::UIFramework::Dialog
	{
		LetterDlg(InstallationWizard* Parent);

		void Init(LPARAM);
		void OnDraw(HDC, RECT);
		LRESULT OnNotify(LPNMHDR);
		void OnDestroy();
		INT_PTR DialogProc(UINT, WPARAM, LPARAM);

		Lourdle::UIFramework::ListView PartVolList;
		Lourdle::UIFramework::Button SetIt;
		Lourdle::UIFramework::Button Refresh;
		Lourdle::UIFramework::ComboBox Letter;

		void Set();
		void RefreshInfo();

		DWORD idThread;

		struct VolInfoOnDynamicDisks
		{
			GUID id;
			ULONGLONG ullSize;
			Lourdle::UIFramework::String Label;
			Lourdle::UIFramework::String Type;
			PCWSTR Filesystem;
			WCHAR Letter;
		};

		std::vector<VolInfoOnDynamicDisks> Volumes;
	}AssigningLetterDlg;
	std::vector<LetterInfo> LetterInfoVector;

	struct SettingVMDlg : Lourdle::UIFramework::Dialog
	{
		SettingVMDlg(InstallationWizard* Parent);

		Lourdle::UIFramework::ListView VolumeList;
		Lourdle::UIFramework::Button Auto;
		Lourdle::UIFramework::Button Set;

		void Init(LPARAM);
		LRESULT OnNotify(LPNMHDR pnmhdr);
		void OnDraw(HDC, RECT);

		void AutoMgmt();
		void SetIt();
	}SettingVM;
	std::vector<VMSetting>VMSettings;

	SettingOOBEDlg SettingOOBE;

	Lourdle::UIFramework::ListView PartList;
	Lourdle::UIFramework::ListView BootPartList;
	Lourdle::UIFramework::Button Refresh;
	Lourdle::UIFramework::Button Next;
	Lourdle::UIFramework::Edit Detail;
	Lourdle::UIFramework::Edit TargetPath;
	Lourdle::UIFramework::Button BrowseTargetPath;
	Lourdle::UIFramework::Edit Boot;
	Lourdle::UIFramework::Button BrowseBootPath;
	Lourdle::UIFramework::Button DontBoot;
	Lourdle::UIFramework::Static BootFromVHD;
	Lourdle::UIFramework::Button ExtraBootOption;
	Lourdle::UIFramework::ComboBox BootMode;
	Lourdle::UIFramework::Edit Label;
	Lourdle::UIFramework::Tooltips ToolTip;
	ProgressRing Ring;

	HICON hIcon;
	enum : BYTE
	{
		Clear,
		WarningSystem,
		WarningPage,
		WarningSpaceNotEnough,
		Updating,
		UpdatingMultiRequests,
		Formatting,
		AdjustingWindow,
		Done
	};
	BYTE State : 6;
	BYTE DeleteLetterAfterInstallation : 1;
	BYTE EFI : 1;

	BYTE VMSysAutoMgmt : 1;
	BYTE InstDotNet3 : 1;
	BYTE DontInstWinRe : 1;
	BYTE Edition : 5;

	struct
	{
		DWORD dwDisk;
		DWORD dwPart;
	} ReImagePart = {};
	Lourdle::UIFramework::String Target;
	Lourdle::UIFramework::String BootPath;
	Lourdle::UIFramework::String BootVHD;
	WCHAR cLetter;
};

struct InstallationProgress : Lourdle::UIFramework::Window
{
	InstallationProgress(InstallationWizard*);
	SessionContext& ctx;
	std::string Unattend;
	std::vector<RegKey> Keys;
	BYTE InstDotNet3 : 1;
	BYTE DontInstWinRe : 1;
	BYTE EFI : 1;
	BYTE Edition : 5;
	Lourdle::UIFramework::String Target;
	Lourdle::UIFramework::String BootPath;
	Lourdle::UIFramework::String BootVHD;

	Lourdle::UIFramework::Edit StateDetail;
	Lourdle::UIFramework::Static AfterInstallation;
	Lourdle::UIFramework::Button Shutdown;
	Lourdle::UIFramework::Button Reboot;
	Lourdle::UIFramework::Button Quit;
	ProgressRing Ring;
	Lourdle::UIFramework::Static StateStatic;

	struct
	{
		DWORD dwDisk;
		DWORD dwPart;
	} ReImagePart = {};
	struct
	{
		union
		{
			GUID guid;
			DWORD id;
		};
		ULONGLONG offset;
	} RePartInfo;

	enum : BYTE
	{
		ApplyingImage = 0,
		InstallingFeatures,
		InstallingUpdates,
		InstallingSoftware,
		ApplyingSettings
	};
	BYTE State : 6;
	BYTE RePartIsGPT : 1;
	BYTE DeleteLetterAfterInstallation : 1;

	LRESULT WindowProc(UINT Msg, WPARAM wParam, LPARAM lParam);
	void OnClose();
	void OnDraw(HDC, RECT);
	void OnDestroy();
};

struct SetPartStruct
{
	std::shared_ptr<InstallationWizard::PartVolInfo::DiskInfo> pDiskInfo;
	GUID id;
	bool Set;
};

class InPlaceSetupWizard : public Lourdle::UIFramework::Window
{
	~InPlaceSetupWizard() = default;
public:
	InPlaceSetupWizard(SessionContext&);
	SessionContext& ctx;

	void OnDraw(HDC, RECT);
	void OnDestroy();
	void OnSize(BYTE type, int nClientWidth, int nClientHeight, Lourdle::UIFramework::WindowBatchPositioner);

	Lourdle::UIFramework::Button InstallUpdates;
	Lourdle::UIFramework::Button InstallAppxes;
	Lourdle::UIFramework::Button AddDrivers;
	Lourdle::UIFramework::Button Next;
	Lourdle::UIFramework::Button PreventUpdating;
	Lourdle::UIFramework::Button NoReboot;
	Lourdle::UIFramework::Button SynergisticInstallation;
	Lourdle::UIFramework::Button RemoveHwReq;

	HANDLE hProcess;
	HANDLE hFile;

	void SwitchInstallationMethod();
	void Execute();
};

class UpgradeProgress : public Lourdle::UIFramework::Window
{
	~UpgradeProgress();
public:
	UpgradeProgress(SessionContext&, HANDLE hEvent);

	LRESULT WindowProc(UINT Msg, WPARAM wParam, LPARAM lParam);
	void OnClose();
	void OnDraw(HDC, RECT);
	HWND ResetOwner();

	enum : BYTE
	{
		PreparingFiles,
		ApplyingImage,
		InstallingUpdates,
		InstallingSoftware,
		WorkingOnMigration
	};
	BYTE State;
	
	enum : BYTE
	{
		Upgrade,
		DataOnly,
		CleanInstall,
		Unknown
	};
	BYTE InstallationType;

	bool InstallNetFx3;
	bool Cancel;

	Lourdle::UIFramework::Edit StateDetail;
	Lourdle::UIFramework::Static StateStatic;
	ProgressRing Ring;

	HANDLE hEvent;

	SessionContext& ctx;
};

struct CreateImageWizard : Lourdle::UIFramework::Window
{
	CreateImageWizard(SessionContext&);

	void OnDraw(HDC, RECT);
	void OnClose();
	void OnDestroy();
	LRESULT OnNotify(LPNMHDR);

	UpdateDlg UpdateDlg;
	AppDlg AppInstalling;
	DriverDlg DrvDlg;

	Lourdle::UIFramework::Button InstallUpdates;
	Lourdle::UIFramework::Button InstallAppxes;
	Lourdle::UIFramework::Button AddDrivers;
	Lourdle::UIFramework::Button InstallDotNetFx3;
	Lourdle::UIFramework::Button Browse;
	Lourdle::UIFramework::Button Next;
	Lourdle::UIFramework::Button NoLegacyBoot;
	Lourdle::UIFramework::Button SplitImage;
	Lourdle::UIFramework::Button CreateISO;
	Lourdle::UIFramework::Button CreateWIM;
	Lourdle::UIFramework::Button BootEX;
	Lourdle::UIFramework::Button RemoveHwReq;
	Lourdle::UIFramework::Tooltips Tips;
	Lourdle::UIFramework::ComboBox UefiBootOption;
	Lourdle::UIFramework::ComboBox Compression;
	Lourdle::UIFramework::ListView Editions;
	Lourdle::UIFramework::Edit Path;
	Lourdle::UIFramework::Edit CDLabel;
	Lourdle::UIFramework::Edit PartSize;
	Lourdle::UIFramework::Edit StateDetail;

	SessionContext& ctx;
	bool Cancel;
	bool CanClose;

	Lourdle::UIFramework::Static State;
	ProgressRing Ring;

	void DisableDialogControls();
	void InstallUpdatesClicked();
	void InstallAppxesClicked();
	void AddDriversClicked();
	void BrowseFile();
	void Execute();
};

constexpr DWORD WIM_FLAG_ALLOW_LZMS = 0x20000000;

struct DismWrapper
{
	DismWrapper(PCWSTR ImagePath, PCWSTR TempPath, bool* pbCancel,
		std::function<void(PCWSTR)> AppendText);
	~DismWrapper();

	UINT Session;
	PCWSTR ImagePath;
	PCWSTR TempPath;
	bool* pbCancel;

	std::function<void(PCWSTR)> AppendText;
	std::function<void(PCWSTR)> SetString;
	std::function<int(LPCTSTR lpText, LPCWSTR lpCaption, UINT uType)> MessageBox;

	bool OpenSession();
	void CloseSession(bool bNoPrompt = false);

	bool EnableDotNetFx3(PCWSTR SourcePath);

	bool SetEdition(PCWSTR EditionId, bool bNoPrompt = false);

	// Return value: TRUE if the DISM session is still open, FALSE if the DISM session is closed, -1 if canceled.
	BYTE AddUpdates(const SessionContext& ctx);

	bool AddDrivers(const SessionContext& ctx);

	bool AddApps(SessionContext& ctx);

	bool AddSinglePackage(PCWSTR PackagePath);
};


struct WIMStruct;
int ApplyWIMImage(WIMStruct* wim, PCWSTR pszPath, int Index, bool* pbCancel, std::function<void(PCWSTR)> AppendText, std::function<void(PCWSTR)> SetString);

void SetReferenceFiles(HANDLE hWim, const std::wstring& TempPath);
void SetReferenceFiles(WIMStruct* wim);

void GetAppxFeatures(SessionContext&);

bool SetPBRAndDiskMBR(PCWSTR pDiskName, DWORD Signature, ULONG ulBytesPerSector, ULONG Number, ULONGLONG ullOffset);

bool GetAdditionalDrivers(HANDLE hFile, std::unordered_set<std::wstring>& InfFiles);
bool FindDrivers(PCWSTR pDirectory, WORD wArch, std::vector<std::wstring>& Drivers);
bool IsApplicableDriver(HANDLE hFile, WORD wArch);

void UnloadMountedImageRegistries(PCWSTR pszMountDir);

bool InstallMicrosoftEdge(PCWSTR pszEdgeWim, PCWSTR pszSystemDrive, PCWSTR pszTempDir, WORD wSystemArch);
bool CheckWhetherNeedToInstallMicrosoftEdge(
	PCWSTR pszEdgeWim, PCWSTR pszTempDir,
	std::wstring& refCurrentEdgeVersion, std::wstring& refCurrentWebView2Version,
	std::wstring& refImageEdgeVersion, std::wstring& refImageWebView2Version
);

struct CreateImageContext
{
	Lourdle::UIFramework::Static& State;
	Lourdle::UIFramework::Edit& StateDetail;
	Lourdle::UIFramework::WindowBase* DisplayWindow;
	bool IsDialog;
	std::vector<Lourdle::UIFramework::String> Editions;
};

void CreateImage(
	SessionContext& ctx, int Compression, PCWSTR pszDestinationImage, PCWSTR pszBootWim,
	bool& Cancel, bool InstallDotNetFx3, std::unique_ptr<CreateImageContext> cictx,
	std::function<void(bool Succeeded)> OnFinish);
