#pragma once 

#include "tui_colors.hpp"
#include "nebula_optimizer.hpp"
#include <iostream>
#include <string>
#include <windows.h>
#include <fstream>
#include <filesystem>
#include <vector>

enum Screen
{
	STARTUP,
	PROFILE_SELECT,
	CREATE_PROFILE,
	PROFILE_DASHBOARD,
	SETTINGS,
    LAUNCH_GAME,
	EXIT
};

class Tui {
    public:
        Tui();
        ~Tui();

        void run();
        void startup_screen();
        void init();
        void clear_screen();
        void create_profile_screen();
        void load_profile_screen();
        void settings_screen();
        void profile_dashboard_screen();
        void optimization_monitor_screen();
        void shutdown_screen();
        std::string get_profile_game_path(const std::string& profile_name);

    private:
        bool _is_running = false;
        Screen _current_screen = STARTUP;
        std::string _selected_profile;
        std::string _selected_profile_path;
        bool _is_optimizing = false;
        bool _shutdown_requested = false;
        std::vector<std::string> _active_optimizations;
};
