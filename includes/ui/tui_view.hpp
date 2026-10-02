#pragma once

#include "optimizer.hpp"

#include <algorithm>
#include <string>
#include <vector>

enum class ViewState { Applied, Configured, Restart, Skipped, Failed, Off, Drift, Pending };

struct ViewCounts {
    size_t applied = 0;
    size_t configured = 0;
    size_t restart = 0;
    size_t skipped = 0;
    size_t failed = 0;
};

inline ViewState view_state(const Optimizer::TweakSetting& setting)
{
    if (setting.durable.drift)
        return ViewState::Drift;
    if (!setting.enabled || setting.status == Optimizer::TweakStatus::Off)
        return ViewState::Off;
    switch (setting.status) {
    case Optimizer::TweakStatus::Applied:
        return ViewState::Applied;
    case Optimizer::TweakStatus::Configured:
    case Optimizer::TweakStatus::AlreadyConfigured:
        return ViewState::Configured;
    case Optimizer::TweakStatus::RestartRequired:
        return ViewState::Restart;
    case Optimizer::TweakStatus::Skipped:
    case Optimizer::TweakStatus::Unsupported:
        return ViewState::Skipped;
    case Optimizer::TweakStatus::AccessDenied:
    case Optimizer::TweakStatus::Failed:
    case Optimizer::TweakStatus::RestoreIncomplete:
        return ViewState::Failed;
    case Optimizer::TweakStatus::Off:
        return ViewState::Off;
    case Optimizer::TweakStatus::On:
        return ViewState::Pending;
    }
    return ViewState::Pending;
}

inline ViewCounts view_counts(const std::vector<Optimizer::TweakSetting>& settings)
{
    ViewCounts result;
    for (const auto& setting : settings) {
        switch (view_state(setting)) {
        case ViewState::Applied: ++result.applied; break;
        case ViewState::Configured: ++result.configured; break;
        case ViewState::Restart: ++result.restart; break;
        case ViewState::Skipped: ++result.skipped; break;
        case ViewState::Failed: ++result.failed; break;
        default: break;
        }
    }
    return result;
}

inline int left_card_width(int terminal_columns)
{
    return (std::max)(8, (std::min)(68, terminal_columns - 4));
}

inline std::vector<size_t> changed_frame_rows(
    const std::vector<std::string>& previous,
    const std::vector<std::string>& next)
{
    std::vector<size_t> changed;
    const size_t count=(std::max)(previous.size(),next.size());
    for (size_t i=0;i<count;++i)
        if (i>=previous.size() || i>=next.size() || previous[i]!=next[i])
            changed.push_back(i);
    return changed;
}
