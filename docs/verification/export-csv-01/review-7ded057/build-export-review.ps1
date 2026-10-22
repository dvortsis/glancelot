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
$configureArgs = @('-S', '.', '-B', $BuildDir, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=ON')
if ($CoreOnly) { $configureArgs += '-DGLANCELOT_BUILD_VIEWER=OFF' }
else { $configureArgs += @('-DGLANCELOT_BUILD_VIEWER=ON', "-DCMAKE_PREFIX_PATH=$QtRoot"); $env:PATH = "$(Join-Path $QtRoot 'bin');$env:PATH" }
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& cmake --build $BuildDir --target selection_export_tests --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
if ($Test) { & ctest --test-dir $BuildDir --output-on-failure; if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' } }
if ($Package) {
    & cmake --install $BuildDir --prefix (Join-Path $repoRoot 'dist\Glancelot')
    if ($LASTEXITCODE -ne 0) { throw 'Deploy failed.' }
    Copy-Item -LiteralPath 'README.md','LICENSE','THIRD_PARTY_NOTICES.md' -Destination 'dist\Glancelot'
    Copy-Item -LiteralPath 'assets\fonts\geist\OFL.txt' -Destination 'dist\Glancelot\Geist-OFL.txt'
    Copy-Item -LiteralPath (Join-Path $QtRoot 'sbom') -Destination 'dist\Glancelot\qt-sbom' -Recurse -Force
    if (Test-Path 'dist\Glancelot.zip') { Remove-Item -LiteralPath (Join-Path $repoRoot 'dist\Glancelot.zip') }
    Compress-Archive -Path 'dist\Glancelot\*' -DestinationPath 'dist\Glancelot.zip'
}
