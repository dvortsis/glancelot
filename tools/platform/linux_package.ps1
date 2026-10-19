param(
    [Parameter(Mandatory=$true)][string]$Output,
    [Parameter(Mandatory=$true)][long]$Epoch,
    [string]$Distribution = 'Ubuntu',
    [string]$Attempt = 'default'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Set-Location -LiteralPath $repoRoot
if (Test-Path -LiteralPath $Output) { throw 'Package output must be new.' }
$outputRoot = if ([IO.Path]::IsPathRooted($Output)) { [IO.Path]::GetFullPath($Output) } else { [IO.Path]::GetFullPath((Join-Path $repoRoot $Output)) }
$sourceCommit = git rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect source commit.' }
$sourceStatus = git status --porcelain --untracked-files=normal
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect source dirty state.' }
$sourceDirty = if ($sourceStatus) { 'true' } else { 'false' }
$unixRepo = & wsl -d $Distribution --exec wslpath -a $repoRoot.Replace('\','/')
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve source mount.' }
$unixOutput = & wsl -d $Distribution --exec wslpath -a $outputRoot.Replace('\','/')
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve new output mount.' }
& wsl -d $Distribution --exec sh "$unixRepo/tools/platform/linux_package.sh" --output $unixOutput --epoch $Epoch --source-commit $sourceCommit --source-dirty $sourceDirty --attempt $Attempt
if ($LASTEXITCODE -ne 0) { throw 'Linux package build, tests, dependency audit or clean-runtime smoke failed.' }
