#pragma once

#include "tui_colors.hpp"
#include "json.hpp"
#include "optimizer.hpp"
# define PROFILES_DIR "config/profiles/"

using json = nlohmann::json;

bool	create_profile(const std::string &profile_name, const std::string &game_path);
bool	delete_profile(const std::string &profile_name);
std::vector<std::string>	get_profiles_list();