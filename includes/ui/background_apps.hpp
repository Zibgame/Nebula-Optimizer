#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct RunningBackgroundApp {
    std::string path;
    std::string name;
    unsigned process_count = 0;
    std::uint64_t working_set_bytes = 0;
};

struct ClosedAppsSummary {
    unsigned closed = 0;
    unsigned skipped = 0;
    unsigned failed = 0;
    std::uint64_t closed_working_set_bytes = 0;
    std::int64_t available_ram_delta_bytes = 0;
};

class BackgroundApps {
public:
    BackgroundApps();

    const std::vector<std::string>& selected() const { return _selected; }
    std::vector<RunningBackgroundApp> running() const;
    bool add(const std::string& path);
    bool remove(size_t index);
    ClosedAppsSummary close_selected(const std::string& game_path) const;

private:
    bool save() const;
    std::string _settings_path;
    std::vector<std::string> _selected;
};
