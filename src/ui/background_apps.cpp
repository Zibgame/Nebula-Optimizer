#include "background_apps.hpp"
#include "json.hpp"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <windows.h>
#include <psapi.h>
#include <shlobj.h>
#include <tlhelp32.h>

namespace {

std::string utf8(const std::wstring& value)
{
    if (value.empty()) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string result(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), bytes, nullptr, nullptr);
    return result;
}

std::wstring wide(const std::string& value)
{
    if (value.empty()) return {};
    const int characters = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (characters <= 0) return {};
    std::wstring result(static_cast<size_t>(characters), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), characters);
    return result;
}

std::wstring normalized(const std::wstring& path)
{
    wchar_t full[MAX_PATH * 4]{};
    const DWORD length = GetFullPathNameW(path.c_str(), MAX_PATH * 4, full, nullptr);
    if (!length || length >= MAX_PATH * 4) return {};
    std::wstring result(full);
    std::transform(result.begin(), result.end(), result.begin(), towlower);
    return result;
}

bool path_inside(const std::wstring& path, const std::wstring& folder)
{
    return path.size() > folder.size() &&
           path.compare(0, folder.size(), folder) == 0 &&
           (folder.back() == L'\\' || path[folder.size()] == L'\\');
}

bool protected_path(const std::wstring& path, const std::wstring& game_path)
{
    const std::wstring full = normalized(path);
    if (full.empty() || (full.size() < 4 || full.substr(full.size()-4) != L".exe"))
        return true;
    wchar_t windows[MAX_PATH]{};
    if (GetWindowsDirectoryW(windows, MAX_PATH) &&
        path_inside(full, normalized(windows)))
        return true;
    wchar_t own[MAX_PATH * 4]{};
    if (GetModuleFileNameW(nullptr, own, MAX_PATH * 4) &&
        full == normalized(own))
        return true;
    if (!game_path.empty() && full == normalized(game_path))
        return true;
    const auto name = std::filesystem::path(full).filename().wstring();
    for (const wchar_t* blocked : {L"fortnite", L"easyanticheat",
         L"battleye", L"epicgameslauncher", L"nebula-optimizer"})
        if (name.find(blocked) != std::wstring::npos)
            return true;
    return false;
}

std::wstring process_path(HANDLE process)
{
    wchar_t path[MAX_PATH * 4]{};
    DWORD length = MAX_PATH * 4;
    if (!QueryFullProcessImageNameW(process, 0, path, &length)) return {};
    return std::wstring(path, length);
}

std::uint64_t working_set(HANDLE process)
{
    PROCESS_MEMORY_COUNTERS counters{};
    if (!GetProcessMemoryInfo(process, &counters, sizeof(counters))) return 0;
    return counters.WorkingSetSize;
}

std::uint64_t available_ram()
{
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    return GlobalMemoryStatusEx(&memory) ? memory.ullAvailPhys : 0;
}

bool same_session(DWORD pid)
{
    DWORD own_session = 0;
    DWORD process_session = 0;
    return ProcessIdToSessionId(GetCurrentProcessId(), &own_session) &&
           ProcessIdToSessionId(pid, &process_session) &&
           own_session == process_session;
}

bool same_user(HANDLE process)
{
    HANDLE own_token = nullptr;
    HANDLE other_token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &own_token))
        return false;
    if (!OpenProcessToken(process, TOKEN_QUERY, &other_token)) {
        CloseHandle(own_token);
        return false;
    }
    DWORD own_size = 0;
    DWORD other_size = 0;
    GetTokenInformation(own_token, TokenUser, nullptr, 0, &own_size);
    GetTokenInformation(other_token, TokenUser, nullptr, 0, &other_size);
    std::vector<BYTE> own(own_size);
    std::vector<BYTE> other(other_size);
    const bool matches = own_size && other_size &&
        GetTokenInformation(own_token, TokenUser, own.data(), own_size,
                            &own_size) &&
        GetTokenInformation(other_token, TokenUser, other.data(), other_size,
                            &other_size) &&
        EqualSid(reinterpret_cast<TOKEN_USER*>(own.data())->User.Sid,
                 reinterpret_cast<TOKEN_USER*>(other.data())->User.Sid);
    CloseHandle(other_token);
    CloseHandle(own_token);
    return matches;
}

struct ProcessToClose {
    HANDLE handle = nullptr;
    DWORD pid = 0;
    std::uint64_t working_set_bytes = 0;
    bool close_sent = false;
};

BOOL CALLBACK request_window_close(HWND window, LPARAM parameter)
{
    auto* processes = reinterpret_cast<std::vector<ProcessToClose>*>(parameter);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    for (auto& process : *processes) {
        if (process.pid == pid && GetWindow(window, GW_OWNER) == nullptr &&
            PostMessageW(window, WM_CLOSE, 0, 0))
            process.close_sent = true;
    }
    return TRUE;
}

} // namespace

