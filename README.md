# Nebula Optimizer

A lightweight Windows game optimizer with automatic attach and session restore.

## Use

For a Windows 10/11 64-bit user, distribute only `Nebula-Optimizer.exe`.
MinGW, Make and the source tree are needed to build it, not to run it.
Windows will request administrator approval. Profiles, tweak preferences and
recovery files are created under `%LOCALAPPDATA%\NebulaOptimizer`.
Existing profiles beside an older copy of Nebula are imported without replacing
profiles already stored there. `PresentMon.exe` is optional and is needed only
for the `P` capture feature.

1. Run `Nebula-Optimizer.exe` and approve the Windows UAC prompt.
   On startup, Nebula closes older instances from the same directory and restores their session before starting a new one.
2. Create or select a profile. Nebula checks the active profile every 2.5 seconds and attaches when the actual game process starts. It restores temporary settings when the game exits.
3. Use Up/Down or `J/K` on the home screen and press Enter to play, manage profiles, optimize Windows, or scan. Esc hides Nebula to the notification area. The same navigation selects profiles, background applications, and confirmation choices. During a session, hover over a category or press Left/Right or `H/L` to switch, use Up/Down or `J/K` to select a tweak, and press Enter to toggle it. The `Saved` tab lists durable settings. Press Tab to focus the compact action bar, then use arrows or `H/J/K/L` and Enter for Save, Reapply, Capture, A/B test, Tray, or Stop. Esc returns to the tweaks; Esc again collapses them. Enter reopens them. The old single-key shortcuts still work, but are not required.
4. Optional: place the official `PresentMon.exe` next to Nebula (or on `PATH`). `Capture` measures one 10-second pass. `A/B test` performs three baseline and three optimized passes after warm-up and saves `%LOCALAPPDATA%\NebulaOptimizer\benchmark-last.json`. No capture runs in the background by default.

In the session's `Apps` carousel, select `Close apps` and press Enter to add a
running application or its exact `.exe` path. Apps added during a session are
asked to close when you return to the carousel, then at the start of later sessions.
Only selected apps in your Windows session are asked to close when optimization
starts. Nebula sends a normal window-close request and never force-terminates
them. Apps without a closable window, apps that refuse, the game, anti-cheat,
Nebula and Windows system processes are left running. The session shows closed,
skipped and failed process counts. `Gain ~` estimates RAM from the working
sets of closed apps and shows heuristic CPU, GPU and latency percentages for
tweaks actually changed in this session. These percentages are not measured
performance improvements; RAM working sets can include shared pages.
Closed apps do not automatically restart when Nebula exits.
Choices are stored in `%LOCALAPPDATA%\NebulaOptimizer\apps-to-close.json`.

Fortnite launches through its official Epic desktop shortcut. Nebula targets `FortniteClient-Win64-Shipping.exe`, not its anti-cheat or launcher.

Registry values, power plan, and changed process priorities are journaled at `%LOCALAPPDATA%\NebulaOptimizer\session-journal.json`. Nebula recovers them after an unexpected exit. A successful restore triggers one notification.

Before the first optimization, Nebula offers an explicit baseline scan. That
scan becomes the restoration reference for every readable durable setting;
unknown targets are never guessed. See [docs/RESTORATION.md](docs/RESTORATION.md)
for the complete 60-tweak restoration map.

Tweak choices are saved at `%LOCALAPPDATA%\NebulaOptimizer\tweaks.json`. During an active game, a change restores and reapplies the session; while waiting for Fortnite to start, the change takes effect next session. Optional Game Bar shortcut tweaks start off because they can interfere with recording controls.

Nebula scans durable settings without changing Windows after session recovery.
`baseline.json` keeps historical observations plus the explicit restoration
snapshot and exact target identities; `Base` means the scanned observation
already matched Nebula's target, while `Unknown` means it
could not be established. `saved.json` stores retained settings (automatic `Base`
imports and manual choices),
and `saved-operation.json` is a separate crash-recovery transaction. A yellow
`*` marks `Saved`; unpinning leaves the Windows value as-is. `Drift` means a
saved value changed outside Nebula: it is not silently reapplied, even when a
session starts. A normal session first restores its exact pre-session state,
then restores the explicit scan reference for durable settings that are not
`Saved`. Dynamic process settings use their exact pre-session state because
they do not exist reliably at scan time. Process priorities, sleep prevention and the temporary
power-plan clone cannot be saved. Saving a power setting changes the base plan
identified by its GUID; GPU preference is tied to the exact game executable.
Display saving requires a 15-second visual confirmation, and experimental,
diagnostic or accessibility settings require an additional confirmation.
The explicit read-only scan adds already-matching durable `Base` settings to
`Saved`; it does not apply anything to Windows. `Scan PC` on the home screen
replaces the restoration reference with a fresh snapshot. If you unpin a setting, Nebula records that choice
in `saved-dismissed.json` and will not import it again automatically. A later
manual `S` can save it again.

The 60 session controls are grouped into six categories plus the `Saved` tab. The extra controls
cover experimental core parking and AC CPU minimum state, AC Wi-Fi performance,
diagnostic USB selective suspend, optional accessibility hotkeys, FilterKeys
fast repeat and keyboard repeat controls. Mouse acceleration, sensitivity,
speed and pointer curves are never modified. The new input
controls and aggressive power controls default off. Power settings
are applied to a temporary copy of the active plan, which is deleted after the
session. The session view deliberately keeps only three results: `Applied`
(already present or successfully configured), `Not applied`, and `Failed`.
None implies a measured performance gain. Saved and drift information remains
available through the Saved tab and is
not counted. The left-aligned layout is preserved, while live metrics update
without clearing the whole frame each time. Every changed value is journaled
for restoration. A hidden watchdog from the same single EXE verifies the parent
PID and creation time and restores the journal immediately after a crash. On
startup, Nebula retires older interactive instances from its own directory,
preserves the watchdog, waits for recovery, then starts.
During an active session the monitor thread runs below normal priority, hidden
UI updates sleep, helper discovery is rate-limited, and display discovery stops
after a valid monitor was checked. PresentMon is never started automatically:
only `Capture` or `A/B test` runs it.
`Apply All` enables the complete catalog and selects the aggressive value preset
after one confirmation. On ordinary
rows, Left/Right keeps moving between tabs. On rows with an inline value,
Left/Right adjusts that value in its native unit instead; FilterKeys repeat uses
5 ms steps. Holding an arrow relies on Windows key repeat. Toggle and value
changes rebuild the active session immediately. An animated `Applying` indicator
stays visible during this short blocking operation so the pause is clearly
intentional. `H/L` always moves between tabs. `Restart required` counts as
applied because the Windows configuration was written; missing targets and
unsupported hardware remain honestly listed as not applied.
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
