$ErrorActionPreference = 'Stop'

$installerPath = Join-Path (Split-Path -Parent $PSScriptRoot) 'install.ps1'
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    $installerPath,
    [ref]$tokens,
    [ref]$parseErrors)
if ($parseErrors.Count -ne 0) {
    throw "install.ps1 has PowerShell parse errors: $($parseErrors -join '; ')"
}

foreach ($functionName in @('Assert-Checksum', 'Find-QuakeLiveInstall')) {
    $functionAst = $ast.Find({
        param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
            $node.Name -eq $functionName
    }, $true)
    if (-not $functionAst) {
        throw "Required installer function was not found: $functionName"
    }
    . ([scriptblock]::Create($functionAst.Extent.Text))
}

$mountedDriveRoots = @([IO.DriveInfo]::GetDrives() | ForEach-Object {
    $_.Name.ToUpperInvariant()
})
$missingDriveRoot = @(67..90 | ForEach-Object {
    ([string][char]$_ + ':\')
} | Where-Object {
    $mountedDriveRoots -notcontains $_
} | Select-Object -First 1)
if ($missingDriveRoot.Count -ne 1) {
    throw 'Installer test requires one unmounted drive letter from C: through Z:.'
}
$missingDriveCandidate = [IO.Path]::Combine(
    $missingDriveRoot[0],
    'ql1k-test-missing-drive\Quake Live')
$missingResult = @(Find-QuakeLiveInstall @($missingDriveCandidate))
if ($missingResult.Count -ne 0) {
    throw 'A candidate on a missing drive must not be reported as a Quake Live installation.'
}

$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('ql1k-install-test-' + [Guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $testRoot | Out-Null
    New-Item -ItemType File -Path (Join-Path $testRoot 'quakelive_steam.exe') | Out-Null
    $found = @(Find-QuakeLiveInstall @($missingDriveCandidate, $testRoot, $testRoot))
    if ($found.Count -ne 1 -or $found[0] -ne $testRoot) {
        throw 'Installer discovery must skip missing drives and return each valid installation once.'
    }

    $payloadPath = Join-Path $testRoot 'ql_fps_injector.exe'
    New-Item -ItemType File -Path $payloadPath | Out-Null
    function Get-FileHash {
        throw [IO.IOException] 'Operation did not complete successfully because the file contains a virus or potentially unwanted software.'
    }

    $blockedMessage = $null
    try {
        Assert-Checksum $payloadPath ('0' * 64) "Package file 'bin/ql_fps_injector.exe'"
    } catch {
        $blockedMessage = $_.Exception.Message
    }
    if (-not $blockedMessage -or
        $blockedMessage -notmatch 'Security software blocked access' -or
        $blockedMessage -notmatch 'checksum verification will not be bypassed' -or
        $blockedMessage -notmatch 'Protection history' -or
        $blockedMessage -notmatch [regex]::Escape($payloadPath)) {
        throw "Security-provider read failure was not translated into the required recovery message: $blockedMessage"
    }
} finally {
    Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host 'Installer regression tests passed.'
