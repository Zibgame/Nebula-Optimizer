#include <winsock2.h>
#include "optimizer.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlobj.h>
#include <powrprof.h>
#include <objbase.h>
#include <winsvc.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <cfgmgr32.h>
#include <chrono>
#include <cwctype>
#include "json.hpp"
#include "power_qos.hpp"

namespace {

struct GameWindowSearch { DWORD pid; HWND window; LONG area; };
BOOL CALLBACK find_game_window(HWND window, LPARAM value)
{
    auto& search = *reinterpret_cast<GameWindowSearch*>(value);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != search.pid || !IsWindowVisible(window) || GetWindow(window, GW_OWNER))
        return TRUE;
    RECT bounds{};
    if (!GetWindowRect(window, &bounds)) return TRUE;
    const LONG area = (bounds.right - bounds.left) * (bounds.bottom - bounds.top);
    if (area > search.area) { search.area = area; search.window = window; }
    return TRUE;
}

constexpr const char* MMCSS_PROFILE =
    "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile";
constexpr const char* MMCSS_GAMES =
    "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games";
size_t tweak_index_for_label(const char* label)
{
    struct Match { const char* label; size_t index; };
    static const Match matches[] = {
        {"High-performance power plan", 0}, {"Sleep paused during session", 1},
        {"Game DVR off", 2}, {"Background capture off", 3},
        {"Game Bar popup off", 4}, {"Game Mode allowed", 5},
        {"Game Mode on", 5}, {"MMCSS network throttling off", 6},
        {"MMCSS system reserve: 10%", 7}, {"MMCSS game priority", 8},
        {"MMCSS game scheduling", 9}, {"Foreground CPU scheduling", 10},
        {"High-performance GPU preference", 12},
        {"Game power throttling off", 13}, {"Game CPU priority: high", 14},
        {"Controller Game Bar shortcut off", 15},
        {"Recording shortcut off", 16}, {"Replay shortcut off", 17},
        {"Game Bar shortcut off", 18},
        {"AC CPU performance preference", 23},
        {"AC PCIe link power saving off", 24}, {"AC CPU boost mode", 25},
        {"Game dynamic priority boost", 26},
        {"Game memory priority normal", 27}
    };
    for (const Match& match : matches)
        if (std::strcmp(match.label, label) == 0)
            return match.index;
    for (size_t index = 0; index < TWEAK_CATALOG.size(); ++index)
        if (std::strcmp(TWEAK_CATALOG[index].label, label) == 0)
            return index;
    return TWEAK_COUNT;
}

constexpr GUID CPU_EPP = {0x36687f9e, 0xe3a5, 0x4dbf,
                          {0xb1, 0xdc, 0x15, 0xeb, 0x38, 0x1c, 0x68, 0x63}};
constexpr GUID CPU_SUBGROUP = {0x54533251, 0x82be, 0x4824,
                               {0x96, 0xc1, 0x47, 0xb6, 0x0b, 0x74, 0x0d, 0x00}};
constexpr GUID PCIE_ASPM = {0xee12f906, 0xd277, 0x404b,
                            {0xb6, 0xda, 0xe5, 0xfa, 0x1a, 0x57, 0x6d, 0xf5}};
constexpr GUID PCIE_SUBGROUP = {0x501a4d13, 0x42af, 0x4429,
                                {0x9f, 0xd1, 0xa8, 0x21, 0x8c, 0x26, 0x8e, 0x20}};
constexpr GUID CPU_BOOST = {0xbe337238, 0x0d82, 0x4146,
                            {0xa9, 0x60, 0x4f, 0x37, 0x49, 0xd4, 0x70, 0xc7}};
constexpr GUID CPU_MINIMUM = {0x893dee8e, 0x2bef, 0x41e0,
                              {0x89, 0xc6, 0xb5, 0x5d, 0x09, 0x29, 0x96, 0x4c}};
constexpr GUID CORE_PARKING = {0x0cc5b647, 0xc1df, 0x4637,
                               {0x89, 0x1a, 0xde, 0xc3, 0x5c, 0x31, 0x85, 0x83}};
constexpr GUID CORE_PARKING_1 = {0x0cc5b647, 0xc1df, 0x4637,
                                 {0x89, 0x1a, 0xde, 0xc3, 0x5c, 0x31, 0x85, 0x84}};
constexpr GUID WIFI_SUBGROUP = {0x19cbb8fa, 0x5279, 0x450e,
                                {0x9f, 0xac, 0x8a, 0x3d, 0x5f, 0xed, 0xd0, 0xc1}};
constexpr GUID WIFI_POWER = {0x12bbebe6, 0x58d6, 0x4636,
                             {0x95, 0xbb, 0x32, 0x17, 0xef, 0x86, 0x7c, 0x1a}};
constexpr GUID USB_SUBGROUP = {0x2a737441, 0x1930, 0x4402,
                               {0x8d, 0x77, 0xb2, 0xbe, 0xbb, 0xa3, 0x08, 0xa3}};
constexpr GUID USB_SUSPEND = {0x48e6b7a6, 0x50f5, 0x4782,
                              {0xa5, 0xd4, 0x53, 0xbb, 0x8f, 0x07, 0xe2, 0x26}};
constexpr GUID HIGH_PERFORMANCE_SCHEME = {0x8c5e7fda, 0xe8bf, 0x4a96,
    {0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c}};

const GUID* power_subgroup(unsigned int kind)
{
    return kind == 0 || kind == 2 || kind == 3 || kind == 4 || kind == 7 ?
           &CPU_SUBGROUP :
           kind == 1 ? &PCIE_SUBGROUP :
           kind == 5 ? &WIFI_SUBGROUP :
           kind == 6 ? &USB_SUBGROUP : nullptr;
}

const GUID* power_setting(unsigned int kind)
{
    return kind == 0 ? &CPU_EPP : kind == 1 ? &PCIE_ASPM :
           kind == 2 ? &CPU_BOOST : kind == 3 ? &CORE_PARKING :
           kind == 4 ? &CORE_PARKING_1 : kind == 5 ? &WIFI_POWER :
           kind == 6 ? &USB_SUSPEND : kind == 7 ? &CPU_MINIMUM : nullptr;
}

std::string guid_text(const GUID& guid)
{
    wchar_t wide[40]{};
    char narrow[40]{};
    if (!StringFromGUID2(guid, wide, 40) ||
        !WideCharToMultiByte(CP_ACP, 0, wide, -1, narrow, 40, nullptr, nullptr))
        return {};
    return narrow;
}

bool parse_guid(const std::string& value, GUID& guid)
{
    wchar_t wide[40]{};
    if (!MultiByteToWideChar(CP_ACP, 0, value.c_str(), -1, wide, 40))
        return false;
    return CLSIDFromString(wide, &guid) == S_OK;
}

bool write_registry(HKEY root, const char* path, const char* name,
                    DWORD type, const void* data, DWORD size)
{
    HKEY key = nullptr;
    if (RegCreateKeyExA(root, path, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    const LONG result = RegSetValueExA(key, name, 0, type,
                                      static_cast<const BYTE*>(data), size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

void save_power_plan(char* output, size_t output_size)
{
    output[0] = '\0';
    GUID* active = nullptr;
    if (PowerGetActiveScheme(nullptr, &active) != ERROR_SUCCESS || !active)
        return;
    const std::string value = guid_text(*active);
    LocalFree(active);
    if (value.size() + 1 <= output_size)
        std::memcpy(output, value.c_str(), value.size() + 1);
}

bool set_power_plan(const char* scheme)
{
    GUID parsed{};
    if (_stricmp(scheme, "SCHEME_MIN") == 0)
        parsed = HIGH_PERFORMANCE_SCHEME;
    else if (!parse_guid(scheme, parsed))
        return false;
    return PowerSetActiveScheme(nullptr, &parsed) == ERROR_SUCCESS;
}

std::string executable_directory(const std::string& path)
{
    const size_t separator = path.find_last_of("\\/");
    return separator == std::string::npos ? std::string() : path.substr(0, separator);
}

std::string target_game_path(const std::string& configured_path)
{
    const std::string name = configured_path.substr(configured_path.find_last_of("\\/") + 1);
    if (_stricmp(name.c_str(), "FortniteClient-Win64-Shipping_EAC_EOS.exe") == 0 ||
        _stricmp(name.c_str(), "FortniteLauncher.exe") == 0)
        return executable_directory(configured_path) + "\\FortniteClient-Win64-Shipping.exe";
    return configured_path;
}

DWORD running_game_pid(const std::string& exact_path)
{
    const std::string name = exact_path.substr(exact_path.find_last_of("\\/") + 1);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;
    PROCESSENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    DWORD pid = 0;
    if (Process32First(snapshot, &entry)) {
        do {
            if (_stricmp(entry.szExeFile, name.c_str()) != 0)
                continue;
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                         FALSE, entry.th32ProcessID);
            if (!process)
                continue;
            char image[MAX_PATH * 4]{};
            DWORD size = sizeof(image);
            const bool match = QueryFullProcessImageNameA(process, 0, image, &size) &&
                               _stricmp(image, exact_path.c_str()) == 0;
            CloseHandle(process);
            if (match) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return pid;
}

bool is_admin()
{
    BOOL member = FALSE;
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID administrators = nullptr;
    if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                 &administrators)) {
        CheckTokenMembership(nullptr, administrators, &member);
        FreeSid(administrators);
    }
    return member == TRUE;
}

ULONGLONG filetime_ticks(const FILETIME& time)
{
    return (static_cast<ULONGLONG>(time.dwHighDateTime) << 32) |
           time.dwLowDateTime;
}

struct ActiveAdapterInfo {
    GUID id{};
    DWORD index = 0;
    ULONG type = 0;
    std::string registry_path;
    std::string device_instance;
};

struct DnsInterfaceSettingsV1 {
    ULONG Version;
    ULONG64 Flags;
    PWSTR Domain;
    PWSTR NameServer;
    PWSTR SearchList;
    ULONG RegistrationEnabled;
    ULONG RegisterAdapterName;
    ULONG EnableLLMNR;
    ULONG QueryAdapterName;
    PWSTR ProfileNameServer;
};
using GetInterfaceDnsSettingsFn = DWORD (WINAPI*)(GUID, DnsInterfaceSettingsV1*);
using SetInterfaceDnsSettingsFn = DWORD (WINAPI*)(GUID, const DnsInterfaceSettingsV1*);
using FreeInterfaceDnsSettingsFn = VOID (WINAPI*)(DnsInterfaceSettingsV1*);
constexpr ULONG DNS_INTERFACE_SETTINGS_VERSION_1 = 1;
constexpr ULONG64 DNS_SETTING_NAME_SERVER = 0x0002;

template<class Function>
Function load_ip_helper(const char* name)
{
    HMODULE module = GetModuleHandleA("iphlpapi.dll");
    if (!module) module = LoadLibraryA("iphlpapi.dll");
    FARPROC address = module ? GetProcAddress(module, name) : nullptr;
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

std::string narrow_utf8(const std::wstring& value)
{
    if (value.empty()) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
                                          static_cast<int>(value.size()),
                                          nullptr, 0, nullptr, nullptr);
    std::string result(bytes, '\0');
    if (bytes) WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
                                   static_cast<int>(value.size()), result.data(),
                                   bytes, nullptr, nullptr);
    return result;
}

std::wstring widen_utf8(const std::string& value)
{
    if (value.empty()) return {};
    const int chars = MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
                                          static_cast<int>(value.size()),
                                          nullptr, 0);
    std::wstring result(chars, L'\0');
    if (chars) MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
                                   static_cast<int>(value.size()), result.data(),
                                   chars);
    return result;
}

bool dns_api(GetInterfaceDnsSettingsFn& get, SetInterfaceDnsSettingsFn& set,
             FreeInterfaceDnsSettingsFn& release)
{
    get = load_ip_helper<GetInterfaceDnsSettingsFn>("GetInterfaceDnsSettings");
    set = load_ip_helper<SetInterfaceDnsSettingsFn>("SetInterfaceDnsSettings");
    release = load_ip_helper<FreeInterfaceDnsSettingsFn>("FreeInterfaceDnsSettings");
    return get && set && release;
}

std::wstring current_dns_servers(const GUID& id)
{
    GetInterfaceDnsSettingsFn get = nullptr;
    SetInterfaceDnsSettingsFn set = nullptr;
    FreeInterfaceDnsSettingsFn release = nullptr;
    if (!dns_api(get, set, release)) return {};
    DnsInterfaceSettingsV1 settings{};
    settings.Version = DNS_INTERFACE_SETTINGS_VERSION_1;
    if (get(id, &settings) != NO_ERROR) return {};
    const std::wstring result = settings.NameServer ? settings.NameServer : L"";
    release(&settings);
    return result;
}

bool set_dns_servers(const GUID& id, const std::wstring& servers)
{
    GetInterfaceDnsSettingsFn get = nullptr;
    SetInterfaceDnsSettingsFn set = nullptr;
    FreeInterfaceDnsSettingsFn release = nullptr;
    if (!dns_api(get, set, release)) return false;
    DnsInterfaceSettingsV1 settings{};
    settings.Version = DNS_INTERFACE_SETTINGS_VERSION_1;
    settings.Flags = DNS_SETTING_NAME_SERVER;
    settings.NameServer = servers.empty() ? nullptr :
        const_cast<PWSTR>(servers.c_str());
    return set(id, &settings) == NO_ERROR;
}

bool same_dns_servers(const std::wstring& left, const std::wstring& right)
{
    auto normalize = [](const std::wstring& input) {
        std::wstring output;
        bool separator = false;
        for (wchar_t character : input) {
            if (character == L',' || character == L';' || iswspace(character)) {
                separator = !output.empty();
            } else {
                if (separator && output.back() != L',') output.push_back(L',');
                separator = false;
                output.push_back(static_cast<wchar_t>(towlower(character)));
            }
        }
        while (!output.empty() && output.back() == L',') output.pop_back();
        return output;
    };
    return normalize(left) == normalize(right);
}

int dns_latency_ms(const char* server)
{
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(53);
    if (inet_pton(AF_INET, server, &target.sin_addr) != 1) return -1;
    std::vector<int> samples;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        SOCKET socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_handle == INVALID_SOCKET) break;
        DWORD timeout = 700;
        setsockopt(socket_handle, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        unsigned char query[64]{};
        const USHORT id = static_cast<USHORT>((GetTickCount() + attempt * 7919) & 0xffff);
        query[0] = static_cast<unsigned char>(id >> 8); query[1] = static_cast<unsigned char>(id);
        query[2] = 1; query[5] = 1;
        size_t position = 12;
        for (const char* label : {"www", "epicgames", "com"}) {
            const size_t length = std::strlen(label);
            query[position++] = static_cast<unsigned char>(length);
            std::memcpy(query + position, label, length); position += length;
        }
        query[position++] = 0; query[position++] = 0; query[position++] = 1;
        query[position++] = 0; query[position++] = 1;
        const auto start = std::chrono::steady_clock::now();
        const int sent = sendto(socket_handle,
            reinterpret_cast<const char*>(query), static_cast<int>(position), 0,
            reinterpret_cast<const sockaddr*>(&target), sizeof(target));
        unsigned char response[512]{}; sockaddr_in from{}; int from_size = sizeof(from);
        const int received = sent > 0 ? recvfrom(socket_handle,
            reinterpret_cast<char*>(response), sizeof(response), 0,
            reinterpret_cast<sockaddr*>(&from), &from_size) : -1;
        const auto stop = std::chrono::steady_clock::now();
        closesocket(socket_handle);
        if (received >= 12 && response[0] == query[0] && response[1] == query[1])
            samples.push_back(static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(stop-start).count()));
    }
    if (samples.size() < 2) return -1;
    std::sort(samples.begin(), samples.end());
    return samples[samples.size()/2];
}

bool read_registry_string(HKEY root, const std::string& path,
                          const char* name, std::string& value)
{
    HKEY key = nullptr;
    if (RegOpenKeyExA(root, path.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    DWORD type = 0, bytes = 0;
    LONG result = RegQueryValueExA(key, name, nullptr, &type, nullptr, &bytes);
    if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        RegCloseKey(key); return false;
    }
    std::vector<char> buffer(bytes + 1, 0);
    result = RegQueryValueExA(key, name, nullptr, &type,
                             reinterpret_cast<BYTE*>(buffer.data()), &bytes);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) return false;
    value.assign(buffer.data());
    return true;
}

