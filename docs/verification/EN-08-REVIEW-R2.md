# EN-08 review, round 2: capability page and stale doc claims

**Verdict: CHANGES REQUIRED**

One blocking item, finding 1, is a one-phrase fix. Round 3 only needs to check that phrase, plus finding 2 if the author takes it.

## Scope

- **Reviewer.** A fresh-context Claude (Opus 5.5) subagent. It did not write EN-08, fix it, or do the round-1 review.
- **Reviewed.** `git diff 6e9b12b origin/claude/en-08-docs`, tip `d78bf66`. This range includes the merge of main `f325ef6` at `8476fe0`.
  - The round-1 commit `d78bf66` itself touches only five files: `README.md`, `docs/DOCKER.md`, `docs/SUPPORTED-CAPABILITIES.md`, `docs/TESTING.md` and `docs/handoff/EN-08-HANDOFF.md`.
  - I also re-read the whole `docs/SUPPORTED-CAPABILITIES.md`.
- **Compared against.** Code and the queue on `origin/main`.
  - **Main has moved.** `origin/main` is now `445fc26`. It changes only `AGENTS.md`, the `integration_lock` line of `docs/WORK-QUEUE.yaml` and `docs/handoff/PHASE0-CYCLE1-2026-10-22.md`. It contains no code.
  - **What carries over.** The round-1 "checked and supported" list was made against `f325ef6`, so it still holds, and the page's "Last checked against `main` at `f325ef6`" is accurate.
  - **Merge.** `git merge-tree --write-tree origin/main origin/claude/en-08-docs` is clean.
- **Method.** Documents only. No build or test was run.

## Grant check

`git diff --stat origin/main...origin/claude/en-08-docs` (three dots, from the merge base) lists six files: `README.md`, `docs/AGENT-LANES-PLAN.md`, `docs/DOCKER.md`, `docs/SUPPORTED-CAPABILITIES.md`, `docs/TESTING.md` and `docs/handoff/EN-08-HANDOFF.md`.

The branch does not touch `AGENTS.md`, `docs/WORK-QUEUE.yaml`, any workflow, `BUILDING.md` or the `Dockerfile`. That matches the handoff's "Not touched" line. A two-dot diff against `origin/main` shows `AGENTS.md` and `PHASE0-CYCLE1` deletions; they are only main's `445fc26`, which the branch does not have yet.

## Findings

### 1. EN-02 does not add `qtimageformats` to the hosted job

**Severity:** blocking (factual error, new in round 1).

**Where:**

- `docs/SUPPORTED-CAPABILITIES.md`, "Image formats": "Packet EN-02 adds `qtimageformats` to Docker and to the hosted job."
- `docs/handoff/EN-08-HANDOFF.md`, review round 1 table, row 2: "EN-02 is noted as adding the plugins there too."

**Evidence:**

- **The queue row.** EN-02 on `origin/main` has the outcome "scripts/linux-bootstrap.sh, qtimageformats in Docker, rewritten BUILDING.md Linux section". Its acceptance names only the Docker gate. Neither field mentions `windows-gate.yml` or the hosted job.
- **The branch.** `git diff --stat origin/main...origin/claude/en-02-linux-bootstrap` (tip `dffc799`, state `review`) touches only four files: `BUILDING.md`, `Dockerfile`, `docs/handoff/EN-02-HANDOFF.md` and `scripts/linux-bootstrap.sh`.
- **The workflow on that branch.** `.github/workflows/windows-gate.yml:51` still installs `--archives qtbase qtdeclarative qtshadertools`.

**Why it blocks.** The packet's own acceptance is "capability page reviewed by a fresh reviewer against the queue", and the queue contradicts this sentence. Round 1 also treated a plugin-and-gate factual error (its finding 2) as blocking.

**Fix:**

- Say that EN-02 adds the module to Docker.
- Say that the hosted advisory job has no packet for it yet; the lead may queue one.
- Correct the handoff table row the same way.

### 2. "Window positions" reads as window geometry

**Severity:** minor (wording).

**Where:** `docs/SUPPORTED-CAPABILITIES.md`, in two places:

- the Viewing table, per-window resume row, Docker column: "Position is stored in the `glancelot-config` volume";
- "Docker browser desktop specifics", the Settings bullet: "Settings and window positions live in the `glancelot-config` volume".

**What the record shows.** `docs/verification/DOCKER-BROWSER.md` says that `settings.json` stored `folder`, `image` and `saved` for that window.

**Fix.** Say "each window's last folder and photo", so the wording is not read as window geometry.

### 3. New folder is in neither intro bullet

**Severity:** nit (an omission, not an overclaim).

**Where:** `docs/SUPPORTED-CAPABILITIES.md`, "File operations", the intro bullets.

