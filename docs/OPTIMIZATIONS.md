# Nebula optimization policy

Nebula applies reversible session tweaks immediately before a game starts and
attempts to restore previous values when the session ends. It deliberately does
not copy source code from other optimizers.

## Applied in the current engine

- Duplicate the active power plan for the session, then reactivate the previous
  plan and delete the copy. Recovery tracks the copy by GUID after a crash.
- On AC power, favor CPU performance through the documented EPP setting. Save
  the original value and restore it even after a crash. This may increase power
  use and heat; firmware/Windows power-mode overlays can limit the effect.
- Offer PCIe link power saving off as an opt-in AC-only setting with exact
  rollback. It is not assumed to improve every GPU or PC.
- On AC power, enable CPU boost mode only when it was disabled; leave existing
  enabled/aggressive modes alone. This can increase heat.
- Offer core parking minimum at 100% for both processor classes as an
  experimental, default-off option. Offer Wi-Fi maximum performance on AC and
  USB selective suspend off as a default-off diagnostic option; skip missing
  power settings.
- Offer a default-off accessibility-hotkey option: if FilterKeys, StickyKeys,
  or ToggleKeys is not in use, temporarily prevent its activation shortcut and
  restore the original structures after the session.
- Offer a separate default-off FilterKeys fast-repeat preset through Windows' own
  accessibility API. It never replaces an already-active custom FilterKeys setup.
  Keyboard repeat delay/rate are separate default-off session controls. Nebula
  never changes mouse acceleration, sensitivity, speed, or pointer curves.
  These keyboard controls affect Windows input behavior, not necessarily a game
  using raw input; each original value is journaled and restored.
- Offer an experimental AC minimum processor state of 100% on the temporary
  power plan. It may reduce frequency ramping at the cost of heat and power;
  it is not enabled by default.
- If needed, re-enable the game's dynamic priority boosts and restore its
  previous memory priority to normal; both are skipped when already optimal.
- Temporarily select the highest tested refresh rate at the primary display's
  current resolution and color depth, then restore the previous mode.
- Lower memory priority for eligible cloud-sync, indexing, Widgets, Phone Link,
  Adobe, and software-update helpers. Updater helpers can also receive lower
  CPU priority and EcoQoS. None of these settings stops the process or service.
  A low-frequency scan includes helpers that start after the game session.
- Prevent sleep and display blanking only while the optimization session exists.
- Disable Game DVR, background capture, and the Game Bar startup panel for the
  session.
- Enable Windows Game Mode, the supported foreground-game resource mechanism.
- Set the selected executable to the Windows high-performance GPU preference.
- Launch a directly configured game suspended, set High process priority,
  disable execution-speed power throttling/EcoQoS, then resume it.
- Tune MMCSS using valid documented values: disable multimedia network
  throttling, reserve 10% for low-priority work, and raise the Games task.
- Favor the foreground application's short, variable CPU quanta.
- Roll back every registry value to its exact former data/type, including
  deleting values that did not exist before the session.
- Roll back automatically if the game cannot be launched.
- Record registry values, power plan, AC power settings and changed background process priorities in
  a write-through session journal so the next launch can recover after a crash.
- Keep the monitor low overhead: sample the dashboard every two seconds, and
  skip its rendering/CPU sampling while hidden in the notification area.
- Render each dashboard frame in memory, then write it once without clearing
  the console first; this avoids the visible flashing of the previous UI.
- Open a horizontal five-category carousel by default. Hover or press Left/Right
  to switch categories without Enter; Up/Down selects a tweak, Enter toggles it,
  and Esc collapses the carousel.
  Each row reports its session state, including already set, unsupported, or
  access denied where detected. Choices persist separately
  from the crash-recovery journal.
- Offer four Game Bar shortcut toggles as opt-in only. They are usability
  controls, not evidence of a measurable FPS or input-latency improvement.
- Launch Fortnite through its Epic shortcut, then attach only to the real
  `FortniteClient-Win64-Shipping.exe` path. Never tune EAC/EOS or the launcher.

## Popular tweaks intentionally rejected

These are common in optimizer scripts but are not unconditional optimizations:

- **Emptying working sets / standby memory:** creates hard page faults and can
  worsen the first minutes of gameplay. Windows reclaims memory on demand.
- **Disabling TCP receive auto-tuning:** can cap throughput severely. Most games
  use UDP for real-time traffic, so TCP/Nagle registry hacks usually do nothing
  for in-game latency.
- **Flushing DNS before every launch:** throws away a useful cache and cannot
  improve an already-resolved game-server route.
- **`SystemResponsiveness=0`:** Microsoft documents that values below 10 are
  clamped; zero is not a special unlimited-gaming mode.
- **MMCSS `GPU Priority`:** Microsoft documents this value as currently unused.
- **Forcing MMCSS `Scheduling Category=High`:** Windows then treats task Priority
  as 2 and reserves that category for pro-audio workloads.
- **Killing WMI, Search, Explorer, launchers, Discord, or random services:** this
  is destructive, breaks monitoring/anti-cheat/launch flows, and is not exactly
  reversible.
