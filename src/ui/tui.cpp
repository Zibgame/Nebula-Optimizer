#include "tui.hpp"
#include "presentmon_metrics.hpp"
#include "tui_colors.hpp"
#include "tweak_navigation.hpp"
#include "tui_view.hpp"
#include "impact_estimate.hpp"
#include "json.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <conio.h>
#include <filesystem>
#include <fstream>
#include <deque>
#include <cmath>
#include <cwchar>
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
    "Input & Capture", "Network", "Saved"
};
constexpr const char* TWEAK_TABS[] = {
    "CPU", "GPU", "RAM", "Apps", "Input", "Net", "Saved"
};
constexpr size_t CLOSE_APPS_ROW = static_cast<size_t>(-1);

enum class MenuKey { None, Up, Down, Left, Right, Select, Back };
enum class SessionAction { ApplyAll, Save, Reapply, Capture, Benchmark, Tray, Stop };

void wait_for_menu_key(DWORD timeout)
{
    if (_kbhit()) return;
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (!input || input == INVALID_HANDLE_VALUE) {
        Sleep(timeout);
        return;
    }
    for (int index = 0; index < 32; ++index) {
        INPUT_RECORD event{};
        DWORD available = 0;
        if (!PeekConsoleInputA(input, &event, 1, &available) || !available)
            break;
        if (event.EventType == KEY_EVENT && event.Event.KeyEvent.bKeyDown)
            return;
        DWORD consumed = 0;
        if (!ReadConsoleInputA(input, &event, 1, &consumed) || !consumed)
            break;
    }
    if (!_kbhit() && WaitForSingleObject(input, timeout) == WAIT_FAILED)
        Sleep(timeout);
}

class MenuCursorGuard {
public:
    MenuCursorGuard() : _output(GetStdHandle(STD_OUTPUT_HANDLE))
    {
        if (_output == INVALID_HANDLE_VALUE ||
            !GetConsoleCursorInfo(_output, &_original)) return;
        CONSOLE_CURSOR_INFO hidden = _original;
        hidden.bVisible = FALSE;
        _active = SetConsoleCursorInfo(_output, &hidden);
    }

    ~MenuCursorGuard()
    {
        if (_active) SetConsoleCursorInfo(_output, &_original);
    }

private:
    HANDLE _output;
    CONSOLE_CURSOR_INFO _original{};
    bool _active = false;
};

const char* action_label(SessionAction action, bool saved = false)
{
    switch (action) {
    case SessionAction::ApplyAll: return "Apply All";
    case SessionAction::Save: return saved ? "Unsave" : "Save";
    case SessionAction::Reapply: return "Reapply";
    case SessionAction::Capture: return "Capture";
    case SessionAction::Benchmark: return "A/B test";
    case SessionAction::Tray: return "Tray";
    case SessionAction::Stop: return "Stop";
    }
    return "";
}

MenuKey read_menu_key()
{
    const int key = _getch();
    if (key == 0 || key == 224) {
        switch (_getch()) {
        case 72: return MenuKey::Up;
        case 80: return MenuKey::Down;
        case 75: return MenuKey::Left;
        case 77: return MenuKey::Right;
        default: return MenuKey::None;
        }
    }
    switch (key) {
    case 'j': case 'J': return MenuKey::Down;
    case 'k': case 'K': return MenuKey::Up;
    case 'h': case 'H': return MenuKey::Left;
    case 'l': case 'L': return MenuKey::Right;
    case 13: return MenuKey::Select;
    case 27: case 8: return MenuKey::Back;
    default: return MenuKey::None;
    }
}

bool confirm_display_mode()
{
    bool keep = false;
    const ULONGLONG deadline = GetTickCount64() + 15000;
    while (GetTickCount64() < deadline) {
        const auto seconds = (deadline - GetTickCount64() + 999) / 1000;
        std::cout << "\r\033[2KKeep display mode? " <<
            (keep ? " Revert  > Keep" : "> Revert    Keep") <<
            "  " << seconds << "s  [Enter]" << std::flush;
        if (_kbhit()) {
            const MenuKey key = read_menu_key();
            if (key == MenuKey::Right || key == MenuKey::Down) keep = true;
            if (key == MenuKey::Left || key == MenuKey::Up) keep = false;
            if (key == MenuKey::Select) return keep;
            if (key == MenuKey::Back) return false;
        }
        wait_for_menu_key(50);
    }
    return false;
}
std::ostream* frame_output = &std::cout;
size_t frame_line = 0;

class BusyIndicator {
public:
    BusyIndicator(HANDLE output, SHORT x, SHORT y, std::string label)
        : _output(output), _position{x,y}, _label(std::move(label)),
          _thread([this] { run(); }) {}
    ~BusyIndicator() {
        _stop.store(true);
        if (_thread.joinable()) _thread.join();
    }
    BusyIndicator(const BusyIndicator&) = delete;
    BusyIndicator& operator=(const BusyIndicator&) = delete;
private:
    void run() {
        constexpr char frames[] = {'|','/','-','\\'};
        size_t frame = 0;
        while (!_stop.load()) {
            const std::string text = _label + " " + frames[frame++ % 4] + "   ";
            DWORD written = 0;
            WriteConsoleOutputCharacterA(_output, text.c_str(),
                                         static_cast<DWORD>(text.size()),
                                         _position, &written);
            Sleep(80);
        }
    }
    HANDLE _output;
    COORD _position;
    std::string _label;
    std::atomic<bool> _stop{false};
    std::thread _thread;
};

const char* view_label(ViewState state)
{
    switch (state) {
    case ViewState::Applied: return "Applied";
    case ViewState::NotApplied: return "Not applied";
    case ViewState::Failed: return "Failed";
    }
    return "Not applied";
}

const char* view_color(ViewState state)
{
    switch (state) {
    case ViewState::Applied: return LIGHT_GREEN;
    case ViewState::NotApplied: return LIGHT_YELLOW;
    case ViewState::Failed: return LIGHT_RED;
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
                    setting.enabled ? LIGHT_GREEN : BRIGHT_BLACK},
                 {setting.adjustable ? "  <[" + setting.value + "]>" : "",
                  setting.adjustable ? LIGHT_CYAN : BRIGHT_BLACK}});
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

