# QL1K

QL1K is an experimental Quake Live client patch for the current 32-bit Steam
build. It targets stable high-framerate play around the engine's 1 ms boundary
(up to roughly 1,000 FPS) while keeping command history/network handling from
failing at high latency.

The safe release deliberately does **not** attempt sub-millisecond input or
physics. Movement, mouse input, simulation, and command timing retain the
game's integer-millisecond behavior.

The production configuration uses zero-queued-frame persistent SMP. Mouse and
keyboard state still enter through Quake Live's stock input and ~1 kHz command
path, while each renderer command list is acknowledged before another frame is
built. This removes the asynchronous one-frame render backlog without returning
the OpenGL context to the main thread every frame. On the controlled reference
demo, median presentation rate remained 2,585 FPS versus 3,957.5 FPS for the
queued control.

## Important disclaimer

Use this only against bots or on servers whose owners/rules explicitly allow
client modifications. Anti-cheat systems or server administrators may treat
injected DLLs as prohibited modifications. You accept all account, kick, ban,
compatibility, and data-loss risk. The authors and contributors are not
responsible for bans or other consequences.

## Requirements

- Quake Live on Steam, Windows, 32-bit game executable.
- Exact supported game/cgame build. Unknown hashes fail closed.
- PowerShell 5.1 or newer.

## Install

1. Download or clone this repository.
2. Open PowerShell in the repository directory.
3. Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1
```

If Steam is under `Program Files`, open PowerShell as Administrator or use a
Steam library where your Windows account has write permission.

If Quake Live is in a less common Steam library:

```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1 `
  -GamePath "X:\SteamLibrary\steamapps\common\Quake Live"
```

The installer copies only QL1K files and creates `QL1K.lnk` on the desktop.
It does not edit `autoexec.cfg`, `qzconfig.cfg`, or other game configs.

Always launch through the new **QL1K** desktop shortcut. It starts Quake Live,
waits for the process, then injects `ql_fps_patch.dll`. Successful activation
eventually appears in `ql_fps_launch.log` as:

```text
status=4 reason=runtime_candidate_active_unverified
```

## Game setting

Set the known Quake Live high-FPS value in the console or your own autoexec:

```text
com_maxfps 250x
```

`250x` exploits the game's split float/integer parsing: it preserves a valid
float value while producing integer zero, selecting the engine's 1 ms frame
floor. A plain `com_maxfps 0` is not uncapped; this build clamps it to 30 FPS.

Useful display setting:

```text
cg_drawFPS 1
```

## LG client accuracy

With `cg_drawFPS 1`, QL1K draws `client accuracy N.NN%` directly below the FPS
counter using the same native font, scale, color, and right alignment. It shows
only the most recently completed lightning-gun fire hold. Starting another hold
immediately hides the previous result; releasing fire publishes the new result
after its command-time boundary is acknowledged. The result then remains until
the next LG hold.

The displayed value is:

```text
client player contacts / native LG fire opportunities * 100
```

It is rounded to the nearest hundredth and always shows two decimal places, such
as `83.33%`, `62.50%`, or `100.00%`. Server hit sounds, `PERS_HITS`, damage values,
weapon-stat percentages, and session totals cannot affect this number.

Timing is event-driven. QL1K hooks the predictor's native `EV_FIRE_WEAPON`
insertion, keys each opportunity by its generating playerstate command time,
and accepts each time only once across prediction replays. It never estimates LG
cadence from rendered frames or an elapsed timer.
Each accepted event immediately performs exactly one client point trace, so the
denominator and its contact result cannot be changed by later render frames.

The trace reproduces the supported game binary's own LG calculation: native
`AngleVectors`, the event playerstate's view angles, snapped muzzle origin,
viewheight, crouch/standing muzzle offset, 768-unit range, serverinfo collision
mask, skip entity, and native cgame point-trace wrapper. That wrapper performs
Quake's world trace and active-solid entity clipping, including each player's
server-transmitted bounds and current cgame position. This respects
server-customized player hitboxes without inferring them from model size.
Teammates, spectators, the local player, and unassigned clients are excluded.
Rendered LG geometry is never used and nearby frames are never OR'd together.

`ql_fps_telemetry.log` exposes the immutable HUD inputs as
`client_accuracy_kind`, `client_accuracy_percent_hundredths`,
`client_accuracy_hits`, and `client_accuracy_opportunities`, plus the latest
hold generation and start/end command times. Existing `hitreg_*` and
session-feedback fields remain diagnostic only and are not displayed inputs.
Rendering performs no file I/O.

## Uninstall

Close the game, then run:

```powershell
powershell -ExecutionPolicy Bypass -File .\uninstall.ps1 `
  -GamePath "X:\SteamLibrary\steamapps\common\Quake Live"
```

## Build from source

Visual Studio 2022 Build Tools and CMake 3.28+:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32 `
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Outputs: `build\Release\ql_fps_patch.dll` and
`build\Release\ql_fps_injector.exe`.

SafetyHook and Zydis are included under `third_party` with their upstream
licenses. QL1K source is MIT licensed.

## Limits

- Exact-build patch; game updates may require new signatures.
- Finite command history; no claim of support for every possible ping.
- Injection may conflict with overlays, anti-cheat, or other DLL mods.
- Steam's normal Play button does not inject QL1K; use the created shortcut.
- Live runtime confirmation of event cadence, custom-server solids, and HUD
  placement is required after a supported game update or environment change.
