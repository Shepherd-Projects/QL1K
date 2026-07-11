# QL1K

QL1K is an experimental Quake Live client patch for the current 32-bit Steam
build. It targets stable high-framerate play around the engine's 1 ms boundary
(up to roughly 1,000 FPS) while keeping command history/network handling from
failing at high latency.

The safe release deliberately does **not** attempt sub-millisecond input or
physics. Movement, mouse input, simulation, and command timing retain the
game's integer-millisecond behavior.

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
