#include "saved_tweaks.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <windows.h>
#include <powrprof.h>
#include <objbase.h>

namespace {
using json = nlohmann::json;
constexpr const char* ids[41] = {
    "power_plan", "prevent_sleep", "game_dvr", "background_capture",
    "game_bar_popup", "game_mode", "mmcss_network", "mmcss_reserve",
    "mmcss_priority", "mmcss_scheduling", "foreground_cpu",
    "background_onedrive", "gpu_preference", "game_ecoqos", "game_priority",
    "game_bar_controller", "recording_hotkey", "history_hotkey",
    "game_bar_hotkey", "background_indexer", "background_widgets",
    "background_phone_link", "background_adobe", "ac_cpu_epp",
    "ac_pcie_aspm", "ac_cpu_boost", "game_dynamic_boost",
    "game_memory_normal", "display_max_refresh", "memory_cloud",
    "memory_indexer", "memory_widgets", "memory_phone_link",
    "memory_adobe", "updater_cpu", "updater_ecoqos",
    "updater_memory", "ac_core_parking", "ac_wifi_performance",
    "diagnostic_usb_suspend", "accessibility_hotkeys"
};
constexpr const char* profile =
    "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile";
constexpr const char* games =
    "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games";
constexpr const char* gamebar = "Software\\Microsoft\\GameBar";
constexpr const char* gamedvr = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\GameDVR";
constexpr GUID cpu_group = {0x54533251,0x82be,0x4824,{0x96,0xc1,0x47,0xb6,0x0b,0x74,0x0d,0x00}};
constexpr GUID pcie_group = {0x501a4d13,0x42af,0x4429,{0x9f,0xd1,0xa8,0x21,0x8c,0x26,0x8e,0x20}};
constexpr GUID wifi_group = {0x19cbb8fa,0x5279,0x450e,{0x9f,0xac,0x8a,0x3d,0x5f,0xed,0xd0,0xc1}};
constexpr GUID usb_group = {0x2a737441,0x1930,0x4402,{0x8d,0x77,0xb2,0xbe,0xbb,0xa3,0x08,0xa3}};
constexpr GUID settings[] = {
    {0x36687f9e,0xe3a5,0x4dbf,{0xb1,0xdc,0x15,0xeb,0x38,0x1c,0x68,0x63}},
    {0xee12f906,0xd277,0x404b,{0xb6,0xda,0xe5,0xfa,0x1a,0x57,0x6d,0xf5}},
    {0xbe337238,0x0d82,0x4146,{0xa9,0x60,0x4f,0x37,0x49,0xd4,0x70,0xc7}},
    {0x0cc5b647,0xc1df,0x4637,{0x89,0x1a,0xde,0xc3,0x5c,0x31,0x85,0x83}},
    {0x0cc5b647,0xc1df,0x4637,{0x89,0x1a,0xde,0xc3,0x5c,0x31,0x85,0x84}},
    {0x12bbebe6,0x58d6,0x4636,{0x95,0xbb,0x32,0x17,0xef,0x86,0x7c,0x1a}},
    {0x48e6b7a6,0x50f5,0x4782,{0xa5,0xd4,0x53,0xbb,0x8f,0x07,0xe2,0x26}}
};
const GUID& group(unsigned kind) {
    return kind == 1 ? pcie_group : kind == 5 ? wifi_group :
           kind == 6 ? usb_group : cpu_group;
}
std::string guid_string(const GUID& guid) {
    wchar_t wide[40]{};
    char narrow[40]{};
    if (!StringFromGUID2(guid, wide, 40) ||
        !WideCharToMultiByte(CP_ACP, 0, wide, -1, narrow, 40, nullptr, nullptr))
        return {};
    return narrow;
}
bool parse_guid(const std::string& text, GUID& guid) {
    wchar_t wide[40]{};
    return MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, wide, 40) &&
           CLSIDFromString(wide, &guid) == S_OK;
}
bool load_json(const std::string& path, json& data) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    try { file >> data; return data.is_object(); } catch (...) { return false; }
}
json reg(const char* root, const char* path, const std::string& name,
         DWORD type, const std::vector<unsigned char>& bytes) {
    return {{"kind","registry"},{"root",root},{"path",path},{"name",name},
            {"desired",{{"exists",true},{"type",type},{"data",bytes}}}};
}
json dword(const char* root, const char* path, const std::string& name, DWORD value) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
    return reg(root,path,name,REG_DWORD,{bytes,bytes+sizeof(value)});
}
json string_value(const char* root, const char* path, const std::string& name,
                  const std::string& value) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(value.c_str());
    return reg(root,path,name,REG_SZ,{bytes,bytes+value.size()+1});
}
json power(unsigned kind, DWORD value, const std::string& scheme) {
    return {{"kind","power"},{"scheme",scheme},{"setting",kind},
            {"desired",{{"value",value}}}};
}
std::string game_executable(const std::string& path) {
    const size_t split = path.find_last_of("\\/");
    const std::string file = path.substr(split == std::string::npos ? 0 : split+1);
    if (_stricmp(file.c_str(),"FortniteLauncher.exe") == 0 ||
        _stricmp(file.c_str(),"FortniteClient-Win64-Shipping_EAC_EOS.exe") == 0)
        return path.substr(0,split+1)+"FortniteClient-Win64-Shipping.exe";
    return path;
}
bool primary_display(std::string& name, DEVMODEA& mode) {
    DISPLAY_DEVICEA device{}; device.cb = sizeof(device);
    for (DWORD n=0; EnumDisplayDevicesA(nullptr,n,&device,0); ++n) {
        if (device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) {
            name=device.DeviceName;
            mode.dmSize=sizeof(mode);
            return EnumDisplaySettingsA(name.c_str(),ENUM_CURRENT_SETTINGS,&mode);
        }
        device={}; device.cb=sizeof(device);
    }
    return false;
}
json display_mode(const DEVMODEA& mode) {
    const auto* bytes=reinterpret_cast<const unsigned char*>(&mode);
    return {{"width",mode.dmPelsWidth},{"height",mode.dmPelsHeight},
            {"bits",mode.dmBitsPerPel},{"frequency",mode.dmDisplayFrequency},
            {"orientation",mode.dmDisplayOrientation},
            {"fixed",mode.dmDisplayFixedOutput},
            {"position_x",mode.dmPosition.x},{"position_y",mode.dmPosition.y},
            {"raw",std::vector<unsigned char>(bytes,bytes+sizeof(mode))}};
}
void fill_display(const json& value, DEVMODEA& mode) {
    mode={};
    if (value.contains("raw") && value.at("raw").is_array()) {
        const auto raw=value.at("raw").get<std::vector<unsigned char>>();
        if (raw.size()==sizeof(mode)) std::memcpy(&mode,raw.data(),sizeof(mode));
    }
    mode.dmSize=sizeof(mode);
    mode.dmFields=DM_PELSWIDTH|DM_PELSHEIGHT|DM_BITSPERPEL|
                  DM_DISPLAYFREQUENCY|DM_DISPLAYORIENTATION|DM_POSITION;
    mode.dmPelsWidth=value.at("width").get<DWORD>();
    mode.dmPelsHeight=value.at("height").get<DWORD>();
    mode.dmBitsPerPel=value.at("bits").get<DWORD>();
    mode.dmDisplayFrequency=value.at("frequency").get<DWORD>();
    mode.dmDisplayOrientation=value.at("orientation").get<DWORD>();
    mode.dmPosition.x=value.at("position_x").get<LONG>();
    mode.dmPosition.y=value.at("position_y").get<LONG>();
}
bool same_display(const json& a, const json& b) {
    return a.at("width")==b.at("width") && a.at("height")==b.at("height") &&
           a.at("bits")==b.at("bits") && a.at("frequency")==b.at("frequency") &&
           a.at("orientation")==b.at("orientation") &&
           a.at("fixed")==b.at("fixed") &&
           a.at("position_x")==b.at("position_x") &&
           a.at("position_y")==b.at("position_y");
}
bool equal_value(const json& target, const json& a, const json& b) {
    return target.value("kind",std::string()) == "display" ? same_display(a,b) : a==b;
}
bool safe_accessibility_partial(const json& target, const json& current) {
    if (target.value("kind",std::string())!="accessibility") return false;
    const auto& before=target.at("before");
    const auto& desired=target.at("desired");
    for (const auto& field:{"filter_flags","filter_wait","filter_delay",
                            "filter_repeat","filter_bounce","sticky_flags","toggle_flags"})
        if (!current.contains(field) ||
            (current.at(field)!=before.at(field) && current.at(field)!=desired.at(field)))
            return false;
    return true;
}
} // namespace

