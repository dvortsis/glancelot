# EN-08 review: capability page and stale doc claims

**Verdict: CHANGES REQUIRED**

- Reviewer: fresh-context Claude (Opus 5.5) subagent. It did not write or fix any part of EN-08.
- Reviewed: `git diff 3893c77 origin/claude/en-08-docs`, with tip `6e9b12b` and base `3893c77`.
- Compared against: code and evidence on `origin/main` `f325ef6`. Main has moved two queue-only commits past the base, `2c61cca` and `f325ef6`.
- Scope: documents only. No build or test was run.

Four blocking findings (1 to 4) must be fixed before integration. The other findings should be fixed in the same round, or recorded for the lead.

## Blocking

### 1. Paste offers no collision choice and no confirmation

**Severity:** blocking (overclaim). **Where:** `docs/SUPPORTED-CAPABILITIES.md`, "File operations" (the intro and the copy/cut/paste row), and `README.md` "Run".

What the docs claim:

- **Capability page intro:** "Every change below happens only on an explicit command, with a confirmation or a conflict choice."
- **Copy/cut/paste row:** "background queue with pause, resume, cancel and collision choice".
- **README:** "File changes happen only on explicit commands with confirmation: … Ctrl+X then Ctrl+V moves files."

What the code does:

- **No confirmation.** `paste` in `ui/Main.qml:811-817` calls `copyMoveService.pasteTo(dest)` directly.
- **No choice.** `CopyMoveService::pasteTo` (`src/app/copy_move_service.cpp:263-280`) always enqueues with `CollisionPolicy::RenameBoth`. A clash keeps both files and names the new one `name (2).ext`.
- **The other policies have no UI.** No QML calls `enqueueTransfer`, so `Skip` and `Overwrite` exist only in the service API (`include/glancelot/copy_move_service.hpp:39-41`) and in `CopyMoveTests::testCollisionPolicies`.
- **The HUD controls do exist.** `ui/TransferHud.qml` has Pause, Resume and Cancel.

Fix:

- Say that paste runs at once, with no confirmation.
- Say that a name clash always keeps both files, the new copy getting a numbered name.
- Drop "collision choice".
- Narrow the section intro and the README sentence so they do not promise a confirmation for paste. Delete and rename do confirm (`DeleteFlow.qml`, `RenameFlow.qml`).

### 2. The authoritative Windows gate does have `qtimageformats`

**Severity:** blocking (factual error). **Where:** `docs/SUPPORTED-CAPABILITIES.md`, "Image formats", Windows bullet.

The page says: "The self-hosted gate job in `windows-gate.yml` does not install `qtimageformats`."

- **The self-hosted job.** The `gate` job (`.github/workflows/windows-gate.yml:76-133`) installs no Qt. It runs `scripts/build.ps1 -BuildDir build-ci -Test` without `-QtRoot`, so `build.ps1:22-32` resolves Qt to the main checkout's `.tools/Qt/6.8.3/msvc2022_64`.
- **That Qt has the plugins.** Its `plugins/imageformats` folder contains `qtiff.dll`, `qwebp.dll`, `qtga.dll`, `qico.dll`, `qwbmp.dll`, `qicns.dll` and `qgif.dll`. I listed the folder on this machine.
- **The job that lacks them.** The *hosted advisory* job (`windows-gate.yml:47-52`) installs only `qtbase qtdeclarative qtshadertools`, the same archives as the Dockerfile. It is the one without the plugins.
- **Why it matters.** As written, the sentence implies `testMultipageTiffNavigation` skips on the authoritative gate. That contradicts the page's own TIFF "Yes".

Fix: name the hosted advisory job, and say that the self-hosted gate uses the local Qt, which includes `qtimageformats`.

### 3. Docker window resume after container recreation is unverified

**Severity:** blocking (overclaim). **Where:** `docs/SUPPORTED-CAPABILITIES.md`, "Viewing", per-window resume row, Docker column.

- **The page says** "Yes (stored in the `glancelot-config` volume)" and cites `docs/verification/DOCKER-BROWSER.md`.
- **What that record shows.** It verifies that `settings.json` in `/config` stored the window's folder and image.
- **What it does not show.** Its "Not verified" list includes "Resume of slot 1 after container recreation".

Fix: keep "position stored in the volume", and mark resume after recreating the container as **unverified**.

### 4. Re-check the page against current `main` before integration

