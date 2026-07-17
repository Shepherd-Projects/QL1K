# QL1K

QL1K is an experimental Quake Live client patch for the current 32-bit Steam
build. **This release is capped at 1,000 FPS.** It uses the game's stock 1 ms
frame floor while keeping command history/network handling from failing at high
latency.

The safe release deliberately does **not** attempt sub-millisecond input or
physics. Movement, mouse input, simulation, and command timing retain the
game's integer-millisecond behavior.

The production configuration deliberately leaves Quake Live's renderer,
OpenGL context ownership, SMP handoff, fonts, HUD, post-processing, and visual
resource lifecycle unchanged. QL1K only patches the stock 1 ms frame boundary,
the client-frame/network command-history path, lifecycle safety seams, and the
native client-accuracy display described below.

## Release notes — renderer-stability fix

- Fixed the observed rare text/scene corruption that made UI, console, and HUD
  text unreadable until `vid_restart`.
- Fixed the associated leave-server freeze/black-screen transition seen in the
  failed renderer-overhaul builds.
- Removed the experimental above-1,000-FPS renderer overhaul and restored stock
  renderer, font, SMP/WGL context, HUD, and post-processing behavior.
- Retained the network/FPS command-history decoupling and native per-hold LG
  client-accuracy calculation.
- User-validated through normal join, leave, menu/UI, and rejoin play with no
  recurrence in the tested release candidate.

## Source-only maintenance cleanup

The repository no longer carries the unused renderer-overhaul helper headers,
their isolated tests, or their obsolete CMake targets. This cleanup does not
change the packaged DLL, injector, configuration, launcher, or checksum
manifest. Existing installations of the renderer-stability release remain
current and do not need to be updated or reinstalled.

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

## Antivirus alerts and false positives

QL1K uses an unsigned DLL and an injector. Those are the same kinds of files
and behaviours that security products watch closely, so some antivirus tools
may flag or quarantine them. A detection is **not** automatically proof of
malware, but it is also not automatically a false positive. Only continue if
you downloaded QL1K from this repository, the published hashes match, and you
are comfortable trusting the code.

Do **not** turn off antivirus protection system-wide. Do not exclude your
Downloads folder, an entire Steam library, a file type such as `.dll`, or a
whole drive. Microsoft warns that exclusions stop Defender's real-time
scanning of the excluded content and can make the device more vulnerable;
scheduled scans and third-party security products may still scan it.

For Microsoft Defender on Windows 10 or 11:

1. First try the normal installation with Defender left on.
2. If QL1K is blocked, open **Windows Security > Virus & threat protection >
   Protection history**. Viewing the details and taking action may require an
   administrator account or approval. Expand the detection and confirm that
   its path is the QL1K download or Quake Live installation. If you have
   reviewed the project and accept the risk:
   - For **Threat found - action needed**, choose **Actions > Allow on
     device**.
   - For **Threat quarantined**, choose **Restore**. Defender may detect the
     restored file again; then choose **Allow on device**.
   - For **Threat blocked**, a removed item, or a SmartScreen block, allowing
     it does not restore the missing file; choose **Allow** so the next copy
     is not blocked automatically.
   After **any** alert that interrupted the download or installation, download
   the current `main` ZIP again, extract the complete archive to a new folder,
   and rerun `install.ps1`. Do not reuse a partial extraction. Let the
   installer verify the fresh payload against `SHA256SUMS.txt`.
3. If Defender repeatedly removes the same verified files, open **Virus &
   threat protection > Manage settings > Exclusions > Add or remove
   exclusions**. Prefer **File** exclusions for the exact installed
   `ql_fps_patch.dll` and `ql_fps_injector.exe`. Use a **Folder** exclusion for
   the exact Quake Live installation only as a last resort. Never broaden the
   exclusion beyond what QL1K needs.

Microsoft documents both [how to review, restore, or allow a detection](https://support.microsoft.com/en-us/windows/security/windows-security/protection-history-in-the-windows-security-app)
and [how exclusions work and why they reduce protection](https://support.microsoft.com/en-us/windows/virus-and-threat-protection-in-the-windows-security-app-1362f4cd-d71a-b52a-0b66-c2820032b65e).
For another antivirus product, use its quarantine/history screen and allow-list
only the exact QL1K files after the same checks; menu names vary by vendor.

The complete source is public in this repository, the installer verifies the
four packaged files against `SHA256SUMS.txt`, and the [build instructions](#build-from-source)
let you compile the binaries yourself. Tools such as Codex or Claude can help
explain or review the source, but an AI review is not proof that software is
safe and should not replace your own security decision.

## Install

`main` is the only supported installation branch. Do not install an old branch,
archive, or previously extracted copy.

1. Close Quake Live completely.
2. [Download the current `main` ZIP](https://github.com/Shepherd-Projects/QL1K/archive/refs/heads/main.zip)
   and extract the complete archive to a new folder, or clone the repository's
   `main` branch.
3. Open PowerShell in that new repository directory.
4. Run:

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

The installer verifies every packaged payload against `SHA256SUMS.txt`, copies
the verified files, verifies the installed copies again, and creates `QL1K.lnk`
on the desktop. If multiple Quake Live installations are detected, it stops and
requires an explicit `-GamePath`. It does not edit `autoexec.cfg`,
`qzconfig.cfg`, or other game configs.

### Installer troubleshooting

- **`Join-Path ... A drive with the name 'D' does not exist`**: this came from
  an older installer probing common Steam drive letters. The current installer
  safely skips drives that are not mounted. Download the current `main` ZIP,
  extract it to a new folder, and run that copy of `install.ps1`.
- **`Get-FileHash ... contains a virus or potentially unwanted software`**:
  Windows or another security provider denied access to that exact file before
  QL1K could verify it. The current installer identifies this condition and
  stops; it never bypasses the checksum. Follow the [antivirus steps above](#antivirus-alerts-and-false-positives),
  including checking both Protection history and any third-party antivirus.
  Visible Windows Security switches being off does not make the blocked file
  readable: restore or allow the exact file in whichever security product or
  policy recorded the detection, then download and extract a fresh ZIP before
  retrying.
- **`Quake Live was not found`**: pass the actual game directory explicitly
  with `-GamePath` as shown above. The directory must contain
  `quakelive_steam.exe`.

Always launch through the new **QL1K** desktop shortcut. It verifies all four
installed payloads, creates Quake Live suspended, injects the verified
`ql_fps_patch.dll`, then resumes the game. Successful activation eventually
appears in `ql_fps_launch.log` as:

```text
status=4 reason=runtime_candidate_active_unverified
```

The start of `ql_fps_launch.log` records the injected DLL path, its verified
SHA-256, and the expected package SHA-256. For this version, both hashes must be:

```text
794EC99C0450D898B570B81FA97213F47EF99A669104394C81A5B5E8506C152B
```

### Minimal renderer rollback

This build fixes the observed rare text/scene corruption by removing the entire
post-1,000-FPS renderer overhaul rather than adding another renderer workaround.
It does not hook WGL/OpenGL, relocate font uploads, replay HUD/player renderer
commands, or add bloom, color, shadow, or player-scene fast paths. The
renderer-corruption diagnostic files from those experimental builds are no
longer produced; normal `ql_fps_telemetry.log` remains available for FPS,
network/history, lifecycle, and client-accuracy state.

The fix was validated through normal join, leave, menu/UI, and rejoin play. To
verify the exact installed release DLL:

```powershell
Get-FileHash "X:\SteamLibrary\steamapps\common\Quake Live\ql_fps_patch.dll" `
  -Algorithm SHA256
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
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
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
