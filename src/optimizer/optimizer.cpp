#include "optimizer.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlobj.h>
#include <powrprof.h>
#include <objbase.h>
#include "json.hpp"

namespace {

constexpr const char* MMCSS_PROFILE =
    "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile";
constexpr const char* MMCSS_GAMES =
    "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games";
constexpr const char* TWEAK_IDS[41] = {
    "power_plan", "prevent_sleep", "game_dvr", "background_capture",
    "game_bar_popup", "game_mode", "mmcss_network", "mmcss_reserve",
    "mmcss_priority", "mmcss_scheduling", "foreground_cpu",
    "background_onedrive", "gpu_preference", "game_ecoqos", "game_priority",
    "game_bar_controller", "recording_hotkey", "history_hotkey",
    "game_bar_hotkey", "background_indexer", "background_widgets",
    "background_phone_link", "background_adobe", "ac_cpu_epp",
    "ac_pcie_aspm", "ac_cpu_boost", "game_dynamic_boost",
    "game_memory_normal", "display_max_refresh", "memory_cloud",
    "memory_indexer", "memory_widgets", "memory_phone_link",
    "memory_adobe", "updater_cpu", "updater_ecoqos",
    "updater_memory", "ac_core_parking", "ac_wifi_performance",
    "diagnostic_usb_suspend", "accessibility_hotkeys"
};
constexpr const char* TWEAK_LABELS[41] = {
    "Temporary session power plan", "Prevent sleep while gaming",
    "Disable Game DVR", "Disable background capture",
    "Hide Game Bar startup popup", "Enable Windows Game Mode",
    "Remove MMCSS network throttle", "MMCSS reserve: 10%",
    "MMCSS game priority", "MMCSS game scheduling",
    "Favor foreground CPU", "Reduce cloud sync priority",
    "Prefer high-performance GPU", "Disable game EcoQoS",
    "High game CPU priority", "Disable controller Game Bar shortcut",
    "Disable recording hotkey", "Disable replay hotkey",
    "Disable Game Bar hotkey", "Reduce search indexing priority",
    "Reduce Widgets priority", "Reduce Phone Link priority",
    "Reduce Adobe helper priority", "AC CPU performance preference",
    "AC PCIe link power saving off", "AC CPU boost mode",
    "Game dynamic priority boost", "Game memory priority normal",
    "Highest display refresh rate", "Cloud sync memory priority",
    "Indexer memory priority", "Widgets memory priority",
    "Phone Link memory priority", "Adobe memory priority",
    "Updater CPU priority", "Updater EcoQoS",
    "Updater memory priority", "[Exp] Core parking off",
    "AC Wi-Fi performance", "[Diag] USB selective suspend off",
    "Accessibility hotkeys off"
};
constexpr const char* TWEAK_CATEGORIES[41] = {
    "CPU & Power", "CPU & Power", "Input & Capture", "Input & Capture",
    "Input & Capture", "CPU & Power", "Background", "CPU & Power",
    "CPU & Power", "CPU & Power", "CPU & Power", "Background",
    "GPU & Display", "CPU & Power", "CPU & Power",
    "Input & Capture", "Input & Capture", "Input & Capture",
    "Input & Capture", "Background", "Background", "Background",
    "Background", "CPU & Power", "GPU & Display",
    "CPU & Power", "CPU & Power", "Memory", "GPU & Display",
    "Memory", "Memory", "Memory", "Memory", "Memory",
    "Background", "Background", "Memory",
    "CPU & Power", "CPU & Power", "Input & Capture", "Input & Capture"
};

size_t tweak_index_for_label(const char* label)
{
    struct Match { const char* label; size_t index; };
    static const Match matches[] = {
        {"High-performance power plan", 0}, {"Sleep paused during session", 1},
        {"Game DVR off", 2}, {"Background capture off", 3},
        {"Game Bar popup off", 4}, {"Game Mode allowed", 5},
        {"Game Mode on", 5}, {"MMCSS network throttling off", 6},
        {"MMCSS system reserve: 10%", 7}, {"MMCSS game priority", 8},
        {"MMCSS game scheduling", 9}, {"Foreground CPU scheduling", 10},
        {"High-performance GPU preference", 12},
        {"Game power throttling off", 13}, {"Game CPU priority: high", 14},
        {"Controller Game Bar shortcut off", 15},
        {"Recording shortcut off", 16}, {"Replay shortcut off", 17},
        {"Game Bar shortcut off", 18},
        {"AC CPU performance preference", 23},
        {"AC PCIe link power saving off", 24}, {"AC CPU boost mode", 25},
        {"Game dynamic priority boost", 26},
        {"Game memory priority normal", 27}
    };
    for (const Match& match : matches)
        if (std::strcmp(match.label, label) == 0)
            return match.index;
    return 41;
}

constexpr GUID CPU_EPP = {0x36687f9e, 0xe3a5, 0x4dbf,
                          {0xb1, 0xdc, 0x15, 0xeb, 0x38, 0x1c, 0x68, 0x63}};
constexpr GUID CPU_SUBGROUP = {0x54533251, 0x82be, 0x4824,
                               {0x96, 0xc1, 0x47, 0xb6, 0x0b, 0x74, 0x0d, 0x00}};
constexpr GUID PCIE_ASPM = {0xee12f906, 0xd277, 0x404b,
                            {0xb6, 0xda, 0xe5, 0xfa, 0x1a, 0x57, 0x6d, 0xf5}};
constexpr GUID PCIE_SUBGROUP = {0x501a4d13, 0x42af, 0x4429,
                                {0x9f, 0xd1, 0xa8, 0x21, 0x8c, 0x26, 0x8e, 0x20}};
constexpr GUID CPU_BOOST = {0xbe337238, 0x0d82, 0x4146,
                            {0xa9, 0x60, 0x4f, 0x37, 0x49, 0xd4, 0x70, 0xc7}};
constexpr GUID CORE_PARKING = {0x0cc5b647, 0xc1df, 0x4637,
                               {0x89, 0x1a, 0xde, 0xc3, 0x5c, 0x31, 0x85, 0x83}};
constexpr GUID CORE_PARKING_1 = {0x0cc5b647, 0xc1df, 0x4637,
                                 {0x89, 0x1a, 0xde, 0xc3, 0x5c, 0x31, 0x85, 0x84}};
constexpr GUID WIFI_SUBGROUP = {0x19cbb8fa, 0x5279, 0x450e,
                                {0x9f, 0xac, 0x8a, 0x3d, 0x5f, 0xed, 0xd0, 0xc1}};
constexpr GUID WIFI_POWER = {0x12bbebe6, 0x58d6, 0x4636,
                             {0x95, 0xbb, 0x32, 0x17, 0xef, 0x86, 0x7c, 0x1a}};
constexpr GUID USB_SUBGROUP = {0x2a737441, 0x1930, 0x4402,
                               {0x8d, 0x77, 0xb2, 0xbe, 0xbb, 0xa3, 0x08, 0xa3}};
constexpr GUID USB_SUSPEND = {0x48e6b7a6, 0x50f5, 0x4782,
                              {0xa5, 0xd4, 0x53, 0xbb, 0x8f, 0x07, 0xe2, 0x26}};
constexpr GUID HIGH_PERFORMANCE_SCHEME = {0x8c5e7fda, 0xe8bf, 0x4a96,
    {0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c}};

const GUID* power_subgroup(unsigned int kind)
{
    return kind == 0 || kind == 2 || kind == 3 || kind == 4 ? &CPU_SUBGROUP :
           kind == 1 ? &PCIE_SUBGROUP :
           kind == 5 ? &WIFI_SUBGROUP :
           kind == 6 ? &USB_SUBGROUP : nullptr;
}

const GUID* power_setting(unsigned int kind)
{
    return kind == 0 ? &CPU_EPP : kind == 1 ? &PCIE_ASPM :
           kind == 2 ? &CPU_BOOST : kind == 3 ? &CORE_PARKING :
           kind == 4 ? &CORE_PARKING_1 : kind == 5 ? &WIFI_POWER :
           kind == 6 ? &USB_SUSPEND : nullptr;
}

std::string guid_text(const GUID& guid)
{
    wchar_t wide[40]{};
    char narrow[40]{};
    if (!StringFromGUID2(guid, wide, 40) ||
        !WideCharToMultiByte(CP_ACP, 0, wide, -1, narrow, 40, nullptr, nullptr))
        return {};
    return narrow;
}

bool parse_guid(const std::string& value, GUID& guid)
{
    wchar_t wide[40]{};
    if (!MultiByteToWideChar(CP_ACP, 0, value.c_str(), -1, wide, 40))
        return false;
    return CLSIDFromString(wide, &guid) == S_OK;
}

bool write_registry(HKEY root, const char* path, const char* name,
                    DWORD type, const void* data, DWORD size)
{
    HKEY key = nullptr;
    if (RegCreateKeyExA(root, path, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    const LONG result = RegSetValueExA(key, name, 0, type,
                                      static_cast<const BYTE*>(data), size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

void save_power_plan(char* output, size_t output_size)
{
    output[0] = '\0';
    GUID* active = nullptr;
    if (PowerGetActiveScheme(nullptr, &active) != ERROR_SUCCESS || !active)
        return;
    const std::string value = guid_text(*active);
    LocalFree(active);
    if (value.size() + 1 <= output_size)
        std::memcpy(output, value.c_str(), value.size() + 1);
}

bool set_power_plan(const char* scheme)
{
    GUID parsed{};
    if (_stricmp(scheme, "SCHEME_MIN") == 0)
        parsed = HIGH_PERFORMANCE_SCHEME;
    else if (!parse_guid(scheme, parsed))
        return false;
    return PowerSetActiveScheme(nullptr, &parsed) == ERROR_SUCCESS;
}

std::string executable_directory(const std::string& path)
{
    const size_t separator = path.find_last_of("\\/");
    return separator == std::string::npos ? std::string() : path.substr(0, separator);
}

std::string target_game_path(const std::string& configured_path)
{
    const std::string name = configured_path.substr(configured_path.find_last_of("\\/") + 1);
    if (_stricmp(name.c_str(), "FortniteClient-Win64-Shipping_EAC_EOS.exe") == 0 ||
        _stricmp(name.c_str(), "FortniteLauncher.exe") == 0)
        return executable_directory(configured_path) + "\\FortniteClient-Win64-Shipping.exe";
    return configured_path;
}

DWORD running_game_pid(const std::string& exact_path)
{
    const std::string name = exact_path.substr(exact_path.find_last_of("\\/") + 1);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;
    PROCESSENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    DWORD pid = 0;
    if (Process32First(snapshot, &entry)) {
        do {
            if (_stricmp(entry.szExeFile, name.c_str()) != 0)
                continue;
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                         FALSE, entry.th32ProcessID);
            if (!process)
                continue;
            char image[MAX_PATH * 4]{};
            DWORD size = sizeof(image);
            const bool match = QueryFullProcessImageNameA(process, 0, image, &size) &&
                               _stricmp(image, exact_path.c_str()) == 0;
            CloseHandle(process);
            if (match) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return pid;
}

bool is_admin()
{
    BOOL member = FALSE;
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID administrators = nullptr;
    if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                 &administrators)) {
        CheckTokenMembership(nullptr, administrators, &member);
        FreeSid(administrators);
    }
    return member == TRUE;
}

ULONGLONG filetime_ticks(const FILETIME& time)
{
    return (static_cast<ULONGLONG>(time.dwHighDateTime) << 32) |
           time.dwLowDateTime;
}

} // namespace

Optimizer::Optimizer()
    : _is_optimized(false),
      _power_request_active(false),
      _game_process(nullptr),
      _game_pid(0),
      _waiting_for_game(false),
      _launch_started(0),
      _last_helper_scan(0),
      _recovery_failed(false),
      _restoration_succeeded(false)
{
    _tweaks_enabled.fill(true);
    for (size_t index = 15; index <= 18; ++index)
        _tweaks_enabled[index] = false;
    _tweaks_enabled[24] = false;
    _tweaks_enabled[37] = false;
    _tweaks_enabled[39] = false;
    _tweaks_enabled[40] = false;
    _tweak_status.fill(TweakStatus::On);
    _saved_power_plan_guid[0] = '\0';
    char local_app_data[MAX_PATH]{};
    if (GetEnvironmentVariableA("LOCALAPPDATA", local_app_data,
                                sizeof(local_app_data))) {
        _journal_path = std::string(local_app_data) +
                        "\\NebulaOptimizer\\session-journal.json";
        _preferences_path = std::string(local_app_data) +
                            "\\NebulaOptimizer\\tweaks.json";
        load_preferences();
        recover_journal();
        _saved_tweaks = std::make_unique<SavedTweaks>(
            std::string(local_app_data) + "\\NebulaOptimizer");
        if (!_recovery_failed && !_saved_tweaks->recover() ) {
            _recovery_failed = true;
            _restoration_status = _saved_tweaks->error();
        }
        if (!_recovery_failed && !_saved_tweaks->scan("")) {
            _recovery_failed = true;
            _restoration_status = _saved_tweaks->error();
        }
    }
    for (size_t index = 0; index < _tweaks_enabled.size(); ++index)
        if (!_tweaks_enabled[index])
            _tweak_status[index] = TweakStatus::Off;
}

Optimizer::~Optimizer()
{
    restore();
}

bool Optimizer::enabled(size_t index) const
{
    return index < _tweaks_enabled.size() && _tweaks_enabled[index] &&
        !(_saved_tweaks && _saved_tweaks->info(index).drift);
}

std::vector<Optimizer::TweakSetting> Optimizer::tweak_settings() const
{
    const std::lock_guard<std::recursive_mutex> lock(_state_mutex);
    std::vector<TweakSetting> result;
    for (size_t index = 0; index < _tweaks_enabled.size(); ++index)
        result.push_back({TWEAK_LABELS[index], TWEAK_CATEGORIES[index],
                          _tweaks_enabled[index], _tweak_status[index],
                          _saved_tweaks ? _saved_tweaks->info(index) : SavedTweaks::Info{}});
    return result;
}

void Optimizer::load_preferences()
{
    if (_preferences_path.empty())
        return;
    try {
        std::ifstream file(_preferences_path);
        if (!file)
            return;
        nlohmann::json settings;
        file >> settings;
        for (size_t index = 0; index < _tweaks_enabled.size(); ++index)
            if (settings.contains(TWEAK_IDS[index]) &&
                settings[TWEAK_IDS[index]].is_boolean())
                _tweaks_enabled[index] = settings[TWEAK_IDS[index]].get<bool>();
    } catch (...) {}
}

bool Optimizer::save_preferences() const
{
    if (_preferences_path.empty())
        return false;
    try {
        nlohmann::json settings;
        for (size_t index = 0; index < _tweaks_enabled.size(); ++index)
            settings[TWEAK_IDS[index]] = _tweaks_enabled[index];
        std::filesystem::create_directories(
            std::filesystem::path(_preferences_path).parent_path());
        const std::string temporary = _preferences_path + ".tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file)
                return false;
            file << settings.dump(2);
            file.flush();
            if (!file)
                return false;
        }
        return MoveFileExA(temporary.c_str(), _preferences_path.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    } catch (...) {
        return false;
    }
}

bool Optimizer::toggle_tweak(size_t index)
{
    const std::lock_guard<std::recursive_mutex> lock(_state_mutex);
    if (index >= _tweaks_enabled.size())
        return false;
    _tweaks_enabled[index] = !_tweaks_enabled[index];
    if (save_preferences())
    {
        _tweak_status[index] = _tweaks_enabled[index] ? TweakStatus::On : TweakStatus::Off;
        return true;
    }
    _tweaks_enabled[index] = !_tweaks_enabled[index];
    return false;
}

bool Optimizer::scan_saved(const std::string& game_path)
{
    const std::lock_guard<std::recursive_mutex> lock(_state_mutex);
    return _saved_tweaks && !_recovery_failed && _saved_tweaks->scan(game_path);
}

bool Optimizer::save_tweak(size_t index, const std::string& game_path,
                           bool confirmed, bool (*confirm_display)())
{
    const std::lock_guard<std::recursive_mutex> lock(_state_mutex);
    return _saved_tweaks && !_recovery_failed && !_is_optimized &&
           index < _tweaks_enabled.size() && _tweaks_enabled[index] &&
           _saved_tweaks->apply(index, game_path, confirmed, confirm_display);
}

bool Optimizer::unsave_tweak(size_t index)
{
    const std::lock_guard<std::recursive_mutex> lock(_state_mutex);
    return _saved_tweaks && !_recovery_failed && _saved_tweaks->remove(index);
}

bool Optimizer::reapply_saved_tweak(size_t index, const std::string& game_path,
                                    bool confirmed, bool (*confirm_display)())
{
    const std::lock_guard<std::recursive_mutex> lock(_state_mutex);
    return _saved_tweaks && !_recovery_failed && !_is_optimized &&
           _saved_tweaks->reapply(index, game_path, confirmed, confirm_display);
}

const std::string& Optimizer::saved_error() const
{
    static const std::string unavailable = "Saved settings unavailable";
    return _saved_tweaks ? _saved_tweaks->error() : unavailable;
}

void Optimizer::record(bool success, const char* label)
{
    (success ? _applied : _failed).push_back(label);
    set_status(tweak_index_for_label(label),
               success ? TweakStatus::Applied : TweakStatus::Failed);
}

void Optimizer::set_status(size_t index, TweakStatus status)
{
    if (index >= _tweak_status.size() || !_tweaks_enabled[index])
        return;
    if (_tweak_status[index] == TweakStatus::Failed && status != TweakStatus::Failed)
        return;
    if (_tweak_status[index] == TweakStatus::Applied &&
        (status == TweakStatus::Skipped ||
         status == TweakStatus::AlreadyConfigured ||
         status == TweakStatus::Unsupported))
        return;
    _tweak_status[index] = status;
}

bool Optimizer::save_journal() const
{
    if (_journal_path.empty())
        return false;
    try {
        nlohmann::json state;
        state["power_plan"] = _saved_power_plan_guid;
        state["session_power_plan"] = _session_power_plan_guid;
        state["registry"] = nlohmann::json::array();
        state["background"] = nlohmann::json::array();
        state["ac_power_settings"] = nlohmann::json::array();
        if (!_display_state.device.empty())
            state["display"] = {{"device", _display_state.device},
                {"frequency", _display_state.frequency},
                {"width", _display_state.width}, {"height", _display_state.height},
                {"bits", _display_state.bits_per_pel},
                {"applied_frequency", _display_state.applied_frequency}};
        if (_accessibility_state.filter_touched ||
            _accessibility_state.sticky_touched ||
            _accessibility_state.toggle_touched) {
            const AccessibilityState& a = _accessibility_state;
            state["accessibility"] = {
                {"filter_flags", a.filter.dwFlags},
                {"filter_wait", a.filter.iWaitMSec},
                {"filter_delay", a.filter.iDelayMSec},
                {"filter_repeat", a.filter.iRepeatMSec},
                {"filter_bounce", a.filter.iBounceMSec},
                {"sticky_flags", a.sticky.dwFlags},
                {"toggle_flags", a.toggle.dwFlags},
                {"filter_applied", a.filter_applied_flags},
                {"sticky_applied", a.sticky_applied_flags},
                {"toggle_applied", a.toggle_applied_flags},
                {"filter_touched", a.filter_touched},
                {"sticky_touched", a.sticky_touched},
                {"toggle_touched", a.toggle_touched}
            };
        }
        for (const RegistryState& entry : _registry_state) {
            state["registry"].push_back({
                {"root", entry.root == HKEY_LOCAL_MACHINE ? "HKLM" : "HKCU"},
                {"path", entry.path}, {"name", entry.name},
                {"type", entry.type}, {"data", entry.data},
                {"existed", entry.existed},
                {"applied_type", entry.applied_type},
                {"applied_data", entry.applied_data},
                {"compare_and_swap", entry.compare_and_swap}
            });
        }
        for (const BackgroundState& entry : _background_state) {
            state["background"].push_back({
                {"pid", entry.pid}, {"created_at", entry.created_at},
                {"priority", entry.priority_class},
                {"throttling_known", entry.throttling_known},
                {"throttling_control", entry.throttling.ControlMask},
                {"throttling_state", entry.throttling.StateMask},
                {"memory_priority", entry.memory_priority},
                {"memory_known", entry.memory_known},
                {"boost_disabled", entry.priority_boost_disabled},
                {"boost_known", entry.boost_known},
                {"priority_touched", entry.priority_touched},
                {"throttling_touched", entry.throttling_touched},
                {"memory_touched", entry.memory_touched},
                {"boost_touched", entry.boost_touched},
                {"compare_and_swap", entry.compare_and_swap},
                {"applied_priority", entry.applied_priority_class},
                {"applied_throttling_state", entry.applied_throttling_state},
                {"applied_memory_priority", entry.applied_memory_priority},
                {"applied_boost_disabled", entry.applied_boost_disabled}
            });
        }
        for (const PowerSettingState& entry : _power_settings) {
            state["ac_power_settings"].push_back({
                {"scheme", guid_text(entry.scheme)},
                {"kind", entry.kind}, {"original", entry.original},
                {"applied", entry.applied},
                {"compare_and_swap", entry.compare_and_swap}
            });
        }
        std::filesystem::create_directories(
            std::filesystem::path(_journal_path).parent_path());
        const std::string temporary = _journal_path + ".tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file)
                return false;
            file << state.dump();
            file.flush();
            if (!file)
                return false;
        }
        return MoveFileExA(temporary.c_str(), _journal_path.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    } catch (...) {
        return false;
    }
}

void Optimizer::recover_journal()
{
    if (GetFileAttributesA(_journal_path.c_str()) == INVALID_FILE_ATTRIBUTES)
        return;
    _recovery_failed = false;
    try {
        std::ifstream file(_journal_path, std::ios::binary);
        nlohmann::json state;
        file >> state;
        file.close();
        const std::string power = state.value("power_plan", std::string());
        if (power.size() >= sizeof(_saved_power_plan_guid))
            throw std::runtime_error("Invalid original power scheme in recovery log");
        std::memcpy(_saved_power_plan_guid, power.c_str(), power.size() + 1);
        _session_power_plan_guid = state.value("session_power_plan", std::string());
        for (const auto& item : state.at("registry")) {
            RegistryState entry{};
            entry.root = item.at("root").get<std::string>() == "HKLM"
                ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
            entry.path = item.at("path").get<std::string>();
            entry.name = item.at("name").get<std::string>();
            entry.type = item.at("type").get<DWORD>();
            entry.data = item.at("data").get<std::vector<unsigned char>>();
            entry.existed = item.at("existed").get<bool>();
            entry.applied_type = item.value("applied_type", DWORD(0));
            entry.applied_data = item.value(
                "applied_data", std::vector<unsigned char>{});
            entry.compare_and_swap = item.value("compare_and_swap", false);
            _registry_state.push_back(std::move(entry));
        }
        for (const auto& item : state.value("background", nlohmann::json::array())) {
            BackgroundState entry{};
            entry.process = nullptr;
            entry.pid = item.at("pid").get<DWORD>();
            entry.created_at = item.at("created_at").get<ULONGLONG>();
            entry.priority_class = item.at("priority").get<DWORD>();
            entry.throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
            entry.throttling.ControlMask = item.at("throttling_control").get<DWORD>();
            entry.throttling.StateMask = item.at("throttling_state").get<DWORD>();
            entry.throttling_known = item.at("throttling_known").get<bool>();
            entry.memory_priority = item.value("memory_priority", DWORD(0));
            entry.memory_known = item.value("memory_known", false);
            entry.priority_boost_disabled = item.value("boost_disabled", false);
            entry.boost_known = item.value("boost_known", false);
            entry.priority_touched = item.value("priority_touched", true);
            entry.throttling_touched = item.value("throttling_touched", true);
            entry.memory_touched = item.value("memory_touched", false);
            entry.boost_touched = item.value("boost_touched", false);
            entry.compare_and_swap = item.value("compare_and_swap", false);
            entry.applied_priority_class = item.value("applied_priority", DWORD(0));
            entry.applied_throttling_state = item.value("applied_throttling_state", DWORD(0));
            entry.applied_memory_priority = item.value("applied_memory_priority", DWORD(0));
            entry.applied_boost_disabled = item.value("applied_boost_disabled", false);
            _background_state.push_back(entry);
        }
        if (state.contains("display")) {
            const auto& display = state.at("display");
            _display_state.device = display.at("device").get<std::string>();
            _display_state.frequency = display.at("frequency").get<DWORD>();
            _display_state.width = display.at("width").get<DWORD>();
            _display_state.height = display.at("height").get<DWORD>();
            _display_state.bits_per_pel = display.at("bits").get<DWORD>();
            _display_state.applied_frequency =
                display.value("applied_frequency", DWORD(0));
        }
        if (state.contains("accessibility")) {
            const auto& a = state.at("accessibility");
            _accessibility_state.filter.cbSize = sizeof(FILTERKEYS);
            _accessibility_state.sticky.cbSize = sizeof(STICKYKEYS);
            _accessibility_state.toggle.cbSize = sizeof(TOGGLEKEYS);
            _accessibility_state.filter.dwFlags = a.at("filter_flags").get<DWORD>();
            _accessibility_state.filter.iWaitMSec = a.at("filter_wait").get<DWORD>();
            _accessibility_state.filter.iDelayMSec = a.at("filter_delay").get<DWORD>();
            _accessibility_state.filter.iRepeatMSec = a.at("filter_repeat").get<DWORD>();
            _accessibility_state.filter.iBounceMSec = a.at("filter_bounce").get<DWORD>();
            _accessibility_state.sticky.dwFlags = a.at("sticky_flags").get<DWORD>();
            _accessibility_state.toggle.dwFlags = a.at("toggle_flags").get<DWORD>();
            _accessibility_state.filter_applied_flags = a.at("filter_applied").get<DWORD>();
            _accessibility_state.sticky_applied_flags = a.at("sticky_applied").get<DWORD>();
            _accessibility_state.toggle_applied_flags = a.at("toggle_applied").get<DWORD>();
            _accessibility_state.filter_touched = a.at("filter_touched").get<bool>();
            _accessibility_state.sticky_touched = a.at("sticky_touched").get<bool>();
            _accessibility_state.toggle_touched = a.at("toggle_touched").get<bool>();
        }
        for (const auto& item : state.value("ac_power_settings", nlohmann::json::array())) {
            PowerSettingState entry{};
            if (!parse_guid(item.at("scheme").get<std::string>(), entry.scheme))
                throw std::runtime_error("Invalid power scheme in recovery log");
            entry.kind = item.at("kind").get<unsigned int>();
            entry.original = item.at("original").get<DWORD>();
            entry.applied = item.value("applied", entry.original);
            entry.compare_and_swap = item.value("compare_and_swap", false);
            if (!power_setting(entry.kind))
                throw std::runtime_error("Invalid power setting in recovery log");
            _power_settings.push_back(entry);
        }
        bool okay = restore_accessibility_hotkeys();
        okay = restore_background_processes() && okay;
        okay = restore_registry() && okay;
        if (_session_power_plan_guid.empty())
            okay = restore_ac_power_settings() && okay;
        else
            _power_settings.clear();
        okay = restore_display() && okay;
        if (!_session_power_plan_guid.empty())
            okay = restore_session_power_plan() && okay;
        else if (!power.empty())
            okay = set_power_plan(power.c_str()) && okay;
        if (okay && DeleteFileA(_journal_path.c_str())) {
            _restoration_status = "Previous session restored";
            _restoration_succeeded = true;
        } else {
            _restoration_status = "Restore incomplete - close game, then press R";
            _recovery_failed = true;
        }
    } catch (...) {
        _registry_state.clear();
        _power_settings.clear();
        _display_state = {};
        _accessibility_state = {};
        _restoration_status = "Session log unreadable - manual restore needed";
        _recovery_failed = true;
    }
}

bool Optimizer::snapshot_registry(HKEY root, const char* path, const char* name,
                                  DWORD applied_type, const void* applied_data,
                                  DWORD applied_size, bool& already_configured)
{
    already_configured = false;
    RegistryState state{root, path, name, 0, {}, false,
                        applied_type, {}, true};
    const auto* bytes = static_cast<const unsigned char*>(applied_data);
    state.applied_data.assign(bytes, bytes + applied_size);
    HKEY key = nullptr;
    const LONG opened = RegOpenKeyExA(root, path, 0, KEY_QUERY_VALUE, &key);
    if (opened == ERROR_SUCCESS) {
        DWORD size = 0;
        const LONG queried = RegQueryValueExA(key, name, nullptr,
                                               &state.type, nullptr, &size);
        if (queried == ERROR_SUCCESS) {
            state.data.resize(size);
            if (RegQueryValueExA(key, name, nullptr, &state.type,
                                 state.data.data(), &size) != ERROR_SUCCESS) {
                RegCloseKey(key);
                return false;
            }
            state.data.resize(size);
            state.existed = true;
        } else if (queried != ERROR_FILE_NOT_FOUND) {
            RegCloseKey(key);
            return false;
        }
        RegCloseKey(key);
    } else if (opened != ERROR_FILE_NOT_FOUND && opened != ERROR_PATH_NOT_FOUND)
        return false;
    already_configured = state.existed && state.type == applied_type &&
                         state.data == state.applied_data;
    if (already_configured)
        return true;
    _registry_state.push_back(std::move(state));
    if (save_journal())
        return true;
    _registry_state.pop_back();
    return false;
}

bool Optimizer::set_dword(HKEY root, const char* path, const char* name,
                          DWORD value, const char* label)
{
    bool already = false;
    if (!snapshot_registry(root, path, name, REG_DWORD, &value,
                           sizeof(value), already)) {
        record(false, label);
        return false;
    }
    if (already) {
        set_status(tweak_index_for_label(label), TweakStatus::AlreadyConfigured);
        return true;
    }
    const bool success = write_registry(root, path, name, REG_DWORD,
                                        &value, sizeof(value));
    record(success, label);
    return success;
}

bool Optimizer::set_string(HKEY root, const char* path, const char* name,
                           const std::string& value, const char* label)
{
    bool already = false;
    if (!snapshot_registry(root, path, name, REG_SZ, value.c_str(),
                           static_cast<DWORD>(value.size() + 1), already)) {
        record(false, label);
        return false;
    }
    if (already) {
        set_status(tweak_index_for_label(label), TweakStatus::AlreadyConfigured);
        return true;
    }
    const bool success = write_registry(root, path, name, REG_SZ, value.c_str(),
                                        static_cast<DWORD>(value.size() + 1));
    record(success, label);
    return success;
}

bool Optimizer::restore_registry()
{
    bool okay = true;
    for (auto it = _registry_state.rbegin(); it != _registry_state.rend(); ++it) {
        HKEY key = nullptr;
        const LONG opened = RegOpenKeyExA(it->root, it->path.c_str(), 0,
                                          KEY_QUERY_VALUE | KEY_SET_VALUE, &key);
        if (opened != ERROR_SUCCESS) {
            if ((opened == ERROR_FILE_NOT_FOUND || opened == ERROR_PATH_NOT_FOUND) &&
                !it->existed)
                continue;
            if (it->compare_and_swap ||
                RegCreateKeyExA(it->root, it->path.c_str(), 0, nullptr, 0,
                                KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr,
                                &key, nullptr) != ERROR_SUCCESS) {
                okay = false;
                continue;
            }
        }
        DWORD current_type = 0;
        DWORD current_size = 0;
        const LONG queried = RegQueryValueExA(key, it->name.c_str(), nullptr,
                                               &current_type, nullptr, &current_size);
        const bool current_exists = queried == ERROR_SUCCESS;
        std::vector<unsigned char> current_data(current_exists ? current_size : 0);
        if (current_exists &&
            RegQueryValueExA(key, it->name.c_str(), nullptr, &current_type,
                             current_data.data(), &current_size) == ERROR_SUCCESS)
            current_data.resize(current_size);
        else if (queried != ERROR_FILE_NOT_FOUND) {
            okay = false;
            RegCloseKey(key);
            continue;
        }
        const bool already_original = current_exists == it->existed &&
            (!it->existed || (current_type == it->type &&
                              current_data == it->data));
        if (already_original) {
            RegCloseKey(key);
            continue;
        }
        if (it->compare_and_swap &&
            (!current_exists || current_type != it->applied_type ||
             current_data != it->applied_data)) {
            okay = false;
            RegCloseKey(key);
            continue;
        }
        LONG result = ERROR_SUCCESS;
        if (it->existed) {
            result = RegSetValueExA(key, it->name.c_str(), 0, it->type,
                           it->data.empty() ? nullptr : it->data.data(),
                           static_cast<DWORD>(it->data.size()));
        } else {
            result = RegDeleteValueA(key, it->name.c_str());
            if (result == ERROR_FILE_NOT_FOUND)
                result = ERROR_SUCCESS;
        }
        if (result != ERROR_SUCCESS)
            okay = false;
        else {
            DWORD verified_type = 0;
            DWORD verified_size = 0;
            const LONG found = RegQueryValueExA(key, it->name.c_str(), nullptr,
                                                 &verified_type, nullptr,
                                                 &verified_size);
            if (it->existed) {
                std::vector<unsigned char> verified(
                    found == ERROR_SUCCESS ? verified_size : 0);
                if (found != ERROR_SUCCESS ||
                    RegQueryValueExA(key, it->name.c_str(), nullptr,
                                     &verified_type, verified.data(),
                                     &verified_size) != ERROR_SUCCESS ||
                    verified_type != it->type || verified != it->data)
                    okay = false;
            } else if (found != ERROR_FILE_NOT_FOUND)
                okay = false;
        }
        RegCloseKey(key);
    }
    _registry_state.clear();
    return okay;
}

bool Optimizer::launch_game(const std::string& game_path, bool launch_if_missing)
{
    const DWORD attributes = GetFileAttributesA(game_path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        _last_error = "Game executable not found: " + game_path;
        return false;
    }

    const std::string file_name = game_path.substr(game_path.find_last_of("\\/") + 1);
    const bool fortnite = _stricmp(file_name.c_str(),
        "FortniteClient-Win64-Shipping_EAC_EOS.exe") == 0 ||
        _stricmp(file_name.c_str(), "FortniteLauncher.exe") == 0 ||
        _stricmp(file_name.c_str(), "FortniteClient-Win64-Shipping.exe") == 0;

    _target_game_path = target_game_path(game_path);
    if (fortnite && GetFileAttributesA(_target_game_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        _last_error = "Fortnite game executable not found";
        return false;
    }

    if (enabled(12))
        set_string(HKEY_CURRENT_USER,
                   "Software\\Microsoft\\DirectX\\UserGpuPreferences",
                   _target_game_path.c_str(), "GpuPreference=2;", "High-performance GPU preference");

    if (find_target_process()) {
        _applied.push_back("Attached to running game");
        return true;
    }
    if (!launch_if_missing) {
        _last_error = "Game closed before attachment";
        return false;
    }

    if (fortnite) {
        char desktop[MAX_PATH]{};
        if (SHGetFolderPathA(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr,
                             SHGFP_TYPE_CURRENT, desktop) != S_OK) {
            _last_error = "Desktop folder not found";
            return false;
        }
        const std::string shortcut = std::string(desktop) + "\\Fortnite.url";
        if (GetFileAttributesA(shortcut.c_str()) == INVALID_FILE_ATTRIBUTES) {
            _last_error = "Create a Fortnite desktop shortcut in Epic Games";
            return false;
        }
        if (reinterpret_cast<INT_PTR>(ShellExecuteA(nullptr, "open",
                shortcut.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32) {
            _last_error = "Epic Games could not launch Fortnite";
            return false;
        }
        _waiting_for_game = true;
        _launch_started = GetTickCount64();
        _applied.push_back("Launched through Epic Games");
        _applied.push_back("Anti-cheat excluded");
        return true;
    }

    STARTUPINFOA startup{};
    PROCESS_INFORMATION process{};
    startup.cb = sizeof(startup);
    std::vector<char> command(game_path.size() + 3, '\0');
    command[0] = '"';
    std::memcpy(command.data() + 1, game_path.data(), game_path.size());
    command[game_path.size() + 1] = '"';
    const std::string working_directory = executable_directory(game_path);

    const BOOL created = CreateProcessA(
        nullptr, command.data(), nullptr, nullptr, FALSE,
        CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP | NORMAL_PRIORITY_CLASS,
        nullptr, working_directory.empty() ? nullptr : working_directory.c_str(),
        &startup, &process);
    if (!created) {
        _last_error = "Game launch failed (code " +
                      std::to_string(GetLastError()) + ")";
        return false;
    }

    const bool tracked = track_process_for_restore(process.hProcess, process.dwProcessId);
    tune_game_process(process.hProcess, tracked);

    if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
        _last_error = "Game launch failed";
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return false;
    }

    _game_process = process.hProcess;
    _game_pid = process.dwProcessId;
    _game_directory = working_directory;
    CloseHandle(process.hThread);
    _applied.push_back("Game launched");
    return true;
}

bool Optimizer::track_process_for_restore(HANDLE process, DWORD pid)
{
    BackgroundState state{};
    if (!DuplicateHandle(GetCurrentProcess(), process, GetCurrentProcess(),
                         &state.process, 0, FALSE, DUPLICATE_SAME_ACCESS))
        return false;
    state.pid = pid;
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(state.process, &created, &exited, &kernel, &user)) {
        CloseHandle(state.process);
        return false;
    }
    state.created_at = filetime_ticks(created);
    state.priority_class = GetPriorityClass(state.process);
    state.throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.throttling_known = GetProcessInformation(
        state.process, ProcessPowerThrottling, &state.throttling,
        sizeof(state.throttling)) != FALSE;
    MEMORY_PRIORITY_INFORMATION memory{};
    state.memory_known = GetProcessInformation(
        state.process, ProcessMemoryPriority, &memory, sizeof(memory)) != FALSE;
    state.memory_priority = memory.MemoryPriority;
    BOOL boost_disabled = FALSE;
    state.boost_known = GetProcessPriorityBoost(state.process, &boost_disabled) != FALSE;
    state.priority_boost_disabled = boost_disabled != FALSE;
    state.priority_touched = enabled(14) &&
        state.priority_class != HIGH_PRIORITY_CLASS;
    state.throttling_touched = enabled(13) && state.throttling_known &&
        !(state.throttling.ControlMask &
          ~PROCESS_POWER_THROTTLING_EXECUTION_SPEED) &&
        (state.throttling.StateMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED);
    state.memory_touched = enabled(27) && state.memory_known &&
        state.memory_priority < MEMORY_PRIORITY_NORMAL;
    state.boost_touched = enabled(26) && state.boost_known &&
        state.priority_boost_disabled;
    state.compare_and_swap = true;
    state.applied_priority_class = HIGH_PRIORITY_CLASS;
    state.applied_throttling_state = 0;
    state.applied_memory_priority = MEMORY_PRIORITY_NORMAL;
    state.applied_boost_disabled = false;
    if (!state.priority_class) {
        CloseHandle(state.process);
        return false;
    }
    _background_state.push_back(state);
    if (save_journal())
        return true;
    _background_state.pop_back();
    CloseHandle(state.process);
    return false;
}

void Optimizer::tune_game_process(HANDLE process, bool tracked)
{
    if (enabled(13)) {
        PROCESS_POWER_THROTTLING_STATE throttling{};
        throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        throttling.StateMask = 0;
        if (tracked && !_background_state.back().throttling_known)
            set_status(13, TweakStatus::Unsupported);
        else if (tracked &&
                 (_background_state.back().throttling.ControlMask &
                  ~PROCESS_POWER_THROTTLING_EXECUTION_SPEED) &&
                 (_background_state.back().throttling.StateMask &
                  PROCESS_POWER_THROTTLING_EXECUTION_SPEED))
            set_status(13, TweakStatus::Unsupported);
        else if (tracked && !_background_state.back().throttling_touched)
            set_status(13, TweakStatus::AlreadyConfigured);
        else
            record(tracked && SetProcessInformation(process, ProcessPowerThrottling,
                                                     &throttling, sizeof(throttling)) != FALSE,
                   "Game power throttling off");
    }
    if (enabled(14)) {
        if (tracked && !_background_state.back().priority_touched)
            set_status(14, TweakStatus::AlreadyConfigured);
        else
            record(tracked && SetPriorityClass(process, HIGH_PRIORITY_CLASS) != FALSE,
                   "Game CPU priority: high");
    }
    if (!tracked || _background_state.empty()) {
        if (enabled(26)) set_status(26, TweakStatus::Failed);
        if (enabled(27)) set_status(27, TweakStatus::Failed);
        return;
    }
    const BackgroundState& original = _background_state.back();
    if (enabled(26)) {
        if (!original.boost_known)
            set_status(26, TweakStatus::Failed);
        else if (!original.priority_boost_disabled)
            set_status(26, TweakStatus::AlreadyConfigured);
        else {
            const bool okay = SetProcessPriorityBoost(process, FALSE) != FALSE;
            set_status(26, okay ? TweakStatus::Applied : TweakStatus::Failed);
            record(okay, "Game dynamic priority boost");
        }
    }
    if (enabled(27)) {
        if (!original.memory_known)
            set_status(27, TweakStatus::Failed);
        else if (original.memory_priority >= MEMORY_PRIORITY_NORMAL)
            set_status(27, TweakStatus::AlreadyConfigured);
        else {
            MEMORY_PRIORITY_INFORMATION memory{};
            memory.MemoryPriority = MEMORY_PRIORITY_NORMAL;
            const bool okay = SetProcessInformation(process, ProcessMemoryPriority,
                                                     &memory, sizeof(memory)) != FALSE;
            set_status(27, okay ? TweakStatus::Applied : TweakStatus::Failed);
            record(okay, "Game memory priority normal");
        }
    }
}

void Optimizer::throttle_background_processes()
{
    // Only non-essential sync/index/update helpers are targeted. Browsers,
    // launchers, Discord/voice, audio and device software are preserved.
    struct Target { const char* name; size_t cpu_tweak; size_t memory_tweak; };
    static const Target targets[] = {
        {"OneDrive.exe", 11, 29}, {"Dropbox.exe", 11, 29},
        {"GoogleDriveFS.exe", 11, 29}, {"iCloudDrive.exe", 11, 29},
        {"SearchIndexer.exe", 19, 30}, {"SearchProtocolHost.exe", 19, 30},
        {"SearchFilterHost.exe", 19, 30},
        {"Widgets.exe", 20, 31}, {"WidgetService.exe", 20, 31},
        {"PhoneExperienceHost.exe", 21, 32},
        {"AdobeUpdateService.exe", 22, 33}, {"CCXProcess.exe", 22, 33},
        {"Creative Cloud.exe", 22, 33},
        {"MicrosoftEdgeUpdate.exe", 34, 36},
        {"GoogleUpdater.exe", 34, 36},
        {"GoogleUpdate.exe", 34, 36}
    };

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return;
    PROCESSENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (Process32First(snapshot, &entry)) {
        do {
            const Target* target = nullptr;
            for (const Target& candidate : targets) {
                if (_stricmp(entry.szExeFile, candidate.name) == 0) {
                    target = &candidate;
                    break;
                }
            }
            if (!target || (!enabled(target->cpu_tweak) &&
                            !enabled(target->memory_tweak) &&
                            !(target->cpu_tweak == 34 && enabled(35))))
                continue;

            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION |
                                         PROCESS_SET_INFORMATION,
                                         FALSE, entry.th32ProcessID);
            if (!process) {
                const TweakStatus unavailable = GetLastError() == ERROR_ACCESS_DENIED ?
                    TweakStatus::AccessDenied : TweakStatus::Failed;
                if (enabled(target->memory_tweak))
                    set_status(target->memory_tweak, unavailable);
                if (target->cpu_tweak == 34 && enabled(34))
                    set_status(34, unavailable);
                if (target->cpu_tweak == 34 && enabled(35))
                    set_status(35, unavailable);
                continue;
            }
            BackgroundState state{};
            state.process = process;
            state.pid = entry.th32ProcessID;
            FILETIME created{}, exited{}, kernel{}, user{};
            if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) {
                CloseHandle(process);
                continue;
            }
            state.created_at = filetime_ticks(created);
            const bool already_tracked = std::any_of(
                _background_state.begin(), _background_state.end(),
                [&](const BackgroundState& previous) {
                    return previous.pid == state.pid &&
                           previous.created_at == state.created_at;
                });
            if (already_tracked) {
                CloseHandle(process);
                continue;
            }
            state.priority_class = GetPriorityClass(process);
            if (!state.priority_class) {
                CloseHandle(process);
                continue;
            }
            state.throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
            state.throttling_known = GetProcessInformation(
                process, ProcessPowerThrottling, &state.throttling,
                sizeof(state.throttling)) != FALSE;
            if (target->cpu_tweak == 34 && enabled(35) &&
                !state.throttling_known)
                set_status(35, TweakStatus::Unsupported);
            MEMORY_PRIORITY_INFORMATION memory{};
            state.memory_known = GetProcessInformation(
                process, ProcessMemoryPriority, &memory, sizeof(memory)) != FALSE;
            if (enabled(target->memory_tweak) && !state.memory_known)
                set_status(target->memory_tweak, TweakStatus::Unsupported);
            state.memory_priority = memory.MemoryPriority;
            BOOL boost_disabled = FALSE;
            state.boost_known = GetProcessPriorityBoost(process, &boost_disabled) != FALSE;
            state.priority_boost_disabled = boost_disabled != FALSE;
            state.priority_touched = enabled(target->cpu_tweak) &&
                state.priority_class != BELOW_NORMAL_PRIORITY_CLASS &&
                state.priority_class != IDLE_PRIORITY_CLASS;
            state.throttling_touched = state.throttling_known &&
                target->cpu_tweak == 34 && enabled(35) &&
                !(state.throttling.ControlMask &
                  ~PROCESS_POWER_THROTTLING_EXECUTION_SPEED) &&
                !(state.throttling.StateMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED);
            const DWORD desired_memory = target->memory_tweak == 36 ?
                MEMORY_PRIORITY_LOW : MEMORY_PRIORITY_BELOW_NORMAL;
            state.memory_touched = enabled(target->memory_tweak) &&
                state.memory_known && state.memory_priority > desired_memory;
            state.compare_and_swap = true;
            state.applied_priority_class = BELOW_NORMAL_PRIORITY_CLASS;
            state.applied_throttling_state = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
            state.applied_memory_priority = desired_memory;
            state.applied_boost_disabled = state.priority_boost_disabled;

            if (!state.priority_touched && !state.throttling_touched &&
                !state.memory_touched) {
                CloseHandle(process);
                continue;
            }

            _background_state.push_back(state);
            if (!save_journal()) {
                _background_state.pop_back();
                CloseHandle(process);
                _failed.push_back(std::string("Could not track: ") + entry.szExeFile);
                continue;
            }

            bool changed = false;
            if (enabled(target->cpu_tweak)) {
                const bool needs_cpu = state.priority_class != BELOW_NORMAL_PRIORITY_CLASS &&
                                       state.priority_class != IDLE_PRIORITY_CLASS;
                const bool cpu_ok = !needs_cpu ||
                    SetPriorityClass(process, BELOW_NORMAL_PRIORITY_CLASS) != FALSE;
                if (target->cpu_tweak == 34)
                    set_status(34, !cpu_ok ? TweakStatus::Failed :
                               needs_cpu ? TweakStatus::Applied : TweakStatus::Skipped);
                else
                    set_status(target->cpu_tweak, !cpu_ok ? TweakStatus::Failed :
                               needs_cpu ? TweakStatus::Applied : TweakStatus::Skipped);
                changed = changed || (needs_cpu && cpu_ok);
            }
            if (target->cpu_tweak == 34 && enabled(35)) {
                const bool needs_eco = state.throttling_known &&
                    !(state.throttling.StateMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED);
                PROCESS_POWER_THROTTLING_STATE eco{};
                eco.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
                eco.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
                eco.StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
                const bool eco_ok = state.throttling_known &&
                    (!needs_eco || (state.throttling_touched &&
                     SetProcessInformation(process, ProcessPowerThrottling,
                                           &eco, sizeof(eco)) != FALSE));
                set_status(35, !eco_ok ? TweakStatus::Failed :
                           needs_eco ? TweakStatus::Applied : TweakStatus::Skipped);
                changed = changed || (needs_eco && eco_ok);
            }
            if (enabled(target->memory_tweak)) {
                const DWORD desired = target->memory_tweak == 36 ?
                    MEMORY_PRIORITY_LOW : MEMORY_PRIORITY_BELOW_NORMAL;
                const bool needs_memory = state.memory_known &&
                    state.memory_priority > desired;
                MEMORY_PRIORITY_INFORMATION target_memory{};
                target_memory.MemoryPriority = desired;
                const bool memory_ok = state.memory_known &&
                    (!needs_memory || SetProcessInformation(process, ProcessMemoryPriority,
                                                             &target_memory,
                                                             sizeof(target_memory)) != FALSE);
                set_status(target->memory_tweak, !memory_ok ? TweakStatus::Failed :
                           needs_memory ? TweakStatus::Applied : TweakStatus::Skipped);
                changed = changed || (needs_memory && memory_ok);
            }
            if (changed) {
                _applied.push_back(std::string("Background load reduced: ") + entry.szExeFile);
            } else {
                _background_state.pop_back();
                save_journal();
                CloseHandle(process);
            }
        } while (Process32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
}

bool Optimizer::restore_background_processes()
{
    bool okay = true;
    for (auto item = _background_state.rbegin();
         item != _background_state.rend(); ++item) {
        const BackgroundState& state = *item;
        if (!state.priority_touched && !state.throttling_touched &&
            !state.memory_touched && !state.boost_touched) {
            if (state.process)
                CloseHandle(state.process);
            continue;
        }
        HANDLE process = state.process;
        if (!process) {
            process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION |
                                  PROCESS_SET_INFORMATION | SYNCHRONIZE,
                                  FALSE, state.pid);
            if (!process) {
                if (GetLastError() != ERROR_INVALID_PARAMETER)
                    okay = false;
                continue;
            }
        }
        FILETIME created{}, exited{}, kernel{}, user{};
        DWORD exit_code = 0;
        const bool readable = GetProcessTimes(process, &created, &exited,
                                              &kernel, &user) &&
                              GetExitCodeProcess(process, &exit_code);
        if (!readable)
            okay = false;
        if (readable && filetime_ticks(created) == state.created_at &&
            exit_code == STILL_ACTIVE) {
            const DWORD current_priority = GetPriorityClass(process);
            if (state.priority_touched && state.priority_class) {
                if (!current_priority)
                    okay = false;
                else if (current_priority != state.priority_class) {
                    if (state.compare_and_swap &&
                        current_priority != state.applied_priority_class)
                        okay = false;
                    else if (!SetPriorityClass(process, state.priority_class) ||
                             GetPriorityClass(process) != state.priority_class)
                        okay = false;
                }
            }
            if (state.throttling_touched && state.throttling_known) {
                PROCESS_POWER_THROTTLING_STATE original = state.throttling;
                original.ControlMask &= PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
                original.StateMask &= PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
                PROCESS_POWER_THROTTLING_STATE current{};
                current.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
                if (!GetProcessInformation(process, ProcessPowerThrottling,
                                           &current, sizeof(current)))
                    okay = false;
                else if (current.ControlMask != original.ControlMask ||
                         current.StateMask != original.StateMask) {
                    if (state.compare_and_swap &&
                        (current.ControlMask !=
                         PROCESS_POWER_THROTTLING_EXECUTION_SPEED ||
                         current.StateMask != state.applied_throttling_state))
                        okay = false;
                    else if (!SetProcessInformation(process, ProcessPowerThrottling,
                                                    &original, sizeof(original)))
                        okay = false;
                }
            }
            if (state.memory_touched && state.memory_known) {
                MEMORY_PRIORITY_INFORMATION current{};
                if (!GetProcessInformation(process, ProcessMemoryPriority,
                                           &current, sizeof(current)))
                    okay = false;
                else if (current.MemoryPriority != state.memory_priority) {
                    MEMORY_PRIORITY_INFORMATION original{};
                    original.MemoryPriority = state.memory_priority;
                    if (state.compare_and_swap &&
                        current.MemoryPriority != state.applied_memory_priority)
                        okay = false;
                    else {
                        MEMORY_PRIORITY_INFORMATION verified{};
                        if (!SetProcessInformation(process, ProcessMemoryPriority,
                                                   &original, sizeof(original)) ||
                            !GetProcessInformation(process, ProcessMemoryPriority,
                                                   &verified, sizeof(verified)) ||
                            verified.MemoryPriority != state.memory_priority)
                            okay = false;
                    }
                }
            }
            if (state.boost_touched && state.boost_known) {
                BOOL disabled = FALSE;
                if (!GetProcessPriorityBoost(process, &disabled))
                    okay = false;
                else if ((disabled != FALSE) != state.priority_boost_disabled) {
                    if (state.compare_and_swap &&
                        (disabled != FALSE) != state.applied_boost_disabled)
                        okay = false;
                    else {
                        BOOL verified = FALSE;
                        if (!SetProcessPriorityBoost(
                                 process, state.priority_boost_disabled ? TRUE : FALSE) ||
                            !GetProcessPriorityBoost(process, &verified) ||
                            (verified != FALSE) != state.priority_boost_disabled)
                            okay = false;
                    }
                }
            }
        }
        CloseHandle(process);
    }
    _background_state.clear();
    return okay;
}

bool Optimizer::set_ac_power_setting(unsigned int kind, DWORD value,
                                     size_t tweak, const char* label)
{
    SYSTEM_POWER_STATUS status{};
    if (!GetSystemPowerStatus(&status) || status.ACLineStatus != 1) {
        set_status(tweak, TweakStatus::Skipped);
        return false;
    }
    GUID* active = nullptr;
    if (PowerGetActiveScheme(nullptr, &active) != ERROR_SUCCESS || !active) {
        set_status(tweak, TweakStatus::Failed);
        record(false, label);
        return false;
    }
    const GUID scheme = *active;
    LocalFree(active);
    GUID session{};
    if (!parse_guid(_session_power_plan_guid, session) ||
        !IsEqualGUID(scheme, session)) {
        set_status(tweak, TweakStatus::Skipped);
        return false;
    }
    const GUID* subgroup = power_subgroup(kind);
    const GUID* setting = power_setting(kind);
    DWORD original = 0;
    const DWORD read_result = subgroup && setting ?
        PowerReadACValueIndex(nullptr, &scheme, subgroup, setting, &original) :
        ERROR_INVALID_PARAMETER;
    if (read_result != ERROR_SUCCESS) {
        if (read_result == ERROR_FILE_NOT_FOUND ||
            read_result == ERROR_INVALID_PARAMETER)
            set_status(tweak, TweakStatus::Unsupported);
        else {
            set_status(tweak, read_result == ERROR_ACCESS_DENIED ?
                       TweakStatus::AccessDenied : TweakStatus::Failed);
            record(false, label);
        }
        return false;
    }
    if (original == value || (kind == 2 && original != 0)) {
        set_status(tweak, TweakStatus::AlreadyConfigured);
        return true;
    }
    _power_settings.push_back({scheme, kind, original, value, true});
    if (!save_journal()) {
        _power_settings.pop_back();
        set_status(tweak, TweakStatus::Failed);
        record(false, label);
        return false;
    }
    DWORD verified = 0;
    const bool changed = PowerWriteACValueIndex(nullptr, &scheme, subgroup,
                                                 setting, value) == ERROR_SUCCESS &&
                         PowerSetActiveScheme(nullptr, &scheme) == ERROR_SUCCESS &&
                         PowerReadACValueIndex(nullptr, &scheme, subgroup,
                                               setting, &verified) == ERROR_SUCCESS &&
                         verified == value;
    set_status(tweak, changed ? TweakStatus::Applied : TweakStatus::Failed);
    record(changed, label);
    return changed;
}

bool Optimizer::restore_ac_power_settings()
{
    bool okay = true;
    for (auto item = _power_settings.rbegin(); item != _power_settings.rend(); ++item) {
        const GUID* subgroup = power_subgroup(item->kind);
        const GUID* setting = power_setting(item->kind);
        DWORD current = 0;
        if (!subgroup || !setting ||
            PowerReadACValueIndex(nullptr, &item->scheme, subgroup, setting,
                                  &current) != ERROR_SUCCESS) {
            okay = false;
            continue;
        }
        if (current == item->original)
            continue;
        if ((item->compare_and_swap && current != item->applied) ||
            PowerWriteACValueIndex(nullptr, &item->scheme, subgroup, setting,
                                   item->original) != ERROR_SUCCESS) {
            okay = false;
            continue;
        }
        GUID* active = nullptr;
        if (PowerGetActiveScheme(nullptr, &active) != ERROR_SUCCESS || !active) {
            okay = false;
            continue;
        }
        const bool scheme_active = IsEqualGUID(*active, item->scheme);
        LocalFree(active);
        DWORD verified = 0;
        if ((scheme_active &&
             PowerSetActiveScheme(nullptr, &item->scheme) != ERROR_SUCCESS) ||
            PowerReadACValueIndex(nullptr, &item->scheme, subgroup, setting,
                                  &verified) != ERROR_SUCCESS ||
            verified != item->original)
            okay = false;
    }
    _power_settings.clear();
    return okay;
}

bool Optimizer::begin_session_power_plan()
{
    GUID original{};
    if (!parse_guid(_saved_power_plan_guid, original))
        return false;
    GUID copy{};
    if (CoCreateGuid(&copy) != S_OK)
        return false;
    _session_power_plan_guid = guid_text(copy);
    if (_session_power_plan_guid.empty() || !save_journal()) {
        _session_power_plan_guid.clear();
        return false;
    }
    GUID* destination = &copy;
    const DWORD duplicated = PowerDuplicateScheme(nullptr, &original, &destination);
    if (duplicated != ERROR_SUCCESS) {
        if (duplicated == ERROR_ALREADY_EXISTS) {
            _session_power_plan_guid.clear();
            if (!save_journal()) {
                _recovery_failed = true;
                _restoration_status = "Power plan journal conflict";
            }
            return false;
        }
        if (!restore_session_power_plan()) {
            _recovery_failed = true;
            _restoration_status = "Temporary power plan cleanup incomplete";
        } else if (!save_journal()) {
            _recovery_failed = true;
            _restoration_status = "Temporary power plan journal incomplete";
        }
        return false;
    }
    if (PowerSetActiveScheme(nullptr, &copy) != ERROR_SUCCESS) {
        if (!restore_session_power_plan()) {
            _recovery_failed = true;
            _restoration_status = "Temporary power plan cleanup incomplete";
        }
        return false;
    }
    if (enabled(0)) {
        set_status(0, TweakStatus::Applied);
        _applied.push_back("Temporary session power plan");
    }
    return true;
}

bool Optimizer::restore_session_power_plan()
{
    if (_session_power_plan_guid.empty())
        return true;
    GUID copy{}, original{};
    if (!parse_guid(_session_power_plan_guid, copy) ||
        !parse_guid(_saved_power_plan_guid, original))
        return false;
    GUID* active = nullptr;
    if (PowerGetActiveScheme(nullptr, &active) != ERROR_SUCCESS || !active)
        return false;
    const bool ours_active = IsEqualGUID(*active, copy);
    LocalFree(active);
    if (ours_active && PowerSetActiveScheme(nullptr, &original) != ERROR_SUCCESS)
        return false;
    // If another application changed the active plan, preserve its choice.
    const DWORD deleted = PowerDeleteScheme(nullptr, &copy);
    if (deleted != ERROR_SUCCESS && deleted != ERROR_FILE_NOT_FOUND)
        return false;
    _session_power_plan_guid.clear();
    return true;
}

void Optimizer::optimize_accessibility_hotkeys()
{
    AccessibilityState state{};
    state.filter.cbSize = sizeof(FILTERKEYS);
    state.sticky.cbSize = sizeof(STICKYKEYS);
    state.toggle.cbSize = sizeof(TOGGLEKEYS);
    if (!SystemParametersInfoW(SPI_GETFILTERKEYS, sizeof(FILTERKEYS),
                               &state.filter, 0) ||
        !SystemParametersInfoW(SPI_GETSTICKYKEYS, sizeof(STICKYKEYS),
                               &state.sticky, 0) ||
        !SystemParametersInfoW(SPI_GETTOGGLEKEYS, sizeof(TOGGLEKEYS),
                               &state.toggle, 0)) {
        set_status(40, TweakStatus::Failed);
        return;
    }
    state.filter_applied_flags = state.filter.dwFlags;
    state.sticky_applied_flags = state.sticky.dwFlags;
    state.toggle_applied_flags = state.toggle.dwFlags;
    if (!(state.filter.dwFlags & FKF_FILTERKEYSON))
        state.filter_applied_flags &= ~(FKF_HOTKEYACTIVE | FKF_CONFIRMHOTKEY);
    if (!(state.sticky.dwFlags & SKF_STICKYKEYSON))
        state.sticky_applied_flags &= ~(SKF_HOTKEYACTIVE | SKF_CONFIRMHOTKEY);
    if (!(state.toggle.dwFlags & TKF_TOGGLEKEYSON))
        state.toggle_applied_flags &= ~(TKF_HOTKEYACTIVE | TKF_CONFIRMHOTKEY);
    state.filter_touched = state.filter_applied_flags != state.filter.dwFlags;
    state.sticky_touched = state.sticky_applied_flags != state.sticky.dwFlags;
    state.toggle_touched = state.toggle_applied_flags != state.toggle.dwFlags;
    if (!state.filter_touched && !state.sticky_touched && !state.toggle_touched) {
        set_status(40, TweakStatus::AlreadyConfigured);
        return;
    }
    _accessibility_state = state;
    if (!save_journal()) {
        _accessibility_state = {};
        set_status(40, TweakStatus::Failed);
        return;
    }
    bool okay = true;
    if (state.filter_touched) {
        FILTERKEYS desired = state.filter;
        desired.dwFlags = state.filter_applied_flags;
        FILTERKEYS verified{};
        verified.cbSize = sizeof(FILTERKEYS);
        okay = SystemParametersInfoW(SPI_SETFILTERKEYS, sizeof(FILTERKEYS),
                                     &desired, 0) &&
               SystemParametersInfoW(SPI_GETFILTERKEYS, sizeof(FILTERKEYS),
                                     &verified, 0) &&
               verified.dwFlags == desired.dwFlags && okay;
    }
    if (state.sticky_touched) {
        STICKYKEYS desired = state.sticky;
        desired.dwFlags = state.sticky_applied_flags;
        STICKYKEYS verified{};
        verified.cbSize = sizeof(STICKYKEYS);
        okay = SystemParametersInfoW(SPI_SETSTICKYKEYS, sizeof(STICKYKEYS),
                                     &desired, 0) &&
               SystemParametersInfoW(SPI_GETSTICKYKEYS, sizeof(STICKYKEYS),
                                     &verified, 0) &&
               verified.dwFlags == desired.dwFlags && okay;
    }
    if (state.toggle_touched) {
        TOGGLEKEYS desired = state.toggle;
        desired.dwFlags = state.toggle_applied_flags;
        TOGGLEKEYS verified{};
        verified.cbSize = sizeof(TOGGLEKEYS);
        okay = SystemParametersInfoW(SPI_SETTOGGLEKEYS, sizeof(TOGGLEKEYS),
                                     &desired, 0) &&
               SystemParametersInfoW(SPI_GETTOGGLEKEYS, sizeof(TOGGLEKEYS),
                                     &verified, 0) &&
               verified.dwFlags == desired.dwFlags && okay;
    }
    set_status(40, okay ? TweakStatus::Applied : TweakStatus::Failed);
    if (okay)
        _applied.push_back("Accessibility hotkeys off");
}

bool Optimizer::restore_accessibility_hotkeys()
{
    AccessibilityState& state = _accessibility_state;
    bool okay = true;
    if (state.toggle_touched) {
        TOGGLEKEYS current{};
        current.cbSize = sizeof(TOGGLEKEYS);
        if (!SystemParametersInfoW(SPI_GETTOGGLEKEYS, sizeof(TOGGLEKEYS),
                                   &current, 0))
            okay = false;
        else if (current.dwFlags != state.toggle.dwFlags) {
            TOGGLEKEYS verified{};
            verified.cbSize = sizeof(TOGGLEKEYS);
            if (current.dwFlags != state.toggle_applied_flags ||
                !SystemParametersInfoW(SPI_SETTOGGLEKEYS, sizeof(TOGGLEKEYS),
                                       &state.toggle, 0) ||
                !SystemParametersInfoW(SPI_GETTOGGLEKEYS, sizeof(TOGGLEKEYS),
                                       &verified, 0) ||
                verified.dwFlags != state.toggle.dwFlags)
                okay = false;
        }
    }
    if (state.sticky_touched) {
        STICKYKEYS current{};
        current.cbSize = sizeof(STICKYKEYS);
        if (!SystemParametersInfoW(SPI_GETSTICKYKEYS, sizeof(STICKYKEYS),
                                   &current, 0))
            okay = false;
        else if (current.dwFlags != state.sticky.dwFlags) {
            STICKYKEYS verified{};
            verified.cbSize = sizeof(STICKYKEYS);
            if (current.dwFlags != state.sticky_applied_flags ||
                !SystemParametersInfoW(SPI_SETSTICKYKEYS, sizeof(STICKYKEYS),
                                       &state.sticky, 0) ||
                !SystemParametersInfoW(SPI_GETSTICKYKEYS, sizeof(STICKYKEYS),
                                       &verified, 0) ||
                verified.dwFlags != state.sticky.dwFlags)
                okay = false;
        }
    }
    if (state.filter_touched) {
        FILTERKEYS current{};
        current.cbSize = sizeof(FILTERKEYS);
        if (!SystemParametersInfoW(SPI_GETFILTERKEYS, sizeof(FILTERKEYS),
                                   &current, 0))
            okay = false;
        else if (current.dwFlags != state.filter.dwFlags ||
                 current.iWaitMSec != state.filter.iWaitMSec ||
                 current.iDelayMSec != state.filter.iDelayMSec ||
                 current.iRepeatMSec != state.filter.iRepeatMSec ||
                 current.iBounceMSec != state.filter.iBounceMSec) {
            FILTERKEYS verified{};
            verified.cbSize = sizeof(FILTERKEYS);
            if (current.dwFlags != state.filter_applied_flags ||
                current.iWaitMSec != state.filter.iWaitMSec ||
                current.iDelayMSec != state.filter.iDelayMSec ||
                current.iRepeatMSec != state.filter.iRepeatMSec ||
                current.iBounceMSec != state.filter.iBounceMSec ||
                !SystemParametersInfoW(SPI_SETFILTERKEYS, sizeof(FILTERKEYS),
                                       &state.filter, 0) ||
                !SystemParametersInfoW(SPI_GETFILTERKEYS, sizeof(FILTERKEYS),
                                       &verified, 0) ||
                verified.dwFlags != state.filter.dwFlags ||
                verified.iWaitMSec != state.filter.iWaitMSec ||
                verified.iDelayMSec != state.filter.iDelayMSec ||
                verified.iRepeatMSec != state.filter.iRepeatMSec ||
                verified.iBounceMSec != state.filter.iBounceMSec)
                okay = false;
        }
    }
    if (okay)
        state = {};
    return okay;
}

void Optimizer::optimize_display_refresh()
{
    DISPLAY_DEVICEA device{};
    device.cb = sizeof(device);
    bool found = false;
    for (DWORD index = 0; EnumDisplayDevicesA(nullptr, index, &device, 0); ++index) {
        if (device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) {
            found = true;
            break;
        }
        device = {};
        device.cb = sizeof(device);
    }
    if (!found) {
        set_status(28, TweakStatus::Failed);
        return;
    }
    DEVMODEA current{};
    current.dmSize = sizeof(current);
    if (!EnumDisplaySettingsA(device.DeviceName, ENUM_CURRENT_SETTINGS, &current)) {
        set_status(28, TweakStatus::Failed);
        return;
    }
    DEVMODEA best = current;
    for (DWORD index = 0; ; ++index) {
        DEVMODEA candidate{};
        candidate.dmSize = sizeof(candidate);
        if (!EnumDisplaySettingsA(device.DeviceName, index, &candidate))
            break;
        if (candidate.dmPelsWidth == current.dmPelsWidth &&
            candidate.dmPelsHeight == current.dmPelsHeight &&
            candidate.dmBitsPerPel == current.dmBitsPerPel &&
            candidate.dmDisplayOrientation == current.dmDisplayOrientation &&
            candidate.dmDisplayFixedOutput == current.dmDisplayFixedOutput &&
            candidate.dmDisplayFrequency > best.dmDisplayFrequency)
            best = candidate;
    }
    if (best.dmDisplayFrequency <= current.dmDisplayFrequency) {
        set_status(28, TweakStatus::Skipped);
        return;
    }
    if (ChangeDisplaySettingsExA(device.DeviceName, &best, nullptr,
                                 CDS_TEST, nullptr) != DISP_CHANGE_SUCCESSFUL) {
        set_status(28, TweakStatus::Failed);
        return;
    }
    _display_state = {device.DeviceName, current.dmDisplayFrequency,
                      current.dmPelsWidth, current.dmPelsHeight,
                      current.dmBitsPerPel, best.dmDisplayFrequency};
    if (!save_journal()) {
        _display_state = {};
        set_status(28, TweakStatus::Failed);
        return;
    }
    if (ChangeDisplaySettingsExA(device.DeviceName, &best, nullptr,
                                 0, nullptr) != DISP_CHANGE_SUCCESSFUL) {
        if (restore_display())
            save_journal();
        set_status(28, TweakStatus::Failed);
        return;
    }
    DEVMODEA verified{};
    verified.dmSize = sizeof(verified);
    if (!EnumDisplaySettingsA(device.DeviceName, ENUM_CURRENT_SETTINGS, &verified) ||
        verified.dmDisplayFrequency != best.dmDisplayFrequency) {
        if (restore_display())
            save_journal();
        set_status(28, TweakStatus::Failed);
        return;
    }
    set_status(28, TweakStatus::Applied);
    _applied.push_back("Display refresh rate raised");
}

bool Optimizer::restore_display()
{
    if (_display_state.device.empty())
        return true;
    DEVMODEA current{};
    current.dmSize = sizeof(current);
    if (!EnumDisplaySettingsA(_display_state.device.c_str(),
                              ENUM_CURRENT_SETTINGS, &current))
        return false;
    if (current.dmPelsWidth == _display_state.width &&
        current.dmPelsHeight == _display_state.height &&
        current.dmBitsPerPel == _display_state.bits_per_pel &&
        current.dmDisplayFrequency == _display_state.frequency) {
        _display_state = {};
        return true;
    }
    if (_display_state.applied_frequency &&
        (current.dmPelsWidth != _display_state.width ||
         current.dmPelsHeight != _display_state.height ||
         current.dmBitsPerPel != _display_state.bits_per_pel ||
         current.dmDisplayFrequency != _display_state.applied_frequency))
        return false;
    DEVMODEA original{};
    original.dmSize = sizeof(original);
    original.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL |
                        DM_DISPLAYFREQUENCY;
    original.dmPelsWidth = _display_state.width;
    original.dmPelsHeight = _display_state.height;
    original.dmBitsPerPel = _display_state.bits_per_pel;
    original.dmDisplayFrequency = _display_state.frequency;
    const bool okay = ChangeDisplaySettingsExA(
        _display_state.device.c_str(), &original, nullptr, CDS_TEST, nullptr) ==
        DISP_CHANGE_SUCCESSFUL &&
        ChangeDisplaySettingsExA(_display_state.device.c_str(), &original,
                                 nullptr, 0, nullptr) == DISP_CHANGE_SUCCESSFUL;
    if (okay)
        _display_state = {};
    return okay;
}

bool Optimizer::optimize(const std::string& game_path, bool launch_if_missing)
{
    const std::lock_guard<std::recursive_mutex> lock(_state_mutex);
    if (_recovery_failed || (_saved_tweaks && _saved_tweaks->blocked())) {
        _last_error = _restoration_status;
        if (_last_error.empty() && _saved_tweaks)
            _last_error = _saved_tweaks->error();
        return false;
    }
    if (_is_optimized) {
        _last_error = "A session is already active";
        return false;
    }

    _applied.clear();
    _failed.clear();
    for (size_t index = 0; index < _tweak_status.size(); ++index)
        _tweak_status[index] = enabled(index) ? TweakStatus::On : TweakStatus::Off;
    for (size_t index = 26; index < _tweak_status.size(); ++index)
        set_status(index, TweakStatus::Skipped);
    for (size_t index : {size_t(11), size_t(12), size_t(13), size_t(14),
                         size_t(19), size_t(20), size_t(21), size_t(22)})
        set_status(index, TweakStatus::Skipped);
    _last_error.clear();
    _last_helper_scan = GetTickCount64();
    _restoration_status.clear();
    _restoration_succeeded = false;
    save_power_plan(_saved_power_plan_guid, sizeof(_saved_power_plan_guid));
    if (!save_journal()) {
        _last_error = "Could not create recovery log";
        return false;
    }
    const bool power_session_needed = enabled(0) || enabled(23) ||
                                      enabled(24) || enabled(25) ||
                                      enabled(37) || enabled(38) || enabled(39);
    const bool power_session_ready = !power_session_needed ||
                                     begin_session_power_plan();
    if (_recovery_failed) {
        _last_error = _restoration_status;
        return false;
    }
    if (!power_session_ready) {
        for (size_t index : {size_t(0), size_t(23), size_t(24), size_t(25),
                             size_t(37), size_t(38), size_t(39)})
            if (enabled(index))
                set_status(index, TweakStatus::Failed);
        _failed.push_back("Temporary power plan unavailable");
    }
    if (power_session_ready && enabled(23))
        set_ac_power_setting(0, 0, 23, "AC CPU performance preference");
    if (power_session_ready && enabled(24))
        set_ac_power_setting(1, 0, 24, "AC PCIe link power saving off");
    if (power_session_ready && enabled(25))
        set_ac_power_setting(2, 1, 25, "AC CPU boost mode");
    if (power_session_ready && enabled(37)) {
        set_ac_power_setting(3, 100, 37, "Core parking minimum");
        set_ac_power_setting(4, 100, 37, "Core parking efficiency class");
    }
    if (power_session_ready && enabled(38))
        set_ac_power_setting(5, 0, 38, "AC Wi-Fi performance");
    if (power_session_ready && enabled(39))
        set_ac_power_setting(6, 0, 39, "USB selective suspend off");
    if (enabled(28))
        optimize_display_refresh();

    if (enabled(1)) {
        _power_request_active = SetThreadExecutionState(
            ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED) != 0;
        record(_power_request_active, "Sleep paused during session");
    }

    if (enabled(2))
        set_dword(HKEY_CURRENT_USER, "System\\GameConfigStore", "GameDVR_Enabled", 0,
                  "Game DVR off");
    if (enabled(3))
        set_dword(HKEY_CURRENT_USER,
                  "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\GameDVR",
                  "AppCaptureEnabled", 0, "Background capture off");
    if (enabled(4))
        set_dword(HKEY_CURRENT_USER, "Software\\Microsoft\\GameBar",
                  "ShowStartupPanel", 0, "Game Bar popup off");
    if (enabled(5)) {
        set_dword(HKEY_CURRENT_USER, "Software\\Microsoft\\GameBar",
                  "AllowAutoGameMode", 1, "Game Mode allowed");
        set_dword(HKEY_CURRENT_USER, "Software\\Microsoft\\GameBar",
                  "AutoGameModeEnabled", 1, "Game Mode on");
    }

    // Keep MMCSS inside documented ranges. GPU Priority is documented as unused.
    if (enabled(6))
        set_dword(HKEY_LOCAL_MACHINE, MMCSS_PROFILE, "NetworkThrottlingIndex",
                  0xFFFFFFFF, "MMCSS network throttling off");
    if (enabled(7))
        set_dword(HKEY_LOCAL_MACHINE, MMCSS_PROFILE, "SystemResponsiveness", 10,
                  "MMCSS system reserve: 10%");
    if (enabled(8))
        set_dword(HKEY_LOCAL_MACHINE, MMCSS_GAMES, "Priority", 6,
                  "MMCSS game priority");
    if (enabled(9))
        set_string(HKEY_LOCAL_MACHINE, MMCSS_GAMES, "Scheduling Category", "Medium",
                   "MMCSS game scheduling");
    if (enabled(10))
        set_dword(HKEY_LOCAL_MACHINE,
                  "SYSTEM\\CurrentControlSet\\Control\\PriorityControl",
                  "Win32PrioritySeparation", 0x26,
                  "Foreground CPU scheduling");

    if (enabled(11) || enabled(19) || enabled(20) || enabled(21) || enabled(22) ||
        enabled(29) || enabled(30) || enabled(31) || enabled(32) ||
        enabled(33) || enabled(34) || enabled(35) || enabled(36))
        throttle_background_processes();

    if (enabled(15))
        set_dword(HKEY_CURRENT_USER, "Software\\Microsoft\\GameBar",
                  "UseNexusForGameBarEnabled", 0, "Controller Game Bar shortcut off");
    if (enabled(16))
        set_dword(HKEY_CURRENT_USER,
                  "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\GameDVR",
                  "VKMToggleRecording", 0, "Recording shortcut off");
    if (enabled(17))
        set_dword(HKEY_CURRENT_USER,
                  "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\GameDVR",
                  "VKMSaveHistoricalVideo", 0, "Replay shortcut off");
    if (enabled(18))
        set_dword(HKEY_CURRENT_USER,
                  "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\GameDVR",
                  "VKMToggleGameBar", 0, "Game Bar shortcut off");
    if (enabled(40))
        optimize_accessibility_hotkeys();

    if (!is_admin())
        _failed.push_back("Administrator rights required for system tweaks");

    _is_optimized = true;
    if (!game_path.empty() && !launch_game(game_path, launch_if_missing)) {
        const std::string launch_error = _last_error;
        restore();
        _last_error = launch_error;
        return false;
    }
    return true;
}

void Optimizer::restore()
{
    const std::lock_guard<std::recursive_mutex> lock(_state_mutex);
    if (!_is_optimized)
        return;

    if (_game_process) {
        CloseHandle(_game_process);
        _game_process = nullptr;
        _game_pid = 0;
    }
    _waiting_for_game = false;
    _target_game_path.clear();
    bool restored = restore_accessibility_hotkeys();
    restored = restore_background_processes() && restored;
    restored = restore_registry() && restored;
    if (_session_power_plan_guid.empty())
        restored = restore_ac_power_settings() && restored;
    else
        _power_settings.clear();
    restored = restore_display() && restored;
    if (!_session_power_plan_guid.empty())
        restored = restore_session_power_plan() && restored;
    else if (_saved_power_plan_guid[0])
        restored = set_power_plan(_saved_power_plan_guid) && restored;
    if (_power_request_active) {
        SetThreadExecutionState(ES_CONTINUOUS);
        _power_request_active = false;
    }
    _restoration_succeeded = restored && !_journal_path.empty() &&
                             DeleteFileA(_journal_path.c_str());
    if (_restoration_succeeded)
        _restoration_status = "Settings restored";
    else {
        _restoration_status = "Restore incomplete - recovery log kept";
        _recovery_failed = true;
    }
    _is_optimized = false;
}

bool Optimizer::is_optimized() const { return _is_optimized; }
bool Optimizer::has_game_process() const { return _game_process != nullptr; }
DWORD Optimizer::game_pid() const { return _game_pid; }
bool Optimizer::restoration_succeeded() const { return _restoration_succeeded; }
bool Optimizer::recovery_blocked() const
{
    return _recovery_failed || (_saved_tweaks && _saved_tweaks->blocked());
}
bool Optimizer::retry_recovery()
{
    const std::lock_guard<std::recursive_mutex> lock(_state_mutex);
    if (_is_optimized)
        return false;
    if (GetFileAttributesA(_journal_path.c_str()) != INVALID_FILE_ATTRIBUTES)
        recover_journal();
    else
        _recovery_failed = false;
    if (!_recovery_failed && _saved_tweaks && !_saved_tweaks->recover()) {
        _recovery_failed = true;
        _restoration_status = _saved_tweaks->error();
    }
    return !_recovery_failed;
}
bool Optimizer::is_game_active(const std::string& game_path) const
{
    if (game_path.empty())
        return false;
    return running_game_pid(target_game_path(game_path)) != 0;
}

bool Optimizer::find_target_process()
{
    const DWORD pid = running_game_pid(_target_game_path);
    if (!pid)
        return false;
    HANDLE candidate = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                                   FALSE, pid);
    if (!candidate)
        return false;
    if (_game_process)
        CloseHandle(_game_process);
    _game_process = candidate;
    _game_pid = pid;
    _waiting_for_game = false;

    HANDLE tune = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION |
                              PROCESS_SET_INFORMATION | SYNCHRONIZE, FALSE, pid);
    const bool tracked = tune && track_process_for_restore(tune, pid);
    tune_game_process(tune, tracked);
    if (tune)
        CloseHandle(tune);
    return true;
}

bool Optimizer::is_game_running()
{
    const std::lock_guard<std::recursive_mutex> lock(_state_mutex);
    if (_waiting_for_game) {
        if (find_target_process())
            return true;
        return GetTickCount64() - _launch_started < 120000;
    }
    if (!_game_process)
        return false;
    DWORD exit_code = 0;
    const bool running = GetExitCodeProcess(_game_process, &exit_code) &&
                         exit_code == STILL_ACTIVE;
    if (running && GetTickCount64() - _last_helper_scan >= 5000) {
        _last_helper_scan = GetTickCount64();
        throttle_background_processes();
    }
    return running;
}

bool Optimizer::is_waiting_for_game() const { return _waiting_for_game; }

std::string Optimizer::game_status() const
{
    if (_waiting_for_game)
        return "Waiting for Fortnite";
    if (_game_process)
        return "Game active (PID " + std::to_string(_game_pid) + ")";
    return "Windows only";
}

const std::vector<std::string>& Optimizer::applied_tweaks() const { return _applied; }
const std::vector<std::string>& Optimizer::failed_tweaks() const { return _failed; }
const std::string& Optimizer::last_error() const { return _last_error; }
const std::string& Optimizer::restoration_status() const { return _restoration_status; }