std::string benchmark_comparison(const std::vector<PresentMonMetrics>& before,
                                 const std::vector<PresentMonMetrics>& after)
{
    const auto mean_of = [](const std::vector<PresentMonMetrics>& runs,
                            bool input, bool p95) {
        double total = 0.0; size_t count = 0;
        for (const auto& run : runs) {
            const MetricSummary& metric = input ? run.input : run.frame;
            if (!metric.samples) continue;
            total += p95 ? metric.p95 : metric.median; ++count;
        }
        return count ? total / count : 0.0;
    };
    const double before_input = mean_of(before, true, false);
    const double after_input = mean_of(after, true, false);
    const double before_frame = mean_of(before, false, true);
    const double after_frame = mean_of(after, false, true);
    const auto spread = [&](const std::vector<PresentMonMetrics>& runs, bool input) {
        const double center = mean_of(runs, input, !input);
        double total = 0.0; size_t count = 0;
        for (const auto& run : runs) {
            const MetricSummary& metric = input ? run.input : run.frame;
            if (!metric.samples) continue;
            total += std::abs((input ? metric.median : metric.p95) - center); ++count;
        }
        return count ? total / count : 0.0;
    };
    const double noise = (std::max)(spread(before, true), spread(after, true));
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << "A/B " << before.size() << "x: ";
    if (before_input && after_input)
        out << "input " << (after_input - before_input) << " ms";
    else out << "input n/a";
    if (before_frame && after_frame)
        out << "  frame p95 " << (after_frame - before_frame) << " ms";
    if (before_input && after_input)
        out << (before_input - after_input > noise && before_input > after_input ?
                "  repeatable improvement" :
                after_input - before_input > noise ? "  regression" : "  within variation");
    return out.str();
}

void save_benchmark_report(const std::vector<PresentMonMetrics>& before,
                           const std::vector<PresentMonMetrics>& after,
                           const std::vector<Optimizer::TweakSetting>& settings,
                           const std::vector<std::string>& errors)
{
    char local[MAX_PATH]{};
    if (!GetEnvironmentVariableA("LOCALAPPDATA", local, sizeof(local))) return;
    try {
        const std::filesystem::path directory =
            std::filesystem::path(local) / "NebulaOptimizer";
        std::filesystem::create_directories(directory);
        nlohmann::json report;
        SYSTEM_INFO system{}; GetNativeSystemInfo(&system);
        MEMORYSTATUSEX memory{}; memory.dwLength = sizeof(memory);
        GlobalMemoryStatusEx(&memory);
        report["hardware"] = {{"logical_processors", system.dwNumberOfProcessors},
            {"architecture", system.wProcessorArchitecture},
            {"physical_memory_mb", memory.ullTotalPhys / (1024 * 1024)}};
        SYSTEM_POWER_STATUS power{};
        if (GetSystemPowerStatus(&power)) {
            report["hardware"]["ac_connected"] = power.ACLineStatus == 1;
            report["hardware"]["battery_present"] = power.BatteryFlag != 128;
        }
        DWORD topology_size = 0;
        GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr,
                                         &topology_size);
        std::vector<unsigned char> topology(topology_size);
        bool hybrid = false;
        BYTE first_efficiency = 0;
        bool have_efficiency = false;
        if (topology_size && GetLogicalProcessorInformationEx(RelationProcessorCore,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(topology.data()),
            &topology_size)) {
            size_t offset = 0;
            while (offset < topology_size) {
                const auto* item = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
                    topology.data() + offset);
                const BYTE efficiency = item->Processor.EfficiencyClass;
                if (have_efficiency && efficiency != first_efficiency) hybrid = true;
                first_efficiency = efficiency; have_efficiency = true;
                if (!item->Size) break;
                offset += item->Size;
            }
        }
        report["hardware"]["hybrid_cpu"] = hybrid;
        report["hardware"]["display_adapters"] = nlohmann::json::array();
        for (DWORD index = 0; ; ++index) {
            DISPLAY_DEVICEA adapter{}; adapter.cb = sizeof(adapter);
            if (!EnumDisplayDevicesA(nullptr, index, &adapter, 0)) break;
            if (adapter.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)
                report["hardware"]["display_adapters"].push_back(adapter.DeviceString);
        }
        const auto encode = [](const PresentMonMetrics& run) {
            return nlohmann::json{{"rows", run.rows}, {"valid_frames", run.valid_frames},
                {"coverage_seconds", run.coverage_seconds},
                {"input_median_ms", run.input.median}, {"input_p95_ms", run.input.p95},
                {"input_p99_ms", run.input.p99}, {"click_median_ms", run.click.median},
                {"frame_median_ms", run.frame.median}, {"frame_p95_ms", run.frame.p95},
                {"frame_p99_ms", run.frame.p99}, {"average_fps", run.average_fps},
                {"one_percent_low_fps", run.one_percent_low_fps},
                {"frame_cv_percent", run.frame_cv_percent}, {"stutters", run.stutters},
                {"cpu_mean_ms", run.cpu.mean}, {"gpu_mean_ms", run.gpu.mean}};
        };
        report["before"] = nlohmann::json::array();
        report["after"] = nlohmann::json::array();
        for (const auto& run : before) report["before"].push_back(encode(run));
        for (const auto& run : after) report["after"].push_back(encode(run));
        report["session_changes"] = nlohmann::json::array();
        for (const auto& setting : settings)
            if (setting.status == Optimizer::TweakStatus::Applied)
                report["session_changes"].push_back(setting.label);
        report["errors"] = errors;
        const auto final_path = directory / "benchmark-last.json";
        const auto temporary = directory / "benchmark-last.json.tmp";
        { std::ofstream out(temporary, std::ios::trunc); out << report.dump(2); }
        MoveFileExA(temporary.string().c_str(), final_path.string().c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    } catch (...) {}
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
        if (_process) {
            if (WaitForSingleObject(_process, 0) == WAIT_TIMEOUT) {
                TerminateProcess(_process, 0);
                WaitForSingleObject(_process, 1000);
            }
            CloseHandle(_process);
        }
        remove_capture_file();
    }

    bool start(DWORD pid)
    {
        if (!pid || _process)
            return false;
        char module[MAX_PATH * 4]{};
        const DWORD module_length = GetModuleFileNameA(nullptr, module,
                                                       sizeof(module));
        if (!module_length || module_length >= sizeof(module)) {
            _status = "Nebula path unavailable";
            return false;
        }
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
        const DWORD local_length = GetEnvironmentVariableA("LOCALAPPDATA", local,
                                                          sizeof(local));
        if (!local_length || local_length >= sizeof(local)) {
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
            remove_capture_file();
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
            remove_capture_file();
            return;
        }
        read_result();
        remove_capture_file();
    }

    const std::string& status() const { return _status; }
    bool running() const { return _process != nullptr; }
    bool take_result(PresentMonMetrics& result)
    {
        if (!_result_ready) return false;
        result = _last_result;
        _result_ready = false;
        return true;
    }

