# Lead only: merge one pull request after checking the merge gate (AGENTS.md, "Pull requests and merging").
# Merges with a merge commit, pinned to the head that was checked, so a later push cannot slip in.
#   ci-ok              required by branch protection; checked here too
#   fresh-review       commit status on the head: a fresh Opus reviewer found nothing blocking
#   local-gate         commit status on the head: full Windows plus Docker gate passed on this commit
# Lead bookkeeping (a lead/ branch that only touches the queue, handoffs, decisions or evidence) needs ci-ok only.
# Any other docs-only pull request needs ci-ok and fresh-review. -Auto hands the wait for ci-ok to GitHub auto-merge.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][int]$Pr,
    [switch]$Auto,
    [string]$Repo = 'dvortsis/glancelot'
)
$ErrorActionPreference = 'Stop'

function Test-DocsOnly([string]$Path) { $Path -like 'docs/*' -or $Path -like '*.md' -or $Path -eq '.gitattributes' }
function Test-Bookkeeping([string]$Path) { $Path -eq 'docs/WORK-QUEUE.yaml' -or $Path -like 'docs/verification/*' -or $Path -like 'docs/handoff/*' -or $Path -like 'docs/decisions/*' -or $Path -eq '.gitattributes' }

$view = gh pr view $Pr --repo $Repo --json state,isDraft,headRefName,headRefOid,title,files,statusCheckRollup | ConvertFrom-Json
if ($view.state -ne 'OPEN') { throw "PR $Pr is $($view.state)." }
if ($view.isDraft) { throw "PR $Pr is a draft." }
$sha = $view.headRefOid
$paths = @($view.files | ForEach-Object { $_.path })

$statuses = gh api "repos/$Repo/commits/$sha/status" | ConvertFrom-Json
function Test-Status([string]$Context) { [bool](@($statuses.statuses) | Where-Object { $_.context -eq $Context -and $_.state -eq 'success' }) }

$problems = [System.Collections.Generic.List[string]]::new()
$ciOk = @($view.statusCheckRollup) | Where-Object { $_.name -eq 'ci-ok' } | Select-Object -First 1
if (-not $Auto -and (-not $ciOk -or $ciOk.conclusion -ne 'SUCCESS')) { $problems.Add("ci-ok is not green on $sha.") }

$bookkeeping = $view.headRefName -like 'lead/*' -and -not @($paths | Where-Object { -not (Test-Bookkeeping $_) })
$docsOnly = -not @($paths | Where-Object { -not (Test-DocsOnly $_) })
if (-not $bookkeeping -and -not (Test-Status 'fresh-review')) { $problems.Add("No fresh-review status on $sha.") }
if (-not $docsOnly -and -not (Test-Status 'local-gate')) { $problems.Add("No local-gate status on $sha.") }
if ($problems.Count) { $problems | ForEach-Object { Write-Host $_ }; throw "PR $Pr does not meet the merge gate." }

if ($Auto) {
    gh pr merge $Pr --repo $Repo --merge --match-head-commit $sha --auto
    if ($LASTEXITCODE) { throw 'gh pr merge failed.' }
    Write-Host "PR $Pr will merge at $sha when ci-ok passes."
    return
}
gh pr merge $Pr --repo $Repo --merge --match-head-commit $sha
if ($LASTEXITCODE) { throw 'gh pr merge failed.' }
gh api -X DELETE "repos/$Repo/git/refs/heads/$($view.headRefName)" | Out-Null
Write-Host "Merged PR $Pr at $sha and deleted $($view.headRefName)."