bool active_adapter(ActiveAdapterInfo& out)
{
    IPAddr destination = inet_addr("1.1.1.1");
    if (GetBestInterface(destination, &out.index) != NO_ERROR)
        return false;
    ULONG bytes = 0;
    GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, nullptr, &bytes);
    std::vector<unsigned char> storage(bytes);
    auto* first = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(storage.data());
    if (!bytes || GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX,
                                       nullptr, first, &bytes) != NO_ERROR)
        return false;
    std::string adapter_name;
    for (auto* item = first; item; item = item->Next) {
        if (item->IfIndex == out.index || item->Ipv6IfIndex == out.index) {
            adapter_name = item->AdapterName ? item->AdapterName : "";
            out.type = item->IfType;
            break;
        }
    }
    if (adapter_name.empty()) return false;
    std::wstring wide(adapter_name.begin(), adapter_name.end());
    if (wide.empty() || wide.front() != L'{') wide = L"{" + wide + L"}";
    if (CLSIDFromString(wide.c_str(), &out.id) != S_OK) return false;

    const std::string base =
        "SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e972-e325-11ce-bfc1-08002be10318}";
    HKEY root = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, base.c_str(), 0,
                      KEY_ENUMERATE_SUB_KEYS, &root) != ERROR_SUCCESS)
        return false;
    for (DWORD number = 0;; ++number) {
        char name[256]{}; DWORD length = sizeof(name);
        if (RegEnumKeyExA(root, number, name, &length, nullptr, nullptr,
                          nullptr, nullptr) != ERROR_SUCCESS)
            break;
        const std::string path = base + "\\" + name;
        std::string id;
        if (read_registry_string(HKEY_LOCAL_MACHINE, path,
                                 "NetCfgInstanceId", id) &&
            _stricmp(id.c_str(), adapter_name.c_str()) == 0) {
            out.registry_path = path;
            read_registry_string(HKEY_LOCAL_MACHINE, path,
                                 "PnPInstanceID", out.device_instance);
            break;
        }
    }
    RegCloseKey(root);
    return !out.registry_path.empty();
}

std::string effective_dns_server(DWORD interface_index)
{
    ULONG bytes = 0;
    GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, nullptr, &bytes);
    std::vector<unsigned char> storage(bytes);
    auto* first = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(storage.data());
    if (!bytes || GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, first, &bytes) != NO_ERROR)
        return {};
    for (auto* item = first; item; item = item->Next) {
        if (item->IfIndex != interface_index && item->Ipv6IfIndex != interface_index)
            continue;
        for (auto* dns = item->FirstDnsServerAddress; dns; dns = dns->Next) {
            if (!dns->Address.lpSockaddr ||
                dns->Address.lpSockaddr->sa_family != AF_INET) continue;
            char host[NI_MAXHOST]{};
            if (getnameinfo(dns->Address.lpSockaddr,
                            static_cast<socklen_t>(dns->Address.iSockaddrLength),
                            host, sizeof(host), nullptr, 0, NI_NUMERICHOST) == 0)
                return host;
        }
    }
    return {};
}

std::string lower_ascii(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool driver_enum_value(const ActiveAdapterInfo& adapter, const char* keyword,
                       const std::vector<std::string>& wanted,
                       DWORD& type, std::string& string_value, DWORD& dword_value)
{
    const std::string enum_path = adapter.registry_path +
        "\\Ndi\\Params\\" + keyword + "\\enum";
    HKEY values = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, enum_path.c_str(), 0,
                      KEY_QUERY_VALUE, &values) != ERROR_SUCCESS)
        return false;
    bool found = false;
    for (DWORD index = 0; !found; ++index) {
        char name[128]{}; DWORD name_size = sizeof(name);
        BYTE data[512]{}; DWORD data_size = sizeof(data), data_type = 0;
        if (RegEnumValueA(values, index, name, &name_size, nullptr, &data_type,
                          data, &data_size) != ERROR_SUCCESS)
            break;
        if (data_type != REG_SZ) continue;
        const std::string display = lower_ascii(reinterpret_cast<char*>(data));
        for (const std::string& token : wanted) {
            if (display.find(lower_ascii(token)) != std::string::npos) {
                string_value.assign(name, name_size);
                dword_value = std::strtoul(string_value.c_str(), nullptr, 0);
                found = true; break;
            }
        }
    }
    RegCloseKey(values);
    if (!found) return false;
    HKEY adapter_key = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, adapter.registry_path.c_str(), 0,
                      KEY_QUERY_VALUE, &adapter_key) != ERROR_SUCCESS)
        return false;
    DWORD size = 0;
    const LONG result = RegQueryValueExA(adapter_key, keyword, nullptr,
                                         &type, nullptr, &size);
    RegCloseKey(adapter_key);
    return result == ERROR_SUCCESS && (type == REG_SZ || type == REG_DWORD);
}