**Severity:** blocking, process. **Where:** `docs/SUPPORTED-CAPABILITIES.md` header ("Last checked against `main` at `3893c77`") and the File operations section.

`f325ef6` added COPY-SAFETY-01 to the queue. The service's Overwrite path does two things:

- It opens the existing target with `QIODevice::Truncate` before copying (`copy_move_service.cpp:437`).
- It deletes the target with `QFile::remove` when the copy fails or is cancelled (`:481`).

Today no UI reaches Overwrite (finding 1), so the risk is latent. Even so, two things on the page need changing:

- **Name the open packet.** A page that sells the copy queue's safety should name COPY-SAFETY-01 as open wherever it mentions collision policies.
- **Say what "verify" means.** "Cross-volume moves copy and verify before removing the source" is true, but the check is a size comparison only (`:479`), not a content check. Say "size-checked" or "verified by size".

Also update the "Last checked" line to the commit the page is re-checked against. `2c61cca` moved the four STACK-F0xx corrections to `review`; see finding 7.

## Non-blocking

### 5. The superseded banner conflicts with AGENTS.md

**Severity:** major, non-blocking. **Where:** `docs/AGENT-LANES-PLAN.md`, the new banner, and `AGENTS.md`.

- **The conflict.** AGENTS.md's first bullet sends readers to "docs/AGENT-LANES-PLAN.md (amendment of October 22)" as the authority for the current override. The new banner says the whole body is "history only; do not assign or route work from it".
- **Fix.** Either exempt the "Amendment, October 22, 2026: single-lead takeover" section in the banner, or have the lead repoint AGENTS.md. AGENTS.md is lead-owned.
- **Separately, the scope item is only half done.** The LEAD-PLAN EN-08 row includes "AGENTS.md: replace lane rules with this model". On main, the lane rules are overridden by the first bullet, not replaced. The three-lane and tools-per-lane bullets are still present. The handoff should record this as open lead work instead of omitting it.

### 6. The proposed queue fields do not mirror the model rows

**Severity:** minor. **Where:** `docs/handoff/EN-08-HANDOFF.md`, block 1.

The handoff says "The field names mirror those two rows" (PATH-COMPLETE-01 and VIEW-POLISH-01). They do not:

- **New keys.** The proposed rows introduce `covered_by`, `confirm_only`, `corrected_by` and `feature_commits`. None of these appears anywhere in the queue on main.
- **Missing key.** The rows omit `acceptance`, which both model rows have and 74 rows use.

Either add an `acceptance` line per row and get the lead's sign-off on the new keys, or reword the claim. The paste point (after `STACK-CORRECTION-01`, before `# Phase 0 cycle 1`) still exists on `f325ef6`.

### 7. Stack feature states versus the queue

**Severity:** minor. **Where:** `docs/SUPPORTED-CAPABILITIES.md`, "Merged, correction pending".

- **The page says** these features "are still in state `integration`" and cites STACK-CORRECTION-01.
- **What the queue shows.**
  - Only PATH-COMPLETE-01 and VIEW-POLISH-01 have rows, both in `integration`.
  - STACK-CORRECTION-01 is `ready`.
  - The other eleven features have no row until the handoff's block 1 is pasted.
  - The four corrections are in `review` on `f325ef6`.
- **Fix.** Reword, for example: "merged; corrections and confirmation outstanding (STACK-CORRECTION-01)". Do not state a queue state the queue does not hold.

### 8. "Later runs on unchanged code"

**Severity:** nit. **Where:** platforms table, Windows row.

`f033070` and `afeac1a` change `CMakeLists.txt`: per-suite QTest results files, 24 lines. No product or test source changed after `ec05e42`. Say "unchanged product and test code".

### 9. Some Docker behaviour is inferred, not observed

**Severity:** minor. **Where:**

- "Known format limits", the second bullet ("In Docker, a WebP, TIFF or TGA file gets that same message").
- The File operations Docker column and "Docker browser desktop specifics" ("Delete, rename and move into that tree are refused").
- `docs/DOCKER.md`, the new read-only sentence.

These follow from the code:

- No WebP, TIFF or TGA plugin in the image, so `reader.format()` is empty and `src/worker/main.cpp:232` gives the generic message.
- `/photos` is a read-only mount (`touch` failed in DOCKER-BROWSER.md), and DELETE-01's planning rejects read-only volumes.

