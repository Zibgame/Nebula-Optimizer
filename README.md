# Nebula Optimizer

A lightweight Windows game optimizer with automatic attach and session restore.

## Use

1. Run `Nebula-Optimizer.exe` and approve the Windows UAC prompt.
   On startup, Nebula closes older instances from the same directory and restores their session before starting a new one.
2. Create or select a profile. Nebula checks the active profile every 2.5 seconds and attaches when the actual game process starts. It restores temporary settings when the game exits.
3. Press `1` to launch the selected game yourself, `2` to manage profiles, or `3` for Windows-only optimization. During a session, the tweak carousel is open by default: hover over a category or press Left/Right to switch immediately, use Up/Down or `J/K` to select a tweak, and press Enter to toggle it. The `Saved` tab lists durable settings; `S` saves or unsaves the selected setting, and `R` deliberately reapplies a saved setting marked `Drift`. `P` remains the capture shortcut. Press Esc to collapse the carousel or `L` to show it again. Press `M` to hide to the notification area or `0` to stop and restore.
4. Optional: place the official `PresentMon.exe` next to Nebula (or on `PATH`) and press `P` during a game for a 10-second input-to-display capture. This is not an edit-to-action measurement. No capture runs in the background by default.

Fortnite launches through its official Epic desktop shortcut. Nebula targets `FortniteClient-Win64-Shipping.exe`, not its anti-cheat or launcher.

Registry values, power plan, and changed process priorities are journaled at `%LOCALAPPDATA%\NebulaOptimizer\session-journal.json`. Nebula recovers them after an unexpected exit. A successful restore triggers one notification.

Tweak choices are saved at `%LOCALAPPDATA%\NebulaOptimizer\tweaks.json`. During an active game, a change restores and reapplies the session; while waiting for Fortnite to start, the change takes effect next session. Optional Game Bar shortcut tweaks start off because they can interfere with recording controls.

Nebula scans durable settings without changing Windows after session recovery.
`baseline.json` keeps the first, first available, and latest raw observations;
`Base` means the first available observation already matched Nebula's target, while `Unknown` means it
could not be established. `saved.json` stores retained settings (automatic `Base`
imports and manual choices),
and `saved-operation.json` is a separate crash-recovery transaction. A yellow
`*` marks `Saved`; unpinning leaves the Windows value as-is. `Drift` means a
saved value changed outside Nebula: it is not silently reapplied, even when a
session starts. The normal session still restores its own pre-session values,
not the baseline scan. Process priorities, sleep prevention and the temporary
power-plan clone cannot be saved. Saving a power setting changes the base plan
identified by its GUID; GPU preference is tied to the exact game executable.
Display saving requires a 15-second visual confirmation, and experimental,
diagnostic or accessibility settings require an additional confirmation.
At startup, a read-only scan automatically adds already-matching durable `Base`
settings to `Saved`; it does not apply anything to Windows. `[S] Scan PC` on the
home screen repeats the scan. If you unpin a setting, Nebula records that choice
in `saved-dismissed.json` and will not import it again automatically. A later
manual `S` can save it again.

The 41 session controls are grouped into five categories plus the `Saved` tab. The extra controls
cover experimental core parking, AC Wi-Fi performance, and diagnostic USB
selective suspend, plus optional accessibility hotkeys. Core parking, USB
diagnostics, and accessibility hotkeys default off. Power settings
are applied to a temporary copy of the active plan, which is deleted after the
session. The session view shows color-coded `Applied`, `Skipped`, and `Failed`
counts. `Applied` includes settings already at the target value; it does not
imply a measured performance gain. `Off` and `Drift` remain separate and are
not counted. The left-aligned layout is preserved, while live metrics update
without clearing the whole frame each time. Every changed value is journaled
for restoration. On startup, Nebula retires older instances
from its own directory, then recovers their session before allowing a new one.
If recovery is incomplete, close the game and press `R` to retry.

The attached optimization plan is only partially implemented. See
[docs/PLAN_IMPLEMENTATION.md](docs/PLAN_IMPLEMENTATION.md) for the exact status
of each requested feature and the remaining safety limitations.

## Build

Requires Windows, MSYS2/MinGW, `g++`, `windres`, and `mingw32-make`.

```powershell
mingw32-make
```

See [docs/OPTIMIZATIONS.md](docs/OPTIMIZATIONS.md) for tweak rationale and limitations. No specific FPS or input-latency gain is guaranteed.