private:
    void remove_capture_file()
    {
        if (!_csv_path.empty()) {
            DeleteFileA(_csv_path.c_str());
            _csv_path.clear();
        }
    }

    void read_result()
    {
        _last_result = parse_presentmon_file(_csv_path);
        _result_ready = true;
        _status = presentmon_compact_text(_last_result);
    }

    HANDLE _process = nullptr;
    std::string _csv_path;
    std::string _status = "Input-to-display: not measured";
    PresentMonMetrics _last_result{};
    bool _result_ready = false;
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
                if (!menu) return 0;
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
            UnregisterClassA(klass.lpszClassName, instance);
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
        UnregisterClassA(klass.lpszClassName, instance);
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
    if (_console_font_changed) {
        HANDLE output=GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_FONT_INFOEX current{};
        current.cbSize=sizeof(current);
        if (output!=INVALID_HANDLE_VALUE &&
            GetCurrentConsoleFontEx(output,FALSE,&current) &&
            _wcsicmp(current.FaceName,_applied_console_font.FaceName)==0 &&
            current.dwFontSize.Y==_applied_console_font.dwFontSize.Y)
            SetCurrentConsoleFontEx(output,FALSE,&_original_console_font);
    }
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
        // Only the classic console owns its font. Windows Terminal and other
        // hosts keep their own user-selected font settings.
        if (GetEnvironmentVariableW(L"WT_SESSION",nullptr,0)==0 &&
            GetConsoleWindow() && IsWindowVisible(GetConsoleWindow())) {
            CONSOLE_FONT_INFOEX original{};
            original.cbSize=sizeof(original);
            if (GetCurrentConsoleFontEx(output,FALSE,&original) &&
                ((original.FontFamily & TMPF_TRUETYPE)==0 ||
                 original.FaceName[0]==L'\0' ||
                 _wcsicmp(original.FaceName,L"Terminal")==0 ||
                 _wcsicmp(original.FaceName,L"__DefaultTTFont__")==0 ||
                 ((_wcsicmp(original.FaceName,L"Consolas")==0 ||
                   _wcsicmp(original.FaceName,L"Lucida Console")==0) &&
                  original.dwFontSize.Y<18))) {
                CONSOLE_FONT_INFOEX desired=original;
                desired.dwFontSize={0,18};
                desired.FontFamily=FF_MODERN | TMPF_VECTOR | TMPF_TRUETYPE;
                desired.FontWeight=FW_NORMAL;
                std::wmemset(desired.FaceName,0,LF_FACESIZE);
                std::wmemcpy(desired.FaceName,L"Consolas",8);
                if (SetCurrentConsoleFontEx(output,FALSE,&desired)) {
                    CONSOLE_FONT_INFOEX verified{};
                    verified.cbSize=sizeof(verified);
                    if (GetCurrentConsoleFontEx(output,FALSE,&verified) &&
                        _wcsicmp(verified.FaceName,L"Consolas")==0) {
                        _original_console_font=original;
                        _applied_console_font=verified;
                        _console_font_changed=true;
                    } else {
                        SetCurrentConsoleFontEx(output,FALSE,&original);
                    }
                }
            }
        }
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

