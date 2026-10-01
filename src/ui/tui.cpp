#include "tui.hpp"
#include "tui_colors.hpp"
#include "tweak_navigation.hpp"
#include "tui_view.hpp"

#include <algorithm>
#include <atomic>
#include <conio.h>
#include <filesystem>
#include <fstream>
#include <deque>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <psapi.h>
#include <shellapi.h>
#include <sstream>
#include <thread>
#include <vector>
#include <windows.h>

namespace {

int box_width = 68;
int box_margin = 2;
constexpr const char* TWEAK_CATEGORIES[] = {
    "CPU & Power", "GPU & Display", "Memory", "Background",
    "Input & Capture", "Saved"
};
constexpr const char* TWEAK_TABS[] = {
    "CPU", "GPU", "RAM", "Apps", "Input", "Saved"
};

bool confirm_display_mode()
{
    std::cout << "\nKeep this display mode? [Y] within 15 seconds: " << std::flush;
    const ULONGLONG deadline = GetTickCount64() + 15000;
    while (GetTickCount64() < deadline) {
        if (_kbhit()) {
            const int key = _getch();
            if (key == 'y' || key == 'Y') return true;
            if (key == 'n' || key == 'N' || key == 27) return false;
        }
        Sleep(50);
    }
    return false;
}
std::ostream* frame_output = &std::cout;
size_t frame_line = 0;

const char* view_label(ViewState state)
{
    switch (state) {
    case ViewState::Applied: return "Applied";
    case ViewState::Skipped: return "Skipped";
    case ViewState::Failed: return "Failed";
    case ViewState::Off: return "Off";
    case ViewState::Drift: return "Drift";
    case ViewState::Pending: return "On";
    }
    return "On";
}

const char* view_color(ViewState state)
{
    switch (state) {
    case ViewState::Applied: return LIGHT_GREEN;
    case ViewState::Skipped: return LIGHT_YELLOW;
    case ViewState::Failed: return LIGHT_RED;
    case ViewState::Drift: return LIGHT_RED;
    case ViewState::Off: return BRIGHT_BLACK;
    case ViewState::Pending: return LIGHT_CYAN;
    }
    return WHITE;
}

void update_layout()
{
    CONSOLE_SCREEN_BUFFER_INFO screen{};
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output != INVALID_HANDLE_VALUE &&
        GetConsoleScreenBufferInfo(output, &screen)) {
        const int columns=screen.srWindow.Right-screen.srWindow.Left+1;
        box_width=left_card_width(columns);
        box_margin=2;
    }
}

std::string repeat(const char* glyph, int count)
{
    std::string result;
    for (int i = 0; i < count; ++i)
        result += glyph;
    return result;
}

void top_border(const char* color = LIGHT_PURPLE)
{
    *frame_output << color << std::string(box_margin,' ') << "╭"
                  << repeat("─", box_width) << "╮\n" << RESET;
    ++frame_line;
}

void divider(const char* color = BRIGHT_BLACK)
{
    *frame_output << color << std::string(box_margin,' ') << "├"
                  << repeat("─", box_width) << "┤\n" << RESET;
    ++frame_line;
}

void bottom_border(const char* color = LIGHT_PURPLE)
{
    *frame_output << color << std::string(box_margin,' ') << "╰"
                  << repeat("─", box_width) << "╯\n" << RESET;
    ++frame_line;
}

size_t display_width(const std::string& text)
{
    size_t width = 0;
    for (unsigned char byte : text) {
        if ((byte & 0xC0) != 0x80)
            ++width;
    }
    return width;
}

void trim_to_width(std::string& text, size_t limit)
{
    while (!text.empty() && display_width(text)>limit) {
        size_t start=text.size()-1;
        while (start>0 &&
               (static_cast<unsigned char>(text[start])&0xc0)==0x80)
            --start;
        text.erase(start);
    }
}

void row(const std::string& text = "", const char* color = WHITE)
{
    std::string visible = text;
    trim_to_width(visible,static_cast<size_t>((std::max)(1,box_width-5)));
    if (visible != text)
        visible += "...";
    const int padding = box_width - 2 - static_cast<int>(display_width(visible));
    *frame_output << BRIGHT_BLACK << std::string(box_margin,' ') << "│ "
              << RESET << color << visible
              << std::string((std::max)(0, padding), ' ')
              << BRIGHT_BLACK << " │\n" << RESET;
    ++frame_line;
}

void colored_row(const std::vector<std::pair<std::string,const char*>>& parts)
{
    *frame_output << BRIGHT_BLACK << std::string(box_margin,' ') << "│ " << RESET;
    size_t used=0;
    const size_t limit=static_cast<size_t>((std::max)(1,box_width-2));
    for (const auto& part:parts) {
        std::string value=part.first;
        trim_to_width(value,limit>used ? limit-used : 0);
        *frame_output << part.second << value;
        used+=display_width(value);
    }
    *frame_output << RESET << std::string(limit-used,' ')
                  << BRIGHT_BLACK << " │\n" << RESET;
    ++frame_line;
}

void tweak_row(const Optimizer::TweakSetting& setting, bool selected)
{
    const ViewState state=view_state(setting);
    colored_row({{selected ? "> " : "  ",selected ? LIGHT_PURPLE : BRIGHT_BLACK},
                 {setting.durable.saved ? "* " : "  ",
                  setting.durable.saved ? LIGHT_YELLOW : BRIGHT_BLACK},
                 {"[",BRIGHT_BLACK},
                 {view_label(state),view_color(state)},
                 {"] ",BRIGHT_BLACK},
                 {setting.label,selected ? LIGHT_PURPLE :
                    setting.enabled ? LIGHT_GREEN : BRIGHT_BLACK}});
}

