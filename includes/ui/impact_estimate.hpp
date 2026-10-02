#pragma once

#include "background_apps.hpp"
#include "optimizer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

// Conservative heuristic percentages, not measured performance deltas.
// Only changes made by this session count; settings already present on the PC
// must not be credited to Nebula a second time.
struct ImpactEstimate {
    std::uint64_t ram_mb = 0;
    unsigned cpu_percent = 0;
    unsigned gpu_percent = 0;
    unsigned latency_percent = 0;
};

struct ImpactWeight {
    unsigned cpu;
    unsigned gpu;
    unsigned latency;
};

inline ImpactWeight impact_weight(std::size_t index)
{
    switch (index) {
    // The cloned session plan is isolation, not a performance tweak by itself.
    case 23: case 25: case 37: case 44: return {2, 0, 1};
    case 2: case 3: return {0, 1, 1};
    case 5: return {1, 1, 1};
    case 6: case 7: case 8: case 9: case 10: return {1, 0, 1};
    case 11: case 19: case 20: case 21: case 22:
    case 34: case 35: return {1, 0, 0};
    case 12: return {0, 2, 0};
    case 13: case 14: case 26: return {2, 0, 1};
    case 24: return {0, 1, 0};
    case 28: return {0, 0, 2};
    case 41: case 42: case 43: return {0, 0, 1};
    case 45: case 46: case 47: case 48: case 49: case 50:
    case 51: case 52: case 53: return {0, 0, 1};
    case 54: case 55: case 57: return {1, 0, 1};
    case 56: case 58: case 59: return {1, 0, 0};
    default: return {0, 0, 0};
    }
}

inline ImpactEstimate estimate_impact(
    const std::vector<Optimizer::TweakSetting>& settings,
    const ClosedAppsSummary& apps)
{
    unsigned cpu = (std::min)(apps.closed, 3u);
    unsigned gpu = 0;
    unsigned latency = (std::min)(apps.closed, 2u);
    for (std::size_t index = 0; index < settings.size(); ++index) {
        const auto& setting = settings[index];
        if (!setting.enabled || setting.durable.drift ||
            setting.status != Optimizer::TweakStatus::Applied)
            continue;
        const auto weight = impact_weight(index);
        cpu += weight.cpu;
        gpu += weight.gpu;
        latency += weight.latency;
    }
    const auto estimated_percent = [](unsigned raw) {
        return (std::min)(5u, (raw + 1) / 2);
    };
    return {
        (apps.closed_working_set_bytes + 512 * 1024) / (1024 * 1024),
        estimated_percent(cpu), estimated_percent(gpu),
        estimated_percent(latency)
    };
}
