#include <winsock2.h>
#include "saved_tweaks.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <chrono>
#include <windows.h>
#include <powrprof.h>
#include <objbase.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <cfgmgr32.h>

namespace {
using json = nlohmann::json;
const char* tweak_id(size_t index) { return TWEAK_CATALOG[index].id; }
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
    {0x48e6b7a6,0x50f5,0x4782,{0xa5,0xd4,0x53,0xbb,0x8f,0x07,0xe2,0x26}},
    {0x893dee8e,0x2bef,0x41e0,{0x89,0xc6,0xb5,0x5d,0x09,0x29,0x96,0x4c}}
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

struct SavedAdapter {
    GUID id{};
    DWORD index=0;
    ULONG type=0;
    std::string path;
    std::string instance;
};

bool registry_text(const std::string& path,const char* name,std::string& value) {
    HKEY key=nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,path.c_str(),0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS)
        return false;
    DWORD type=0,size=0;
    LONG result=RegQueryValueExA(key,name,nullptr,&type,nullptr,&size);
    if (result!=ERROR_SUCCESS || (type!=REG_SZ && type!=REG_EXPAND_SZ)) {
        RegCloseKey(key); return false;
    }
    std::vector<char> data(size+1,0);
    result=RegQueryValueExA(key,name,nullptr,&type,
        reinterpret_cast<BYTE*>(data.data()),&size);
    RegCloseKey(key);
    if (result!=ERROR_SUCCESS) return false;
    value=data.data(); return true;
}

bool saved_active_adapter(SavedAdapter& out) {
    if (GetBestInterface(inet_addr("1.1.1.1"),&out.index)!=NO_ERROR) return false;
    ULONG bytes=0;
    GetAdaptersAddresses(AF_UNSPEC,0,nullptr,nullptr,&bytes);
    std::vector<unsigned char> storage(bytes);
    auto* first=reinterpret_cast<PIP_ADAPTER_ADDRESSES>(storage.data());
    if (!bytes || GetAdaptersAddresses(AF_UNSPEC,0,nullptr,first,&bytes)!=NO_ERROR)
        return false;
    std::string adapter_name;
    for (auto* item=first;item;item=item->Next)
        if (item->IfIndex==out.index || item->Ipv6IfIndex==out.index) {
            adapter_name=item->AdapterName ? item->AdapterName : "";
            out.type=item->IfType; break;
        }
    if (adapter_name.empty()) return false;
    std::wstring wide(adapter_name.begin(),adapter_name.end());
    if (wide.empty() || wide.front()!=L'{') wide=L"{"+wide+L"}";
    if (CLSIDFromString(wide.c_str(),&out.id)!=S_OK) return false;
    const std::string base=
        "SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e972-e325-11ce-bfc1-08002be10318}";
    HKEY root=nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,base.c_str(),0,KEY_ENUMERATE_SUB_KEYS,&root)
        !=ERROR_SUCCESS) return false;
    for (DWORD number=0;;++number) {
        char name[256]{}; DWORD length=sizeof(name);
        if (RegEnumKeyExA(root,number,name,&length,nullptr,nullptr,nullptr,nullptr)
            !=ERROR_SUCCESS) break;
        const std::string path=base+"\\"+name;
        std::string id;
        if (registry_text(path,"NetCfgInstanceId",id) &&
            _stricmp(id.c_str(),adapter_name.c_str())==0) {
            out.path=path; registry_text(path,"PnPInstanceID",out.instance); break;
        }
    }
    RegCloseKey(root); return !out.path.empty();
}

std::string lower_text(std::string value) {
    std::transform(value.begin(),value.end(),value.begin(),
        [](unsigned char c){return static_cast<char>(std::tolower(c));});
    return value;
}

