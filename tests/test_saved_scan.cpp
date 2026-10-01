#include "saved_tweaks.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <windows.h>

int main()
{
    char temporary[MAX_PATH]{};
    char unique[MAX_PATH]{};
    if (!GetTempPathA(sizeof(temporary), temporary) ||
        !GetTempFileNameA(temporary, "neb", 0, unique)) return 1;
    DeleteFileA(unique);
    if (!CreateDirectoryA(unique, nullptr)) return 2;
    bool okay = true;
    {
        SavedTweaks saved(unique);
        okay = saved.recover() && saved.scan("") && okay;
        okay = !saved.info(1).eligible && saved.info(2).eligible &&
               saved.info(12).unknown && saved.info(40).eligible && okay;
        nlohmann::json first;
        { std::ifstream file(std::string(unique)+"\\baseline.json"); file >> first; }
        okay = first.at("gpu_preference").at("first").is_null() && okay;
        okay = saved.scan("") && okay;
        nlohmann::json second;
        { std::ifstream file(std::string(unique)+"\\baseline.json"); file >> second; }
        okay = first.at("game_dvr").at("first") ==
               second.at("game_dvr").at("first") && okay;
        size_t candidate=41;
        for (size_t i=0;i<41;++i) {
            const auto item=saved.info(i);
            if (item.saved) okay=item.base && item.eligible && okay;
            if (candidate==41 && item.saved && i!=40) candidate=i;
        }
        if (candidate<41) {
            okay=saved.remove(candidate) && !saved.info(candidate).saved && okay;
            okay=saved.scan("") && !saved.info(candidate).saved && okay;
            SavedTweaks reopened(unique);
            okay=reopened.recover() && reopened.scan("") &&
                 !reopened.info(candidate).saved && okay;
        }
    }
    std::filesystem::remove_all(std::filesystem::path(unique));
    if (!okay) std::cerr << "Saved scan test failed\n";
    return okay ? 0 : 3;
}