bool restart_adapter_device(const std::string& instance)
{
    if (instance.empty()) return false;
    std::vector<char> text(instance.begin(), instance.end());
    text.push_back('\0');
    DEVINST device = 0;
    if (CM_Locate_DevNodeA(&device, text.data(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
        return false;
    if (CM_Disable_DevNode(device, 0) != CR_SUCCESS)
        return false;
    Sleep(300);
    return CM_Enable_DevNode(device, 0) == CR_SUCCESS;
}

} // namespace

Optimizer::Optimizer()
    : _is_optimized(false),
      _power_request_active(false),
      _game_process(nullptr),
      _game_pid(0),
      _waiting_for_game(false),
      _launch_started(0),
      _last_helper_scan(0),
      _recovery_failed(false),
      _restoration_succeeded(false)
{
    for (size_t index = 0; index < TWEAK_COUNT; ++index)
        _tweaks_enabled[index] = TWEAK_CATALOG[index].default_enabled;
    _tweak_status.fill(TweakStatus::On);
    _saved_power_plan_guid[0] = '\0';
    char local_app_data[MAX_PATH]{};
    const DWORD app_data_length = GetEnvironmentVariableA(
        "LOCALAPPDATA", local_app_data, sizeof(local_app_data));
    if (app_data_length && app_data_length < sizeof(local_app_data)) {
        _journal_path = std::string(local_app_data) +
                        "\\NebulaOptimizer\\session-journal.json";
        _preferences_path = std::string(local_app_data) +
                            "\\NebulaOptimizer\\tweaks.json";
        load_preferences();
        _saved_tweaks = std::make_unique<SavedTweaks>(
            std::string(local_app_data) + "\\NebulaOptimizer");
        if (!_saved_tweaks->recover()) {
            _recovery_failed = true;
            _restoration_status = _saved_tweaks->error();
        }
        if (!_recovery_failed)
            recover_journal();
    }
    for (size_t index = 0; index < _tweaks_enabled.size(); ++index)
        if (!_tweaks_enabled[index])
            _tweak_status[index] = TweakStatus::Off;
}

Optimizer::~Optimizer()
{
    restore();
}

bool Optimizer::enabled(size_t index) const
{
    return index < _tweaks_enabled.size() && _tweaks_enabled[index] &&
        !(_saved_tweaks && _saved_tweaks->info(index).drift);
}

std::vector<Optimizer::TweakSetting> Optimizer::tweak_settings() const
{
    const std::lock_guard<std::recursive_mutex> lock(_state_mutex);
    std::vector<TweakSetting> result;
    result.reserve(_tweaks_enabled.size());
    for (size_t index = 0; index < _tweaks_enabled.size(); ++index)
        result.push_back({TWEAK_CATALOG[index].label, TWEAK_CATALOG[index].category,
                          _tweaks_enabled[index], _tweak_status[index],
                          _saved_tweaks ? _saved_tweaks->info(index) : SavedTweaks::Info{}});
    return result;
}

void Optimizer::load_preferences()
{
    if (_preferences_path.empty())
        return;
    try {
        std::ifstream file(_preferences_path);
        if (!file)
            return;
        nlohmann::json settings;
        file >> settings;
        const unsigned version = settings.value("_catalog_version", 1u);
        for (size_t index = 0; index < _tweaks_enabled.size(); ++index)
            if (settings.contains(TWEAK_CATALOG[index].id) &&
                settings[TWEAK_CATALOG[index].id].is_boolean())
                _tweaks_enabled[index] = settings[TWEAK_CATALOG[index].id].get<bool>();
        if (version < 2)
            for (size_t index = 6; index <= 10; ++index)
                _tweaks_enabled[index] = false;
        if (version < 2) save_preferences();
    } catch (...) {}
}

bool Optimizer::save_preferences() const
{
    if (_preferences_path.empty())
        return false;
    try {
        nlohmann::json settings;
        settings["_catalog_version"] = 2;
        for (size_t index = 0; index < _tweaks_enabled.size(); ++index)
            settings[TWEAK_CATALOG[index].id] = _tweaks_enabled[index];
        std::filesystem::create_directories(
            std::filesystem::path(_preferences_path).parent_path());
        const std::string temporary = _preferences_path + ".tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file)
                return false;
            file << settings.dump(2);
            file.flush();
            if (!file)
                return false;
        }
        return MoveFileExA(temporary.c_str(), _preferences_path.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    } catch (...) {
        return false;
    }
}

bool Optimizer::toggle_tweak(size_t index)
{
    const std::lock_guard<std::recursive_mutex> lock(_state_mutex);
    if (index >= _tweaks_enabled.size())
        return false;
    _tweaks_enabled[index] = !_tweaks_enabled[index];
    if (save_preferences())
    {
        _tweak_status[index] = _tweaks_enabled[index] ? TweakStatus::On : TweakStatus::Off;
        return true;
    }
    _tweaks_enabled[index] = !_tweaks_enabled[index];
    return false;
}

bool Optimizer::scan_saved(const std::string& game_path)
{
    const std::lock_guard<std::recursive_mutex> lock(_state_mutex);
    return _saved_tweaks && !_recovery_failed && _saved_tweaks->scan(game_path);
}

bool Optimizer::has_baseline_scan(const std::string& game_path) const
{
    const std::lock_guard<std::recursive_mutex> lock(_state_mutex);
    return _saved_tweaks && _saved_tweaks->has_baseline_for(game_path);
}

bool Optimizer::save_tweak(size_t index, const std::string& game_path,
                           bool confirmed, bool (*confirm_display)())
{
    const std::lock_guard<std::recursive_mutex> lock(_state_mutex);
    return _saved_tweaks && !_recovery_failed && !_is_optimized &&
           index < _tweaks_enabled.size() && _tweaks_enabled[index] &&
           _saved_tweaks->apply(index, game_path, confirmed, confirm_display);
}

bool Optimizer::unsave_tweak(size_t index)
{
    const std::lock_guard<std::recursive_mutex> lock(_state_mutex);
    return _saved_tweaks && !_recovery_failed && _saved_tweaks->remove(index);
}

bool Optimizer::reapply_saved_tweak(size_t index, const std::string& game_path,
                                    bool confirmed, bool (*confirm_display)())
{
    const std::lock_guard<std::recursive_mutex> lock(_state_mutex);
    return _saved_tweaks && !_recovery_failed && !_is_optimized &&
           _saved_tweaks->reapply(index, game_path, confirmed, confirm_display);
}

const std::string& Optimizer::saved_error() const
{
    static const std::string unavailable = "Saved settings unavailable";
    return _saved_tweaks ? _saved_tweaks->error() : unavailable;
}

void Optimizer::record(bool success, const char* label)
{
    (success ? _applied : _failed).push_back(label);
    set_status(tweak_index_for_label(label),
               success ? TweakStatus::Applied : TweakStatus::Failed);
}

bool Optimizer::start_recovery_watchdog()
{
    if (_watchdog_started) return true;
    char executable[MAX_PATH * 4]{};
    if (!GetModuleFileNameA(nullptr, executable, sizeof(executable))) return false;
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return false;
    const ULONGLONG identity = filetime_ticks(created);
    const std::string command = std::string("\"") + executable +
        "\" --watchdog " + std::to_string(GetCurrentProcessId()) + " " +
        std::to_string(identity);
    std::vector<char> mutable_command(command.begin(), command.end());
    mutable_command.push_back('\0');
    STARTUPINFOA startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    const BOOL started = CreateProcessA(executable, mutable_command.data(), nullptr,
        nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child);
    if (!started) return false;
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    _watchdog_started = true;
    return true;
}

void Optimizer::set_status(size_t index, TweakStatus status)
{
    if (index >= _tweak_status.size() || !_tweaks_enabled[index])
        return;
    if (_tweak_status[index] == TweakStatus::Failed && status != TweakStatus::Failed)
        return;
    if (_tweak_status[index] == TweakStatus::Applied &&
        (status == TweakStatus::Skipped ||
         status == TweakStatus::AlreadyConfigured ||
         status == TweakStatus::Unsupported))
        return;
    _tweak_status[index] = status;
}

bool Optimizer::save_journal() const
{
    if (_journal_path.empty())
        return false;
    try {
        nlohmann::json state;
        state["power_plan"] = _saved_power_plan_guid;
        state["baseline_game_path"] = _baseline_game_path;
        state["session_power_plan"] = _session_power_plan_guid;
        state["registry"] = nlohmann::json::array();
        state["background"] = nlohmann::json::array();
        state["ac_power_settings"] = nlohmann::json::array();
        state["advanced_processes"] = nlohmann::json::array();
        state["services"] = nlohmann::json::array();
        state["network_device_instance"] = _network_device_instance;
        state["network_restart_needed"] = _network_restart_needed;
        if (_dns_state.touched) {
            state["dns"] = {{"interface", guid_text(_dns_state.interface_id)},
                {"original", narrow_utf8(_dns_state.original)},
                {"applied", narrow_utf8(_dns_state.applied)},
                {"touched", true}};
        }
        if (!_display_state.device.empty())
            state["display"] = {{"device", _display_state.device},
                {"frequency", _display_state.frequency},
                {"width", _display_state.width}, {"height", _display_state.height},
                {"bits", _display_state.bits_per_pel},
                {"orientation", _display_state.orientation},
                {"fixed_output", _display_state.fixed_output},
                {"position_x", _display_state.position_x},
                {"position_y", _display_state.position_y},
                {"applied_frequency", _display_state.applied_frequency}};
        if (_accessibility_state.filter_touched ||
            _accessibility_state.sticky_touched ||
            _accessibility_state.toggle_touched) {
            const AccessibilityState& a = _accessibility_state;
            state["accessibility"] = {
                {"filter_flags", a.filter.dwFlags},
                {"filter_wait", a.filter.iWaitMSec},
                {"filter_delay", a.filter.iDelayMSec},
                {"filter_repeat", a.filter.iRepeatMSec},
                {"filter_bounce", a.filter.iBounceMSec},
                {"sticky_flags", a.sticky.dwFlags},
                {"toggle_flags", a.toggle.dwFlags},
                {"filter_applied", a.filter_applied_flags},
                {"sticky_applied", a.sticky_applied_flags},
                {"toggle_applied", a.toggle_applied_flags},
                {"filter_touched", a.filter_touched},
                {"sticky_touched", a.sticky_touched},
                {"toggle_touched", a.toggle_touched}
            };
        }
        const InputTuningState& tuning = _input_tuning_state;
        if (tuning.filter_touched || tuning.delay_touched ||
            tuning.speed_touched || tuning.mouse_touched) {
            state["input_tuning"] = {
                {"filter_touched", tuning.filter_touched},
                {"filter_original", {tuning.filter.dwFlags, tuning.filter.iWaitMSec,
                    tuning.filter.iDelayMSec, tuning.filter.iRepeatMSec,
                    tuning.filter.iBounceMSec}},
                {"filter_applied", {tuning.filter_applied.dwFlags,
                    tuning.filter_applied.iWaitMSec, tuning.filter_applied.iDelayMSec,
                    tuning.filter_applied.iRepeatMSec,
                    tuning.filter_applied.iBounceMSec}},
                {"delay_touched", tuning.delay_touched},
                {"delay", tuning.delay}, {"delay_applied", tuning.delay_applied},
                {"speed_touched", tuning.speed_touched},
                {"speed", tuning.speed}, {"speed_applied", tuning.speed_applied},
                {"mouse_touched", tuning.mouse_touched},
                {"mouse", tuning.mouse}, {"mouse_applied", tuning.mouse_applied}
            };
        }
        for (const RegistryState& entry : _registry_state) {
            state["registry"].push_back({
                {"root", entry.root == HKEY_LOCAL_MACHINE ? "HKLM" : "HKCU"},
                {"path", entry.path}, {"name", entry.name},
                {"type", entry.type}, {"data", entry.data},
                {"existed", entry.existed},
                {"applied_type", entry.applied_type},
                {"applied_data", entry.applied_data},
                {"compare_and_swap", entry.compare_and_swap}
            });
        }
        for (const BackgroundState& entry : _background_state) {
            state["background"].push_back({
                {"pid", entry.pid}, {"created_at", entry.created_at},
                {"priority", entry.priority_class},
                {"throttling_known", entry.throttling_known},
                {"throttling_control", entry.throttling.ControlMask},
                {"throttling_state", entry.throttling.StateMask},
                {"memory_priority", entry.memory_priority},
                {"memory_known", entry.memory_known},
                {"boost_disabled", entry.priority_boost_disabled},
                {"boost_known", entry.boost_known},
                {"priority_touched", entry.priority_touched},
                {"throttling_touched", entry.throttling_touched},
                {"memory_touched", entry.memory_touched},
                {"boost_touched", entry.boost_touched},
                {"compare_and_swap", entry.compare_and_swap},
                {"applied_priority", entry.applied_priority_class},
                {"applied_throttling_control", entry.applied_throttling_control},
                {"applied_throttling_state", entry.applied_throttling_state},
                {"applied_memory_priority", entry.applied_memory_priority},
                {"applied_boost_disabled", entry.applied_boost_disabled}
            });
        }
        for (const PowerSettingState& entry : _power_settings) {
            state["ac_power_settings"].push_back({
                {"scheme", guid_text(entry.scheme)},
                {"kind", entry.kind}, {"original", entry.original},
                {"applied", entry.applied},
                {"compare_and_swap", entry.compare_and_swap}
            });
        }
        for (const AdvancedProcessState& entry : _advanced_processes) {
            state["advanced_processes"].push_back({
                {"pid", entry.pid}, {"created_at", entry.created_at},
                {"cpu_sets", entry.cpu_sets},
                {"applied_cpu_sets", entry.applied_cpu_sets},
                {"io_priority", entry.io_priority},
                {"applied_io_priority", entry.applied_io_priority},
                {"cpu_sets_touched", entry.cpu_sets_touched},
                {"io_touched", entry.io_touched}
            });
        }
        for (const ServiceState& entry : _service_states) {
            state["services"].push_back({
                {"name", entry.name}, {"original_state", entry.original_state},
                {"applied_state", entry.applied_state}, {"touched", entry.touched}
            });
        }
        std::filesystem::create_directories(
            std::filesystem::path(_journal_path).parent_path());
        const std::string temporary = _journal_path + ".tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file)
                return false;
            file << state.dump();
            file.flush();
            if (!file)
                return false;
        }
        return MoveFileExA(temporary.c_str(), _journal_path.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    } catch (...) {
        return false;
    }
}

void Optimizer::recover_journal()
{
    if (GetFileAttributesA(_journal_path.c_str()) == INVALID_FILE_ATTRIBUTES)
        return;
    _recovery_failed = false;
    try {
        std::ifstream file(_journal_path, std::ios::binary);
        nlohmann::json state;
        file >> state;
        file.close();
        const std::string power = state.value("power_plan", std::string());
        _baseline_game_path = state.value("baseline_game_path", std::string());
        if (power.size() >= sizeof(_saved_power_plan_guid))
            throw std::runtime_error("Invalid original power scheme in recovery log");
        std::memcpy(_saved_power_plan_guid, power.c_str(), power.size() + 1);
        _session_power_plan_guid = state.value("session_power_plan", std::string());
        for (const auto& item : state.at("registry")) {
            RegistryState entry{};
            entry.root = item.at("root").get<std::string>() == "HKLM"
                ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
            entry.path = item.at("path").get<std::string>();
            entry.name = item.at("name").get<std::string>();
            entry.type = item.at("type").get<DWORD>();
            entry.data = item.at("data").get<std::vector<unsigned char>>();
            entry.existed = item.at("existed").get<bool>();
            entry.applied_type = item.value("applied_type", DWORD(0));
            entry.applied_data = item.value(
                "applied_data", std::vector<unsigned char>{});
            entry.compare_and_swap = item.value("compare_and_swap", false);
            _registry_state.push_back(std::move(entry));
        }
        for (const auto& item : state.value("background", nlohmann::json::array())) {
            BackgroundState entry{};
            entry.process = nullptr;
            entry.pid = item.at("pid").get<DWORD>();
            entry.created_at = item.at("created_at").get<ULONGLONG>();
            entry.priority_class = item.at("priority").get<DWORD>();
            entry.throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
            entry.throttling.ControlMask = item.at("throttling_control").get<DWORD>();
            entry.throttling.StateMask = item.at("throttling_state").get<DWORD>();
            entry.throttling_known = item.at("throttling_known").get<bool>();
            entry.memory_priority = item.value("memory_priority", DWORD(0));
            entry.memory_known = item.value("memory_known", false);
            entry.priority_boost_disabled = item.value("boost_disabled", false);
            entry.boost_known = item.value("boost_known", false);
            entry.priority_touched = item.value("priority_touched", true);
            entry.throttling_touched = item.value("throttling_touched", true);
            entry.memory_touched = item.value("memory_touched", false);
            entry.boost_touched = item.value("boost_touched", false);
            entry.compare_and_swap = item.value("compare_and_swap", false);
            entry.applied_priority_class = item.value("applied_priority", DWORD(0));
            entry.applied_throttling_control = item.value(
                "applied_throttling_control",
                DWORD(PROCESS_POWER_THROTTLING_EXECUTION_SPEED));
            entry.applied_throttling_state = item.value("applied_throttling_state", DWORD(0));
            entry.applied_memory_priority = item.value("applied_memory_priority", DWORD(0));
            entry.applied_boost_disabled = item.value("applied_boost_disabled", false);
            _background_state.push_back(entry);
        }
        if (state.contains("display")) {
            const auto& display = state.at("display");
            _display_state.device = display.at("device").get<std::string>();
            _display_state.frequency = display.at("frequency").get<DWORD>();
            _display_state.width = display.at("width").get<DWORD>();
            _display_state.height = display.at("height").get<DWORD>();
            _display_state.bits_per_pel = display.at("bits").get<DWORD>();
            _display_state.orientation = display.value("orientation", DWORD(DMDO_DEFAULT));
            _display_state.fixed_output = display.value("fixed_output", DWORD(DMDFO_DEFAULT));
            _display_state.position_x = display.value("position_x", LONG(0));
            _display_state.position_y = display.value("position_y", LONG(0));
            _display_state.applied_frequency =
                display.value("applied_frequency", DWORD(0));
        }
        if (state.contains("accessibility")) {
            const auto& a = state.at("accessibility");
            _accessibility_state.filter.cbSize = sizeof(FILTERKEYS);
            _accessibility_state.sticky.cbSize = sizeof(STICKYKEYS);
            _accessibility_state.toggle.cbSize = sizeof(TOGGLEKEYS);
            _accessibility_state.filter.dwFlags = a.at("filter_flags").get<DWORD>();
            _accessibility_state.filter.iWaitMSec = a.at("filter_wait").get<DWORD>();
            _accessibility_state.filter.iDelayMSec = a.at("filter_delay").get<DWORD>();
            _accessibility_state.filter.iRepeatMSec = a.at("filter_repeat").get<DWORD>();
            _accessibility_state.filter.iBounceMSec = a.at("filter_bounce").get<DWORD>();
            _accessibility_state.sticky.dwFlags = a.at("sticky_flags").get<DWORD>();
            _accessibility_state.toggle.dwFlags = a.at("toggle_flags").get<DWORD>();
            _accessibility_state.filter_applied_flags = a.at("filter_applied").get<DWORD>();
            _accessibility_state.sticky_applied_flags = a.at("sticky_applied").get<DWORD>();
            _accessibility_state.toggle_applied_flags = a.at("toggle_applied").get<DWORD>();
            _accessibility_state.filter_touched = a.at("filter_touched").get<bool>();
            _accessibility_state.sticky_touched = a.at("sticky_touched").get<bool>();
            _accessibility_state.toggle_touched = a.at("toggle_touched").get<bool>();
        }
        if (state.contains("input_tuning")) {
            const auto& t = state.at("input_tuning");
            InputTuningState& tuning = _input_tuning_state;
            const auto original = t.at("filter_original").get<std::array<DWORD, 5>>();
            const auto applied = t.at("filter_applied").get<std::array<DWORD, 5>>();
            tuning.filter = {sizeof(FILTERKEYS), original[0], original[1],
                             original[2], original[3], original[4]};
            tuning.filter_applied = {sizeof(FILTERKEYS), applied[0], applied[1],
                                     applied[2], applied[3], applied[4]};
            tuning.filter_touched = t.at("filter_touched").get<bool>();
            tuning.delay_touched = t.at("delay_touched").get<bool>();
            tuning.delay = t.at("delay").get<UINT>();
            tuning.delay_applied = t.at("delay_applied").get<UINT>();
            tuning.speed_touched = t.at("speed_touched").get<bool>();
            tuning.speed = t.at("speed").get<UINT>();
            tuning.speed_applied = t.at("speed_applied").get<UINT>();
            tuning.mouse_touched = t.at("mouse_touched").get<bool>();
            tuning.mouse = t.at("mouse").get<std::array<int, 3>>();
            tuning.mouse_applied = t.at("mouse_applied").get<std::array<int, 3>>();
        }
        for (const auto& item : state.value("ac_power_settings", nlohmann::json::array())) {
            PowerSettingState entry{};
            if (!parse_guid(item.at("scheme").get<std::string>(), entry.scheme))
                throw std::runtime_error("Invalid power scheme in recovery log");
            entry.kind = item.at("kind").get<unsigned int>();
            entry.original = item.at("original").get<DWORD>();
            entry.applied = item.value("applied", entry.original);
            entry.compare_and_swap = item.value("compare_and_swap", false);
            if (!power_setting(entry.kind))
                throw std::runtime_error("Invalid power setting in recovery log");
            _power_settings.push_back(entry);
        }
        for (const auto& item : state.value("advanced_processes", nlohmann::json::array())) {
            AdvancedProcessState entry{};
            entry.pid = item.at("pid").get<DWORD>();
            entry.created_at = item.at("created_at").get<ULONGLONG>();
            entry.cpu_sets = item.value("cpu_sets", std::vector<ULONG>{});
            entry.applied_cpu_sets = item.value("applied_cpu_sets",
                                                std::vector<ULONG>{});
            entry.io_priority = item.value("io_priority", ULONG(0));
            entry.applied_io_priority = item.value("applied_io_priority", ULONG(0));
            entry.cpu_sets_touched = item.value("cpu_sets_touched", false);
            entry.io_touched = item.value("io_touched", false);
            _advanced_processes.push_back(std::move(entry));
        }
        for (const auto& item : state.value("services", nlohmann::json::array())) {
            ServiceState entry{};
            entry.name = item.at("name").get<std::string>();
            entry.original_state = item.at("original_state").get<DWORD>();
            entry.applied_state = item.at("applied_state").get<DWORD>();
            entry.touched = item.value("touched", false);
            _service_states.push_back(std::move(entry));
        }
        _network_device_instance = state.value("network_device_instance", std::string());
        _network_restart_needed = state.value("network_restart_needed", false);
        if (state.contains("dns")) {
            const auto& dns = state.at("dns");
            if (!parse_guid(dns.at("interface").get<std::string>(),
                            _dns_state.interface_id))
                throw std::runtime_error("Invalid DNS interface in recovery log");
            _dns_state.original = widen_utf8(dns.value("original", std::string()));
            _dns_state.applied = widen_utf8(dns.value("applied", std::string()));
            _dns_state.touched = dns.value("touched", false);
        }
        bool okay = restore_input_tuning();
        okay = restore_accessibility_hotkeys() && okay;
        okay = restore_advanced_processes() && okay;
        okay = restore_services() && okay;
        okay = restore_background_processes() && okay;
        okay = restore_registry() && okay;
        okay = restore_network() && okay;
        if (_session_power_plan_guid.empty())
            okay = restore_ac_power_settings() && okay;
        else
            _power_settings.clear();
        okay = restore_display() && okay;
        if (!_session_power_plan_guid.empty())
            okay = restore_session_power_plan() && okay;
        else if (!power.empty())
            okay = set_power_plan(power.c_str()) && okay;
        if (_saved_tweaks && _saved_tweaks->has_baseline())
            okay = _saved_tweaks->restore_baseline(_baseline_game_path) && okay;
        _baseline_game_path.clear();
        if (okay && DeleteFileA(_journal_path.c_str())) {
            _restoration_status = "Previous session restored";
            _restoration_succeeded = true;
        } else {
            _restoration_status = "Restore incomplete - close game, then press R";
            _recovery_failed = true;
        }
    } catch (...) {
        _registry_state.clear();
        _power_settings.clear();
        _display_state = {};
        _accessibility_state = {};
        _input_tuning_state = {};
        _restoration_status = "Session log unreadable - manual restore needed";
        _recovery_failed = true;
    }
}

bool Optimizer::snapshot_registry(HKEY root, const char* path, const char* name,
                                  DWORD applied_type, const void* applied_data,
                                  DWORD applied_size, bool& already_configured)
{
    already_configured = false;
    RegistryState state{root, path, name, 0, {}, false,
                        applied_type, {}, true};
    const auto* bytes = static_cast<const unsigned char*>(applied_data);
    state.applied_data.assign(bytes, bytes + applied_size);
    HKEY key = nullptr;
    const LONG opened = RegOpenKeyExA(root, path, 0, KEY_QUERY_VALUE, &key);
    if (opened == ERROR_SUCCESS) {
        DWORD size = 0;
        const LONG queried = RegQueryValueExA(key, name, nullptr,
                                               &state.type, nullptr, &size);
        if (queried == ERROR_SUCCESS) {
            state.data.resize(size);
            if (RegQueryValueExA(key, name, nullptr, &state.type,
                                 state.data.data(), &size) != ERROR_SUCCESS) {
                RegCloseKey(key);
                return false;
            }
            state.data.resize(size);
            state.existed = true;
        } else if (queried != ERROR_FILE_NOT_FOUND) {
            RegCloseKey(key);
            return false;
        }
        RegCloseKey(key);
    } else if (opened != ERROR_FILE_NOT_FOUND && opened != ERROR_PATH_NOT_FOUND)
        return false;
    already_configured = state.existed && state.type == applied_type &&
                         state.data == state.applied_data;
    if (already_configured)
        return true;
    _registry_state.push_back(std::move(state));
    if (save_journal())
        return true;
    _registry_state.pop_back();
    return false;
}

bool Optimizer::set_dword(HKEY root, const char* path, const char* name,
                          DWORD value, const char* label)
{
    bool already = false;
    if (!snapshot_registry(root, path, name, REG_DWORD, &value,
                           sizeof(value), already)) {
        record(false, label);
        return false;
    }
    if (already) {
        set_status(tweak_index_for_label(label), TweakStatus::AlreadyConfigured);
        return true;
    }
    bool success = write_registry(root, path, name, REG_DWORD,
                                  &value, sizeof(value));
    HKEY key = nullptr;
    DWORD verified = 0, type = 0, size = sizeof(verified);
    success = success && RegOpenKeyExA(root, path, 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS &&
        RegQueryValueExA(key, name, nullptr, &type,
                         reinterpret_cast<BYTE*>(&verified), &size) == ERROR_SUCCESS &&
        type == REG_DWORD && size == sizeof(verified) && verified == value;
    if (key) RegCloseKey(key);
    (success ? _applied : _failed).push_back(label);
    set_status(tweak_index_for_label(label), success ? TweakStatus::Configured :
                                                      TweakStatus::Failed);
    return success;
}

bool Optimizer::set_string(HKEY root, const char* path, const char* name,
                           const std::string& value, const char* label)
{
    bool already = false;
    if (!snapshot_registry(root, path, name, REG_SZ, value.c_str(),
                           static_cast<DWORD>(value.size() + 1), already)) {
        record(false, label);
        return false;
    }
    if (already) {
        set_status(tweak_index_for_label(label), TweakStatus::AlreadyConfigured);
        return true;
    }
    bool success = write_registry(root, path, name, REG_SZ, value.c_str(),
                                  static_cast<DWORD>(value.size() + 1));
    HKEY key = nullptr;
    DWORD type = 0, size = 0;
    success = success && RegOpenKeyExA(root, path, 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS &&
        RegQueryValueExA(key, name, nullptr, &type, nullptr, &size) == ERROR_SUCCESS &&
        type == REG_SZ;
    std::vector<char> verified(success ? size : 0);
    success = success && RegQueryValueExA(key, name, nullptr, &type,
        reinterpret_cast<BYTE*>(verified.data()), &size) == ERROR_SUCCESS &&
        std::string(verified.data()) == value;
    if (key) RegCloseKey(key);
    (success ? _applied : _failed).push_back(label);
    set_status(tweak_index_for_label(label), success ? TweakStatus::Configured :
                                                      TweakStatus::Failed);
    return success;
}

bool Optimizer::restore_registry()
{
    bool okay = true;
    for (auto it = _registry_state.rbegin(); it != _registry_state.rend(); ++it) {
        HKEY key = nullptr;
        const LONG opened = RegOpenKeyExA(it->root, it->path.c_str(), 0,
                                          KEY_QUERY_VALUE | KEY_SET_VALUE, &key);
        if (opened != ERROR_SUCCESS) {
            if ((opened == ERROR_FILE_NOT_FOUND || opened == ERROR_PATH_NOT_FOUND) &&
                !it->existed)
                continue;
            if (it->compare_and_swap ||
                RegCreateKeyExA(it->root, it->path.c_str(), 0, nullptr, 0,
                                KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr,
                                &key, nullptr) != ERROR_SUCCESS) {
                okay = false;
                continue;
            }
        }
        DWORD current_type = 0;
        DWORD current_size = 0;
        const LONG queried = RegQueryValueExA(key, it->name.c_str(), nullptr,
                                               &current_type, nullptr, &current_size);
        const bool current_exists = queried == ERROR_SUCCESS;
        std::vector<unsigned char> current_data(current_exists ? current_size : 0);
        if (current_exists &&
            RegQueryValueExA(key, it->name.c_str(), nullptr, &current_type,
                             current_data.data(), &current_size) == ERROR_SUCCESS)
            current_data.resize(current_size);
        else if (queried != ERROR_FILE_NOT_FOUND) {
            okay = false;
            RegCloseKey(key);
            continue;
        }
        const bool already_original = current_exists == it->existed &&
            (!it->existed || (current_type == it->type &&
                              current_data == it->data));
        if (already_original) {
            RegCloseKey(key);
            continue;
        }
        if (it->compare_and_swap &&
            (!current_exists || current_type != it->applied_type ||
             current_data != it->applied_data)) {
            okay = false;
            RegCloseKey(key);
            continue;
        }
        LONG result = ERROR_SUCCESS;
        if (it->existed) {
            result = RegSetValueExA(key, it->name.c_str(), 0, it->type,
                           it->data.empty() ? nullptr : it->data.data(),
                           static_cast<DWORD>(it->data.size()));
        } else {
            result = RegDeleteValueA(key, it->name.c_str());
            if (result == ERROR_FILE_NOT_FOUND)
                result = ERROR_SUCCESS;
        }
        if (result != ERROR_SUCCESS)
            okay = false;
        else {
            DWORD verified_type = 0;
            DWORD verified_size = 0;
            const LONG found = RegQueryValueExA(key, it->name.c_str(), nullptr,
                                                 &verified_type, nullptr,
                                                 &verified_size);
            if (it->existed) {
                std::vector<unsigned char> verified(
                    found == ERROR_SUCCESS ? verified_size : 0);
                if (found != ERROR_SUCCESS ||
                    RegQueryValueExA(key, it->name.c_str(), nullptr,
                                     &verified_type, verified.data(),
                                     &verified_size) != ERROR_SUCCESS ||
                    verified_type != it->type || verified != it->data)
                    okay = false;
            } else if (found != ERROR_FILE_NOT_FOUND)
                okay = false;
        }
        RegCloseKey(key);
    }
    _registry_state.clear();
    return okay;
}

bool Optimizer::launch_game(const std::string& game_path, bool launch_if_missing)
{
    const DWORD attributes = GetFileAttributesA(game_path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        _last_error = "Game executable not found: " + game_path;
        return false;
    }

    const std::string file_name = game_path.substr(game_path.find_last_of("\\/") + 1);
    const bool fortnite = _stricmp(file_name.c_str(),
        "FortniteClient-Win64-Shipping_EAC_EOS.exe") == 0 ||
        _stricmp(file_name.c_str(), "FortniteLauncher.exe") == 0 ||
        _stricmp(file_name.c_str(), "FortniteClient-Win64-Shipping.exe") == 0;

    _target_game_path = target_game_path(game_path);
    if (fortnite && GetFileAttributesA(_target_game_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        _last_error = "Fortnite game executable not found";
        return false;
    }

    // UserGpuPreferences is consumed when the process starts.  Writing it after
    // attachment is a verified configuration change, not proof that the running
    // game is already using that adapter preference.
    const bool game_was_running = is_game_active(_target_game_path);
    if (enabled(12)) {
        const bool configured = set_string(
            HKEY_CURRENT_USER,
            "Software\\Microsoft\\DirectX\\UserGpuPreferences",
            _target_game_path.c_str(), "GpuPreference=2;",
            "High-performance GPU preference");
        if (configured && game_was_running)
            set_status(12, TweakStatus::RestartRequired);
    }

    if (find_target_process()) {
        if (enabled(12) && !game_was_running)
            set_status(12, TweakStatus::RestartRequired);
        _applied.push_back("Attached to running game");
        return true;
    }
    if (!launch_if_missing) {
        _last_error = "Game closed before attachment";
        return false;
    }

    if (fortnite) {
        char desktop[MAX_PATH]{};
        if (SHGetFolderPathA(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr,
                             SHGFP_TYPE_CURRENT, desktop) != S_OK) {
            _last_error = "Desktop folder not found";
            return false;
        }
        const std::string shortcut = std::string(desktop) + "\\Fortnite.url";
        if (GetFileAttributesA(shortcut.c_str()) == INVALID_FILE_ATTRIBUTES) {
            _last_error = "Create a Fortnite desktop shortcut in Epic Games";
            return false;
        }
        if (reinterpret_cast<INT_PTR>(ShellExecuteA(nullptr, "open",
                shortcut.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32) {
            _last_error = "Epic Games could not launch Fortnite";
            return false;
        }
        _waiting_for_game = true;
        _launch_started = GetTickCount64();
        _applied.push_back("Launched through Epic Games");
        _applied.push_back("Anti-cheat excluded");
        return true;
    }

    STARTUPINFOA startup{};
    PROCESS_INFORMATION process{};
    startup.cb = sizeof(startup);
    std::vector<char> command(game_path.size() + 3, '\0');
    command[0] = '"';
    std::memcpy(command.data() + 1, game_path.data(), game_path.size());
    command[game_path.size() + 1] = '"';
    const std::string working_directory = executable_directory(game_path);

    const BOOL created = CreateProcessA(
        nullptr, command.data(), nullptr, nullptr, FALSE,
        CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP | NORMAL_PRIORITY_CLASS,
        nullptr, working_directory.empty() ? nullptr : working_directory.c_str(),
        &startup, &process);
    if (!created) {
        _last_error = "Game launch failed (code " +
                      std::to_string(GetLastError()) + ")";
        return false;
    }

    const bool tracked = track_process_for_restore(process.hProcess, process.dwProcessId);
    tune_game_process(process.hProcess, tracked);
    tune_advanced_game_process(process.hProcess, process.dwProcessId);

    if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
        _last_error = "Game launch failed";
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return false;
    }

    _game_process = process.hProcess;
    _game_pid = process.dwProcessId;
    _game_directory = working_directory;
    CloseHandle(process.hThread);
    _applied.push_back("Game launched");
    return true;
}

bool Optimizer::track_process_for_restore(HANDLE process, DWORD pid)
{
    BackgroundState state{};
    if (!DuplicateHandle(GetCurrentProcess(), process, GetCurrentProcess(),
                         &state.process, 0, FALSE, DUPLICATE_SAME_ACCESS))
        return false;
    state.pid = pid;
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(state.process, &created, &exited, &kernel, &user)) {
        CloseHandle(state.process);
        return false;
    }
    state.created_at = filetime_ticks(created);
    state.priority_class = GetPriorityClass(state.process);
    state.throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.throttling_known = GetProcessInformation(
        state.process, ProcessPowerThrottling, &state.throttling,
        sizeof(state.throttling)) != FALSE;
    MEMORY_PRIORITY_INFORMATION memory{};
    state.memory_known = GetProcessInformation(
        state.process, ProcessMemoryPriority, &memory, sizeof(memory)) != FALSE;
    state.memory_priority = memory.MemoryPriority;
    BOOL boost_disabled = FALSE;
    state.boost_known = GetProcessPriorityBoost(state.process, &boost_disabled) != FALSE;
    state.priority_boost_disabled = boost_disabled != FALSE;
    state.priority_touched = enabled(14) &&
        state.priority_class != HIGH_PRIORITY_CLASS;
    state.throttling_touched = enabled(13) && state.throttling_known &&
        explicit_high_qos_required(state.throttling.ControlMask,
                                   state.throttling.StateMask);
    state.memory_touched = enabled(27) && state.memory_known &&
        state.memory_priority < MEMORY_PRIORITY_NORMAL;
    state.boost_touched = enabled(26) && state.boost_known &&
        state.priority_boost_disabled;
    state.compare_and_swap = true;
    state.applied_priority_class = HIGH_PRIORITY_CLASS;
    const auto high_qos = explicit_high_qos_masks(state.throttling.ControlMask,
                                                   state.throttling.StateMask);
    state.applied_throttling_control = high_qos.control;
    state.applied_throttling_state = high_qos.state;
    state.applied_memory_priority = MEMORY_PRIORITY_NORMAL;
    state.applied_boost_disabled = false;
    if (!state.priority_class) {
        CloseHandle(state.process);
        return false;
    }
    _background_state.push_back(state);
    if (save_journal())
        return true;
    _background_state.pop_back();
    CloseHandle(state.process);
    return false;
}

void Optimizer::tune_game_process(HANDLE process, bool tracked)
{
    if (enabled(13)) {
        PROCESS_POWER_THROTTLING_STATE throttling{};
        if (tracked && !_background_state.empty()) {
            throttling = _background_state.back().throttling;
            throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
            const auto masks = explicit_high_qos_masks(throttling.ControlMask,
                                                        throttling.StateMask);
            throttling.ControlMask = masks.control;
            throttling.StateMask = masks.state;
        }
        if (tracked && !_background_state.back().throttling_known)
            set_status(13, TweakStatus::Unsupported);
        else if (tracked && !_background_state.back().throttling_touched)
            set_status(13, TweakStatus::AlreadyConfigured);
        else {
            const bool written = tracked && SetProcessInformation(
                process, ProcessPowerThrottling, &throttling,
                sizeof(throttling)) != FALSE;
            PROCESS_POWER_THROTTLING_STATE verified{};
            verified.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
            const bool okay = written && GetProcessInformation(
                process, ProcessPowerThrottling, &verified, sizeof(verified)) &&
                verified.ControlMask == throttling.ControlMask &&
                verified.StateMask == throttling.StateMask;
            record(okay, "Game power throttling off");
        }
    }
    if (enabled(14)) {
        if (tracked && !_background_state.back().priority_touched)
            set_status(14, TweakStatus::AlreadyConfigured);
        else
            record(tracked && SetPriorityClass(process, HIGH_PRIORITY_CLASS) != FALSE,
                   "Game CPU priority: high");
    }
    if (!tracked || _background_state.empty()) {
        if (enabled(26)) set_status(26, TweakStatus::Failed);
        if (enabled(27)) set_status(27, TweakStatus::Failed);
        return;
    }
    const BackgroundState& original = _background_state.back();
    if (enabled(26)) {
        if (!original.boost_known)
            set_status(26, TweakStatus::Failed);
        else if (!original.priority_boost_disabled)
            set_status(26, TweakStatus::AlreadyConfigured);
        else {
            const bool okay = SetProcessPriorityBoost(process, FALSE) != FALSE;
            set_status(26, okay ? TweakStatus::Applied : TweakStatus::Failed);
            record(okay, "Game dynamic priority boost");
        }
    }
    if (enabled(27)) {
        if (!original.memory_known)
            set_status(27, TweakStatus::Failed);
        else if (original.memory_priority >= MEMORY_PRIORITY_NORMAL)
            set_status(27, TweakStatus::AlreadyConfigured);
        else {
            MEMORY_PRIORITY_INFORMATION memory{};
            memory.MemoryPriority = MEMORY_PRIORITY_NORMAL;
            const bool okay = SetProcessInformation(process, ProcessMemoryPriority,
                                                     &memory, sizeof(memory)) != FALSE;
            set_status(27, okay ? TweakStatus::Applied : TweakStatus::Failed);
            record(okay, "Game memory priority normal");
        }
    }
}

void Optimizer::throttle_background_processes()
{
    if (!(enabled(11) || enabled(19) || enabled(20) || enabled(21) ||
          enabled(22) || enabled(29) || enabled(30) || enabled(31) ||
          enabled(32) || enabled(33) || enabled(34) || enabled(35) ||
          enabled(36)))
        return;
    // Only non-essential sync/index/update helpers are targeted. Browsers,
    // launchers, Discord/voice, audio and device software are preserved.
    struct Target { const char* name; size_t cpu_tweak; size_t memory_tweak; };
    static const Target targets[] = {
        {"OneDrive.exe", 11, 29}, {"Dropbox.exe", 11, 29},
        {"GoogleDriveFS.exe", 11, 29}, {"iCloudDrive.exe", 11, 29},
        {"SearchIndexer.exe", 19, 30}, {"SearchProtocolHost.exe", 19, 30},
        {"SearchFilterHost.exe", 19, 30},
        {"Widgets.exe", 20, 31}, {"WidgetService.exe", 20, 31},
        {"PhoneExperienceHost.exe", 21, 32},
        {"AdobeUpdateService.exe", 22, 33}, {"CCXProcess.exe", 22, 33},
        {"Creative Cloud.exe", 22, 33},
        {"MicrosoftEdgeUpdate.exe", 34, 36},
        {"GoogleUpdater.exe", 34, 36},
        {"GoogleUpdate.exe", 34, 36}
    };

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return;
    PROCESSENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (Process32First(snapshot, &entry)) {
        do {
            const Target* target = nullptr;
            for (const Target& candidate : targets) {
                if (_stricmp(entry.szExeFile, candidate.name) == 0) {
                    target = &candidate;
                    break;
                }
            }
            if (!target || (!enabled(target->cpu_tweak) &&
                            !enabled(target->memory_tweak) &&
                            !(target->cpu_tweak == 34 && enabled(35))))
                continue;

            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION |
                                         PROCESS_SET_INFORMATION,
                                         FALSE, entry.th32ProcessID);
            if (!process) {
                const TweakStatus unavailable = GetLastError() == ERROR_ACCESS_DENIED ?
                    TweakStatus::AccessDenied : TweakStatus::Failed;
                if (enabled(target->memory_tweak))
                    set_status(target->memory_tweak, unavailable);
                if (target->cpu_tweak == 34 && enabled(34))
                    set_status(34, unavailable);
                if (target->cpu_tweak == 34 && enabled(35))
                    set_status(35, unavailable);
                continue;
            }
            BackgroundState state{};
            state.process = process;
            state.pid = entry.th32ProcessID;
            FILETIME created{}, exited{}, kernel{}, user{};
            if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) {
                CloseHandle(process);
                continue;
            }
            state.created_at = filetime_ticks(created);
            const bool already_tracked = std::any_of(
                _background_state.begin(), _background_state.end(),
                [&](const BackgroundState& previous) {
                    return previous.pid == state.pid &&
                           previous.created_at == state.created_at;
                });
            if (already_tracked) {
                CloseHandle(process);
                continue;
            }
            state.priority_class = GetPriorityClass(process);
            if (!state.priority_class) {
                CloseHandle(process);
                continue;
            }
            state.throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
            state.throttling_known = GetProcessInformation(
                process, ProcessPowerThrottling, &state.throttling,
                sizeof(state.throttling)) != FALSE;
            if (target->cpu_tweak == 34 && enabled(35) &&
                !state.throttling_known)
                set_status(35, TweakStatus::Unsupported);
            MEMORY_PRIORITY_INFORMATION memory{};
            state.memory_known = GetProcessInformation(
                process, ProcessMemoryPriority, &memory, sizeof(memory)) != FALSE;
            if (enabled(target->memory_tweak) && !state.memory_known)
                set_status(target->memory_tweak, TweakStatus::Unsupported);
            state.memory_priority = memory.MemoryPriority;
            BOOL boost_disabled = FALSE;
            state.boost_known = GetProcessPriorityBoost(process, &boost_disabled) != FALSE;
            state.priority_boost_disabled = boost_disabled != FALSE;
            state.priority_touched = enabled(target->cpu_tweak) &&
                state.priority_class != BELOW_NORMAL_PRIORITY_CLASS &&
                state.priority_class != IDLE_PRIORITY_CLASS;
            state.throttling_touched = state.throttling_known &&
                target->cpu_tweak == 34 && enabled(35) &&
                (!(state.throttling.ControlMask &
                   PROCESS_POWER_THROTTLING_EXECUTION_SPEED) ||
                 !(state.throttling.StateMask &
                   PROCESS_POWER_THROTTLING_EXECUTION_SPEED));
            const DWORD desired_memory = target->memory_tweak == 36 ?
                MEMORY_PRIORITY_LOW : MEMORY_PRIORITY_BELOW_NORMAL;
            state.memory_touched = enabled(target->memory_tweak) &&
                state.memory_known && state.memory_priority > desired_memory;
            state.compare_and_swap = true;
            state.applied_priority_class = BELOW_NORMAL_PRIORITY_CLASS;
            state.applied_throttling_control = state.throttling.ControlMask |
                PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
            state.applied_throttling_state = state.throttling.StateMask |
                PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
            state.applied_memory_priority = desired_memory;
            state.applied_boost_disabled = state.priority_boost_disabled;

            if (!state.priority_touched && !state.throttling_touched &&
                !state.memory_touched) {
                CloseHandle(process);
                continue;
            }

            _background_state.push_back(state);
            if (!save_journal()) {
                _background_state.pop_back();
                CloseHandle(process);
                _failed.push_back(std::string("Could not track: ") + entry.szExeFile);
                continue;
            }

            bool changed = false;
            if (enabled(target->cpu_tweak)) {
                const bool needs_cpu = state.priority_class != BELOW_NORMAL_PRIORITY_CLASS &&
                                       state.priority_class != IDLE_PRIORITY_CLASS;
                const bool cpu_ok = !needs_cpu ||
                    SetPriorityClass(process, BELOW_NORMAL_PRIORITY_CLASS) != FALSE;
                if (target->cpu_tweak == 34)
                    set_status(34, !cpu_ok ? TweakStatus::Failed :
                               needs_cpu ? TweakStatus::Applied : TweakStatus::Skipped);
                else
                    set_status(target->cpu_tweak, !cpu_ok ? TweakStatus::Failed :
                               needs_cpu ? TweakStatus::Applied : TweakStatus::Skipped);
                changed = changed || (needs_cpu && cpu_ok);
            }
            if (target->cpu_tweak == 34 && enabled(35)) {
                const bool needs_eco = state.throttling_known &&
                    (!(state.throttling.ControlMask &
                       PROCESS_POWER_THROTTLING_EXECUTION_SPEED) ||
                     !(state.throttling.StateMask &
                       PROCESS_POWER_THROTTLING_EXECUTION_SPEED));
                PROCESS_POWER_THROTTLING_STATE eco = state.throttling;
                eco.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
                const auto masks = explicit_eco_qos_masks(eco.ControlMask,
                                                           eco.StateMask);
                eco.ControlMask = masks.control;
                eco.StateMask = masks.state;
                const bool eco_ok = state.throttling_known &&
                    (!needs_eco || (state.throttling_touched &&
                     SetProcessInformation(process, ProcessPowerThrottling,
                                           &eco, sizeof(eco)) != FALSE));
                set_status(35, !eco_ok ? TweakStatus::Failed :
                           needs_eco ? TweakStatus::Applied : TweakStatus::Skipped);
                changed = changed || (needs_eco && eco_ok);
            }
            if (enabled(target->memory_tweak)) {
                const DWORD desired = target->memory_tweak == 36 ?
                    MEMORY_PRIORITY_LOW : MEMORY_PRIORITY_BELOW_NORMAL;
                const bool needs_memory = state.memory_known &&
                    state.memory_priority > desired;
                MEMORY_PRIORITY_INFORMATION target_memory{};
                target_memory.MemoryPriority = desired;
                const bool memory_ok = state.memory_known &&
                    (!needs_memory || SetProcessInformation(process, ProcessMemoryPriority,
                                                             &target_memory,
                                                             sizeof(target_memory)) != FALSE);
                set_status(target->memory_tweak, !memory_ok ? TweakStatus::Failed :
                           needs_memory ? TweakStatus::Applied : TweakStatus::Skipped);
                changed = changed || (needs_memory && memory_ok);
            }
            if (changed) {
                _applied.push_back(std::string("Background load reduced: ") + entry.szExeFile);
            } else {
                _background_state.pop_back();
                save_journal();
                CloseHandle(process);
            }
        } while (Process32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
}

bool Optimizer::restore_background_processes()
{
    bool okay = true;
    for (auto item = _background_state.rbegin();
         item != _background_state.rend(); ++item) {
        const BackgroundState& state = *item;
        if (!state.priority_touched && !state.throttling_touched &&
            !state.memory_touched && !state.boost_touched) {
            if (state.process)
                CloseHandle(state.process);
            continue;
        }
        HANDLE process = state.process;
        if (!process) {
            process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION |
                                  PROCESS_SET_INFORMATION | SYNCHRONIZE,
                                  FALSE, state.pid);
            if (!process) {
                if (GetLastError() != ERROR_INVALID_PARAMETER)
                    okay = false;
                continue;
            }
        }
        FILETIME created{}, exited{}, kernel{}, user{};
        DWORD exit_code = 0;
        const bool readable = GetProcessTimes(process, &created, &exited,
                                              &kernel, &user) &&
                              GetExitCodeProcess(process, &exit_code);
        if (!readable)
            okay = false;
        if (readable && filetime_ticks(created) == state.created_at &&
            exit_code == STILL_ACTIVE) {
            const DWORD current_priority = GetPriorityClass(process);
            if (state.priority_touched && state.priority_class) {
                if (!current_priority)
                    okay = false;
                else if (current_priority != state.priority_class) {
                    if (state.compare_and_swap &&
                        current_priority != state.applied_priority_class)
                        okay = false;
                    else if (!SetPriorityClass(process, state.priority_class) ||
                             GetPriorityClass(process) != state.priority_class)
                        okay = false;
                }
            }
            if (state.throttling_touched && state.throttling_known) {
                PROCESS_POWER_THROTTLING_STATE original = state.throttling;
                PROCESS_POWER_THROTTLING_STATE current{};
                current.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
                if (!GetProcessInformation(process, ProcessPowerThrottling,
                                           &current, sizeof(current)))
                    okay = false;
                else if (current.ControlMask != original.ControlMask ||
                         current.StateMask != original.StateMask) {
                    if (state.compare_and_swap &&
                        (current.ControlMask != state.applied_throttling_control ||
                         current.StateMask != state.applied_throttling_state))
                        okay = false;
                    else if (!SetProcessInformation(process, ProcessPowerThrottling,
                                                    &original, sizeof(original)))
                        okay = false;
                }
            }
            if (state.memory_touched && state.memory_known) {
                MEMORY_PRIORITY_INFORMATION current{};
                if (!GetProcessInformation(process, ProcessMemoryPriority,
                                           &current, sizeof(current)))
                    okay = false;
                else if (current.MemoryPriority != state.memory_priority) {
                    MEMORY_PRIORITY_INFORMATION original{};
                    original.MemoryPriority = state.memory_priority;
                    if (state.compare_and_swap &&
                        current.MemoryPriority != state.applied_memory_priority)
                        okay = false;
                    else {
                        MEMORY_PRIORITY_INFORMATION verified{};
                        if (!SetProcessInformation(process, ProcessMemoryPriority,
                                                   &original, sizeof(original)) ||
                            !GetProcessInformation(process, ProcessMemoryPriority,
                                                   &verified, sizeof(verified)) ||
                            verified.MemoryPriority != state.memory_priority)
                            okay = false;
                    }
                }
            }
            if (state.boost_touched && state.boost_known) {
                BOOL disabled = FALSE;
                if (!GetProcessPriorityBoost(process, &disabled))
                    okay = false;
                else if ((disabled != FALSE) != state.priority_boost_disabled) {
                    if (state.compare_and_swap &&
                        (disabled != FALSE) != state.applied_boost_disabled)
                        okay = false;
                    else {
                        BOOL verified = FALSE;
                        if (!SetProcessPriorityBoost(
                                 process, state.priority_boost_disabled ? TRUE : FALSE) ||
                            !GetProcessPriorityBoost(process, &verified) ||
                            (verified != FALSE) != state.priority_boost_disabled)
                            okay = false;
                    }
                }
            }
        }
        CloseHandle(process);
    }
    _background_state.clear();
    return okay;
}

bool Optimizer::set_ac_power_setting(unsigned int kind, DWORD value,
                                     size_t tweak, const char* label)
{
    SYSTEM_POWER_STATUS status{};
    if (!GetSystemPowerStatus(&status) || status.ACLineStatus != 1) {
        set_status(tweak, TweakStatus::Skipped);
        return false;
    }
    GUID* active = nullptr;
    if (PowerGetActiveScheme(nullptr, &active) != ERROR_SUCCESS || !active) {
        set_status(tweak, TweakStatus::Failed);
        record(false, label);
        return false;
    }
    const GUID scheme = *active;
    LocalFree(active);
    GUID session{};
    if (!parse_guid(_session_power_plan_guid, session) ||
        !IsEqualGUID(scheme, session)) {
        set_status(tweak, TweakStatus::Skipped);
        return false;
    }
    const GUID* subgroup = power_subgroup(kind);
    const GUID* setting = power_setting(kind);
    DWORD original = 0;
    const DWORD read_result = subgroup && setting ?
        PowerReadACValueIndex(nullptr, &scheme, subgroup, setting, &original) :
        ERROR_INVALID_PARAMETER;
    if (read_result != ERROR_SUCCESS) {
        if (read_result == ERROR_FILE_NOT_FOUND ||
            read_result == ERROR_INVALID_PARAMETER)
            set_status(tweak, TweakStatus::Unsupported);
        else {
            set_status(tweak, read_result == ERROR_ACCESS_DENIED ?
                       TweakStatus::AccessDenied : TweakStatus::Failed);
            record(false, label);
        }
        return false;
    }
    if (original == value || (kind == 2 && original != 0)) {
        set_status(tweak, TweakStatus::AlreadyConfigured);
        return true;
    }
    _power_settings.push_back({scheme, kind, original, value, true});
    if (!save_journal()) {
        _power_settings.pop_back();
        set_status(tweak, TweakStatus::Failed);
        record(false, label);
        return false;
    }
    DWORD verified = 0;
    const bool changed = PowerWriteACValueIndex(nullptr, &scheme, subgroup,
                                                 setting, value) == ERROR_SUCCESS &&
                         PowerSetActiveScheme(nullptr, &scheme) == ERROR_SUCCESS &&
                         PowerReadACValueIndex(nullptr, &scheme, subgroup,
                                               setting, &verified) == ERROR_SUCCESS &&
                         verified == value;
    set_status(tweak, changed ? TweakStatus::Applied : TweakStatus::Failed);
    record(changed, label);
    return changed;
}

bool Optimizer::restore_ac_power_settings()
{
    bool okay = true;
    for (auto item = _power_settings.rbegin(); item != _power_settings.rend(); ++item) {
        const GUID* subgroup = power_subgroup(item->kind);
        const GUID* setting = power_setting(item->kind);
        DWORD current = 0;
        if (!subgroup || !setting ||
            PowerReadACValueIndex(nullptr, &item->scheme, subgroup, setting,
                                  &current) != ERROR_SUCCESS) {
            okay = false;
            continue;
        }
        if (current == item->original)
            continue;
        if ((item->compare_and_swap && current != item->applied) ||
            PowerWriteACValueIndex(nullptr, &item->scheme, subgroup, setting,
                                   item->original) != ERROR_SUCCESS) {
            okay = false;
            continue;
        }
        GUID* active = nullptr;
        if (PowerGetActiveScheme(nullptr, &active) != ERROR_SUCCESS || !active) {
            okay = false;
            continue;
        }
        const bool scheme_active = IsEqualGUID(*active, item->scheme);
        LocalFree(active);
        DWORD verified = 0;
        if ((scheme_active &&
             PowerSetActiveScheme(nullptr, &item->scheme) != ERROR_SUCCESS) ||
            PowerReadACValueIndex(nullptr, &item->scheme, subgroup, setting,
                                  &verified) != ERROR_SUCCESS ||
            verified != item->original)
            okay = false;
    }
    _power_settings.clear();
    return okay;
}

bool Optimizer::begin_session_power_plan()
{
    GUID original{};
    if (!parse_guid(_saved_power_plan_guid, original))
        return false;
    GUID copy{};
    if (CoCreateGuid(&copy) != S_OK)
        return false;
    _session_power_plan_guid = guid_text(copy);
    if (_session_power_plan_guid.empty() || !save_journal()) {
        _session_power_plan_guid.clear();
        return false;
    }
    GUID* destination = &copy;
    const DWORD duplicated = PowerDuplicateScheme(nullptr, &original, &destination);
    if (duplicated != ERROR_SUCCESS) {
        if (duplicated == ERROR_ALREADY_EXISTS) {
            _session_power_plan_guid.clear();
            if (!save_journal()) {
                _recovery_failed = true;
                _restoration_status = "Power plan journal conflict";
            }
            return false;
        }
        if (!restore_session_power_plan()) {
            _recovery_failed = true;
            _restoration_status = "Temporary power plan cleanup incomplete";
        } else if (!save_journal()) {
            _recovery_failed = true;
            _restoration_status = "Temporary power plan journal incomplete";
        }
        return false;
    }
    if (PowerSetActiveScheme(nullptr, &copy) != ERROR_SUCCESS) {
        if (!restore_session_power_plan()) {
            _recovery_failed = true;
            _restoration_status = "Temporary power plan cleanup incomplete";
        }
        return false;
    }
    if (enabled(0)) {
        set_status(0, TweakStatus::Applied);
        _applied.push_back("Temporary session power plan");
    }
    return true;
}

bool Optimizer::restore_session_power_plan()
{
    if (_session_power_plan_guid.empty())
        return true;
    GUID copy{}, original{};
    if (!parse_guid(_session_power_plan_guid, copy) ||
        !parse_guid(_saved_power_plan_guid, original))
        return false;
    GUID* active = nullptr;
    if (PowerGetActiveScheme(nullptr, &active) != ERROR_SUCCESS || !active)
        return false;
    const bool ours_active = IsEqualGUID(*active, copy);
    LocalFree(active);
    if (ours_active && PowerSetActiveScheme(nullptr, &original) != ERROR_SUCCESS)
        return false;
    // If another application changed the active plan, preserve its choice.
    const DWORD deleted = PowerDeleteScheme(nullptr, &copy);
    if (deleted != ERROR_SUCCESS && deleted != ERROR_FILE_NOT_FOUND)
        return false;
    _session_power_plan_guid.clear();
    return true;
}

void Optimizer::optimize_accessibility_hotkeys()
{
    AccessibilityState state{};
    state.filter.cbSize = sizeof(FILTERKEYS);
    state.sticky.cbSize = sizeof(STICKYKEYS);
    state.toggle.cbSize = sizeof(TOGGLEKEYS);
    if (!SystemParametersInfoW(SPI_GETFILTERKEYS, sizeof(FILTERKEYS),
                               &state.filter, 0) ||
        !SystemParametersInfoW(SPI_GETSTICKYKEYS, sizeof(STICKYKEYS),
                               &state.sticky, 0) ||
        !SystemParametersInfoW(SPI_GETTOGGLEKEYS, sizeof(TOGGLEKEYS),
                               &state.toggle, 0)) {
        set_status(40, TweakStatus::Failed);
        return;
    }
    state.filter_applied_flags = state.filter.dwFlags;
    state.sticky_applied_flags = state.sticky.dwFlags;
    state.toggle_applied_flags = state.toggle.dwFlags;
    if (!(state.filter.dwFlags & FKF_FILTERKEYSON))
        state.filter_applied_flags &= ~(FKF_HOTKEYACTIVE | FKF_CONFIRMHOTKEY);
    if (!(state.sticky.dwFlags & SKF_STICKYKEYSON))
        state.sticky_applied_flags &= ~(SKF_HOTKEYACTIVE | SKF_CONFIRMHOTKEY);
    if (!(state.toggle.dwFlags & TKF_TOGGLEKEYSON))
        state.toggle_applied_flags &= ~(TKF_HOTKEYACTIVE | TKF_CONFIRMHOTKEY);
    state.filter_touched = state.filter_applied_flags != state.filter.dwFlags;
    state.sticky_touched = state.sticky_applied_flags != state.sticky.dwFlags;
    state.toggle_touched = state.toggle_applied_flags != state.toggle.dwFlags;
    if (!state.filter_touched && !state.sticky_touched && !state.toggle_touched) {
        set_status(40, TweakStatus::AlreadyConfigured);
        return;
    }
    _accessibility_state = state;
    if (!save_journal()) {
        _accessibility_state = {};
        set_status(40, TweakStatus::Failed);
        return;
    }
    bool okay = true;
    if (state.filter_touched) {
        FILTERKEYS desired = state.filter;
        desired.dwFlags = state.filter_applied_flags;
        FILTERKEYS verified{};
        verified.cbSize = sizeof(FILTERKEYS);
        okay = SystemParametersInfoW(SPI_SETFILTERKEYS, sizeof(FILTERKEYS),
                                     &desired, 0) &&
               SystemParametersInfoW(SPI_GETFILTERKEYS, sizeof(FILTERKEYS),
                                     &verified, 0) &&
               verified.dwFlags == desired.dwFlags && okay;
    }
    if (state.sticky_touched) {
        STICKYKEYS desired = state.sticky;
        desired.dwFlags = state.sticky_applied_flags;
        STICKYKEYS verified{};
        verified.cbSize = sizeof(STICKYKEYS);
        okay = SystemParametersInfoW(SPI_SETSTICKYKEYS, sizeof(STICKYKEYS),
                                     &desired, 0) &&
               SystemParametersInfoW(SPI_GETSTICKYKEYS, sizeof(STICKYKEYS),
                                     &verified, 0) &&
               verified.dwFlags == desired.dwFlags && okay;
    }
    if (state.toggle_touched) {
        TOGGLEKEYS desired = state.toggle;
        desired.dwFlags = state.toggle_applied_flags;
        TOGGLEKEYS verified{};
        verified.cbSize = sizeof(TOGGLEKEYS);
        okay = SystemParametersInfoW(SPI_SETTOGGLEKEYS, sizeof(TOGGLEKEYS),
                                     &desired, 0) &&
               SystemParametersInfoW(SPI_GETTOGGLEKEYS, sizeof(TOGGLEKEYS),
                                     &verified, 0) &&
               verified.dwFlags == desired.dwFlags && okay;
    }
    set_status(40, okay ? TweakStatus::Applied : TweakStatus::Failed);
    if (okay)
        _applied.push_back("Accessibility hotkeys off");
}

bool Optimizer::restore_accessibility_hotkeys()
{
    AccessibilityState& state = _accessibility_state;
    bool okay = true;
    if (state.toggle_touched) {
        TOGGLEKEYS current{};
        current.cbSize = sizeof(TOGGLEKEYS);
        if (!SystemParametersInfoW(SPI_GETTOGGLEKEYS, sizeof(TOGGLEKEYS),
                                   &current, 0))
            okay = false;
        else if (current.dwFlags != state.toggle.dwFlags) {
            TOGGLEKEYS verified{};
            verified.cbSize = sizeof(TOGGLEKEYS);
            if (current.dwFlags != state.toggle_applied_flags ||
                !SystemParametersInfoW(SPI_SETTOGGLEKEYS, sizeof(TOGGLEKEYS),
                                       &state.toggle, 0) ||
                !SystemParametersInfoW(SPI_GETTOGGLEKEYS, sizeof(TOGGLEKEYS),
                                       &verified, 0) ||
                verified.dwFlags != state.toggle.dwFlags)
                okay = false;
        }
    }
    if (state.sticky_touched) {
        STICKYKEYS current{};
        current.cbSize = sizeof(STICKYKEYS);
        if (!SystemParametersInfoW(SPI_GETSTICKYKEYS, sizeof(STICKYKEYS),
                                   &current, 0))
            okay = false;
        else if (current.dwFlags != state.sticky.dwFlags) {
            STICKYKEYS verified{};
            verified.cbSize = sizeof(STICKYKEYS);
            if (current.dwFlags != state.sticky_applied_flags ||
                !SystemParametersInfoW(SPI_SETSTICKYKEYS, sizeof(STICKYKEYS),
                                       &state.sticky, 0) ||
                !SystemParametersInfoW(SPI_GETSTICKYKEYS, sizeof(STICKYKEYS),
                                       &verified, 0) ||
                verified.dwFlags != state.sticky.dwFlags)
                okay = false;
        }
    }
    if (state.filter_touched) {
        FILTERKEYS current{};
        current.cbSize = sizeof(FILTERKEYS);
        if (!SystemParametersInfoW(SPI_GETFILTERKEYS, sizeof(FILTERKEYS),
                                   &current, 0))
            okay = false;
        else if (current.dwFlags != state.filter.dwFlags ||
                 current.iWaitMSec != state.filter.iWaitMSec ||
                 current.iDelayMSec != state.filter.iDelayMSec ||
                 current.iRepeatMSec != state.filter.iRepeatMSec ||
                 current.iBounceMSec != state.filter.iBounceMSec) {
            FILTERKEYS verified{};
            verified.cbSize = sizeof(FILTERKEYS);
            if (current.dwFlags != state.filter_applied_flags ||
                current.iWaitMSec != state.filter.iWaitMSec ||
                current.iDelayMSec != state.filter.iDelayMSec ||
                current.iRepeatMSec != state.filter.iRepeatMSec ||
                current.iBounceMSec != state.filter.iBounceMSec ||
                !SystemParametersInfoW(SPI_SETFILTERKEYS, sizeof(FILTERKEYS),
                                       &state.filter, 0) ||
                !SystemParametersInfoW(SPI_GETFILTERKEYS, sizeof(FILTERKEYS),
                                       &verified, 0) ||
                verified.dwFlags != state.filter.dwFlags ||
                verified.iWaitMSec != state.filter.iWaitMSec ||
                verified.iDelayMSec != state.filter.iDelayMSec ||
                verified.iRepeatMSec != state.filter.iRepeatMSec ||
                verified.iBounceMSec != state.filter.iBounceMSec)
                okay = false;
        }
    }
    if (okay)
        state = {};
    return okay;
}

namespace {
bool same_filter(const FILTERKEYS& left, const FILTERKEYS& right)
{
    return left.dwFlags == right.dwFlags &&
           left.iWaitMSec == right.iWaitMSec &&
           left.iDelayMSec == right.iDelayMSec &&
           left.iRepeatMSec == right.iRepeatMSec &&
           left.iBounceMSec == right.iBounceMSec;
}
}

void Optimizer::optimize_input_tuning()
{
    if (enabled(41)) {
        FILTERKEYS original{};
        original.cbSize = sizeof(original);
        if (!SystemParametersInfoW(SPI_GETFILTERKEYS, sizeof(original),
                                   &original, 0)) {
            set_status(41, TweakStatus::Failed);
        } else {
            FILTERKEYS desired = original;
            desired.dwFlags = (original.dwFlags | FKF_FILTERKEYSON) &
                              ~(FKF_HOTKEYACTIVE | FKF_CONFIRMHOTKEY);
            desired.iWaitMSec = 1;
            desired.iDelayMSec = 100;
            desired.iRepeatMSec = 20;
            desired.iBounceMSec = 0;
            if (same_filter(original, desired)) {
                set_status(41, TweakStatus::AlreadyConfigured);
            } else if (original.dwFlags & FKF_FILTERKEYSON) {
                set_status(41, TweakStatus::Skipped);
            } else {
                _input_tuning_state.filter = original;
                _input_tuning_state.filter_applied = desired;
                _input_tuning_state.filter_touched = true;
                if (!save_journal()) {
                    _input_tuning_state.filter_touched = false;
                    set_status(41, TweakStatus::Failed);
                } else {
                    FILTERKEYS verified{};
                    verified.cbSize = sizeof(verified);
                    const bool okay = SystemParametersInfoW(
                        SPI_SETFILTERKEYS, sizeof(desired), &desired, 0) &&
                        SystemParametersInfoW(
                            SPI_GETFILTERKEYS, sizeof(verified), &verified, 0) &&
                        same_filter(verified, desired);
                    set_status(41, okay ? TweakStatus::Applied :
                                         TweakStatus::Failed);
                    if (okay) _applied.push_back("FilterKeys fast repeat");
                }
            }
        }
    }

    if (enabled(42)) {
        UINT original = 0;
        if (!SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &original, 0)) {
            set_status(42, TweakStatus::Failed);
        } else if (original == 0) {
            set_status(42, TweakStatus::AlreadyConfigured);
        } else {
            _input_tuning_state.delay = original;
            _input_tuning_state.delay_applied = 0;
            _input_tuning_state.delay_touched = true;
            if (!save_journal()) {
                _input_tuning_state.delay_touched = false;
                set_status(42, TweakStatus::Failed);
            } else {
                UINT verified = 0;
                const bool okay = SystemParametersInfoW(
                    SPI_SETKEYBOARDDELAY, 0, nullptr, 0) &&
                    SystemParametersInfoW(
                        SPI_GETKEYBOARDDELAY, 0, &verified, 0) && verified == 0;
                set_status(42, okay ? TweakStatus::Applied : TweakStatus::Failed);
                if (okay) _applied.push_back("Keyboard repeat delay shortest");
            }
        }
    }

    if (enabled(43)) {
        UINT original = 0;
        if (!SystemParametersInfoW(SPI_GETKEYBOARDSPEED, 0, &original, 0)) {
            set_status(43, TweakStatus::Failed);
        } else if (original == 31) {
            set_status(43, TweakStatus::AlreadyConfigured);
        } else {
            _input_tuning_state.speed = original;
            _input_tuning_state.speed_applied = 31;
            _input_tuning_state.speed_touched = true;
            if (!save_journal()) {
                _input_tuning_state.speed_touched = false;
                set_status(43, TweakStatus::Failed);
            } else {
                UINT verified = 0;
                const bool okay = SystemParametersInfoW(
                    SPI_SETKEYBOARDSPEED, 31, nullptr, 0) &&
                    SystemParametersInfoW(
                        SPI_GETKEYBOARDSPEED, 0, &verified, 0) && verified == 31;
                set_status(43, okay ? TweakStatus::Applied : TweakStatus::Failed);
                if (okay) _applied.push_back("Keyboard repeat speed fastest");
            }
        }
    }
}

bool Optimizer::restore_input_tuning()
{
    InputTuningState& state = _input_tuning_state;
    bool okay = true;
    if (state.mouse_touched) {
        std::array<int, 3> current{};
        if (!SystemParametersInfoW(SPI_GETMOUSE, 0, current.data(), 0))
            okay = false;
        else if (current != state.mouse) {
            std::array<int, 3> verified{};
            if (current != state.mouse_applied ||
                !SystemParametersInfoW(SPI_SETMOUSE, 0, state.mouse.data(), 0) ||
                !SystemParametersInfoW(SPI_GETMOUSE, 0, verified.data(), 0) ||
                verified != state.mouse)
                okay = false;
        }
    }
    if (state.speed_touched) {
        UINT current = 0;
        if (!SystemParametersInfoW(SPI_GETKEYBOARDSPEED, 0, &current, 0))
            okay = false;
        else if (current != state.speed) {
            UINT verified = 0;
            if (current != state.speed_applied ||
                !SystemParametersInfoW(SPI_SETKEYBOARDSPEED, state.speed,
                                       nullptr, 0) ||
                !SystemParametersInfoW(SPI_GETKEYBOARDSPEED, 0, &verified, 0) ||
                verified != state.speed)
                okay = false;
        }
    }
    if (state.delay_touched) {
        UINT current = 0;
        if (!SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &current, 0))
            okay = false;
        else if (current != state.delay) {
            UINT verified = 0;
            if (current != state.delay_applied ||
                !SystemParametersInfoW(SPI_SETKEYBOARDDELAY, state.delay,
                                       nullptr, 0) ||
                !SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &verified, 0) ||
                verified != state.delay)
                okay = false;
        }
    }
    if (state.filter_touched) {
        FILTERKEYS current{};
        current.cbSize = sizeof(current);
        if (!SystemParametersInfoW(SPI_GETFILTERKEYS, sizeof(current),
                                   &current, 0))
            okay = false;
        else if (!same_filter(current, state.filter)) {
            FILTERKEYS verified{};
            verified.cbSize = sizeof(verified);
            if (!same_filter(current, state.filter_applied) ||
                !SystemParametersInfoW(SPI_SETFILTERKEYS, sizeof(state.filter),
                                       &state.filter, 0) ||
                !SystemParametersInfoW(SPI_GETFILTERKEYS, sizeof(verified),
                                       &verified, 0) ||
                !same_filter(verified, state.filter))
                okay = false;
        }
    }
    if (okay) state = {};
    return okay;
}

namespace {
using NtQueryInformationProcessFn = LONG (NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
using NtSetInformationProcessFn = LONG (NTAPI*)(HANDLE, ULONG, PVOID, ULONG);
using NtSetTimerResolutionFn = LONG (NTAPI*)(ULONG, BOOLEAN, PULONG);
constexpr ULONG PROCESS_IO_PRIORITY_CLASS = 33;

NtQueryInformationProcessFn nt_query_process()
{
    static NtQueryInformationProcessFn fn = [] {
        FARPROC address = GetProcAddress(GetModuleHandleA("ntdll.dll"),
                                         "NtQueryInformationProcess");
        NtQueryInformationProcessFn value = nullptr;
        static_assert(sizeof(value) == sizeof(address));
        std::memcpy(&value, &address, sizeof(value));
        return value;
    }();
    return fn;
}

NtSetInformationProcessFn nt_set_process()
{
    static NtSetInformationProcessFn fn = [] {
        FARPROC address = GetProcAddress(GetModuleHandleA("ntdll.dll"),
                                         "NtSetInformationProcess");
        NtSetInformationProcessFn value = nullptr;
        static_assert(sizeof(value) == sizeof(address));
        std::memcpy(&value, &address, sizeof(value));
        return value;
    }();
    return fn;
}

bool query_io_priority(HANDLE process, ULONG& value)
{
    auto query = nt_query_process();
    return query && query(process, PROCESS_IO_PRIORITY_CLASS, &value,
                          sizeof(value), nullptr) >= 0;
}

bool set_io_priority(HANDLE process, ULONG value)
{
    auto set = nt_set_process();
    return set && set(process, PROCESS_IO_PRIORITY_CLASS, &value,
                      sizeof(value)) >= 0;
}

bool process_identity(HANDLE process, ULONGLONG& created_at)
{
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user))
        return false;
    created_at = filetime_ticks(created);
    return true;
}

bool wait_service_state(SC_HANDLE service, DWORD desired, DWORD timeout_ms)
{
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    do {
        SERVICE_STATUS_PROCESS status{};
        DWORD needed = 0;
        if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                                  reinterpret_cast<BYTE*>(&status), sizeof(status),
                                  &needed))
            return false;
        if (status.dwCurrentState == desired)
            return true;
        Sleep(50);
    } while (GetTickCount64() < deadline);
    return false;
}
}