bool saved_driver_value(const SavedAdapter& adapter,const char* keyword,
                        const std::vector<std::string>& wanted,json& target) {
    const std::string enum_path=adapter.path+"\\Ndi\\Params\\"+keyword+"\\enum";
    HKEY values=nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,enum_path.c_str(),0,KEY_QUERY_VALUE,&values)
        !=ERROR_SUCCESS) return false;
    std::string selected;
    for (DWORD index=0;selected.empty();++index) {
        char name[128]{}; DWORD name_size=sizeof(name);
        BYTE data[512]{}; DWORD data_size=sizeof(data),type=0;
        if (RegEnumValueA(values,index,name,&name_size,nullptr,&type,data,&data_size)
            !=ERROR_SUCCESS) break;
        if (type!=REG_SZ) continue;
        const std::string display=lower_text(reinterpret_cast<char*>(data));
        for (const auto& token:wanted)
            if (display.find(lower_text(token))!=std::string::npos) {
                selected.assign(name,name_size); break;
            }
    }
    RegCloseKey(values);
    if (selected.empty()) return false;
    HKEY key=nullptr; DWORD type=0,size=0;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,adapter.path.c_str(),0,KEY_QUERY_VALUE,&key)
        !=ERROR_SUCCESS) return false;
    const LONG read=RegQueryValueExA(key,keyword,nullptr,&type,nullptr,&size);
    RegCloseKey(key);
    if (read!=ERROR_SUCCESS) return false;
    target=type==REG_DWORD ? dword("HKLM",adapter.path.c_str(),keyword,
        std::strtoul(selected.c_str(),nullptr,0)) :
        string_value("HKLM",adapter.path.c_str(),keyword,selected);
    target["network_device"]=adapter.instance;
    return true;
}

bool restart_saved_adapter(const std::string& instance) {
    if (instance.empty()) return false;
    std::vector<char> id(instance.begin(),instance.end()); id.push_back('\0');
    DEVINST device=0;
    if (CM_Locate_DevNodeA(&device,id.data(),CM_LOCATE_DEVNODE_NORMAL)!=CR_SUCCESS ||
        CM_Disable_DevNode(device,0)!=CR_SUCCESS) return false;
    Sleep(300);
    return CM_Enable_DevNode(device,0)==CR_SUCCESS;
}

