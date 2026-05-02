#include "optimizer.hpp"

Optimizer::Optimizer()
{
    _is_optimized = false;
}

Optimizer::~Optimizer()
{
    restore();
    _is_optimized = false;
}

static void kill_process(const std::string& process)
{
    std::string cmd = "taskkill /f /im " + process + " > nul 2>&1";
    system(cmd.c_str());
}

void Optimizer::optimize(const std::string& game_path)
{
    STARTUPINFOA        si;
    PROCESS_INFORMATION pi;
    std::string         command;

    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    _is_fortnite = (game_path.find("Fortnite") != std::string::npos
        || game_path.find("fortnite") != std::string::npos);

    kill_process("explorer.exe");
    kill_process("OneDrive.exe");
    kill_process("XboxPcApp.exe");
    kill_process("GameBar.exe");
    kill_process("SteamWebHelper.exe");
    kill_process("SearchHost.exe");
    kill_process("Widgets.exe");
    kill_process("RuntimeBroker.exe");
    kill_process("backgroundTaskHost.exe");
    kill_process("EpicWebHelper.exe");
    kill_process("EpicGamesLauncher.exe");
    kill_process("AdobeUpdater.exe");
    kill_process("Teams.exe");
    kill_process("MicrosoftEdge.exe");

    system("powercfg /setactive SCHEME_MIN > nul 2>&1");

    system("bcdedit /set useplatformtick yes > nul 2>&1");
    system("bcdedit /set disabledynamictick yes > nul 2>&1");
    system("bcdedit /set tscsyncpolicy Enhanced > nul 2>&1");
    system("bcdedit /set nx optout > nul 2>&1");

    system("wmic process where name='dwm.exe' CALL setpriority 128 > nul 2>&1");
    system("wmic process where name='csrss.exe' CALL setpriority 128 > nul 2>&1");

    system("net stop wuauserv > nul 2>&1");
    system("net stop SysMain > nul 2>&1");
    system("net stop DiagTrack > nul 2>&1");
    system("net stop WSearch > nul 2>&1");
    system("net stop Spooler > nul 2>&1");
    system("net stop DPS > nul 2>&1");
    system("net stop MapsBroker > nul 2>&1");

    system("ipconfig /flushdns > nul 2>&1");
    system("del /s /f /q %temp%\\* > nul 2>&1");
    system("del /s /f /q C:\\Windows\\Temp\\* > nul 2>&1");
    system("cleanmgr /sagerun:1 > nul 2>&1");

    system("reg add \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced\" /v TaskbarAnimations /t REG_DWORD /d 0 /f > nul 2>&1");
    system("reg add \"HKCU\\Control Panel\\Desktop\" /v MenuShowDelay /t REG_SZ /d 0 /f > nul 2>&1");
    system("reg add \"HKCU\\Control Panel\\Desktop\" /v AutoEndTasks /t REG_SZ /d 1 /f > nul 2>&1");
    system("reg add \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\BackgroundAccessApplications\" /v GlobalUserDisabled /t REG_DWORD /d 1 /f > nul 2>&1");

    system("fsutil behavior set memoryusage 2 > nul 2>&1");
    system("fsutil behavior set disablelastaccess 1 > nul 2>&1");

    if (_is_fortnite)
    {
        system("reg add \"HKCU\\System\\GameConfigStore\" /v GameDVR_Enabled /t REG_DWORD /d 0 /f > nul 2>&1");
        system("reg add \"HKLM\\SOFTWARE\\Microsoft\\PolicyManager\\default\\ApplicationManagement\\AllowGameDVR\" /v value /t REG_DWORD /d 0 /f > nul 2>&1");

        system("reg add \"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\" /v NetworkThrottlingIndex /t REG_DWORD /d 4294967295 /f > nul 2>&1");
        system("reg add \"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\" /v SystemResponsiveness /t REG_DWORD /d 0 /f > nul 2>&1");

        system("reg add \"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games\" /v GPU Priority /t REG_DWORD /d 8 /f > nul 2>&1");
        system("reg add \"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games\" /v Priority /t REG_DWORD /d 6 /f > nul 2>&1");
        system("reg add \"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games\" /v Scheduling Category /t REG_SZ /d High /f > nul 2>&1");
    }

    command = "\"" + game_path + "\"";

    if (CreateProcessA(
            NULL,
            command.data(),
            NULL,
            NULL,
            FALSE,
            0,
            NULL,
            NULL,
            &si,
            &pi))
    {
        _is_optimized = true;

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

void Optimizer::restore()
{
    if (!_is_optimized)
        return;

    system("start \"\" explorer.exe");

    system("net start wuauserv > nul 2>&1");
    system("net start SysMain > nul 2>&1");
    system("net start DiagTrack > nul 2>&1");
    system("net start WSearch > nul 2>&1");
    system("net start Spooler > nul 2>&1");
    system("net start DPS > nul 2>&1");
    system("net start MapsBroker > nul 2>&1");

    if (_is_fortnite)
    {
        system("reg add \"HKCU\\System\\GameConfigStore\" /v GameDVR_Enabled /t REG_DWORD /d 1 /f > nul 2>&1");
    }

    _is_optimized = false;
}