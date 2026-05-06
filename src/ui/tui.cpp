#include "tui.hpp"

#include <iostream>
#include <string>

void Tui::clear_screen() {
    std::cout << "\033[2J\033[H";
}

void	Tui::init()
{
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
	system("chcp 65001 > nul");
	_is_running = true;
}

Tui::Tui()
{
    init();
}

Tui::~Tui()
{
    clear_screen();
}

void	Tui::startup_screen()
{
	int	choice;

	clear_screen();

	std::cout << LIGHT_PURPLE << BOLD;
	std::cout << "┌──────────────────────────────────────────────────────────────┐" << std::endl;
	std::cout << "│░█▀█░█▀▀░█▀▄░█░█░█░░░█▀█░░░█▀█░█▀█░▀█▀░▀█▀░█▄█░▀█▀░▀▀█░█▀▀░█▀▄│" << std::endl;
	std::cout << "│░█░█░█▀▀░█▀▄░█░█░█░░░█▀█░░░█░█░█▀▀░░█░░░█░░█░█░░█░░▄▀░░█▀▀░█▀▄│" << std::endl;
	std::cout << "│░▀░▀░▀▀▀░▀▀░░▀▀▀░▀▀▀░▀░▀░░░▀▀▀░▀░░░░▀░░▀▀▀░▀░▀░▀▀▀░▀▀▀░▀▀▀░▀░▀│" << std::endl;
	std::cout << "└──────────────────────────────────────────────────────────────┘" << std::endl;
	std::cout << RESET << std::endl;

	std::cout << PURPLE << BOLD;
	std::cout << "                 [ Competitive Gaming Optimizer ]" << std::endl;
	std::cout << RESET << std::endl;

	std::cout << CYAN << BOLD;
	std::cout << "┌──────────────────────────────────────────────────────────────┐" << std::endl;
	std::cout << "│                      SELECT AN OPTION                        │" << std::endl;
	std::cout << "├──────────────────────────────────────────────────────────────┤" << std::endl;
	std::cout << RESET;

	std::cout << WHITE;
	std::cout << "│  [1] Add New Profile                                         │" << std::endl;
	std::cout << "│  [2] Load Existing Profile                                   │" << std::endl;
	std::cout << "│  [3] Settings                                                │" << std::endl;
	std::cout << "│  [0] Exit                                                    │" << std::endl;

	std::cout << CYAN << BOLD;
	std::cout << "└──────────────────────────────────────────────────────────────┘" << std::endl;
	std::cout << RESET << std::endl;

	std::cout << LIGHT_PURPLE << BOLD << "Enter your choice: " << RESET;
	std::cin >> choice;

	if (choice == 1)
		_current_screen = CREATE_PROFILE;
	else if (choice == 2)
		_current_screen = PROFILE_SELECT;
	else if (choice == 3)
		_current_screen = SETTINGS;
	else if (choice == 0)
		_current_screen = EXIT;
}

void	Tui::create_profile_screen()
{
	std::string	profile_name;
	std::string	game_path;
	bool		success;

	clear_screen();

	std::cout << LIGHT_PURPLE << BOLD;
	std::cout << "┌──────────────────────────────────────────────────────────────┐" << std::endl;
	std::cout << "│                     ADD NEW PROFILE                          │" << std::endl;
	std::cout << "└──────────────────────────────────────────────────────────────┘" << std::endl;
	std::cout << RESET << std::endl;

	std::cout << CYAN << "Enter profile name: " << RESET;
	std::getline(std::cin >> std::ws, profile_name);

	std::cout << CYAN << "Drag & drop game executable (.exe): " << RESET;
	std::getline(std::cin, game_path);

	if (!game_path.empty() && game_path.front() == '"' && game_path.back() == '"')
		game_path = game_path.substr(1, game_path.size() - 2);

	if (profile_name.empty() || game_path.empty())
	{
		std::cout << RED << "Invalid profile data." << RESET << std::endl;
		std::cout << YELLOW << "Press Enter to return..." << RESET;
		std::cin.get();
		_current_screen = STARTUP;
		return;
	}

	success = create_profile(profile_name, game_path);

	if (!success)
	{
		std::cout << RED << std::endl;
		std::cout << "Failed to create profile." << std::endl;
		std::cout << RESET;
		std::cout << YELLOW << "Press Enter to return..." << RESET;
		std::cin.get();
		_current_screen = STARTUP;
		return;
	}

	std::cout << GREEN << std::endl;
	std::cout << "Profile Created Successfully!" << std::endl;
	std::cout << "Name: " << profile_name << std::endl;
	std::cout << "Executable: " << game_path << std::endl;
	std::cout << RESET << std::endl;

	std::cout << YELLOW << "Press Enter to continue..." << RESET;
	std::cin.get();

	_current_screen = STARTUP;
}

