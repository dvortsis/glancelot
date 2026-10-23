# Lead inbox: one read-only status line per origin/claude/* packet branch. Fetches (with prune) and reports the tip,
# its handoff and latest "Review round" heading, matching review branches with their verdicts, whether the branch
# is merged into origin/main and its queue state on origin/main. Usage:
#   & C:\src\Github\Glancelot\scripts\lead-inbox.ps1            # table
#   & C:\src\Github\Glancelot\scripts\lead-inbox.ps1 -Json      # machine output
# Review branches (origin/claude/review-<task>[-*]) appear under their packet; ones that match no packet get a row.
param([switch]$Json, [switch]$NoFetch, [string]$Remote = 'origin', [string]$Main = 'main', [int]$Width = 240)
$ErrorActionPreference = 'Stop'
function Write-Warning([string]$Message) { [Console]::Error.WriteLine("WARNING: $Message") }
if (-not $NoFetch) { git fetch --prune --quiet $Remote 2>$null; if ($LASTEXITCODE -ne 0) { Write-Warning "git fetch --prune $Remote failed; using cached refs" } }
$mainRef = "$Remote/$Main"

# Queue on origin/main: tasks are "  - id:" entries; their own keys sit at exactly four spaces.
$queue = @{}; $queueById = @{}
$yaml = git show "${mainRef}:docs/WORK-QUEUE.yaml" 2>$null
if ($LASTEXITCODE -eq 0 -and $yaml) {
    $inTasks = $false; $task = $null
    $flush = { if ($task) { $queueById[$task.id] = $task; if ($task.branch) { $queue[$task.branch] = $task } } }
    foreach ($line in $yaml) {
        if ($line -match '^tasks:\s*$') { $inTasks = $true; continue }
        if (-not $inTasks) { continue }
        if ($line -match '^\S') { & $flush; $task = $null; $inTasks = $false; continue }
        if ($line -match '^  - id:\s*(.+?)\s*$') { & $flush; $task = @{ id = $Matches[1].Trim('"', "'"); state = ''; branch = '' }; continue }
        if ($task -and $line -match '^    (state|branch):\s*(.+?)\s*$') { $task[$Matches[1]] = $Matches[2].Trim('"', "'") }
    }
    & $flush
} else { Write-Warning "docs/WORK-QUEUE.yaml not readable on $mainRef" }

function Get-OwnFiles([string]$ref) {
    # Files the branch's own commits touched (newest first, deduplicated); main-only files never show up here.
    $seen = [Collections.Generic.HashSet[string]]::new()
    git log "$mainRef..$ref" --name-only --format= 2>$null | Where-Object { $_ -and $seen.Add($_) }
}
function Get-TaskKey([string]$name) {
    # claude/en-09-feasibility -> EN-09, claude/f002 -> F002, claude/stack-f015-recovery -> F015
    if ($name -match '^(?:stack-)?([a-z]+-?\d+)') { $Matches[1].ToUpperInvariant() } else { $null }
}

# Ref names and SHAs only (they never need the object), then each date separately: a ref whose object is missing
# gets a blank date instead of breaking the whole listing.
$refs = git for-each-ref --format='%(refname:short)|%(objectname:short)' "refs/remotes/$Remote/claude/" 2>$null
$branches = foreach ($r in $refs) {
    $p = $r -split '\|'
    if ($p.Count -lt 2 -or $p[0].Length -le "$Remote/claude/".Length) { continue }
    $date = git log -1 --format=%cs $p[1] 2>$null; if ($LASTEXITCODE -ne 0) { $date = $null }
    [pscustomobject]@{ ref = $p[0]; name = $p[0].Substring("$Remote/claude/".Length); sha = $p[1]; date = $date }
}
$reviewRefs = @($branches | Where-Object { $_.name -like 'review-*' })
$matchedReviews = [Collections.Generic.HashSet[string]]::new()

function Get-ReviewVerdict($rb) {
    # Newest top-level docs/verification/*REVIEW*.md touched by the review branch's own commits; verdict from its top.
    $file = Get-OwnFiles $rb.ref | Where-Object { $_ -match '^docs/verification/[^/]*REVIEW[^/]*\.md$' } | Select-Object -First 1
    $taskKey = Get-TaskKey ($rb.name -replace '^review-', '')
    if (-not $file -and $taskKey) {
        # Already merged into main, so no own commits: take the newest <TASK>*REVIEW*.md on the branch.
        $file = git ls-tree --name-only "$($rb.ref)" docs/verification/ 2>$null |
            Where-Object { (Split-Path $_ -Leaf) -like "$taskKey*REVIEW*.md" } |
            Sort-Object { [long](git log -1 --format=%ct $rb.ref -- $_) } -Descending | Select-Object -First 1
    }
    if (-not $file) { return [pscustomobject]@{ branch = "claude/$($rb.name)"; sha = $rb.sha; file = $null; verdict = 'no review file' } }
    $top = git show "$($rb.ref):$file" 2>$null | Select-Object -First 15
    # A file without a verdict line (or an unreadable one) gives no match, never an error.
    $verdict = $null
    foreach ($pattern in 'Verdict:?\s*\**\s*(APPROVED|CHANGES REQUIRED)', '\b(APPROVED|CHANGES REQUIRED)\b') {
        $hit = @($top) | Where-Object { "$_" -match $pattern } | Select-Object -First 1
        if ($hit -and "$hit" -match $pattern) { $verdict = $Matches[1].ToUpperInvariant(); break }
    }
    [pscustomobject]@{ branch = "claude/$($rb.name)"; sha = $rb.sha; file = (Split-Path $file -Leaf); verdict = $(if ($verdict) { $verdict } else { 'no verdict line' }) }
}

