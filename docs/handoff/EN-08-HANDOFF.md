# EN-08 handoff: capability page, stale claims and proposed queue rows

Written for: the lead. Documents only; no build, test or queue edit was made. Branch `claude/en-08-docs` from `origin/main` `3893c77`, with `main` `f325ef6` merged in for review round 1. Worktree `.worktrees/claude-en-08`.

## Files changed

| File | Change |
| --- | --- |
| `docs/SUPPORTED-CAPABILITIES.md` | Rewritten per platform (Windows, Docker browser desktop, native Linux). Covers formats with Windows/Docker columns, viewing, the merged-but-uncorrected stack features, file operations, second display, safe mode and recovery, Docker specifics, and an "unverified on physical hardware" table. Every row cites code or a verification record. |
| `README.md` | Replaced "JPEG and PNG" with the actual format set. Replaced "Linux core tests run in WSL" with the full offscreen suite inside the Docker build. Narrowed "RAW … not delivered" to full RAW development. Added the file-operation keys and the 256 MiB decoder limit. Linked the current plan. |
| `docs/TESTING.md` | "Drop a JPEG/PNG" now reads "drop a supported image" with a link to the formats table. Added a note that file-changing commands should be tested on disposable copies, plus the Linux/Docker suite and the missing-`qtimageformats` caveat. |
| `docs/DOCKER.md` | "First JPEG/PNG" now reads "first supported image". Added which formats decode in the container (WebP/TIFF/TGA expected missing until EN-02; inferred, not observed), that the read-only `/photos` mount should refuse delete/rename/move (inferred from the mount, not observed), and that the container has no second-monitor output. |
| `docs/AGENT-LANES-PLAN.md` | "Superseded" banner at the top pointing to AGENTS.md and LEAD-PLAN-2026-10-22.md; body kept as history. |

Not touched: `AGENTS.md`, `docs/WORK-QUEUE.yaml`, workflows, `BUILDING.md` and `Dockerfile` (EN-02 owns the Linux section and the Qt modules).

## Proposed queue changes (ready to paste)

### 1. Eleven rows for the merged stack features

Paste these under `tasks:` directly after the `STACK-CORRECTION-01` row, before the `# Phase 0 cycle 1` comment. Do not append them at the end of the file: top-level keys (`unverified`, `theme_task`, `remaining_waves`) follow the task list. None of these IDs exists in the queue today. F002 and F037 are not repeated: they are PATH-COMPLETE-01 and VIEW-POLISH-01.

**Keys.** The rows use the keys of those two model rows (`requirements`, `milestone`, `owner`, `state`, `depends`, `outcome`, `acceptance`, `branch`, `candidate_commit`, `merged_commit`, `blocked_by`, `evidence`), plus three keys that other queue rows already use:

- `feature` (eight rows, for example PLATFORM-OPS-01);
- `gated_commit` (EXPORT-CSV-01);
- `review_evidence` (four rows, for example EXPORT-CSV-01).

No new key is introduced. Every value that looks like a SHA is quoted, because an all-digit SHA such as `2566014` would otherwise parse as an integer.

Where each value comes from:

- **`merged_commit`.** `git show 3b2b0db`, whose parents are main `d632a53` and `claude/f051` `cf40fcc`.
- **`candidate_commit`.** The branch tips in `docs/handoff/CLAUDE-STACK-SYNC-2026-10-22.md`.
- **Feature commits** (listed at the end of `evidence`). `git log --no-merges d181d4f..cf40fcc`.
- **`acceptance`.** The note column of `docs/FEATURE-OPPORTUNITIES.md` for each feature, plus the frozen corrections in `docs/handoff/LEAD-WRAPUP-2026-10-22.md` for F040, F049, F054 and F015.
- **`review_evidence`.**
  - The SHA pairs are the headers of `docs/verification/F0xx-GEMINI-REVIEW.md`.
  - The F051 review range `f7aa764..8c3189c` contains `2566014` (F015), `a504baa` (F042) and the F052/F053/F054 commits; I checked this with `git merge-base --is-ancestor`.
  - That review names F052, F053 and F054 but not F015 or F042.
  - `fb698c0` and `cf40fcc` come after the review.
