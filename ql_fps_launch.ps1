$ErrorActionPreference = 'Stop'
$log = Join-Path $PSScriptRoot 'ql_fps_launch.log'

function Write-LaunchLog([string]$Message) {
    "$(Get-Date -Format o) $Message" | Add-Content -LiteralPath $log
}

try {
"$(Get-Date -Format o) launch_start script_path=$PSCommandPath" | Set-Content -LiteralPath $log
$game = Join-Path $PSScriptRoot 'quakelive_steam.exe'
$injector = Join-Path $PSScriptRoot 'ql_fps_injector.exe'
$patch = Join-Path $PSScriptRoot 'ql_fps_patch.dll'
$manifest = Join-Path $PSScriptRoot 'SHA256SUMS.txt'
$payloads = [ordered]@{
    'bin/ql_fps_patch.dll'    = 'ql_fps_patch.dll'
    'bin/ql_fps_injector.exe' = 'ql_fps_injector.exe'
    'ql_fps.cfg'              = 'ql_fps.cfg'
    'ql_fps_launch.ps1'       = 'ql_fps_launch.ps1'
}

if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
    throw "QL1K checksum manifest was not found: $manifest"
}
$checksums = @{}
Get-Content -LiteralPath $manifest | ForEach-Object {
    if ($_ -match '^([0-9A-Fa-f]{64})\s+(.+)$') {
        $checksums[$Matches[2].Trim().Replace('\', '/')] = $Matches[1].ToUpperInvariant()
    }
}
foreach ($manifestName in $payloads.Keys) {
    if (-not $checksums.ContainsKey($manifestName)) {
        throw "SHA256SUMS.txt does not contain $manifestName."
    }
    $installedPath = Join-Path $PSScriptRoot $payloads[$manifestName]
    if (-not (Test-Path -LiteralPath $installedPath -PathType Leaf)) {
        throw "QL1K payload was not found: $installedPath"
    }
    $actualHash = (Get-FileHash -LiteralPath $installedPath -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($actualHash -ne $checksums[$manifestName]) {
        throw "QL1K payload checksum mismatch for $($payloads[$manifestName]). Expected $($checksums[$manifestName]) but found $actualHash. Reinstall from GitHub main."
    }
}
$patchHash = $checksums['bin/ql_fps_patch.dll']
Write-LaunchLog "patch_path=$patch patch_sha256=$patchHash expected_patch_sha256=$patchHash payloads_verified=4"

$savedErrorPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    $result = @(& $injector --launch $game $patch 2>&1)
    $injectorExitCode = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $savedErrorPreference
}
if ($injectorExitCode -ne 0) {
    throw "QL1K suspended launch failed: $($result -join ' | ')"
}
$launchedPid = $null
foreach ($entry in $result) {
    if ([string]$entry -match '^launched_pid=(\d+)$') {
        $launchedPid = [int]$Matches[1]
    }
}
if (-not $launchedPid) {
    throw "QL1K injector did not report the launched process ID: $($result -join ' | ')"
}
$line = "PID=$launchedPid $($result -join ' | ')"
Write-LaunchLog $line
$process = Get-Process -Id $launchedPid -ErrorAction Stop

# Keep watching because cgame loads only after joining a game. Record state
# changes so status=4 or a fail-closed reason remains available for diagnosis.
while (-not $process.HasExited) {
    Start-Sleep -Seconds 1
    if ($process.HasExited) { break }
    $result = & $injector $process.Id $patch 2>&1
    $next = "PID=$($process.Id) $result"
    if ($next -ne $line) {
        "$(Get-Date -Format o) $next" | Add-Content -LiteralPath $log
        $line = $next
    }
    $process.Refresh()
}
} catch {
    try {
        Write-LaunchLog ("error=" + ($_.Exception.Message -replace '\s+', ' '))
    } catch {}
    throw
}
