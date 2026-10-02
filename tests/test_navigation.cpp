#include "optimizer.hpp"
#include "tweak_navigation.hpp"

#include <filesystem>
#include <windows.h>

int main()
{
    char temporary[MAX_PATH]{};
    char unique[MAX_PATH]{};
    if (!GetTempPathA(sizeof(temporary), temporary) ||
        !GetTempFileNameA(temporary, "nav", 0, unique))
        return 1;
    DeleteFileA(unique);
    if (!CreateDirectoryA(unique, nullptr) ||
        !SetEnvironmentVariableA("LOCALAPPDATA", unique))
        return 2;
    Optimizer optimizer;
    const auto settings = optimizer.tweak_settings();
    size_t total = 0;
    for (const char* category : {"CPU & Power", "GPU & Display", "Memory",
                                 "Background", "Input & Capture", "Network"})
        total += tweaks_in_category(settings, category).size();
    const bool okay = total == 60 &&
        make_carousel({"CPU", "GPU", "RAM"}).text ==
            "TWEAKS   CPU   GPU   RAM " &&
        make_carousel({"CPU", "GPU"}).tab_columns[0].first == 12 &&
        carousel_tab_at(make_carousel({"CPU", "GPU"}), 12) == 0 &&
        carousel_tab_at(make_carousel({"CPU", "GPU"}), 16) == 0 &&
        carousel_tab_at(make_carousel({"CPU", "GPU"}), 17) == 2 &&
        carousel_tab_at(make_carousel({"CPU", "GPU"}), 18) == 1 &&
        move_carousel(0, -1, 5) == 4 &&
        move_carousel(4, 1, 5) == 0 &&
        move_tweak_cursor(0, -1, 5) == 0 &&
        move_tweak_cursor(4, 1, 5) == 4 &&
        move_tweak_cursor(0, 1, 5) == 1 &&
        scroll_to_cursor(0, 4, 1) == 4 &&
        scroll_to_cursor(4, 3, 1) == 3;
    std::filesystem::remove_all(std::filesystem::path(unique));
    return okay ? 0 : 3;
}
