# QL1K

QL1K is an experimental patch for the current 32-bit Steam version of Quake Live. It fixes `com_maxfps` so you can use a normal integer cap from 30 to 1000 FPS, including values such as 500 or 600. QPC-backed pacing keeps fractional cap timing accurate while the game keeps its normal whole-millisecond simulation and input timing.

The FPS value is a ceiling, not a guarantee: the rate you actually reach still depends on your PC and the game workload. The legacy `com_maxfps 250x` setting remains available as an alias for 1000 FPS.

## Install

Requirements: Windows, Quake Live on Steam, and PowerShell 5.1 or newer. QL1K supports one exact game build and stops safely when the game files are unknown.

1. Close Quake Live completely.
2. [Download the tested accuracy-fix ZIP (`c31d565`)](https://github.com/Shepherd-Projects/QL1K/archive/c31d56566fe47e601c5d5ce0e02dbdc9be5b7fa3.zip) and extract it to a **new folder**. This version-specific link contains the corrected DLL and installer; no build tools are required.
3. Open PowerShell in that folder and run:

```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1
```

If Quake Live is in a nonstandard Steam library, provide its directory:

```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1 `
  -GamePath "X:\SteamLibrary\steamapps\common\Quake Live"
```

The installer verifies the packaged files, copies them into Quake Live, verifies them again, and creates a **QL1K** desktop shortcut. It does not edit `autoexec.cfg`, `qzconfig.cfg`, or your other game settings.

Always start the game with the **QL1K** shortcut. Steam's normal Play button does not load the patch.

### Updating an older installation

Downloading or extracting a ZIP does **not** update the installed patch. Close Quake Live, run `install.ps1` from the newly extracted folder, and then start the game with the **QL1K** shortcut. Do not run an installer from an older `QL1K-main` folder. Existing installations do not update automatically.

The tested build above is commit `c31d565`. Its `ql_fps_patch.dll` SHA-256 is:

```text
AF0A92F0AE43AD5A22EC7A17A4201D4354EB198794D71666119AFE5FA74D3BF4
```

The installer prints the installed DLL checksum. It must match the value above for this build. The download link is pinned to this tested version; developers who want the changing source branch can [browse `main`](https://github.com/Shepherd-Projects/QL1K/tree/main) instead.

## Set your FPS cap

Open the Quake Live console or add a value to your own autoexec:

```text
com_maxfps 500
```

Any integer from 30 through 1000 is supported and changes apply without restarting. Values outside that range clamp to the nearest limit. Examples:

```text
com_maxfps 250
com_maxfps 600
com_maxfps 1000
com_maxfps 250x
```

Use `cg_drawFPS 1` to show the achieved frame rate.

## Client LG accuracy

With `cg_drawFPS 1`, QL1K also shows `client accuracy N.NN%` below the FPS counter for your most recently completed continuous lightning-gun hold, or `client accuracy n/a` when that hold could not be measured.

This is a client-side hitreg indicator: client-side opponent contacts divided by native LG fire opportunities. It is useful for seeing what your client predicted, but it is not the server's official hit count, damage, or weapon-stat accuracy.

Contacts use Quake Live's native point trace, native entity bounds and interpolated positions, world obstruction, and server-selected `g_playerCylinders` mode. Each native LG fire event contributes one sample; server hit feedback does not supply the client hit count. Beam width does not enlarge the trace.

For cylinder contacts, this measurement uses the supported game's native default body scale of `1.1`. The local `sv_cylinderScale` cvar is not replicated by the server: an archived client value such as `0` must not collapse the measured body radius. This measurement-only correction leaves your cvar, normal game traces, rendered beam, FPS pacing, and any local server unchanged. A remote server's undisclosed custom cylinder scale cannot be inferred by the client, so this indicator is a native-default reference, not a guarantee for custom collision settings.

## Common install issues

- **Antivirus warning:** QL1K uses an unsigned DLL and injector, which some security products may quarantine. A warning is not automatically safe or malicious. Download only from this repository, review the source and `SHA256SUMS.txt`, and allow or restore only the exact QL1K files if you trust them. Do not disable antivirus protection globally. After a quarantine, extract a fresh ZIP before retrying.
- **Access denied:** If Steam is under `Program Files`, run PowerShell as Administrator or install the game in a Steam library your account can write to.
- **Game not found or multiple copies found:** Run the installer with `-GamePath` as shown above. The selected folder must contain `quakelive_steam.exe`.
- **Checksum or unsupported-build error:** QL1K fails closed when files are incomplete, changed, or from an unsupported game update. Download a fresh copy of QL1K; if Quake Live was just updated, wait for a compatible QL1K build.
- **Patch does not load:** Launch the **QL1K** desktop shortcut instead of Steam's Play button.

## Uninstall

Close Quake Live, then run:

```powershell
powershell -ExecutionPolicy Bypass -File .\uninstall.ps1 `
  -GamePath "X:\SteamLibrary\steamapps\common\Quake Live"
```

This removes the QL1K files and desktop shortcut. Your game configuration remains unchanged.

## Build from source

Use Visual Studio 2022 Build Tools and CMake 3.28 or newer:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Disclaimer

QL1K is experimental client-injection software for one exact Quake Live build. Game updates, overlays, anti-cheat tools, or other DLL modifications may break or reject it. Use it only against bots or on servers whose owners and rules allow client modifications. You accept the risk of crashes, compatibility problems, kicks, or bans.

QL1K is provided under the MIT License without warranty.
