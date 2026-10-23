# Machine-wide build slots: a counting semaphore that caps concurrent heavy builds (configure, compile, targeted
# ctest) on this machine at N (default 3). Each slot is one file in the shared git common dir, so every worktree
# sees the same slots; creating a slot file is atomic (CreateNew) and the holder keeps it open until it releases,
# so on Windows nobody can delete a live holder's slot. The full gate is exclusive: while scripts/gate-lock.ps1 is
# held by another process, -Run waits. Usage (all in one PowerShell process):
#   & C:\src\Github\Glancelot\scripts\build-slot.ps1 -Branch claude/x -Run { cmake --build build-claude-x }
#   & C:\src\Github\Glancelot\scripts\build-slot.ps1 -Status
# Use the same -Slots value (or $env:GLANCELOT_BUILD_SLOTS) for every caller on the machine. Never kills processes.
param([scriptblock]$Run, [switch]$Status, [string]$Branch = '', [string]$Holder = $env:USERNAME,
      [int]$Slots = $(if ($env:GLANCELOT_BUILD_SLOTS) { [int]$env:GLANCELOT_BUILD_SLOTS } else { 3 }),
      [int]$TimeoutMinutes = 120, [int]$StaleHours = 6, [int]$PollSeconds = 2)
$ErrorActionPreference = 'Stop'
if ($Slots -lt 1) { throw '-Slots must be at least 1.' }
$commonDir = git rev-parse --path-format=absolute --git-common-dir 2>$null
if ($LASTEXITCODE -ne 0 -or -not $commonDir) { $commonDir = Join-Path (Split-Path $PSScriptRoot -Parent) '.git' }
$slotPrefix = 'glancelot-build-slot-'
$gateLock = Join-Path $commonDir 'glancelot-gate-lock'
$gateOwnerFile = Join-Path $gateLock 'owner.json'
$machine = [Environment]::MachineName

function Read-Json([string]$path) {
    # Readers share Write/Delete so they never block the holder, which keeps its slot file open for writing.
    try {
        $fs = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]'ReadWrite, Delete')
        try { $text = ([IO.StreamReader]::new($fs)).ReadToEnd() } finally { $fs.Dispose() }
        if ($text.Trim()) { $text | ConvertFrom-Json } else { $null }
    } catch { $null }
}
function Test-OwnerStale($owner, [string]$path) {
    # Same rules as gate-lock.ps1, plus PID reuse: a live process that started after the claim is not the holder.
    if (-not $owner) {
        $item = Get-Item -LiteralPath $path -ErrorAction SilentlyContinue
        return (-not $item) -or ((Get-Date) - $item.CreationTime).TotalMinutes -gt 2
    }
    if (((Get-Date) - [datetime]$owner.started).TotalHours -gt $StaleHours) { return $true }
    if ($owner.host -ne $machine) { return $false }
    $proc = Get-Process -Id $owner.pid -ErrorAction SilentlyContinue
    if (-not $proc) { return $true }
    try { return $proc.StartTime -gt ([datetime]$owner.started).AddSeconds(2) } catch { return $false }
}
function Get-GateHolder {
    # Returns the gate owner (or a placeholder) when another process holds the gate lock; $null when free or stale.
    # Never removes the gate lock: gate-lock.ps1 owns its own stale cleanup.
    if (-not (Test-Path -LiteralPath $gateLock)) { return $null }
    $owner = Read-Json $gateOwnerFile
    if (Test-OwnerStale $owner $gateLock) { return $null }
    if (-not $owner) { return [pscustomobject]@{ holder = '?'; branch = '?'; started = '?'; pid = 0 } }
    # Our own process, or a child launched from inside the gate holder's -Run block, is part of that gate.
    if ($env:GLANCELOT_GATE_LOCK_HELD -and $env:GLANCELOT_GATE_LOCK_HELD -eq "$($owner.pid)") { return $null }
    if ($owner.pid -eq $PID -and $owner.host -eq $machine) { return $null }
    $owner
}
function Get-SlotFiles { Get-ChildItem -LiteralPath $commonDir -Filter "$slotPrefix*.json" -File -ErrorAction SilentlyContinue }
function Remove-StaleSlot([string]$path, $judged) {
    # Re-read just before deleting and delete only the record we judged stale, so a racing waiter's fresh claim
    # survives. On Windows a live holder's open handle also makes the delete fail.
    $now = Read-Json $path
    if ($judged -and (-not $now -or $now.pid -ne $judged.pid -or $now.started -ne $judged.started)) { return $false }
    if (-not $judged -and $now) { return $false }
    try { [IO.File]::Delete($path) } catch { return $false }
    Write-Warning "Removed stale build slot $(Split-Path $path -Leaf): $($judged | ConvertTo-Json -Compress)"
    $true
}