- **`gated_commit`.** From `docs/verification/lead-integrate-all-2026-10-22/README.md`. The gate ran on `ec05e42`, a descendant of `3b2b0db`, not on `3b2b0db` itself, and it does not approve the corrections.
- **`requirements`.** From the "Requirement" column of `docs/FEATURE-OPPORTUNITIES.md`. F009, F015 and F054 are marked "N" (new, no requirement ID), so they get `requirements: []` plus `feature:`.
- **`depends`.** Every row depends on STACK-CORRECTION-01. The four features that need a frozen correction (LEAD-PLAN section 9) also depend on their packet: STACK-F040-PINNED, STACK-F049-PRESETS, STACK-F054-COVERAGE and STACK-F015-RECOVERY. The other seven are confirm-only, and their `blocked_by` names only STACK-CONFIRM.
- **`milestone`.** Inherited from STACK-CORRECTION-01 (M3); the lead may prefer each requirement's own milestone.

```yaml
  - id: SLIDESHOW-01
    requirements: [UX-10]
    feature: F031
    milestone: M3
    owner: lead
    state: integration
    depends: [STACK-CORRECTION-01]
    outcome: "Slideshow with start/stop and per-photo duration (F031)"
    acceptance: "Animation-aware clock and bounded prefetch; user input cancels reliably; missing files skipped with limits; an unplugged presentation monitor recovers"
    branch: "claude/f031"
    candidate_commit: "53a789b"
    merged_commit: "3b2b0db (user-directed stack merge, 2026-10-22)"
    gated_commit: "ec05e42 Windows 43/43 722.35s, Docker 43/43 411.23s; docs/verification/lead-integrate-all-2026-10-22/README.md; does not approve STACK-CORRECTION-01"
    review_evidence: "docs/verification/F031-GEMINI-REVIEW.md APPROVED 319cdf4..26f8a89 (pre-sync pin)"
    blocked_by: "STACK-CONFIRM fresh-context confirmation and a passing authoritative gate"
    evidence: "docs/F031-PLAN.md; docs/handoff/CLAUDE-STACK-SYNC-2026-10-22.md"
  - id: REFERENCE-WINDOW-01
    requirements: [UX-09]
    feature: F040
    milestone: M3
    owner: lead
    state: integration
    depends: [STACK-CORRECTION-01, STACK-F040-PINNED]
    outcome: "Pin a photo in a separate reference window (F040)"
    acceptance: "Platform capability checks with clear unsupported states; always-accessible recovery route; shares aggregate pixel/texture admission and release with MONITOR-OUTPUT pinned output as a distinct command, no duplicate decode, selected image unchanged; parity tests for close, pin replacement and recovery while both are active"
    branch: "claude/f040"
    candidate_commit: "3cb96bf"
    merged_commit: "3b2b0db (user-directed stack merge, 2026-10-22)"
    gated_commit: "ec05e42 Windows 43/43 722.35s, Docker 43/43 411.23s; docs/verification/lead-integrate-all-2026-10-22/README.md; does not approve STACK-CORRECTION-01"
    review_evidence: "docs/verification/F040-GEMINI-REVIEW.md APPROVED 26f8a89..75dd2d7 (pre-sync pin)"
    blocked_by: "STACK-F040-PINNED (shared admission with MONITOR-OUTPUT pinned output, parity tests), then STACK-CONFIRM and a passing authoritative gate"
    evidence: "docs/F040-PLAN.md; docs/handoff/CLAUDE-STACK-SYNC-2026-10-22.md finding 2"
  - id: LAYOUT-PRESETS-01
    requirements: [UX-14, UX-17]
    feature: F049
    milestone: M3
    owner: lead
    state: integration
    depends: [STACK-CORRECTION-01, STACK-F049-PRESETS]
    outcome: "One-command focus mode (claude/f049) and named layout presets (66dda8e on claude/f051) (F049)"
    acceptance: "Reset/recover hidden panes; per-window scope; no loss of selection when switching layouts; presets obey the canonical layout lock and frozen panes, keep per-slot placement and unknown/newer preference data, and have names and scopes distinct from monitor workspace presets"
    branch: "claude/f049"
    candidate_commit: "d4eb4cc"
    merged_commit: "3b2b0db (user-directed stack merge, 2026-10-22)"
    gated_commit: "ec05e42 Windows 43/43 722.35s, Docker 43/43 411.23s; docs/verification/lead-integrate-all-2026-10-22/README.md; does not approve STACK-CORRECTION-01"
    review_evidence: "docs/verification/F049-GEMINI-REVIEW.md APPROVED 75dd2d7..a1738fa (focus mode); presets inside the F051 review range f7aa764..8c3189c"
    blocked_by: "STACK-F049-PRESETS (canonical lock/freeze, unknown-data preservation, distinct from monitor workspace presets), then STACK-CONFIRM and a passing authoritative gate"
    evidence: "docs/F049-PLAN.md; feature commits 66dda8e"
  - id: DETAILS-VIEW-01
    requirements: []
    feature: F009
    milestone: M3
    owner: lead
    state: integration
    depends: [STACK-CORRECTION-01]
    outcome: "Details or thumbnails mode in the folder grid (F009); sortable persisted columns remain packet A6"
    acceptance: "Shared selection and sort with the grid; virtualized rows; changing mode preserves the visible anchor"
    branch: "claude/f009"
    candidate_commit: "f22cbfe"
    merged_commit: "3b2b0db (user-directed stack merge, 2026-10-22)"
    gated_commit: "ec05e42 Windows 43/43 722.35s, Docker 43/43 411.23s; docs/verification/lead-integrate-all-2026-10-22/README.md; does not approve STACK-CORRECTION-01"
    review_evidence: "docs/verification/F009-GEMINI-REVIEW.md APPROVED a1738fa..c266824 (pre-sync pin)"
    blocked_by: "STACK-CONFIRM fresh-context confirmation and a passing authoritative gate"
    evidence: "docs/F009-PLAN.md"
  - id: GRID-BADGES-01
    requirements: [UX-02, META-05]
    feature: F014
    milestone: M3
    owner: lead
    state: integration
    depends: [STACK-CORRECTION-01]
    outcome: "File-type and failed-preview badges on grid thumbnails (F014, 67d3f83); rating, dimension and quality badges remain packet A4"
    acceptance: "Badges readable at small sizes, never colour-only, with accessible labels, and do not overlap filenames"
    branch: "claude/f014"
    candidate_commit: "d181d4f"
    merged_commit: "3b2b0db (user-directed stack merge, 2026-10-22)"
    gated_commit: "ec05e42 Windows 43/43 722.35s, Docker 43/43 411.23s; docs/verification/lead-integrate-all-2026-10-22/README.md; does not approve STACK-CORRECTION-01"
    review_evidence: "docs/verification/F014-GEMINI-REVIEW.md APPROVED c266824..f7aa764 (pre-sync pin)"
    blocked_by: "STACK-CONFIRM fresh-context confirmation and a passing authoritative gate"
    evidence: "docs/F014-PLAN.md; feature commits 67d3f83"
  - id: SHORTCUT-RECORDER-01
    requirements: [IN-01, IN-04, IN-05]
    feature: F051
    milestone: M3
    owner: lead
    state: integration
    depends: [STACK-CORRECTION-01]
    outcome: "Shortcut recorder with chords and conflict explanations in Settings (F051)"
    acceptance: "Keyboard-layout/IME and scope tests; unavailable OS shortcuts explained; no destructive command capture leaks"
    branch: "claude/f051"
    candidate_commit: "cf40fcc"
    merged_commit: "3b2b0db (user-directed stack merge, 2026-10-22)"
    gated_commit: "ec05e42 Windows 43/43 722.35s, Docker 43/43 411.23s; docs/verification/lead-integrate-all-2026-10-22/README.md; does not approve STACK-CORRECTION-01"
    review_evidence: "docs/verification/F051-GEMINI-REVIEW.md APPROVED f7aa764..8c3189c; cf40fcc (test key F8 to F7) is after the review"
    blocked_by: "STACK-CONFIRM fresh-context confirmation and a passing authoritative gate"
    evidence: "docs/F051-PLAN.md; feature commits ed12ebe, cf40fcc"
  - id: A11Y-AUDIT-01
    requirements: [UX-11]
    feature: F052
    milestone: M3
    owner: lead
    state: integration
    depends: [STACK-CORRECTION-01]
    outcome: "Accessibility audit test for every interactive control (F052, f597012); NVDA/Orca trials remain packet E6 RUNBOOK"
    acceptance: "Logical focus restore, semantic selection, accessible names and restrained live status for path, tree, grid and settings; NVDA/Orca trials by RUNBOOK"
    branch: "claude/f051"
    candidate_commit: "cf40fcc"
    merged_commit: "3b2b0db (user-directed stack merge, 2026-10-22)"
    gated_commit: "ec05e42 Windows 43/43 722.35s, Docker 43/43 411.23s; docs/verification/lead-integrate-all-2026-10-22/README.md; does not approve STACK-CORRECTION-01"
    review_evidence: "docs/verification/F051-GEMINI-REVIEW.md APPROVED f7aa764..8c3189c; review names F052"
    blocked_by: "STACK-CONFIRM fresh-context confirmation and a passing authoritative gate"
    evidence: "docs/F052-PLAN.md; feature commits f597012"
  - id: CONTRAST-TARGETS-01
    requirements: [UX-11, UX-17]
    feature: F053
    milestone: M3
    owner: lead
    state: integration
    depends: [STACK-CORRECTION-01]
    outcome: "High-contrast palette and 44 px large targets (F053, e1c1c51); native high-contrast detection remains packet E6"
    acceptance: "200% text, long translated labels, no colour-only state, supported narrow layouts and no hover-only essential actions"
    branch: "claude/f051"
    candidate_commit: "cf40fcc"
    merged_commit: "3b2b0db (user-directed stack merge, 2026-10-22)"
    gated_commit: "ec05e42 Windows 43/43 722.35s, Docker 43/43 411.23s; docs/verification/lead-integrate-all-2026-10-22/README.md; does not approve STACK-CORRECTION-01"
    review_evidence: "docs/verification/F051-GEMINI-REVIEW.md APPROVED f7aa764..8c3189c; review names F053"
    blocked_by: "STACK-CONFIRM fresh-context confirmation and a passing authoritative gate"
    evidence: "docs/F053-PLAN.md; feature commits e1c1c51"
  - id: I18N-01
    requirements: []
    feature: F054
    milestone: M3
    owner: lead
    state: integration
    depends: [STACK-CORRECTION-01, STACK-F054-COVERAGE]
    outcome: "Pseudo-locale catalog, qsTr wrapping and right-to-left mirroring (F054, 209e703 through 8c3189c)"
    acceptance: "Paths stay exact; traversal comparator policy stays deterministic; bidirectional text and plural counts tested; main's newer strings and detached windows covered with pseudo-locale, clipping and RTL evidence"
    branch: "claude/f051"
    candidate_commit: "cf40fcc"
    merged_commit: "3b2b0db (user-directed stack merge, 2026-10-22)"
    gated_commit: "ec05e42 Windows 43/43 722.35s, Docker 43/43 411.23s; docs/verification/lead-integrate-all-2026-10-22/README.md; does not approve STACK-CORRECTION-01"
    review_evidence: "docs/verification/F051-GEMINI-REVIEW.md APPROVED f7aa764..8c3189c; review names F054"
    blocked_by: "STACK-F054-COVERAGE (main's newer strings, detached-window RTL, pseudo-locale/clipping/RTL evidence), then STACK-CONFIRM and a passing authoritative gate"
    evidence: "docs/F054-PLAN.md; docs/verification/F054-pseudo-locale.png; docs/handoff/CLAUDE-STACK-SYNC-2026-10-22.md finding 4; feature commits 209e703, 3d771f4, 1482d7e, de95624, 06f09ae, 776fa1e, c054973, 853ab3c, fb05f5f, e32b378, 3e882fb, 4990f0c, 64fe012, 8c3189c"
  - id: TABS-01
    requirements: []
    feature: F015
    milestone: M3
    owner: lead
    state: integration
    depends: [STACK-CORRECTION-01, STACK-F015-RECOVERY]
    outcome: "Tabs as saved locations with a top-bar tab strip (F015, shape A)"
    acceptance: "Closing a tab releases work; restore lazily; reopening does not multiply memory limits; tab state persists through the production recovery checkpoint (at most 8 records plus active index, legacy read, no auto-restore under SAFE/reset)"
    branch: "claude/f051"
    candidate_commit: "cf40fcc"
    merged_commit: "3b2b0db (user-directed stack merge, 2026-10-22)"
    gated_commit: "ec05e42 Windows 43/43 722.35s, Docker 43/43 411.23s; docs/verification/lead-integrate-all-2026-10-22/README.md; does not approve STACK-CORRECTION-01"
    review_evidence: "2566014 lies inside the F051 review range f7aa764..8c3189c, but docs/verification/F051-GEMINI-REVIEW.md does not name F015; fb698c0 is after the review. Treat as unreviewed."
    blocked_by: "STACK-F015-RECOVERY (tabs through the production recovery checkpoint), then STACK-CONFIRM and a passing authoritative gate"
    evidence: "docs/F015-PLAN.md; docs/handoff/CLAUDE-STACK-SYNC-2026-10-22.md finding 1; feature commits 2566014, fb698c0"
  - id: TYPE-CHIPS-01
    requirements: [NAV-05, NAV-06]
    feature: F042
    milestone: M3
    owner: lead
    state: integration
    depends: [STACK-CORRECTION-01]
    outcome: "File-type filter chips in the grid header (F042 chips only, a504baa); capture-date/dimension sorts and rating/orientation/date chips remain packets A2 and A4"
    acceptance: "No metadata read per QML delegate; chip counts and filtering stay consistent with the grid model; missing data handled by a defined policy"
    branch: "claude/f051"
    candidate_commit: "cf40fcc"
    merged_commit: "3b2b0db (user-directed stack merge, 2026-10-22)"
    gated_commit: "ec05e42 Windows 43/43 722.35s, Docker 43/43 411.23s; docs/verification/lead-integrate-all-2026-10-22/README.md; does not approve STACK-CORRECTION-01"
    review_evidence: "a504baa lies inside the F051 review range f7aa764..8c3189c, but docs/verification/F051-GEMINI-REVIEW.md does not name F042. Treat as unreviewed."
    blocked_by: "STACK-CONFIRM fresh-context confirmation and a passing authoritative gate"
    evidence: "docs/F013-F042-UI-PLAN.md; feature commits a504baa"
```

