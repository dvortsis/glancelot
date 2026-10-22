param([string]$BuildDir = 'build', [string]$QtRoot = '', [switch]$Test, [switch]$CoreOnly, [switch]$Package)
$ErrorActionPreference = 'Stop'
# Full test gates are serialized machine-wide (AGENTS.md gate lock); scripts/gate-lock.ps1 is re-entrant.
if ($Test -and $env:GLANCELOT_GATE_LOCK_HELD -ne "$PID") {
    $gateSelf = $PSCommandPath; $gateArgs = $PSBoundParameters
    & (Join-Path $PSScriptRoot 'gate-lock.ps1') -Run ({ & $gateSelf @gateArgs }.GetNewClosure())
    return
}
$repoRoot = 'C:\src\Github\Glancelot\.worktrees\codex-integrate-export-csv-01'
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
$env:QT_QPA_PLATFORM='offscreen'; $env:QT_QUICK_BACKEND='software'
$proof=Join-Path $repoRoot '.cache/csv-integrated-proof'
New-Item -ItemType Directory -Path $proof -Force | Out-Null
git rev-parse HEAD | Set-Content "$proof/source-pin.txt"
& ctest --test-dir build-codex-integrate-export-csv-01 -R '^(selection_export|cache)$' --output-on-failure -V *> "$proof/selection-export.txt"
if($LASTEXITCODE -ne 0){throw 'Integrated CSV/cache failed'}
Copy-Item build-codex-integrate-export-csv-01/cache-results.txt "$proof/cache-results.txt"
& ./build-codex-integrate-export-csv-01/grid_ui_tests.exe reviewCompactCellsSnapToWholeRows -o "$proof/grid-resize.txt,txt"
if($LASTEXITCODE -ne 0){throw 'Integrated grid resize failed'}
& cmake -S . -B build-codex-integrate-export-csv-01-production -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DGLANCELOT_BUILD_VIEWER=OFF *> "$proof/production-configure.txt"
if($LASTEXITCODE -ne 0){throw 'Production core configure failed'}
& cmake --build build-codex-integrate-export-csv-01-production --target glancelot_core --parallel 4 *> "$proof/production-build.txt"
if($LASTEXITCODE -ne 0){throw 'Production core build failed'}
@{exit=0;source=(git rev-parse HEAD);finished=(Get-Date -Format o)} | ConvertTo-Json | Set-Content "$proof/status.json"