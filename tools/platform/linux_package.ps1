param(
    [Parameter(Mandatory=$true)][string]$Output,
    [Parameter(Mandatory=$true)][long]$Epoch,
    [string]$Distribution = 'Ubuntu'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Set-Location -LiteralPath $repoRoot
if (Test-Path -LiteralPath $Output) { throw 'Package output must be new.' }
$outputRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot $Output))
$sourceCommit = git rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect source commit.' }
$sourceStatus = git status --porcelain --untracked-files=normal
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect source dirty state.' }
$sourceDirty = if ($sourceStatus) { 'true' } else { 'false' }
$unixRepo = & wsl -d $Distribution -- wslpath -a $repoRoot
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve source mount.' }
$unixOutput = & wsl -d $Distribution -- wslpath -a $outputRoot
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve new output mount.' }
& wsl -d $Distribution -- sh "$unixRepo/tools/platform/linux_package.sh" --output $unixOutput --epoch $Epoch --source-commit $sourceCommit --source-dirty $sourceDirty
if ($LASTEXITCODE -ne 0) { throw 'Linux package build, tests, dependency audit or clean-runtime smoke failed.' }
