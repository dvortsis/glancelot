[CmdletBinding()]
param(
    [string]$Path = '',
    [switch]$NoBuild,
    [switch]$NoLaunch
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$packageRoot = Join-Path $repoRoot 'dist\Glancelot'
$exe = Join-Path $packageRoot 'bin\glancelot.exe'
$decoder = Join-Path $packageRoot 'bin\glancelot-decoder.exe'
$manifestPath = Join-Path $packageRoot 'test-build.json'
$configDir = Join-Path $repoRoot '.cache\manual-testing\config'

# Resolve input before build.ps1 changes the current directory.
if ($Path) {
    $Path = (Resolve-Path -LiteralPath $Path).Path
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw 'Choose an image file, or launch without a path and use Open in the app.' }
}

if (-not $NoBuild) {
    foreach ($process in @(Get-Process -Name glancelot,glancelot-decoder -ErrorAction SilentlyContinue)) {
        if ($process.Path -in @($exe, $decoder)) { throw 'Close this checkout''s test app before rebuilding, then try again.' }
    }
    $revision = & git -C $repoRoot rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot identify the checkout revision.' }
    $branch = & git -C $repoRoot branch --show-current
    if ($LASTEXITCODE -ne 0) { throw 'Cannot identify the checkout branch.' }
    $changes = & git -C $repoRoot status --porcelain
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect checkout changes.' }
    $dirty = [bool]$changes
    Write-Host "Building $branch @ $($revision.Substring(0, 12)) (local changes: $dirty)"
    # A failed/partial install must never be advertised as the last successful test build.
    if (Test-Path -LiteralPath $manifestPath) { Remove-Item -LiteralPath $manifestPath }
    Push-Location -LiteralPath $repoRoot
    try { & (Join-Path $PSScriptRoot 'build.ps1') -BuildDir 'build-manual-testing' -Test -Package }
    finally { Pop-Location }
    $manifest = [ordered]@{
        branch = $branch
        revision = $revision
        localChanges = $dirty
        testedAtUtc = [DateTime]::UtcNow.ToString('o')
        viewerSha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
        decoderSha256 = (Get-FileHash -LiteralPath $decoder -Algorithm SHA256).Hash
    }
    $manifest | ConvertTo-Json | Set-Content -LiteralPath $manifestPath -Encoding UTF8
}

if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw 'No successful test build is available. Double-click Test Glancelot.cmd first.'
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne $manifest.viewerSha256 -or
    (Get-FileHash -LiteralPath $decoder -Algorithm SHA256).Hash -ne $manifest.decoderSha256) {
    throw 'The portable package changed since testing. Double-click Test Glancelot.cmd to rebuild and test it.'
}
Write-Host "Test build: $($manifest.branch) @ $($manifest.revision)"
Write-Host "Local changes: $($manifest.localChanges); tests passed: $($manifest.testedAtUtc)"
Write-Host "Test settings: $configDir"
Write-Host "Portable package: $packageRoot"
if ($NoLaunch) { return }

New-Item -ItemType Directory -Path $configDir -Force | Out-Null
$previousConfig = $env:GLANCELOT_CONFIG_DIR
try {
    $env:GLANCELOT_CONFIG_DIR = $configDir
    $start = @{ FilePath = $exe; WorkingDirectory = (Split-Path $exe -Parent); PassThru = $true }
    # Windows filenames cannot contain a quote; quoting preserves spaces and Unicode.
    if ($Path) { $start.ArgumentList = '"' + $Path + '"' }
    $viewer = Start-Process @start
    Write-Host "Opened Glancelot (PID $($viewer.Id)). Ctrl+O opens a photo; Ctrl+, opens Settings."
}
finally { $env:GLANCELOT_CONFIG_DIR = $previousConfig }