void Optimizer::request_timer_resolution()
{
    if (!enabled(57))
        return;
    FARPROC address = GetProcAddress(GetModuleHandleA("ntdll.dll"),
                                     "NtSetTimerResolution");
    NtSetTimerResolutionFn set = nullptr;
    static_assert(sizeof(set) == sizeof(address));
    std::memcpy(&set, &address, sizeof(set));
    ULONG actual = 0;
    _timer_resolution_active = set && set(5000, TRUE, &actual) >= 0;
    set_status(57, _timer_resolution_active ? TweakStatus::Applied :
                                             TweakStatus::Unsupported);
    if (_timer_resolution_active)
        _applied.push_back("Global timer resolution: 0.5 ms");
}

void Optimizer::release_timer_resolution()
{
    if (!_timer_resolution_active)
        return;
    FARPROC address = GetProcAddress(GetModuleHandleA("ntdll.dll"),
                                     "NtSetTimerResolution");
    NtSetTimerResolutionFn set = nullptr;
    static_assert(sizeof(set) == sizeof(address));
    std::memcpy(&set, &address, sizeof(set));
    ULONG actual = 0;
    if (set)
        set(5000, FALSE, &actual);
    _timer_resolution_active = false;
}

bool Optimizer::pause_service(const char* name, size_t tweak)
{
    SC_HANDLE manager = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) {
        set_status(tweak, GetLastError() == ERROR_ACCESS_DENIED ?
                   TweakStatus::AccessDenied : TweakStatus::Failed);
        return false;
    }
    SC_HANDLE service = OpenServiceA(manager, name, SERVICE_QUERY_STATUS |
                                     SERVICE_PAUSE_CONTINUE | SERVICE_STOP |
                                     SERVICE_START);
    if (!service) {
        const DWORD error = GetLastError();
        CloseServiceHandle(manager);
        set_status(tweak, error == ERROR_SERVICE_DOES_NOT_EXIST ?
                   TweakStatus::Unsupported : error == ERROR_ACCESS_DENIED ?
                   TweakStatus::AccessDenied : TweakStatus::Failed);
        return false;
    }
    SERVICE_STATUS_PROCESS status{};
    DWORD needed = 0;
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                              reinterpret_cast<BYTE*>(&status), sizeof(status),
                              &needed)) {
        CloseServiceHandle(service); CloseServiceHandle(manager);
        set_status(tweak, TweakStatus::Failed);
        return false;
    }
    if (status.dwCurrentState == SERVICE_STOPPED ||
        status.dwCurrentState == SERVICE_PAUSED) {
        CloseServiceHandle(service); CloseServiceHandle(manager);
        set_status(tweak, TweakStatus::AlreadyConfigured);
        return true;
    }
    ServiceState saved{name, status.dwCurrentState, SERVICE_STOPPED, true};
    _service_states.push_back(saved);
    if (!save_journal()) {
        _service_states.pop_back();
        CloseServiceHandle(service); CloseServiceHandle(manager);
        set_status(tweak, TweakStatus::Failed);
        return false;
    }
    SERVICE_STATUS result{};
    DWORD target = SERVICE_STOPPED;
    bool changed = false;
    if ((status.dwControlsAccepted & SERVICE_ACCEPT_PAUSE_CONTINUE) &&
        ControlService(service, SERVICE_CONTROL_PAUSE, &result)) {
        target = SERVICE_PAUSED;
        changed = wait_service_state(service, target, 5000);
    } else if ((status.dwControlsAccepted & SERVICE_ACCEPT_STOP) &&
               ControlService(service, SERVICE_CONTROL_STOP, &result)) {
        target = SERVICE_STOPPED;
        changed = wait_service_state(service, target, 10000);
    }
    _service_states.back().applied_state = target;
    save_journal();
    CloseServiceHandle(service); CloseServiceHandle(manager);
    set_status(tweak, changed ? TweakStatus::Applied : TweakStatus::Failed);
    if (changed)
        _applied.push_back(std::string("Paused service: ") + name);
    return changed;
}

