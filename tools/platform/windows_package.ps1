param(
    [Parameter(Mandatory=$true)][string]$Output,
    [Parameter(Mandatory=$true)][long]$Epoch,
    [string]$BuildDir = 'build-codex-platform-01',
    [string]$QtRoot = '',
    [switch]$UseExistingDeployment
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Set-Location -LiteralPath $repoRoot
if (Test-Path -LiteralPath $Output) { throw 'Package output must be new.' }
$commonDir = git rev-parse --path-format=absolute --git-common-dir
if ($LASTEXITCODE -ne 0) { throw 'Cannot locate project tools.' }
$mainRoot = Split-Path $commonDir -Parent
if (-not $QtRoot) { $QtRoot = Join-Path $mainRoot '.tools/Qt/6.8.3/msvc2022_64' }
$python = Join-Path $mainRoot '.tools/python/Scripts/python.exe'
if (-not $UseExistingDeployment) {
    & (Join-Path $repoRoot 'scripts/build.ps1') -BuildDir $BuildDir -QtRoot $QtRoot -Test -Package
    if ($LASTEXITCODE -ne 0) { throw 'Required build/test/deployment failed.' }
}
& $python (Join-Path $PSScriptRoot 'stage_windows.py') --repo $repoRoot --build $BuildDir --qt $QtRoot --deployed 'dist/Glancelot' --output $Output --epoch $Epoch
if ($LASTEXITCODE -ne 0) { throw 'Package integrity or metadata collection failed.' }
