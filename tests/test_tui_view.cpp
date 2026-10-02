#include "tui_view.hpp"
#include "impact_estimate.hpp"

#include <iostream>

int main()
{
    using Status=Optimizer::TweakStatus;
    auto item=[](Status status) {
        return Optimizer::TweakSetting{"Test","CPU & Power",true,status,{}};
    };
    std::vector<Optimizer::TweakSetting> settings={
        item(Status::Applied),item(Status::AlreadyConfigured),
        item(Status::Skipped),item(Status::Unsupported),
        item(Status::Failed),item(Status::AccessDenied),
        item(Status::RestoreIncomplete),item(Status::On),item(Status::Off)
    };
    settings[8].enabled=false;
    const ViewCounts first=view_counts(settings);
    bool okay=first.applied==1 && first.configured==1 && first.restart==0 &&
        first.skipped==2 && first.failed==3 &&
        view_state(settings[1])==ViewState::Configured &&
        view_state(settings[7])==ViewState::Pending &&
        view_state(settings[8])==ViewState::Off;
    settings[0].durable.drift=true;
    const ViewCounts second=view_counts(settings);
    okay=second.applied==0 && second.configured==1 && second.skipped==2 &&
         second.failed==3 &&
         view_state(settings[0])==ViewState::Drift && okay;
    for (int columns:{32,40,60,80,120}) {
        const int width=left_card_width(columns);
        okay=width<=68 && width+4<=columns && okay;
    }
    okay=left_card_width(120)==68 && okay;
    okay=changed_frame_rows({"one","two"},{"one","three"})==
         std::vector<size_t>{1} &&
         changed_frame_rows({"one","two"},{"one"})==
         std::vector<size_t>{1} &&
         changed_frame_rows({"one"},{"one"}).empty() && okay;
    std::vector<Optimizer::TweakSetting> impact_settings(60,
        {"Test", "CPU & Power", true, Status::Skipped, {}});
    ClosedAppsSummary apps{};
    apps.closed = 1;
    apps.closed_working_set_bytes = 150 * 1024 * 1024;
    impact_settings[0].status = Status::Applied;
    impact_settings[12].status = Status::Applied;
    impact_settings[28].status = Status::AlreadyConfigured;
    const auto impact = estimate_impact(impact_settings, apps);
    okay = impact.ram_mb == 150 && impact.cpu_percent == 1 &&
        impact.gpu_percent == 1 && impact.latency_percent == 1 && okay;
    impact_settings[0].status = Status::AlreadyConfigured;
    apps = {};
    const auto no_new_gain = estimate_impact(impact_settings, apps);
    okay = no_new_gain.ram_mb == 0 && no_new_gain.cpu_percent == 0 &&
        no_new_gain.gpu_percent == 1 &&
        no_new_gain.latency_percent == 0 && okay;
    if (!okay) std::cerr << "TUI view test failed\n";
    return okay ? 0 : 1;
}