The intro sorts file-changing commands into "confirmed first" (delete and rename) and "runs at once" (paste). New folder (**Ctrl+Shift+N**) also opens a naming dialog first: `RenameFlow.qml` `openNewFolder` / `newFolderDialog`, reached from `ui/Main.qml` `new-folder`. Consider adding it to the first bullet.

### 4. The handoff's "Files changed" row for DOCKER.md is stale

**Severity:** nit.

**Where:** `docs/handoff/EN-08-HANDOFF.md`, "Files changed", the `docs/DOCKER.md` row.

It still says the read-only `/photos` mount "refuses delete/rename/move". `docs/DOCKER.md` itself now correctly says "should be refused (inferred from the mount, not observed in a container run)". Align the row.

### 5. The handoff will need the new pre-handoff checklist sections

**Severity:** information, for the lead.

Main's `445fc26` adds a pre-handoff checklist to `AGENTS.md`. It asks for "Decisions taken", "Open questions for the lead", a grant check and an exact base. Once main is merged into the branch, the EN-08 handoff has none of these sections as such. The content is mostly there: "Not verified, or left for the lead", the base `3893c77`, and the merge of `f325ef6`. Whether a docs-only packet that predates the rule must add them is the lead's call.

## Round-1 findings

| # | Round-1 finding | Status | Evidence |
| --- | --- | --- | --- |
| 1 | Paste offers no collision choice and no confirmation (blocking) | **Resolved** | See "Paste behaviour" below the table. |
| 2 | The self-hosted gate has `qtimageformats` (blocking) | **Resolved**, but the new EN-02 sentence is wrong (finding 1) | See "Windows gate and plugins" below the table. |
| 3 | Docker resume after recreating the container (blocking) | **Resolved** | The Viewing row and the Docker specifics both mark resume after recreating the container as unverified, citing the "Not verified" list of `DOCKER-BROWSER.md`, which says "Resume of slot 1 after container recreation". The "unverified container behaviour" bullet adds it too. Wording nit in finding 2. |
| 4 | Re-check against current main; COPY-SAFETY-01; "verify" is size-only (blocking) | **Resolved** | See "COPY-SAFETY-01 and the size check" below the table. |
| 5 | The superseded banner conflicts with AGENTS.md | **Moot** | `AGENTS.md` on `origin/main` now says that `docs/AGENT-LANES-PLAN.md` and the lane rules in `docs/handoff/LEAD-TAKEOVER-2026-10-22.md` "are history". The banner agrees. |
| 6 | The queue keys do not mirror the model rows | **Resolved** | See "Proposed queue YAML". |
| 7 | Stack states | **Resolved** | See "Stack states" below the table. |
| 8 | "Unchanged code" | **Resolved** | Now "unchanged product and test code". |
| 9 | Inferred Docker behaviour | **Resolved** | "Inferred, not observed" now appears in four places: the capability page (the damaged-file message, the read-only refusals in the table and the specifics, and WebP/TIFF/TGA), `docs/DOCKER.md` (both sentences) and `docs/TESTING.md`. |
| 10 | README "in-app comparison" | **Resolved** | Now "two-up/four-up comparison in the main window (the second display has a comparison mode)". |
| 11 | TESTING.md list of commands | **Resolved** | Adds new folder, and notes that paste runs without confirmation. |
| 12 | Safe mode wording | **Resolved** | Now "it requests a software rendering device". |
| 13 | WebP and TGA on Windows | **Resolved** | The table says "Expected (plugin present); **no decode test**". The README says "without decode tests yet". |
| 14 | PERF-RENDER | Information only | No change needed. |

### Paste behaviour (round-1 finding 1)

The page intro now lists "confirmed first: delete and rename" and "runs at once: paste". The copy/cut/paste row no longer says "collision choice", and the README sentence is narrowed. I re-checked each claim on `origin/main`:

- **No confirmation.** `ui/Main.qml` `paste` (around line 811) calls `viewer.copyMoveService.pasteTo(dest)` with no dialog.
- **Always keep both.** `CopyMoveService::pasteTo` (`src/app/copy_move_service.cpp:272-275`) always enqueues with `CollisionPolicy::RenameBoth`. The resolver (`:68-77`) picks the first free name of the form `name (N).ext`, so the existing file is not opened. The README is therefore accurate for cut-and-paste: "the moved file gets a numbered name and the existing file is left untouched".
- **Delete always confirms.** `FileOperationService` has two steps, plan then `confirm(id)`, and `DeleteFlow.qml` has no skip-confirmation preference.
- **Rename confirms.** `RenameFlow.qml` opens a dialog.
- **The HUD controls exist.** `ui/TransferHud.qml` has Pause, Resume and Cancel.

### Windows gate and plugins (round-1 finding 2)

The page now has two bullets: the authoritative self-hosted gate uses the main checkout's local Qt, and the hosted advisory job lacks the plugins. I confirmed both:

