# EN-08 handoff: capability page, stale claims and proposed queue rows

Written for: the lead. Documents only; no build, test or queue edit was made. Branch `claude/en-08-docs` from `origin/main` `3893c77`, worktree `.worktrees/claude-en-08`.

## Files changed

| File | Change |
| --- | --- |
| `docs/SUPPORTED-CAPABILITIES.md` | Rewritten per platform (Windows, Docker browser desktop, native Linux). Covers formats with Windows/Docker columns, viewing, the merged-but-uncorrected stack features, file operations, second display, safe mode and recovery, Docker specifics, and an "unverified on physical hardware" table. Every row cites code or a verification record. |
| `README.md` | Replaced "JPEG and PNG" with the actual format set. Replaced "Linux core tests run in WSL" with the full offscreen suite inside the Docker build. Narrowed "RAW … not delivered" to full RAW development. Added the file-operation keys and the 256 MiB decoder limit. Linked the current plan. |
| `docs/TESTING.md` | "Drop a JPEG/PNG" now reads "drop a supported image" with a link to the formats table. Added a note that file-changing commands should be tested on disposable copies, plus the Linux/Docker suite and the missing-`qtimageformats` caveat. |
| `docs/DOCKER.md` | "First JPEG/PNG" now reads "first supported image". Added which formats decode in the container (no WebP/TIFF/TGA until EN-02), that the read-only `/photos` mount refuses delete/rename/move, and that the container has no second-monitor output. |
| `docs/AGENT-LANES-PLAN.md` | "Superseded" banner at the top pointing to AGENTS.md and LEAD-PLAN-2026-10-22.md; body kept as history. |

Not touched: `AGENTS.md`, `docs/WORK-QUEUE.yaml`, workflows, `BUILDING.md` and `Dockerfile` (EN-02 owns the Linux section and the Qt modules).

## Proposed queue changes (ready to paste)

### 1. Eleven rows for the merged stack features

Paste these under `tasks:` directly after the `STACK-CORRECTION-01` row, before the `# Phase 0 cycle 1` comment. Do not append them at the end of the file: top-level keys (`unverified`, `theme_task`, `remaining_waves`) follow the task list. None of these IDs exists in the queue today. F002 and F037 are not repeated: they are PATH-COMPLETE-01 and VIEW-POLISH-01. The field names mirror those two rows.

Where each value comes from:

- **`merged_commit`.** `git show 3b2b0db`, whose parents are main `d632a53` and `claude/f051` `cf40fcc`.
- **`candidate_commit`.** The branch tips in `docs/handoff/CLAUDE-STACK-SYNC-2026-10-22.md`.
- **Feature commits.** `git log --no-merges d181d4f..cf40fcc`.
- **`review_evidence`.**
  - The SHA pairs are the headers of `docs/verification/F0xx-GEMINI-REVIEW.md`.
  - The F051 review range `f7aa764..8c3189c` contains `2566014` (F015), `a504baa` (F042) and the F052/F053/F054 commits; I checked this with `git merge-base --is-ancestor`.
  - That review names F052, F053 and F054 but not F015 or F042.
  - `fb698c0` and `cf40fcc` come after the review.
- **`gated_commit`.** From `docs/verification/lead-integrate-all-2026-10-22/README.md`. The gate ran on `ec05e42`, a descendant of `3b2b0db`, not on `3b2b0db` itself, and it does not approve the corrections.
- **`requirements`.** From the "Requirement" column of `docs/FEATURE-OPPORTUNITIES.md`. F009, F015 and F054 are marked "N" (new, no requirement ID), so they get `requirements: []` plus `feature:`.
- **`corrected_by` versus `confirm_only`.** From `docs/handoff/LEAD-PLAN-2026-10-22.md` section 9.
- **`milestone`.** Inherited from STACK-CORRECTION-01 (M3); the lead may prefer each requirement's own milestone.

