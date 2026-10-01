#include "tui.hpp"

#include <windows.h>
#include <tlhelp32.h>
#include <string>

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

BOOL WINAPI console_control(DWORD event)
{
    if ((event == CTRL_CLOSE_EVENT || event == CTRL_C_EVENT ||
         event == CTRL_BREAK_EVENT || event == CTRL_LOGOFF_EVENT ||
         event == CTRL_SHUTDOWN_EVENT) && active_tui)
        active_tui->emergency_restore();
    return FALSE;
}
}

int main()
{
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