int Tui::select_menu(const std::string& heading,
                     const std::vector<std::string>& options, size_t initial)
{
    if (options.empty()) return -1;
    MenuCursorGuard cursor_guard;
    size_t cursor = (std::min)(initial, options.size() - 1);
    size_t scroll = 0;
    bool redraw = true;
    std::vector<std::string> previous_frame;
    ULONGLONG next_game_check = 0;
    while (true) {
        if (_tray && _tray->stop_requested()) return -1;
        const ULONGLONG now = GetTickCount64();
        if (now >= next_game_check) {
            next_game_check = now + 500;
            if ((_optimizer.has_game_process() || _optimizer.is_waiting_for_game()) &&
                !_optimizer.is_game_running()) return -1;
        }
        if (_tray && _tray->show_requested()) {
            HWND window = GetConsoleWindow();
            if (window) {
                ShowWindow(window, SW_SHOW);
                ShowWindow(window, SW_RESTORE);
                SetForegroundWindow(window);
            }
        }
        HWND window = GetConsoleWindow();
        if (window && IsIconic(window)) ShowWindow(window, SW_HIDE);
        if (redraw) {
            update_layout();
            std::ostringstream frame;
            frame_output = &frame;
            frame_line = 0;
            title(heading);
            CONSOLE_SCREEN_BUFFER_INFO info{};
            const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
            size_t visible = 10;
            if (output != INVALID_HANDLE_VALUE &&
                GetConsoleScreenBufferInfo(output, &info)) {
                const int height = info.srWindow.Bottom - info.srWindow.Top + 1;
                visible = static_cast<size_t>((std::max)(1,
                    (std::min)(10, height - 9)));
            }
            scroll = scroll_to_cursor(scroll, cursor, visible);
            for (size_t index = scroll;
                 index < options.size() && index < scroll + visible; ++index)
                row(std::string(index == cursor ? "> " : "  ") + options[index],
                    index == cursor ? LIGHT_PURPLE : WHITE);
            divider();
            row("[J/K or arrows] Move  [Enter/L] Select  [Esc/H] Back",
                BRIGHT_BLACK);
            bottom_border();
            frame_output = &std::cout;
            present_frame(frame.str(), previous_frame);
            redraw = false;
        }
        if (!_kbhit()) { wait_for_menu_key(100); continue; }
        switch (read_menu_key()) {
        case MenuKey::Up:
            cursor = move_tweak_cursor(cursor, -1, options.size());
            redraw = true;
            break;
        case MenuKey::Down:
            cursor = move_tweak_cursor(cursor, 1, options.size());
            redraw = true;
            break;
        case MenuKey::Right:
        case MenuKey::Select: return static_cast<int>(cursor);
        case MenuKey::Left:
        case MenuKey::Back: return -1;
        default: break;
        }
    }
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
    MenuCursorGuard cursor_guard;
    enum class Action { Play, Profiles, Windows, Scan, Tray, Retry, Exit };
    std::vector<std::pair<Action, std::string>> actions = {
        {Action::Play, _selected_profile.empty() ? "Create profile" :
                       "Play / optimize"},
        {Action::Profiles, "Profiles"},
        {Action::Windows, "Optimize Windows"},
        {Action::Scan, "Scan PC"},
        {Action::Tray, "Hide to tray"}
    };
    if (_optimizer.recovery_blocked())
        actions.push_back({Action::Retry, "Retry restore"});
    actions.push_back({Action::Exit, "Exit"});
    _home_cursor = (std::min)(_home_cursor, actions.size() - 1);
    const auto settings=_optimizer.tweak_settings();
    size_t base_count=0, saved_count=0;
    for (const auto& item:settings) {
        if (item.durable.base) ++base_count;
        if (item.durable.saved) ++saved_count;
    }
    bool redraw = true;
    size_t scroll = 0;
    std::vector<std::string> previous_frame;
    ULONGLONG next_scan = 0;
    while (_is_running) {
        if (redraw) {
            update_layout();
            CONSOLE_SCREEN_BUFFER_INFO info{};
            size_t visible = actions.size();
            const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
            if (output != INVALID_HANDLE_VALUE &&
                GetConsoleScreenBufferInfo(output, &info)) {
                const int height = info.srWindow.Bottom - info.srWindow.Top + 1;
                const int fixed = 8 + (_selected_profile.empty() ? 1 : 2) +
                    (!_scan_message.empty() ? 1 : 0) +
                    (!_optimizer.restoration_status().empty() ? 1 : 0);
                visible = (std::min)(actions.size(), static_cast<size_t>(
                    (std::max)(1, height-fixed)));
            }
            scroll = scroll_to_cursor(scroll, _home_cursor, visible);
            std::ostringstream frame;
            frame_output = &frame;
            frame_line = 0;
            title("");
            if (_selected_profile.empty()) row("No profile", LIGHT_YELLOW);
            else {
                row("Profile: " + _selected_profile, LIGHT_GREEN);
                row("Auto-detect: on", BRIGHT_BLACK);
            }
            divider();
            for (size_t index=scroll;
                 index<actions.size() && index<scroll+visible; ++index)
                row(std::string(index == _home_cursor ? "> " : "  ") +
                    actions[index].second,
                    index == _home_cursor ? LIGHT_PURPLE : WHITE);
            divider();
            row("[J/K or arrows] Move  [Enter/L] Select  [Esc] Tray",
                BRIGHT_BLACK);
            row(std::string("Admin: ") + (elevated() ? "on" : "off") +
                "  |  Base: " + std::to_string(base_count) +
                "  |  Saved: " + std::to_string(saved_count), LIGHT_CYAN);
            if (!_scan_message.empty()) row(_scan_message, LIGHT_YELLOW);
            if (!_optimizer.restoration_status().empty())
                row(_optimizer.restoration_status(),
                    _optimizer.restoration_succeeded() ? LIGHT_GREEN : LIGHT_RED);
            bottom_border();
            frame_output = &std::cout;
            present_frame(frame.str(), previous_frame);
            redraw = false;
        }
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
            const MenuKey key = read_menu_key();
            if (key == MenuKey::Up || key == MenuKey::Down) {
                _home_cursor = move_tweak_cursor(_home_cursor,
                    key == MenuKey::Up ? -1 : 1, actions.size());
                redraw = true;
            } else if (key == MenuKey::Back || key == MenuKey::Left) {
                if (window) ShowWindow(window, SW_HIDE);
            } else if (key == MenuKey::Select || key == MenuKey::Right) {
                switch (actions[_home_cursor].first) {
                case Action::Play:
                    if (_optimizer.recovery_blocked()) {
                        _scan_message = "Recovery required before optimization";
                        redraw = true;
                    } else if (_selected_profile.empty()) {
                        _current_screen = CREATE_PROFILE;
                        return;
                    } else {
                        if (!_optimizer.has_baseline_scan(_selected_profile_path)) {
                            const int choice=select_menu("Create baseline scan?",
                                {"Scan now", "Continue without scan", "Cancel"});
                            if (choice<0 || choice==2) {redraw=true; break;}
                            if (choice==0 && !_optimizer.scan_saved(
                                    _selected_profile_path)) {
                                _scan_message="Scan failed: "+_optimizer.saved_error();
                                redraw=true; break;
                            }
                        }
                        _launch_path = _selected_profile_path;
                        _attach_only = false;
                        _current_screen = LAUNCH_GAME;
                        return;
                    }
                    break;
                case Action::Profiles:
                    _current_screen = PROFILE_SELECT;
                    return;
                case Action::Windows:
                    if (_optimizer.recovery_blocked()) {
                        _scan_message = "Recovery required before optimization";
                        redraw = true;
                    } else {
                        if (!_optimizer.has_baseline_scan()) {
                            const int choice=select_menu("Create baseline scan?",
                                {"Scan now", "Continue without scan", "Cancel"});
                            if (choice<0 || choice==2) {redraw=true; break;}
                            if (choice==0 && !_optimizer.scan_saved("")) {
                                _scan_message="Scan failed: "+_optimizer.saved_error();
                                redraw=true; break;
                            }
                        }
                        _launch_path.clear();
                        _attach_only = false;
                        _current_screen = LAUNCH_GAME;
                        return;
                    }
                    break;
                case Action::Scan:
                    _scan_message = _optimizer.recovery_blocked() ?
                        "Recovery required before scan" :
                        _optimizer.scan_saved(_selected_profile_path) ?
                        "Scan complete" : "Scan failed: " + _optimizer.saved_error();
                    _current_screen = STARTUP;
                    return;
                case Action::Tray:
                    if (window) ShowWindow(window, SW_HIDE);
                    break;
                case Action::Retry:
                    _optimizer.retry_recovery();
                    _current_screen = STARTUP;
                    return;
                case Action::Exit:
                    _current_screen = EXIT;
                    return;
                }
            }
        }
        const ULONGLONG now = GetTickCount64();
        if (!_optimizer.recovery_blocked() &&
            !_selected_profile_path.empty() && now >= next_scan) {
            const bool running = _optimizer.is_game_active(_selected_profile_path);
            if (!running)
                _suppress_auto_attach = false;
            else if (!_suppress_auto_attach) {
                if (!_optimizer.has_baseline_scan(_selected_profile_path)) {
                    _scan_message = "Scan PC before the first auto-attach";
                    redraw = true;
                } else {
                    _launch_path = _selected_profile_path;
                    _attach_only = true;
                    _current_screen = LAUNCH_GAME;
                    return;
                }
            }
            next_scan = now + 2500;
        }
        wait_for_menu_key(100);
    }
}