STACK-CORRECTION-01's existing `covers:` list already names these features, so it needs no new key. Its `depends` is the link from each new row back to it.

### 2. COPY-MOVE-01: add `feature`, `commit` and `integrated_evidence`

```yaml
    feature: F020
    commit: b13b73941d9420ac2740bdb58aefe9132540daa0
    integrated_evidence: "Integration merge b13b739 (parents 44d9cf2, 079564b), closed by a78b930. Pre-merge: source e4a5a07 Windows scripts/build.ps1 -Test 28/28 (200.80s) and Docker 28/28 (116.69s, recorded in 44d9cf2; run commit not stated), docs/verification/COPY-MOVE-01.md. No post-integration check of b13b739 itself. Nearest recorded full gate on a descendant: 259d337 Windows 29/29 (220.69s), docs/verification/MAIN-259d337-CLAUDE-VERIFY.md (git merge-base --is-ancestor b13b739 259d337 succeeds); its Docker test count was not recorded (unverified)."
```

Sources: `git log --grep copy-move` (`c304b54` freeze, `e4a5a07` source, `a800951` lock, `079564b` main sync, `44d9cf2` Docker evidence, `b13b739` merge, `a78b930` close). The test counts come from `docs/verification/COPY-MOVE-01.md` lines 121–128.