std::vector<std::string> frame_lines(const std::string& frame)
{
    std::vector<std::string> lines;
    size_t start=0;
    for (size_t end=frame.find('\n',start);end!=std::string::npos;
         end=frame.find('\n',start)) {
        lines.push_back(frame.substr(start,end-start));
        start=end+1;
    }
    return lines;
}

void present_frame(const std::string& frame, std::vector<std::string>& previous)
{
    const auto next=frame_lines(frame);
    if (previous.empty()) std::cout << RESET << "\033[2J";
    for (size_t i:changed_frame_rows(previous,next)) {
        std::cout << RESET << "\033[" << (i+1) << ";1H\033[2K";
        if (i<next.size()) std::cout << next[i];
    }
    std::cout << std::flush;
    previous=next;
}

void carousel_row(const CarouselLayout& layout, size_t selected)
{
    *frame_output << BRIGHT_BLACK << std::string(box_margin,' ') << "│ " << RESET
                  << BOLD << LIGHT_PURPLE << "TWEAKS  " << RESET;
    for (size_t tab = 0; tab < layout.tab_columns.size(); ++tab) {
        if (tab)
            *frame_output << " ";
        const auto range = layout.tab_columns[tab];
        const std::string label = layout.text.substr(
            static_cast<size_t>(range.first - (box_margin+2)),
            static_cast<size_t>(range.second - range.first));
        *frame_output << (tab == selected ? BG_PURPLE : BG_BRIGHT_BLACK)
                      << (tab == selected ? BRIGHT_WHITE : LIGHT_CYAN)
                      << BOLD << label << RESET;
    }
    const int padding = box_width - 2 - static_cast<int>(layout.text.size());
    *frame_output << std::string((std::max)(0, padding), ' ')
                  << BRIGHT_BLACK << " │\n" << RESET;
    ++frame_line;
}

void title(const std::string& subtitle)
{
    top_border();
    row("NEBULA",BRIGHT_WHITE);
    if (!subtitle.empty()) row(subtitle,LIGHT_PURPLE);
    divider();
}

std::string shorten_path(const std::string& path, size_t maximum)
{
    if (path.size() <= maximum)
        return path;
    return "..." + path.substr(path.size() - (maximum - 3));
}

bool elevated()
{
    HANDLE token = nullptr;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    const bool success = GetTokenInformation(token, TokenElevation, &elevation,
                                             sizeof(elevation), &size) != FALSE;
    CloseHandle(token);
    return success && elevation.TokenIsElevated != 0;
}

std::string memory_bar(double ratio, int width)
{
    ratio = (std::max)(0.0, (std::min)(1.0, ratio));
    const int filled = static_cast<int>(ratio * width + 0.5);
    return repeat("█", filled) + repeat("░", width - filled);
}

ULONGLONG ticks(const FILETIME& time)
{
    return (static_cast<ULONGLONG>(time.dwHighDateTime) << 32) |
           time.dwLowDateTime;
}

class LatencyCapture {
public:
    ~LatencyCapture()
    {
        if (_process)
            CloseHandle(_process);
    }

    bool start(DWORD pid)
    {
        if (!pid || _process)
            return false;
        char module[MAX_PATH * 4]{};
        GetModuleFileNameA(nullptr, module, sizeof(module));
        std::filesystem::path executable =
            std::filesystem::path(module).parent_path() / "PresentMon.exe";
        if (!std::filesystem::exists(executable)) {
            char found[MAX_PATH * 4]{};
            const DWORD length = SearchPathA(nullptr, "PresentMon.exe", nullptr,
                                             sizeof(found), found, nullptr);
            if (!length || length >= sizeof(found)) {
                _status = "PresentMon.exe not found";
                return false;
            }
            executable = found;
        }
        char local[MAX_PATH]{};
        if (!GetEnvironmentVariableA("LOCALAPPDATA", local, sizeof(local))) {
            _status = "AppData unavailable";
            return false;
        }
        const std::filesystem::path directory =
            std::filesystem::path(local) / "NebulaOptimizer";
        try {
            std::filesystem::create_directories(directory);
        } catch (...) {
            _status = "Capture folder unavailable";
            return false;
        }
        _csv_path = (directory /
            ("presentmon-" + std::to_string(pid) + "-" +
             std::to_string(GetTickCount64()) + ".csv")).string();
        const std::string command = "\"" + executable.string() +
            "\" --process_id " + std::to_string(pid) +
            " --output_file \"" + _csv_path +
            "\" --no_console_stats --timed 10 --terminate_after_timed";
        std::vector<char> mutable_command(command.begin(), command.end());
        mutable_command.push_back('\0');
        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        if (!CreateProcessA(executable.string().c_str(), mutable_command.data(),
                            nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                            nullptr, nullptr, &startup, &child)) {
            _status = "PresentMon could not start";
            return false;
        }
        CloseHandle(child.hThread);
        _process = child.hProcess;
        _status = "Capturing input-to-display (10 s)";
        return true;
    }

    void poll()
    {
        if (!_process)
            return;
        DWORD code = STILL_ACTIVE;
        if (!GetExitCodeProcess(_process, &code) || code == STILL_ACTIVE)
            return;
        CloseHandle(_process);
        _process = nullptr;
        if (code != 0) {
            _status = "PresentMon capture failed";
            return;
        }
        read_result();
    }

