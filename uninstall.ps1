param([Parameter(Mandatory)][string]$GamePath)
$ErrorActionPreference = 'Stop'
$GamePath = (Resolve-Path -LiteralPath $GamePath).Path
$game = Join-Path $GamePath 'quakelive_steam.exe'
if (-not (Test-Path -LiteralPath $game -PathType Leaf)) {
    throw 'Invalid Quake Live directory: quakelive_steam.exe was not found.'
}
Get-Process quakelive_steam -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -eq $game } | Stop-Process
@('ql_fps_patch.dll','ql_fps_injector.exe','ql_fps.cfg','ql_fps_launch.ps1',
  'SHA256SUMS.txt','ql_fps_launch.log','ql_fps_telemetry.log') | ForEach-Object {
    Remove-Item -LiteralPath (Join-Path $GamePath $_) -Force -ErrorAction SilentlyContinue
}
Remove-Item -LiteralPath (Join-Path ([Environment]::GetFolderPath('Desktop')) 'QL1K.lnk') `
    -Force -ErrorAction SilentlyContinue
Write-Host 'QL1K removed. Game configs were not changed.'