### 3. FORMAT-02: add `commit`, `integrated_evidence` and, optionally, `feature`

```yaml
    feature: F035
    commit: 362c2814f520dab2516836f842cbbf8c90defffe
    integrated_evidence: "Integration merge 362c281 (parents 9dccd78, 694ac2f), closed by 259d337 (queue-only child). Pre-merge: source 5e05c6f Windows 29/29 (210.32s) and Docker 29/29 (115.44s, recorded in 9dccd78), docs/verification/FORMAT-02.md. Post-integration: 259d337 Windows scripts/build.ps1 -Test 29/29 (220.69s), docs/verification/MAIN-259d337-CLAUDE-VERIFY.md; Docker exit status recorded only in an unnamed handoff, test count unverified. Docker lacks qtimageformats, so WebP/TIFF/TGA decode and testMultipageTiffNavigation differ there until EN-02."
```

Sources: `git log --grep format` (`2b0eec1` freeze, `5e05c6f` source, `881b0c4` lock, `694ac2f` main sync, `9dccd78` Docker evidence, `362c281` merge, `259d337` close). `git show --stat 259d337` touches only `docs/WORK-QUEUE.yaml`. The commit messages for `5e05c6f` and `362c281` name F035.

### 4. RENAME-01: add `feature`

```yaml
    feature: F018
```

