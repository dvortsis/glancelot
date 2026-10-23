# Machine-wide build slots: a counting semaphore that caps concurrent heavy builds (configure, compile, targeted
# ctest) on this machine at N (default 3). Each slot is one file in the shared git common dir (see lock-common.ps1).
# The full gate is exclusive in both directions: -Run waits while another process tree holds scripts/gate-lock.ps1
# or has a gate request pending, and gate-lock.ps1 waits until no other process tree holds a slot.
# Usage (all in one PowerShell process):
#   & C:\src\Github\Glancelot\scripts\build-slot.ps1 -Branch claude/x -Run { cmake --build build-claude-x }
#   & C:\src\Github\Glancelot\scripts\build-slot.ps1 -Status
# Use the same -Slots value (or $env:GLANCELOT_BUILD_SLOTS) for every caller on the machine. Never kills processes.
param([scriptblock]$Run, [switch]$Status, [string]$Branch = '', [string]$Holder = $env:USERNAME,
      [int]$Slots = $(if ($env:GLANCELOT_BUILD_SLOTS) { [int]$env:GLANCELOT_BUILD_SLOTS } else { 3 }),
      [int]$TimeoutMinutes = 120, [int]$StaleHours = 6, [int]$PollSeconds = 2)
$ErrorActionPreference = 'Stop'
if ($Slots -lt 1) { throw '-Slots must be at least 1.' }
. (Join-Path $PSScriptRoot 'lock-common.ps1')

if ($Status) { Show-LockStatus $StaleHours $Slots; return }
if (-not $Run) { throw 'Pass -Run { <build command> } or -Status.' }
# Re-entrant: a nested call in this process, or in a child of the process holding the slot, takes no second slot.
$heldBy = $env:GLANCELOT_BUILD_SLOT_HELD
if ($heldBy -and ($heldBy -eq "$PID" -or (Get-AncestorIds).Contains([int]$heldBy))) { & $Run; return }
if (-not $Branch) { $Branch = git branch --show-current 2>$null }
$ancestors = Get-AncestorIds

function Get-ForeignGate {
    # A gate held or requested by another process tree. A build started inside the gate holder's own tree (same
    # process, an ancestor, or a child that inherited GLANCELOT_GATE_LOCK_HELD) is part of that gate and never waits,
    # not even for other pending gate requests, which are waiting on that gate.
    $gate = Get-GateOwner $StaleHours
    if ($gate) {
        $ours = ($gate.host -eq $machine -and $ancestors.Contains([int]$gate.pid)) -or
                ($env:GLANCELOT_GATE_LOCK_HELD -and $env:GLANCELOT_GATE_LOCK_HELD -eq "$($gate.pid)")
        if ($ours) { return $null }
        return "full gate held by $($gate.holder) on $($gate.branch) since $($gate.started)"
    }
    $pending = @(Get-LiveLocks $pendingPrefix $StaleHours $ancestors)
    if ($pending) { return "$($pending.Count) gate request(s) pending ($(($pending | ForEach-Object branch) -join ', '))" }
    $null
}

$deadline = (Get-Date).AddMinutes($TimeoutMinutes)
$stream = $null; $slotPath = $null; $lastMessage = ''
$priorHeld = $env:GLANCELOT_BUILD_SLOT_HELD
try {
    while (-not $stream) {
        if ((Get-Date) -gt $deadline) { throw "No build slot after $TimeoutMinutes minutes. $lastMessage" }
        $gate = Get-ForeignGate
        if ($gate) {
            if ("Waiting: $gate" -ne $lastMessage) { $lastMessage = "Waiting: $gate"; Write-Host $lastMessage }
            Start-Sleep -Seconds $PollSeconds; continue
        }
        $reclaimed = $false
        foreach ($n in 1..$Slots) {
            $path = Join-Path $commonDir "$slotPrefix$n.json"
            $stream = Open-LockFile $path (New-LockRecord @{ slot = $n; holder = $Holder; branch = $Branch })
            if ($stream) { $slotPath = $path; break }
            $owner = Read-LockJson $path
            if ((Test-LockStale $owner $path $StaleHours) -and (Remove-StaleLockFile $path $owner)) { $reclaimed = $true }
        }
        if (-not $stream -and $reclaimed) { continue }  # a stale slot was just removed: retry at once
        if (-not $stream) {
            if ("Waiting: all $Slots build slots held" -ne $lastMessage) { $lastMessage = "Waiting: all $Slots build slots held"; Write-Host $lastMessage }
            Start-Sleep -Seconds $PollSeconds; continue
        }
        # A gate may have been taken or requested between the check and the claim: give the slot back and wait.
        if (Get-ForeignGate) { Close-LockFile $stream $slotPath; $stream = $null; $slotPath = $null }
    }
    $env:GLANCELOT_BUILD_SLOT_HELD = "$PID"
    & $Run
} finally {
    if ($priorHeld) { $env:GLANCELOT_BUILD_SLOT_HELD = $priorHeld } else { Remove-Item Env:GLANCELOT_BUILD_SLOT_HELD -ErrorAction SilentlyContinue }
    Close-LockFile $stream $slotPath
}