- **Disabling security mitigations, Defender, VBS, HPET, dynamic tick, or CPU idle
  states:** reboot-level, hardware-dependent changes with security, stability,
  anti-cheat, heat, or battery costs. They need controlled A/B benchmarks and are
  unsuitable for an automatic per-game session.
- **Disabling USB selective suspend globally:** Microsoft strongly recommends
  leaving it available. It is not a general mouse-latency optimization.
- **Global MSI/interrupt affinity, HAGS, MPO, FSO, and core-affinity forcing:**
  useful only on specific hardware or for specific faults. A universal value can
  reduce performance, especially on hybrid CPUs and current Windows 11 builds.

## Research baseline

The implementation was compared against the public tweak inventories and safety
guidance of Chris Titus Tech WinUtil, AtlasOS, ReviOS, SophiApp, and Hellzerg
Optimizer, then checked against Microsoft documentation for MMCSS, timer
resolution, TCP auto-tuning, process power throttling, and Game Mode.

The rule is simple: a 0.1% improvement is welcome when it has a plausible path,
can be measured, and can be restored. A tweak is not included merely because it
appears in many scripts.

MMCSS `Tasks\Games`, network throttling, system responsiveness, and
`Win32PrioritySeparation` are experimental and off by default. A registry value
that is present or written/read back is shown as `Set`; Nebula does not claim
that Fortnite registered its important threads with the MMCSS task. The GPU
preference is written before a Nebula launch. On an already-running game it is
shown as `Next run`, because the current process cannot be proven to use it.

EcoQoS handling distinguishes Windows automatic policy (`ControlMask=0`) from
explicit EcoQoS and explicit HighQoS. Nebula preserves unrelated power-
throttling bits, verifies the requested masks, and journals the exact original
state for compare-and-swap restoration.

Nebula displays actual CPU and RAM usage. It does not invent a physical
input-to-edit figure. An on-demand 10-second capture with a user-supplied
official `PresentMon.exe` parses the complete CSV period and reports valid
sample count, coverage, median/mean/p95/p99, frame pacing, FPS, 1% low,
stutters, and available CPU/GPU/click columns. Missing, invalid, and unsupported
schemas are distinct. `A/B test` runs three warmed baseline/optimized pairs,
reports dispersion-aware improvement/regression, and records the raw summaries,
session changes, errors, and basic hardware inventory. These software telemetry
metrics do not prove full peripheral-to-display latency or that a particular
Fortnite edit succeeded.
Full end-to-end click-to-display testing can
also use a compatible NVIDIA Reflex Analyzer monitor and mouse. Epic's 2026
Fortnite guide recommends Reflex On + Boost for NVIDIA users; Nebula does not
silently edit Fortnite's in-game graphics settings.

Razer Cortex's published booster description focuses on suspending non-essential
background processes when a game starts. Nebula takes the safer, reversible
approach of temporarily reducing selected helper priorities instead of killing
them, and automatically attaches to a configured game that was launched outside
Nebula. AtlasOS lists many additional changes, but several require reboot,
Explorer restart, or permanent security/usability tradeoffs and cannot be
honestly treated as per-session gaming boosts.

### Sources reviewed

- Microsoft MMCSS: https://learn.microsoft.com/windows/win32/procthread/multimedia-class-scheduler-service
- Microsoft TCP auto-tuning: https://learn.microsoft.com/windows-server/networking/technologies/network-subsystem/net-sub-performance-tuning-nics
- Microsoft timer resolution: https://learn.microsoft.com/windows/win32/api/timeapi/nf-timeapi-timebeginperiod
- Microsoft Game Mode: https://learn.microsoft.com/previous-versions/windows/desktop/gamemode/game-mode-portal
- WinUtil: https://github.com/ChrisTitusTech/winutil
- AtlasOS: https://github.com/Atlas-OS/Atlas
- ReviOS: https://github.com/meetrevision/playbook
- SophiApp: https://github.com/Sophia-Community/SophiApp
- Optimizer: https://github.com/hellzerg/optimizer
- Razer Cortex: https://www.razer.com/cortex/booster
- Microsoft Windows 11 Game Bar settings: https://learn.microsoft.com/en-us/windows/apps/develop/settings/settings-windows-11
- PresentMon console metrics: https://github.com/GameTechDev/PresentMon/blob/main/README-ConsoleApplication.md
- Epic Games Fortnite settings: https://store.epicgames.com/news/fortnite-on-pc-best-settings-for-competitive-play-in-2026
- NVIDIA Reflex Analyzer: https://www.nvidia.com/en-us/geforce/technologies/reflex/
- Microsoft EPP: https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/options-for-perf-state-engine-perfenergypreference
- Microsoft PCIe ASPM: https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/pci-express-settings-link-state-power-management
- Microsoft USB selective suspend: https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-selective-suspend
- Microsoft process memory priority: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessinformation
- Microsoft process priority boost: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocesspriorityboost
- Microsoft display modes: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-changedisplaysettingsexw
- OpenGameBoost: https://github.com/bensheed/OpenGameBoost
- GameShift: https://github.com/lhceist41/GameShift
- ThreadPilot: https://github.com/PrimeBuild-pc/ThreadPilot
