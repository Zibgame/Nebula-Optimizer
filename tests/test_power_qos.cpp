#include "power_qos.hpp"
#include <iostream>

int main()
{
    constexpr DWORD execution = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    constexpr DWORD other = 0x80;
    const auto automatic = explicit_high_qos_masks(0, 0);
    const auto eco = explicit_high_qos_masks(execution | other, execution | other);
    const auto high = explicit_high_qos_masks(execution | other, other);
    const auto background = explicit_eco_qos_masks(other, 0);
    const bool okay = explicit_high_qos_required(0, 0) &&
        explicit_high_qos_required(execution, execution) &&
        !explicit_high_qos_required(execution, 0) &&
        automatic.control == execution && automatic.state == 0 &&
        eco.control == (execution | other) && eco.state == other &&
        high.control == (execution | other) && high.state == other &&
        background.control == (execution | other) && background.state == execution;
    if (!okay) std::cerr << "Power QoS state test failed\n";
    return okay ? 0 : 1;
}
