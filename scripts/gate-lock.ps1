# Machine-wide gate lock: only one full gate (scripts/build.ps1 -Test or docker build) runs at a time, because
# overlapping runs break timing-budget tests. The lock is a directory in the shared git common dir, so every
# worktree on this machine sees the same lock; creating it is atomic. Usage (all in one PowerShell process):
#   & C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch codex/x -Run { docker build -t glancelot-browser:main . }
#   & C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Status
param([scriptblock]$Run, [switch]$Status, [string]$Holder = $env:USERNAME, [string]$Branch = '',
      [int]$WaitMinutes = 180, [int]$StaleHours = 6)
$ErrorActionPreference = 'Stop'
$commonDir = git rev-parse --path-format=absolute --git-common-dir 2>$null
if ($LASTEXITCODE -ne 0 -or -not $commonDir) { $commonDir = Join-Path (Split-Path $PSScriptRoot -Parent) '.git' }
$lock = Join-Path $commonDir 'glancelot-gate-lock'
$ownerFile = Join-Path $lock 'owner.json'
function Read-Owner { try { Get-Content -LiteralPath $ownerFile -Raw | ConvertFrom-Json } catch { $null } }
function Test-Stale($owner) {
    if (-not $owner) { return ((Get-Date) - (Get-Item -LiteralPath $lock).CreationTime).TotalMinutes -gt 2 }
    $alive = $owner.host -ne [Environment]::MachineName -or (Get-Process -Id $owner.pid -ErrorAction SilentlyContinue)
    -not $alive -or ((Get-Date) - [datetime]$owner.started).TotalHours -gt $StaleHours
}
if ($Status) { if (Test-Path -LiteralPath $lock) { Read-Owner | Format-List } else { 'gate lock free' }; return }
if (-not $Run) { throw 'Pass -Run { <full gate command> } or -Status.' }
# Re-entrant: build.ps1 -Test inside an integrator's locked block runs without taking the lock again.
if ($env:GLANCELOT_GATE_LOCK_HELD -eq "$PID") { & $Run; return }
if (-not $Branch) { $Branch = git branch --show-current 2>$null }
$deadline = (Get-Date).AddMinutes($WaitMinutes)
while ($true) {
    try { New-Item -ItemType Directory -Path $lock -ErrorAction Stop | Out-Null; break } catch {
        if (-not (Test-Path -LiteralPath $lock)) { continue }
        $owner = Read-Owner
        if (Test-Stale $owner) { Write-Warning "Removing stale gate lock: $($owner | ConvertTo-Json -Compress)"; Remove-Item -LiteralPath $lock -Recurse -Force; continue }
        if ((Get-Date) -gt $deadline) { throw "Gate lock still held after $WaitMinutes minutes: $($owner | ConvertTo-Json -Compress)" }
        Write-Host "Waiting for gate lock held by $($owner.holder) on $($owner.branch) since $($owner.started)"
        Start-Sleep -Seconds 30
    }
}
@{ holder = $Holder; branch = $Branch; started = (Get-Date).ToString('o'); pid = $PID; host = [Environment]::MachineName; cwd = (Get-Location).Path } |
    ConvertTo-Json | Set-Content -LiteralPath $ownerFile
$env:GLANCELOT_GATE_LOCK_HELD = "$PID"
try { & $Run } finally {
    Remove-Item Env:GLANCELOT_GATE_LOCK_HELD -ErrorAction SilentlyContinue
    $owner = Read-Owner
    if ($owner -and $owner.pid -eq $PID -and $owner.host -eq [Environment]::MachineName) { Remove-Item -LiteralPath $lock -Recurse -Force }
}