struct SavedDnsSettings {
    ULONG Version; ULONG64 Flags; PWSTR Domain; PWSTR NameServer;
    PWSTR SearchList; ULONG RegistrationEnabled; ULONG RegisterAdapterName;
    ULONG EnableLLMNR; ULONG QueryAdapterName; PWSTR ProfileNameServer;
};
using GetDnsFn=DWORD (WINAPI*)(GUID,SavedDnsSettings*);
using SetDnsFn=DWORD (WINAPI*)(GUID,const SavedDnsSettings*);
using FreeDnsFn=VOID (WINAPI*)(SavedDnsSettings*);
template<class Function> Function dns_function(const char* name) {
    HMODULE module=GetModuleHandleA("iphlpapi.dll");
    FARPROC address=module ? GetProcAddress(module,name) : nullptr;
    Function function=nullptr;
    static_assert(sizeof(function)==sizeof(address));
    std::memcpy(&function,&address,sizeof(function)); return function;
}
bool get_dns(const GUID& id,std::wstring& servers) {
    auto get=dns_function<GetDnsFn>("GetInterfaceDnsSettings");
    auto release=dns_function<FreeDnsFn>("FreeInterfaceDnsSettings");
    if (!get || !release) return false;
    SavedDnsSettings settings{}; settings.Version=1;
    if (get(id,&settings)!=NO_ERROR) return false;
    servers=settings.NameServer ? settings.NameServer : L"";
    release(&settings); return true;
}
bool set_dns(const GUID& id,const std::wstring& servers) {
    auto set=dns_function<SetDnsFn>("SetInterfaceDnsSettings");
    if (!set) return false;
    SavedDnsSettings settings{}; settings.Version=1; settings.Flags=0x0002;
    settings.NameServer=servers.empty() ? nullptr : const_cast<PWSTR>(servers.c_str());
    return set(id,&settings)==NO_ERROR;
}
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size=WideCharToMultiByte(CP_UTF8,0,value.c_str(),
        static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    std::string result(size,'\0');
    WideCharToMultiByte(CP_UTF8,0,value.c_str(),static_cast<int>(value.size()),
        result.data(),size,nullptr,nullptr); return result;
}
std::wstring wide_utf8(const std::string& value) {
    if (value.empty()) return {};
    const int size=MultiByteToWideChar(CP_UTF8,0,value.c_str(),
        static_cast<int>(value.size()),nullptr,0);
    std::wstring result(size,L'\0');
    MultiByteToWideChar(CP_UTF8,0,value.c_str(),static_cast<int>(value.size()),
        result.data(),size); return result;
}
int saved_dns_latency(const char* server) {
    sockaddr_in target{}; target.sin_family=AF_INET; target.sin_port=htons(53);
    if (inet_pton(AF_INET,server,&target.sin_addr)!=1) return -1;
    std::vector<int> samples;
    for (unsigned attempt=0;attempt<3;++attempt) {
        SOCKET handle=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        if (handle==INVALID_SOCKET) break;
        DWORD timeout=500;
        setsockopt(handle,SOL_SOCKET,SO_RCVTIMEO,
            reinterpret_cast<const char*>(&timeout),sizeof(timeout));
        unsigned char query[64]{}; const USHORT transaction=
            static_cast<USHORT>((GetTickCount()+attempt*3571)&0xffff);
        query[0]=static_cast<unsigned char>(transaction>>8);
        query[1]=static_cast<unsigned char>(transaction); query[2]=1; query[5]=1;
        size_t position=12;
        for (const char* label:{"www","epicgames","com"}) {
            const size_t length=std::strlen(label); query[position++]=length;
            std::memcpy(query+position,label,length); position+=length;
        }
        query[position++]=0; query[position++]=0; query[position++]=1;
        query[position++]=0; query[position++]=1;
        const auto start=std::chrono::steady_clock::now();
        const int sent=sendto(handle,reinterpret_cast<const char*>(query),position,0,
            reinterpret_cast<const sockaddr*>(&target),sizeof(target));
        unsigned char response[512]{}; int from_size=sizeof(target);
        const int received=sent>0 ? recvfrom(handle,reinterpret_cast<char*>(response),
            sizeof(response),0,reinterpret_cast<sockaddr*>(&target),&from_size) : -1;
        const auto stop=std::chrono::steady_clock::now(); closesocket(handle);
        if (received>=12 && response[0]==query[0] && response[1]==query[1])
            samples.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(
                stop-start).count());
    }
    if (samples.size()<2) return -1;
    std::sort(samples.begin(),samples.end()); return samples[samples.size()/2];
}
std::string saved_effective_dns(DWORD interface_index) {
    ULONG bytes=0; GetAdaptersAddresses(AF_UNSPEC,0,nullptr,nullptr,&bytes);
    std::vector<unsigned char> storage(bytes);
    auto* first=reinterpret_cast<PIP_ADAPTER_ADDRESSES>(storage.data());
    if (!bytes || GetAdaptersAddresses(AF_UNSPEC,0,nullptr,first,&bytes)!=NO_ERROR)
        return {};
    for (auto* item=first;item;item=item->Next) {
        if (item->IfIndex!=interface_index && item->Ipv6IfIndex!=interface_index) continue;
        for (auto* dns=item->FirstDnsServerAddress;dns;dns=dns->Next) {
            if (!dns->Address.lpSockaddr || dns->Address.lpSockaddr->sa_family!=AF_INET)
                continue;
            char host[NI_MAXHOST]{};
            if (getnameinfo(dns->Address.lpSockaddr,dns->Address.iSockaddrLength,
                host,sizeof(host),nullptr,0,NI_NUMERICHOST)==0) return host;
        }
    }
    return {};
}
} // namespace

