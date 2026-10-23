# Lead pause, October 23, 2026

Written for: the lead (or a new lead session) resuming the work, and the user. This is the resume point named by `pause_now` in docs/WORK-QUEUE.yaml.

The user asked to pause once the running tasks finished. They have: nothing is running, no session is linked, `integration_lock` is null and the gate lock is free. Phase 1 is not complete. The plan is still docs/handoff/LEAD-PLAN-2026-10-22.md; `pause_after_phase1` still applies after the resume (finish Phase 1, then pause again before any Phase 2 contract freeze or EN-07).

## 1. On `main` (7711988)

| Packet | Integrated in | Evidence |
| --- | --- | --- |
| EN-02, EN-03, EN-06, EN-08 | cycle1-b | docs/verification/integrate-cycle1-b-2026-10-23/ |
| EN-04, STACK-F054-COVERAGE part 1 | cycle1-c2 | docs/verification/integrate-cycle1-c2-2026-10-23/ |
| EN-05, EN-09, lead lock scripts, inbox fix | cycle1-a and lead merges | docs/verification/integrate-cycle1-a-2026-10-22/ |
| EN-11 (gate stability) | cycle1-d, merge 72d5de5 | docs/verification/integrate-cycle1-d-2026-10-23/: Windows 46/46 and Docker 46/46 on 5908284, first run, no reruns |

EN-11 is in `integration`, gate 1 of 3. Its acceptance needs two more consecutive full gates with folder_tree, folder_grid and fs_watch passing; the next two integration gates count. A failure in those three suites resets the count and reopens EN-11.

## 2. Packets in flight, at the pause

| Packet | Branch and tip | State | Next step |
| --- | --- | --- | --- |
| STACK-F040-PINNED | `claude/stack-f040-pinned` b60b65f (round 3, merges main b9dc0ec) | fixes pushed | fresh round 3 review (Windows targeted, includes `reference_pin_native`) |
| STACK-F015-RECOVERY | `claude/stack-f015-recovery` b498e09 (round 3, merges main b9dc0ec) | fixes pushed | fresh round 3 review |
| STACK-F049-PRESETS follow-ups + STACK-F054-COVERAGE part 2 | `claude/stack-f054-part2` 98adda9 (F049 follow-ups end at 04aef7c, F054 part 2 from e1d584b) | implemented | one fresh review of both packets; integrate as one unit |
| COPY-SAFETY-01 | `claude/copy-safety-01` 4005869 | APPROVED (`claude/review-copy-safety-01-r2` 2f6ac32, docs/verification/COPY-SAFETY-01-REVIEW-R2.md) | N2 one-line fix, then integrate |

Round 3 handoffs are on each branch (`docs/handoff/<TASK>-HANDOFF.md`; F049 follow-ups and F054 part 2 have `STACK-F049-FOLLOWUP-HANDOFF.md` and `STACK-F054-PART2-HANDOFF.md`). Read the "Decisions taken" and "Open questions" sections before briefing the reviewers.

### Points each reviewer and the integrator must know

- **F040.**
  - Behaviour change: an output pin that does not fit is now refused and the old pin stays, instead of the output being unpinned. An output pin may shed the reference, with a notice.
  - The test helper `step()` opens files directly instead of pressing "next"; see the navigation stall in section 3.
  - Lead request in the handoff: reword the F040 comment in `include/glancelot/viewer_controller.hpp`, which now overstates "no memory beyond the one image".
- **F015.**
  - B2 fixed: a tab's saved location changes only from its own folder or the user navigating in it.
  - Decision (b): a failed folder clears the photo and marks the tab unavailable, and an empty-state placeholder in `ui/Main.qml` shows the folder name and Retry (same command as F5).
  - Not done: the placeholder was never rendered on screen; the en_XA catalog was not regenerated for its strings; with one remaining tab whose folder is unavailable there is no placeholder, because the frozen header keeps `tabs()` empty at one tab (covering it needs a header property, a lead decision).
  - `folder_grid` failed once in a sort race during its run and passed on rerun; record it if it recurs.
- **F040 and F015 both touch `cmake/ReferencePinTests.cmake`.** F015 added `${GLANCELOT_VIEWER_CONTEXT_SOURCES}` to `reference_pin`; F040 added labels and `reference_pin_native`. Expect a conflict when both are batched; keep both.
- **F049 follow-ups + F054 part 2.**
  - The R1 test seam `settings_detail::attemptPendingSettingsNow` is outside F049's grant (`src/app/settings_retry.hpp`, `src/app/settings_file.cpp`); the lead accepts or moves it at integration.
  - `ui/Main.qml` has two non-string edits: a `withShortcut()` helper and an `orderName` property on the sort button.
  - The F049 follow-up commit alone fails `translation_coverage_ui`; only the branch tip passes. Integrate the branch as a whole.
  - Follow-ups left for later: PhotoGrid strings, `DeleteFlow.trashName`, and plain C++ literals in `pane_layout.cpp`.
