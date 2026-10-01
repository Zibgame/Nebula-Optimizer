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
        okay = settings.size() == 41 && settings.front().enabled &&
               !settings[15].enabled && settings[23].enabled &&
               !settings[24].enabled && settings[36].enabled &&
               !settings[37].enabled && settings[38].enabled &&
               !settings[39].enabled && !settings[40].enabled &&
               settings[36].category == "Memory";
        okay = optimizer.toggle_tweak(0) && okay;
        okay = optimizer.toggle_tweak(36) && okay;
    }
    {
        Optimizer optimizer;
        const auto settings = optimizer.tweak_settings();
        okay = !settings.front().enabled && !settings[36].enabled && okay;
    }
    std::filesystem::remove_all(std::filesystem::path(unique));
    if (!okay)
        std::cerr << "Preference test failed\n";
    return okay ? 0 : 3;
}
