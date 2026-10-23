# EN-08 review, round 3: capability page and stale doc claims

**Verdict: APPROVED**

The round-2 blocking item is fixed, the four non-blocking items are done, and the changed lines add no new overclaim. Two nits follow; neither blocks.

## Scope

- **Reviewer.** A fresh-context Claude (Opus 5.5) subagent. It did not write EN-08, fix it, or do the round-1 or round-2 review.
- **Reviewed.** `git diff d78bf66 origin/claude/en-08-docs`, tip `7d152cc`.
  - The range includes the merge of main `4f29124` at `91bd61b`, plus `900d892` and `7d152cc` (round-2 fixes).
  - The branch's own round-2 edits touch only `docs/SUPPORTED-CAPABILITIES.md` and `docs/handoff/EN-08-HANDOFF.md`. The other files in the range (`AGENTS.md`, `docs/WORK-QUEUE.yaml`, `docs/handoff/PHASE0-CYCLE1-2026-10-22.md`) arrive only through the merge.
  - The round-2 review is `docs/verification/EN-08-REVIEW-R2.md` at `41a65ec`.
- **Compared against.** Code and the queue on `origin/main` `4f29124`.
- **Method.** Documents only. No build or test was run.

## Grant and merge

- **Grant.** `git diff --stat origin/main...origin/claude/en-08-docs` lists the same six files as round 2: `README.md`, `docs/AGENT-LANES-PLAN.md`, `docs/DOCKER.md`, `docs/SUPPORTED-CAPABILITIES.md`, `docs/TESTING.md` and `docs/handoff/EN-08-HANDOFF.md`.
- **Main-owned files.** `git diff origin/main origin/claude/en-08-docs -- docs/WORK-QUEUE.yaml AGENTS.md docs/handoff/PHASE0-CYCLE1-2026-10-22.md` is empty, so the branch carries main's versions of those files unchanged.
- **Merge.** `git merge-tree --write-tree origin/main origin/claude/en-08-docs` is clean (tree `8906136`).

## Round-2 findings

| # | Round-2 finding | Status | Evidence |
| --- | --- | --- | --- |
| 1 | EN-02 does not add `qtimageformats` to the hosted job (blocking) | **Resolved** | See "Hosted-job phrase" below the table. |
| 2 | "Window positions" reads as window geometry | **Done** | The Viewing row and the Docker Settings bullet both now say "each window's last folder and photo". No "position" or "positions" wording remains on the page. |
| 3 | New folder is in neither intro bullet | **Done** | The bullet now reads "delete, rename and new folder open a dialog". On `origin/main`, `ui/Main.qml:783` (`new-folder`) calls `renameFlow.openNewFolder(folder)` (`:792`), which opens `newFolderDialog` (`ui/RenameFlow.qml:65-73`). The bullet already cites `ui/RenameFlow.qml`. |
| 4 | The handoff's "Files changed" row for DOCKER.md is stale | **Done** | The row now says the formats and the read-only refusals are "inferred, not observed", matching `docs/DOCKER.md:33` and the read-only sentence. |
| 5 | Pre-handoff checklist sections | **Done** | The handoff adds a "Pre-handoff checklist (AGENTS.md)" section: grant check output, exact base (cut from `3893c77`, merge base `4f29124`), Decisions taken and Open questions for the lead. |

### Hosted-job phrase (round-2 finding 1)

The page now says, under "Image formats":

> Packet EN-02 adds `qtimageformats` to Docker. The lead adds it to the hosted advisory job under EN-01 once EN-02 is integrated.

The first sentence limits EN-02 to Docker, and the second assigns the hosted job to the lead under EN-01. I checked it:

- **EN-02 scope.** On `origin/main`, EN-02's outcome is "scripts/linux-bootstrap.sh, qtimageformats in Docker, rewritten BUILDING.md Linux section". Neither EN-02 branch touches a workflow:
  - `claude/en-02-linux-bootstrap` (`dffc799`) changes `BUILDING.md`, `Dockerfile`, `docs/handoff/EN-02-HANDOFF.md` and `scripts/linux-bootstrap.sh`.
  - `claude/en-02-cloud-proof` (`5472e32`) changes the same four files, plus files under `docs/verification/`.
