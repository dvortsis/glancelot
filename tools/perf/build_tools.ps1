param([string]$BuildDir = 'build-codex-perf-measure', [string]$QtRoot = '')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Set-Location -LiteralPath $repoRoot
# Reuse the pinned MSVC/Qt environment import; this builds but does not run unrelated UI tests.
& (Join-Path $repoRoot 'scripts/build.ps1') -BuildDir $BuildDir -QtRoot $QtRoot
if ($LASTEXITCODE -ne 0) { throw 'Viewer build failed' }
if (-not $QtRoot) {
    $common = git rev-parse --path-format=absolute --git-common-dir
    if ($LASTEXITCODE -ne 0) { throw 'Cannot locate shared tools' }
    $QtRoot = Join-Path (Split-Path $common -Parent) '.tools/Qt/6.8.3/msvc2022_64'
}
$toolBuild = Join-Path $BuildDir 'perf-tools'
& cmake -S tools/perf -B $toolBuild -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$QtRoot"
if ($LASTEXITCODE -ne 0) { throw 'Performance tools configure failed' }
& cmake --build $toolBuild --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Performance tools build failed' }
