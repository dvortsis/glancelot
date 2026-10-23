# Shared helpers for gate-lock.ps1 and build-slot.ps1 (dot-sourced; not run directly). All lock files are
# machine-local, in the git common dir of the current directory's repository, so every worktree sees them:
#   glancelot-gate-lock\owner.json      the full-gate lock (the directory is kept for older gate-lock.ps1 copies)
#   glancelot-build-slot-<n>.json       one file per held build slot
#   glancelot-gate-pending-<ticket>.json  a gate request waiting to start (FIFO ticket); new build slots wait while one exists
# Every claim is an exclusive file create (FileMode.CreateNew) whose handle the holder keeps open until release, so
# exactly one process wins and, on Windows, a live holder's file cannot be deleted. A record on this host is stale
# only when its PID is dead or now belongs to a later process; -StaleHours applies only to records from another
# host. Stale files are removed only after a re-read shows the same record. Nothing here kills processes.
$commonDir = git rev-parse --path-format=absolute --git-common-dir 2>$null
if ($LASTEXITCODE -ne 0 -or -not $commonDir) { $commonDir = Join-Path (Split-Path $PSScriptRoot -Parent) '.git' }
$machine = [Environment]::MachineName
$gateLock = Join-Path $commonDir 'glancelot-gate-lock'
$gateOwnerFile = Join-Path $gateLock 'owner.json'
$slotPrefix = 'glancelot-build-slot-'
$pendingPrefix = 'glancelot-gate-pending-'