    const std::string& status() const { return _status; }

private:
    void read_result()
    {
        std::ifstream file(_csv_path);
        std::string line;
        if (!file || !std::getline(file, line)) {
            _status = "No PresentMon data";
            return;
        }
        std::stringstream headers(line);
        std::string column;
        size_t metric_index = 0;
        bool found = false;
        while (std::getline(headers, column, ',')) {
            if (column == "MsAllInputToPhotonLatency") {
                found = true;
                break;
            }
            ++metric_index;
        }
        if (!found) {
            _status = "Input metric unavailable";
            return;
        }
        std::deque<double> samples;
        double total = 0.0;
        while (std::getline(file, line)) {
            std::stringstream fields(line);
            size_t index = 0;
            while (index <= metric_index && std::getline(fields, column, ',')) {
                if (index++ != metric_index)
                    continue;
                try {
                    const double value = std::stod(column);
                    if (std::isfinite(value) && value > 0.0) {
                        samples.push_back(value);
                        total += value;
                        if (samples.size() > 100) {
                            total -= samples.front();
                            samples.pop_front();
                        }
                    }
                } catch (...) {}
            }
        }
        if (samples.empty()) {
            _status = "No input samples";
            return;
        }
        std::ostringstream measured;
        measured << "Input-to-display: " << std::fixed << std::setprecision(1)
                 << total / samples.size() << " ms (PresentMon)";
        _status = measured.str();
    }

    HANDLE _process = nullptr;
    std::string _csv_path;
    std::string _status = "Input-to-display: not measured";
};

} // namespace

class TrayIcon {
public:
    TrayIcon() : _window(nullptr), _show(false), _stop(false), _ready(false)
    {
        _thread = std::thread([this] { message_loop(); });
        while (!_ready.load())
            Sleep(1);
    }

    ~TrayIcon()
    {
        if (_window)
            PostMessageA(_window, WM_CLOSE, 0, 0);
        if (_thread.joinable())
            _thread.join();
    }

    bool show_requested() { return _show.exchange(false); }
    bool stop_requested() { return _stop.exchange(false); }
    void notify_restored() const
    {
        if (!_window)
            return;
        NOTIFYICONDATAA icon{};
        icon.cbSize = sizeof(icon);
        icon.hWnd = _window;
        icon.uID = 1;
        icon.uFlags = NIF_INFO;
        icon.dwInfoFlags = NIIF_INFO;
        strcpy_s(icon.szInfoTitle, "Nebula");
        strcpy_s(icon.szInfo, "Settings restored");
        Shell_NotifyIconA(NIM_MODIFY, &icon);
    }

private:
    static constexpr UINT CALLBACK_MESSAGE = WM_APP + 7;
    static constexpr UINT SHOW_COMMAND = 101;
    static constexpr UINT STOP_COMMAND = 102;

