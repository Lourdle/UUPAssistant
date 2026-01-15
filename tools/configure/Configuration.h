#pragma once

#include <string>
#include <vector>
#include <map>

struct FeatureManager
{
	FeatureManager() = default;
	~FeatureManager() = default;

	bool Load(const char* FilePath);

	std::map<std::string, bool>::const_iterator begin() const { return Features.begin(); }
	std::map<std::string, bool>::const_iterator end() const { return Features.end(); }

private:
	std::map<std::string, bool> Features;
};

struct FeatureItem
{
	std::string Name;
	std::string Description;
	std::string Macro;
	std::string Value;
	bool Enable = false;
};

class Configuration
{
public:
	Configuration() = default;
	~Configuration() = default;

	// Load configuration from a file
	bool Load(const char* FilePath);

	// Get global_features.h content
	std::string GetFeatureHeader(const FeatureManager&);
	
private:
	std::map<std::string, FeatureItem> FeatureMap;
	bool UseCRLF = false;
};