function Read-LockJson([string]$path) {
    # Readers share Write/Delete so they never block a holder that keeps its file open for writing.
    try {
        $fs = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]'ReadWrite, Delete')
        try { $text = ([IO.StreamReader]::new($fs)).ReadToEnd() } finally { $fs.Dispose() }
        if ($text.Trim()) { $text | ConvertFrom-Json } else { $null }
    } catch { $null }
}
function Test-Unheld([string]$path) {
    # True when no process has the file open: an exclusive open succeeds only without any other handle. A claim
    # holder keeps its handle for the whole claim, so an unheld claim file is a leftover. Fails safe (held) on error.
    try { $fs = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None); $fs.Dispose(); $true } catch { $false }
}
function Test-LockStale($owner, [string]$path, [double]$staleHours) {
    # A missing file is gone. A claim file without a record yet is stale only when nobody holds it open; an older
    # gate-lock.ps1 directory without owner.json is held for two minutes after its last write (LastWriteTime, not
    # CreationTime, because Windows tunnels the creation time of a re-created name). A record on this host is stale
    # when its PID is dead or now belongs to a later process, or, for handle claims (claim = 'handle'), when no
    # process holds the file open any more. Records from another host go stale after -StaleHours.
    $item = Get-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
    if (-not $item) { return $true }
    $isFile = -not $item.PSIsContainer
    # A malformed record (no pid, no parsable start time) is judged like a missing one.
    $started = [datetime]::MinValue
    if ($owner -and (-not $owner.pid -or -not [datetime]::TryParse("$($owner.started)", [ref]$started))) { $owner = $null }
    if (-not $owner) {
        if ($isFile) { return Test-Unheld $path }
        return ((Get-Date) - $item.LastWriteTime).TotalMinutes -gt 2
    }
    if ($owner.host -ne $machine) { return ((Get-Date) - [datetime]$owner.started).TotalHours -gt $staleHours }
    $proc = Get-Process -Id $owner.pid -ErrorAction SilentlyContinue
    if (-not $proc) { return $true }
    try { if ($proc.StartTime -gt ([datetime]$owner.started).AddSeconds(2)) { return $true } } catch { }
    if ($isFile -and $owner.claim -eq 'handle') { return Test-Unheld $path }
    $false
}
function Get-AncestorIds {
    # This process and its ancestors. Locks they hold belong to the caller's own process tree.
    $ids = [Collections.Generic.HashSet[int]]::new()
    $p = Get-Process -Id $PID
    for ($i = 0; $p -and $i -lt 64 -and $ids.Add($p.Id); $i++) { try { $p = $p.Parent } catch { $p = $null } }
    , $ids
}
function New-LockRecord([hashtable]$extra) {
    $r = [ordered]@{ holder = $env:USERNAME; started = (Get-Date).ToString('o'); pid = $PID; host = $machine; cwd = (Get-Location).Path; claim = 'handle' }
    foreach ($k in $extra.Keys) { $r[$k] = $extra[$k] }
    $r
}
function Open-LockFile([string]$path, $record) {
    # Exclusive claim. Returns the open stream (held until release, no Delete share) or $null when the file exists
    # (or its directory is missing). A failed write gives the claim back.
    try { $stream = [IO.File]::Open($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read) }
    catch [IO.IOException] { return $null } catch [UnauthorizedAccessException] { return $null }
    try {
        $bytes = [Text.UTF8Encoding]::new($false).GetBytes(($record | ConvertTo-Json -Compress))
        $stream.Write($bytes, 0, $bytes.Length); $stream.Flush()
    } catch { $stream.Dispose(); [IO.File]::Delete($path); throw }
    $stream
}
function Close-LockFile($stream, [string]$path) {
    # Release our own claim: empty the file through the held handle, close it, then delete it (retrying while a
    # reader that does not share Delete has it open). A leftover is empty and unheld, so it is stale at once.
    if ($stream) {
        try { $stream.SetLength(0); $stream.Flush() } catch { }
        $stream.Dispose()
    }
    if (-not $path) { return }
    for ($i = 0; $i -lt 20; $i++) {
        if (-not (Test-Path -LiteralPath $path)) { return }
        $owner = Read-LockJson $path
        if ($owner -and -not ($owner.pid -eq $PID -and $owner.host -eq $machine)) { return }  # someone else's claim now
        if (-not $owner -and -not $stream) { return }  # not ours to judge
        try { [IO.File]::Delete($path); return } catch { Start-Sleep -Milliseconds 50 }
    }
}
function Remove-StaleLockFile([string]$path, $judged) {
    # Re-read just before deleting and delete only the record judged stale, so a racing fresh claim survives.
    if (-not (Test-Path -LiteralPath $path)) { return $true }  # already released
    $now = Read-LockJson $path
    if ($judged -and (-not $now -or $now.pid -ne $judged.pid -or $now.started -ne $judged.started)) { return $false }
    if (-not $judged -and $now) { return $false }
    try { [IO.File]::Delete($path) } catch { return $false }
    Write-Warning "Removed stale $(Split-Path $path -Leaf): $($judged | ConvertTo-Json -Compress)"
    $true
}
function Get-LiveLocks([string]$prefix, [double]$staleHours, $ignoreIds) {
    # Live slot or pending files, minus those whose holder PID is in $ignoreIds on this host. A stale file is
    # skipped only once it is actually removed; one that cannot be removed (a live holder) still counts.
    foreach ($f in Get-ChildItem -LiteralPath $commonDir -Filter "$prefix*.json" -File -ErrorAction SilentlyContinue) {
        $o = Read-LockJson $f.FullName
        if ((Test-LockStale $o $f.FullName $staleHours) -and (Remove-StaleLockFile $f.FullName $o)) { continue }
        if ($o -and $null -ne $ignoreIds -and $o.host -eq $machine -and $ignoreIds.Contains([int]$o.pid)) { continue }
        if (-not $o) { $o = [pscustomobject]@{ pid = 0; branch = '?'; started = '?'; host = '?' } }
        $o | Add-Member -NotePropertyName file -NotePropertyValue $f.Name -PassThru
    }
}
function Get-GateState([double]$staleHours) {
    # free | held | stale, with the owner record. A directory without owner.json is an older gate-lock.ps1 copy
    # between creating the directory and writing its owner, or an empty leftover.
    if (-not (Test-Path -LiteralPath $gateLock)) { return [pscustomobject]@{ state = 'free'; owner = $null } }
    $hasFile = Test-Path -LiteralPath $gateOwnerFile
    $owner = if ($hasFile) { Read-LockJson $gateOwnerFile } else { $null }
    $stale = Test-LockStale $owner $(if ($hasFile) { $gateOwnerFile } else { $gateLock }) $staleHours
    if (-not $owner -and -not $stale) { $owner = [pscustomobject]@{ holder = '?'; branch = '?'; started = '?'; pid = 0; host = $machine } }
    [pscustomobject]@{ state = $(if ($stale) { 'stale' } else { 'held' }); owner = $owner; fromFile = $hasFile }
}
function Get-GateOwner([double]$staleHours) {
    # The live gate owner (or a placeholder while it is being written); $null when free or stale. Never removes it.
    $g = Get-GateState $staleHours
    if ($g.state -eq 'held') { $g.owner } else { $null }
}
function Get-GateRequesterIds([double]$staleHours) {
    # Process trees with a live gate request (pending markers and the gate owner record ancestors). Their slot
    # holders are blocked inside that request rather than building, so a gate does not wait for those slots.
    $ids = [Collections.Generic.HashSet[int]]::new()
    $records = @(Get-LiveLocks $pendingPrefix $staleHours $null) + @(Get-GateOwner $staleHours)
    foreach ($r in $records) { if ($r -and $r.host -eq $machine -and $r.ancestors) { foreach ($a in $r.ancestors) { [void]$ids.Add([int]$a) } } }
    , $ids
}
function Show-LockStatus([double]$staleHours, [int]$slots) {
    $g = Get-GateState $staleHours
    "gate lock: $(switch ($g.state) { 'free' { 'free' } 'stale' { 'stale (the next gate-lock.ps1 request removes it)' }
        default { "held by $($g.owner.holder) on $($g.owner.branch) (pid $($g.owner.pid)) since $($g.owner.started)" } })"
    $rows = foreach ($prefix in $pendingPrefix, $slotPrefix) {
        foreach ($f in Get-ChildItem -LiteralPath $commonDir -Filter "$prefix*.json" -File -ErrorAction SilentlyContinue | Sort-Object Name) {
            $o = Read-LockJson $f.FullName
            [pscustomobject]@{ kind = $(if ($prefix -eq $slotPrefix) { "slot $($f.BaseName.Substring($prefix.Length))" } else { "gate ticket $(Get-TicketNumber $f.Name)" })
                state = $(if (Test-LockStale $o $f.FullName $staleHours) { 'stale' } else { 'live' })
                pid = $o.pid; branch = $o.branch; started = $o.started; cwd = $o.cwd }
        }
    }
    $live = @($rows | Where-Object state -eq 'live')
    $held = @($live | Where-Object { $_.kind -like 'slot*' }).Count
    $queued = @($live | Where-Object { $_.kind -like 'gate ticket*' }).Count
    $stale = @($rows).Count - $live.Count
    "build slots: $held held of $slots; gate requests queued: $queued; stale files: $stale (the next request removes them) (files in $commonDir)"
    if ($rows) { $rows | Format-Table -AutoSize | Out-String -Width 200 }
}

