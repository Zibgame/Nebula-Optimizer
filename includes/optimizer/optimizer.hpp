#pragma once

#include <string>
#include <array>
#include <vector>
#include <mutex>
#include <memory>
#include <windows.h>
#include "saved_tweaks.hpp"
#include "tweak_catalog.hpp"

class Optimizer {
public:
    enum class TweakStatus {
        Off, On, Applied, Configured, RestartRequired, Skipped, AlreadyConfigured,
        Unsupported, AccessDenied, Failed, RestoreIncomplete
    };
    struct TweakSetting {
        std::string label;
        std::string category;
        bool enabled;
        TweakStatus status;
        SavedTweaks::Info durable;
        std::string value;
        bool adjustable = false;
    };
    Optimizer();
    ~Optimizer();

    // An empty path applies system/session optimizations without launching a game.
    bool optimize(const std::string& game_path, bool launch_if_missing = true);
    void restore();
    bool is_game_active(const std::string& game_path) const;
    bool restoration_succeeded() const;
    bool recovery_blocked() const;
    bool retry_recovery();

    bool is_optimized() const;
    bool has_game_process() const;
    DWORD game_pid() const;
    bool is_game_running();
    bool is_waiting_for_game() const;
    std::string game_status() const;
    const std::vector<std::string>& applied_tweaks() const;
    const std::vector<std::string>& failed_tweaks() const;
    const std::string& last_error() const;
    const std::string& restoration_status() const;
    std::vector<TweakSetting> tweak_settings() const;
    bool toggle_tweak(size_t index);
    bool enable_all_tweaks();
    bool adjust_tweak_value(size_t index, int direction);
    bool scan_saved(const std::string& game_path);
    bool has_baseline_scan(const std::string& game_path = {}) const;
    bool save_tweak(size_t index, const std::string& game_path,
                    bool confirmed, bool (*confirm_display)() = nullptr);
    bool unsave_tweak(size_t index);
    bool reapply_saved_tweak(size_t index, const std::string& game_path,
                             bool confirmed, bool (*confirm_display)() = nullptr);
    const std::string& saved_error() const;

private:
    struct RegistryState {
        HKEY root;
        std::string path;
        std::string name;
        DWORD type;
        std::vector<unsigned char> data;
        bool existed;
        DWORD applied_type;
        std::vector<unsigned char> applied_data;
        bool compare_and_swap;
    };

    struct BackgroundState {
        HANDLE process;
        DWORD pid;
        ULONGLONG created_at;
        DWORD priority_class;
        PROCESS_POWER_THROTTLING_STATE throttling;
        bool throttling_known;
        DWORD memory_priority;
        bool memory_known;
        bool priority_boost_disabled;
        bool boost_known;
        bool priority_touched;
        bool throttling_touched;
        bool memory_touched;
        bool boost_touched;
        bool compare_and_swap;
        DWORD applied_priority_class;
        DWORD applied_throttling_state;
        DWORD applied_throttling_control;
        DWORD applied_memory_priority;
        bool applied_boost_disabled;
    };

    struct PowerSettingState {
        GUID scheme;
        unsigned int kind;
        DWORD original;
        DWORD applied;
        bool compare_and_swap;
    };

    struct DisplayState {
        std::string device;
        DWORD frequency;
        DWORD width;
        DWORD height;
        DWORD bits_per_pel;
        DWORD orientation;
        DWORD fixed_output;
        LONG position_x;
        LONG position_y;
        DWORD applied_frequency;
    };

    struct AccessibilityState {
        FILTERKEYS filter{};
        STICKYKEYS sticky{};
        TOGGLEKEYS toggle{};
        DWORD filter_applied_flags = 0;
        DWORD sticky_applied_flags = 0;
        DWORD toggle_applied_flags = 0;
        bool filter_touched = false;
        bool sticky_touched = false;
        bool toggle_touched = false;
    };

    struct InputTuningState {
        FILTERKEYS filter{};
        FILTERKEYS filter_applied{};
        bool filter_touched = false;
        UINT delay = 0;
        UINT delay_applied = 0;
        bool delay_touched = false;
        UINT speed = 0;
        UINT speed_applied = 0;
        bool speed_touched = false;
        std::array<int, 3> mouse{};
        std::array<int, 3> mouse_applied{};
        bool mouse_touched = false;
    };

