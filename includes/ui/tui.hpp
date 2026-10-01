#pragma once

#include "nebula_optimizer.hpp"
#include "optimizer.hpp"

#include <string>
#include <memory>
#include <atomic>

class TrayIcon;

enum Screen
{
    STARTUP,
    PROFILE_SELECT,
    CREATE_PROFILE,
    SETTINGS,
    LAUNCH_GAME,
    OPTIMIZATION_MONITOR,
    EXIT
};

class Tui {
public:
    Tui();
    ~Tui();

    void run();
    void emergency_restore();

private:
    void init();
    void clear_screen();
    void startup_screen();
    void profiles_screen();
    void create_profile_screen();
    void delete_profile_flow();
    void optimization_info_screen();
    void optimization_monitor_screen();
    void launch_current_mode();
    void pause(const std::string& message = "Press Enter...");

    std::string read_input(const std::string& prompt);
    void select_default_profile();

    Optimizer _optimizer;
    bool _is_running;
    Screen _current_screen;
    std::string _selected_profile;
    std::string _selected_profile_path;
    std::string _launch_path;
    std::string _scan_message;
    bool _attach_only = false;
    bool _suppress_auto_attach = false;
    std::unique_ptr<TrayIcon> _tray;
    std::atomic<bool> _restore_notice_sent{false};
};