None of this was observed in a container run. The image-format intro already says so for decoding. Add "(inferred, not observed)" to these sentences too.

### 10. README "in-app comparison"

**Severity:** minor. **Where:** `README.md`, "Development and status".

The README says "in-app comparison … not delivered". But the second display has a comparison mode (`presentation-compare`, MONITOR-OUTPUT-01, state `done`), and F040 pins a reference window. Say what is missing, for example "two-up/four-up comparison in the main window" (F026).

### 11. TESTING.md list of file-changing commands

**Severity:** nit. **Where:** `docs/TESTING.md`, the third paragraph.

"Only explicit delete, rename or copy/move commands change files" leaves out new folder (**Ctrl+Shift+N**).

### 12. Safe mode wording

**Severity:** nit. **Where:** "Safe mode and recovery", the safe-mode row.

`app/startup_intent.cpp:41` says safe mode will "request a software RHI device". The page says it uses one. Say "requests".

### 13. WebP and TGA on Windows

**Severity:** nit. **Where:** `README.md` intro and the formats table.

The Windows "Yes" rests on code and plugin presence, not on a test:

- The allowlist and decoder accept these formats (`navigation.cpp:171-185`, `main.cpp:232`).
- The local package ships `dist/Glancelot/plugins/imageformats/qwebp.dll` and `qtga.dll`.

The table carries a bold "no decode test", which is acceptable. The README lists WebP and TGA with no qualifier. Consider "expected (plugin present; no decode test)" in the table and a short qualifier in the README.

### 14. `PERF-RENDER` is a packet ID, not a requirement ID

**Severity:** information only. It is not in `docs/REQUIREMENTS.md`. It is defined in `docs/PERFORMANCE-STRATEGY.md`. That is fine for a "Tracked by" column.

## Checked and supported

**Navigation allowlist.** `imageCategory` in `src/core/navigation.cpp:165-205` matches the page's extension list exactly:

- `gif`, `webp`, `apng`
- `jpg`, `jpeg`, `png`, `bmp`, `tga`, `tif`, `tiff`
- the 12 RAW extensions

ICO and WBMP are not on it.

**Decoder.**

- It decides the format from the file content (`setDecideFormatFromContent`).
- It accepts `jpeg`, `png`, `gif`, `webp`, `tiff`, `bmp`, `tga`, `wbmp` and `ico`.
- It enforces a 256 MiB decoded limit and a 32768 px edge.
- It converts to `Format_RGBA8888` sRGB (`src/worker/main.cpp:227-252`).
- RAW goes through the embedded-preview path, which picks the largest candidate (`raw_preview.cpp:263-273`).

**Qt modules.**

- **Docker** installs only `qtbase qtdeclarative qtshadertools` and ICU (`Dockerfile:5-7`).
- **The `build.yml` viewer job** installs `qtimageformats`.
- **BUILDING.md** installs `qtimageformats`.
- **The deploy script** is `qt_generate_deploy_qml_app_script` (`CMakeLists.txt:323`).

**Animation.**

- Speed is clamped to 0.1 to 4.0 (`animation_controller.cpp:222`).
- Up to 64 frames are pre-decoded in-process.
- The TIFF test calls `QSKIP` when the plugin is absent (`animation_tests.cpp:237`).
- The cited test names exist.

**RAW (FORMAT-02).** The record uses synthetic and scanned fixtures, with absent-preview and malformed inputs. No real camera files are used.

**Gate counts.** On `ec05e42`: Windows 43/43 in 722.35 s and Docker 43/43 in 411.23 s (`lead-integrate-all-2026-10-22/README.md`). The triage failures match `gate-triage-2026-10-22/README.md`.

**Default keys.** These match `src/app/command_registry.cpp`:

- Delete: **Delete**, **Ctrl+Delete**, **Ctrl+Shift+Delete**
- **F2** and **Ctrl+Shift+N**
- **Ctrl+X** and **Ctrl+V**; `copy-selection` has no default key
- **Ctrl+C**, **Ctrl+Shift+C** and **Ctrl+Alt+C**
- **Ctrl+Alt+R** and **Ctrl+Alt+T**
- **F8**, **I**, **Ctrl+F**, **F5**, **Ctrl+Shift+P** and **F1**
- 50/200/400% zoom presets and zoom lock

**Trash.**

