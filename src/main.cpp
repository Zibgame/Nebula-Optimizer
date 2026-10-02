#include "tui.hpp"

#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <cstdlib>
#include <algorithm>
#include <cwctype>
#include <cstring>

namespace {
Tui* active_tui = nullptr;

bool nebula_name(const char* name)
{
    return _stricmp(name, "Nebula-Optimizer.exe") == 0 ||
           _stricmp(name, "nebula_optimizer.exe") == 0 ||
           _stricmp(name, "nebula_optimizer_next.exe") == 0 ||
           _stricmp(name, "nebula_optimizer_ui.exe") == 0 ||
           _stricmp(name, "nebula_optimizer_ui_v2.exe") == 0;
}

std::string directory_of(const char* path)
{
    const std::string value(path);
    const size_t slash = value.find_last_of("\\/");
    return slash == std::string::npos ? std::string() : value.substr(0, slash);
}

bool process_is_watchdog(HANDLE process);

bool retire_old_instances()
{
    char own_path[MAX_PATH * 4]{};
    if (!GetModuleFileNameA(nullptr, own_path, sizeof(own_path)))
        return false;
    const std::string own_directory = directory_of(own_path);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;
    PROCESSENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    bool okay = true;
    if (Process32First(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == GetCurrentProcessId() ||
                !nebula_name(entry.szExeFile))
                continue;
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION |
                                         PROCESS_TERMINATE | SYNCHRONIZE,
                                         FALSE, entry.th32ProcessID);
            if (!process) {
                okay = false;
                continue;
            }
            if (process_is_watchdog(process)) {
                CloseHandle(process);
                continue;
            }
            char image[MAX_PATH * 4]{};
            DWORD length = sizeof(image);
            const bool same_directory =
                QueryFullProcessImageNameA(process, 0, image, &length) &&
                _stricmp(directory_of(image).c_str(), own_directory.c_str()) == 0;
            if (same_directory &&
                (!TerminateProcess(process, 0) ||
                 WaitForSingleObject(process, 5000) != WAIT_OBJECT_0))
                okay = false;
            CloseHandle(process);
        } while (Process32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return okay;
}

bool process_is_watchdog(HANDLE process)
{
    using Query = LONG (WINAPI*)(HANDLE, int, void*, unsigned long, unsigned long*);
    const FARPROC address = GetProcAddress(GetModuleHandleA("ntdll.dll"),
                                           "NtQueryInformationProcess");
    Query query = nullptr;
    static_assert(sizeof(query) == sizeof(address), "function pointer size");
    std::memcpy(&query, &address, sizeof(query));
    if (!query) return false;
    unsigned long needed = 0;
    query(process, 60, nullptr, 0, &needed);
    if (!needed || needed > 1024 * 1024) return false;
    std::vector<unsigned char> buffer(needed);
    if (query(process, 60, buffer.data(), needed, &needed) < 0) return false;
    struct CommandLine { USHORT length; USHORT maximum; PWSTR text; };
    const auto* command = reinterpret_cast<const CommandLine*>(buffer.data());
    if (!command->text || !command->length) return false;
    std::wstring value(command->text, command->length / sizeof(wchar_t));
    std::transform(value.begin(), value.end(), value.begin(), towlower);
    return value.find(L"--watchdog") != std::wstring::npos;
}

BOOL WINAPI console_control(DWORD event)
{
    if ((event == CTRL_CLOSE_EVENT || event == CTRL_C_EVENT ||
         event == CTRL_BREAK_EVENT || event == CTRL_LOGOFF_EVENT ||
         event == CTRL_SHUTDOWN_EVENT) && active_tui)
        active_tui->emergency_restore();
    return FALSE;
}
}

int main(int argc, char** argv)
{
    if (argc == 4 && std::string(argv[1]) == "--watchdog") {
        const DWORD parent_pid = static_cast<DWORD>(std::strtoul(argv[2], nullptr, 10));
        const ULONGLONG expected_created = std::strtoull(argv[3], nullptr, 10);
        HANDLE recovery = CreateMutexA(nullptr, TRUE, "Local\\NebulaOptimizer.Recovery");
        HANDLE parent = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                    FALSE, parent_pid);
        FILETIME created{}, exited{}, kernel{}, user{};
        const bool same_process = parent && GetProcessTimes(parent, &created, &exited,
            &kernel, &user) &&
            ((static_cast<ULONGLONG>(created.dwHighDateTime) << 32) |
             created.dwLowDateTime) == expected_created;
        if (same_process) WaitForSingleObject(parent, INFINITE);
        if (parent) CloseHandle(parent);
        if (same_process) { Optimizer recovery_agent; }
        if (recovery) { ReleaseMutex(recovery); CloseHandle(recovery); }
        return same_process ? 0 : 2;
    }
    HANDLE launch_gate = CreateMutexA(nullptr, FALSE,
                                      "Local\\NebulaOptimizer.LaunchGate");
    if (!launch_gate || WaitForSingleObject(launch_gate, 10000) != WAIT_OBJECT_0) {
        MessageBoxA(nullptr, "Nebula is already starting.",
                    "Nebula", MB_OK | MB_ICONINFORMATION);
        if (launch_gate)
            CloseHandle(launch_gate);
        return 1;
    }
    if (!retire_old_instances()) {
        MessageBoxA(nullptr, "Could not close an older Nebula instance safely.",
                    "Nebula", MB_OK | MB_ICONWARNING);
        ReleaseMutex(launch_gate);
        CloseHandle(launch_gate);
        return 1;
    }
    HANDLE recovery = OpenMutexA(SYNCHRONIZE, FALSE,
                                 "Local\\NebulaOptimizer.Recovery");
    if (recovery) {
        WaitForSingleObject(recovery, 15000);
        CloseHandle(recovery);
    }
    HANDLE instance = CreateMutexA(nullptr, TRUE, "Local\\NebulaOptimizer.Session");
    if (!instance || GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxA(nullptr, "Another Nebula session is still running.",
                    "Nebula", MB_OK | MB_ICONWARNING);
        if (instance)
            CloseHandle(instance);
        ReleaseMutex(launch_gate);
        CloseHandle(launch_gate);
        return 1;
    }
    ReleaseMutex(launch_gate);
    CloseHandle(launch_gate);
    Tui tui;
    active_tui = &tui;
    SetConsoleCtrlHandler(console_control, TRUE);
    tui.run();
    SetConsoleCtrlHandler(console_control, FALSE);
    active_tui = nullptr;
    ReleaseMutex(instance);
    CloseHandle(instance);
    return 0;
}