- **COPY-SAFETY-01** (review N1/N2 and the smaller notes are in the review document).
  - N2 (fix before integration): after 8 backup-name collisions the code reuses an existing name, and `ReplaceFileW` overwrote an existing backup on NTFS and exFAT. The fix is to refuse instead.
  - N1 (data-safety decision for the lead): `ReplaceFileW` is not atomic. A crash inside it, or an unrecoverable error 1177, can leave the target name missing, with the original saved beside it as a hidden `.~gb*.tmp` backup. Recommended: accept for COPY-SAFETY-01 and add startup recovery of those backups to B2 (OPERATION-JOURNAL), recorded in B2's acceptance at the contract freeze.
  - Accept the lead-owned `CMakeLists.txt` line and `cmake/CopySafetyTests.cmake`.
  - Unverified: Windows file symlinks, folder-mounted volumes, a real SMB server, a real 1177.

## 3. Open issue to check first: "next" left queued

The F040 fixer saw `ViewerController::next()`, pressed twice, leave a navigation intent queued with nothing running ("step(2) timed out: index 1 ready 1 loading 0"), after merging main b9dc0ec. `navigationIntents` drains only when a navigation finishes. b9dc0ec predates EN-11, so EN-11 is not the cause; EN-06 command routing, or F040's change interacting with it, is the likely area.

If this reproduces on `main`, Next can silently do nothing for the user. On resume, before the next integration: reproduce on `main` with a deterministic test (two quick `next()` calls in the reference_pin harness, or the acceptance harness), then fix it as its own packet or fold it into F040 if F040 causes it.

## 4. Remaining Phase 1 steps, in order

1. Check the "next" stall (section 3).
2. COPY-SAFETY-01 N2 fix, then lead decision on N1.
3. Fresh reviews: F040 round 3, F015 round 3, F049 follow-ups + F054 part 2 (one review). Local reviewers, at most three local builds at once.
4. Integration batches through the local tester (full Windows plus Docker gate on the exact commit). Suggested: COPY-SAFETY-01 with F049+F054 part 2 first (no shared files), then F040 with F015 (shared CMake file). Each passing gate also counts toward EN-11's three.
5. STACK-CONFIRM, then the Phase 1 closeout record. Then pause again (`pause_after_phase1`).

## 5. Housekeeping (EN-10) and lead tails

- **Worktrees.** 83 worktrees exist. Most review and fix worktrees for merged packets can go: `review-en-*`, `review-f0*`, `review-lead-scripts*`, `claude-en-0*`, `claude-en-11`, `lead-inbox-fix`, `lead-scripts`, and older lane worktrees. Keep `lead-tester`, `lead-contracts-a`, `lead-contracts-b`, and the worktrees of the four in-flight packets. Remove only worktrees with a clean status and no unpushed commits.
- **Left by the COPY-SAFETY-01 reviewer:** `.worktrees\review-copy-safety-r2`, `.worktrees\review-cs2-doc`, and in WSL: apt packages installed as root, `~/Qt`, `~/gl-cs`.
- **Remote branches.** Merged packet and review branches can be deleted after their evidence is on `main`. `claude/en-02-cloud-proof` (5472e32) is unmerged: check whether its handoff update is wanted.
- **`scripts/lead-inbox.ps1` quirks.**
  - It exits 1 after printing a full table.
  - It reads the first verdict in a review file, so a later round's verdict can be missed.
  - Its F054 part 2 row shows task `F054`.
  - `claude/review-copy-safety-01` shows "no verdict line".
- **Queue `branch:` fields.** Add them for EN-08 (marked `*` in the inbox) and for the F054 part 2 branch, so the inbox matches the rows exactly.
- **EN-01 tail.** Still `implementing`: one recorded `lead-windows-gate` run on `main` with duration, and a check that `gate/*` pushes start only the gate workflow.
- **AGENTS.md "Integration (lead only)".** It still describes the `gate/*` CI flow only. Update it to the local-tester flow used in cycles 1-b to 1-d, with the CI gate as the backup.

## 6. Needs the user

- **Cloud sessions cannot download Qt.** Adding download.qt.io to the environment's allowed domains did not take effect: session S12 still got HTTP 403. The likely fix is to set the cloud environment's network access to "Limited" (with download.qt.io allowed) rather than a custom list that the proxy ignores. Until then cloud sessions use the documented conda-forge Qt fallback (docs/handoff/PHASE0-CYCLE1-2026-10-22.md), which has known baseline failures.
- **Resume.** Say when to resume; the lead starts at section 3.

## 7. Held until the user resumes after Phase 1

Phase 2 contract freeze (drafts on `claude/contracts-draft-a` 25e63a9 and `claude/contracts-draft-b` b1c835d), EN-07, and all Phase 2 track packets.
