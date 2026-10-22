param(
    [Parameter(Mandatory=$true)][string]$Exe,
    [Parameter(Mandatory=$true)][string]$Fixtures,
    [Parameter(Mandatory=$true)][string]$Output,
    [string]$Python = 'python',
    [int]$Runs = 1, [int]$Steps = 5, [int]$Cycles = 2,
    [switch]$RequireRender
)
$ErrorActionPreference = 'Stop'
$replayArgs = @((Join-Path $PSScriptRoot 'replay.py'), '--exe', $Exe, '--fixtures', $Fixtures, '--output', $Output,
    '--runs', $Runs, '--steps', $Steps, '--cycles', $Cycles)
if ($RequireRender) { $replayArgs += '--require-render' }
& $Python @replayArgs
if ($LASTEXITCODE -ne 0) { throw 'Performance replay failed; inspect its local log/verdict' }
