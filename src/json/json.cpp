#include "json.hpp"
#include "nebula_optimizer.hpp"

#include <fstream>
#include <filesystem>

using json = nlohmann::json;

static std::string	sanitize_filename(const std::string &profile_name)
{
	std::string	filename;
	size_t		i;

	filename = profile_name;
	i = 0;
	while (i < filename.length())
	{
		if (filename[i] == ' ')
			filename[i] = '_';
		i++;
	}
	return (filename);
}

bool	create_profile(const std::string &profile_name,
			const std::string &game_path)
{
	json		profile;
	std::ofstream	file;
	std::string	filename;

	if (profile_name.empty() || game_path.empty())
		return (false);

	std::filesystem::create_directories(PROFILES_DIR);

	filename = PROFILES_DIR + sanitize_filename(profile_name) + ".json";

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
	std::string	filename;

	if (profile_name.empty())
		return (false);

	filename = PROFILES_DIR + sanitize_filename(profile_name) + ".json";

	if (!std::filesystem::exists(filename))
		return (false);

	return (std::filesystem::remove(filename));
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

		filename = entry.path().stem().string();

		size_t i = 0;
		while (i < filename.length())
		{
			if (filename[i] == '_')
				filename[i] = ' ';
			i++;
		}

		profiles.push_back(filename);
	}

	return (profiles);
}