SavedTweaks::SavedTweaks(const std::string& directory)
    : _scan_path(directory+"\\baseline.json"),
      _saved_path(directory+"\\saved.json"),
      _dismissed_path(directory+"\\saved-dismissed.json"),
      _operation_path(directory+"\\saved-operation.json") {
    load();
    for (size_t i=0;i<_info.size();++i) {
        _info[i].eligible = (i>=2 && i<=10) || i==12 ||
            (i>=15 && i<=18) || (i>=23 && i<=25) || i==28 ||
            (i>=37 && i<=40);
        _info[i].saved = _saved.contains(ids[i]);
    }
}

bool SavedTweaks::load() {
    if (std::filesystem::exists(_scan_path) && !load_json(_scan_path,_scan)) {
        _blocked=true; _error="Baseline file is invalid"; return false;
    }
    if (std::filesystem::exists(_saved_path) && !load_json(_saved_path,_saved)) {
        _blocked=true; _error="Saved settings file is invalid"; return false;
    }
    if (std::filesystem::exists(_dismissed_path) &&
        !load_json(_dismissed_path,_dismissed)) {
        _blocked=true; _error="Saved exclusions file is invalid"; return false;
    }
    return true;
}

bool SavedTweaks::write(const std::string& path, const json& data) const {
    try {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        const std::string temporary=path+".tmp";
        { std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
          if (!file) return false;
          file << data.dump(2); file.flush(); if (!file) return false; }
        return MoveFileExA(temporary.c_str(),path.c_str(),
                           MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    } catch (...) { return false; }
}

bool SavedTweaks::targets(size_t index, const std::string& game_path, json& out) const {
    out=json::array();
    if (index>=41 || !_info[index].eligible) return false;
    const char* user="HKCU"; const char* machine="HKLM";
    switch(index) {
    case 2: out.push_back(dword(user,"System\\GameConfigStore","GameDVR_Enabled",0)); break;
    case 3: out.push_back(dword(user,gamedvr,"AppCaptureEnabled",0)); break;
    case 4: out.push_back(dword(user,gamebar,"ShowStartupPanel",0)); break;
    case 5: out.push_back(dword(user,gamebar,"AllowAutoGameMode",1));
            out.push_back(dword(user,gamebar,"AutoGameModeEnabled",1)); break;
    case 6: out.push_back(dword(machine,profile,"NetworkThrottlingIndex",0xffffffff)); break;
    case 7: out.push_back(dword(machine,profile,"SystemResponsiveness",10)); break;
    case 8: out.push_back(dword(machine,games,"Priority",6)); break;
    case 9: out.push_back(string_value(machine,games,"Scheduling Category","Medium")); break;
    case 10: out.push_back(dword(machine,"SYSTEM\\CurrentControlSet\\Control\\PriorityControl",
                                 "Win32PrioritySeparation",0x26)); break;
    case 12: if (game_path.empty() ||
                 !std::filesystem::path(game_path).is_absolute()) return false;
             { const DWORD attributes=GetFileAttributesA(game_executable(game_path).c_str());
               if (attributes==INVALID_FILE_ATTRIBUTES ||
                   (attributes & FILE_ATTRIBUTE_DIRECTORY)) return false; }
             out.push_back(string_value(user,"Software\\Microsoft\\DirectX\\UserGpuPreferences",
                                        game_executable(game_path),"GpuPreference=2;")); break;
    case 15: out.push_back(dword(user,gamebar,"UseNexusForGameBarEnabled",0)); break;
    case 16: out.push_back(dword(user,gamedvr,"VKMToggleRecording",0)); break;
    case 17: out.push_back(dword(user,gamedvr,"VKMSaveHistoricalVideo",0)); break;
    case 18: out.push_back(dword(user,gamedvr,"VKMToggleGameBar",0)); break;
    case 23: case 24: case 25: case 37: case 38: case 39: {
        GUID* active=nullptr;
        if (PowerGetActiveScheme(nullptr,&active)!=ERROR_SUCCESS || !active) return false;
        const std::string scheme=guid_string(*active); LocalFree(active);
        if (scheme.empty()) return false;
        if (index==23) out.push_back(power(0,0,scheme));
        if (index==24) out.push_back(power(1,0,scheme));
        if (index==25) out.push_back(power(2,1,scheme));
        if (index==37) {out.push_back(power(3,100,scheme)); out.push_back(power(4,100,scheme));}
        if (index==38) out.push_back(power(5,0,scheme));
        if (index==39) out.push_back(power(6,0,scheme));
        break;
    }
    case 28: {
        std::string name; DEVMODEA current{};
        if (!primary_display(name,current)) return false;
        DEVMODEA best=current;
        for (DWORD n=0;;++n) {
            DEVMODEA candidate{}; candidate.dmSize=sizeof(candidate);
            if (!EnumDisplaySettingsA(name.c_str(),n,&candidate)) break;
            if (candidate.dmPelsWidth==current.dmPelsWidth &&
                candidate.dmPelsHeight==current.dmPelsHeight &&
                candidate.dmBitsPerPel==current.dmBitsPerPel &&
                candidate.dmDisplayOrientation==current.dmDisplayOrientation &&
                candidate.dmDisplayFixedOutput==current.dmDisplayFixedOutput &&
                candidate.dmDisplayFrequency>best.dmDisplayFrequency) best=candidate;
        }
        out.push_back({{"kind","display"},{"device",name},
                       {"desired",display_mode(best)}}); break;
    }
    case 40: out.push_back({{"kind","accessibility"},{"desired",json::object()}}); break;
    default: return false;
    }
    return !out.empty();
}

bool SavedTweaks::read_target(const json& target, json& value) const {
    try {
        const std::string kind=target.at("kind").get<std::string>();
        if (kind=="registry") {
            HKEY root=target.at("root")=="HKLM" ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
            HKEY key=nullptr;
            LONG result=RegOpenKeyExA(root,target.at("path").get<std::string>().c_str(),
                                      0,KEY_QUERY_VALUE,&key);
            if (result==ERROR_FILE_NOT_FOUND) { value={{"exists",false}}; return true; }
            if (result!=ERROR_SUCCESS) return false;
            DWORD type=0,size=0;
            const std::string name=target.at("name").get<std::string>();
            result=RegQueryValueExA(key,name.c_str(),nullptr,&type,nullptr,&size);
            if (result==ERROR_FILE_NOT_FOUND) {
                RegCloseKey(key); value={{"exists",false}}; return true;
            }
            if (result!=ERROR_SUCCESS) {RegCloseKey(key); return false;}
            std::vector<unsigned char> bytes(size);
            result=RegQueryValueExA(key,name.c_str(),nullptr,&type,bytes.data(),&size);
            RegCloseKey(key);
            if (result!=ERROR_SUCCESS) return false;
            bytes.resize(size);
            value={{"exists",true},{"type",type},{"data",bytes}};
            return true;
        }
        if (kind=="power") {
            GUID scheme{};
            if (!parse_guid(target.at("scheme").get<std::string>(),scheme)) return false;
            const unsigned n=target.at("setting").get<unsigned>();
            if (n>=std::size(settings)) return false;
            DWORD number=0;
            if (PowerReadACValueIndex(nullptr,&scheme,&group(n),&settings[n],&number)!=ERROR_SUCCESS)
                return false;
            value={{"value",number}}; return true;
        }
        if (kind=="display") {
            DEVMODEA current{}; current.dmSize=sizeof(current);
            if (!EnumDisplaySettingsA(target.at("device").get<std::string>().c_str(),
                                      ENUM_CURRENT_SETTINGS,&current)) return false;
            value=display_mode(current); return true;
        }
        if (kind=="accessibility") {
            FILTERKEYS filter{}; filter.cbSize=sizeof(filter);
            STICKYKEYS sticky{}; sticky.cbSize=sizeof(sticky);
            TOGGLEKEYS toggle{}; toggle.cbSize=sizeof(toggle);
            if (!SystemParametersInfoW(SPI_GETFILTERKEYS,sizeof(filter),&filter,0) ||
                !SystemParametersInfoW(SPI_GETSTICKYKEYS,sizeof(sticky),&sticky,0) ||
                !SystemParametersInfoW(SPI_GETTOGGLEKEYS,sizeof(toggle),&toggle,0)) return false;
            value={{"filter_flags",filter.dwFlags},{"filter_wait",filter.iWaitMSec},
                   {"filter_delay",filter.iDelayMSec},{"filter_repeat",filter.iRepeatMSec},
                   {"filter_bounce",filter.iBounceMSec},{"sticky_flags",sticky.dwFlags},
                   {"toggle_flags",toggle.dwFlags}};
            return true;
        }
    } catch (...) { return false; }
    return false;
}

bool SavedTweaks::write_target(const json& target, const json& value,
                               bool (*confirm_display)()) const {
    try {
        const std::string kind=target.at("kind").get<std::string>();
        if (kind=="registry") {
            HKEY root=target.at("root")=="HKLM" ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
            HKEY key=nullptr;
            const std::string path=target.at("path").get<std::string>();
            const std::string name=target.at("name").get<std::string>();
            const bool exists=value.at("exists").get<bool>();
            LONG result=exists ? RegCreateKeyExA(root,path.c_str(),0,nullptr,0,KEY_SET_VALUE,
                                                  nullptr,&key,nullptr) :
                                 RegOpenKeyExA(root,path.c_str(),0,KEY_SET_VALUE,&key);
            if (!exists && result==ERROR_FILE_NOT_FOUND) return true;
            if (result!=ERROR_SUCCESS) return false;
            if (exists) {
                const auto bytes=value.at("data").get<std::vector<unsigned char>>();
                result=RegSetValueExA(key,name.c_str(),0,value.at("type").get<DWORD>(),
                                      bytes.data(),static_cast<DWORD>(bytes.size()));
            } else result=RegDeleteValueA(key,name.c_str());
            RegCloseKey(key);
            return result==ERROR_SUCCESS || (!exists && result==ERROR_FILE_NOT_FOUND);
        }
        if (kind=="power") {
            GUID scheme{};
            if (!parse_guid(target.at("scheme").get<std::string>(),scheme)) return false;
            const unsigned n=target.at("setting").get<unsigned>();
            if (n>=std::size(settings)) return false;
            if (PowerWriteACValueIndex(nullptr,&scheme,&group(n),&settings[n],
                                       value.at("value").get<DWORD>())!=ERROR_SUCCESS) return false;
            GUID* active=nullptr;
            if (PowerGetActiveScheme(nullptr,&active)==ERROR_SUCCESS && active) {
                const bool same=IsEqualGUID(*active,scheme); LocalFree(active);
                if (same && PowerSetActiveScheme(nullptr,&scheme)!=ERROR_SUCCESS) return false;
            }
            return true;
        }
        if (kind=="display") {
            const std::string device=target.at("device").get<std::string>();
            DEVMODEA desired{}; fill_display(value,desired);
            if (ChangeDisplaySettingsExA(device.c_str(),&desired,nullptr,CDS_TEST,nullptr)
                != DISP_CHANGE_SUCCESSFUL) return false;
            if (ChangeDisplaySettingsExA(device.c_str(),&desired,nullptr,0,nullptr)
                != DISP_CHANGE_SUCCESSFUL) return false;
            // Preview must be explicitly accepted before persisting in the display profile.
            if (confirm_display && !confirm_display()) return false;
            return ChangeDisplaySettingsExA(device.c_str(),&desired,nullptr,
                                             CDS_UPDATEREGISTRY,nullptr)==DISP_CHANGE_SUCCESSFUL;
        }
        if (kind=="accessibility") {
            FILTERKEYS f{}; f.cbSize=sizeof(f);
            f.dwFlags=value.at("filter_flags").get<DWORD>();
            f.iWaitMSec=value.at("filter_wait").get<DWORD>();
            f.iDelayMSec=value.at("filter_delay").get<DWORD>();
            f.iRepeatMSec=value.at("filter_repeat").get<DWORD>();
            f.iBounceMSec=value.at("filter_bounce").get<DWORD>();
            STICKYKEYS s{}; s.cbSize=sizeof(s);
            s.dwFlags=value.at("sticky_flags").get<DWORD>();
            TOGGLEKEYS t{}; t.cbSize=sizeof(t);
            t.dwFlags=value.at("toggle_flags").get<DWORD>();
            constexpr UINT flags=SPIF_UPDATEINIFILE|SPIF_SENDCHANGE;
            return SystemParametersInfoW(SPI_SETFILTERKEYS,sizeof(f),&f,flags) &&
                   SystemParametersInfoW(SPI_SETSTICKYKEYS,sizeof(s),&s,flags) &&
                   SystemParametersInfoW(SPI_SETTOGGLEKEYS,sizeof(t),&t,flags);
        }
    } catch (...) { return false; }
    return false;
}

SavedTweaks::Info SavedTweaks::info(size_t index) const {
    return index<_info.size() ? _info[index] : Info{};
}

bool SavedTweaks::scan(const std::string& game_path) {
    if (_blocked) return false;
    std::array<json,41> observed_targets{};
    std::array<json,41> observed_values{};
    std::array<bool,41> observed_known{};
    for (size_t i=0;i<_info.size();++i) {
        if (!_info[i].eligible) continue;
        json target;
        json values=json::array();
        bool known=targets(i,game_path,target);
        if (known) for (const auto& item:target) {
            json value;
            if (!read_target(item,value)) {known=false; break;}
            values.push_back(value);
        }
        if (known && i==40) {
            json desired=values[0];
            DWORD flags=desired.at("filter_flags").get<DWORD>();
            if (!(flags & FKF_FILTERKEYSON)) flags &= ~(FKF_HOTKEYACTIVE|FKF_CONFIRMHOTKEY);
            desired["filter_flags"]=flags;
            flags=desired.at("sticky_flags").get<DWORD>();
            if (!(flags & SKF_STICKYKEYSON)) flags &= ~(SKF_HOTKEYACTIVE|SKF_CONFIRMHOTKEY);
            desired["sticky_flags"]=flags;
            flags=desired.at("toggle_flags").get<DWORD>();
            if (!(flags & TKF_TOGGLEKEYSON)) flags &= ~(TKF_HOTKEYACTIVE|TKF_CONFIRMHOTKEY);
            desired["toggle_flags"]=flags;
            target[0]["desired"]=desired;
        }
        observed_targets[i]=target;
        observed_values[i]=values;
        observed_known[i]=known;
        if (!_scan.contains(ids[i]))
            _scan[ids[i]]["first"]=known ? values : json(nullptr);
        if (known && (!_scan[ids[i]].contains("first_known") ||
                      _scan[ids[i]]["first_known"].is_null()))
            _scan[ids[i]]["first_known"]=values;
        _scan[ids[i]]["last"]=known ? values : json(nullptr);
        _info[i].unknown=!known;
        const json& reference=_scan[ids[i]].contains("first_known") ?
            _scan[ids[i]]["first_known"] : _scan[ids[i]]["first"];
        _info[i].base=known && reference.is_array() &&
                      reference.size()==target.size();
        if (_info[i].base) for (size_t n=0;n<target.size();++n)
            if (!equal_value(target[n],reference[n],target[n]["desired"]))
                _info[i].base=false;
    }
    if (!write(_scan_path,_scan)) {
        _blocked=true; _error="Could not save baseline scan"; return false;
    }
    if (!import_base_settings(observed_targets,observed_values,observed_known))
        return false;
    refresh_saved(game_path);
    return true;
}

bool SavedTweaks::import_base_settings(
    const std::array<json,41>& target_lists,
    const std::array<json,41>& values,
    const std::array<bool,41>& known)
{
    json updated=_saved;
    bool changed=false;
    for (size_t i=0;i<_info.size();++i) {
        if (!_info[i].eligible || !known[i] || !_info[i].base ||
            updated.contains(ids[i]) || _dismissed.value(ids[i],false)) continue;
        if (i==40) {
            const auto& state=values[i][0];
            if ((state.at("filter_flags").get<DWORD>() & FKF_FILTERKEYSON) ||
                (state.at("sticky_flags").get<DWORD>() & SKF_STICKYKEYSON) ||
                (state.at("toggle_flags").get<DWORD>() & TKF_TOGGLEKEYSON))
                continue; // A deliberately active accessibility feature is not an optimization.
        }
        const json& targets=target_lists[i];
        bool still_optimal=targets.is_array() && targets.size()==values[i].size();
        for (size_t n=0;still_optimal && n<targets.size();++n)
            still_optimal=equal_value(targets[n],values[i][n],targets[n]["desired"]);
        if (!still_optimal) continue;
        json entries=json::array();
        for (size_t n=0;n<targets.size();++n) {
            json item=targets[n];
            item["before"]=values[i][n];
            entries.push_back(item);
        }
        updated[ids[i]]={{"targets",entries},{"source","base_scan"}};
        changed=true;
    }
    if (!changed) return true;
    if (!write(_saved_path,updated)) {
        _blocked=true; _error="Could not save detected base settings";
        return false;
    }
    _saved=std::move(updated);
    return true;
}

void SavedTweaks::refresh_saved(const std::string&) {
    for (size_t i=0;i<_info.size();++i) {
        _info[i].saved=_saved.contains(ids[i]);
        _info[i].drift=false;
        if (!_info[i].saved) continue;
        try {
            const json& entries=_saved.at(ids[i]).at("targets");
            for (const auto& item:entries) {
                json actual;
                if (!read_target(item,actual) ||
                    !equal_value(item,actual,item.at("desired"))) {
                    _info[i].drift=true; break;
                }
            }
        } catch (...) { _info[i].drift=true; }
    }
}

bool SavedTweaks::rollback(const json& operation) {
    try {
        const auto& entries=operation.at("targets");
        for (size_t n=entries.size();n>0;--n) {
            const auto& item=entries[n-1];
            json current;
            if (!read_target(item,current)) return false;
            if (equal_value(item,current,item.at("before"))) continue;
            if (!equal_value(item,current,item.at("desired")) &&
                !safe_accessibility_partial(item,current)) return false;
            if (!write_target(item,item.at("before"),nullptr)) return false;
            json verified;
            if (!read_target(item,verified) ||
                !equal_value(item,verified,item.at("before"))) return false;
        }
        return true;
    } catch (...) { return false; }
}

bool SavedTweaks::recover() {
    if (_blocked && _error!="Saved operation conflicts with an external change" &&
        _error!="Saved operation recovery incomplete") return false;
    _blocked=false;
    if (!std::filesystem::exists(_operation_path)) return true;
    json operation;
    if (!load_json(_operation_path,operation)) {
        _blocked=true; _error="Saved transaction log is invalid"; return false;
    }
    try {
        bool complete=true;
        for (const auto& item:operation.at("targets")) {
            json current;
            if (!read_target(item,current) ||
                !equal_value(item,current,item.at("desired"))) {complete=false;break;}
        }
        // A display preview may be visible but not yet accepted. Other fully
        // applied transactions can be committed after a crash.
        const bool display_preview=operation.at("id")=="display_max_refresh" &&
            !operation.value("ready_to_commit",false);
        if (complete && !display_preview) {
            const std::string id=operation.at("id").get<std::string>();
            _saved[id]={{"targets",operation.at("targets")}};
            if (!write(_saved_path,_saved)) throw std::runtime_error("saved file");
        } else if (!rollback(operation)) {
            _blocked=true; _error="Saved operation conflicts with an external change";
            return false;
        }
        if (!DeleteFileA(_operation_path.c_str())) throw std::runtime_error("journal delete");
        _error.clear();
        return true;
    } catch (...) {
        _blocked=true; _error="Saved operation recovery incomplete"; return false;
    }
}

bool SavedTweaks::perform(size_t index, const std::string& game_path,
                          bool confirmed, bool (*confirm_display)()) {
    if (_blocked || index>=41 || !_info[index].eligible) return false;
    if ((index==28 || index==37 || index==39 || index==40) && !confirmed) {
        _error="Confirmation required"; return false;
    }
    json entries;
    if (!targets(index,game_path,entries)) { _error="Target unavailable"; return false; }
    if (index==28 && !confirm_display) { _error="Display confirmation unavailable"; return false; }
    for (auto& item:entries) {
        json before;
        if (!read_target(item,before)) { _error="Cannot read current setting"; return false; }
        item["before"]=before;
        if (index==40) {
            json desired=before;
            DWORD flags=desired.at("filter_flags").get<DWORD>();
            if (!(flags & FKF_FILTERKEYSON)) flags &= ~(FKF_HOTKEYACTIVE|FKF_CONFIRMHOTKEY);
            desired["filter_flags"]=flags;
            flags=desired.at("sticky_flags").get<DWORD>();
            if (!(flags & SKF_STICKYKEYSON)) flags &= ~(SKF_HOTKEYACTIVE|SKF_CONFIRMHOTKEY);
            desired["sticky_flags"]=flags;
            flags=desired.at("toggle_flags").get<DWORD>();
            if (!(flags & TKF_TOGGLEKEYSON)) flags &= ~(TKF_HOTKEYACTIVE|TKF_CONFIRMHOTKEY);
            desired["toggle_flags"]=flags;
            item["desired"]=desired;
        }
        if (index==25 && before.at("value").get<DWORD>()!=0 &&
            !equal_value(item,before,item.at("desired"))) {
            _error="CPU boost has a custom mode"; return false;
        }
    }
    json operation={{"id",ids[index]},{"targets",entries},
                    {"ready_to_commit",false}};
    if (!write(_operation_path,operation)) { _error="Cannot create saved transaction"; return false; }
    for (const auto& item:entries) {
        if (equal_value(item,item.at("before"),item.at("desired"))) continue;
        if (!write_target(item,item.at("desired"),confirm_display)) break;
        json verified;
        if (!read_target(item,verified) || !equal_value(item,verified,item.at("desired"))) break;
    }
    bool complete=true;
    for (const auto& item:entries) {
        json current;
        if (!read_target(item,current) || !equal_value(item,current,item.at("desired"))) {
            complete=false; break;
        }
    }
    if (!complete) {
        if (rollback(operation)) DeleteFileA(_operation_path.c_str());
        else { _blocked=true; _error="Saved restore incomplete; transaction retained"; }
        if (!_blocked) _error="Could not apply and verify saved setting";
        return false;
    }
    operation["ready_to_commit"]=true;
    if (!write(_operation_path,operation)) {
        _blocked=true; _error="Could not finalize saved transaction"; return false;
    }
    json updated=_saved;
    updated[ids[index]]={{"targets",entries}};
    if (!write(_saved_path,updated)) {
        _blocked=true; _error="Saved setting applied but commit failed"; return false;
    }
    _saved=updated;
    if (!DeleteFileA(_operation_path.c_str())) {
        _blocked=true; _error="Saved transaction cleanup failed"; return false;
    }
    refresh_saved(game_path);
    return true;
}

bool SavedTweaks::apply(size_t index, const std::string& game_path,
                        bool confirmed, bool (*confirm_display)()) {
    if (index>=41 || _saved.contains(ids[index])) return false;
    return perform(index,game_path,confirmed,confirm_display);
}
bool SavedTweaks::reapply(size_t index, const std::string& game_path,
                          bool confirmed, bool (*confirm_display)()) {
    if (index>=41 || !_saved.contains(ids[index])) return false;
    // Preserve the original target identity (game path, power scheme, display).
    try {
        json entries=_saved.at(ids[index]).at("targets");
        for (auto& item:entries) {
            json before;
            if (!read_target(item,before)) return false;
            item["before"]=before;
        }
        if ((index==28 || index==37 || index==39 || index==40) && !confirmed) return false;
        if (index==28 && !confirm_display) return false;
        json operation={{"id",ids[index]},{"targets",entries},
                        {"ready_to_commit",false}};
        if (!write(_operation_path,operation)) return false;
        for (const auto& item:entries) {
            if (!equal_value(item,item.at("before"),item.at("desired")) &&
                !write_target(item,item.at("desired"),confirm_display)) break;
        }
        bool complete=true;
        for (const auto& item:entries) {
            json current;
            if (!read_target(item,current) || !equal_value(item,current,item.at("desired")))
                complete=false;
        }
        if (!complete) {
            if (rollback(operation)) DeleteFileA(_operation_path.c_str());
            else { _blocked=true; _error="Saved restore incomplete"; }
            return false;
        }
        operation["ready_to_commit"]=true;
        if (!write(_operation_path,operation)) { _blocked=true; return false; }
        json updated=_saved;
        updated[ids[index]]={{"targets",entries}};
        if (!write(_saved_path,updated)) { _blocked=true; return false; }
        _saved=updated;
        if (!DeleteFileA(_operation_path.c_str())) { _blocked=true; return false; }
        refresh_saved(game_path);
        return true;
    } catch (...) { return false; }
}
bool SavedTweaks::remove(size_t index) {
    if (_blocked || index>=41 || !_saved.contains(ids[index])) return false;
    json excluded=_dismissed;
    excluded[ids[index]]=true;
    if (!write(_dismissed_path,excluded)) return false;
    _dismissed=std::move(excluded);
    json updated=_saved;
    updated.erase(ids[index]);
    if (!write(_saved_path,updated)) return false;
    _saved=updated;
    _info[index].saved=false;
    _info[index].drift=false;
    return true;
}
