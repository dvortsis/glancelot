param([string]$BuildDir = 'build', [string]$QtRoot = '', [switch]$Test, [switch]$CoreOnly, [switch]$Package)
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

$env:PATH="$(Join-Path $QtRoot 'bin');$env:PATH"
$proofRoot=Join-Path $repoRoot '.cache/monitor-integrated-proof'
New-Item -ItemType Directory -Path $proofRoot -Force | Out-Null
$gateStatus=Get-Content -Raw .cache/monitor-full-gates/status.json | ConvertFrom-Json
if($gateStatus.exit -ne 0 -or $gateStatus.phase -ne 'complete'){throw 'Both full gates must pass before integrated proof'}
$qualified=(Get-Content -Raw .cache/monitor-full-gates/source-pin.txt).Trim()
if($qualified -ne $gateStatus.source){throw 'Qualified source pin disagrees with gate result'}
$source=git rev-parse HEAD
$dirty=git status --porcelain -- app include src ui tests CMakeLists.txt cmake
if($dirty){throw 'Integrated product sources must be clean'}
git diff --exit-code $qualified HEAD -- app include src ui tests CMakeLists.txt cmake *> (Join-Path $proofRoot 'qualified-product-diff.txt')
if($LASTEXITCODE -ne 0){throw 'Integrated product differs from full-gated source'}
@{source=$source;qualified=$qualified;productDiffExit=0;started=(Get-Date -Format o)} | ConvertTo-Json | Set-Content (Join-Path $proofRoot 'source-equality.json')
$global:GlancelotMonitorIntegratedPhase='waiting for machine gate lock'
try {
 & C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch codex/monitor-custom-01 -Run {
    $global:GlancelotMonitorIntegratedPhase='integrated focused regressions'
    Write-Host "COMMAND: ctest --test-dir build-codex-monitor-custom-01 -R '^(input_ui|monitor_output|monitor_customization|monitor_customization_ui|recovery_controller)$' --output-on-failure"
    ctest --test-dir $BuildDir -R '^(input_ui|monitor_output|monitor_customization|monitor_customization_ui|recovery_controller)$' --output-on-failure *> (Join-Path $proofRoot 'focused-ctest.txt')
    if($LASTEXITCODE -ne 0){throw 'Integrated focused regressions failed'}
    Copy-Item (Join-Path $BuildDir 'Testing/Temporary/LastTest.log') (Join-Path $proofRoot 'focused-LastTest.log')
    foreach($name in @('monitor_customization-results.txt','monitor_customization_ui-results.txt')){Copy-Item (Join-Path $BuildDir $name) (Join-Path $proofRoot $name)}
    $global:GlancelotMonitorIntegratedPhase='complete integrated native UI'
    Write-Host 'COMMAND: & ./.cache/run-monitor-native-proof.ps1 -Attempt 13'
    & ./.cache/run-monitor-native-proof.ps1 -Attempt 13
    if($LASTEXITCODE -ne 0){throw 'Integrated native UI failed'}
    $global:GlancelotMonitorIntegratedPhase='native two-display frame and move proof'
    Write-Host 'COMMAND: & ./.cache/run-native-monitor-probe-integrated.ps1'
    & ./.cache/run-native-monitor-probe-integrated.ps1 *> (Join-Path $proofRoot 'native-probe-build-run.txt')
    if($LASTEXITCODE -ne 0){throw 'Integrated native geometry probe failed'}
    $native=Get-Content -Raw .cache/native-monitor-probe-integrated.json | ConvertFrom-Json
    if(-not $native.passed){throw 'Integrated native geometry predicate failed'}
    Copy-Item .cache/native-monitor-probe-integrated.json (Join-Path $proofRoot 'native-geometry.json')
    @{source=$source;binarySha256=(Get-FileHash .cache/native-monitor-probe.exe).Hash} | ConvertTo-Json | Set-Content (Join-Path $proofRoot 'native-probe-provenance.json')
    $global:GlancelotMonitorIntegratedPhase='isolated production rendering'
    $runtime=Join-Path $proofRoot 'production-runtime'
    New-Item -ItemType Directory -Path "$runtime/media","$runtime/config" -Force | Out-Null
    Copy-Item .cache/monitor-production-runtime/media/generated.png "$runtime/media/generated.png"
    $before=(Get-FileHash "$runtime/media/generated.png").Hash
    $binaryBefore=(Get-FileHash (Join-Path $BuildDir 'glancelot.exe')).Hash
    $env:QT_QPA_PLATFORM='windows';$env:QSG_RHI_BACKEND='d3d11'
    Remove-Item Env:QT_QUICK_BACKEND -ErrorAction SilentlyContinue
    $env:GLANCELOT_CONFIG_DIR="$runtime/config"
    Write-Host 'COMMAND: glancelot.exe --no-resume --quit-after 2500 --report <isolated report> --screenshot <isolated PNG> <generated image>'
    & (Join-Path $BuildDir 'glancelot.exe') --no-resume --quit-after 2500 --report "$runtime/report.json" --screenshot "$runtime/capture.png" "$runtime/media/generated.png" *> "$runtime/command-output.txt" | Out-Null
    $runtimeExit=$LASTEXITCODE
    $after=(Get-FileHash "$runtime/media/generated.png").Hash
    $binaryAfter=(Get-FileHash (Join-Path $BuildDir 'glancelot.exe')).Hash
    @{source=$source;qualifiedBinarySource=$qualified;exit=$runtimeExit;inputBefore=$before;inputAfter=$after;inputUnchanged=($before -eq $after);binaryBefore=$binaryBefore;binaryAfter=$binaryAfter;binaryUnchanged=($binaryBefore -eq $binaryAfter)} | ConvertTo-Json | Set-Content "$runtime/status.json"
    if($runtimeExit -ne 0 -or $before -ne $after -or $binaryBefore -ne $binaryAfter){throw 'Integrated production smoke failed'}
    $report=Get-Content -Raw "$runtime/report.json" | ConvertFrom-Json
    if(-not $report.pre_grab.window.exposed -or $report.pre_grab.frames -lt 1 -or $report.canvas.renderer_error){throw 'Production exposure/render predicate failed'}
    $global:GlancelotMonitorIntegratedPhase='complete'
 }
 @{source=$source;exit=0;phase=$global:GlancelotMonitorIntegratedPhase;finished=(Get-Date -Format o)} | ConvertTo-Json | Set-Content (Join-Path $proofRoot 'status.json')
} catch {
 @{source=$source;exit=1;phase=$global:GlancelotMonitorIntegratedPhase;error="$_";finished=(Get-Date -Format o)} | ConvertTo-Json | Set-Content (Join-Path $proofRoot 'status.json')
 throw
}