void Optimizer::optimize_advanced_session()
{
    request_timer_resolution();
    if (enabled(58))
        pause_service("WSearch", 58);
    if (enabled(59)) {
        const bool bits = pause_service("BITS", 59);
        const bool delivery = pause_service("DoSvc", 59);
        if (bits || delivery)
            set_status(59, TweakStatus::Applied);
    }
}

void Optimizer::tune_advanced_game_process(HANDLE process, DWORD pid)
{
    if (!process || (!enabled(54) && !enabled(55)))
        return;
    AdvancedProcessState state{};
    state.pid = pid;
    if (!process_identity(process, state.created_at)) {
        if (enabled(54)) set_status(54, TweakStatus::Failed);
        if (enabled(55)) set_status(55, TweakStatus::Failed);
        return;
    }
    if (enabled(54)) {
        ULONG count = 0;
        GetProcessDefaultCpuSets(process, nullptr, 0, &count);
        state.cpu_sets.resize(count);
        if (count && !GetProcessDefaultCpuSets(process, state.cpu_sets.data(),
                                               count, &count)) {
            state.cpu_sets.clear();
            set_status(54, TweakStatus::Failed);
        } else {
            ULONG bytes = 0;
            GetSystemCpuSetInformation(nullptr, 0, &bytes, process, 0);
            std::vector<unsigned char> data(bytes);
            bool okay = bytes && GetSystemCpuSetInformation(
                reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(data.data()),
                bytes, &bytes, process, 0);
            BYTE best = 255, worst = 0;
            std::vector<ULONG> performance;
            for (ULONG offset = 0; okay && offset < bytes;) {
                auto* info = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(
                    data.data() + offset);
                if (!info->Size) break;
                if (info->Type == CpuSetInformation) {
                    best = std::min(best, info->CpuSet.EfficiencyClass);
                    worst = std::max(worst, info->CpuSet.EfficiencyClass);
                }
                offset += info->Size;
            }
            for (ULONG offset = 0; okay && offset < bytes;) {
                auto* info = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(
                    data.data() + offset);
                if (!info->Size) break;
                if (info->Type == CpuSetInformation &&
                    info->CpuSet.EfficiencyClass == best &&
                    !info->CpuSet.Parked &&
                    (!info->CpuSet.Allocated || info->CpuSet.AllocatedToTargetProcess))
                    performance.push_back(info->CpuSet.Id);
                offset += info->Size;
            }
            if (!okay || best == worst || performance.empty()) {
                set_status(54, TweakStatus::Unsupported);
            } else {
                state.cpu_sets_touched = true;
                state.applied_cpu_sets = performance;
                _advanced_processes.push_back(state);
                if (!save_journal()) {
                    _advanced_processes.pop_back();
                    state.cpu_sets_touched = false;
                    set_status(54, TweakStatus::Failed);
                } else {
                    const bool written = SetProcessDefaultCpuSets(
                        process, performance.data(),
                        static_cast<ULONG>(performance.size())) != FALSE;
                    ULONG verified_count = 0;
                    GetProcessDefaultCpuSets(process, nullptr, 0, &verified_count);
                    std::vector<ULONG> verified(verified_count);
                    const bool applied = written &&
                        (!verified_count || GetProcessDefaultCpuSets(
                            process, verified.data(), verified_count,
                            &verified_count)) && verified == performance;
                    set_status(54, applied ? TweakStatus::Applied : TweakStatus::Failed);
                    if (!written) {
                        _advanced_processes.pop_back(); save_journal();
                        state.cpu_sets_touched = false;
                    }
                }
            }
        }
    }
    if (enabled(55)) {
        ULONG original = 0;
        if (!query_io_priority(process, original)) {
            set_status(55, TweakStatus::Unsupported);
        } else if (original >= 3) {
            set_status(55, TweakStatus::AlreadyConfigured);
        } else {
            auto found = std::find_if(_advanced_processes.begin(),
                _advanced_processes.end(), [pid](const AdvancedProcessState& item) {
                    return item.pid == pid;
                });
            if (found == _advanced_processes.end()) {
                state.io_priority = original;
                state.applied_io_priority = 3;
                state.io_touched = true;
                _advanced_processes.push_back(state);
                found = std::prev(_advanced_processes.end());
            } else {
                found->io_priority = original;
                found->applied_io_priority = 3;
                found->io_touched = true;
            }
            if (!save_journal() || !set_io_priority(process, 3)) {
                set_status(55, TweakStatus::Failed);
            } else {
                set_status(55, TweakStatus::Applied);
                _applied.push_back("High game I/O priority");
            }
        }
    }
}