if ($Status) {
    $gate = if (Test-Path -LiteralPath $gateLock) { $o = Read-Json $gateOwnerFile; if (Test-OwnerStale $o $gateLock) { 'stale' } else { "held by $($o.holder) on $($o.branch) (pid $($o.pid)) since $($o.started)" } } else { 'free' }
    "gate lock: $gate"
    $files = @(Get-SlotFiles | Sort-Object Name)
    "build slots: $($files.Count) held of $Slots (files in $commonDir)"
    foreach ($f in $files) {
        $o = Read-Json $f.FullName
        [pscustomobject]@{ slot = $f.BaseName.Substring($slotPrefix.Length); state = $(if (Test-OwnerStale $o $f.FullName) { 'stale' } else { 'live' })
            pid = $o.pid; host = $o.host; branch = $o.branch; started = $o.started; cwd = $o.cwd }
    }
    return
}
if (-not $Run) { throw 'Pass -Run { <build command> } or -Status.' }
# Re-entrant: a nested build-slot call in the same process runs without taking a second slot.
if ($env:GLANCELOT_BUILD_SLOT_HELD -eq "$PID") { & $Run; return }
if (-not $Branch) { $Branch = git branch --show-current 2>$null }

$deadline = (Get-Date).AddMinutes($TimeoutMinutes)
$stream = $null; $slotPath = $null; $lastMessage = ''
while (-not $stream) {
    if ((Get-Date) -gt $deadline) { throw "No build slot after $TimeoutMinutes minutes. $lastMessage" }
    $gate = Get-GateHolder
    if ($gate) {
        $msg = "Waiting: full gate held by $($gate.holder) on $($gate.branch) since $($gate.started)"
        if ($msg -ne $lastMessage) { Write-Host $msg; $lastMessage = $msg }
        Start-Sleep -Seconds $PollSeconds; continue
    }
    $reclaimed = $false
    foreach ($n in 1..$Slots) {
        $path = Join-Path $commonDir "$slotPrefix$n.json"
        try {
            # CreateNew is atomic; the handle stays open (no Delete share) until release.
            $stream = [IO.File]::Open($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
            $slotPath = $path; break
        } catch [IO.IOException] {
            $owner = Read-Json $path
            if (Test-OwnerStale $owner $path) { if (Remove-StaleSlot $path $owner) { $reclaimed = $true } }
        } catch [UnauthorizedAccessException] { }  # slot file is being deleted right now; retry next poll
    }
    if (-not $stream -and $reclaimed) { continue }  # a stale slot was just removed: retry at once
    if (-not $stream) {
        $msg = "Waiting: all $Slots build slots held"
        if ($msg -ne $lastMessage) { Write-Host $msg; $lastMessage = $msg }
        Start-Sleep -Seconds $PollSeconds; continue
    }
    $record = [ordered]@{ slot = [int]((Split-Path $slotPath -LeafBase).Substring($slotPrefix.Length)); holder = $Holder; branch = $Branch
        started = (Get-Date).ToString('o'); pid = $PID; host = $machine; cwd = (Get-Location).Path } | ConvertTo-Json
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($record)
    $stream.Write($bytes, 0, $bytes.Length); $stream.Flush()
    # A gate may have started between the gate check and the claim: give the slot back and wait for it.
    if (Get-GateHolder) { $stream.Dispose(); [IO.File]::Delete($slotPath); $stream = $null; $slotPath = $null }
}
$env:GLANCELOT_BUILD_SLOT_HELD = "$PID"
try { & $Run } finally {
    Remove-Item Env:GLANCELOT_BUILD_SLOT_HELD -ErrorAction SilentlyContinue
    $stream.Dispose()
    $owner = Read-Json $slotPath
    if ($owner -and $owner.pid -eq $PID -and $owner.host -eq $machine) { Remove-Item -LiteralPath $slotPath -Force }
}
