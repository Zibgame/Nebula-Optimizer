#pragma once

#include "optimizer.hpp"

#include <algorithm>
#include <string>
#include <vector>

enum class ViewState { Applied, NotApplied, Failed };

struct ViewCounts {
    size_t applied = 0;
    size_t not_applied = 0;
    size_t failed = 0;
};

inline ViewState view_state(const Optimizer::TweakSetting& setting)
{
    if (setting.durable.drift || !setting.enabled ||
        setting.status == Optimizer::TweakStatus::Off)
        return ViewState::NotApplied;
    switch (setting.status) {
    case Optimizer::TweakStatus::Applied:
    case Optimizer::TweakStatus::Configured:
    case Optimizer::TweakStatus::AlreadyConfigured:
    case Optimizer::TweakStatus::RestartRequired:
        return ViewState::Applied;
    case Optimizer::TweakStatus::Skipped:
    case Optimizer::TweakStatus::Unsupported:
    case Optimizer::TweakStatus::Off:
    case Optimizer::TweakStatus::On:
        return ViewState::NotApplied;
    case Optimizer::TweakStatus::AccessDenied:
    case Optimizer::TweakStatus::Failed:
    case Optimizer::TweakStatus::RestoreIncomplete:
        return ViewState::Failed;
    }
    return ViewState::NotApplied;
}

inline ViewCounts view_counts(const std::vector<Optimizer::TweakSetting>& settings)
{
    ViewCounts result;
    for (const auto& setting : settings) {
        switch (view_state(setting)) {
        case ViewState::Applied: ++result.applied; break;
        case ViewState::NotApplied: ++result.not_applied; break;
        case ViewState::Failed: ++result.failed; break;
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