void Optimizer::tune_background_io()
{
    if (!enabled(56))
        return;
    static const char* names[] = {
        "OneDrive.exe", "Dropbox.exe", "GoogleDriveFS.exe", "iCloudDrive.exe",
        "SearchIndexer.exe", "SearchProtocolHost.exe", "SearchFilterHost.exe",
        "CCXProcess.exe", "Creative Cloud.exe", "AdobeUpdateService.exe",
        "MicrosoftEdgeUpdate.exe", "GoogleUpdater.exe", "GoogleUpdate.exe"
    };
    bool found_any = false, changed_any = false;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        set_status(56, TweakStatus::Failed); return;
    }
    PROCESSENTRY32 entry{}; entry.dwSize = sizeof(entry);
    if (Process32First(snapshot, &entry)) do {
        bool match = false;
        for (const char* name : names)
            if (_stricmp(name, entry.szExeFile) == 0) { match = true; break; }
        if (!match) continue;
        found_any = true;
        if (std::any_of(_advanced_processes.begin(), _advanced_processes.end(),
            [&](const AdvancedProcessState& item) {
                return item.pid == entry.th32ProcessID && item.io_touched;
            })) continue;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION |
                                     PROCESS_SET_INFORMATION, FALSE,
                                     entry.th32ProcessID);
        if (!process) continue;
        ULONG original = 0;
        AdvancedProcessState state{};
        state.pid = entry.th32ProcessID;
        if (process_identity(process, state.created_at) &&
            query_io_priority(process, original) && original > 0) {
            state.io_priority = original;
            state.applied_io_priority = 0;
            state.io_touched = true;
            _advanced_processes.push_back(state);
            if (save_journal() && set_io_priority(process, 0))
                changed_any = true;
            else {
                _advanced_processes.pop_back(); save_journal();
            }
        }
        CloseHandle(process);
    } while (Process32Next(snapshot, &entry));
    CloseHandle(snapshot);
    set_status(56, changed_any ? TweakStatus::Applied :
                   found_any ? TweakStatus::Skipped : TweakStatus::Skipped);
    if (changed_any) _applied.push_back("Low background I/O priority");
}