void	Tui::load_profile_screen()
{
	std::vector<std::string>	profiles;
	std::string			input;
	int				choice;

	profiles = get_profiles_list();

	clear_screen();

	std::cout << LIGHT_PURPLE << BOLD;
	std::cout << "┌──────────────────────────────────────────────────────────────┐" << std::endl;
	std::cout << "│                  LOAD EXISTING PROFILE                       │" << std::endl;
	std::cout << "└──────────────────────────────────────────────────────────────┘" << std::endl;
	std::cout << RESET << std::endl;

	if (profiles.empty())
	{
		std::cout << RED << "No profiles found." << RESET << std::endl;
		std::cout << YELLOW << "Press Enter to return..." << RESET;
		std::cin.get();
		_current_screen = STARTUP;
		return;
	}

	std::cout << CYAN << BOLD;
	std::cout << "┌──────────────────────────────────────────────────────────────┐" << std::endl;
	std::cout << "│                     AVAILABLE PROFILES                       │" << std::endl;
	std::cout << "├──────────────────────────────────────────────────────────────┤" << std::endl;
	std::cout << RESET;

	choice = 0;
	while (choice < (int)profiles.size())
	{
		std::cout << WHITE;
		std::cout << "│  [" << choice + 1 << "] " << profiles[choice];

		if (profiles[choice].length() < 52)
			std::cout << std::string(52 - profiles[choice].length(), ' ');

		std::cout << "│" << std::endl;
		choice++;
	}

	std::cout << WHITE;
	std::cout << "│  [0] Return to Main Menu                                     │" << std::endl;

	std::cout << CYAN << BOLD;
	std::cout << "└──────────────────────────────────────────────────────────────┘" << std::endl;
	std::cout << RESET << std::endl;

	std::cout << LIGHT_PURPLE << BOLD << "Select profile: " << RESET;
	std::getline(std::cin >> std::ws, input);

	if (input == "0")
	{
		_current_screen = STARTUP;
		return;
	}

	try
	{
		choice = std::stoi(input);
	}
	catch (...)
	{
		_current_screen = STARTUP;
		return;
	}

	if (choice < 1 || choice > (int)profiles.size())
	{
		_current_screen = STARTUP;
		return;
	}

	_selected_profile = profiles[choice - 1];
	_selected_profile_path = get_profile_game_path(_selected_profile);
	_current_screen = PROFILE_DASHBOARD;
}

std::string	Tui::get_profile_game_path(const std::string& profile_name)
{
	std::ifstream	file("config/profiles/" + profile_name + ".json");
	std::string		line;
	std::string		path;
	size_t			start;
	size_t			end;

	if (!file.is_open())
		return ("Unknown path");

	while (std::getline(file, line))
	{
		start = line.find("\"game_path\"");
		if (start != std::string::npos)
		{
			start = line.find(":", start);
			start = line.find("\"", start);
			end = line.find("\"", start + 1);
			if (start != std::string::npos && end != std::string::npos)
				path = line.substr(start + 1, end - start - 1);
			break;
		}
	}

	file.close();

	if (path.empty())
		return ("Unknown path");
	return (path);
}

// ─────────────────────────────────────────────
//  Settings Screen
// ─────────────────────────────────────────────
 
void Tui::settings_screen()
{
	int choice;
 
	clear_screen();
    clear_screen();
    clear_screen();
 
	std::cout << LIGHT_PURPLE << BOLD;
	std::cout << "  ┌────────────────────────────────────────────────────────────────┐\n";
	std::cout << "  │                        SETTINGS                                │\n";
	std::cout << "  └────────────────────────────────────────────────────────────────┘\n";
	std::cout << RESET << "\n";
 
	std::cout << CYAN << BOLD;
	std::cout << "  ┌────────────────────────────────────────────────────────────────┐\n";
	std::cout << "  │  General                                                       │\n";
	std::cout << "  ├────────────────────────────────────────────────────────────────┤\n";
	std::cout << RESET;
 
	std::cout << WHITE;
	std::cout << "  │    [1]  Default optimization mode       " << DIM << "[ Coming soon ]" << RESET << WHITE << "       │\n";
	std::cout << "  │    [2]  Auto-update configs             " << DIM << "[ Coming soon ]" << RESET << WHITE << "       │\n";
	std::cout << "  │    [3]  Theme / color palette           " << DIM << "[ Coming soon ]" << RESET << WHITE << "       │\n";
	std::cout << "  │    [4]  Startup behavior                " << DIM << "[ Coming soon ]" << RESET << WHITE << "       │\n";
	std::cout << "  │                                                                │\n";
 
	std::cout << CYAN << BOLD;
	std::cout << "  ├────────────────────────────────────────────────────────────────┤\n";
	std::cout << "  │  Data                                                          │\n";
	std::cout << "  ├────────────────────────────────────────────────────────────────┤\n";
	std::cout << RESET;
 
	std::cout << WHITE;
	std::cout << "  │    [5]  Backup all configs              " << DIM << "[ Coming soon ]" << RESET << WHITE << "       │\n";
	std::cout << "  │    [6]  Reset ALL profiles                                     │\n";
	std::cout << "  │                                                                │\n";
	std::cout << "  │    [0]  Back                                                   │\n";
	std::cout << "  │                                                                │\n";
 
	std::cout << CYAN << BOLD;
	std::cout << "  └────────────────────────────────────────────────────────────────┘\n";
	std::cout << RESET << "\n";
 
	std::cout << LIGHT_PURPLE << BOLD << "  ▸ Enter choice: " << RESET;
	std::cin >> choice;
 
	if (choice == 6)
	{
		// Reset all profiles
		std::cout << "\n" << RED << BOLD;
		std::cout << "  Delete ALL profiles? This cannot be undone.\n";
		std::cout << "  Type 'YES' to confirm: " << RESET;
 
		std::string confirm;
		std::getline(std::cin >> std::ws, confirm);
 
		if (confirm == "YES")
		{
			std::vector<std::string> all = get_profiles_list();
			int deleted = 0;
			for (const auto &p : all)
			{
				if (delete_profile(p)) deleted++;
			}
			std::cout << GREEN << BOLD << "\n  [✓] Deleted " << deleted << " profile(s).\n" << RESET;
		}
		else
		{
			std::cout << YELLOW << "\n  Cancelled.\n" << RESET;
		}
 
		std::cout << YELLOW << "\n  Press Enter to continue..." << RESET;
		std::cin.get();
		_current_screen = SETTINGS;   // Stay in settings
		return;
	}
 
	if (choice >= 1 && choice <= 5)
	{
		std::cout << "\n" << YELLOW << BOLD << "  [ Coming soon — this feature is not yet implemented. ]\n" << RESET;
		std::cout << YELLOW << "\n  Press Enter to continue..." << RESET;
		std::cin.ignore();
		std::cin.get();
		_current_screen = SETTINGS;
		return;
	}
 
	_current_screen = STARTUP;
}

