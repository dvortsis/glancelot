# Machine-wide gate lock: only one full gate (scripts/build.ps1 -Test or docker build) runs at a time, because
# overlapping runs break timing-budget tests. The lock is a directory in the shared git common dir, so every
# worktree on this machine sees the same lock; creating it is atomic. A full gate also never overlaps a build:
# while it waits, a gate-pending marker stops new build slots (scripts/build-slot.ps1), and it starts only once no
# other process tree holds a build slot. Slots held by this process or its ancestors are ignored, so a build that
# calls gate-lock for its own tests does not wait on itself. Usage (all in one PowerShell process):
#   & C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch codex/x -Run { docker build -t glancelot-browser:main . }
#   & C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Status
param([scriptblock]$Run, [switch]$Status, [string]$Holder = $env:USERNAME, [string]$Branch = '',
      [int]$WaitMinutes = 180, [int]$StaleHours = 6, [int]$PollSeconds = 30)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lock-common.ps1')
$lock = $gateLock; $ownerFile = $gateOwnerFile
if ($Status) { Show-LockStatus $StaleHours $(if ($env:GLANCELOT_BUILD_SLOTS) { [int]$env:GLANCELOT_BUILD_SLOTS } else { 3 }); return }
if (-not $Run) { throw 'Pass -Run { <full gate command> } or -Status.' }
# Re-entrant: build.ps1 -Test inside an integrator's locked block runs without taking the lock again.
if ($env:GLANCELOT_GATE_LOCK_HELD -eq "$PID") { & $Run; return }
if (-not $Branch) { $Branch = git branch --show-current 2>$null }
$ancestors = Get-AncestorIds
$deadline = (Get-Date).AddMinutes($WaitMinutes)
$pendingPath = Join-Path $commonDir "$pendingPrefix$PID.json"
$pending = $null; $acquired = $false; $lastMessage = ''
function Write-Wait([string]$msg) { if ($msg -ne $script:lastMessage) { Write-Host $msg; $script:lastMessage = $msg } }
try {
    # Announce the request first, so no new build slot starts while this gate waits.
    $record = New-LockRecord @{ holder = $Holder; branch = $Branch }
    $pending = Open-LockFile $pendingPath $record
    if (-not $pending) {
        # A leftover marker from a dead process that had this PID.
        $old = Read-LockJson $pendingPath
        if (-not $old -or [datetime]$old.started -lt (Get-Process -Id $PID).StartTime) { [void](Remove-StaleLockFile $pendingPath $old) }
        $pending = Open-LockFile $pendingPath $record
        if (-not $pending) { throw "Cannot write gate-pending marker $pendingPath" }
    }
    while ($true) {
        if ((Get-Date) -gt $deadline) { throw "No gate after $WaitMinutes minutes. $lastMessage" }
        $slots = @(Get-LiveLocks $slotPrefix $StaleHours $ancestors)
        if ($slots) {
            Write-Wait "Waiting: $($slots.Count) build slots held ($(($slots | ForEach-Object { "$($_.branch) pid $($_.pid)" }) -join ', '))"
            Start-Sleep -Seconds $PollSeconds; continue
        }
        try { New-Item -ItemType Directory -Path $lock -ErrorAction Stop | Out-Null } catch {
            if (-not (Test-Path -LiteralPath $lock)) { continue }
            $owner = Read-LockJson $ownerFile
            if (Test-LockStale $owner $lock $StaleHours) { Write-Warning "Removing stale gate lock: $($owner | ConvertTo-Json -Compress)"; Remove-Item -LiteralPath $lock -Recurse -Force; continue }
            Write-Wait "Waiting for gate lock held by $($owner.holder) on $($owner.branch) since $($owner.started)"
            Start-Sleep -Seconds $PollSeconds; continue
        }
        New-LockRecord @{ holder = $Holder; branch = $Branch } | ConvertTo-Json | Set-Content -LiteralPath $ownerFile
        $acquired = $true
        # A slot claimed just before our marker appeared: give the lock back and wait for that build.
        if (@(Get-LiveLocks $slotPrefix $StaleHours $ancestors)) { Remove-Item -LiteralPath $lock -Recurse -Force; $acquired = $false; continue }
        break
    }
    Close-LockFile $pending $pendingPath; $pending = $null
    $env:GLANCELOT_GATE_LOCK_HELD = "$PID"
    & $Run
} finally {
    Remove-Item Env:GLANCELOT_GATE_LOCK_HELD -ErrorAction SilentlyContinue
    if ($pending) { Close-LockFile $pending $pendingPath }
    if ($acquired) {
        $owner = Read-LockJson $ownerFile
        if ($owner -and $owner.pid -eq $PID -and $owner.host -eq $machine) { Remove-Item -LiteralPath $lock -Recurse -Force }
    }
}