bool Tui::background_apps_screen()
{
    bool added = false;
    while (true) {
        if ((_optimizer.has_game_process() || _optimizer.is_waiting_for_game()) &&
            !_optimizer.is_game_running()) return added;
        const auto& selected = _background_apps.selected();
        std::vector<std::string> options;
        for (const auto& path : selected) {
            const auto offset = path.find_last_of("\\/");
            const auto name = path.substr(
                offset == std::string::npos ? 0 : offset + 1);
            options.push_back("[x] " + name);
        }
        options.push_back("+ Running app");
        options.push_back("+ Enter .exe path");
        options.push_back("Back");
        const int choice = select_menu("Close on optimize  (" +
            std::to_string(selected.size()) + " selected)", options,
            selected.size());
        if (choice < 0 || static_cast<size_t>(choice) == options.size()-1)
            return added;
        if (static_cast<size_t>(choice) < selected.size()) {
            const auto path = selected[static_cast<size_t>(choice)];
            if (select_menu("Remove " + std::filesystem::path(path).filename().string() +
                            "?", {"Keep", "Remove"}) == 1) {
                for (size_t index = 0; index < selected.size(); ++index)
                    if (selected[index] == path) {
                        _background_apps.remove(index);
                        break;
                    }
            }
        } else if (static_cast<size_t>(choice) == selected.size()) {
            const auto all = _background_apps.running();
            std::vector<std::string> running_options;
            for (const auto& app : all)
                running_options.push_back("+ " + app.name + "  ~" +
                    std::to_string(app.working_set_bytes / (1024 * 1024)) +
                    " MB");
            running_options.push_back("Back");
            const int picked = select_menu("Running apps", running_options);
            if (picked >= 0 && static_cast<size_t>(picked) < all.size()) {
                const bool okay = _background_apps.add(all[picked].path);
                added = added || okay;
                if (!okay) pause("Could not add app. Press Enter...");
            }
        } else {
            std::string path = read_input("App .exe:");
            if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
                path = path.substr(1, path.size() - 2);
            if (!path.empty()) {
                const bool okay = _background_apps.add(path);
                added = added || okay;
                if (!okay) pause("Invalid or protected app. Press Enter...");
            }
        }
    }
}

void Tui::profiles_screen()
{
    const std::vector<std::string> profiles = get_profiles_list();
    std::vector<std::string> options;
    size_t initial = 0;
    for (size_t index = 0; index < profiles.size(); ++index) {
        if (profiles[index] == _selected_profile) initial = index;
        options.push_back(profiles[index] +
                          (profiles[index] == _selected_profile ? "  ACTIVE" : ""));
    }
    options.push_back("New profile");
    if (!profiles.empty()) options.push_back("Delete profile");
    options.push_back("Back");
    const int choice = select_menu("Profiles", options, initial);
    if (choice < 0 || static_cast<size_t>(choice) == options.size() - 1) {
        _current_screen = STARTUP;
    } else if (static_cast<size_t>(choice) == profiles.size()) {
        _current_screen = CREATE_PROFILE;
    } else if (static_cast<size_t>(choice) > profiles.size()) {
        delete_profile_flow();
    } else {
        _selected_profile = profiles[static_cast<size_t>(choice)];
        _selected_profile_path = load_profile_game_path(_selected_profile);
        _suppress_auto_attach = false;
        _current_screen = STARTUP;
    }
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
    std::vector<std::string> options(profiles.begin(), profiles.end());
    options.push_back("Back");
    const int choice = select_menu("Delete profile", options);
    if (choice >= 0 && static_cast<size_t>(choice) < profiles.size()) {
        const auto& name = profiles[static_cast<size_t>(choice)];
        if (select_menu("Delete " + name + "?", {"Keep", "Delete"}) == 1 &&
            delete_profile(name)) {
            if (_selected_profile == name) _selected_profile.clear();
            select_default_profile();
        }
    }
    _current_screen = PROFILE_SELECT;
}