SavedTweaks::SavedTweaks(const std::string& directory)
    : _scan_path(directory+"\\baseline.json"),
      _saved_path(directory+"\\saved.json"),
      _dismissed_path(directory+"\\saved-dismissed.json"),
      _operation_path(directory+"\\saved-operation.json") {
    load();
    // One-time cleanup for the short-lived build that exposed mouse
    // acceleration as a Saved tweak. Restore only when the current value is
    // still exactly the value Nebula wrote; never overwrite a third-party edit.
    if (!_blocked && _saved.contains("mouse_acceleration_off")) {
        bool safe=true;
        try {
            for (const auto& target:_saved.at("mouse_acceleration_off").at("targets")) {
                json current;
                if (!read_target(target,current)) {safe=false; break;}
                if (equal_value(target,current,target.at("before"))) continue;
                if (!equal_value(target,current,target.at("desired")) ||
                    !write_target(target,target.at("before"),nullptr)) {
                    safe=false; break;
                }
            }
        } catch (...) {safe=false;}
        if (safe) {
            _saved.erase("mouse_acceleration_off");
            _scan.erase("mouse_acceleration_off");
            _dismissed.erase("mouse_acceleration_off");
            safe=write(_saved_path,_saved) && write(_scan_path,_scan) &&
                 write(_dismissed_path,_dismissed);
        }
        if (!safe) {
            _blocked=true;
            _error="Legacy mouse restore conflicts with an external change";
        }
    }
    for (size_t i=0;i<_info.size();++i) {
        _info[i].eligible = TWEAK_CATALOG[i].saved_eligible;
        _info[i].saved = _saved.contains(tweak_id(i));
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
    if (index>=TWEAK_COUNT || !_info[index].eligible) return false;
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
    case 23: case 24: case 25: case 37: case 38: case 39: case 44: {
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
        if (index==44) out.push_back(power(7,100,scheme));
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
    case 40: case 41:
        out.push_back({{"kind","accessibility"},{"desired",json::object()}}); break;
    case 42: out.push_back({{"kind","keyboard"},{"setting","delay"},
                            {"desired",{{"value",0}}}}); break;
    case 43: out.push_back({{"kind","keyboard"},{"setting","speed"},
                            {"desired",{{"value",31}}}}); break;
    case 45: {
        SavedAdapter adapter;
        if (!saved_active_adapter(adapter)) return false;
        std::wstring original;
        if (!get_dns(adapter.id,original)) return false;
        WSADATA winsock{};
        if (WSAStartup(MAKEWORD(2,2),&winsock)!=0) return false;
        const std::string current=saved_effective_dns(adapter.index);
        int best=current.empty() ? -1 : saved_dns_latency(current.c_str());
        std::wstring desired=original;
        for (const auto& candidate:std::vector<std::pair<const char*,const wchar_t*>>{
                {"1.1.1.1",L"1.1.1.1,1.0.0.1"},
                {"8.8.8.8",L"8.8.8.8,8.8.4.4"},
                {"9.9.9.9",L"9.9.9.9,149.112.112.112"}}) {
            const int latency=saved_dns_latency(candidate.first);
            if (latency>=0 && (best<0 || latency<best)) {
                best=latency; desired=candidate.second;
            }
        }
        WSACleanup();
        if (best<0) return false;
        out.push_back({{"kind","dns"},{"interface",guid_string(adapter.id)},
                       {"desired",{{"servers",utf8(desired)}}}}); break;
    }
    case 46: case 47: case 48: case 49: case 50:
    case 51: case 52: case 53: {
        SavedAdapter adapter;
        if (!saved_active_adapter(adapter)) return false;
        if (index>=46 && index<=50 && adapter.type!=IF_TYPE_ETHERNET_CSMACD)
            return false;
        if ((index==52 || index==53) && adapter.type!=IF_TYPE_IEEE80211)
            return false;
        struct Choice { const char* key; std::vector<std::string> values; };
        std::vector<Choice> choices;
        if (index==46) choices={{"*RSS",{"enabled","on"}}};
        if (index==47) choices={{"*InterruptModeration",{"disabled","off"}},
                                {"InterruptModeration",{"disabled","off"}}};
        if (index==48) choices={{"*RscIPv4",{"disabled","off"}},
                                {"*RscIPv6",{"disabled","off"}},
                                {"RSC",{"disabled","off"}}};
        if (index==49) choices={{"*EEE",{"disabled","off"}},
                                {"EEELinkAdvertisement",{"disabled","off"}},
                                {"AdvancedEEE",{"disabled","off"}},
                                {"EnableGreenEthernet",{"disabled","off"}}};
        if (index==50) choices={{"*FlowControl",{"disabled","off"}},
                                {"FlowControl",{"disabled","off"}}};
        if (index==51) choices={{"PowerSavingMode",{"disabled","maximum performance","off"}},
                                {"PowerSaveMode",{"disabled","maximum performance","off"}}};
        if (index==52) choices={{"MIMOPowerSaveMode",{"no smps"}},
                                {"MimoPowerSaveMode",{"no smps"}}};
        if (index==53) choices={{"TransmitPower",{"highest","maximum"}},
                                {"*TransmitPower",{"highest","maximum"}}};
        json target;
        bool found=false;
        for (const Choice& choice:choices)
            if (saved_driver_value(adapter,choice.key,choice.values,target)) {
                found=true; break;
            }
        if (!found) return false;
        out.push_back(target); break;
    }
    default: return false;
    }
    return !out.empty();
}

bool SavedTweaks::read_target(const json& target, json& value) const {
    try {
        const std::string kind=target.at("kind").get<std::string>();
        if (kind=="dns") {
            GUID id{};
            if (!parse_guid(target.at("interface").get<std::string>(),id)) return false;
            std::wstring servers;
            if (!get_dns(id,servers)) return false;
            value={{"servers",utf8(servers)}}; return true;
        }
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
        if (kind=="keyboard") {
            UINT setting=0;
            const std::string name=target.at("setting").get<std::string>();
            const UINT action=name=="delay" ? SPI_GETKEYBOARDDELAY :
                              name=="speed" ? SPI_GETKEYBOARDSPEED : 0;
            if (!action || !SystemParametersInfoW(action,0,&setting,0)) return false;
            value={{"value",setting}}; return true;
        }
        if (kind=="mouse_acceleration") {
            int mouse[3]{};
            if (!SystemParametersInfoW(SPI_GETMOUSE,0,mouse,0)) return false;
            value={{"threshold1",mouse[0]},{"threshold2",mouse[1]},
                   {"acceleration",mouse[2]}};
            return true;
        }
    } catch (...) { return false; }
    return false;
}

bool SavedTweaks::write_target(const json& target, const json& value,
                               bool (*confirm_display)()) const {
    try {
        const std::string kind=target.at("kind").get<std::string>();
        if (kind=="dns") {
            GUID id{};
            if (!parse_guid(target.at("interface").get<std::string>(),id)) return false;
            return set_dns(id,wide_utf8(value.at("servers").get<std::string>()));
        }
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
            const bool written=result==ERROR_SUCCESS ||
                (!exists && result==ERROR_FILE_NOT_FOUND);
            if (!written) return false;
            if (target.contains("network_device"))
                return restart_saved_adapter(
                    target.at("network_device").get<std::string>());
            return true;
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
        if (kind=="keyboard") {
            const std::string name=target.at("setting").get<std::string>();
            const UINT action=name=="delay" ? SPI_SETKEYBOARDDELAY :
                              name=="speed" ? SPI_SETKEYBOARDSPEED : 0;
            return action && SystemParametersInfoW(action,
                value.at("value").get<UINT>(),nullptr,
                SPIF_UPDATEINIFILE|SPIF_SENDCHANGE);
        }
        if (kind=="mouse_acceleration") {
            int mouse[3] = {value.at("threshold1").get<int>(),
                            value.at("threshold2").get<int>(),
                            value.at("acceleration").get<int>()};
            return SystemParametersInfoW(SPI_SETMOUSE,0,mouse,
                                         SPIF_UPDATEINIFILE|SPIF_SENDCHANGE);
        }
    } catch (...) { return false; }
    return false;
}

SavedTweaks::Info SavedTweaks::info(size_t index) const {
    return index<_info.size() ? _info[index] : Info{};
}

bool SavedTweaks::scan(const std::string& game_path) {
    if (_blocked) return false;
    std::array<json,TWEAK_COUNT> observed_targets{};
    std::array<json,TWEAK_COUNT> observed_values{};
    std::array<bool,TWEAK_COUNT> observed_known{};
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
        if (known && i==41) {
            json desired=values[0];
            desired["filter_flags"]=(desired.at("filter_flags").get<DWORD>() |
                FKF_FILTERKEYSON) & ~(FKF_HOTKEYACTIVE|FKF_CONFIRMHOTKEY);
            desired["filter_wait"]=1;
            desired["filter_delay"]=100;
            desired["filter_repeat"]=20;
            desired["filter_bounce"]=0;
            target[0]["desired"]=desired;
        }
        observed_targets[i]=target;
        observed_values[i]=values;
        observed_known[i]=known;
        if (!_scan.contains(tweak_id(i)))
            _scan[tweak_id(i)]["first"]=known ? values : json(nullptr);
        // An explicit scan defines the reference to restore.  Unknown targets
        // are stored as null and are never guessed during restoration.
        _scan[tweak_id(i)]["baseline"]=known ? values : json(nullptr);
        _scan[tweak_id(i)]["baseline_targets"]=known ? target : json(nullptr);
        if (known && (!_scan[tweak_id(i)].contains("first_known") ||
                      _scan[tweak_id(i)]["first_known"].is_null()))
            _scan[tweak_id(i)]["first_known"]=values;
        _scan[tweak_id(i)]["last"]=known ? values : json(nullptr);
        _info[i].unknown=!known;
        const json& reference=values;
        _info[i].base=known && reference.is_array() &&
                      reference.size()==target.size();
        if (_info[i].base) for (size_t n=0;n<target.size();++n)
            if (!equal_value(target[n],reference[n],target[n]["desired"]))
                _info[i].base=false;
    }
    _scan["_meta"]={{"complete",true},{"game_path",game_path}};
    if (!write(_scan_path,_scan)) {
        _blocked=true; _error="Could not save baseline scan"; return false;
    }
    if (!import_base_settings(observed_targets,observed_values,observed_known))
        return false;
    refresh_saved(game_path);
    return true;
}

