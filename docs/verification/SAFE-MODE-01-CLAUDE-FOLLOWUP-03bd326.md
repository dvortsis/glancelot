# SAFE-MODE-01 Claude follow-up review: 03bd326

Reviewer: Claude lane (claude/*), read-only; pinned commit 03bd326 on codex/safe-mode-01 (child of 61e4752, which was reviewed in SAFE-MODE-01-CLAUDE-FOLLOWUP-61e4752.md). Worktree .worktrees/claude-review-safe-mode-01-r3.

## Scope
src/app/viewer_controller.cpp (sort feedback), tests/recovery_controller_tests.cpp (two new tests), correction notes.

## Findings
No blocking findings. Checked against settings_file.cpp SettingsRetries::submit/run:
- Sync success: completed() runs inside saveWithRetry, clearing sortSavePending; message ends with "." — correct.
- Deferred (lock held): sortSavePending stays true, status says "for this session; saving settings is pending."; when the retry settles the callback rewrites status only if it still equals the sort feedback, so newer unrelated status is preserved (covered by sortCompletionPreservesNewerFeedback).
- Same-channel supersede erases the older pending edit without calling its completion, so only the latest edit's callback clears sortSavePending; no premature "saved". 
- Busy after the 4000 ms budget or the 128-entry cap calls completed(Busy) -> "settings could not be saved" (truthful). Hard failure completes synchronously; the extra if(!accepted) sortSavePending=false is redundant but harmless.
- Empty-folder and loaded paths both go through setSortFeedback; the completion path uses the live sortSavePending/sortSaved, not a value captured at job submit (the earlier defect).

## Nits (non-blocking)
- sortFeedbackPrefix/sortFeedback are never cleared; an unrelated status that happens to equal the old feedback string would be rewritten. Very unlikely.
- The Sorting... in-progress status is not rewritten when persistence settles; the completion message reads the live state, so this is correct.

## Verification
Static review only. I did not build or run recovery_controller tests for this pin; Codex's own evidence is in the commit's verification folder. Verdict: favourable, not blocking.
