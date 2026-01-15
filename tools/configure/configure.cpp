#include "pch.h"
#include "Configuration.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

int main()
{
	std::cout << "Lourdle Configuration Tool\n";

	fs::path ConfigPath = "configuration/features.json";
	fs::path EnablePath = "configuration/features.enable";
	fs::path HeaderPath = "configuration/global_features.h";

	Configuration config;
	if (!config.Load(ConfigPath.string().c_str()))
	{
		return 1;
	}

	FeatureManager Mgr;
	if (!Mgr.Load(EnablePath.string().c_str()))
	{
		std::cout << "Use default settings.\n";
	}

	std::string HeaderContent = config.GetFeatureHeader(Mgr);

	// Check if file exists and content matches
	if (fs::exists(HeaderPath))
	{
		std::ifstream inFile(HeaderPath, std::ios::binary | std::ios::ate);
		if (inFile)
		{
			auto size = inFile.tellg();
			if (size == HeaderContent.size())
			{
				inFile.seekg(0);
				std::string existingContent(size, '\0');
				if (inFile.read(&existingContent[0], size))
				{
					if (existingContent == HeaderContent)
					{
						std::cout << "No changes detected in global_features.h. Exiting.\n";
						return 0;
					}
				}
			}
		}
	}

	std::ofstream OutFile(HeaderPath, std::ios::binary | std::ios::trunc);
	if (!OutFile)
	{
		std::cerr << "Failed to create global_features.h\n";
		return 1;
	}

	OutFile.write(HeaderContent.c_str(), HeaderContent.size());
	if (!OutFile)
	{
		std::cerr << "Failed to write to global_features.h\n";
		return 1;
	}

	std::cout << "The file global_features.h has been updated successfully.\n";
	return 0;
}