The existing row already lists `F018` under `requirements`, and the integration commit `d1e5461` message ends "(F018)".

### 5. Header and EN-08 row (optional)

- **`lanes:` line.** Line 5, `lanes: docs/AGENT-LANES-PLAN.md`, points at a superseded document. Suggest replacing it with `lanes: "retired October 22, 2026 (history: docs/AGENT-LANES-PLAN.md); operating model AGENTS.md"`.
- **EN-08 `progress` line.** Append: `capability page, README/TESTING/DOCKER stale claims and AGENT-LANES-PLAN banner on claude/en-08-docs; queue proposals in docs/handoff/EN-08-HANDOFF.md`.

## Checks run

- **Proposed YAML parses.** Each block above, indented under a `tasks:` list or a mapping, parses with a PyYAML 6.0.3 loader that rejects duplicate keys.
- **Queue still parses after the paste.** I inserted block 1 into a copy of `docs/WORK-QUEUE.yaml` at the paste point above and parsed it with the same strict loader: 85 tasks (74 before, on `f325ef6`), no duplicate IDs. The current queue also parses strictly: 74 tasks, no duplicates.
- **IDs are new.** None of the eleven new IDs appears in `docs/WORK-QUEUE.yaml`, and none repeats within the block.
- **Commits verified.**
  - All 55 distinct hex tokens in this file resolve to commits with `git rev-parse --verify`.
  - Every stack SHA used in the rows is an ancestor of `3b2b0db`, checked with `git merge-base --is-ancestor`.
  - `3b2b0db` is an ancestor of `ec05e42`.
  - `b13b739` is an ancestor of `259d337`.
