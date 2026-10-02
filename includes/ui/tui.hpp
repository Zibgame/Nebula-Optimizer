#pragma once

#include "nebula_optimizer.hpp"
#include "optimizer.hpp"
#include "background_apps.hpp"

#include <string>
#include <memory>
#include <atomic>
#include <vector>
#include <windows.h>

class TrayIcon;

enum Screen
{
    STARTUP,
    PROFILE_SELECT,
    CREATE_PROFILE,
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
    bool background_apps_screen();
    void delete_profile_flow();
    void optimization_monitor_screen();
    void launch_current_mode();
    void pause(const std::string& message = "Press Enter...");

    std::string read_input(const std::string& prompt);
    int select_menu(const std::string& heading,
                    const std::vector<std::string>& options,
                    size_t initial = 0);
    void select_default_profile();

    Optimizer _optimizer;
    BackgroundApps _background_apps;
    ClosedAppsSummary _closed_apps_summary;
    bool _is_running;
    Screen _current_screen;
    std::string _selected_profile;
    std::string _selected_profile_path;
    std::string _launch_path;
    std::string _scan_message;
    bool _attach_only = false;
    bool _suppress_auto_attach = false;
    size_t _home_cursor = 0;
    std::unique_ptr<TrayIcon> _tray;
    std::atomic<bool> _restore_notice_sent{false};
    CONSOLE_FONT_INFOEX _original_console_font{};
    CONSOLE_FONT_INFOEX _applied_console_font{};
    bool _console_font_changed = false;
};