```yaml
  - id: SLIDESHOW-01
    feature: F031
    requirements: [UX-10]
    milestone: M3
    owner: lead
    state: integration
    covered_by: STACK-CORRECTION-01
    confirm_only: true
    outcome: Slideshow with start/stop and per-photo duration (F031)
    branch: claude/f031
    candidate_commit: 53a789b
    merged_commit: "3b2b0db (user-directed stack merge, 2026-10-22)"
    gated_commit: "ec05e42 Windows 43/43 722.35s, Docker 43/43 411.23s; docs/verification/lead-integrate-all-2026-10-22/README.md; does not approve STACK-CORRECTION-01"
    review_evidence: "docs/verification/F031-GEMINI-REVIEW.md APPROVED 319cdf4..26f8a89 (pre-sync pin)"
    blocked_by: "STACK-CONFIRM fresh-context confirmation and a passing authoritative gate"
    evidence: docs/F031-PLAN.md; docs/handoff/CLAUDE-STACK-SYNC-2026-10-22.md
  - id: REFERENCE-WINDOW-01
    feature: F040
    requirements: [UX-09]
    milestone: M3
    owner: lead
    state: integration
    covered_by: STACK-CORRECTION-01
    corrected_by: STACK-F040-PINNED
    outcome: Pin a photo in a separate reference window (F040)
    branch: claude/f040
    candidate_commit: 3cb96bf
    merged_commit: "3b2b0db (user-directed stack merge, 2026-10-22)"
    gated_commit: "ec05e42 Windows 43/43 722.35s, Docker 43/43 411.23s; docs/verification/lead-integrate-all-2026-10-22/README.md; does not approve STACK-CORRECTION-01"
    review_evidence: "docs/verification/F040-GEMINI-REVIEW.md APPROVED 26f8a89..75dd2d7 (pre-sync pin)"
    blocked_by: "STACK-F040-PINNED (shared admission with MONITOR-OUTPUT pinned output, parity tests), then STACK-CONFIRM and a passing authoritative gate"
    evidence: docs/F040-PLAN.md; docs/handoff/CLAUDE-STACK-SYNC-2026-10-22.md finding 2
  - id: LAYOUT-PRESETS-01
    feature: F049
    requirements: [UX-14, UX-17]
    milestone: M3
    owner: lead
    state: integration
    covered_by: STACK-CORRECTION-01
    corrected_by: STACK-F049-PRESETS
    outcome: One-command focus mode (claude/f049) and named layout presets (66dda8e on claude/f051) (F049)
    branch: claude/f049
    candidate_commit: d4eb4cc
    feature_commits: [66dda8e]
    merged_commit: "3b2b0db (user-directed stack merge, 2026-10-22)"
    gated_commit: "ec05e42 Windows 43/43 722.35s, Docker 43/43 411.23s; docs/verification/lead-integrate-all-2026-10-22/README.md; does not approve STACK-CORRECTION-01"
    review_evidence: "docs/verification/F049-GEMINI-REVIEW.md APPROVED 75dd2d7..a1738fa (focus mode); presets inside the F051 review range f7aa764..8c3189c"
    blocked_by: "STACK-F049-PRESETS (canonical lock/freeze, unknown-data preservation, distinct from monitor workspace presets), then STACK-CONFIRM and a passing authoritative gate"
    evidence: docs/F049-PLAN.md
  - id: DETAILS-VIEW-01
    feature: F009
    requirements: []
    milestone: M3
    owner: lead
    state: integration
    covered_by: STACK-CORRECTION-01
    confirm_only: true
    outcome: Details or thumbnails mode in the folder grid (F009); sortable persisted columns remain packet A6
    branch: claude/f009
    candidate_commit: f22cbfe
    merged_commit: "3b2b0db (user-directed stack merge, 2026-10-22)"
    gated_commit: "ec05e42 Windows 43/43 722.35s, Docker 43/43 411.23s; docs/verification/lead-integrate-all-2026-10-22/README.md; does not approve STACK-CORRECTION-01"
    review_evidence: "docs/verification/F009-GEMINI-REVIEW.md APPROVED a1738fa..c266824 (pre-sync pin)"
    blocked_by: "STACK-CONFIRM fresh-context confirmation and a passing authoritative gate"
    evidence: docs/F009-PLAN.md
  - id: GRID-BADGES-01
    feature: F014
    requirements: [UX-02, META-05]
    milestone: M3
    owner: lead
    state: integration
    covered_by: STACK-CORRECTION-01
    confirm_only: true
    outcome: File-type and failed-preview badges on grid thumbnails (F014, 67d3f83); rating, dimension and quality badges remain packet A4
    branch: claude/f014
    candidate_commit: d181d4f
    feature_commits: [67d3f83]
    merged_commit: "3b2b0db (user-directed stack merge, 2026-10-22)"
    gated_commit: "ec05e42 Windows 43/43 722.35s, Docker 43/43 411.23s; docs/verification/lead-integrate-all-2026-10-22/README.md; does not approve STACK-CORRECTION-01"
    review_evidence: "docs/verification/F014-GEMINI-REVIEW.md APPROVED c266824..f7aa764 (pre-sync pin)"
    blocked_by: "STACK-CONFIRM fresh-context confirmation and a passing authoritative gate"
    evidence: docs/F014-PLAN.md
  - id: SHORTCUT-RECORDER-01
    feature: F051
    requirements: [IN-01, IN-04, IN-05]
    milestone: M3
    owner: lead
    state: integration
    covered_by: STACK-CORRECTION-01
    confirm_only: true
    outcome: Shortcut recorder with chords and conflict explanations in Settings (F051)
    branch: claude/f051
    candidate_commit: cf40fcc
    feature_commits: [ed12ebe, cf40fcc]
    merged_commit: "3b2b0db (user-directed stack merge, 2026-10-22)"
    gated_commit: "ec05e42 Windows 43/43 722.35s, Docker 43/43 411.23s; docs/verification/lead-integrate-all-2026-10-22/README.md; does not approve STACK-CORRECTION-01"
    review_evidence: "docs/verification/F051-GEMINI-REVIEW.md APPROVED f7aa764..8c3189c; cf40fcc (test key F8 to F7) is after the review"
    blocked_by: "STACK-CONFIRM fresh-context confirmation and a passing authoritative gate"
    evidence: docs/F051-PLAN.md
  - id: A11Y-AUDIT-01
    feature: F052
    requirements: [UX-11]
    milestone: M3
    owner: lead
    state: integration
    covered_by: STACK-CORRECTION-01
    confirm_only: true
    outcome: Accessibility audit test for every interactive control (F052, f597012); NVDA/Orca trials remain packet E6 RUNBOOK
    branch: claude/f051
    candidate_commit: cf40fcc
    feature_commits: [f597012]
    merged_commit: "3b2b0db (user-directed stack merge, 2026-10-22)"
    gated_commit: "ec05e42 Windows 43/43 722.35s, Docker 43/43 411.23s; docs/verification/lead-integrate-all-2026-10-22/README.md; does not approve STACK-CORRECTION-01"
    review_evidence: "docs/verification/F051-GEMINI-REVIEW.md APPROVED f7aa764..8c3189c; review names F052"
    blocked_by: "STACK-CONFIRM fresh-context confirmation and a passing authoritative gate"
    evidence: docs/F052-PLAN.md
  - id: CONTRAST-TARGETS-01
    feature: F053
    requirements: [UX-11, UX-17]
    milestone: M3
    owner: lead
    state: integration
    covered_by: STACK-CORRECTION-01
    confirm_only: true
    outcome: High-contrast palette and 44 px large targets (F053, e1c1c51); native high-contrast detection remains packet E6
    branch: claude/f051
    candidate_commit: cf40fcc
    feature_commits: [e1c1c51]
    merged_commit: "3b2b0db (user-directed stack merge, 2026-10-22)"
    gated_commit: "ec05e42 Windows 43/43 722.35s, Docker 43/43 411.23s; docs/verification/lead-integrate-all-2026-10-22/README.md; does not approve STACK-CORRECTION-01"
    review_evidence: "docs/verification/F051-GEMINI-REVIEW.md APPROVED f7aa764..8c3189c; review names F053"
    blocked_by: "STACK-CONFIRM fresh-context confirmation and a passing authoritative gate"
    evidence: docs/F053-PLAN.md
  - id: I18N-01
    feature: F054
    requirements: []
    milestone: M3
    owner: lead
    state: integration
    covered_by: STACK-CORRECTION-01
    corrected_by: STACK-F054-COVERAGE
    outcome: Pseudo-locale catalog, qsTr wrapping and right-to-left mirroring (F054, 209e703 through 8c3189c)
    branch: claude/f051
    candidate_commit: cf40fcc
    feature_commits: [209e703, 3d771f4, 1482d7e, de95624, 06f09ae, 776fa1e, c054973, 853ab3c, fb05f5f, e32b378, 3e882fb, 4990f0c, 64fe012, 8c3189c]
    merged_commit: "3b2b0db (user-directed stack merge, 2026-10-22)"
    gated_commit: "ec05e42 Windows 43/43 722.35s, Docker 43/43 411.23s; docs/verification/lead-integrate-all-2026-10-22/README.md; does not approve STACK-CORRECTION-01"
    review_evidence: "docs/verification/F051-GEMINI-REVIEW.md APPROVED f7aa764..8c3189c; review names F054"
    blocked_by: "STACK-F054-COVERAGE (main's newer strings, detached-window RTL, pseudo-locale/clipping/RTL evidence), then STACK-CONFIRM and a passing authoritative gate"
    evidence: docs/F054-PLAN.md; docs/verification/F054-pseudo-locale.png; docs/handoff/CLAUDE-STACK-SYNC-2026-10-22.md finding 4
  - id: TABS-01
    feature: F015
    requirements: []
    milestone: M3
    owner: lead
    state: integration
    covered_by: STACK-CORRECTION-01
    corrected_by: STACK-F015-RECOVERY
    outcome: Tabs as saved locations with a top-bar tab strip (F015, shape A)
    branch: claude/f051
    candidate_commit: cf40fcc
    feature_commits: [2566014, fb698c0]
    merged_commit: "3b2b0db (user-directed stack merge, 2026-10-22)"
    gated_commit: "ec05e42 Windows 43/43 722.35s, Docker 43/43 411.23s; docs/verification/lead-integrate-all-2026-10-22/README.md; does not approve STACK-CORRECTION-01"
    review_evidence: "2566014 lies inside the F051 review range f7aa764..8c3189c, but docs/verification/F051-GEMINI-REVIEW.md does not name F015; fb698c0 is after the review. Treat as unreviewed."
    blocked_by: "STACK-F015-RECOVERY (tabs through the production recovery checkpoint), then STACK-CONFIRM and a passing authoritative gate"
    evidence: docs/F015-PLAN.md; docs/handoff/CLAUDE-STACK-SYNC-2026-10-22.md finding 1
  - id: TYPE-CHIPS-01
    feature: F042
    requirements: [NAV-05, NAV-06]
    milestone: M3
    owner: lead
    state: integration
    covered_by: STACK-CORRECTION-01
    confirm_only: true
    outcome: File-type filter chips in the grid header (F042 chips only, a504baa); capture-date/dimension sorts and rating/orientation/date chips remain packets A2 and A4
    branch: claude/f051
    candidate_commit: cf40fcc
    feature_commits: [a504baa]
    merged_commit: "3b2b0db (user-directed stack merge, 2026-10-22)"
    gated_commit: "ec05e42 Windows 43/43 722.35s, Docker 43/43 411.23s; docs/verification/lead-integrate-all-2026-10-22/README.md; does not approve STACK-CORRECTION-01"
    review_evidence: "a504baa lies inside the F051 review range f7aa764..8c3189c, but docs/verification/F051-GEMINI-REVIEW.md does not name F042. Treat as unreviewed."
    blocked_by: "STACK-CONFIRM fresh-context confirmation and a passing authoritative gate"
    evidence: docs/F013-F042-UI-PLAN.md
```