    static LRESULT CALLBACK window_proc(HWND window, UINT message,
                                        WPARAM wparam, LPARAM lparam)
    {
        TrayIcon* tray = reinterpret_cast<TrayIcon*>(
            GetWindowLongPtrA(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTA*>(lparam);
            SetWindowLongPtrA(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return TRUE;
        }
        if (!tray)
            return DefWindowProcA(window, message, wparam, lparam);
        if (message == CALLBACK_MESSAGE) {
            if (lparam == WM_LBUTTONDBLCLK || lparam == WM_LBUTTONUP)
                tray->_show.store(true);
            if (lparam == WM_RBUTTONUP) {
                HMENU menu = CreatePopupMenu();
                AppendMenuA(menu, MF_STRING, SHOW_COMMAND, "Open Nebula");
                AppendMenuA(menu, MF_STRING, STOP_COMMAND, "Stop and restore");
                POINT cursor{};
                GetCursorPos(&cursor);
                SetForegroundWindow(window);
                TrackPopupMenu(menu, TPM_RIGHTBUTTON, cursor.x, cursor.y,
                               0, window, nullptr);
                DestroyMenu(menu);
            }
            return 0;
        }
        if (message == WM_COMMAND) {
            if (LOWORD(wparam) == SHOW_COMMAND)
                tray->_show.store(true);
            if (LOWORD(wparam) == STOP_COMMAND)
                tray->_stop.store(true);
            return 0;
        }
        if (message == WM_CLOSE) {
            DestroyWindow(window);
            return 0;
        }
        if (message == WM_DESTROY) {
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcA(window, message, wparam, lparam);
    }

    void message_loop()
    {
        HINSTANCE instance = GetModuleHandleA(nullptr);
        WNDCLASSA klass{};
        klass.lpfnWndProc = window_proc;
        klass.hInstance = instance;
        klass.lpszClassName = "NebulaOptimizerTray";
        RegisterClassA(&klass);
        HWND window = CreateWindowExA(WS_EX_TOOLWINDOW, klass.lpszClassName,
            "Nebula tray", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
            instance, this);
        _window = window;
        if (!window) {
            _ready.store(true);
            return;
        }
        NOTIFYICONDATAA icon{};
        icon.cbSize = sizeof(icon);
        icon.hWnd = window;
        icon.uID = 1;
        icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        icon.uCallbackMessage = CALLBACK_MESSAGE;
        icon.hIcon = LoadIconA(instance, MAKEINTRESOURCEA(1));
        if (!icon.hIcon)
            icon.hIcon = LoadIconA(nullptr, IDI_APPLICATION);
        strcpy_s(icon.szTip, "Nebula - active");
        Shell_NotifyIconA(NIM_ADD, &icon);
        _ready.store(true);
        MSG message{};
        while (GetMessageA(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
        Shell_NotifyIconA(NIM_DELETE, &icon);
    }

    std::thread _thread;
    HWND _window;
    std::atomic<bool> _show;
    std::atomic<bool> _stop;
    std::atomic<bool> _ready;
};

Tui::Tui()
    : _is_running(true), _current_screen(STARTUP)
{
    init();
    _tray = std::make_unique<TrayIcon>();
    select_default_profile();
    if (!_selected_profile_path.empty())
        _optimizer.scan_saved(_selected_profile_path);
}

Tui::~Tui()
{
    _optimizer.restore();
    std::cout << "\033[0m\033[?25h";
}

void Tui::emergency_restore()
{
    const bool was_active = _optimizer.is_optimized();
    _optimizer.restore();
    if (was_active && _optimizer.restoration_succeeded() &&
        !_restore_notice_sent.exchange(true) && _tray) {
        _tray->notify_restored();
        Sleep(1000);
    }
}

void Tui::init()
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleTitleA("Nebula Optimizer");

    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (output != INVALID_HANDLE_VALUE && GetConsoleMode(output, &mode)) {
        SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        CONSOLE_SCREEN_BUFFER_INFOEX info{};
        info.cbSize = sizeof(info);
        if (elevated() && GetConsoleScreenBufferInfoEx(output, &info)) {
            const COLORREF palette[16] = {
                RGB(0, 0, 0), RGB(70, 95, 180), RGB(70, 155, 125),
                RGB(65, 155, 180), RGB(195, 75, 85), RGB(145, 95, 190),
                RGB(200, 160, 75), RGB(200, 210, 225), RGB(105, 120, 140),
                RGB(105, 145, 230), RGB(108, 214, 174), RGB(121, 198, 220),
                RGB(244, 117, 130), RGB(163, 148, 243), RGB(230, 194, 120),
                RGB(238, 243, 250)
            };
            std::copy(std::begin(palette), std::end(palette), info.ColorTable);
            SetConsoleScreenBufferInfoEx(output, &info);
        }
        SetConsoleTextAttribute(output, FOREGROUND_RED | FOREGROUND_GREEN |
                                        FOREGROUND_BLUE);
    }
    std::cout << RESET;
}

void Tui::clear_screen()
{
    update_layout();
    std::cout << RESET << "\033[2J\033[H";
}

std::string Tui::read_input(const std::string& prompt)
{
    std::cout << LIGHT_CYAN << BOLD << "\n"
              << std::string(box_margin+2,' ') << "> " << RESET << prompt << " ";
    std::string value;
    std::getline(std::cin, value);
    return value;
}

void Tui::pause(const std::string& message)
{
    read_input(message);
}

void Tui::select_default_profile()
{
    const std::vector<std::string> profiles = get_profiles_list();
    if (profiles.empty()) {
        _selected_profile.clear();
        _selected_profile_path.clear();
        return;
    }
    if (_selected_profile.empty() ||
        std::find(profiles.begin(), profiles.end(), _selected_profile) == profiles.end())
        _selected_profile = profiles.front();
    _selected_profile_path = load_profile_game_path(_selected_profile);
}

void Tui::startup_screen()
{
    clear_screen();
    title("");
    const auto settings=_optimizer.tweak_settings();
    size_t base_count=0, saved_count=0;
    for (const auto& item:settings) {
        if (item.durable.base) ++base_count;
        if (item.durable.saved) ++saved_count;
    }

    if (_selected_profile.empty()) {
        row("No profile", LIGHT_YELLOW);
    } else {
        row("Profile: " + _selected_profile, LIGHT_GREEN);
        row("Auto-detect: on", BRIGHT_BLACK);
    }

    divider();
    if (_selected_profile.empty())
        row("[1] Create profile",LIGHT_GREEN);
    else
        row("[1] Play / optimize",LIGHT_GREEN);
    row("[2] Profiles",WHITE);
    row("[3] Optimize Windows",WHITE);
    row("[4] About tweaks",WHITE);
    row("[S] Scan PC",LIGHT_CYAN);
    row("[M] Hide to tray",WHITE);
    if (_optimizer.recovery_blocked())
        row("[R] Retry restore (close the game first)",LIGHT_YELLOW);
    row("[0] Exit",BRIGHT_BLACK);
    divider();
    row(std::string("Admin: ") + (elevated() ? "on" : "off"),
        elevated() ? LIGHT_GREEN : LIGHT_YELLOW);
    row("Base: " + std::to_string(base_count) +
        "  |  Saved: " + std::to_string(saved_count),LIGHT_CYAN);
    if (!_scan_message.empty()) row(_scan_message, LIGHT_YELLOW);
    if (!_optimizer.restoration_status().empty())
        row(_optimizer.restoration_status(),
            _optimizer.restoration_succeeded() ? LIGHT_GREEN : LIGHT_RED);
    bottom_border();

    std::cout << "\n" << std::string(box_margin+2,' ') << "> ";
    std::cout.flush();
    std::string choice;
    ULONGLONG next_scan = 0;
    while (_is_running) {
        if (_tray->stop_requested()) {
            _current_screen = EXIT;
            return;
        }
        if (_tray->show_requested()) {
            HWND window = GetConsoleWindow();
            if (window) {
                ShowWindow(window, SW_SHOW);
                ShowWindow(window, SW_RESTORE);
                SetForegroundWindow(window);
            }
        }
        HWND window = GetConsoleWindow();
        if (window && IsIconic(window))
            ShowWindow(window, SW_HIDE);
        if (_kbhit()) {
            std::getline(std::cin, choice);
            break;
        }
        const ULONGLONG now = GetTickCount64();
        if (!_optimizer.recovery_blocked() &&
            !_selected_profile_path.empty() && now >= next_scan) {
            const bool running = _optimizer.is_game_active(_selected_profile_path);
            if (!running)
                _suppress_auto_attach = false;
            else if (!_suppress_auto_attach) {
                _launch_path = _selected_profile_path;
                _attach_only = true;
                _current_screen = LAUNCH_GAME;
                return;
            }
            next_scan = now + 2500;
        }
        Sleep(250);
    }
    if (choice == "r" || choice == "R") {
        _optimizer.retry_recovery();
    } else if (choice == "s" || choice == "S") {
        if (_optimizer.recovery_blocked())
            _scan_message="Recovery required before scan";
        else
            _scan_message=_optimizer.scan_saved(_selected_profile_path) ?
                "Scan complete" : "Scan failed: " + _optimizer.saved_error();
    } else if ((choice == "1" || choice == "3") &&
               _optimizer.recovery_blocked()) {
        // Keep the warning visible; do not start an automatic retry loop.
    } else if (choice == "1") {
        if (_selected_profile.empty())
            _current_screen = CREATE_PROFILE;
        else {
            _launch_path = _selected_profile_path;
            _attach_only = false;
            _current_screen = LAUNCH_GAME;
        }
    } else if (choice == "2") {
        _current_screen = PROFILE_SELECT;
    } else if (choice == "3") {
        _launch_path.clear();
        _attach_only = false;
        _current_screen = LAUNCH_GAME;
    } else if (choice == "4") {
        _current_screen = SETTINGS;
    } else if (choice == "0") {
        _current_screen = EXIT;
    } else if (choice == "m" || choice == "M") {
        HWND window = GetConsoleWindow();
        if (window)
            ShowWindow(window, SW_HIDE);
    }
}

void Tui::profiles_screen()
{
    clear_screen();
    const std::vector<std::string> profiles = get_profiles_list();
    title("Profiles");

    if (profiles.empty()) {
        row("No profiles", LIGHT_YELLOW);
    } else {
        row("Select a profile:",BRIGHT_BLACK);
        for (size_t i = 0; i < profiles.size(); ++i) {
            const bool selected = profiles[i] == _selected_profile;
            std::ostringstream item;
            item << "[" << (i + 1) << "]  " << profiles[i];
            if (selected) item << "   < ACTIVE";
            row(item.str(),selected ? LIGHT_GREEN : WHITE);
        }
    }

    divider();
    row("[A] Add profile",LIGHT_CYAN);
    if (!profiles.empty())
        row("[S] Delete profile",LIGHT_RED);
    row("[0] Back",BRIGHT_BLACK);
    bottom_border();

    const std::string choice = read_input("Choice:");
    if (choice == "A" || choice == "a") {
        _current_screen = CREATE_PROFILE;
        return;
    }
    if (choice == "S" || choice == "s") {
        delete_profile_flow();
        return;
    }
    if (choice == "0") {
        _current_screen = STARTUP;
        return;
    }

    try {
        const size_t index = static_cast<size_t>(std::stoul(choice));
        if (index >= 1 && index <= profiles.size()) {
            _selected_profile = profiles[index - 1];
            _selected_profile_path = load_profile_game_path(_selected_profile);
            _suppress_auto_attach = false;
            _current_screen = STARTUP;
        }
    } catch (...) {}
}

void Tui::create_profile_screen()
{
    clear_screen();
    title("New profile");
    row("Name + game .exe",WHITE);
    row("Drag the .exe into this window",BRIGHT_BLACK);
    bottom_border();

    const std::string name = read_input("Name:");
    if (name.empty()) {
        _current_screen = PROFILE_SELECT;
        return;
    }

    std::string path = read_input("Game .exe:");
    if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
        path = path.substr(1, path.size() - 2);

    if (!std::filesystem::exists(path) ||
        std::filesystem::is_directory(path) ||
        std::filesystem::path(path).extension() != ".exe") {
        std::cout << LIGHT_RED << "\n  Invalid .exe.\n" << RESET;
        pause();
        _current_screen = CREATE_PROFILE;
        return;
    }

    if (!create_profile(name, path)) {
        std::cout << LIGHT_RED << "\n  Could not save profile.\n" << RESET;
        pause();
        _current_screen = PROFILE_SELECT;
        return;
    }

    _selected_profile = name;
    _selected_profile_path = path;
    std::cout << LIGHT_GREEN << BOLD << "\n  Profile saved.\n" << RESET;
    pause();
    _current_screen = STARTUP;
}

void Tui::delete_profile_flow()
{
    const std::vector<std::string> profiles = get_profiles_list();
    if (profiles.empty()) {
        _current_screen = PROFILE_SELECT;
        return;
    }

    const std::string value = read_input("Profile number:");
    try {
        const size_t index = static_cast<size_t>(std::stoul(value));
        if (index < 1 || index > profiles.size())
            return;
        const std::string confirm = read_input("Type DELETE to confirm:");
        if (confirm == "DELETE" && delete_profile(profiles[index - 1])) {
            if (_selected_profile == profiles[index - 1])
                _selected_profile.clear();
            select_default_profile();
            std::cout << LIGHT_GREEN << "\n  Profile deleted.\n" << RESET;
            pause();
        }
    } catch (...) {}
    _current_screen = PROFILE_SELECT;
}

void Tui::optimization_info_screen()
{
    clear_screen();
    title("Tweaks");
    row("Power plan / game priority / GPU preference",WHITE);
    row("Game Mode / capture / foreground scheduling",WHITE);
    row("Fortnite: use NVIDIA Reflex On + Boost in-game",LIGHT_CYAN);
    row("Temporary changes. Restored when the game closes.",BRIGHT_BLACK);
    row("Fortnite anti-cheat is never tuned.",BRIGHT_BLACK);
    bottom_border();
    pause();
    _current_screen = STARTUP;
}

void Tui::launch_current_mode()
{
    clear_screen();
    title("Starting");
    row(_launch_path.empty() ? "Windows only" :
        "Game: " + shorten_path(_launch_path, 55), LIGHT_CYAN);
    bottom_border();

    if (_optimizer.scan_saved(_launch_path) &&
        _optimizer.optimize(_launch_path, !_attach_only)) {
        _restore_notice_sent.store(false);
        _current_screen = OPTIMIZATION_MONITOR;
        return;
    }

    if (!_attach_only) {
        std::cout << LIGHT_RED << "\n  " << _optimizer.last_error() << "\n" << RESET;
        pause();
    }
    _current_screen = STARTUP;
}

void Tui::optimization_monitor_screen()
{
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO original_cursor{};
    GetConsoleCursorInfo(console, &original_cursor);
    CONSOLE_CURSOR_INFO hidden_cursor = original_cursor;
    hidden_cursor.bVisible = FALSE;
    SetConsoleCursorInfo(console, &hidden_cursor);
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD original_input_mode = 0;
    const bool mouse_enabled = input != INVALID_HANDLE_VALUE &&
        GetConsoleMode(input, &original_input_mode) &&
        SetConsoleMode(input, (original_input_mode | ENABLE_MOUSE_INPUT |
                               ENABLE_EXTENDED_FLAGS) & ~ENABLE_QUICK_EDIT_MODE);
    HWND console_window = GetConsoleWindow();
    TrayIcon& tray = *_tray;
    LatencyCapture latency;

    const ULONGLONG started = GetTickCount64();
    bool active = true;
    bool game_ended = false;
    bool expanded = true;
    size_t selected_category = 0;
    size_t selected_tweak = 0;
    size_t scroll = 0;
    size_t visible_rows = 8;
    SHORT carousel_y = 0;
    CarouselLayout carousel;
    std::string tweak_message;
    ULONGLONG previous_idle = 0;
    ULONGLONG previous_total = 0;
    ULONGLONG previous_self = 0;
    double cpu_percent = 0.0;
    double nebula_percent = 0.0;
    std::vector<std::string> previous_frame;
    while (active) {
        if (tray.stop_requested())
            break;
        if (tray.show_requested() && console_window) {
            ShowWindow(console_window, SW_SHOW);
            ShowWindow(console_window, SW_RESTORE);
            SetForegroundWindow(console_window);
        }
        if (console_window && IsIconic(console_window))
            ShowWindow(console_window, SW_HIDE);
        CONSOLE_SCREEN_BUFFER_INFO screen_info{};
        if (GetConsoleScreenBufferInfo(console, &screen_info)) {
            const int height = screen_info.srWindow.Bottom -
                               screen_info.srWindow.Top + 1;
            visible_rows=static_cast<size_t>((std::max)(1,(std::min)(8,
                height-(tweak_message.empty() ? 22 : 23))));
        }
        update_layout();
        if (expanded)
            scroll = scroll_to_cursor(scroll, selected_tweak, visible_rows);
        if ((_optimizer.has_game_process() || _optimizer.is_waiting_for_game()) &&
            !_optimizer.is_game_running()) {
            game_ended = true;
            break;
        }

        if (console_window && !IsWindowVisible(console_window)) {
            Sleep(1500);
            continue;
        }
        latency.poll();

        MEMORYSTATUSEX memory{};
        memory.dwLength = sizeof(memory);
        GlobalMemoryStatusEx(&memory);
        const double used_ratio = memory.ullTotalPhys == 0 ? 0.0 :
            1.0 - static_cast<double>(memory.ullAvailPhys) /
                  static_cast<double>(memory.ullTotalPhys);
        const ULONGLONG seconds = (GetTickCount64() - started) / 1000;

        FILETIME idle{}, kernel{}, user{}, created{}, exited{}, self_kernel{}, self_user{};
        if (GetSystemTimes(&idle, &kernel, &user) &&
            GetProcessTimes(GetCurrentProcess(), &created, &exited,
                            &self_kernel, &self_user)) {
            const ULONGLONG idle_now = ticks(idle);
            const ULONGLONG total_now = ticks(kernel) + ticks(user);
            const ULONGLONG self_now = ticks(self_kernel) + ticks(self_user);
            if (previous_total && total_now > previous_total) {
                const double elapsed = static_cast<double>(total_now - previous_total);
                cpu_percent = 100.0 * (1.0 -
                    static_cast<double>(idle_now - previous_idle) / elapsed);
                nebula_percent = 100.0 *
                    static_cast<double>(self_now - previous_self) / elapsed;
            }
            previous_idle = idle_now;
            previous_total = total_now;
            previous_self = self_now;
        }

        PROCESS_MEMORY_COUNTERS_EX own_memory{};
        own_memory.cb = sizeof(own_memory);
        GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&own_memory),
                             sizeof(own_memory));

        std::ostringstream uptime;
        uptime << std::setfill('0') << std::setw(2) << (seconds / 3600) << ":"
               << std::setw(2) << ((seconds / 60) % 60) << ":"
               << std::setw(2) << (seconds % 60);
        std::ostringstream ram;
        ram << "RAM  " << memory_bar(used_ratio,box_width<52 ? 10 : 22) << "  "
            << std::fixed << std::setprecision(1) << used_ratio * 100.0 << "%";
        std::ostringstream cpu;
        cpu << "CPU: " << std::fixed << std::setprecision(1)
            << (std::max)(0.0, (std::min)(100.0, cpu_percent)) << "%";
        std::ostringstream own;
        own << "Nebula: " << std::fixed << std::setprecision(2)
            << nebula_percent << "% CPU  /  "
            << (own_memory.WorkingSetSize / (1024 * 1024)) << " MB";

        std::ostringstream frame;
        frame_output = &frame;
        frame_line = 0;
        title("Active session");
        row(_optimizer.game_status(),LIGHT_GREEN);
        row("Time: " + uptime.str(),BRIGHT_BLACK);
        divider();
        row(ram.str(), LIGHT_CYAN);
        row(cpu.str(),LIGHT_CYAN);
        row(own.str(),BRIGHT_BLACK);
        row(_optimizer.game_pid() ? latency.status() + "  [P] Capture 10 s" :
            "Input-to-display: no game attached", BRIGHT_BLACK);
        divider();
        const std::vector<Optimizer::TweakSetting> settings = _optimizer.tweak_settings();
        const ViewCounts counts=view_counts(settings);
        colored_row({{"Applied: " + std::to_string(counts.applied),LIGHT_GREEN},
                     {"   |   Skipped: " + std::to_string(counts.skipped),LIGHT_YELLOW},
                     {"   |   Failed: " + std::to_string(counts.failed),LIGHT_RED}});
        std::vector<size_t> category_tweaks;
        if (selected_category == std::size(TWEAK_CATEGORIES)-1) {
            for (size_t n=0;n<settings.size();++n)
                if (settings[n].durable.saved) category_tweaks.push_back(n);
        } else category_tweaks = tweaks_in_category(
            settings, TWEAK_CATEGORIES[selected_category]);
        selected_tweak = move_tweak_cursor(selected_tweak, 0, category_tweaks.size());
        if (expanded) {
            carousel_y = screen_info.srWindow.Top + static_cast<SHORT>(frame_line);
            if (box_width < 34) {
                carousel.tab_columns.clear();
                row(std::string("< ")+TWEAK_TABS[selected_category]+" >",LIGHT_PURPLE);
            } else {
                const std::vector<std::string> tabs = box_width < 50 ?
                    std::vector<std::string>{"C","G","R","A","I","S"} :
                    std::vector<std::string>(std::begin(TWEAK_TABS),
                                             std::end(TWEAK_TABS));
                carousel = make_carousel(tabs,box_margin+2);
                carousel_row(carousel, selected_category);
            }
            for (size_t position = scroll;
                 position < category_tweaks.size() && position < scroll + visible_rows;
                 ++position) {
                const auto& setting = settings[category_tweaks[position]];
                const bool selected = position == selected_tweak;
                tweak_row(setting,selected);
            }
            const std::string marker=category_tweaks.empty() ? "" :
                settings[category_tweaks[selected_tweak]].durable.base ?
                "   BASE" : "";
            colored_row({{TWEAK_CATEGORIES[selected_category],LIGHT_PURPLE},
                         {"   " + std::to_string(category_tweaks.empty() ? 0 : scroll+1) +
                          "-" + std::to_string((std::min)(scroll+visible_rows,
                                                        category_tweaks.size())) +
                          "/" + std::to_string(category_tweaks.size())+marker,
                          BRIGHT_BLACK}});
        }
        divider();
        if (!tweak_message.empty())
            row(tweak_message, LIGHT_YELLOW);
        row(expanded ?
            "[Left/Right] Category  [Up/Down] Tweak  [Enter] Toggle" :
            "[L] Tweaks",WHITE);
        if (expanded) {
            const bool has_drift=!category_tweaks.empty() &&
                settings[category_tweaks[selected_tweak]].durable.drift;
            row(std::string("[S] Save/Unsave") +
                (has_drift ? "  [R] Reapply drift" : "") +
                "  [Esc] Hide",BRIGHT_BLACK);
        }
        row("[M] Hide to tray",WHITE);
        row("[0] Stop and restore",LIGHT_RED);
        bottom_border();
        frame_output = &std::cout;
        present_frame(frame.str(),previous_frame);

        for (int i = 0; i < 10 && active; ++i) {
            Sleep(100);
            if (tray.stop_requested())
                active = false;
            bool hover_changed = false;
            if (mouse_enabled) {
                for (int event_index = 0; event_index < 32; ++event_index) {
                    INPUT_RECORD event{};
                    DWORD available = 0;
                    if (!PeekConsoleInputA(input, &event, 1, &available) ||
                        !available)
                        break;
                    if (event.EventType == KEY_EVENT && event.Event.KeyEvent.bKeyDown)
                        break;
                    DWORD consumed = 0;
                    if (!ReadConsoleInputA(input, &event, 1, &consumed) || !consumed)
                        break;
                    if (!expanded || event.EventType != MOUSE_EVENT ||
                        event.Event.MouseEvent.dwMousePosition.Y != carousel_y)
                        continue;
                    const int column = event.Event.MouseEvent.dwMousePosition.X;
                    const size_t tab = carousel_tab_at(carousel, column);
                    if (tab < carousel.tab_columns.size() &&
                        tab != selected_category) {
                        selected_category = tab;
                        selected_tweak = 0;
                        scroll = 0;
                        hover_changed = true;
                    }
                }
            }
            if (hover_changed)
                break;
            if (_kbhit()) {
                const int key = _getch();
                if (key == '0')
                    active = false;
                else if (key == 'l' || key == 'L') {
                    expanded = !expanded;
                    break;
                } else if (expanded && (key == 27 || key == 8)) {
                    expanded = false;
                    break;
                } else if (expanded && (key == 'j' || key == 'J' ||
                                        key == 'k' || key == 'K' ||
                                        key == 0 || key == 224)) {
                    int direction = 0;
                    int category_direction = 0;
                    if (key == 'j' || key == 'J')
                        direction = 1;
                    else if (key == 'k' || key == 'K')
                        direction = -1;
                    else {
                        const int arrow = _getch();
                        direction = arrow == 80 ? 1 : (arrow == 72 ? -1 : 0);
                        category_direction = arrow == 77 ? 1 :
                                             (arrow == 75 ? -1 : 0);
                    }
                    if (category_direction) {
                        selected_category = move_carousel(
                            selected_category, category_direction,
                            std::size(TWEAK_CATEGORIES));
                        selected_tweak = 0;
                        scroll = 0;
                    } else {
                        selected_tweak = move_tweak_cursor(
                            selected_tweak, direction, category_tweaks.size());
                        scroll = scroll_to_cursor(scroll, selected_tweak,
                                                  visible_rows);
                    }
                    break;
                } else if (expanded && key == 13) {
                    if (category_tweaks.empty() ||
                        !_optimizer.toggle_tweak(category_tweaks[selected_tweak])) {
                        tweak_message = "Could not save tweak settings";
                    } else if (_optimizer.is_waiting_for_game()) {
                        tweak_message = "Saved for the next session";
                    } else {
                        _optimizer.restore();
                        if (!_optimizer.restoration_succeeded() ||
                            !_optimizer.optimize(_launch_path, false)) {
                            tweak_message = "Could not reapply session";
                            active = false;
                        } else {
                            tweak_message = "Tweak updated";
                        }
                    }
                    break;
                } else if (expanded && (key == 's' || key == 'S' ||
                                        key == 'r' || key == 'R')) {
                    if (category_tweaks.empty()) { tweak_message="No tweak selected"; break; }
                    const size_t index=category_tweaks[selected_tweak];
                    const auto& item=settings[index];
                    const bool reapply=key=='r' || key=='R';
                    if (!item.durable.eligible || (!item.enabled && !item.durable.saved)) {
                        tweak_message="Enable a durable tweak first"; break;
                    }
                    if (reapply && (!item.durable.saved || !item.durable.drift)) {
                        tweak_message="No saved drift to reapply"; break;
                    }
                    if (!reapply && item.durable.saved) {
                        tweak_message=_optimizer.unsave_tweak(index) ?
                            "Unsaved; Windows value kept" : "Could not unsave";
                        break;
                    }
                    const bool sensitive=index==28 || index==37 || index==39 || index==40;
                    bool confirmed=!sensitive;
                    if (sensitive) {
                        std::cout << "\nSave this setting permanently? [Y/N] " << std::flush;
                        const int answer=_getch();
                        confirmed=answer=='y' || answer=='Y';
                        previous_frame.clear();
                    }
                    if (!confirmed) { tweak_message="Cancelled"; break; }
                    _optimizer.restore();
                    if (!_optimizer.restoration_succeeded()) {
                        tweak_message="Restore incomplete; save blocked";
                        active=false; break;
                    }
                    const bool saved=reapply ?
                        _optimizer.reapply_saved_tweak(index,_launch_path,confirmed,
                            index==28 ? confirm_display_mode : nullptr) :
                        _optimizer.save_tweak(index,_launch_path,confirmed,
                            index==28 ? confirm_display_mode : nullptr);
                    previous_frame.clear();
                    if (!_optimizer.optimize(_launch_path,false)) {
                        tweak_message="Session could not restart"; active=false;
                    } else tweak_message=saved ? "Saved" :
                        ("Save failed: " + _optimizer.saved_error());
                    break;
                } else if (key == 'p' || key == 'P') {
                    latency.start(_optimizer.game_pid());
                    break;
                } else if ((key == 'm' || key == 'M') && console_window) {
                    ShowWindow(console_window, SW_HIDE);
                    break;
                }
            }
        }
    }

    _optimizer.restore();
    if (_optimizer.restoration_succeeded() && !_restore_notice_sent.exchange(true))
        tray.notify_restored();
    _suppress_auto_attach = !game_ended && !_launch_path.empty();
    if (mouse_enabled)
        SetConsoleMode(input, original_input_mode);
    SetConsoleCursorInfo(console, &original_cursor);
    _current_screen = STARTUP;
}

void Tui::run()
{
    while (_is_running) {
        switch (_current_screen) {
        case STARTUP: startup_screen(); break;
        case PROFILE_SELECT: profiles_screen(); break;
        case CREATE_PROFILE: create_profile_screen(); break;
        case SETTINGS: optimization_info_screen(); break;
        case LAUNCH_GAME: launch_current_mode(); break;
        case OPTIMIZATION_MONITOR: optimization_monitor_screen(); break;
        case EXIT:
            _optimizer.restore();
            clear_screen();
            std::cout << LIGHT_PURPLE << "\n  Nebula closed.\n\n" << RESET;
            _is_running = false;
            break;
        default:
            _optimizer.restore();
            _is_running = false;
            break;
        }
    }
}
