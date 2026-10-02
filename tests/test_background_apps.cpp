#include "background_apps.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <windows.h>

int main()
{
    wchar_t temporary[MAX_PATH]{};
    wchar_t unique[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, temporary) ||
        !GetTempFileNameW(temporary, L"nba", 0, unique)) return 1;
    DeleteFileW(unique);
    if (!CreateDirectoryW(unique, nullptr) ||
        !SetEnvironmentVariableW(L"LOCALAPPDATA", unique)) return 2;

    const auto target = std::filesystem::path(unique) / "SampleBackground.exe";
    { std::ofstream file(target); file << "test fixture"; }
    const auto protected_target = std::filesystem::path(unique) /
                                  "FortniteClient-Win64-Shipping.exe";
    { std::ofstream file(protected_target); file << "test fixture"; }
    bool okay = true;
    {
        BackgroundApps apps;
        okay = apps.selected().empty() && apps.add(target.string()) && okay;
        okay = !apps.add(target.string()) && apps.selected().size() == 1 && okay;
        okay = !apps.add(protected_target.string()) && okay;
        const auto summary = apps.close_selected("");
        okay = summary.closed == 0 && summary.failed == 0 && okay;
    }
    {
        BackgroundApps apps;
        okay = apps.selected().size() == 1 && apps.remove(0) && okay;
        DWORD before = 0;
        DWORD after = 0;
        okay = GetProcessHandleCount(GetCurrentProcess(), &before) && okay;
        for (int i = 0; i < 5; ++i) {
            apps.running();
            apps.close_selected("");
        }
        okay = GetProcessHandleCount(GetCurrentProcess(), &after) &&
               after <= before + 2 && okay;
    }
    {
        BackgroundApps apps;
        okay = apps.selected().empty() && okay;
    }
    std::filesystem::remove_all(std::filesystem::path(unique));
    if (!okay) std::cerr << "Background app settings test failed\n";
    return okay ? 0 : 3;
}
