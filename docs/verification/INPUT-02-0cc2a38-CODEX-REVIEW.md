# INPUT-02 review and correction, October 21, 2026

Reviewed exact Claude candidate `0cc2a38990a9bac03c2f623701fc287ba74011f8` against the prior pinned review and frozen INPUT contract. Worktree `.worktrees/codex-input-02-r3`, branch `codex/input-02-r3`, build `build-codex-input-02-r3`. No main merge; original Claude worktrees untouched. User explicitly authorized fixes and commits.

## Finding and correction

**P2, required:** registration is fixed, but the candidate adds a separate `blocked` property to `CommandMenuItem` and exempts blocked rows from parity checks. A root's Delete shortcut remains enabled while the menu is disabled; busy operations have the same discrepancy. This leaves the exact prior shared-eligibility finding open.

Codex product correction `953adf9fe7bca980a87d02fe6a79d9ff487aa17f` removes the exception. `commandBlockedReason` is shared by scope eligibility, explicit routes, and the execution guard. Menu labels explain root/busy refusal. Claude's registered command route, cursor target and root protection remain. Strict parity checks again compare every actionable row, including Trash, against the live shortcut. Exact captured folder paths are checked for Delete, menu keyboard and pointer routes. A semaphore-held fake trash backend proves busy blocking/recovery without mutating any media. Early test exits release that backend before service teardown.

Test-only follow-up `330d050881f2330f80755f2c30c7d37f4aef3436` addresses the reproduced Linux results-delegate race: re-query the live tree until exactly two failure reasons exist, keeping both required message assertions. No timeout, product behavior or expected result count was changed. This modifies an existing test; all newly added feature tests remain in `input_ui_tests.cpp`.

## Actual evidence so far

All raw commands/results are in [input-02-r3](input-02-r3/). Full gates use the main machine gate-lock in the same PowerShell process.

- Initial build: 199/199, exit0. A later targeted invocation used a stale binary missing the newly added busy case; its failed `Function not found` output is retained separately. Explicitly refreshing the test source timestamp rebuilt the executable before the final successful targeted run.
- Strict root probe on unchanged candidate UI: exit2, `2 passed, 2 failed`; both failures are `!allowed(h, "delete-selection")`. Actual `windows-original-strict-parity.txt`.
- Corrected focused run: exit0, `5 passed, 0 failed`,4491ms, actual `windows-correction-cases-final.txt`.
- Full Windows on product953adf9 before the delegate-test follow-up:14/15,245.88s, exit1. `input_ui` passed21.76s. Sole failure is the previously recorded platform terminal invalid-path test on this old branch; the full gate did not print its GUI QtTest detail. No full baseline rerun or platform waiver is claimed.
- First Docker gate: exit130, context transfer canceled before compilation; unqualified. Retry:14/15,128.34s, exit1. `input_ui` passed16.06s. Sole failure: `deleteBatchProgressStopAndPartialFailureResults`, actual reason-delegate count1, expected2. This prompted330d050; it is not classified as an input product regression or silently discarded.

Commands:

```powershell
& C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch codex/input-02-r3 -Run { & ./scripts/build.ps1 -BuildDir build-codex-input-02-r3 -Test }
& C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch codex/input-02-r3 -Run { docker build --progress=plain -t glancelot-browser:codex-input-02-r3 . }
# Targeted run, Qt bin on PATH, QT_QPA_PLATFORM=offscreen, QT_QUICK_BACKEND=software:
& ./build-codex-input-02-r3/input_ui_tests.exe treeDeleteKeyMatchesItsMenuOnARoot treeTrashRowRunsTheDeleteCommandOnTheCursorFolder treeTrashAndDeleteKeyShareBusyEligibility -o docs/verification/input-02-r3/windows-correction-cases-final.txt,txt
```

## Independent review and limits

Codex authored the correction and cannot approve it. Actual read-only Sonnet5.5 review attempts hit the configured service's session limit (429; reset noon Denver); unedited JSON/prompts/status retained. A retry after reset is pending. No main/product integration approval.

Applied native UI design skills: shared Theme menu primitive, destructive label/icon treatment, keyboard/focus lifecycle and compact reachability. No new geometry, palette, motion or typography. Physical trackpads/layouts/IMEs, accessibility tooling, mixed-DPI hardware and physical presentation remain unverified. Source acceptance and full integrated-commit qualification are separate.

## Completed follow-up evidence

Final Linux Docker on330d050:15/15,136.64s,exit0 (docker-final.txt and status). Windows targeted delegate regression:3passed,0failed,1524ms,exit0. Direct awaited platform detail:4passed,1failed,exit1; openTerminal on nonexistent C:/nonexistent_dir_path_12345 returnedtrue (windows-platform-detail.txt). Windows full14/15 remains unqualified; no new source change waives it.

Actual Sonnet5.5 source review ACCEPT sessioneb110055-cb9d-4a25-83d0-16c2e91f46d0 (claude-noon.json,exit0), with nonblocking advisories. It skipped requested skills, so an explicit follow-up read all9requiredskills and ACCEPTED (CLAUDE-UI-FOLLOWUP.md,claude-ui-followup.json,exit0). The follow-up reviewed relevant UI/model source and only grepped test patch; the first source review read the testsource. Reports disclose their limits. Root model-notification hardening and silentblockedkeyfeedback remain nonblocking advisories; no mandatory source correction was requested. This sourceacceptance does not approve integration or oldbranch Windowsplatformqualification.