BackgroundApps::BackgroundApps()
{
    wchar_t local[MAX_PATH * 4]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local,
                                                  MAX_PATH * 4);
    if ((!length || length >= MAX_PATH * 4) &&
        FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr,
                                SHGFP_TYPE_CURRENT, local))) return;
    _settings_path = utf8((std::filesystem::path(local) / "NebulaOptimizer" /
                           "apps-to-close.json").wstring());
    try {
        std::ifstream file(std::filesystem::path(wide(_settings_path)));
        if (!file) return;
        const auto data = nlohmann::json::parse(file);
        if (!data.is_object() || !data.contains("paths") ||
            !data["paths"].is_array()) return;
        for (const auto& item : data["paths"]) {
            if (!item.is_string()) continue;
            const auto value = item.get<std::string>();
            if (!wide(value).empty() && !protected_path(wide(value), L""))
                _selected.push_back(value);
        }
    } catch (...) {}
}

std::vector<RunningBackgroundApp> BackgroundApps::running() const
{
    std::map<std::wstring, RunningBackgroundApp> grouped;
    std::vector<std::wstring> selected_paths;
    selected_paths.reserve(_selected.size());
    for (const auto& path : _selected)
        selected_paths.push_back(normalized(wide(path)));
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return {};
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == GetCurrentProcessId() ||
                !same_session(entry.th32ProcessID)) continue;
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                         FALSE, entry.th32ProcessID);
            if (!process) continue;
            if (!same_user(process)) { CloseHandle(process); continue; }
            const std::wstring path = process_path(process);
            const auto full = normalized(path);
            if (!protected_path(path, L"") &&
                std::find(selected_paths.begin(), selected_paths.end(), full) ==
                    selected_paths.end()) {
                auto& app = grouped[full];
                if (app.path.empty()) {
                    app.path = utf8(path);
                    app.name = utf8(std::filesystem::path(path).filename().wstring());
                }
                ++app.process_count;
                app.working_set_bytes += working_set(process);
            }
            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    std::vector<RunningBackgroundApp> result;
    for (const auto& item : grouped) result.push_back(item.second);
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.name < right.name;
    });
    return result;
}

bool BackgroundApps::save() const
{
    if (_settings_path.empty()) return false;
    try {
        const std::filesystem::path path(wide(_settings_path));
        std::filesystem::create_directories(path.parent_path());
        const auto temporary = path.wstring() + L".tmp";
        {
            std::ofstream file(std::filesystem::path(temporary),
                               std::ios::binary | std::ios::trunc);
            if (!file) return false;
            file << nlohmann::json{{"version", 1}, {"paths", _selected}}.dump(2);
            file.flush();
            if (!file) return false;
        }
        return MoveFileExW(temporary.c_str(), path.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    } catch (...) { return false; }
}

bool BackgroundApps::add(const std::string& path)
{
    const auto value = wide(path);
    if (value.empty() || protected_path(value, L"")) return false;
    const auto full = normalized(value);
    std::error_code error;
    if (!std::filesystem::is_regular_file(std::filesystem::path(full), error) ||
        error) return false;
    for (const auto& existing : _selected)
        if (normalized(wide(existing)) == full) return false;
    _selected.push_back(utf8(std::filesystem::path(full).wstring()));
    if (save()) return true;
    _selected.pop_back();
    return false;
}

bool BackgroundApps::remove(size_t index)
{
    if (index >= _selected.size()) return false;
    const auto removed = _selected[index];
    _selected.erase(_selected.begin() + static_cast<std::ptrdiff_t>(index));
    if (save()) return true;
    _selected.insert(_selected.begin() + static_cast<std::ptrdiff_t>(index), removed);
    return false;
}

ClosedAppsSummary BackgroundApps::close_selected(const std::string& game_path) const
{
    ClosedAppsSummary result;
    if (_selected.empty()) return result;
    const std::uint64_t before = available_ram();
    std::vector<std::wstring> selected_paths;
    selected_paths.reserve(_selected.size());
    for (const auto& path : _selected)
        selected_paths.push_back(normalized(wide(path)));
    const std::wstring game = wide(game_path);
    std::vector<ProcessToClose> processes;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == GetCurrentProcessId() ||
                !same_session(entry.th32ProcessID)) continue;
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION |
                                         SYNCHRONIZE, FALSE, entry.th32ProcessID);
            if (!process) continue;
            if (!same_user(process)) { CloseHandle(process); continue; }
            const auto path = process_path(process);
            const auto full = normalized(path);
            bool selected = false;
            for (const auto& entry_path : selected_paths)
                selected = selected || full == entry_path;
            if (selected && !protected_path(path, game)) {
                processes.push_back({process, entry.th32ProcessID,
                                     working_set(process), false});
            } else CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    if (processes.empty()) return result;

    EnumWindows(request_window_close, reinterpret_cast<LPARAM>(&processes));
    const ULONGLONG deadline = GetTickCount64() + 3000;
    while (GetTickCount64() < deadline) {
        bool any_running = false;
        for (const auto& process : processes)
            if (process.close_sent &&
                WaitForSingleObject(process.handle, 0) != WAIT_OBJECT_0)
                any_running = true;
        if (!any_running) break;
        Sleep(100);
    }
    for (const auto& process : processes) {
        if (!process.close_sent) ++result.skipped;
        else if (WaitForSingleObject(process.handle, 0) == WAIT_OBJECT_0) {
            ++result.closed;
            result.closed_working_set_bytes += process.working_set_bytes;
        } else ++result.failed;
        CloseHandle(process.handle);
    }
    const std::uint64_t after = available_ram();
    result.available_ram_delta_bytes = static_cast<std::int64_t>(after) -
                                       static_cast<std::int64_t>(before);
    return result;
}
