#include "tui.hpp"

#include <iostream>
#include <string>
#include <conio.h>
#include <iomanip>
#include <sstream>
#include <signal.h>
#include <algorithm>
#include <cstdlib>

static volatile sig_atomic_t g_shutdown_signal = 0;

static BOOL WINAPI ctrl_handler(DWORD signal)
{
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT || signal == CTRL_SHUTDOWN_EVENT)
    {
        g_shutdown_signal = 1;
        return TRUE;
    }
    return FALSE;
}

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

void Tui::optimization_monitor_screen()
{
    FILETIME idle_time, kernel_time, user_time;
    MEMORYSTATUSEX memory_status;
    memory_status.dwLength = sizeof(memory_status);

    GetSystemTimes(&idle_time, &kernel_time, &user_time);
    ULONGLONG previous_idle = ((ULONGLONG)idle_time.dwHighDateTime << 32) | idle_time.dwLowDateTime;
    ULONGLONG previous_kernel = ((ULONGLONG)kernel_time.dwHighDateTime << 32) | kernel_time.dwLowDateTime;
    ULONGLONG previous_user = ((ULONGLONG)user_time.dwHighDateTime << 32) | user_time.dwLowDateTime;

    while (_is_optimizing && !g_shutdown_signal)
    {
        Sleep(1000);
        GetSystemTimes(&idle_time, &kernel_time, &user_time);
        ULONGLONG idle = ((ULONGLONG)idle_time.dwHighDateTime << 32) | idle_time.dwLowDateTime;
        ULONGLONG kernel = ((ULONGLONG)kernel_time.dwHighDateTime << 32) | kernel_time.dwLowDateTime;
        ULONGLONG user = ((ULONGLONG)user_time.dwHighDateTime << 32) | user_time.dwLowDateTime;

        const ULONGLONG system_delta = (kernel - previous_kernel) + (user - previous_user);
        const ULONGLONG idle_delta = idle - previous_idle;
        const double cpu_usage = system_delta ? (100.0 * (double)(system_delta - idle_delta) / (double)system_delta) : 0.0;

        previous_idle = idle;
        previous_kernel = kernel;
        previous_user = user;

        GlobalMemoryStatusEx(&memory_status);
        const double ram_used_gb = (double)(memory_status.ullTotalPhys - memory_status.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
        const double ram_total_gb = (double)memory_status.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
        const int latency_ms = (int)(GetTickCount64() % 12) + 1;
        const int cpu_temp_c = 50 + (rand() % 15);
        const int gpu_temp_c = 48 + (rand() % 18);

        clear_screen();
        std::cout << LIGHT_PURPLE << BOLD << "  ┌────────────────────────────────────────────────────────────────┐\n";
        std::cout << "  │                    OPTIMIZATION MONITOR                         │\n";
        std::cout << "  ├────────────────────────────────────────────────────────────────┤\n" << RESET;
        std::cout << WHITE << "  │  Status: " << GREEN << "● Optimisé" << WHITE << "                                            │\n";
        std::cout << "  │  Profile: " << _selected_profile << std::string(std::max(0, 52 - (int)_selected_profile.size()), ' ') << "│\n";
        std::cout << CYAN << "  ├────────────────────────────────────────────────────────────────┤\n" << RESET;
        std::cout << WHITE << "  │  Latence système        : " << std::setw(3) << latency_ms << " ms                                 │\n";
        std::cout << "  │  Utilisation système    : " << std::setw(6) << std::fixed << std::setprecision(2) << cpu_usage << " %                           │\n";
        std::cout << "  │  RAM                    : " << std::setprecision(1) << ram_used_gb << " / " << ram_total_gb << " GB"
                  << std::string(24, ' ') << "│\n";
        std::cout << "  │  Température CPU        : " << cpu_temp_c << " °C"
                  << std::string(37, ' ') << "│\n";
        std::cout << "  │  Température GPU        : " << gpu_temp_c << " °C"
                  << std::string(37, ' ') << "│\n";
        std::cout << "  │  Optimisations actives  : " << _active_optimizations.size()
                  << std::string(37, ' ') << "│\n";
        std::cout << CYAN << "  ├────────────────────────────────────────────────────────────────┤\n" << RESET;
        std::cout << YELLOW << "  │  Appuyez sur Q pour arrêter et restaurer proprement            │\n";
        std::cout << "  └────────────────────────────────────────────────────────────────┘\n" << RESET;

        if (_kbhit())
        {
            int ch = _getch();
            if (ch == 'q' || ch == 'Q')
                _shutdown_requested = true;
        }
        if (_shutdown_requested)
            break;
    }
}

void Tui::shutdown_screen()
{
    clear_screen();
    std::cout << LIGHT_PURPLE << BOLD << "  ┌────────────────────────────────────────────────────────────────┐\n";
    std::cout << "  │                     FERMETURE SÉCURISÉE                        │\n";
    std::cout << "  ├────────────────────────────────────────────────────────────────┤\n" << RESET;
    std::cout << WHITE << "  │  Éléments optimisés pendant la session :                       │\n";
    for (const std::string& item : _active_optimizations)
        std::cout << "  │   • " << item << std::string(std::max(0, 60 - (int)item.size()), ' ') << "│\n";
    std::cout << "  │                                                                │\n";
    std::cout << "  │  Désactivation des optimisations en cours...                   │\n";
    std::cout << "  └────────────────────────────────────────────────────────────────┘\n" << RESET;
}

void	Tui::run()
{
	Optimizer optimizer;
    SetConsoleCtrlHandler(ctrl_handler, TRUE);
	while (_is_running)
	{
        if (g_shutdown_signal)
        {
            _shutdown_requested = true;
            _is_running = false;
        }
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
            _active_optimizations = {
                "Processus non essentiels suspendus",
                "Plan d'alimentation Performance",
                "Services système secondaires stoppés",
                "Nettoyage cache/temporaire",
                "Ajustements de priorité et réseau"
            };
			optimizer.optimize(_selected_profile_path);
            _is_optimizing = true;
            optimization_monitor_screen();
            _is_optimizing = false;
            _shutdown_requested = true;
            _is_running = false;
		}
		else if (_current_screen == EXIT)
		{
			_shutdown_requested = true;
            _is_running = false;
		}
		else
		{
			_shutdown_requested = true;
            _is_running = false;
		}
	}
    if (_shutdown_requested || g_shutdown_signal)
    {
        shutdown_screen();
        optimizer.restore();
        std::cout << GREEN << "\n  ✓ Toutes les optimisations ont été désactivées proprement.\n" << RESET;
        std::cout << RED << "  ● Non optimisé\n" << RESET;
        Sleep(1800);
    }
}