bool SavedTweaks::has_baseline() const
{
    try {
        return _scan.contains("_meta") &&
               _scan.at("_meta").value("complete", false);
    } catch (...) { return false; }
}

bool SavedTweaks::has_baseline_for(const std::string& game_path) const
{
    if (!has_baseline() || game_path.empty()) return has_baseline();
    try {
        const std::string scanned=_scan.at("_meta").value("game_path",std::string());
        return !scanned.empty() && _stricmp(scanned.c_str(),game_path.c_str())==0;
    } catch (...) { return false; }
}

bool SavedTweaks::restore_baseline(const std::string& game_path)
{
    if (_blocked || !has_baseline()) return !_blocked;
    bool okay=true;
    for (size_t i=0;i<TWEAK_COUNT;++i) {
        if (!_info[i].eligible || _saved.contains(tweak_id(i)) ||
            !_scan.contains(tweak_id(i))) continue;
        const json& entry=_scan.at(tweak_id(i));
        if (!entry.contains("baseline") || !entry.contains("baseline_targets") ||
            !entry.at("baseline").is_array() ||
            !entry.at("baseline_targets").is_array()) continue;
        const json& values=entry.at("baseline");
        const json& stored_targets=entry.at("baseline_targets");
        if (values.size()!=stored_targets.size()) {okay=false; continue;}
        for (size_t n=0;n<values.size();++n) {
            json current;
            if (!read_target(stored_targets[n],current)) {okay=false; continue;}
            if (equal_value(stored_targets[n],current,values[n])) continue;
            if (!write_target(stored_targets[n],values[n],nullptr)) {
                okay=false; continue;
            }
            json verified;
            if (!read_target(stored_targets[n],verified) ||
                !equal_value(stored_targets[n],verified,values[n])) okay=false;
        }
    }
    (void)game_path;
    if (!okay) _error="Baseline restore incomplete";
    return okay;
}

