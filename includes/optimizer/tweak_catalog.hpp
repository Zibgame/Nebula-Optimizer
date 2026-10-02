#pragma once

#include <array>
#include <cstddef>

struct TweakDefinition {
    const char* id;
    const char* label;
    const char* category;
    bool default_enabled;
    bool aggressive;
    bool saved_eligible;
};

inline constexpr std::array<TweakDefinition, 60> TWEAK_CATALOG{{
    {"power_plan", "Temporary session power plan", "CPU & Power", true, false, false},
    {"prevent_sleep", "Prevent sleep while gaming", "CPU & Power", true, false, false},
    {"game_dvr", "Disable Game DVR", "Input & Capture", true, false, true},
    {"background_capture", "Disable background capture", "Input & Capture", true, false, true},
    {"game_bar_popup", "Hide Game Bar startup popup", "Input & Capture", true, false, true},
    {"game_mode", "Enable Windows Game Mode", "CPU & Power", true, false, true},
    {"mmcss_network", "[Exp] Remove MMCSS network throttle", "Background", false, true, true},
    {"mmcss_reserve", "[Exp] MMCSS system reserve", "CPU & Power", false, true, true},
    {"mmcss_priority", "[Exp] MMCSS game priority", "CPU & Power", false, true, true},
    {"mmcss_scheduling", "[Exp] MMCSS game scheduling", "CPU & Power", false, true, true},
    {"foreground_cpu", "[Exp] Favor foreground CPU", "CPU & Power", false, true, true},
    {"background_onedrive", "Reduce cloud sync priority", "Background", true, false, false},
    {"gpu_preference", "Prefer high-performance GPU", "GPU & Display", true, false, true},
    {"game_ecoqos", "Disable game EcoQoS", "CPU & Power", true, false, false},
    {"game_priority", "High game CPU priority", "CPU & Power", true, false, false},
    {"game_bar_controller", "Disable controller Game Bar shortcut", "Input & Capture", false, false, true},
    {"recording_hotkey", "Disable recording hotkey", "Input & Capture", false, false, true},
    {"history_hotkey", "Disable replay hotkey", "Input & Capture", false, false, true},
    {"game_bar_hotkey", "Disable Game Bar hotkey", "Input & Capture", false, false, true},
    {"background_indexer", "Reduce search indexing priority", "Background", true, false, false},
    {"background_widgets", "Reduce Widgets priority", "Background", true, false, false},
    {"background_phone_link", "Reduce Phone Link priority", "Background", true, false, false},
    {"background_adobe", "Reduce Adobe helper priority", "Background", true, false, false},
    {"ac_cpu_epp", "AC CPU performance preference", "CPU & Power", true, false, true},
    {"ac_pcie_aspm", "AC PCIe link power saving off", "GPU & Display", false, true, true},
    {"ac_cpu_boost", "AC CPU boost mode", "CPU & Power", true, false, true},
    {"game_dynamic_boost", "Game dynamic priority boost", "CPU & Power", true, false, false},
    {"game_memory_normal", "Game memory priority normal", "Memory", true, false, false},
    {"display_max_refresh", "Highest display refresh rate", "GPU & Display", true, false, true},
    {"memory_cloud", "Cloud sync page retention", "Memory", true, false, false},
    {"memory_indexer", "Indexer page retention", "Memory", true, false, false},
    {"memory_widgets", "Widgets page retention", "Memory", true, false, false},
    {"memory_phone_link", "Phone Link page retention", "Memory", true, false, false},
    {"memory_adobe", "Adobe page retention", "Memory", true, false, false},
    {"updater_cpu", "Updater CPU priority", "Background", true, false, false},
    {"updater_ecoqos", "Updater EcoQoS", "Background", true, false, false},
    {"updater_memory", "Updater memory priority", "Memory", true, false, false},
    {"ac_core_parking", "[Exp] Core parking minimum", "CPU & Power", false, true, true},
    {"ac_wifi_performance", "AC Wi-Fi performance", "CPU & Power", true, false, true},
    {"diagnostic_usb_suspend", "[Diag] USB selective suspend off", "Input & Capture", false, true, true},
    {"accessibility_hotkeys", "Accessibility hotkeys off", "Input & Capture", false, true, true},
    {"filterkeys_fast_repeat", "[Exp] FilterKeys fast repeat", "Input & Capture", false, true, true},
    {"keyboard_repeat_delay", "Shortest keyboard repeat delay", "Input & Capture", false, false, true},
    {"keyboard_repeat_speed", "Fastest keyboard repeat rate", "Input & Capture", false, false, true},
    {"ac_cpu_minimum_100", "[Exp] AC CPU minimum", "CPU & Power", false, true, true},
    {"fastest_dns", "Fastest DNS for active adapter", "Network", true, false, true},
    {"ethernet_rss", "Enable Ethernet RSS", "Network", true, false, true},
    {"nic_interrupt_moderation", "[Agg] Disable interrupt moderation", "Network", false, true, true},
    {"ethernet_rsc", "[Agg] Disable Ethernet RSC", "Network", false, true, true},
    {"energy_efficient_ethernet", "[Agg] Disable Energy Efficient Ethernet", "Network", false, true, true},
    {"nic_flow_control", "[Agg] Disable NIC flow control", "Network", false, true, true},
    {"nic_power_saving", "Disable active NIC power saving", "Network", true, false, true},
    {"wifi_mimo_smps", "Wi-Fi MIMO: No SMPS", "Network", true, false, true},
    {"wifi_transmit_power", "Wi-Fi transmit power: Highest", "Network", true, false, true},
    {"game_performance_cpu_sets", "[Agg] Performance-core CPU Sets", "CPU & Power", false, true, false},
    {"game_io_priority", "[Agg] High game I/O priority", "CPU & Power", false, true, false},
    {"background_io_priority", "Low background I/O priority", "Background", true, false, false},
    {"global_timer_resolution", "[Agg] Global timer resolution: 0.5 ms", "CPU & Power", false, true, false},
    {"pause_search_indexing", "[Agg] Pause Search indexing", "Background", false, true, false},
    {"pause_update_downloads", "[Agg] Pause update downloads", "Background", false, true, false}
}};

inline constexpr std::size_t TWEAK_COUNT = TWEAK_CATALOG.size();