- **EN-01 scope.** EN-01 (owner `lead`, state `implementing`) has the outcome "Authoritative and advisory Windows gate jobs that ... match Docker's Qt modules". That supports putting the hosted-job change under EN-01.
- **Workflow today.** `.github/workflows/windows-gate.yml` on `origin/main` still installs `--archives qtbase qtdeclarative qtshadertools`. The page's "Hosted advisory Windows job" bullet is therefore still accurate.
- **Handoff, round-1 table, row 2.** It now ends "EN-02 was noted as adding the plugins there too; corrected in round 2." The round-2 table row 1 and the "Open questions for the lead" item ("Add `qtimageformats` to it under EN-01 when EN-02 is integrated") agree with the page.
- **Rest of the branch.** No other line on the branch says that EN-02 changes the hosted job. `git grep -i "hosted\|EN-02\|EN-01"` over the five documents finds only the lines above, plus Docker-only EN-02 mentions in `docs/DOCKER.md:33`, `docs/TESTING.md:26` and the Docker specifics.

## New claims in the changed lines

I checked each new factual claim against `origin/main`:

- **Stale code strings (Open questions).** `app/startup_intent.cpp:25` is `"A local-first native photo viewer. JPEG/PNG alpha."`. `src/worker/main.cpp:232` is the unsupported-format message, which still says "JPEG, PNG, GIF, WebP, TIFF, BMP, and TGA" (no ICO or WBMP, although the check on the same line accepts both). Both are accurately described as stale.
- **Exact base.** `4f2912476defde4f549a53c7c1dde6581c8a3539` is `origin/main` and the merge base. `3893c77` and `f325ef6` are ancestors of it.
- **"Last checked against `main` at `f325ef6`" on the page.** It still holds. `445fc26` and `4f29124` change only documents and the queue's `integration_lock` line, as round 2 recorded.
- **Decisions taken.** They match the page: Docker format gaps are labelled inferred, ICO and WBMP are listed as not openable, and the stack features have their own section.

## Proposed queue YAML

I parsed it under WSL with Python 3 and PyYAML 6.0.1, using a `SafeLoader` subclass that rejects duplicate keys. The base is `docs/WORK-QUEUE.yaml` from `origin/main` `4f29124`.

- **Base queue.** It parses strictly: 74 tasks, 74 unique IDs.
- **Block 1 alone, under `tasks:`.** It gives 11 rows.
  - Every value is a string or a list of strings.
  - Every key already exists in the queue.
  - Every `depends` ID exists, and none of the eleven IDs is already present.
  - No SHA-like value is left unquoted.
- **Block 1 merged as instructed.** I inserted it before the `# Phase 0 cycle 1` comment; the row before that point is STACK-CORRECTION-01. The result parses strictly: 85 tasks, 85 unique IDs, STACK-CORRECTION-01 directly before SLIDESHOW-01. `unverified`, `theme_task` and `remaining_waves` are still top-level keys.
- **Blocks 2 to 4 merged too.** They are appended to COPY-MOVE-01, FORMAT-02 and RENAME-01 respectively. The result still parses strictly, with no duplicate key. `feature`, `commit` and `integrated_evidence` load as strings.

## Nits (non-blocking)

1. **"All SHA values are quoted" (Decisions taken).** This is true of block 1. In blocks 2 and 3, though, `commit:` is unquoted (`b13b73941d94…` and `362c2814f520…`). Both contain letters, so they load as strings, and existing queue rows also leave `commit:` unquoted. Either quote the two values or scope the sentence to block 1.
2. **Round-1 table, row 3.** It still says "the position is stored in the volume". This is a record of the round-1 fix, and round 2 has since replaced the wording on the page. Leaving it is acceptable.