- On Windows: `IFileOperation` with `FOFX_RECYCLEONDELETE`, refusing any item that would be deleted permanently.
- Elsewhere: `QFile::moveToTrash` (`platform_trash.hpp`).
- Delete flows confirm (DELETE-01).
- Cross-volume moves remove the source only after the size check.

**Platform operations.** `xdg-open`, `$TERMINAL` and the fallback emulators, `wt.exe` and PowerShell (`platform_integration.cpp`). The image installs no file manager, terminal or `xdg-utils` (`Dockerfile:26`).

**Safe mode and recovery.**

- These flags exist in `app/startup_intent.cpp`: `--safe-mode`, `--reset-settings`, `--settings-backup`, `--settings-diff`, `--settings-restore`, `--settings-preview`, `--settings-scope` and `--settings-slot`.
- Lazy crash recovery is wired into the controller (SAFE-MODE-01, "Controller checkpoint and lazy-resume integration").
- The restore UI is B9.

**Docker.**

- The desktop size is limited to 560x400 through 3840x2160, with `VncAuth` only and `-AlwaysShared` (`docker/start.py`).
- `LIBGL_ALWAYS_SOFTWARE=1` (`Dockerfile:34`).
- `/photos` is mounted `read_only: true`, and `glancelot-config` is a named volume (`compose.yaml`).

**Monitor output.**

- MONITOR-OUTPUT-01 and MONITOR-CUSTOM-01 are `done`, and MONITOR-VERIFY-01 is `ready`.
- Native shader and DPR 1/2 state proofs are recorded in the queue row.
- Physical items are marked unverified.

**Other states and records.**

- DECODE-01 and CANVAS-UI are `integration`, and FS-WATCH-01 and SAFE-MODE-01 are `done`.
- Every cited verification file exists.

**Native Linux.** PLATFORM-01 and `tools/platform/LINUX-PACKAGE.md` support the experimental X11/XWayland package with no Wayland plugin, smoke-tested with software GL.

**Hardware items.** The unverified-hardware table labels every item unverified or not implemented. I found no hardware claim that is presented as verified.

**Queue SHAs.** I resolved 35 cited SHAs with `git cat-file -t`. All are commits, and all pass `git merge-base --is-ancestor <sha> origin/main`:

- 3b2b0db, ec05e42, 53a789b, 3cb96bf, d4eb4cc, 66dda8e, f22cbfe, d181d4f, 67d3f83
- cf40fcc, ed12ebe, f597012, e1c1c51, 209e703, 8c3189c, 2566014, fb698c0, a504baa
- b13b739, 44d9cf2, 079564b, a78b930, 259d337, 362c281, 9dccd78, 694ac2f, 5e05c6f
- e4a5a07, d1e5461, f7aa764, 319cdf4, 26f8a89, 75dd2d7, a1738fa, c266824

**Queue relations.**

- **Merge parents.** `3b2b0db` has parents `d632a53` and `cf40fcc`. `b13b739` has `44d9cf2` and `079564b`. `362c281` has `9dccd78` and `694ac2f`.
- **Queue-only commit.** `259d337` touches only `docs/WORK-QUEUE.yaml`.
- **Review range.** `2566014`, `a504baa`, `66dda8e`, `f597012` and `e1c1c51` lie inside `f7aa764..8c3189c`, and `fb698c0` is after `8c3189c`.
- **F054 commits.** The 14 listed commits match `git log --no-merges d181d4f..cf40fcc`.
- **Candidate commits.** They match `CLAUDE-STACK-SYNC-2026-10-22.md`.

**Requirement IDs.** These all exist in `docs/REQUIREMENTS.md`: UX-10, UX-09, UX-14, UX-17, UX-02, META-05, IN-01, IN-04, IN-05, UX-11, NAV-05, NAV-06, COLOR-01, COLOR-04, VIEW-04, IN-03, FMT-04, FMT-05, FMT-06, SYS-03 and PERF-03. The mappings match the Requirement column of `docs/FEATURE-OPPORTUNITIES.md`, including F009, F015 and F054 as "N".

**Other docs.**

- **README:** apart from findings 1, 10 and 13, the README edits are accurate. The "WSL core only" line is correctly replaced: `build.yml` runs `core` on `ubuntu-24.04` and `windows-2022`, and the Dockerfile runs the full suite offscreen.
- **TESTING.md:** apart from finding 11, accurate.
- **DOCKER.md:** apart from finding 9, accurate.
