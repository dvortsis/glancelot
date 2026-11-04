# Lead only: merge one pull request after checking the merge gate (AGENTS.md, "Pull requests and merging").
# Merges with a merge commit, pinned to the head that was checked, so a later push cannot slip in.
#   ci-ok              required by branch protection; checked here too
#   fresh-review       commit status on the head: a fresh Opus reviewer found nothing blocking
#   local-gate         commit status on the head: full Windows plus Docker gate passed on this commit
# Needs local-gate unless the PR is only docs or CI tooling, and then main must hold no code the head lacks, so the
# tree that lands is the tree that was gated. Lead bookkeeping (a lead/ branch that only touches the queue, handoffs,
# decisions or evidence) needs ci-ok only. -Auto hands the wait for ci-ok to GitHub auto-merge; refused when the PR
# needs local-gate, because main could move during the wait.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][int]$Pr,
    [switch]$Auto,
    [string]$Repo = 'dvortsis/glancelot'
)
$ErrorActionPreference = 'Stop'

function Test-DocsOnly([string]$Path, [string]$Status = 'modified') {
    # Same rule as is_docs_only in scripts/ci/pr_guard.py. Dockerfile and scripts/build.ps1 copy the two root files.
    if ($Path -in 'README.md', 'THIRD_PARTY_NOTICES.md') { return $Status -ne 'removed' }
    $Path -like 'docs/*' -or $Path -like '*.md'
}
function Test-Bookkeeping([string]$Path) { $Path -eq 'docs/WORK-QUEUE.yaml' -or $Path -like 'docs/verification/*' -or $Path -like 'docs/handoff/*' -or $Path -like 'docs/decisions/*' }
function Test-CiTooling([string]$Path) { $Path -like '.github/*' -or $Path -like 'scripts/ci/*' -or $Path -like 'scripts/lead-*' }
function Test-LeadOnly([string]$Path) { $Path -like '.github/workflows/*' -or $Path -like 'scripts/ci/*' }

$pull = gh api "repos/$Repo/pulls/$Pr" | ConvertFrom-Json
if ($pull.state -ne 'open') { throw "PR $Pr is $($pull.state)." }
if ($pull.draft) { throw "PR $Pr is a draft." }
if ($pull.head.repo.full_name -ne $Repo) { throw "PR $Pr comes from a fork ($($pull.head.repo.full_name)); this script merges only this repository's branches." }
$sha = $pull.head.sha
$headRef = $pull.head.ref

# The REST list is paged, and carries the old name of a rename: a move out of src/ must not read as docs.
$files = @(gh api --paginate "repos/$Repo/pulls/$Pr/files" --jq '.[]|[.status, .filename, (.previous_filename // "")]|@tsv')
if ($files.Count -ne $pull.changed_files) { throw "PR $Pr lists $($files.Count) of $($pull.changed_files) files." }
$entries = @($files | ForEach-Object {
    $status, $path, $previous = $_ -split "`t"
    [pscustomobject]@{ Status = $status; Path = $path }
    if ($previous) { [pscustomobject]@{ Status = 'removed'; Path = $previous } }
})
$paths = @($entries | ForEach-Object Path)

$statuses = gh api "repos/$Repo/commits/$sha/status" | ConvertFrom-Json
function Test-Status([string]$Context) { [bool](@($statuses.statuses) | Where-Object { $_.context -eq $Context -and $_.state -eq 'success' }) }

$checks = gh pr view $Pr --repo $Repo --json statusCheckRollup | ConvertFrom-Json
$ciOk = @($checks.statusCheckRollup) | Where-Object { $_.name -eq 'ci-ok' } | Sort-Object { [datetime]$_.completedAt } -Descending | Select-Object -First 1

$bookkeeping = $headRef -like 'lead/*' -and -not @($paths | Where-Object { -not (Test-Bookkeeping $_) })
$needsGate = [bool]@($entries | Where-Object { -not (Test-DocsOnly $_.Path $_.Status) -and -not (Test-CiTooling $_.Path) })

$problems = [System.Collections.Generic.List[string]]::new()
if (-not $Auto -and (-not $ciOk -or $ciOk.conclusion -ne 'SUCCESS')) { $problems.Add("ci-ok is not green on $sha.") }
if (-not $bookkeeping -and -not (Test-Status 'fresh-review')) { $problems.Add("No fresh-review status on $sha.") }
if ($needsGate) {
    if ($Auto) { $problems.Add('-Auto is for PRs without code: main could move while it waits, and the landed tree would not be the gated one.') }
    if (-not (Test-Status 'local-gate')) { $problems.Add("No local-gate status on $sha.") }
    $drift = @(gh api --paginate "repos/$Repo/compare/$sha...main" --jq '.files[]|[.status, .filename]|@tsv' | Where-Object {
        $status, $path = $_ -split "`t"; -not (Test-DocsOnly $path $status) })
    if ($drift.Count) { $problems.Add("main has code this head lacks ($($drift.Count) files): merge main in and gate the new head.") }
}
if ($headRef -like 'claude/*' -and @($pull.labels.name) -notcontains 'lead-grant') {
    # pr_guard.py cannot be trusted for this: a pull request runs its own copy of it.
    @($paths | Where-Object { Test-LeadOnly $_ }) | ForEach-Object { $problems.Add("$_ is lead-owned and the PR has no lead-grant label.") }
}
if ($problems.Count) { $problems | ForEach-Object { Write-Host $_ }; throw "PR $Pr does not meet the merge gate." }

if ($Auto) {
    gh pr merge $Pr --repo $Repo --merge --match-head-commit $sha --auto
    if ($LASTEXITCODE) { throw 'gh pr merge failed.' }
    Write-Host "PR $Pr will merge at $sha when ci-ok passes."
    return
}
gh pr merge $Pr --repo $Repo --merge --match-head-commit $sha
if ($LASTEXITCODE) { throw 'gh pr merge failed.' }
gh api -X DELETE "repos/$Repo/git/refs/heads/$headRef" | Out-Null
if ($LASTEXITCODE) { Write-Warning "Merged PR $Pr at $sha, but could not delete $headRef."; return }
Write-Host "Merged PR $Pr at $sha and deleted $headRef."