- **Stale claims gone.** No "JPEG/PNG", "JPEG and PNG", "WSL" or "core only" claim remains in `README.md`, `docs/TESTING.md`, `docs/DOCKER.md` or `docs/SUPPORTED-CAPABILITIES.md`.

## Not verified, or left for the lead

- **Fresh review.** Round 1 returned CHANGES REQUIRED (`docs/verification/EN-08-REVIEW.md` on `claude/review-en-08`, `1401966`). Round 1 fixes are below; a fresh re-review is owed.
- **AGENTS.md lane rules (LEAD-PLAN EN-08 scope).** On the coordinator's instruction, the banner on `docs/AGENT-LANES-PLAN.md` stays as written: AGENTS.md on `main` already calls the lanes plan history. Any further AGENTS.md wording is lead work.
- **Docker formats not observed.** That WebP, TIFF and TGA fail in Docker is inferred from the `Dockerfile` (no `qtimageformats`) and LEAD-PLAN section 2. No container run of those files was observed.
- **Windows format decoding untested.** WebP, TGA and APNG decoding on Windows has no fixture test. APNG animation and animated WebP are marked unverified.
- **Native Linux package formats.** It is unknown whether the package's Qt image (`glancelot-qt:codex-perf-measure` in `tools/platform/Dockerfile.package`) includes `qtimageformats`.
- **ICO and WBMP.** They are listed as not openable because their extensions are not on the navigation allowlist. Opening one directly by path is untested.
- **Stale claims in code.** These are out of scope for a docs packet and are for a code packet:
  - `app/startup_intent.cpp:25`: the `--help` description still says "JPEG/PNG alpha."
  - `src/worker/main.cpp:232`: the unsupported-format message lists WebP/TIFF/TGA even in builds without those plugins, and it is also shown for damaged files (AUDIT-UX major 2).
