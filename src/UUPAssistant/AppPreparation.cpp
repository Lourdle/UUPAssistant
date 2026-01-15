#include "pch.h"
#include "misc.h"
#include "Xml.h"
#include <Shlwapi.h>
#include <vector>
#include <string>
#include <map>
#include <filesystem>

using namespace std;
namespace fs = std::filesystem;


struct Package_t
{
	wstring Path;
	wstring Prefix;
	wstring File;
};

struct Feature_t
{
	string ID;
	vector<string> vPackages;
	vector<string> vDependencies;
	string LicenseData;
};

static wstring ForceToWstring(const char* p)
{
	if (!p) return {};
	int len = MultiByteToWideChar(CP_UTF8, 0, p, -1, nullptr, 0);
	if (len <= 0) return {};
	wstring w(len - 1, 0);
	MultiByteToWideChar(CP_UTF8, 0, p, -1, w.data(), len);
	return w;
}

void GetAppxFeatures(SessionContext& ctx)
{
	fs::path outDir = ctx.PathTemp;
	fs::path srcDir = ctx.PathUUP;
	fs::path metadataDir = outDir / L"Metadata";
	fs::path cfgFile = metadataDir / L"DesktopTargetCompDB_App_Neutral.xml.cab";
	bool found = false;

	for (const auto& entry : fs::directory_iterator(srcDir))
	{
		if (entry.is_regular_file())
		{
			wstring filename = entry.path().filename().wstring();
			if (filename.length() >= 22 && filename.substr(filename.length() - 22) == L"AggregatedMetadata.cab")
			{
				fs::create_directories(metadataDir);
				if (!ExpandCabFile(entry.path().c_str(), metadataDir.c_str(), nullptr, nullptr))
					continue;

				if (!ExpandCabFile(cfgFile.c_str(), metadataDir.c_str(), nullptr, nullptr))
				{
					fs::path cfgFileUnderscore = metadataDir / L"_DesktopTargetCompDB_App_Neutral.xml.cab";
					if (!ExpandCabFile(cfgFileUnderscore.c_str(), metadataDir.c_str(), nullptr, nullptr))
					{
						DeleteDirectory(metadataDir.c_str());
						continue;
					}
					cfgFile = cfgFileUnderscore;
				}
				found = true;
				break;
			}
		}
	}

	if (!found) return;

	cfgFile.replace_extension("");

	HANDLE hFile = CreateFileW(cfgFile.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		DeleteDirectory(metadataDir.c_str());
		return;
	}

	string text;
	if (!ReadText(hFile, text, GetFileSize(hFile, nullptr)))
	{
		CloseHandle(hFile);
		DeleteDirectory(metadataDir.c_str());
		return;
	}
	CloseHandle(hFile);

	DeleteDirectory(metadataDir.c_str());

	fs::path appxFeaturesDir = outDir / L"AppxFeatures";
	fs::create_directories(appxFeaturesDir);

	rapidxml::xml_document<char> doc;
	try
	{
		doc.parse<0>(const_cast<char*>(text.c_str()));
	}
	catch (rapidxml::parse_error&)
	{
		return;
	}

	auto Features = doc.first_node()->first_node("Features");
	if (!Features)
		return;
	auto Packages = doc.first_node()->first_node("Packages");
	if (!Packages)
		return;

	map<string, Package_t> mPackages;
	for (auto Package = Packages->first_node("Package"); Package; Package = Package->next_sibling("Package"))
	{
		string ID = Package->first_attribute("ID")->value();
		auto Payload = Package->first_node("Payload");
		auto PayloadItem = Payload->first_node("PayloadItem");

		char* pathAttr = PayloadItem->first_attribute("Path")->value();
		char* pathPtr = pathAttr + 22;
		while (*pathPtr && *pathPtr != '\\') ++pathPtr;
		if (*pathPtr == '\\') ++pathPtr;

		string fullPathStr = pathPtr;
		fs::path p(ForceToWstring(fullPathStr.c_str()));
		wstring fileName = p.filename().wstring();
		wstring dirPath = p.parent_path().wstring();
		if (!dirPath.empty() && dirPath.back() != L'\\') dirPath += L'\\';

		wstring Prefix = ForceToWstring(pathAttr + 17);
		for (auto& c : Prefix) if (c == L'\\') c = L'_';

		for (auto it = ctx.AppVector.begin(); it != ctx.AppVector.end(); ++it)
			if (_wcsicmp(it->GetPointer() + ctx.PathUUP.size(), fileName.c_str()) == 0)
			{
				ctx.AppVector.erase(it);
				break;
			}

		mPackages[ID] = { dirPath, move(Prefix), fileName };
	}

	vector<Feature_t> vFeatures;
	for (auto Feature = Features->first_node("Feature"); Feature; Feature = Feature->next_sibling("Feature"))
	{
		auto ID = Feature->first_attribute("FeatureID")->value();
		Feature_t f;
		f.ID = ID;
		auto Dependencies = Feature->first_node("Dependencies");
		if (Dependencies)
			for (auto Feature = Dependencies->first_node("Feature"); Feature; Feature = Feature->next_sibling("Feature"))
				f.vDependencies.push_back(Feature->first_attribute("FeatureID")->value());
		auto CustomInformation = Feature->first_node("CustomInformation");
		if (CustomInformation)
			for (auto CustomInfo = CustomInformation->first_node("CustomInfo"); CustomInfo; CustomInfo = CustomInfo->next_sibling("CustomInfo"))
				if (_stricmp(CustomInfo->first_attribute("Key")->value(), "licensedata") == 0)
				{
					f.LicenseData = CustomInfo->first_node()->value();
					break;
				}
		auto Packages = Feature->first_node("Packages");
		if (Packages)
			for (auto Package = Packages->first_node("Package"); Package; Package = Package->next_sibling("Package"))
				f.vPackages.push_back(Package->first_attribute("ID")->value());
		vFeatures.push_back(move(f));
	}

	for (size_t i = 0; i != vFeatures.size();)
	{
		auto& f = vFeatures[i];
		fs::path currentOutDir = appxFeaturesDir / ForceToWstring(f.ID.c_str());

		error_code ec;
		if (!fs::create_directories(currentOutDir, ec))
		{
			if (ec)
			{
				vFeatures.erase(vFeatures.begin() + i);
				continue;
			}
		}

		if (!f.LicenseData.empty())
		{
			fs::path licensePath = currentOutDir / L"License.xml";
			HANDLE hFile = CreateFileW(licensePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (hFile == INVALID_HANDLE_VALUE)
			{
				f.LicenseData.clear();
				continue;
			}

			DWORD dwWritten;
			if (!WriteFile(hFile, f.LicenseData.c_str(), static_cast<DWORD>(f.LicenseData.size()), &dwWritten, nullptr)
				|| dwWritten != f.LicenseData.size())
			{
				f.LicenseData.clear();
				CloseHandle(hFile);
				continue;
			}
			CloseHandle(hFile);
		}

		bool featureFailed = false;
		for (auto& pkg : f.vPackages)
		{
			try
			{
				const auto& package = mPackages.at(pkg);
				fs::path pkgPath = currentOutDir / package.Path;
				fs::create_directories(pkgPath, ec);
				if (ec) {
					featureFailed = true;
					break;
				}

				fs::path linkPath = pkgPath / package.File;
				fs::path targetPath = srcDir / package.File;

				if (!fs::exists(targetPath))
				{
					targetPath = srcDir / (package.Prefix + package.File);
				}

				if ((!fs::exists(targetPath) || !CreateSymbolicLinkW(linkPath.c_str(), targetPath.c_str(), 0))
					&& package.Path.find(L"Stub") == wstring::npos)
				{
					featureFailed = true;
					break;
				}
				else
				{
					auto it = find(ctx.AppVector.begin(), ctx.AppVector.end(), targetPath.wstring());
					if (it != ctx.AppVector.end())
						ctx.AppVector.erase(it);
				}
			}
			catch (...)
			{
				featureFailed = true;
				break;
			}
		}

		if (featureFailed)
		{
			DeleteDirectory(currentOutDir.c_str());
			vFeatures.erase(vFeatures.begin() + i);
		}
		else
		{
			++i;
		}
	}

	for (bool check = true; check;)
	{
		check = false;
		for (size_t i = 0; i != vFeatures.size();)
		{
			auto& f = vFeatures[i];
			bool missingDep = false;
			for (auto& dep : f.vDependencies)
			{
				auto it = find_if(vFeatures.begin(), vFeatures.end(), [&](const Feature_t& f) { return f.ID == dep; });
				if (it == vFeatures.end())
				{
					missingDep = true;
					break;
				}
			}
			if (missingDep)
			{
				vFeatures.erase(vFeatures.begin() + i);
				check = true;
			}
			else
			{
				++i;
			}
		}
	}

	auto& vAppxFeatures = ctx.AppxFeatures;
	vAppxFeatures.reserve(vFeatures.size());
	for (auto& f : vFeatures)
	{
		AppxFeature af;
		af.Feature = ForceToWstring(f.ID.c_str());
		af.Bundle = mPackages[f.vPackages[0]].File;
		af.bHasLicense = !f.LicenseData.empty();
		af.bInstall = true;
		af.bInstalled = false;
		vAppxFeatures.push_back(move(af));
	}

	for (size_t i = 0; i != vFeatures.size(); ++i)
		for (auto& j : vFeatures[i].vDependencies)
			for (size_t k = 0; k != vAppxFeatures.size(); ++k)
				if (vFeatures[k].ID == j)
				{
					vAppxFeatures[i].Dependencies.push_back(&vAppxFeatures[k]);
					break;
				}
}
