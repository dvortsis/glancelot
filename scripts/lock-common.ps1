# Shared helpers for gate-lock.ps1 and build-slot.ps1 (dot-sourced; not run directly). All lock files are
# machine-local, in the shared git common dir, so every worktree sees them:
#   glancelot-gate-lock\owner.json      the full-gate lock (a directory; creating it is atomic)
#   glancelot-build-slot-<n>.json       one file per held build slot
#   glancelot-gate-pending-<pid>.json   a gate request waiting to start; new build slots wait while one exists
# Slot and pending files are created atomically (CreateNew) and kept open by their holder, so on Windows a live
# holder's file cannot be deleted. Stale files (dead or reused PID, or older than the stale limit) are removed only
# after a re-read shows the same record. Nothing here kills processes.
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
function Test-LockStale($owner, [string]$path, [double]$staleHours) {
    # No record yet: held for two minutes (the holder may still be writing it). Otherwise stale when older than
    # the limit, or on this host when the PID is dead or now belongs to a process that started after the claim.
    if (-not $owner) {
        $item = Get-Item -LiteralPath $path -ErrorAction SilentlyContinue
        return (-not $item) -or ((Get-Date) - $item.CreationTime).TotalMinutes -gt 2
    }
    if (((Get-Date) - [datetime]$owner.started).TotalHours -gt $staleHours) { return $true }
    if ($owner.host -ne $machine) { return $false }
    $proc = Get-Process -Id $owner.pid -ErrorAction SilentlyContinue
    if (-not $proc) { return $true }
    try { return $proc.StartTime -gt ([datetime]$owner.started).AddSeconds(2) } catch { return $false }
}
function Get-AncestorIds {
    # This process and its ancestors. Locks they hold belong to the caller's own process tree.
    $ids = [Collections.Generic.HashSet[int]]::new()
    $p = Get-Process -Id $PID
    for ($i = 0; $p -and $i -lt 64 -and $ids.Add($p.Id); $i++) { try { $p = $p.Parent } catch { $p = $null } }
    , $ids
}
function New-LockRecord([hashtable]$extra) {
    $r = [ordered]@{ holder = $env:USERNAME; started = (Get-Date).ToString('o'); pid = $PID; host = $machine; cwd = (Get-Location).Path }
    foreach ($k in $extra.Keys) { $r[$k] = $extra[$k] }
    $r
}
function Open-LockFile([string]$path, $record) {
    # Atomic claim. Returns the open stream (held until release, no Delete share) or $null when the file exists.
    try { $stream = [IO.File]::Open($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read) }
    catch [IO.IOException] { return $null } catch [UnauthorizedAccessException] { return $null }
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes(($record | ConvertTo-Json))
    $stream.Write($bytes, 0, $bytes.Length); $stream.Flush()
    $stream
}
function Close-LockFile($stream, [string]$path) {
    # Release our own claim only: close the handle, then delete the file if it still carries our PID.
    if ($stream) { $stream.Dispose() }
    if (-not $path) { return }
    $owner = Read-LockJson $path
    if ($owner -and $owner.pid -eq $PID -and $owner.host -eq $machine) { Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue }
}
function Remove-StaleLockFile([string]$path, $judged) {
    # Re-read just before deleting and delete only the record judged stale, so a racing fresh claim survives.
    $now = Read-LockJson $path
    if ($judged -and (-not $now -or $now.pid -ne $judged.pid -or $now.started -ne $judged.started)) { return $false }
    if (-not $judged -and $now) { return $false }
    try { [IO.File]::Delete($path) } catch { return $false }
    Write-Warning "Removed stale $(Split-Path $path -Leaf): $($judged | ConvertTo-Json -Compress)"
    $true
}
function Get-LiveLocks([string]$prefix, [double]$staleHours, $ignoreIds) {
    # Live slot or pending files, minus those held by $ignoreIds on this host; stale ones are removed safely.
    foreach ($f in Get-ChildItem -LiteralPath $commonDir -Filter "$prefix*.json" -File -ErrorAction SilentlyContinue) {
        $o = Read-LockJson $f.FullName
        if (Test-LockStale $o $f.FullName $staleHours) { [void](Remove-StaleLockFile $f.FullName $o); continue }
        if ($o -and $null -ne $ignoreIds -and $o.host -eq $machine -and $ignoreIds.Contains([int]$o.pid)) { continue }
        if (-not $o) { $o = [pscustomobject]@{ pid = 0; branch = '?'; started = '?'; host = '?' } }
        $o | Add-Member -NotePropertyName file -NotePropertyValue $f.Name -PassThru
    }
}
function Get-GateOwner([double]$staleHours) {
    # The gate lock's owner (or a placeholder while it is being written); $null when free or stale. Never removes it.
    if (-not (Test-Path -LiteralPath $gateLock)) { return $null }
    $owner = Read-LockJson $gateOwnerFile
    if (Test-LockStale $owner $gateLock $staleHours) { return $null }
    if ($owner) { $owner } else { [pscustomobject]@{ holder = '?'; branch = '?'; started = '?'; pid = 0; host = $machine } }
}
function Show-LockStatus([double]$staleHours, [int]$slots) {
    $g = if (-not (Test-Path -LiteralPath $gateLock)) { 'free' } else {
        $o = Read-LockJson $gateOwnerFile
        if (Test-LockStale $o $gateLock $staleHours) { 'stale (gate-lock.ps1 removes it on its next request)' }
        else { "held by $($o.holder) on $($o.branch) (pid $($o.pid)) since $($o.started)" } }
    "gate lock: $g"
    $rows = foreach ($prefix in $pendingPrefix, $slotPrefix) {
        foreach ($f in Get-ChildItem -LiteralPath $commonDir -Filter "$prefix*.json" -File -ErrorAction SilentlyContinue | Sort-Object Name) {
            $o = Read-LockJson $f.FullName
            [pscustomobject]@{ kind = $(if ($prefix -eq $slotPrefix) { "slot $($f.BaseName.Substring($prefix.Length))" } else { 'gate pending' })
                state = $(if (Test-LockStale $o $f.FullName $staleHours) { 'stale' } else { 'live' })
                pid = $o.pid; branch = $o.branch; started = $o.started; cwd = $o.cwd }
        }
    }
    $held = @($rows | Where-Object { $_.kind -like 'slot*' }).Count
    "build slots: $held held of $slots; gate requests pending: $(@($rows | Where-Object kind -eq 'gate pending').Count) (files in $commonDir)"
    if ($rows) { $rows | Format-Table -AutoSize | Out-String -Width 200 }
}