- **Trash on a writable mount.** Where a read-write Docker mount's Trash goes is unverified.

## Review round 1

Review: `docs/verification/EN-08-REVIEW.md` at `1401966` (branch `claude/review-en-08`), verdict CHANGES REQUIRED. I merged `origin/main` `f325ef6` into the branch first, then re-checked the page against it.

| Finding | Fix |
| --- | --- |
| 1. Paste has no collision choice and no confirmation (blocking) | Removed "collision choice" and the blanket "confirmation" claims from the capability page and the README. Both now say: delete and rename confirm; paste runs at once; a name clash always keeps both files, the new copy gets a numbered name, and the existing file is untouched. Sources: `CopyMoveService::pasteTo` with `CollisionPolicy::RenameBoth`, and `ui/Main.qml` `paste`. |
| 2. The self-hosted gate has `qtimageformats` (blocking) | The Windows bullet now says the self-hosted gate uses the main checkout's local Qt, which includes the plugins. A new bullet names the hosted advisory job, which installs only `qtbase`, `qtdeclarative` and `qtshadertools`, as the one without them. EN-02 was noted as adding the plugins there too; corrected in round 2. |
| 3. Docker resume after container recreation (blocking) | Viewing row and Docker specifics: the position is stored in the volume; resume after recreating the container is unverified, citing DOCKER-BROWSER.md's "not verified" list. |
| 4. Re-check against `f325ef6` (blocking) | "Last checked" is now `f325ef6`. File operations has a COPY-SAFETY-01 known-issue note: the Overwrite path truncates the target and deletes it on failure; no UI reaches it; the fix is queued. The cross-volume move "verify" is now described as a size comparison only. |
| 5. Banner versus AGENTS.md | Kept the banner, on the coordinator's instruction. AGENTS.md on `main` already treats the lanes plan as history. |
| 6. Queue keys | Block 1 rewritten. The rows now use only keys that already exist in the queue, add `acceptance` per row, and replace `covered_by`, `confirm_only`, `corrected_by` and `feature_commits` with `depends`, `blocked_by` and `evidence`. All SHA-like values are quoted: the old block parsed `2566014` as an integer. |
| 7. Stack states | The section is now headed "Merged, corrections outstanding". It no longer claims a queue state these features lack, and it says the four STACK-F0xx packets are in `review` on `f325ef6`. |
| 8. "Unchanged code" | Now reads "unchanged product and test code". |
| 9. Inferred Docker behaviour | Marked "inferred, not observed" in the capability page (damaged-file message, read-only refusals, WebP/TIFF/TGA), DOCKER.md and TESTING.md. |
| 10. "In-app comparison" | The README now names what is missing: two-up/four-up comparison in the main window. It notes that the second display has a comparison mode. |
| 11. TESTING.md list | Added new folder, and noted that paste runs without confirmation. |
| 12. Safe mode wording | Safe mode "requests" a software rendering device. |
| 13. WebP and TGA | The Windows column now reads "Expected (plugin present); no decode test". The README qualifies them as "without decode tests yet". |
| 14. PERF-RENDER | Information only; no change. |

Checks after round 1:

