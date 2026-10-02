#include "optimizer.hpp"

#include <filesystem>
#include <iostream>
#include <windows.h>

int main()
{
    char temporary[MAX_PATH]{};
    char unique[MAX_PATH]{};
    if (!GetTempPathA(sizeof(temporary), temporary) ||
        !GetTempFileNameA(temporary, "neb", 0, unique))
        return 1;
    DeleteFileA(unique);
    if (!CreateDirectoryA(unique, nullptr) ||
        !SetEnvironmentVariableA("LOCALAPPDATA", unique))
        return 2;

    bool okay = true;
    {
        Optimizer optimizer;
        const auto settings = optimizer.tweak_settings();
        okay = settings.size() == 60 && settings.front().enabled &&
               !settings[15].enabled && settings[23].enabled &&
               !settings[24].enabled && settings[36].enabled &&
               !settings[37].enabled && settings[38].enabled &&
               !settings[39].enabled && !settings[40].enabled &&
               !settings[41].enabled && !settings[42].enabled &&
               !settings[43].enabled && !settings[44].enabled &&
               settings[45].enabled && settings[46].enabled &&
               !settings[47].enabled && !settings[48].enabled &&
               !settings[49].enabled && !settings[50].enabled &&
               settings[51].enabled && settings[52].enabled &&
               settings[53].enabled && !settings[54].enabled &&
               !settings[55].enabled && settings[56].enabled &&
               !settings[57].enabled && !settings[58].enabled &&
               !settings[59].enabled &&
               settings[36].category == "Memory";
        okay = optimizer.toggle_tweak(0) && okay;
        okay = optimizer.toggle_tweak(36) && okay;
        okay = optimizer.adjust_tweak_value(41, 1) && okay;
        okay = optimizer.adjust_tweak_value(44, -1) && okay;
        okay = !optimizer.adjust_tweak_value(0, 1) && okay;
        okay = optimizer.enable_all_tweaks() && okay;
    }
    {
        Optimizer optimizer;
        const auto settings = optimizer.tweak_settings();
        bool all_enabled = true;
        for (const auto& setting : settings)
            all_enabled = all_enabled && setting.enabled;
        okay = all_enabled && settings[41].value == "25 ms" &&
               settings[44].value == "95%" && okay;
    }
    std::filesystem::remove_all(std::filesystem::path(unique));
    if (!okay)
        std::cerr << "Preference test failed\n";
    return okay ? 0 : 3;
}
