#include "tui_view.hpp"

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
    bool okay=first.applied==2 && first.skipped==2 && first.failed==3 &&
        view_state(settings[1])==ViewState::Applied &&
        view_state(settings[7])==ViewState::Pending &&
        view_state(settings[8])==ViewState::Off;
    settings[0].durable.drift=true;
    const ViewCounts second=view_counts(settings);
    okay=second.applied==1 && second.skipped==2 && second.failed==3 &&
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
    if (!okay) std::cerr << "TUI view test failed\n";
    return okay ? 0 : 1;
}
