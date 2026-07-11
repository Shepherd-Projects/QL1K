$ErrorActionPreference = 'Stop'
$game = Join-Path $PSScriptRoot 'quakelive_steam.exe'
$injector = Join-Path $PSScriptRoot 'ql_fps_injector.exe'
$patch = Join-Path $PSScriptRoot 'ql_fps_patch.dll'
$log = Join-Path $PSScriptRoot 'ql_fps_launch.log'

Start-Process -FilePath $game -WorkingDirectory $PSScriptRoot
$process = $null
for ($attempt = 0; $attempt -lt 60; $attempt++) {
    $process = Get-Process -Name quakelive_steam -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -eq $game } | Select-Object -First 1
    if ($process) { break }
    Start-Sleep -Milliseconds 500
}
if (-not $process) { throw 'Quake Live process was not found.' }

$result = & $injector $process.Id $patch 2>&1
$line = "PID=$($process.Id) $result"
"$(Get-Date -Format o) $line" | Set-Content -LiteralPath $log

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