bool SavedTweaks::import_base_settings(
    const std::array<json,TWEAK_COUNT>& target_lists,
    const std::array<json,TWEAK_COUNT>& values,
    const std::array<bool,TWEAK_COUNT>& known)
{
    json updated=_saved;
    bool changed=false;
    for (size_t i=0;i<_info.size();++i) {
        if (!_info[i].eligible || !known[i] || !_info[i].base ||
            updated.contains(tweak_id(i)) || _dismissed.value(tweak_id(i),false)) continue;
        if (i==40 || i==41) {
            const auto& state=values[i][0];
            if ((i==40 && (state.at("filter_flags").get<DWORD>() & FKF_FILTERKEYSON)) ||
                (state.at("sticky_flags").get<DWORD>() & SKF_STICKYKEYSON) ||
                (state.at("toggle_flags").get<DWORD>() & TKF_TOGGLEKEYSON))
                continue; // A deliberately active accessibility feature is not an optimization.
        }
        if ((i==40 && updated.contains(tweak_id(41))) ||
            (i==41 && updated.contains(tweak_id(40)))) continue;
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
        updated[tweak_id(i)]={{"targets",entries},{"source","base_scan"}};
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
        _info[i].saved=_saved.contains(tweak_id(i));
        _info[i].drift=false;
        if (!_info[i].saved) continue;
        try {
            const json& entries=_saved.at(tweak_id(i)).at("targets");
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
    if (_blocked || index>=TWEAK_COUNT || !_info[index].eligible) return false;
    if ((index==28 || index==37 || index==39 || index==40 ||
         index==41 || index==44 || (index>=47 && index<=50)) && !confirmed) {
        _error="Confirmation required"; return false;
    }
    if ((index==40 && _saved.contains(tweak_id(41))) ||
        (index==41 && _saved.contains(tweak_id(40)))) {
        _error="Remove the conflicting Saved accessibility tweak first"; return false;
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
        if (index==41) {
            json desired=before;
            DWORD flags=before.at("filter_flags").get<DWORD>();
            desired["filter_flags"]=(flags|FKF_FILTERKEYSON) &
                ~(FKF_HOTKEYACTIVE|FKF_CONFIRMHOTKEY);
            desired["filter_wait"]=1;
            desired["filter_delay"]=100;
            desired["filter_repeat"]=20;
            desired["filter_bounce"]=0;
            if ((flags & FKF_FILTERKEYSON) && before!=desired) {
                _error="FilterKeys is already in use"; return false;
            }
            item["desired"]=desired;
        }
        if (index==25 && before.at("value").get<DWORD>()!=0 &&
            !equal_value(item,before,item.at("desired"))) {
            _error="CPU boost has a custom mode"; return false;
        }
    }
    json operation={{"id",tweak_id(index)},{"targets",entries},
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
    updated[tweak_id(index)]={{"targets",entries}};
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
    if (index>=TWEAK_COUNT || _saved.contains(tweak_id(index))) return false;
    return perform(index,game_path,confirmed,confirm_display);
}
bool SavedTweaks::reapply(size_t index, const std::string& game_path,
                          bool confirmed, bool (*confirm_display)()) {
    if (index>=TWEAK_COUNT || !_saved.contains(tweak_id(index))) return false;
    // Preserve the original target identity (game path, power scheme, display).
    try {
        json entries=_saved.at(tweak_id(index)).at("targets");
        for (auto& item:entries) {
            json before;
            if (!read_target(item,before)) return false;
            item["before"]=before;
        }
        if ((index==28 || index==37 || index==39 || index==40 ||
             index==41 || index==44 || (index>=47 && index<=50)) && !confirmed) return false;
        if (index==28 && !confirm_display) return false;
        json operation={{"id",tweak_id(index)},{"targets",entries},
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
        updated[tweak_id(index)]={{"targets",entries}};
        if (!write(_saved_path,updated)) { _blocked=true; return false; }
        _saved=updated;
        if (!DeleteFileA(_operation_path.c_str())) { _blocked=true; return false; }
        refresh_saved(game_path);
        return true;
    } catch (...) { return false; }
}
bool SavedTweaks::remove(size_t index) {
    if (_blocked || index>=TWEAK_COUNT || !_saved.contains(tweak_id(index))) return false;
    json excluded=_dismissed;
    excluded[tweak_id(index)]=true;
    if (!write(_dismissed_path,excluded)) return false;
    _dismissed=std::move(excluded);
    json updated=_saved;
    updated.erase(tweak_id(index));
    if (!write(_saved_path,updated)) return false;
    _saved=updated;
    _info[index].saved=false;
    _info[index].drift=false;
    return true;
}
