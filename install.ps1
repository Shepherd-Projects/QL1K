param(
    [string]$GamePath,
    [switch]$NoShortcut
)
$ErrorActionPreference = 'Stop'

$payloads = [ordered]@{
    'bin/ql_fps_patch.dll'    = 'ql_fps_patch.dll'
    'bin/ql_fps_injector.exe' = 'ql_fps_injector.exe'
    'ql_fps.cfg'              = 'ql_fps.cfg'
    'ql_fps_launch.ps1'       = 'ql_fps_launch.ps1'
}
$manifestPath = Join-Path $PSScriptRoot 'SHA256SUMS.txt'

function Read-Checksums([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Checksum manifest was not found: $Path"
    }
    $checksums = @{}
    Get-Content -LiteralPath $Path | ForEach-Object {
        if ($_ -match '^([0-9A-Fa-f]{64})\s+(.+)$') {
            $checksums[$Matches[2].Trim().Replace('\', '/')] = $Matches[1].ToUpperInvariant()
        }
    }
    return $checksums
}

function Assert-Checksum([string]$Path, [string]$Expected, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label was not found: $Path"
    }
    try {
        $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
    } catch {
        $detail = $_.Exception.Message
        if ($detail -match '(?i)virus|malware|potentially unwanted|0x800700e1') {
            throw @"
Security software blocked access to $Label at: $Path

QL1K cannot install while this file is blocked, and checksum verification will not be bypassed.
Open Windows Security > Virus & threat protection > Protection history, plus any third-party antivirus history, and review the detection. If you trust this repository, allow or restore the exact QL1K file. Then download the current main ZIP again, extract it to a new folder, and rerun install.ps1.

If the Windows Security switches already appear disabled, another antivirus provider, Windows policy, or a still-active security service may be enforcing the block.

Windows reported: $detail
"@
        }
        throw @"
$Label could not be read at: $Path

Security software, file permissions, or an incomplete download may be blocking access. Check Windows Security > Virus & threat protection > Protection history and any third-party antivirus history, then download and extract a fresh current main ZIP before retrying.

Windows reported: $detail
"@
    }
    if ($actual -ne $Expected) {
        throw "$Label checksum mismatch. Expected $Expected but found $actual at: $Path"
    }
}

function Find-QuakeLiveInstall([string[]]$Candidates) {
    return @($Candidates |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        Select-Object -Unique |
        Where-Object {
            $gameExecutable = [IO.Path]::Combine($_, 'quakelive_steam.exe')
            Test-Path -LiteralPath $gameExecutable -PathType Leaf
        })
}

$checksums = Read-Checksums $manifestPath
foreach ($sourceName in $payloads.Keys) {
    if (-not $checksums.ContainsKey($sourceName)) {
        throw "Checksum manifest is missing required payload: $sourceName"
    }
    Assert-Checksum (Join-Path $PSScriptRoot $sourceName) $checksums[$sourceName] "Package file '$sourceName'"
}

if (-not $GamePath) {
    $candidates = @(
        "${env:ProgramFiles(x86)}\Steam\steamapps\common\Quake Live",
        'C:\SteamLibrary\steamapps\common\Quake Live',
        'D:\SteamLibrary\steamapps\common\Quake Live',
        'E:\SteamLibrary\steamapps\common\Quake Live'
    )
    $matches = @(Find-QuakeLiveInstall $candidates)
    if ($matches.Count -gt 1) {
        throw "Multiple Quake Live installations were found. Re-run with -GamePath and choose one: $($matches -join ', ')"
    }
    $GamePath = $matches | Select-Object -First 1
}
if (-not $GamePath -or -not (Test-Path -LiteralPath ([IO.Path]::Combine($GamePath, 'quakelive_steam.exe')) -PathType Leaf)) {
    throw 'Quake Live was not found. Run: .\install.ps1 -GamePath "X:\...\Quake Live"'
}
$GamePath = (Resolve-Path -LiteralPath $GamePath).Path
$game = Join-Path $GamePath 'quakelive_steam.exe'
$running = Get-Process -Name quakelive_steam -ErrorAction SilentlyContinue | Where-Object {
    try { [string]::Equals($_.Path, $game, [StringComparison]::OrdinalIgnoreCase) } catch { $false }
}
if ($running) {
    throw 'Quake Live is running. Close it completely before installing QL1K.'
}

$shortcutPath = Join-Path ([Environment]::GetFolderPath('Desktop')) 'QL1K.lnk'
if (-not $NoShortcut) {
    Remove-Item -LiteralPath $shortcutPath -Force -ErrorAction SilentlyContinue
}
$stagingPath = Join-Path $GamePath ('.ql1k-install-' + [Guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $stagingPath -ErrorAction Stop | Out-Null
    foreach ($sourceName in $payloads.Keys) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $sourceName) `
            -Destination (Join-Path $stagingPath $payloads[$sourceName]) -Force
        Assert-Checksum (Join-Path $stagingPath $payloads[$sourceName]) $checksums[$sourceName] `
            "Staged file '$($payloads[$sourceName])'"
    }
    Copy-Item -LiteralPath $manifestPath `
        -Destination (Join-Path $stagingPath 'SHA256SUMS.txt') -Force

    foreach ($installedName in $payloads.Values) {
        Remove-Item -LiteralPath (Join-Path $GamePath $installedName) `
            -Force -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath (Join-Path $GamePath 'SHA256SUMS.txt') `
        -Force -ErrorAction SilentlyContinue

    foreach ($sourceName in $payloads.Keys) {
        Copy-Item -LiteralPath (Join-Path $stagingPath $payloads[$sourceName]) `
            -Destination (Join-Path $GamePath $payloads[$sourceName]) -Force
    }
    Copy-Item -LiteralPath (Join-Path $stagingPath 'SHA256SUMS.txt') `
        -Destination (Join-Path $GamePath 'SHA256SUMS.txt') -Force

    foreach ($sourceName in $payloads.Keys) {
        Assert-Checksum (Join-Path $GamePath $payloads[$sourceName]) $checksums[$sourceName] `
            "Installed file '$($payloads[$sourceName])'"
    }

    if (-not $NoShortcut) {
        $shell = New-Object -ComObject WScript.Shell
        $shortcut = $shell.CreateShortcut($shortcutPath)
        $shortcut.TargetPath = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
        $shortcut.Arguments = "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$GamePath\ql_fps_launch.ps1`""
        $shortcut.WorkingDirectory = $GamePath
        $shortcut.IconLocation = "$GamePath\quakelive_steam.exe,0"
        $shortcut.Save()
    }
} finally {
    Remove-Item -LiteralPath $stagingPath -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Installed QL1K to: $GamePath"
Write-Host "Verified ql_fps_patch.dll SHA-256: $($checksums['bin/ql_fps_patch.dll'])"
if (-not $NoShortcut) {
    Write-Host "Launch with: $shortcutPath"
}