bool Optimizer::restore_advanced_processes()
{
    bool okay = true;
    for (auto item = _advanced_processes.rbegin();
         item != _advanced_processes.rend(); ++item) {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION |
                                     PROCESS_SET_INFORMATION, FALSE, item->pid);
        if (!process) continue;
        ULONGLONG created = 0;
        if (!process_identity(process, created) || created != item->created_at) {
            CloseHandle(process); continue;
        }
        if (item->io_touched) {
            ULONG current = 0;
            if (!query_io_priority(process, current) ||
                (current != item->io_priority &&
                 (current != item->applied_io_priority ||
                  !set_io_priority(process, item->io_priority))))
                okay = false;
        }
        if (item->cpu_sets_touched) {
            ULONG count = 0;
            GetProcessDefaultCpuSets(process, nullptr, 0, &count);
            std::vector<ULONG> current(count);
            if (count && !GetProcessDefaultCpuSets(process, current.data(), count, &count)) {
                okay = false;
            } else if (current != item->cpu_sets) {
                if (current != item->applied_cpu_sets ||
                    !SetProcessDefaultCpuSets(process,
                        item->cpu_sets.empty() ? nullptr : item->cpu_sets.data(),
                        static_cast<ULONG>(item->cpu_sets.size()))) {
                    okay = false;
                } else {
                    ULONG verified_count = 0;
                    GetProcessDefaultCpuSets(process, nullptr, 0, &verified_count);
                    std::vector<ULONG> verified(verified_count);
                    if ((verified_count && !GetProcessDefaultCpuSets(
                            process, verified.data(), verified_count,
                            &verified_count)) || verified != item->cpu_sets)
                        okay = false;
                }
            }
        }
        CloseHandle(process);
    }
    _advanced_processes.clear();
    return okay;
}

bool Optimizer::restore_services()
{
    bool okay = true;
    SC_HANDLE manager = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager && !_service_states.empty()) return false;
    for (auto item = _service_states.rbegin(); item != _service_states.rend(); ++item) {
        if (!item->touched) continue;
        SC_HANDLE service = OpenServiceA(manager, item->name.c_str(),
                                         SERVICE_QUERY_STATUS |
                                         SERVICE_PAUSE_CONTINUE | SERVICE_START);
        if (!service) { okay = false; continue; }
        SERVICE_STATUS_PROCESS status{}; DWORD needed = 0;
        if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                                  reinterpret_cast<BYTE*>(&status), sizeof(status),
                                  &needed)) okay = false;
        else if (status.dwCurrentState != item->original_state) {
            if (status.dwCurrentState != item->applied_state) okay = false;
            else if (item->original_state == SERVICE_RUNNING) {
                SERVICE_STATUS ignored{};
                if (status.dwCurrentState == SERVICE_PAUSED) {
                    if (!ControlService(service, SERVICE_CONTROL_CONTINUE, &ignored) ||
                        !wait_service_state(service, SERVICE_RUNNING, 5000)) okay = false;
                } else if (!StartServiceA(service, 0, nullptr) ||
                           !wait_service_state(service, SERVICE_RUNNING, 10000)) okay = false;
            }
        }
        CloseServiceHandle(service);
    }
    if (manager) CloseServiceHandle(manager);
    _service_states.clear();
    return okay;
}

void Optimizer::optimize_network(bool may_restart)
{
    bool any = false;
    for (size_t index = 45; index <= 53; ++index)
        any = any || enabled(index);
    if (!any) return;
    ActiveAdapterInfo adapter{};
    if (!active_adapter(adapter)) {
        for (size_t index = 45; index <= 53; ++index)
            if (enabled(index)) set_status(index, TweakStatus::Unsupported);
        return;
    }
    const bool ethernet = adapter.type == IF_TYPE_ETHERNET_CSMACD;
    const bool wifi = adapter.type == IF_TYPE_IEEE80211;
    _network_device_instance = adapter.device_instance;
    save_journal();

    auto property = [&](size_t tweak, const std::vector<const char*>& keywords,
                        const std::vector<std::string>& wanted) {
        if (!enabled(tweak)) return;
        if (!may_restart) { set_status(tweak, TweakStatus::Skipped); return; }
        for (const char* keyword : keywords) {
            DWORD type = 0, number = 0; std::string text;
            if (!driver_enum_value(adapter, keyword, wanted, type, text, number))
                continue;
            const size_t previous = _registry_state.size();
            const bool okay = type == REG_DWORD ?
                set_dword(HKEY_LOCAL_MACHINE, adapter.registry_path.c_str(),
                          keyword, number, TWEAK_CATALOG[tweak].label) :
                set_string(HKEY_LOCAL_MACHINE, adapter.registry_path.c_str(),
                           keyword, text, TWEAK_CATALOG[tweak].label);
            if (okay && _registry_state.size() > previous)
                _network_restart_needed = true;
            return;
        }
        set_status(tweak, TweakStatus::Unsupported);
    };

    if (enabled(45)) {
        GetInterfaceDnsSettingsFn get = nullptr;
        SetInterfaceDnsSettingsFn set = nullptr;
        FreeInterfaceDnsSettingsFn release = nullptr;
        WSADATA winsock{};
        if (!dns_api(get, set, release) || WSAStartup(MAKEWORD(2,2), &winsock) != 0) {
            set_status(45, TweakStatus::Unsupported);
        } else {
            const std::wstring original = current_dns_servers(adapter.id);
            const std::string current = effective_dns_server(adapter.index);
            struct Candidate { const char* primary; const wchar_t* pair; };
            const Candidate candidates[] = {
                {"1.1.1.1", L"1.1.1.1,1.0.0.1"},
                {"8.8.8.8", L"8.8.8.8,8.8.4.4"},
                {"9.9.9.9", L"9.9.9.9,149.112.112.112"}
            };
            int current_latency = current.empty() ? -1 : dns_latency_ms(current.c_str());
            int best_latency = current_latency;
            const Candidate* best = nullptr;
            for (const Candidate& candidate : candidates) {
                const int latency = dns_latency_ms(candidate.primary);
                if (latency >= 0 && (best_latency < 0 || latency < best_latency)) {
                    best_latency = latency; best = &candidate;
                }
            }
            if (!best || (current_latency >= 0 &&
                          best_latency + 2 >= current_latency)) {
                set_status(45, TweakStatus::AlreadyConfigured);
            } else {
                _dns_state.interface_id = adapter.id;
                _dns_state.original = original;
                _dns_state.applied = best->pair;
                _dns_state.touched = true;
                if (!save_journal() || !set_dns_servers(adapter.id, _dns_state.applied) ||
                    !same_dns_servers(current_dns_servers(adapter.id), _dns_state.applied)) {
                    if (same_dns_servers(current_dns_servers(adapter.id), _dns_state.applied))
                        set_dns_servers(adapter.id, original);
                    _dns_state = {};
                    save_journal();
                    set_status(45, TweakStatus::Failed);
                } else {
                    set_status(45, TweakStatus::Applied);
                    _applied.push_back("Fastest DNS for active adapter");
                }
            }
            WSACleanup();
        }
    }
    if (ethernet) {
        property(46, {"*RSS"}, {"enabled", "on"});
        property(47, {"*InterruptModeration", "InterruptModeration"},
                 {"disabled", "off"});
        property(48, {"*RscIPv4", "*RscIPv6", "RSC"},
                 {"disabled", "off"});
        property(49, {"*EEE", "EEELinkAdvertisement", "AdvancedEEE",
                      "EnableGreenEthernet"}, {"disabled", "off"});
        property(50, {"*FlowControl", "FlowControl"}, {"disabled", "off"});
    } else {
        for (size_t index = 46; index <= 50; ++index)
            if (enabled(index)) set_status(index, TweakStatus::Unsupported);
    }
    // The device power checkbox is not a stable NDIS advanced property. It is
    // intentionally skipped unless a driver exposes a real enumerated setting.
    property(51, {"PowerSavingMode", "PowerSaveMode"},
             {"disabled", "maximum performance", "off"});
    if (wifi) {
        property(52, {"MIMOPowerSaveMode", "MimoPowerSaveMode"},
                 {"no smps"});
        property(53, {"TransmitPower", "*TransmitPower"},
                 {"highest", "maximum"});
    } else {
        if (enabled(52)) set_status(52, TweakStatus::Unsupported);
        if (enabled(53)) set_status(53, TweakStatus::Unsupported);
    }
    if (_network_restart_needed) {
        save_journal();
        bool connected = restart_adapter_device(_network_device_instance);
        const ULONGLONG deadline = GetTickCount64() + 15000;
        while (connected && GetTickCount64() < deadline) {
            ActiveAdapterInfo check{};
            if (active_adapter(check)) break;
            Sleep(200);
        }
        ActiveAdapterInfo check{};
        connected = connected && active_adapter(check);
        if (!connected) {
            for (size_t index = 46; index <= 53; ++index)
                if (enabled(index) && _tweak_status[index] == TweakStatus::Applied)
                    _tweak_status[index] = TweakStatus::Failed;
            const bool rolled_back = restore_registry();
            const bool network_restored = restore_network();
            if (rolled_back && network_restored) {
                save_journal();
            } else {
                _recovery_failed = true;
                _restoration_status = "Network recovery incomplete";
            }
        }
    }
}

bool Optimizer::restore_network()
{
    bool okay = true;
    if (_dns_state.touched) {
        const std::wstring current = current_dns_servers(_dns_state.interface_id);
        if (!same_dns_servers(current, _dns_state.original)) {
            if (!same_dns_servers(current, _dns_state.applied) ||
                !set_dns_servers(_dns_state.interface_id, _dns_state.original) ||
                !same_dns_servers(current_dns_servers(_dns_state.interface_id),
                                  _dns_state.original))
                okay = false;
        }
        if (okay) _dns_state = {};
    }
    if (!_network_restart_needed) {
        _network_device_instance.clear();
        return okay;
    }
    const bool restarted = restart_adapter_device(_network_device_instance);
    if (restarted) {
        _network_restart_needed = false;
        _network_device_instance.clear();
    }
    return okay && restarted;
}