Optionally, list the new IDs on STACK-CORRECTION-01: `covered_rows: [PATH-COMPLETE-01, VIEW-POLISH-01, SLIDESHOW-01, REFERENCE-WINDOW-01, LAYOUT-PRESETS-01, DETAILS-VIEW-01, GRID-BADGES-01, SHORTCUT-RECORDER-01, A11Y-AUDIT-01, CONTRAST-TARGETS-01, I18N-01, TABS-01, TYPE-CHIPS-01]`.

### 2. COPY-MOVE-01: add `feature`, `commit` and `integrated_evidence`

```yaml
    feature: F020
    commit: b13b73941d9420ac2740bdb58aefe9132540daa0
    integrated_evidence: "Integration merge b13b739 (parents 44d9cf2, 079564b), closed by a78b930. Pre-merge: source e4a5a07 Windows scripts/build.ps1 -Test 28/28 (200.80s) and Docker 28/28 (116.69s, recorded in 44d9cf2; run commit not stated), docs/verification/COPY-MOVE-01.md. No post-integration check of b13b739 itself. First full gate on a descendant: 259d337 Windows 29/29 (220.69s), docs/verification/MAIN-259d337-CLAUDE-VERIFY.md (git merge-base --is-ancestor b13b739 259d337 succeeds); its Docker test count was not recorded (unverified)."
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
- **Queue still parses after the paste.** I inserted block 1 into a copy of `docs/WORK-QUEUE.yaml` at the paste point above and parsed it with the same strict loader: 84 tasks (73 before), no duplicate IDs. The current queue also parses strictly: 73 tasks, no duplicates.
- **IDs are new.** None of the eleven new IDs appears in `docs/WORK-QUEUE.yaml`, and none repeats within the block.
- **Commits verified.** Every SHA cited above resolves with `git rev-parse` and is an ancestor of `3b2b0db` (except `ec05e42`, which is a descendant).
- **Stale claims gone.** No "JPEG/PNG", "JPEG and PNG", "WSL" or "core only" claim remains in `README.md`, `docs/TESTING.md`, `docs/DOCKER.md` or `docs/SUPPORTED-CAPABILITIES.md`.

## Not verified, or left for the lead

- **Fresh review.** EN-08's acceptance asks for a fresh reviewer to check the capability page against the queue. That review is pending and belongs to the lead.
- **Docker formats not observed.** That WebP, TIFF and TGA fail in Docker is inferred from the `Dockerfile` (no `qtimageformats`) and LEAD-PLAN section 2. No container run of those files was observed.
- **Windows format decoding untested.** WebP, TGA and APNG decoding on Windows has no fixture test. APNG animation and animated WebP are marked unverified.
- **Native Linux package formats.** It is unknown whether the package's Qt image (`glancelot-qt:codex-perf-measure` in `tools/platform/Dockerfile.package`) includes `qtimageformats`.
- **ICO and WBMP.** They are listed as not openable because their extensions are not on the navigation allowlist. Opening one directly by path is untested.
- **Stale claims in code.** These are out of scope for a docs packet and are for a code packet:
  - `app/startup_intent.cpp:25`: the `--help` description still says "JPEG/PNG alpha."
  - `src/worker/main.cpp:232`: the unsupported-format message lists WebP/TIFF/TGA even in builds without those plugins, and it is also shown for damaged files (AUDIT-UX major 2).
- **Trash on a writable mount.** Where a read-write Docker mount's Trash goes is unverified.
