# Restoration reference

`Scan PC` is an explicit reference snapshot. Values that can be read are stored
with their exact target identity in `%LOCALAPPDATA%\NebulaOptimizer\baseline.json`.
Missing, inaccessible, process-only, or unsupported targets are `Unknown`; no
default value is invented for them.

| Tweaks | Reference and restoration |
|---|---|
| 0 | The active power-plan GUID is read each session. Nebula edits a clone, restores the original active plan, and deletes the clone. |
| 1, 57 | Windows execution-state and timer-resolution requests are reference-counted requests and are released, not replaced with guessed values. |
| 2–10, 15–18 | Exact registry existence, type, and bytes are scanned and journaled. Missing values are restored as missing. |
| 11, 13–14, 19–22, 26–27, 29–36 | Process priority, EcoQoS, memory retention, and priority-boost state are read for each PID plus creation time immediately before modification. Dynamic process state cannot be meaningfully scanned before the process exists. |
| 12 | The exact per-game GPU-preference registry value is scanned for the selected executable path. |
| 23–25, 37–39, 44 | The exact AC value and power-plan GUID are scanned; session changes are made only on the temporary clone. |
| 28 | The complete display mode and device identity are journaled. The scan stores the available display reference; the live session also snapshots the monitor actually used by the game. |
| 40–43 | Complete FilterKeys/StickyKeys/ToggleKeys structures and keyboard repeat values are scanned and restored. An intentionally enabled accessibility function is not treated as an optimization. |
| 45–53 | DNS interface GUID and exact server list, NIC device identity, registry value type, and raw driver value are scanned. Unsupported driver properties remain unknown. |
| 54–56 | CPU Sets and I/O priorities are read from the identified process immediately before modification. Restoration uses PID plus creation time and compare-and-swap. |
| 58–59 | The service state is read immediately before pause/stop and restored only when it still matches Nebula's applied state. Startup type is never changed. |

Settings explicitly placed in `Saved` are excluded from baseline restoration,
because the user chose to keep them. For every other scannable setting, normal
exit and crash recovery restore the explicit scan reference. Session-only
process settings restore their exact pre-session value. If recovery cannot read
or verify a target, the recovery journal is kept and new modifications are
blocked rather than assuming a value.
