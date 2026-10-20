# SAFE-MODE-01 independent review preparation

Status: concrete review snapshot; complete native qualification remains open.
Implementing lane: Codex, branch `codex/safe-mode-01`, assigned worktree
`.worktrees/codex-safe-mode-01`, build `build-codex-safe-mode-01`.
The lead pins the source/evidence commit in the review request. A reviewer must come from another lane
and use an isolated checkout/build. Do not review a moving branch as though it
were the final candidate. Claude service review of this separate SAFE payload
still needs the user's concrete authorization; PLATFORM/FS approvals do not cover it.

## Frozen scope

Read `docs/SAFE-MODE-01-PLAN.md`, `docs/REQUIREMENTS.md`, and the F064/F057 engine
assignment in `docs/AGENT-LANES-PLAN.md`. This packet includes safe startup,
configuration diff/backup/checked restore and the crash-recovery engine. Claude
owns later recovery UI. No product QML changes or original-media writes belong
to this packet. Hardware/physical power-loss qualification remains unverified.

## Behaviors requiring independent inspection

- Early native Unicode parsing, Qt GUI option compatibility, argument rejection
  before mutation, QCore-only maintenance/help/version, exact bounded stdout and
  stable exit/status codes. Maintenance must never instantiate viewer owners.
- Pre-QGui safe temporary namespace, original settings and evidence preservation,
  actual software RHI/backend/frame/RGB proof, owner lifetime through outstanding
  work, and normal-launch visibility of original preferences after safe exit.
- Raw primary/backup presence and exact bytes, bounded reads and revisions,
  supported/corrupt/future handling, sealed exclusive backup packages, fixed member
  names and bounded manifest validation, cancellation and partial-package refusal.
- Immutable preview plans and fingerprints, bounded aggregate/depth/row output,
  schema/policy/scope/package/namespace/revision revalidation, unknown nested data
  preservation, no applicable plan after truncation or unsupported input, and an
  exact sealed raw preimage before primary replacement. NoChange still checks both
  primary and backup. Selecting a package's last-good member is explicit. A corrupt
  current primary may use its supported backup as the disclosed logical base;
  a future current primary must still be refused.
- Session guard before settings lock, nonce/PID/start/namespace claim identity,
  same-process reclaim revocation, offline refusal for live/ambiguous ownership,
  other-slot preservation, atomic checkpoint position/sequence/revision, and clean
  final marks only for the newest acknowledged successful checkpoint.
- Value-owned asynchronous jobs, one active/one latest pending, cancellation,
  guarded controller completion and reentrant generation changes, bounded shutdown,
  explicit-open precedence even on failure, late claims, lazy resume and empty folders.
- Shipped command/binding behavior and settings writer compatibility across all
  existing callers. Production must have no private test hooks or crash-helper mode.

## Current actual evidence and open gates

`docs/verification/SAFE-MODE-01.md` and its referenced raw artifacts are authoritative
for measured results, including failures. Release process verification passed33
commands on Windows with actual D3D11 CPU device/frame/RGB evidence. An intervening
zero-frame attempt is retained and remains unexplained; do not replace it with a
claim inferred from renderer request flags. Physical power loss is not measured.

Real owned-child restore interruption tests cover supported and corrupt primary
with a supported last-good base at four transaction boundaries: before evidence,
after sealed preimage, before replacement and after replacement. Eight cases
passed, including exact original pair/preimage and OS-lock release checks. These
are process-death checks, not filesystem durability or physical power-loss proof.

First full Windows and Docker builds failed because settings-only consumers were
not attached to the recovery engine. Dedicated recovery CMake tests then exposed
a doubled catalog resource alias;13 policy cases fail closed as Unreadable.
Both shared build corrections are now committed by the lead in main as f17a2a3
and synchronized into the feature with monitor-output in candidate d4f0e70.
The generated resource now has prefix / and alias themes/catalog.json. All229
registry literal/flag/platform tokens match integrated main, including its five
presentation commands. The corrected Docker gate passed37/37 in257.43s, with
recovery135/135, startup59/59, controller10/10 and the actual software-RHI process
test all passing. New recovery targets have zero skips on both platforms.

The corrected complete Windows gate finished35/37 in919.59s: folder_grid hit its
original400s timeout, and fs_watch failed the unchanged-cache-hit assertion in
noncurrentSameTupleReplacementInvalidatesCache (81other cases passed). Every other
test passed, including all recovery targets and the actual33-command process test.
The unchanged grid binary then passed isolated under identical renderer settings
and timeout:74passed,4existing opt-in capture skips,206.56s. The selected cache-hit
case passed20 consecutive isolated runs (28.41s). These do not establish the cause
or replace a complete Windows pass. A quiet full rerun remains required; its first
start precondition refused when a separate monitor-output fix build occupied the
slot. No tests or budgets were weakened.

Both complete gate logs, actual per-test results and raw generated package/RHI/RGB
artifacts are committed under docs/verification/safe-mode-01. Start with the updated
SAFE-MODE-01 verification narrative and inspect the source/tests, rather than
treating this handoff or green components as approval. Review the whole frozen
engine contract and interactions with inherited code; report any source defect
that could explain the native failures. An independent source review can proceed
while native qualification is investigated, but the packet cannot be merged or
marked done until all required gates and integrated checks pass.

Required before this packet can become done: all required
Windows and Docker/Linux tests including actual software-RHI process proof,
independent cross-lane review of a pinned candidate, any resulting fixes, serial
main merge and actual integrated checks. Report failed runs and unavailable physical
platform tests explicitly. Do not disable existing tests or weaken renderer proof.
