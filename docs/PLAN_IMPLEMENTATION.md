# Attached Windows optimization plan: implementation status

This document tracks the 20 requested items. "Available" means the code path
exists; it does **not** mean a performance gain was measured on this PC. The
standard profile does not close apps, restart Windows, touch the anti-cheat,
inject into a game, or edit GPU/BIOS settings.

| # | Requested feature | Status and limitation |
|---|---|---|
| 1 | Adaptive helper CPU priority | Not implemented. Existing allowlisted helper CPU priority is fixed, not utilization-triggered with hysteresis. |
| 2 | Adaptive helper EcoQoS | Partial. Allowlisted updater EcoQoS is retried for late processes only when its previous state can be read. Other helper EcoQoS is not applied. |
| 3 | Helper CPU Sets | Not implemented. Safe topology and exclusion testing is still required. |
| 4 | Helper memory priority | Partial. Existing allowlisted process memory priority is restored and includes late arrivals, but is not gated by measured memory pressure. |
| 5 | Dedicated updaters | Partial. Edge/Google updater CPU and memory options exist; EcoQoS is conditional on a readable original state. No other updater executable is inferred by name. |
| 6 | Game CPU Sets | Not implemented. No universal P/E-core or CCD placement is imposed. |
| 7 | Game SMT affinity test | Not implemented. Affinity is not changed. |
| 8 | Normal game memory priority | Available for the real game process when its original value is readable; already-normal is reported separately. |
| 9 | Game dynamic thread-priority boost | Available when the previous disabled/enabled value is readable; already-enabled is reported separately. |
| 10 | Session power plan | Available. A copy of the active plan is journaled before activation, then deactivated and deleted on restore/recovery. An external plan switch is preserved. |
| 11 | Core parking | Experimental, off by default. AC minimum cores 100% is attempted for both processor classes when each setting exists. |
| 12 | CPU boost mode | Available on AC. The copied plan is changed from Disabled to Enabled only; an already-enabled mode is kept. |
| 13 | CPU increase threshold | Not implemented. It does not apply to autonomous performance-state systems; support detection is needed before offering it. |
| 14 | CPU decrease threshold | Not implemented for the same reason; temperature/clock effects also require testing. |
| 15 | Wi-Fi power saving | Available on AC when the copied plan exposes the setting; it does not restart the adapter. It does not detect whether Wi-Fi is currently used. |
| 16 | USB selective suspend | Diagnostic option, off by default, via the copied plan only. No latency gain is claimed. |
| 17 | Maximum display refresh | Partial. Tests a same-resolution/color/orientation mode on the primary display and restores the previous frequency. It does not identify the game's display in a multi-monitor setup or persist the full DEVMODE. |
| 18 | FilterKeys/accessibility shortcuts | Available as an explicit default-off option for FilterKeys, StickyKeys and ToggleKeys activation hotkeys. An accessibility feature already in use is respected; original structures are journaled and restored unless another application changed them. |
| 19 | Selected BITS transfers | Not implemented. Transfer ownership/consent and safe resume rules are needed. |
| 20 | Tasks and services | Not implemented. No tasks/services are paused or disabled. |

## Recovery and verification

The journal is written to a temporary file and atomically replaced before each
change. Process entries include PID and creation time. New process entries also
store the value Nebula intended to apply; restoration checks that current value
before writing the original. A conflicting external change is not overwritten,
and the recovery journal is kept. Process restoration runs in reverse order.
The same compare-before-restore rule is used for new registry, power-setting,
and display entries; registry values already at the desired state are no longer
journaled as changes. Legacy journal entries remain readable.

The navigation and preference tests compile and pass. The application builds
with `-Wall -Wextra -Werror`. No live A/B FPS, 1% low, frametime, input latency,
anti-cheat, hybrid-CPU, multi-monitor, crash-recovery, or hardware power-policy
test has been run here; those results must not be inferred from compilation.

The requested external watchdog is not implemented. If Nebula crashes, recovery
runs on the next launch rather than immediately. Changes to Game Bar/MMCSS
registry values also remain part of the pre-existing engine; they are restored
from the journal, but were not independently verified for immediate per-game
effect in this change.

API references: [power-scheme lifecycle](https://learn.microsoft.com/en-us/windows/win32/power/managing-power-schemes),
[process memory priority and EcoQoS](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessinformation),
[core parking minimum](https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/options-for-core-parking-cpmincores),
and [CPU increase-threshold applicability](https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/options-for-perf-state-engine-perfincreasethreshold).
