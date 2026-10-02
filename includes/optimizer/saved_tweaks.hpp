#pragma once

#include <array>
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
};