function Get-TicketNumber([string]$name) {
    $n = 0L
    # Tickets are 1 to 99999999 (eight digits); anything else, such as a planted huge number, is not a ticket.
    if ($name -match "^$([regex]::Escape($pendingPrefix))(\d{1,8})\.json$" -and [long]::TryParse($Matches[1], [ref]$n) -and $n -ge 1) { $n } else { $null }
}
function New-GateTicket($record, [double]$staleHours) {
    # FIFO ticket: one past the highest live ticket (stale ticket files are removed first, and numbering restarts at 1
    # when none is live), claimed with CreateNew (a collision moves on to the next number), so a new request always
    # queues behind every request already waiting.
    for ($try = 0; $try -lt 200; $try++) {
        $max = 0L
        foreach ($r in Get-LiveLocks $pendingPrefix $staleHours $null) {
            $n = Get-TicketNumber $r.file; if ($null -ne $n -and $n -gt $max) { $max = $n }
        }
        if ($max + 1 + $try -gt 99999999) { throw "Gate ticket numbers exhausted in $commonDir; remove stale gate-pending files" }
        $ticket = $max + 1 + $try
        $path = Join-Path $commonDir ('{0}{1:D8}.json' -f $pendingPrefix, $ticket)
        $record['ticket'] = $ticket
        $stream = Open-LockFile $path $record
        if ($stream) { return [pscustomobject]@{ ticket = $ticket; path = $path; stream = $stream } }
    }
    throw "Cannot take a gate ticket in $commonDir"
}