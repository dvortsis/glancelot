param([string]$BuildDir = 'build', [string]$QtRoot = '', [switch]$Test, [switch]$CoreOnly, [switch]$Package, [switch]$ProductionOnly)
$ErrorActionPreference = 'Stop'
# Full test gates are serialized machine-wide (AGENTS.md gate lock); scripts/gate-lock.ps1 is re-entrant.
if ($Test -and $env:GLANCELOT_GATE_LOCK_HELD -ne "$PID") {
    $gateSelf = $PSCommandPath; $gateArgs = $PSBoundParameters
    & (Join-Path $PSScriptRoot 'gate-lock.ps1') -Run ({ & $gateSelf @gateArgs }.GetNewClosure())
    return
}
$repoRoot = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $repoRoot
if ($IsWindows -or $env:OS -eq 'Windows_NT') {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $env:PATH = "$(Split-Path $vswhere -Parent);$env:PATH"
    $vsPath = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) { throw 'Visual Studio 2022 C++ Build Tools are required.' }
    $devCmd = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
    $devEnvironment = & cmd.exe /d /c "call `"$devCmd`" -arch=x64 -host_arch=x64 >nul && set"
    if ($LASTEXITCODE -ne 0) { throw 'MSVC environment initialization failed.' }
    foreach ($entry in $devEnvironment) { if ($entry -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') } }
}
# Project-local tools live in the main checkout; agent worktrees under .worktrees share them.
$toolsRoot = Join-Path $repoRoot '.tools'
if (-not (Test-Path -LiteralPath $toolsRoot)) {
    $commonDir = git -C $repoRoot rev-parse --path-format=absolute --git-common-dir 2>$null
    if ($LASTEXITCODE -eq 0 -and $commonDir) {
        $mainTools = Join-Path (Split-Path $commonDir -Parent) '.tools'
        if (Test-Path -LiteralPath $mainTools) { $toolsRoot = $mainTools }
    }
}
$localTools = Join-Path $toolsRoot 'python\Scripts'
if (Test-Path -LiteralPath $localTools) { $env:PATH = "$localTools;$env:PATH" }
if (-not $QtRoot) { $QtRoot = Join-Path $toolsRoot 'Qt\6.8.3\msvc2022_64' }

$proofRoot=Join-Path $repoRoot '.cache/monitor-full-gates'
New-Item -ItemType Directory -Path $proofRoot -Force | Out-Null
$global:GlancelotMonitorGatePhase='waiting for machine gate lock'
@{running=$true;exit=$null;source=(git rev-parse HEAD);phase=$global:GlancelotMonitorGatePhase;started=(Get-Date -Format o)} | ConvertTo-Json | Set-Content (Join-Path $proofRoot 'status.json')
try {
 & C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch codex/monitor-custom-01 -Run {
    git rev-parse HEAD | Set-Content (Join-Path $proofRoot 'source-pin.txt')
    $global:GlancelotMonitorGatePhase='production configure without tests'
    Write-Host 'COMMAND: cmake -S . -B build-codex-monitor-custom-01 -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DGLANCELOT_BUILD_VIEWER=ON -DCMAKE_PREFIX_PATH=<project-local Qt6.8.3>'
    & cmake -S . -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DGLANCELOT_BUILD_VIEWER=ON "-DCMAKE_PREFIX_PATH=$QtRoot" *> (Join-Path $proofRoot 'production-configure.txt')
    if($LASTEXITCODE -ne 0){throw 'Production configure failed'}
    $global:GlancelotMonitorGatePhase='production build without tests'
    Write-Host 'COMMAND: cmake --build build-codex-monitor-custom-01 --target glancelot --parallel 4'
    & cmake --build $BuildDir --target glancelot --parallel 4 *> (Join-Path $proofRoot 'production-build.txt')
    if($LASTEXITCODE -ne 0){throw 'Production build failed'}
    if($ProductionOnly){$global:GlancelotMonitorGatePhase='production-only complete';return}
    $global:GlancelotMonitorGatePhase='full Windows gate'
    Write-Host 'COMMAND: & ./scripts/build.ps1 -BuildDir build-codex-monitor-custom-01 -Test'
    & ./scripts/build.ps1 -BuildDir $BuildDir -Test *> (Join-Path $proofRoot 'windows-gate.txt')
    if($LASTEXITCODE -ne 0){throw 'Windows gate failed'}
    Copy-Item -LiteralPath (Join-Path $BuildDir 'Testing/Temporary/LastTest.log') -Destination (Join-Path $proofRoot 'windows-LastTest.log')
    foreach($name in @('monitor_customization-results.txt','monitor_customization_ui-results.txt')) { Copy-Item -LiteralPath (Join-Path $BuildDir $name) -Destination (Join-Path $proofRoot $name) }
    $global:GlancelotMonitorGatePhase='fresh source Docker gate'
    Write-Host 'COMMAND: docker build --progress=plain -t glancelot-browser:main .'
    docker build --progress=plain -t glancelot-browser:main . *> (Join-Path $proofRoot 'docker-gate.txt')
    if($LASTEXITCODE -ne 0){throw 'Docker gate failed'}
    $global:GlancelotMonitorGatePhase='complete'
 }
 @{exit=0;phase=$global:GlancelotMonitorGatePhase;source=(git rev-parse HEAD);finished=(Get-Date -Format o)} | ConvertTo-Json | Set-Content (Join-Path $proofRoot 'status.json')
} catch {
 @{exit=1;phase=$global:GlancelotMonitorGatePhase;source=(git rev-parse HEAD);error="$_";finished=(Get-Date -Format o)} | ConvertTo-Json | Set-Content (Join-Path $proofRoot 'status.json')
 throw
}