void Tui::launch_current_mode()
{
    clear_screen();
    title("Starting");
    row(_launch_path.empty() ? "Windows only" :
        "Game: " + shorten_path(_launch_path, 55), LIGHT_CYAN);
    bottom_border();

    if (_optimizer.optimize(_launch_path, !_attach_only)) {
        _closed_apps_summary = _background_apps.close_selected(_launch_path);
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
    const int original_thread_priority = GetThreadPriority(GetCurrentThread());
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
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
    enum class BenchmarkStage { Idle, BaselineWarmup, BaselineCapture,
                                OptimizedWarmup, OptimizedCapture };
    BenchmarkStage benchmark_stage = BenchmarkStage::Idle;
    ULONGLONG benchmark_deadline = 0;
    DWORD benchmark_pid = 0;
    size_t benchmark_run = 0;
    std::vector<PresentMonMetrics> benchmark_before, benchmark_after;
    std::string benchmark_status;

    const ULONGLONG started = GetTickCount64();
    bool active = true;
    bool game_ended = false;
    bool expanded = true;
    bool actions_focused = false;
    size_t action_cursor = 0;
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
    std::vector<Optimizer::TweakSetting> cached_settings;
    bool cached_has_game = false;
    bool cached_waiting = false;
    bool cached_game_running = false;
    DWORD cached_game_pid = 0;
    std::string cached_game_status;
    std::vector<std::string> previous_frame;
    const auto refresh_optimizer_cache = [&]() {
        cached_has_game = _optimizer.has_game_process();
        cached_waiting = _optimizer.is_waiting_for_game();
        cached_game_running = _optimizer.is_game_running();
        cached_game_pid = _optimizer.game_pid();
        cached_game_status = _optimizer.game_status();
        cached_settings = _optimizer.tweak_settings();
    };
    const auto apply_live_changes = [&](SHORT y) {
        BusyIndicator busy(console, static_cast<SHORT>(box_margin + 2), y,
                           "Applying");
        _optimizer.restore();
        const bool success = _optimizer.restoration_succeeded() &&
                             _optimizer.optimize(_launch_path, false);
        tweak_message = success ? "Changes applied" :
                                  "Could not apply changes";
        previous_frame.clear();
        return success;
    };
    refresh_optimizer_cache();
    while (active) {
        refresh_optimizer_cache();
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
            const bool show_apps = !_background_apps.selected().empty() ||
                _closed_apps_summary.closed || _closed_apps_summary.skipped ||
                _closed_apps_summary.failed;
            visible_rows=static_cast<size_t>((std::max)(1,(std::min)(8,
                height-(tweak_message.empty() ? 23 : 24)-
                (show_apps ? 1 : 0))));
        }
        update_layout();
        if (expanded)
            scroll = scroll_to_cursor(scroll, selected_tweak, visible_rows);
        if ((cached_has_game || cached_waiting) && !cached_game_running) {
            game_ended = true;
            break;
        }

        if (console_window && !IsWindowVisible(console_window)) {
            Sleep(1500);
            continue;
        }
        latency.poll();
        PresentMonMetrics completed{};
        if (benchmark_stage != BenchmarkStage::Idle && latency.take_result(completed)) {
            if (completed.state != PresentMonDataState::Valid) {
                benchmark_status = "A/B stopped: invalid PresentMon data";
                if (!_optimizer.is_optimized()) _optimizer.optimize(_launch_path, false);
                benchmark_stage = BenchmarkStage::Idle;
            } else if (benchmark_stage == BenchmarkStage::BaselineCapture) {
                benchmark_before.push_back(completed);
                if (!_optimizer.optimize(_launch_path, false)) {
                    benchmark_status = "A/B stopped: session could not restart";
                    benchmark_stage = BenchmarkStage::Idle;
                } else {
                    benchmark_stage = BenchmarkStage::OptimizedWarmup;
                    benchmark_deadline = GetTickCount64() + 5000;
                    benchmark_status = "A/B: optimized warm-up";
                }
            } else if (benchmark_stage == BenchmarkStage::OptimizedCapture) {
                benchmark_after.push_back(completed);
                ++benchmark_run;
                if (benchmark_run >= 3) {
                    benchmark_status = benchmark_comparison(benchmark_before, benchmark_after);
                    save_benchmark_report(benchmark_before, benchmark_after,
                        _optimizer.tweak_settings(), _optimizer.failed_tweaks());
                    benchmark_stage = BenchmarkStage::Idle;
                } else {
                    _optimizer.restore();
                    benchmark_stage = BenchmarkStage::BaselineWarmup;
                    benchmark_deadline = GetTickCount64() + 5000;
                    benchmark_status = "A/B: baseline warm-up " +
                        std::to_string(benchmark_run + 1) + "/3";
                }
            }
        }
        if (benchmark_stage == BenchmarkStage::BaselineWarmup &&
            GetTickCount64() >= benchmark_deadline) {
            if (latency.start(benchmark_pid)) {
                benchmark_stage = BenchmarkStage::BaselineCapture;
                benchmark_status = "A/B: baseline capture " +
                    std::to_string(benchmark_run + 1) + "/3";
            }
        } else if (benchmark_stage == BenchmarkStage::OptimizedWarmup &&
                   GetTickCount64() >= benchmark_deadline) {
            if (latency.start(benchmark_pid)) {
                benchmark_stage = BenchmarkStage::OptimizedCapture;
                benchmark_status = "A/B: optimized capture " +
                    std::to_string(benchmark_run + 1) + "/3";
            }
        }

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
        const std::vector<Optimizer::TweakSetting>& settings = cached_settings;
        const ImpactEstimate impact = estimate_impact(settings, _closed_apps_summary);

        std::ostringstream frame;
        frame_output = &frame;
        frame_line = 0;
        title("Active session");
        row(cached_game_status,LIGHT_GREEN);
        row("Time: " + uptime.str(),BRIGHT_BLACK);
        divider();
        row(ram.str(), LIGHT_CYAN);
        row(cpu.str(),LIGHT_CYAN);
        row(own.str(),BRIGHT_BLACK);
        row(cached_game_pid ? latency.status() + "  [P] Capture 10 s" :
            "Input-to-display: no game attached", BRIGHT_BLACK);
        if (!benchmark_status.empty()) row(benchmark_status, LIGHT_PURPLE);
        if (box_width < 52)
            colored_row({{"~", LIGHT_PURPLE},
                         {std::to_string(impact.ram_mb) + "MB  ", LIGHT_CYAN},
                         {"C+" + std::to_string(impact.cpu_percent) + "%  ", LIGHT_GREEN},
                         {"G+" + std::to_string(impact.gpu_percent) + "%  ", LIGHT_BLUE},
                         {"L-" + std::to_string(impact.latency_percent) + "%", LIGHT_YELLOW}});
        else
            colored_row({{"Gain ~  ", LIGHT_PURPLE},
                         {"RAM " + std::to_string(impact.ram_mb) + " MB  ", LIGHT_CYAN},
                         {"CPU +" + std::to_string(impact.cpu_percent) + "%  ", LIGHT_GREEN},
                         {"GPU +" + std::to_string(impact.gpu_percent) + "%  ", LIGHT_BLUE},
                         {"Latency -" + std::to_string(impact.latency_percent) + "%", LIGHT_YELLOW}});
        const auto& apps = _closed_apps_summary;
        const std::int64_t apps_ram_mb = apps.available_ram_delta_bytes / (1024 * 1024);
        if (!_background_apps.selected().empty() || apps.closed ||
            apps.skipped || apps.failed)
            row("Apps: " + std::to_string(apps.closed) + " closed  " +
                std::to_string(apps.skipped) + " skipped  " +
                std::to_string(apps.failed) + " failed  |  RAM " +
                (apps_ram_mb >= 0 ? "+" : "") + std::to_string(apps_ram_mb) +
                " MB", apps.closed ? LIGHT_GREEN : BRIGHT_BLACK);
        divider();
        const ViewCounts counts=view_counts(settings);
        colored_row({{"Applied " + std::to_string(counts.applied),LIGHT_GREEN},
                     {"  |  Not applied " + std::to_string(counts.not_applied),LIGHT_YELLOW},
                     {"  |  Failed " + std::to_string(counts.failed),LIGHT_RED}});
        std::vector<size_t> category_tweaks;
        if (selected_category == std::size(TWEAK_CATEGORIES)-1) {
            for (size_t n=0;n<settings.size();++n)
                if (settings[n].durable.saved) category_tweaks.push_back(n);
        } else category_tweaks = tweaks_in_category(
            settings, TWEAK_CATEGORIES[selected_category]);
        if (selected_category == 3)
            category_tweaks.insert(category_tweaks.begin(), CLOSE_APPS_ROW);
        selected_tweak = move_tweak_cursor(selected_tweak, 0, category_tweaks.size());
        if (expanded) {
            carousel_y = screen_info.srWindow.Top + static_cast<SHORT>(frame_line);
            if (box_width < 34) {
                carousel.tab_columns.clear();
                row(std::string("< ")+TWEAK_TABS[selected_category]+" >",LIGHT_PURPLE);
            } else {
                const std::vector<std::string> tabs = box_width < 50 ?
                    std::vector<std::string>{"C","G","R","A","I","N","S"} :
                    std::vector<std::string>(std::begin(TWEAK_TABS),
                                             std::end(TWEAK_TABS));
                carousel = make_carousel(tabs,box_margin+2);
                carousel_row(carousel, selected_category);
            }
            for (size_t position = scroll;
                 position < category_tweaks.size() && position < scroll + visible_rows;
                 ++position) {
                const bool selected = position == selected_tweak;
                if (category_tweaks[position] == CLOSE_APPS_ROW)
                    colored_row({{selected ? "> " : "  ",
                                  selected ? LIGHT_PURPLE : BRIGHT_BLACK},
                                 {"Close apps (" +
                                  std::to_string(_background_apps.selected().size()) +
                                  ")  [Enter]", selected ? LIGHT_PURPLE : LIGHT_CYAN}});
                else tweak_row(settings[category_tweaks[position]],selected);
            }
            std::string marker;
            if (!category_tweaks.empty() &&
                category_tweaks[selected_tweak] != CLOSE_APPS_ROW) {
                const auto& selected = settings[category_tweaks[selected_tweak]];
                if (selected.durable.base) marker = "   BASE";
                else if (selected.status == Optimizer::TweakStatus::Skipped)
                    marker = "   NO TARGET";
                else if (selected.status == Optimizer::TweakStatus::Unsupported)
                    marker = "   UNSUPPORTED";
                else if (selected.status == Optimizer::TweakStatus::RestartRequired)
                    marker = "   RESTART";
            }
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
        const bool app_action = expanded && !category_tweaks.empty() &&
            category_tweaks[selected_tweak] == CLOSE_APPS_ROW;
        const bool selected_adjustable = expanded && !category_tweaks.empty() &&
            !app_action && settings[category_tweaks[selected_tweak]].adjustable;
        std::vector<SessionAction> actions;
        actions.push_back(SessionAction::ApplyAll);
        bool selected_saved = false;
        if (expanded && !category_tweaks.empty() && !app_action) {
            const auto& selected = settings[category_tweaks[selected_tweak]];
            selected_saved = selected.durable.saved;
            if (selected.durable.eligible &&
                (selected.enabled || selected.durable.saved))
                actions.push_back(SessionAction::Save);
            if (selected.durable.saved && selected.durable.drift)
                actions.push_back(SessionAction::Reapply);
        }
        if (cached_game_pid) {
            actions.push_back(SessionAction::Capture);
            actions.push_back(SessionAction::Benchmark);
        }
        actions.push_back(SessionAction::Tray);
        actions.push_back(SessionAction::Stop);
        action_cursor = (std::min)(action_cursor, actions.size()-1);
        row(actions_focused ?
            "J/K/H/L Action  Enter Select  Esc Tweaks" :
            !expanded ? "Enter Open tweaks  Tab Actions" :
            app_action ? "H/L Tabs  J/K Select  Enter Manage  Tab Actions  Esc Hide" :
            selected_adjustable ?
            "H/L Tabs  J/K Tweaks  Left/Right Value  Enter Toggle  Tab Actions" :
            "H/L Tabs  J/K Tweaks  Left/Right Tabs  Enter Toggle  Tab Actions", WHITE);
        std::vector<std::pair<std::string,const char*>> action_parts = {
            {"ACTIONS  ", LIGHT_PURPLE}
        };
        if (box_width < 50) {
            action_parts.push_back({std::string("< ") +
                action_label(actions[action_cursor], selected_saved) + " >",
                actions_focused ? BG_PURPLE BRIGHT_WHITE : LIGHT_CYAN});
        } else {
            for (size_t index=0; index<actions.size(); ++index)
                action_parts.push_back({std::string(" ") +
                    action_label(actions[index], selected_saved) + " ",
                    actions_focused && index == action_cursor ?
                    BG_PURPLE BRIGHT_WHITE : BG_BRIGHT_BLACK LIGHT_CYAN});
        }
        colored_row(action_parts);
        bottom_border();
        frame_output = &std::cout;
        present_frame(frame.str(),previous_frame);

        const ULONGLONG refresh_at = GetTickCount64() + 1000;
        while (active && GetTickCount64() < refresh_at) {
            const DWORD remaining = static_cast<DWORD>(refresh_at - GetTickCount64());
            const DWORD wait_ms = (std::min)(remaining, DWORD{100});
            if (input == INVALID_HANDLE_VALUE || input == nullptr ||
                WaitForSingleObject(input, wait_ms) == WAIT_FAILED)
                Sleep(wait_ms);
            if (tray.stop_requested())
                active = false;
            bool hover_changed = false;
            if (input != INVALID_HANDLE_VALUE && input != nullptr) {
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
                    if (!mouse_enabled || !expanded ||
                        event.EventType != MOUSE_EVENT ||
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
                int key = _getch();
                if (key == 9) {
                    actions_focused = !actions_focused;
                    break;
                }
                if (actions_focused) {
                    if (key == 27 || key == 8) {
                        actions_focused = false;
                        break;
                    }
                    int direction = 0;
                    if (key == 'j' || key == 'J' || key == 'l' || key == 'L')
                        direction = 1;
                    else if (key == 'k' || key == 'K' ||
                             key == 'h' || key == 'H')
                        direction = -1;
                    else if (key == 0 || key == 224) {
                        const int arrow = _getch();
                        direction = arrow == 80 || arrow == 77 ? 1 :
                                    arrow == 72 || arrow == 75 ? -1 : 0;
                    }
                    if (direction) {
                        action_cursor = move_carousel(action_cursor,
                                                      direction, actions.size());
                        break;
                    }
                    if (key != 13) break;
                    switch (actions[action_cursor]) {
                    case SessionAction::ApplyAll: key = 'a'; break;
                    case SessionAction::Save: key = 's'; break;
                    case SessionAction::Reapply: key = 'r'; break;
                    case SessionAction::Capture: key = 'p'; break;
                    case SessionAction::Benchmark: key = 'b'; break;
                    case SessionAction::Tray: key = 'm'; break;
                    case SessionAction::Stop: key = '0'; break;
                    }
                }
                if (key == '0')
                    active = false;
                else if (key == 'a' || key == 'A') {
                    if (benchmark_stage != BenchmarkStage::Idle) {
                        tweak_message = "Wait for the current operation";
                        break;
                    }
                    SetConsoleCursorInfo(console, &original_cursor);
                    const bool confirmed = select_menu(
                        "Apply all compatible tweaks?",
                        {"Cancel", "Apply all"}) == 1;
                    SetConsoleCursorInfo(console, &hidden_cursor);
                    previous_frame.clear();
                    if (!confirmed) {
                        tweak_message = "Cancelled";
                    } else if (!_optimizer.enable_all_tweaks()) {
                        tweak_message = "Could not save tweak settings";
                    } else if (cached_waiting) {
                        tweak_message = "All tweaks enabled for the next session";
                    } else {
                        cached_settings = _optimizer.tweak_settings();
                        const SHORT busy_y = screen_info.srWindow.Top +
                            static_cast<SHORT>(frame_line > 2 ? frame_line - 2 : 1);
                        if (!apply_live_changes(busy_y)) active = false;
                    }
                    break;
                }
                else if (!expanded && (key == 13 || key == 'l' || key == 'L')) {
                    expanded = true;
                    break;
                } else if (expanded && (key == 27 || key == 8)) {
                    expanded = false;
                    break;
                } else if (expanded && (key == 'j' || key == 'J' ||
                                        key == 'k' || key == 'K' ||
                                        key == 'h' || key == 'H' ||
                                        key == 'l' || key == 'L' ||
                                        key == 0 || key == 224)) {
                    int direction = 0;
                    int category_direction = 0;
                    int value_direction = 0;
                    if (key == 'j' || key == 'J')
                        direction = 1;
                    else if (key == 'k' || key == 'K')
                        direction = -1;
                    else if (key == 'h' || key == 'H')
                        category_direction = -1;
                    else if (key == 'l' || key == 'L')
                        category_direction = 1;
                    else {
                        const int arrow = _getch();
                        direction = arrow == 80 ? 1 : (arrow == 72 ? -1 : 0);
                        const int horizontal = arrow == 77 ? 1 :
                                               (arrow == 75 ? -1 : 0);
                        if (horizontal && selected_adjustable)
                            value_direction = horizontal;
                        else
                            category_direction = horizontal;
                    }
                    if (value_direction) {
                        const size_t index = category_tweaks[selected_tweak];
                        if (benchmark_stage != BenchmarkStage::Idle) {
                            tweak_message = "Wait for the current operation";
                        } else if (!_optimizer.adjust_tweak_value(index, value_direction)) {
                            tweak_message = "Could not save value";
                        } else if (cached_waiting) {
                            tweak_message = "Value saved for the next session";
                        } else {
                            cached_settings = _optimizer.tweak_settings();
                            const SHORT busy_y = screen_info.srWindow.Top +
                                static_cast<SHORT>(frame_line > 2 ? frame_line - 2 : 1);
                            if (!apply_live_changes(busy_y)) active = false;
                        }
                    } else if (category_direction) {
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
                    if (!category_tweaks.empty() &&
                        category_tweaks[selected_tweak] == CLOSE_APPS_ROW) {
                        SetConsoleCursorInfo(console, &original_cursor);
                        if (mouse_enabled)
                            SetConsoleMode(input, original_input_mode &
                                                  ~ENABLE_MOUSE_INPUT &
                                                  ~ENABLE_QUICK_EDIT_MODE);
                        const bool added = background_apps_screen();
                        if (mouse_enabled)
                            SetConsoleMode(input, (original_input_mode |
                                ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS) &
                                ~ENABLE_QUICK_EDIT_MODE);
                        SetConsoleCursorInfo(console, &hidden_cursor);
                        if (added &&
                            ((!cached_has_game && !cached_waiting) ||
                             cached_game_running)) {
                            const auto result = _background_apps.close_selected(_launch_path);
                            _closed_apps_summary.closed += result.closed;
                            _closed_apps_summary.skipped += result.skipped;
                            _closed_apps_summary.failed += result.failed;
                            _closed_apps_summary.available_ram_delta_bytes +=
                                result.available_ram_delta_bytes;
                        }
                        previous_frame.clear();
                    } else if (benchmark_stage != BenchmarkStage::Idle) {
                        tweak_message = "Wait for the current operation";
                    } else if (category_tweaks.empty() ||
                        !_optimizer.toggle_tweak(category_tweaks[selected_tweak])) {
                        tweak_message = "Could not save tweak settings";
                    } else if (cached_waiting) {
                        tweak_message = "Saved for the next session";
                    } else {
                        cached_settings = _optimizer.tweak_settings();
                        const SHORT busy_y = screen_info.srWindow.Top +
                            static_cast<SHORT>(frame_line > 2 ? frame_line - 2 : 1);
                        if (!apply_live_changes(busy_y)) active = false;
                    }
                    break;
                } else if (expanded && (key == 's' || key == 'S' ||
                                        key == 'r' || key == 'R')) {
                    if (category_tweaks.empty()) { tweak_message="No tweak selected"; break; }
                    const size_t index=category_tweaks[selected_tweak];
                    if (index == CLOSE_APPS_ROW) {
                        tweak_message="Press Enter to manage apps"; break;
                    }
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
                    const bool sensitive=index==28 || index==37 || index==39 ||
                        index==40 || index==41 || index==44 ||
                        (index>=47 && index<=50) || index==54 || index==55 ||
                        index==57 || index==58 || index==59;
                    bool confirmed=!sensitive;
                    if (sensitive) {
                        SetConsoleCursorInfo(console, &original_cursor);
                        confirmed=select_menu("Save permanently?",
                                              {"Cancel", "Save"}) == 1;
                        SetConsoleCursorInfo(console, &hidden_cursor);
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
                    latency.start(cached_game_pid);
                    break;
                } else if ((key == 'b' || key == 'B') &&
                           benchmark_stage == BenchmarkStage::Idle) {
                    if (_launch_path.empty() || !cached_game_pid) {
                        benchmark_status = "A/B needs an active game profile";
                        break;
                    }
                    PresentMonMetrics stale{};
                    latency.take_result(stale);
                    benchmark_pid = cached_game_pid;
                    benchmark_run = 0;
                    benchmark_before.clear(); benchmark_after.clear();
                    _optimizer.restore();
                    benchmark_stage = BenchmarkStage::BaselineWarmup;
                    benchmark_deadline = GetTickCount64() + 5000;
                    benchmark_status = "A/B: baseline warm-up 1/3";
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
    if (original_thread_priority != THREAD_PRIORITY_ERROR_RETURN)
        SetThreadPriority(GetCurrentThread(), original_thread_priority);
    _current_screen = STARTUP;
}

void Tui::run()
{
    while (_is_running) {
        switch (_current_screen) {
        case STARTUP: startup_screen(); break;
        case PROFILE_SELECT: profiles_screen(); break;
        case CREATE_PROFILE: create_profile_screen(); break;
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
