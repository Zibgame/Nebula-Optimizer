#include "json.hpp"
#include "nebula_optimizer.hpp"

#include <fstream>
#include <filesystem>
#include <algorithm>

using json = nlohmann::json;

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

	if (profile_name.empty() || game_path.empty() ||
		std::find(profiles.begin(), profiles.end(), profile_name) != profiles.end())
		return (false);

	std::filesystem::create_directories(PROFILES_DIR);

	filename = std::string(PROFILES_DIR) + "profile_" +
		sanitize_filename(profile_name) + ".json";
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
	if (!std::filesystem::exists(PROFILES_DIR))
		return (false);

	for (const auto &entry : std::filesystem::directory_iterator(PROFILES_DIR))
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

	profiles_dir = PROFILES_DIR;

	if (!std::filesystem::exists(profiles_dir))
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
	if (!std::filesystem::exists(PROFILES_DIR))
		return ("");

	for (const auto &entry : std::filesystem::directory_iterator(PROFILES_DIR))
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
