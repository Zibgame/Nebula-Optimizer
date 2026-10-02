#pragma once
#include <windows.h>

struct PowerQosMasks { DWORD control; DWORD state; };

inline bool explicit_high_qos_required(DWORD control, DWORD state)
{
    return !(control & PROCESS_POWER_THROTTLING_EXECUTION_SPEED) ||
           (state & PROCESS_POWER_THROTTLING_EXECUTION_SPEED);
}

inline PowerQosMasks explicit_high_qos_masks(DWORD control, DWORD state)
{
    return {control | PROCESS_POWER_THROTTLING_EXECUTION_SPEED,
            state & ~PROCESS_POWER_THROTTLING_EXECUTION_SPEED};
}

inline PowerQosMasks explicit_eco_qos_masks(DWORD control, DWORD state)
{
    return {control | PROCESS_POWER_THROTTLING_EXECUTION_SPEED,
            state | PROCESS_POWER_THROTTLING_EXECUTION_SPEED};
}