// ─────────────────────────────────────────────
//  Profile Dashboard Screen
// ─────────────────────────────────────────────
 
void Tui::profile_dashboard_screen()
{
	int choice;
 
	clear_screen();
 
	// ── Header ────────────────────────────────
	std::cout << LIGHT_PURPLE << BOLD;
	std::cout << "  ┌────────────────────────────────────────────────────────────────┐\n";
	std::cout << "  │                     PROFILE DASHBOARD                          │\n";
	std::cout << "  ├────────────────────────────────────────────────────────────────┤\n";
	std::cout << RESET;
 
	std::string name_row = "  │  ✦  " + _selected_profile;
	int pad = 66 - (int)name_row.size();
	if (pad < 1) pad = 1;
	std::cout << LIGHT_PURPLE << BOLD << name_row << std::string(pad, ' ') << "│\n" << RESET;
 
	std::string path_display = _selected_profile_path;
	if (path_display.size() > 52) path_display = "..." + path_display.substr(path_display.size() - 49);
	std::string path_row = "  │     " + path_display;
	pad = 66 - (int)path_row.size();
	if (pad < 1) pad = 1;
	std::cout << DIM << WHITE << path_row << std::string(pad, ' ') << "│\n" << RESET;
 
	// ── Menu ──────────────────────────────────
	std::cout << CYAN << BOLD;
	std::cout << "  ├────────────────────────────────────────────────────────────────┤\n";
	std::cout << RESET;
 
	std::cout << WHITE;
	std::cout << "  │                                                                │\n";
	std::cout << "  │    [1]  Launch Game with Optimization                          │\n";
	std::cout << "  │                                                                │\n";
	std::cout << "  │    [0]  Back                                                   │\n";
	std::cout << "  │                                                                │\n";
 
	std::cout << CYAN << BOLD;
	std::cout << "  └────────────────────────────────────────────────────────────────┘\n";
	std::cout << RESET << "\n";
 
	std::cout << LIGHT_PURPLE << BOLD << "  ▸ Enter choice: " << RESET;
	std::cin >> choice;
 
	if      (choice == 1)  _current_screen = LAUNCH_GAME;
	else if (choice == 0)  _current_screen = PROFILE_SELECT;
}

void	Tui::run()
{
	Optimizer optimizer;
	while (_is_running)
	{
		if (_current_screen == STARTUP)
			startup_screen();
        else if (_current_screen == CREATE_PROFILE)
            create_profile_screen();
        else if (_current_screen == PROFILE_SELECT)
            load_profile_screen();
        else if (_current_screen == SETTINGS)
            settings_screen();
		else if (_current_screen == PROFILE_DASHBOARD)
			profile_dashboard_screen();
		else if (_current_screen == LAUNCH_GAME)
		{
			_current_screen = STARTUP;
			optimizer.optimize(_selected_profile_path);
		}
		else if (_current_screen == EXIT)
		{
			_is_running = false;
			optimizer.restore();
			Sleep(1000);
			clear_screen();
			std::cout << GREEN << "Thank you for using Nebula Optimizer! Goodbye!" << RESET << std::endl;
			Sleep(2000);
		}
		else
		{
			_is_running = false;
			optimizer.restore();
			Sleep(1000);
			clear_screen();
			std::cout << RED << "An unexpected error occurred. Exiting..." << RESET << std::endl;
			Sleep(2000);
		}
	}
}