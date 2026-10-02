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
        okay = saved.recover() && !saved.has_baseline() &&
               saved.scan("") && saved.has_baseline() && okay;
        okay = !saved.has_baseline_for("C:\\Games\\Fortnite.exe") && okay;
        okay = !saved.info(1).eligible && saved.info(2).eligible &&
               saved.info(12).unknown && saved.info(40).eligible &&
               saved.info(41).eligible && saved.info(44).eligible &&
               saved.info(45).eligible && okay;
        nlohmann::json first;
        { std::ifstream file(std::string(unique)+"\\baseline.json"); file >> first; }
        okay = first.at("gpu_preference").at("first").is_null() && okay;
        okay = first.at("game_dvr").contains("baseline") &&
               first.at("game_dvr").contains("baseline_targets") &&
               saved.restore_baseline("") && okay;
        okay = saved.scan("") && okay;
        nlohmann::json second;
        { std::ifstream file(std::string(unique)+"\\baseline.json"); file >> second; }
        okay = first.at("game_dvr").at("first") ==
               second.at("game_dvr").at("first") && okay;
        size_t candidate=60;
        for (size_t i=0;i<60;++i) {
            const auto item=saved.info(i);
            if (item.saved) okay=item.base && item.eligible && okay;
            if (candidate==60 && item.saved && i!=40) candidate=i;
        }
        if (candidate<60) {
            okay=saved.remove(candidate) && !saved.info(candidate).saved && okay;
            okay=saved.scan("") && !saved.info(candidate).saved && okay;
            SavedTweaks reopened(unique);
            okay=reopened.recover() && reopened.scan("") &&
                 !reopened.info(candidate).saved && okay;
        }
    }
    {
        const std::string directory=std::string(unique)+"\\restore-test";
        std::filesystem::create_directories(directory);
        const std::string key_path="Software\\NebulaOptimizerBaselineTest";
        HKEY key=nullptr;
        DWORD current=22;
        RegCreateKeyExA(HKEY_CURRENT_USER,key_path.c_str(),0,nullptr,0,
                        KEY_SET_VALUE|KEY_QUERY_VALUE,nullptr,&key,nullptr);
        RegSetValueExA(key,"Value",0,REG_DWORD,
            reinterpret_cast<const BYTE*>(&current),sizeof(current));
        RegCloseKey(key);
        DWORD baseline=11;
        std::vector<unsigned char> bytes(reinterpret_cast<unsigned char*>(&baseline),
            reinterpret_cast<unsigned char*>(&baseline)+sizeof(baseline));
        nlohmann::json target={{"kind","registry"},{"root","HKCU"},
            {"path",key_path},{"name","Value"},
            {"desired",{{"exists",true},{"type",REG_DWORD},{"data",bytes}}}};
        nlohmann::json file={{"_meta",{{"complete",true}}},
            {"game_dvr",{{"baseline",nlohmann::json::array({target["desired"]})},
                          {"baseline_targets",nlohmann::json::array({target})}}}};
        { std::ofstream out(directory+"\\baseline.json"); out << file.dump(2); }
        SavedTweaks saved(directory);
        okay=saved.has_baseline() && saved.restore_baseline("") && okay;
        key=nullptr; DWORD size=sizeof(current),type=0; current=0;
        okay=RegOpenKeyExA(HKEY_CURRENT_USER,key_path.c_str(),0,KEY_QUERY_VALUE,&key)==ERROR_SUCCESS &&
            RegQueryValueExA(key,"Value",nullptr,&type,
                reinterpret_cast<BYTE*>(&current),&size)==ERROR_SUCCESS &&
            type==REG_DWORD && current==baseline && okay;
        if (key) RegCloseKey(key);
        RegDeleteTreeA(HKEY_CURRENT_USER,key_path.c_str());
    }
    std::filesystem::remove_all(std::filesystem::path(unique));
    if (!okay) std::cerr << "Saved scan test failed\n";
    return okay ? 0 : 3;
}
