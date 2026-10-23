# Machine-wide gate lock: only one full gate (scripts/build.ps1 -Test or docker build) runs at a time, because
# overlapping runs break timing-budget tests. The lock lives in the shared git common dir, so every worktree on this
# machine sees it. The claim is an exclusive create of glancelot-gate-lock\owner.json whose handle the holder keeps
# open (the directory itself is kept so older gate-lock.ps1 copies still see the lock). Requests are served first
# come, first served: each takes a ticket (a gate-pending file) and only the lowest live ticket may claim the gate.
# A full gate also never overlaps a build: a waiting ticket stops new build slots (scripts/build-slot.ps1), and the
# head of the queue starts only once no other process tree holds a build slot. Slots held by the requester's own
# tree, or by a tree that is itself waiting for or holding the gate, are not waited for. Usage (one PowerShell process):
#   & C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch codex/x -Run { docker build -t glancelot-browser:main . }
#   & C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Status
param([scriptblock]$Run, [switch]$Status, [string]$Holder = $env:USERNAME, [string]$Branch = '',
      [int]$WaitMinutes = 180, [int]$StaleHours = 6, [int]$PollSeconds = 5)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lock-common.ps1')
if ($Status) { Show-LockStatus $StaleHours $(if ($env:GLANCELOT_BUILD_SLOTS) { [int]$env:GLANCELOT_BUILD_SLOTS } else { 3 }); return }
if (-not $Run) { throw 'Pass -Run { <full gate command> } or -Status.' }
# Re-entrant: build.ps1 -Test inside an integrator's locked block runs without taking the lock again.
if ($env:GLANCELOT_GATE_LOCK_HELD -eq "$PID") { & $Run; return }
$priorHeld = $env:GLANCELOT_GATE_LOCK_HELD
function Restore-Held { if ($script:priorHeld) { $env:GLANCELOT_GATE_LOCK_HELD = $script:priorHeld } else { Remove-Item Env:GLANCELOT_GATE_LOCK_HELD -ErrorAction SilentlyContinue } }
# So does a child process started inside the gate holder's block (the owner is an ancestor, or set the inherited
# GLANCELOT_GATE_LOCK_HELD). It marks itself as inside the gate, so build.ps1 -Test in that child does not recurse.
$ancestors = Get-AncestorIds
$current = Get-GateOwner $StaleHours
if ($current -and $current.pid -and $current.host -eq $machine -and
    ($ancestors.Contains([int]$current.pid) -or $priorHeld -eq "$($current.pid)")) {
    $env:GLANCELOT_GATE_LOCK_HELD = "$PID"
    try { & $Run } finally { Restore-Held }
    return
}
if (-not $Branch) { $Branch = git branch --show-current 2>$null }

$deadline = (Get-Date).AddMinutes($WaitMinutes)
$record = New-LockRecord @{ holder = $Holder; branch = $Branch; ancestors = @($ancestors) }
$ticket = $null; $gate = $null; $lastMessage = ''
function Write-Wait([string]$msg) { if ($msg -ne $script:lastMessage) { Write-Host $msg; $script:lastMessage = $msg } }
function Remove-StaleGate {
    # Delete a dead or released holder's owner.json (re-read guarded; a live holder's open handle also blocks it),
    # then the directory only if it is empty.
    if (Test-Path -LiteralPath $gateOwnerFile) { [void](Remove-StaleLockFile $gateOwnerFile (Read-LockJson $gateOwnerFile)) }
    try { [IO.Directory]::Delete($gateLock, $false) } catch { }
}
function Close-Gate {
    Close-LockFile $script:gate $gateOwnerFile; $script:gate = $null
    try { [IO.Directory]::Delete($gateLock, $false) } catch { }
}
try {
    # Take a ticket first: from now on no new build slot and no later gate request can start ahead of this one.
    $ticket = New-GateTicket $record
    while ($true) {
        if ((Get-Date) -gt $deadline) { throw "No gate after $WaitMinutes minutes. $lastMessage" }
        $ahead = @(Get-LiveLocks $pendingPrefix $StaleHours $null | Where-Object { $n = Get-TicketNumber $_.file; $null -ne $n -and $n -lt $ticket.ticket })
        if ($ahead) {
            Write-Wait "Waiting: $($ahead.Count) earlier gate request(s) ahead of ticket $($ticket.ticket) ($(($ahead | ForEach-Object branch) -join ', '))"
            Start-Sleep -Seconds $PollSeconds; continue
        }
        $slots = @(Get-LiveLocks $slotPrefix $StaleHours (Get-GateRequesterIds $StaleHours))
        if ($slots) {
            Write-Wait "Waiting: $($slots.Count) build slots held ($(($slots | ForEach-Object { "$($_.branch) pid $($_.pid)" }) -join ', '))"
            Start-Sleep -Seconds $PollSeconds; continue
        }
        try { [void][IO.Directory]::CreateDirectory($gateLock) } catch { Start-Sleep -Milliseconds 200; continue }
        $gate = Open-LockFile $gateOwnerFile $record
        if (-not $gate) {
            $g = Get-GateState $StaleHours
            if ($g.state -eq 'free') { continue }  # directory removed by a releasing holder; retry
            if ($g.state -eq 'stale') { Remove-StaleGate; Start-Sleep -Milliseconds 200; continue }
            Write-Wait "Waiting for gate lock held by $($g.owner.holder) on $($g.owner.branch) since $($g.owner.started)"
            Start-Sleep -Seconds $PollSeconds; continue
        }
        # A slot claimed just before our ticket appeared: give the lock back and wait for that build.
        if (@(Get-LiveLocks $slotPrefix $StaleHours (Get-GateRequesterIds $StaleHours))) { Close-Gate; continue }
        break
    }
    Close-LockFile $ticket.stream $ticket.path; $ticket = $null
    $env:GLANCELOT_GATE_LOCK_HELD = "$PID"
    & $Run
} finally {
    Restore-Held
    if ($ticket) { Close-LockFile $ticket.stream $ticket.path }
    if ($gate) { Close-Gate }
}