void Optimizer::optimize_display_refresh(DWORD game_pid)
{
    DISPLAY_DEVICEA device{};
    device.cb = sizeof(device);
    bool found = false;
    std::string target_device;
    if (game_pid) {
        GameWindowSearch search{game_pid, nullptr, 0};
        EnumWindows(find_game_window, reinterpret_cast<LPARAM>(&search));
        if (!search.window) { return; }
        MONITORINFOEXA monitor{};
        monitor.cbSize = sizeof(monitor);
        if (GetMonitorInfoA(MonitorFromWindow(search.window, MONITOR_DEFAULTTONEAREST),
                            &monitor))
            target_device = monitor.szDevice;
    }
    for (DWORD index = 0; EnumDisplayDevicesA(nullptr, index, &device, 0); ++index) {
        if ((!target_device.empty() && _stricmp(device.DeviceName, target_device.c_str()) == 0) ||
            (target_device.empty() && (device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE))) {
            found = true; break;
        }
        device = {};
        device.cb = sizeof(device);
    }
    if (!found) {
        _display_refresh_checked = true;
        set_status(28, TweakStatus::Failed);
        return;
    }
    _display_refresh_checked = true;
    DEVMODEA current{};
    current.dmSize = sizeof(current);
    if (!EnumDisplaySettingsA(device.DeviceName, ENUM_CURRENT_SETTINGS, &current)) {
        set_status(28, TweakStatus::Failed);
        return;
    }
    DEVMODEA best = current;
    for (DWORD index = 0; ; ++index) {
        DEVMODEA candidate{};
        candidate.dmSize = sizeof(candidate);
        if (!EnumDisplaySettingsA(device.DeviceName, index, &candidate))
            break;
        if (candidate.dmPelsWidth == current.dmPelsWidth &&
            candidate.dmPelsHeight == current.dmPelsHeight &&
            candidate.dmBitsPerPel == current.dmBitsPerPel &&
            candidate.dmDisplayOrientation == current.dmDisplayOrientation &&
            candidate.dmDisplayFixedOutput == current.dmDisplayFixedOutput &&
            candidate.dmDisplayFrequency > best.dmDisplayFrequency)
            best = candidate;
    }
    if (best.dmDisplayFrequency <= current.dmDisplayFrequency) {
        set_status(28, TweakStatus::Skipped);
        return;
    }
    if (ChangeDisplaySettingsExA(device.DeviceName, &best, nullptr,
                                 CDS_TEST, nullptr) != DISP_CHANGE_SUCCESSFUL) {
        set_status(28, TweakStatus::Failed);
        return;
    }
    _display_state = {device.DeviceName, current.dmDisplayFrequency,
                      current.dmPelsWidth, current.dmPelsHeight,
                      current.dmBitsPerPel, current.dmDisplayOrientation,
                      current.dmDisplayFixedOutput, current.dmPosition.x,
                      current.dmPosition.y, best.dmDisplayFrequency};
    if (!save_journal()) {
        _display_state = {};
        set_status(28, TweakStatus::Failed);
        return;
    }
    if (ChangeDisplaySettingsExA(device.DeviceName, &best, nullptr,
                                 0, nullptr) != DISP_CHANGE_SUCCESSFUL) {
        if (restore_display())
            save_journal();
        set_status(28, TweakStatus::Failed);
        return;
    }
    DEVMODEA verified{};
    verified.dmSize = sizeof(verified);
    if (!EnumDisplaySettingsA(device.DeviceName, ENUM_CURRENT_SETTINGS, &verified) ||
        verified.dmDisplayFrequency != best.dmDisplayFrequency) {
        if (restore_display())
            save_journal();
        set_status(28, TweakStatus::Failed);
        return;
    }
    set_status(28, TweakStatus::Applied);
    _applied.push_back("Display refresh rate raised");
}

bool Optimizer::restore_display()
{
    if (_display_state.device.empty())
        return true;
    DEVMODEA current{};
    current.dmSize = sizeof(current);
    if (!EnumDisplaySettingsA(_display_state.device.c_str(),
                              ENUM_CURRENT_SETTINGS, &current))
        return false;
    if (current.dmPelsWidth == _display_state.width &&
        current.dmPelsHeight == _display_state.height &&
        current.dmBitsPerPel == _display_state.bits_per_pel &&
        current.dmDisplayOrientation == _display_state.orientation &&
        current.dmDisplayFixedOutput == _display_state.fixed_output &&
        current.dmPosition.x == _display_state.position_x &&
        current.dmPosition.y == _display_state.position_y &&
        current.dmDisplayFrequency == _display_state.frequency) {
        _display_state = {};
        return true;
    }
    if (_display_state.applied_frequency &&
        (current.dmPelsWidth != _display_state.width ||
         current.dmPelsHeight != _display_state.height ||
         current.dmBitsPerPel != _display_state.bits_per_pel ||
         current.dmDisplayOrientation != _display_state.orientation ||
         current.dmDisplayFixedOutput != _display_state.fixed_output ||
         current.dmPosition.x != _display_state.position_x ||
         current.dmPosition.y != _display_state.position_y ||
         current.dmDisplayFrequency != _display_state.applied_frequency))
        return false;
    DEVMODEA original{};
    original.dmSize = sizeof(original);
    original.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL |
                        DM_DISPLAYFREQUENCY | DM_DISPLAYORIENTATION |
                        DM_DISPLAYFIXEDOUTPUT | DM_POSITION;
    original.dmPelsWidth = _display_state.width;
    original.dmPelsHeight = _display_state.height;
    original.dmBitsPerPel = _display_state.bits_per_pel;
    original.dmDisplayFrequency = _display_state.frequency;
    original.dmDisplayOrientation = _display_state.orientation;
    original.dmDisplayFixedOutput = _display_state.fixed_output;
    original.dmPosition.x = _display_state.position_x;
    original.dmPosition.y = _display_state.position_y;
    const bool okay = ChangeDisplaySettingsExA(
        _display_state.device.c_str(), &original, nullptr, CDS_TEST, nullptr) ==
        DISP_CHANGE_SUCCESSFUL &&
        ChangeDisplaySettingsExA(_display_state.device.c_str(), &original,
                                 nullptr, 0, nullptr) == DISP_CHANGE_SUCCESSFUL;
    if (okay)
        _display_state = {};
    return okay;
}

bool Optimizer::optimize(const std::string& game_path, bool launch_if_missing)
{
    const std::lock_guard<std::recursive_mutex> lock(_state_mutex);
    if (_recovery_failed || (_saved_tweaks && _saved_tweaks->blocked())) {
        _last_error = _restoration_status;
        if (_last_error.empty() && _saved_tweaks)
            _last_error = _saved_tweaks->error();
        return false;
    }
    if (_is_optimized) {
        _last_error = "A session is already active";
        return false;
    }

    _applied.clear();
    _failed.clear();
    for (size_t index = 0; index < _tweak_status.size(); ++index)
        _tweak_status[index] = enabled(index) ? TweakStatus::On : TweakStatus::Off;
    for (size_t index = 26; index < _tweak_status.size(); ++index)
        set_status(index, TweakStatus::Skipped);
    for (size_t index : {size_t(11), size_t(12), size_t(13), size_t(14),
                         size_t(19), size_t(20), size_t(21), size_t(22)})
        set_status(index, TweakStatus::Skipped);
    _last_error.clear();
    _last_helper_scan = GetTickCount64();
    _restoration_status.clear();
    _restoration_succeeded = false;
    _display_refresh_checked = false;
    _baseline_game_path = game_path;
    save_power_plan(_saved_power_plan_guid, sizeof(_saved_power_plan_guid));
    if (!save_journal()) {
        _last_error = "Could not create recovery log";
        return false;
    }
    if (!start_recovery_watchdog()) {
        _last_error = "Could not start recovery watchdog";
        DeleteFileA(_journal_path.c_str());
        return false;
    }
    optimize_network(game_path.empty() ||
                     (launch_if_missing && !is_game_active(game_path)));
    if (_recovery_failed) {
        _last_error = _restoration_status;
        return false;
    }
    const bool power_session_needed = enabled(0) || enabled(23) ||
                                      enabled(24) || enabled(25) ||
                                      enabled(37) || enabled(38) || enabled(39) ||
                                      enabled(44);
    const bool power_session_ready = !power_session_needed ||
                                     begin_session_power_plan();
    if (_recovery_failed) {
        _last_error = _restoration_status;
        return false;
    }
    if (!power_session_ready) {
        for (size_t index : {size_t(0), size_t(23), size_t(24), size_t(25),
                             size_t(37), size_t(38), size_t(39), size_t(44)})
            if (enabled(index))
                set_status(index, TweakStatus::Failed);
        _failed.push_back("Temporary power plan unavailable");
    }
    if (power_session_ready && enabled(23))
        set_ac_power_setting(0, 0, 23, "AC CPU performance preference");
    if (power_session_ready && enabled(24))
        set_ac_power_setting(1, 0, 24, "AC PCIe link power saving off");
    if (power_session_ready && enabled(25))
        set_ac_power_setting(2, 1, 25, "AC CPU boost mode");
    if (power_session_ready && enabled(44))
        set_ac_power_setting(7, 100, 44, "AC CPU minimum: 100%");
    if (power_session_ready && enabled(37)) {
        set_ac_power_setting(3, 100, 37, "Core parking minimum");
        set_ac_power_setting(4, 100, 37, "Core parking efficiency class");
    }
    if (power_session_ready && enabled(38))
        set_ac_power_setting(5, 0, 38, "AC Wi-Fi performance");
    if (power_session_ready && enabled(39))
        set_ac_power_setting(6, 0, 39, "USB selective suspend off");
    if (enabled(28) && game_path.empty())
        optimize_display_refresh();

    if (enabled(1)) {
        _power_request_active = SetThreadExecutionState(
            ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED) != 0;
        record(_power_request_active, "Sleep paused during session");
    }

    if (enabled(2))
        set_dword(HKEY_CURRENT_USER, "System\\GameConfigStore", "GameDVR_Enabled", 0,
                  "Game DVR off");
    if (enabled(3))
        set_dword(HKEY_CURRENT_USER,
                  "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\GameDVR",
                  "AppCaptureEnabled", 0, "Background capture off");
    if (enabled(4))
        set_dword(HKEY_CURRENT_USER, "Software\\Microsoft\\GameBar",
                  "ShowStartupPanel", 0, "Game Bar popup off");
    if (enabled(5)) {
        set_dword(HKEY_CURRENT_USER, "Software\\Microsoft\\GameBar",
                  "AllowAutoGameMode", 1, "Game Mode allowed");
        set_dword(HKEY_CURRENT_USER, "Software\\Microsoft\\GameBar",
                  "AutoGameModeEnabled", 1, "Game Mode on");
    }

    // Keep MMCSS inside documented ranges. GPU Priority is documented as unused.
    if (enabled(6))
        set_dword(HKEY_LOCAL_MACHINE, MMCSS_PROFILE, "NetworkThrottlingIndex",
                  0xFFFFFFFF, "MMCSS network throttling off");
    if (enabled(7))
        set_dword(HKEY_LOCAL_MACHINE, MMCSS_PROFILE, "SystemResponsiveness", 10,
                  "MMCSS system reserve: 10%");
    if (enabled(8))
        set_dword(HKEY_LOCAL_MACHINE, MMCSS_GAMES, "Priority", 6,
                  "MMCSS game priority");
    if (enabled(9))
        set_string(HKEY_LOCAL_MACHINE, MMCSS_GAMES, "Scheduling Category", "Medium",
                   "MMCSS game scheduling");
    if (enabled(10))
        set_dword(HKEY_LOCAL_MACHINE,
                  "SYSTEM\\CurrentControlSet\\Control\\PriorityControl",
                  "Win32PrioritySeparation", 0x26,
                  "Foreground CPU scheduling");

    if (enabled(11) || enabled(19) || enabled(20) || enabled(21) || enabled(22) ||
        enabled(29) || enabled(30) || enabled(31) || enabled(32) ||
        enabled(33) || enabled(34) || enabled(35) || enabled(36))
        throttle_background_processes();

    if (enabled(15))
        set_dword(HKEY_CURRENT_USER, "Software\\Microsoft\\GameBar",
                  "UseNexusForGameBarEnabled", 0, "Controller Game Bar shortcut off");
    if (enabled(16))
        set_dword(HKEY_CURRENT_USER,
                  "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\GameDVR",
                  "VKMToggleRecording", 0, "Recording shortcut off");
    if (enabled(17))
        set_dword(HKEY_CURRENT_USER,
                  "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\GameDVR",
                  "VKMSaveHistoricalVideo", 0, "Replay shortcut off");
    if (enabled(18))
        set_dword(HKEY_CURRENT_USER,
                  "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\GameDVR",
                  "VKMToggleGameBar", 0, "Game Bar shortcut off");
    if (enabled(40))
        optimize_accessibility_hotkeys();
    optimize_input_tuning();
    optimize_advanced_session();
    tune_background_io();

    if (!is_admin())
        _failed.push_back("Administrator rights required for system tweaks");

    _is_optimized = true;
    if (!game_path.empty() && !launch_game(game_path, launch_if_missing)) {
        const std::string launch_error = _last_error;
        restore();
        _last_error = launch_error;
        return false;
    }
    return true;
}

void Optimizer::restore()
{
    const std::lock_guard<std::recursive_mutex> lock(_state_mutex);
    if (!_is_optimized)
        return;

    if (_game_process) {
        CloseHandle(_game_process);
        _game_process = nullptr;
        _game_pid = 0;
    }
    _waiting_for_game = false;
    _target_game_path.clear();
    release_timer_resolution();
    bool restored = restore_input_tuning();
    restored = restore_accessibility_hotkeys() && restored;
    restored = restore_advanced_processes() && restored;
    restored = restore_services() && restored;
    restored = restore_background_processes() && restored;
    restored = restore_registry() && restored;
    restored = restore_network() && restored;
    if (_session_power_plan_guid.empty())
        restored = restore_ac_power_settings() && restored;
    else
        _power_settings.clear();
    restored = restore_display() && restored;
    if (!_session_power_plan_guid.empty())
        restored = restore_session_power_plan() && restored;
    else if (_saved_power_plan_guid[0])
        restored = set_power_plan(_saved_power_plan_guid) && restored;
    if (_power_request_active) {
        SetThreadExecutionState(ES_CONTINUOUS);
        _power_request_active = false;
    }
    if (_saved_tweaks && _saved_tweaks->has_baseline())
        restored = _saved_tweaks->restore_baseline(_baseline_game_path) && restored;
    _baseline_game_path.clear();
    _restoration_succeeded = restored && !_journal_path.empty() &&
                             DeleteFileA(_journal_path.c_str());
    if (_restoration_succeeded)
        _restoration_status = "Settings restored";
    else {
        _restoration_status = "Restore incomplete - recovery log kept";
        _recovery_failed = true;
    }
    _is_optimized = false;
}

bool Optimizer::is_optimized() const { return _is_optimized; }
bool Optimizer::has_game_process() const { return _game_process != nullptr; }
DWORD Optimizer::game_pid() const { return _game_pid; }
bool Optimizer::restoration_succeeded() const { return _restoration_succeeded; }
bool Optimizer::recovery_blocked() const
{
    return _recovery_failed || (_saved_tweaks && _saved_tweaks->blocked());
}
bool Optimizer::retry_recovery()
{
    const std::lock_guard<std::recursive_mutex> lock(_state_mutex);
    if (_is_optimized)
        return false;
    if (GetFileAttributesA(_journal_path.c_str()) != INVALID_FILE_ATTRIBUTES)
        recover_journal();
    else
        _recovery_failed = false;
    if (!_recovery_failed && _saved_tweaks && !_saved_tweaks->recover()) {
        _recovery_failed = true;
        _restoration_status = _saved_tweaks->error();
    }
    return !_recovery_failed;
}
bool Optimizer::is_game_active(const std::string& game_path) const
{
    if (game_path.empty())
        return false;
    return running_game_pid(target_game_path(game_path)) != 0;
}

bool Optimizer::find_target_process()
{
    const DWORD pid = running_game_pid(_target_game_path);
    if (!pid)
        return false;
    HANDLE candidate = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                                   FALSE, pid);
    if (!candidate)
        return false;
    if (_game_process)
        CloseHandle(_game_process);
    _game_process = candidate;
    _game_pid = pid;
    _waiting_for_game = false;
    if (enabled(28) && !_display_refresh_checked)
        optimize_display_refresh(pid);

    HANDLE tune = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION |
                              PROCESS_SET_INFORMATION | SYNCHRONIZE, FALSE, pid);
    const bool tracked = tune && track_process_for_restore(tune, pid);
    tune_game_process(tune, tracked);
    tune_advanced_game_process(tune, pid);
    if (tune)
        CloseHandle(tune);
    return true;
}

bool Optimizer::is_game_running()
{
    const std::lock_guard<std::recursive_mutex> lock(_state_mutex);
    if (_waiting_for_game) {
        if (find_target_process())
            return true;
        return GetTickCount64() - _launch_started < 120000;
    }
    if (!_game_process)
        return false;
    DWORD exit_code = 0;
    const bool running = GetExitCodeProcess(_game_process, &exit_code) &&
                         exit_code == STILL_ACTIVE;
    if (running && GetTickCount64() - _last_helper_scan >= 30000) {
        _last_helper_scan = GetTickCount64();
        throttle_background_processes();
        tune_background_io();
    }
    if (running && enabled(28) && !_display_refresh_checked)
        optimize_display_refresh(_game_pid);
    return running;
}

bool Optimizer::is_waiting_for_game() const { return _waiting_for_game; }

std::string Optimizer::game_status() const
{
    if (_waiting_for_game)
        return "Waiting for Fortnite";
    if (_game_process)
        return "Game active (PID " + std::to_string(_game_pid) + ")";
    return "Windows only";
}

const std::vector<std::string>& Optimizer::applied_tweaks() const { return _applied; }
const std::vector<std::string>& Optimizer::failed_tweaks() const { return _failed; }
const std::string& Optimizer::last_error() const { return _last_error; }
const std::string& Optimizer::restoration_status() const { return _restoration_status; }
