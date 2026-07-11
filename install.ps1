param([string]$GamePath)
$ErrorActionPreference = 'Stop'

if (-not $GamePath) {
    $candidates = @(
        "${env:ProgramFiles(x86)}\Steam\steamapps\common\Quake Live",
        'C:\SteamLibrary\steamapps\common\Quake Live',
        'D:\SteamLibrary\steamapps\common\Quake Live',
        'E:\SteamLibrary\steamapps\common\Quake Live'
    )
    $GamePath = $candidates | Where-Object { Test-Path (Join-Path $_ 'quakelive_steam.exe') } |
        Select-Object -First 1
}
if (-not $GamePath -or -not (Test-Path (Join-Path $GamePath 'quakelive_steam.exe'))) {
    throw 'Quake Live was not found. Run: .\install.ps1 -GamePath "X:\...\Quake Live"'
}

Copy-Item "$PSScriptRoot\bin\ql_fps_patch.dll" $GamePath -Force
Copy-Item "$PSScriptRoot\bin\ql_fps_injector.exe" $GamePath -Force
Copy-Item "$PSScriptRoot\ql_fps.cfg" $GamePath -Force
Copy-Item "$PSScriptRoot\ql_fps_launch.ps1" $GamePath -Force

$shortcutPath = Join-Path ([Environment]::GetFolderPath('Desktop')) 'QL1K.lnk'
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
$shortcut.Arguments = "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$GamePath\ql_fps_launch.ps1`""
$shortcut.WorkingDirectory = $GamePath
$shortcut.IconLocation = "$GamePath\quakelive_steam.exe,0"
$shortcut.Save()

Write-Host "Installed QL1K to: $GamePath"
Write-Host "Launch with: $shortcutPath"
