#include "json.hpp"
#include "nebula_optimizer.hpp"

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <shlobj.h>
#include <windows.h>

using json = nlohmann::json;

namespace {

std::filesystem::path profiles_directory()
{
    wchar_t local_app_data[MAX_PATH * 4]{};
    const DWORD length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", local_app_data, MAX_PATH * 4);
    if (length > 0 && length < MAX_PATH * 4)
        return std::filesystem::path(local_app_data) / "NebulaOptimizer" / "profiles";
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr,
                                   SHGFP_TYPE_CURRENT, local_app_data)))
        return std::filesystem::path(local_app_data) / "NebulaOptimizer" / "profiles";
    return {};
}

void import_legacy_profiles(const std::filesystem::path& destination)
{
    static bool attempted = false;
    if (attempted || destination.empty())
        return;
    attempted = true;

    wchar_t module[MAX_PATH * 4]{};
    const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH * 4);
    if (!length || length >= MAX_PATH * 4)
        return;
    const auto legacy = std::filesystem::path(module).parent_path() /
                        "config" / "profiles";
    std::error_code error;
    if (!std::filesystem::is_directory(legacy, error))
        return;
    std::filesystem::create_directories(destination, error);
    if (error)
        return;
    for (const auto& entry : std::filesystem::directory_iterator(legacy, error)) {
        if (error)
            break;
        if (entry.is_regular_file(error) && entry.path().extension() == ".json")
            std::filesystem::copy_file(entry.path(),
                destination / entry.path().filename(),
                std::filesystem::copy_options::skip_existing, error);
        error.clear();
    }
}

std::filesystem::path active_profiles_directory()
{
    const auto directory = profiles_directory();
    import_legacy_profiles(directory);
    return directory;
}

} // namespace

static std::string	sanitize_filename(const std::string &profile_name)
{
	std::string	filename;
	for (unsigned char ch : profile_name)
	{
		if (ch < 32 || ch == '/' || ch == '\\' || ch == ':' || ch == '*' ||
			ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|')
			filename += '_';
		else if (ch == ' ')
			filename += '_';
		else
			filename += static_cast<char>(ch);
	}
	while (!filename.empty() && (filename.back() == '.' || filename.back() == ' '))
		filename.pop_back();
	return (filename);
}

bool	create_profile(const std::string &profile_name,
			const std::string &game_path)
{
	json		profile;
	std::ofstream	file;
	std::string	filename;
	const std::vector<std::string> profiles = get_profiles_list();
	const auto profiles_dir = active_profiles_directory();

	if (profiles_dir.empty() || profile_name.empty() || game_path.empty() ||
		std::find(profiles.begin(), profiles.end(), profile_name) != profiles.end())
		return (false);

	std::filesystem::create_directories(profiles_dir);

	filename = (profiles_dir / ("profile_" +
		sanitize_filename(profile_name) + ".json")).string();
	if (std::filesystem::exists(filename))
		return (false);

	profile["name"] = profile_name;
	profile["game_path"] = game_path;

	file.open(filename.c_str());
	if (!file.is_open())
		return (false);

	file << profile.dump(4);
	file.close();

	return (true);
}

bool	delete_profile(const std::string &profile_name)
{
	if (profile_name.empty())
		return (false);
	const auto profiles_dir = active_profiles_directory();
	if (profiles_dir.empty() || !std::filesystem::exists(profiles_dir))
		return (false);

	for (const auto &entry : std::filesystem::directory_iterator(profiles_dir))
	{
		if (!entry.is_regular_file() || entry.path().extension() != ".json")
			continue;
		try
		{
			std::ifstream file(entry.path());
			json profile;
			file >> profile;
			if (profile.value("name", entry.path().stem().string()) == profile_name)
				return (std::filesystem::remove(entry.path()));
		}
		catch (...) {}
	}
	return (false);
}

std::vector<std::string>	get_profiles_list()
{
	std::vector<std::string>	profiles;
	std::filesystem::path		profiles_dir;
	std::string			filename;

	profiles_dir = active_profiles_directory();

	if (profiles_dir.empty() || !std::filesystem::exists(profiles_dir))
		return (profiles);

	for (const auto &entry : std::filesystem::directory_iterator(profiles_dir))
	{
		if (!entry.is_regular_file())
			continue;

		if (entry.path().extension() != ".json")
			continue;

		try
		{
			std::ifstream file(entry.path());
			json profile;
			file >> profile;
			filename = profile.value("name", entry.path().stem().string());
			profiles.push_back(filename);
		}
		catch (...) {}
	}

	std::sort(profiles.begin(), profiles.end());
	return (profiles);
}

std::string	load_profile_game_path(const std::string &profile_name)
{
	const auto profiles_dir = active_profiles_directory();
	if (profiles_dir.empty() || !std::filesystem::exists(profiles_dir))
		return ("");

	for (const auto &entry : std::filesystem::directory_iterator(profiles_dir))
	{
		if (!entry.is_regular_file() || entry.path().extension() != ".json")
			continue;
		try
		{
			std::ifstream file(entry.path());
			json profile;
			file >> profile;
			if (profile.value("name", entry.path().stem().string()) == profile_name)
				return (profile.value("game_path", std::string()));
		}
		catch (...) {}
	}
	return ("");
}
