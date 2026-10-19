param(
    [Parameter(Mandatory=$true)][string]$Output,
    [Parameter(Mandatory=$true)][long]$Epoch,
    [string]$BuildDir = 'build-codex-platform-01',
    [string]$QtRoot = '',
    [Alias('UseExistingDeployment')][switch]$UseExistingBuild,
    [string]$SourceIdentity = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Set-Location -LiteralPath $repoRoot
if (Test-Path -LiteralPath $Output) { throw 'Package output must be new.' }
if ($Epoch -lt 315532800 -or $Epoch -gt 4354819198) { throw 'Source epoch outside supported range.' }
if ($UseExistingBuild -and -not $SourceIdentity) { throw 'Existing builds require an explicit declared SourceIdentity JSON from their recorded build evidence.' }
if (-not $UseExistingBuild -and $SourceIdentity) { throw 'SourceIdentity is only for explicit existing-build packaging.' }
$commonDir = git rev-parse --path-format=absolute --git-common-dir
if ($LASTEXITCODE -ne 0) { throw 'Cannot locate project tools.' }
$mainRoot = Split-Path $commonDir -Parent
if (-not $QtRoot) { $QtRoot = Join-Path $mainRoot '.tools/Qt/6.8.3/msvc2022_64' }
$python = Join-Path $mainRoot '.tools/python/Scripts/python.exe'
$oldSourceEpoch = $env:SOURCE_DATE_EPOCH
try {
    $env:SOURCE_DATE_EPOCH = "$Epoch"
    if (-not $UseExistingBuild) {
        $sourceCommit = git rev-parse HEAD
        if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect source revision.' }
        $sourceStatus = @(git status --porcelain --untracked-files=normal)
        & (Join-Path $repoRoot 'scripts/build.ps1') -BuildDir $BuildDir -QtRoot $QtRoot -Test -Package
        if ($LASTEXITCODE -ne 0) { throw 'Required build/test/deployment failed.' }
        if ((git rev-parse HEAD) -ne $sourceCommit -or (@(git status --porcelain --untracked-files=normal) -join "`n") -ne ($sourceStatus -join "`n")) {
            throw 'Source identity changed during build; refusing to label the package.'
        }
    } else {
        # Fresh Qt deployment still needs the installed compiler's runtime search
        # environment even when the recorded binary itself is reused explicitly.
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        $vsPath = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (-not $vsPath) { throw 'Visual Studio 2022 C++ Build Tools are required for runtime deployment.' }
        $devCmd = Join-Path $vsPath 'Common7/Tools/VsDevCmd.bat'
        $devEnvironment = & cmd.exe /d /c "call `"$devCmd`" -arch=x64 -host_arch=x64 >nul && set"
        if ($LASTEXITCODE -ne 0) { throw 'MSVC deployment environment initialization failed.' }
        foreach ($entry in $devEnvironment) {
            if ($entry -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
        }
    }
    $stageArgs = @('--repo', $repoRoot, '--build', $BuildDir, '--qt', $QtRoot, '--output', $Output, '--epoch', "$Epoch")
    if ($UseExistingBuild) { $stageArgs += @('--source-identity', $SourceIdentity) }
    else { $stageArgs += @('--deployed', 'dist/Glancelot') }
    & $python (Join-Path $PSScriptRoot 'stage_windows.py') @stageArgs
    if ($LASTEXITCODE -ne 0) { throw 'Package integrity or metadata collection failed.' }
} finally {
    if ($null -eq $oldSourceEpoch) { Remove-Item Env:SOURCE_DATE_EPOCH -ErrorAction SilentlyContinue }
    else { $env:SOURCE_DATE_EPOCH = $oldSourceEpoch }
}