    struct AdvancedProcessState {
        DWORD pid = 0;
        ULONGLONG created_at = 0;
        std::vector<ULONG> cpu_sets;
        std::vector<ULONG> applied_cpu_sets;
        ULONG io_priority = 0;
        ULONG applied_io_priority = 0;
        bool cpu_sets_touched = false;
        bool io_touched = false;
    };

    struct ServiceState {
        std::string name;
        DWORD original_state = SERVICE_STOPPED;
        DWORD applied_state = SERVICE_STOPPED;
        bool touched = false;
    };

    struct DnsState {
        GUID interface_id{};
        std::wstring original;
        std::wstring applied;
        bool touched = false;
    };

    bool snapshot_registry(HKEY root, const char* path, const char* name,
                           DWORD applied_type, const void* applied_data,
                           DWORD applied_size, bool& already_configured);
    bool save_journal() const;
    void recover_journal();
    bool set_dword(HKEY root, const char* path, const char* name,
                   DWORD value, const char* label);
    bool set_string(HKEY root, const char* path, const char* name,
                    const std::string& value, const char* label);
    bool restore_registry();
    bool launch_game(const std::string& game_path, bool launch_if_missing);
    bool find_target_process();
    bool track_process_for_restore(HANDLE process, DWORD pid);
    void throttle_background_processes();
    bool restore_background_processes();
    bool set_ac_power_setting(unsigned int kind, DWORD value, size_t tweak,
                              const char* label);
    bool restore_ac_power_settings();
    bool begin_session_power_plan();
    bool restore_session_power_plan();
    void optimize_display_refresh(DWORD game_pid = 0);
    bool restore_display();
    void optimize_accessibility_hotkeys();
    bool restore_accessibility_hotkeys();
    void optimize_input_tuning();
    bool restore_input_tuning();
    void optimize_advanced_session();
    void tune_advanced_game_process(HANDLE process, DWORD pid);
    void tune_background_io();
    bool restore_advanced_processes();
    bool restore_services();
    bool pause_service(const char* name, size_t tweak);
    void request_timer_resolution();
    void release_timer_resolution();
    bool start_recovery_watchdog();
    void optimize_network(bool may_restart);
    bool restore_network();
    void tune_game_process(HANDLE process, bool tracked);
    void set_status(size_t index, TweakStatus status);
    void record(bool success, const char* label);
    bool save_preferences() const;
    void load_preferences();
    bool enabled(size_t index) const;

    bool _is_optimized;
    bool _power_request_active;
    HANDLE _game_process;
    DWORD _game_pid;
    std::string _game_directory;
    std::string _target_game_path;
    std::string _baseline_game_path;
    bool _waiting_for_game;
    ULONGLONG _launch_started;
    ULONGLONG _last_helper_scan;
    char _saved_power_plan_guid[64];
    std::string _session_power_plan_guid;
    std::vector<RegistryState> _registry_state;
    std::vector<BackgroundState> _background_state;
    std::vector<PowerSettingState> _power_settings;
    DisplayState _display_state{};
    bool _display_refresh_checked = false;
    AccessibilityState _accessibility_state{};
    InputTuningState _input_tuning_state{};
    std::vector<AdvancedProcessState> _advanced_processes;
    std::vector<ServiceState> _service_states;
    DnsState _dns_state{};
    std::string _network_device_instance;
    bool _network_restart_needed = false;
    bool _timer_resolution_active = false;
    bool _watchdog_started = false;
    DWORD _filter_repeat_ms = 10;
    DWORD _mmcss_reserve_percent = 10;
    DWORD _cpu_epp_percent = 0;
    DWORD _core_parking_percent = 100;
    DWORD _cpu_minimum_percent = 100;
    std::vector<std::string> _applied;
    std::vector<std::string> _failed;
    std::string _last_error;
    std::string _restoration_status;
    std::string _journal_path;
    std::string _preferences_path;
    std::unique_ptr<SavedTweaks> _saved_tweaks;
    std::array<bool, TWEAK_COUNT> _tweaks_enabled;
    std::array<TweakStatus, TWEAK_COUNT> _tweak_status;
    bool _recovery_failed;
    bool _restoration_succeeded;
    mutable std::recursive_mutex _state_mutex;
};