function Get-ReviewVerdictSafe($rb) {
    # Any branch shape (deleted after --prune, no files, odd names) degrades to a blank verdict instead of failing.
    try { Get-ReviewVerdict $rb } catch { [pscustomobject]@{ branch = "claude/$($rb.name)"; sha = $rb.sha; file = $null; verdict = '' } }
}
$rows = foreach ($b in $branches | Where-Object { $_.name -notlike 'review-*' }) { try {
    # Queue row by its branch: field; rows without one are matched by the id parsed from the branch name.
    $q = $queue["claude/$($b.name)"]; $queueMatch = if ($q) { 'branch' } else { $null }
    $taskId = if ($q) { $q.id } else { Get-TaskKey $b.name }
    if (-not $q -and $taskId -and $queueById[$taskId] -and -not $queueById[$taskId].branch) { $q = $queueById[$taskId]; $queueMatch = 'id' }
    $own = @(Get-OwnFiles $b.ref)
    $handoff = $own | Where-Object { $_ -match '^docs/handoff/[^/]+-HANDOFF\.md$' } | Select-Object -First 1
    if (-not $handoff -and $taskId) {
        $candidate = "docs/handoff/$taskId-HANDOFF.md"
        git cat-file -e "$($b.ref):$candidate" 2>$null; if ($LASTEXITCODE -eq 0) { $handoff = $candidate }
    }
    $round = $null
    if ($handoff) { $round = (git show "$($b.ref):$handoff" 2>$null | Where-Object { $_ -match '^#+\s*Review round\b' } | Select-Object -Last 1) -replace '^#+\s*', '' }
    $reviews = @()
    if ($taskId) {
        $key = "review-$($taskId.ToLowerInvariant())"
        $reviews = @($reviewRefs | Where-Object { $_.name -eq $key -or $_.name -like "$key-*" } | Sort-Object name | ForEach-Object { [void]$matchedReviews.Add($_.name); Get-ReviewVerdictSafe $_ })
    }
    git merge-base --is-ancestor $b.ref $mainRef 2>$null; $merged = $LASTEXITCODE -eq 0
    [pscustomobject]@{ branch = "claude/$($b.name)"; tip = $b.sha; date = $b.date; task = $taskId; queue = $(if ($q) { $q.state } else { $null }); queueMatch = $queueMatch
        merged = $merged; handoff = $(if ($handoff) { Split-Path $handoff -Leaf } else { $null }); reviewRound = $round; reviews = $reviews }
} catch {
    # Any branch shape that still fails degrades to blank cells, with the reason on the warning stream.
    Write-Warning "claude/$($b.name): $($_.Exception.Message)"
    [pscustomobject]@{ branch = "claude/$($b.name)"; tip = $b.sha; date = $b.date; task = $null; queue = $null; queueMatch = $null
        merged = $null; handoff = $null; reviewRound = $null; reviews = @() }
} }
$orphans = foreach ($rb in $reviewRefs | Where-Object { -not $matchedReviews.Contains($_.name) }) {
    git merge-base --is-ancestor $rb.ref $mainRef 2>$null; $merged = $LASTEXITCODE -eq 0
    [pscustomobject]@{ branch = "claude/$($rb.name)"; tip = $rb.sha; date = $rb.date; task = $null; queue = $null; queueMatch = $null; merged = $merged
        handoff = $null; reviewRound = '(review branch, no matching packet)'; reviews = @(Get-ReviewVerdictSafe $rb) }
}
$all = @($rows) + @($orphans)

if ($Json) { $all | ConvertTo-Json -Depth 5; return }
$all | Select-Object branch, tip, date, task, @{ n = 'queue'; e = { if ($_.queueMatch -eq 'id') { "$($_.queue)*" } else { $_.queue } } }, @{ n = 'merged'; e = { if ($_.merged) { 'yes' } else { 'no' } } }, handoff,
    @{ n = 'round'; e = { $_.reviewRound } },
    @{ n = 'reviews'; e = { ($_.reviews | ForEach-Object { "$($_.branch -replace '^claude/', ''): $($_.verdict)" }) -join '; ' } } |
    Format-Table -AutoSize | Out-String -Width $Width
'queue*: matched by task id (the queue row has no branch: field)'
