#pragma once

#include <array>
#include <cstdint>
#include <string>
#include "json.hpp"
#include "tweak_catalog.hpp"

// Durable settings are deliberately separate from the session recovery journal.
// A saved setting is never silently re-applied after an external change.
class SavedTweaks {
public:
    struct Info {
        bool eligible = false;
        bool saved = false;
        bool base = false;
        bool unknown = true;
        bool drift = false;
    };

    explicit SavedTweaks(const std::string& directory);
    bool recover();
    bool scan(const std::string& game_path);
    bool has_baseline() const;
    bool has_baseline_for(const std::string& game_path) const;
    bool restore_baseline(const std::string& game_path);
    void set_filter_repeat_ms(std::uint32_t value) { _filter_repeat_ms = value; }
    void set_tunable_values(std::uint32_t mmcss, std::uint32_t epp,
                            std::uint32_t parking, std::uint32_t minimum) {
        _mmcss_reserve = mmcss; _cpu_epp = epp;
        _core_parking = parking; _cpu_minimum = minimum;
    }
    bool apply(size_t index, const std::string& game_path,
               bool confirmed, bool (*confirm_display)() = nullptr);
    bool remove(size_t index);
    bool reapply(size_t index, const std::string& game_path,
                 bool confirmed, bool (*confirm_display)() = nullptr);
    Info info(size_t index) const;
    bool blocked() const { return _blocked; }
    const std::string& error() const { return _error; }

private:
    using json = nlohmann::json;
    bool load();
    bool write(const std::string& path, const json& data) const;
    bool targets(size_t index, const std::string& game_path, json& out) const;
    bool read_target(const json& target, json& value) const;
    bool write_target(const json& target, const json& value,
                      bool (*confirm_display)()) const;
    bool perform(size_t index, const std::string& game_path, bool confirmed,
                 bool (*confirm_display)());
    bool rollback(const json& operation);
    bool import_base_settings(const std::array<json, TWEAK_COUNT>& targets,
                              const std::array<json, TWEAK_COUNT>& values,
                              const std::array<bool, TWEAK_COUNT>& known);
    void refresh_saved(const std::string& game_path);

    std::string _scan_path, _saved_path, _dismissed_path, _operation_path;
    json _scan = json::object();
    json _saved = json::object();
    json _dismissed = json::object();
    std::array<Info, TWEAK_COUNT> _info{};
    bool _blocked = false;
    std::string _error;
    std::uint32_t _filter_repeat_ms = 10;
    std::uint32_t _mmcss_reserve = 10;
    std::uint32_t _cpu_epp = 0;
    std::uint32_t _core_parking = 100;
    std::uint32_t _cpu_minimum = 100;
};