- **The self-hosted job.** The `gate` job (`windows-gate.yml:76` onward) runs `./scripts/build.ps1 -BuildDir build-ci -Test` with no `-QtRoot`, in a worktree of `C:\src\Github\Glancelot`.
- **How Qt is found.** `build.ps1` resolves `.tools` through `git rev-parse --git-common-dir` to the main checkout and defaults to `Qt\6.8.3\msvc2022_64`.
- **That Qt has the plugins.** `plugins\imageformats\qtiff.dll` exists there.
- **The hosted job lacks them.** It installs only `--archives qtbase qtdeclarative qtshadertools` (`:51`).

### COPY-SAFETY-01 and the size check (round-1 finding 4)

- **Last checked.** The page now says `f325ef6`. That remains accurate on `445fc26` (see Scope).
- **Known-issue note.** The page names COPY-SAFETY-01 as open and `ready`, which the queue on `origin/main` confirms. The note says that no UI reaches Skip or Overwrite (confirmed: no QML calls `enqueueTransfer`), and that Overwrite truncates the target (`:437`) and removes it on failure or cancellation (`:479-481`).
- **Size check.** The page says the cross-volume check "compares sizes only, not content", which matches `:479`. Source deletion follows only after that check (`:489-491`).

### Stack states (round-1 finding 7)

On `origin/main`, the four STACK-F0xx packets (F040-PINNED, F015-RECOVERY, F049-PRESETS, F054-COVERAGE) are `review`, and STACK-CONFIRM is `ready`. "Has not started" is a fair reading of `ready`.

## Whole-page re-check (beyond round 1)

I found no remaining overclaim apart from finding 1. I spot-checked these again on `origin/main`:

- **Decoder.** `setAutoTransform(true)` plus `convertToColorSpace(QColorSpace::SRgb, QImage::Format_RGBA8888)` (`src/worker/main.cpp:78-89`, `:101-119`). This supports "EXIF orientation applied; embedded ICC profile converted to sRGB".
- **Queue states the page quotes.**
  - `integration`: DECODE-01, CANVAS-UI, PATH-COMPLETE-01 and VIEW-POLISH-01.
  - `done`: FS-WATCH-01, SAFE-MODE-01, MONITOR-OUTPUT-01, MONITOR-CUSTOM-01, WINDOW-RESUME-01, SORT-01, COPY-MOVE-01 and DOCKER-BROWSER-01.
  - `ready`: MONITOR-VERIFY-01 and COPY-SAFETY-01.
- **SHAs on the page.** `f325ef6`, `ec05e42`, `3b2b0db` and `6cc87ee` are all commits and all ancestors of `origin/main`.
- **README, TESTING.md and DOCKER.md round-1 edits.** All accurate against the code above. The README's 256 MiB decoder-limit line matches the decoder.

## Proposed queue YAML (`docs/handoff/EN-08-HANDOFF.md`)

I parsed the YAML with a PyYAML 6.0.3 loader that rejects duplicate keys, against `docs/WORK-QUEUE.yaml` from `origin/main` (74 tasks, 74 unique IDs).

- **Block 1, the eleven rows.**
  - **Keys.** Every key already exists in the current queue rows.
  - **Strings.** Every value is a string or a list of strings, and no SHA-like value is left unquoted.
  - **IDs.** None of the eleven IDs exists yet.
  - **References.** Every `depends` ID exists: STACK-CORRECTION-01, STACK-F040-PINNED, STACK-F049-PRESETS, STACK-F054-COVERAGE and STACK-F015-RECOVERY. So does every STACK-* name in `blocked_by`, including STACK-CONFIRM.
  - **Requirement IDs.** All exist in `docs/REQUIREMENTS.md`: UX-10, UX-09, UX-14, UX-17, UX-02, META-05, IN-01, IN-04, IN-05, UX-11, NAV-05 and NAV-06.
- **Paste.** Inserting block 1 before the `# Phase 0 cycle 1` comment gives 85 tasks with no duplicate IDs, and STACK-CORRECTION-01 is the row directly before SLIDESHOW-01.
- **Blocks 2 to 4.** These add `feature`, `commit` and `integrated_evidence` to COPY-MOVE-01 and FORMAT-02, and `feature` to RENAME-01.
  - All three keys exist in current rows, in 8, 31 and 10 rows respectively.
  - None is already present on the target row: COPY-MOVE-01 and FORMAT-02 lack all three, and RENAME-01 lacks `feature`.
  - The values are strings.
- **SHAs.** I found 57 distinct hex tokens in the handoff, counting only tokens with a digit.
  - Every one is a commit (`git cat-file -t`).
  - All but one pass `git merge-base --is-ancestor <sha> origin/main`.
  - The exception is `1401966`, the round-1 review commit on `claude/review-en-08`. Being off main is expected for a review branch, and it is not a finding.