- **Strict parse.** The strict duplicate-key parse of every block passes.
- **Queue still valid.** Block 1 inserted into the `f325ef6` queue gives 85 tasks with no duplicate IDs.
- **Values are strings.** No proposed row value is a non-string.

## Pre-handoff checklist (AGENTS.md)

These are the items that apply to a documents-only packet.

### 1. Fail-before evidence

Not applicable: no assertion or test was added or changed.

### 2. Grant check

The packet grants documents. It excludes `AGENTS.md`, `docs/WORK-QUEUE.yaml`, workflows, and also `BUILDING.md` and `Dockerfile`, which EN-02 owns.

The branch's own changes, from the merge base with `main`:

```
> git diff --name-only origin/main...HEAD
README.md
docs/AGENT-LANES-PLAN.md
docs/DOCKER.md
docs/SUPPORTED-CAPABILITIES.md
docs/TESTING.md
docs/handoff/EN-08-HANDOFF.md
```

Nothing falls outside the grant.

`git diff --name-only 3893c77..HEAD` additionally lists `AGENTS.md`, `docs/WORK-QUEUE.yaml` and `docs/handoff/PHASE0-CYCLE1-2026-10-22.md`. Those changes arrive only through the merges of `main` (`f325ef6`, then `4f29124`); this branch makes no edit of its own to them.

### 3. Exact base

- **Cut from:** `origin/main` `3893c77ef` (`3893c77`).
- **Main merged in:** `f325ef6` for round 1 and `4f29124` for round 2.
- **Merge base with `origin/main` now:** `4f2912476defde4f549a53c7c1dde6581c8a3539`.
- **Stacking:** not a stacked packet.

### 4. Tests

Not applicable: documents only, so no build or `ctest` was run. The checks actually run are the strict duplicate-key YAML parse and the SHA and ancestry checks, listed under "Checks run".

### 5. Decisions taken

- **Docker format claims are inferred.** Docker's missing WebP, TIFF and TGA are reported as inferred from the Dockerfile, not as observed. The alternative was running a container, which is out of scope for a docs packet.
- **ICO and WBMP are listed as not openable.** The decoder accepts them, but the navigation allowlist does not include their extensions, so the page does not list them as supported.
- **Stack features have their own section.** The thirteen merged stack features are labelled "Merged, corrections outstanding" rather than supported, and the page does not quote a queue state they lack.
- **Proposed queue rows use existing keys only.** `feature`, `gated_commit` and `review_evidence` each already appear on other rows. All SHA values are quoted.
- **BUILDING.md and the Dockerfile are left alone.** EN-02 owns them.

### 6. Open questions for the lead

- **Queue rows.** Apply the proposed rows (blocks 1 to 4). Then decide whether `lanes:` on line 5 should be repointed.
- **Hosted advisory job.** Add `qtimageformats` to it under EN-01 when EN-02 is integrated. The capability page now says this will happen.
- **Code packet for stale strings.** Two strings in code are stale: `app/startup_intent.cpp:25` ("JPEG/PNG alpha") and the unsupported-format message at `src/worker/main.cpp:232`. They need a code packet.

## Review round 2

Review: `docs/verification/EN-08-REVIEW-R2.md` at `41a65ec` (branch `claude/review-en-08-r2`), verdict CHANGES REQUIRED. I merged `origin/main` `4f29124` (docs only) first.

| Finding | Fix |
| --- | --- |
| 1. EN-02 does not add `qtimageformats` to the hosted job (blocking) | The page now says EN-02 adds it to Docker, and the lead adds it to the hosted advisory job under EN-01 once EN-02 is integrated. The round-1 table row 2 is annotated as corrected. |
| 2. "Window positions" | Both places now say "each window's last folder and photo", per `docs/verification/DOCKER-BROWSER.md`. |
| 3. New folder missing from the intro | The "Confirmed first" bullet now reads "delete, rename and new folder open a dialog". |
| 4. Stale DOCKER.md row in "Files changed" | Aligned: the formats and read-only claims are marked inferred, not observed. |
| 5. Pre-handoff checklist | Added the applicable items above: grant check output, exact base, Decisions taken, Open questions for the lead. |
