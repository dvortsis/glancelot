# EN-11 handoff: gate stability under load

Written for: the lead and the fresh local Windows reviewer of EN-11. Track session S7 (cloud, Linux).

| | |
| --- | --- |
| Branch | `claude/en-11-gate-stability` |
| Base (contract) SHA | `3893c77a729511c4a6a8056e2787ba21c39fdfb3` (`origin/main` at start) |
| Code tip SHA | `7c9fe09` (handoff commits sit on top of it; review with `git diff 3893c77 <branch tip>`) |
| Qt used | conda-forge `qt6-main 6.8.3` at `/opt/qt683`. `aqt` got a proxy 403 from `download.qt.io` and `micro.mamba.pm`, so micromamba came from its GitHub release |
| Docker | not run (no Docker in the cloud session) |
| ui-design-skills | not used. No UI was changed (the only QML-adjacent change is one grid test precondition) |

## Commits

1. `e99b255` cmake: provisional target for EN-11. Adds `late_baseline_tests` to `cmake/FSWatchTests.cmake` (lead-owned; replace or move it as you like). It uses the same sources and `GLANCELOT_FS_WATCH_TESTING` define as `fs_watch_tests`, depends on `glancelot-decoder` and `fs_watch_decoder_fixture`, runs offscreen with a 120 s timeout, and writes `late-baseline-results.txt`.
2. `5d9b8fc` test: new `tests/late_baseline_tests.cpp` (deterministic regressions) and `src/app/shared_file_read.hpp` (the shared-delete read helper the sharing test uses).
3. `6a06dd4` fix: `src/app/viewer_controller.cpp`, the late-baseline branch of the monitor callback.
4. `119dbe6` fix: `src/app/metadata_parser.cpp`, `src/app/animation_controller.cpp`, which now use the shared-delete reader.
5. `9af0092` test: `tests/fs_watch_tests.cpp` (the `png()` fixture helper and one decode-count oracle) and `tests/grid_ui_tests.cpp` (`auditFailedCurrentPhotoCanBeRetriedFromGrid` only).
6. `d267a02` test: adds an explicit `<QPointer>` include for MSVC.
7. `b7a0e3f` test: stderr diagnostics, because QTest output does not reach CTest's log on Windows.
8. `7c9fe09` test: corrects the sharing test after the Windows evidence (`sharedReaderAllowsRenameAndDelete`) and fixes the comments in the readers, the helper and `png()`.

## Root cause 1: a late first scan dropped the user's newer request (confirmed, product bug)

`open()` waits up to 100 ms for the file watcher's first scan (the baseline) before decoding. If the scan is late, the `decodeStart` fallback decodes anyway and sets `decodeStartedWithoutBaseline`. When the late baseline then arrived, the old branch (viewer_controller.cpp:351 at base) ran:

```
decodeStartedWithoutBaseline=false;++generation;cancel();cacheReuseAllowed=false;scan(path,generation);decode(path,generation);
```

This caused three separate faults, all triggered by a background event:

- `++generation`: any user navigation already in flight (`openFolder`, `navigateDirectory`, next/previous, sort) was submitted with the old token. Its finish handler (`token==generation && sequence==navigationGeneration`, :1011 at base) silently dropped the result, so no status was set and the request was lost.
- `scan()`: this calls `navigationStop.request_stop()`, which cancelled the user's running search. It also takes the single pending-navigation slot, so it could replace a pending user job.
- The re-decode then always set the status line (and emitted `imageChanged` and `stateChanged`). That overwrote a user message that had already arrived, or arrived first.

This matches every load-only symptom:

- `folder_tree` :427 / :430: the status ended as the decoder's colour text instead of "No supported images" or "cannot be read".
- `folder_grid` :947: an `imageChanged` from the re-decode landed during the test's 80 ms window.
- `folder_grid` :958: the re-scan and re-decode `stateChanged` re-synced the live model and replaced the test's forced path spelling.

Why the runner makes it likely: the monitor's first scan also reads the parent folder. Under the runner that is a large `%TEMP%`, and it is slow under load, so the baseline misses the 100 ms window.

### Fix (watcher/first-scan region only)

A late baseline is now a background refresh:

- It keeps `generation` and never touches navigation (no `scan()`, no `request_stop`). The open's own listing job and any newer user request land normally. The re-scan was only needed because the generation bump had thrown away the open's listing job; without the bump, the listing semantics are the same as when the baseline is on time.
- Pixels:
  - If an image is shown and the cache entry for the path carries the worker-verified source token equal to `fs_watch::sourceToken(value.imageAfter)` (the revision the monitor registered), the shown pixels are proven current and nothing happens: no decode, no `imageChanged`, no status. The decoder and the monitor compute this token with the same `fs_watch::probe`.
  - Otherwise one decode runs with `cacheReuseAllowed=false`, as before. If an image was already shown, that decode is quiet: it updates the pixels but does not replace the status line (`quietDecode`).
  - If the fallback decode is still running, it restarts once and reports normally.
- Generation fencing for genuinely newer user requests is unchanged: `open()` still bumps the generation and cancels.

Two one-line edits sit just outside the strict watcher region, and both serve the quiet flag:

- `cancel()` resets `quietDecode`, so any retired decode retires its flag (open, fail, new navigation).
- In the decoded handler, `status=…` became `if(!std::exchange(quietDecode,false))status=…`. This line is three unchanged lines after EN-05's `cache.insert` hunk, so it should merge cleanly.

**Merge note for EN-05:** the proof line reads `cache.object(cacheKey(path))` and `->sourceToken` (the base `QCache`). After EN-05 it becomes:

```
const bool proven=!registered.isEmpty()&&!cache.reuse(cacheKey(path),registered).isNull();
```

with the `if` testing `!(shown&&proven)`, where `shown` reduces to `!loading&&imageReady`.

The navigation finish handler is unchanged. Its silent drop is correct for genuinely newer requests, and no background path bumps the generation while navigation is busy anymore: `reconcileObservation` already defers until navigation is idle.

## Root cause 2: `QSaveFile` rename refused with "Access is denied" (Windows, verified on hosted windows-2022)

`QFileDevice` error 10 is `RenameError`: `MoveFileEx(REPLACE_EXISTING)` over the PNG was refused. Right after a decode, the viewer opens the current image in-process:

- `MetadataParser::parse` (QImageReader, then a `QFile` header read)
- `AnimationController::load` (QImageReader, reading up to 64 frames)

Both failing fs_watch tests rewrite the image immediately after `imageReady`, which is exactly that window. External scanners (Defender, the indexer) can hold a just-written file the same way.

**Windows evidence** (hosted `viewer` job, [run 39894499556](https://github.com/dvortsis/glancelot/actions/runs/39894499556), stderr diagnostics):

```
late_baseline: replace over shared-delete reader: commit 10: Access is denied.
late_baseline: replace over plain QFile reader: commit 10: Access is denied.
```

My first hypothesis was wrong. The brief suggested that delete sharing would let the replace through, but Windows refuses a non-POSIX replace onto a name while **any** handle is open, shared-delete or not. **Production share mode therefore cannot fix `:298`/`:1023`.**

**Decision:**

- **Fixture writer: this is the fix for `:298`/`:1023`.** `png()` in `tests/fs_watch_tests.cpp` retries the whole write on `RenameError` for up to 2 s, with backoff from 10 ms to 200 ms, and logs each retry with `qInfo` (`generated PNG atomic commit refused; retrying`). This is test media generation, not an oracle or synchronization. A holder that never lets go still fails the test after 2 s, with the attempt count. The retry branch never runs on Linux, so it is unexercised here. The gate's `fs-watch-results.txt` shows whether and how often it fires.
- **Production readers: kept, with a narrower and verified justification.** `src/app/shared_file_read.hpp` `openSharedRead()` opens with the decoder worker's share mode (`CreateFileW` + `_open_osfhandle`, falling back to plain `QFile`). `MetadataParser` and `AnimationController` read through it. While the viewer reads an image, other programs can now delete it or rename it away (Explorer, editors that save by renaming the original to a backup). A plain `QFile` handle blocks both.
  - `late_baseline sharedReaderAllowsRenameAndDelete` asserts this, and records the replace results instead of asserting them.
  - Metadata still reports name and size for an unreadable file.
  - I checked that `QImageReader(&handleOpenedFile).format()` and `size()` equal the by-name results for jpg, jpeg, png, webp, tiff, bmp and a misnamed file.
  - The lead may instead revert `119dbe6` if this product benefit is not wanted in EN-11. The late-baseline fix does not depend on it.
  - Commit `119dbe6`'s message overstates the effect (it claims the replace is fixed). The comments were corrected in a later commit; the history was not rewritten.
- **Follow-ups, not done here:**
  - `AnimationController::load` holds the handle while it pre-decodes up to 64 frames. Shorten that hold, for example by reading the bounded file into memory first, if product saves over animated files matter.
  - `src/app/raw_preview.cpp` (inside the decoder process; Track D's file) opens with plain `QFile`.

## Deterministic tests (gates only, no sleeps as synchronization)

`tests/late_baseline_tests.cpp`:

- The monitor's first read of the folder is held by an explicit gate (`ControllerTestHooks::options.read`), so the baseline is late by construction and the production 100 ms fallback decodes.
- Lookahead discovery (`hooks.discover`) runs on the controller's single navigation thread. Holding it queues the user's `openFolder` behind it, so the open is in flight when the baseline is delivered (observed through `hooks.pending`).

Tests:

| Test | Interleaving | Asserts |
| --- | --- | --- |
| `lateBaselineKeepsInFlightFolderOpen` | baseline lands while `openFolder(bare)` is in flight (real decoder) | status "No supported images"; current image and pixels kept; exactly 1 `decode:begin` and 0 `imageChanged` when the source token is provable (2 decodes otherwise); the next `openFolder(does-not-exist)` reports "cannot be read" |
| `lateBaselineRefreshKeepsNewerMessage` | baseline lands after the user's message; registered revision unproven (fixture tokens) and pixels changed | pixels refresh to blue in exactly 2 decodes; the status still says "No supported images" |
| `lateBaselineDuringDecodeRestartsOnce` | baseline lands while the fallback decode runs (fixture, 60 s delay) | one restart, blue pixels, normal decoder status, first worker retired (regression guard; passes before and after) |
| `sharedReaderAllowsRenameAndDelete` | the image is held through `openSharedRead` and a QImageReader | rename-away and delete succeed. The replace result is logged, and on Windows so are the plain-`QFile` replace and rename results (not asserted) |

### Fail before (base controller; helper header present, readers unchanged)

```
PASS   : LateBaselineTests::initTestCase()
FAIL!  : LateBaselineTests::lateBaselineKeepsInFlightFolderOpen() 'viewer.status().contains("No supported images")' returned FALSE. (Untagged source assumed sRGB · display profile not yet managed)
   Loc: [/home/user/glancelot/tests/late_baseline_tests.cpp(99)]
FAIL!  : LateBaselineTests::lateBaselineRefreshKeepsNewerMessage() 'viewer.status().contains("No supported images")' returned FALSE. (sRGB fixture · display profile not yet managed)
   Loc: [/home/user/glancelot/tests/late_baseline_tests.cpp(141)]
PASS   : LateBaselineTests::lateBaselineDuringDecodeRestartsOnce()
PASS   : LateBaselineTests::sharedReaderAllowsAtomicReplace()   (since renamed and corrected, see root cause 2)
PASS   : LateBaselineTests::cleanupTestCase()
Totals: 4 passed, 2 failed, 0 skipped, 0 blacklisted, 46420ms
```

### Pass after (`6a06dd4` and later)

```
PASS   : LateBaselineTests::initTestCase()
QINFO  : LateBaselineTests::lateBaselineKeepsInFlightFolderOpen() source revision provable true
PASS   : LateBaselineTests::lateBaselineKeepsInFlightFolderOpen()
PASS   : LateBaselineTests::lateBaselineRefreshKeepsNewerMessage()
PASS   : LateBaselineTests::lateBaselineDuringDecodeRestartsOnce()
PASS   : LateBaselineTests::sharedReaderAllowsAtomicReplace()   (since renamed and corrected, see root cause 2)
PASS   : LateBaselineTests::cleanupTestCase()
Totals: 6 passed, 0 failed, 0 skipped, 0 blacklisted, 888ms
```

Stability: 20/20 passes idle, and 10/10 under 8 busy-loop CPU hogs on a 4-core container.

## Changes to existing tests, and why no oracle is weaker

- `fs_watch controllerLateCoalescedBaselineDecodesOnce(newer-open-wins)`: the expected decode count was `3`. It is now `bProvable?2:3`. b was decoded on the fallback, and its late baseline registers the same revision, so the proof makes the old third decode unnecessary; without a source token the count is still 3. The count stays exact and now also pins the no-waste behaviour. The fresh-pixels row is unchanged (2). **Reviewer: please confirm you accept this oracle change.**
- `folder_grid auditFailedCurrentPhotoCanBeRetriedFromGrid`: the forced path spelling is re-applied (`h.model.sync(snapshot); QCOMPARE(h.selected(),0);`) with no event dispatch between it and `QCOMPARE(pathAt(0), filePath())` and the activation. Any background `stateChanged` (a late scan, a monitor status) legitimately re-syncs the live model to the navigator's spelling, so the old precondition depended on timing. All original assertions and timeouts are unchanged.
- `fs_watch png()`: retry as described above (this is the fix for `:298`/`:1023`); every other line of the helper is the same.
- `folder_tree`, and `folder_grid auditReadyCurrentPhotoDoesNotReopen`: unchanged. They are fixed by the product change.

No timeout was changed anywhere.

## ctest output (Linux, offscreen, conda Qt 6.8.3)

Targeted, on `d267a02` (only test-side and comment changes followed; `late_baseline`, `fs_watch`, `metadata` and `animation` were re-run on `7c9fe09`: 4/4 passed):

```
1/9 Test  #9: folder_tree ......................   Passed    6.07 sec
2/9 Test #12: folder_grid ......................***Failed  143.10 sec
3/9 Test #23: fs_watch .........................   Passed   70.30 sec
4/9 Test #24: fs_watch_budget ..................   Passed   20.90 sec
5/9 Test #25: late_baseline ....................   Passed    0.94 sec
6/9 Test #26: fs_watch_ui ......................   Passed    1.43 sec
7/9 Test #31: cache ............................   Passed    2.25 sec
8/9 Test #33: metadata .........................   Passed    0.02 sec
9/9 Test #34: animation ........................   Passed    0.37 sec
89% tests passed, 1 tests failed out of 9
folder_grid: Totals: 73 passed, 1 failed, 4 skipped  (only externalRenameAndDeleteRefreshKeepSelection, baseline)
fs_watch:    Totals: 83 passed, 0 failed
folder_tree: Totals: 27 passed, 0 failed, 2 skipped
```

At base `3893c77` before any change: `folder_tree` 27/0, `fs_watch` 83/0, and `folder_grid` 73 passed with 1 failed (the same baseline function).

Full suite, once, on `9af0092`. The only later change is an `#include`.

```
 1/44 selection_export Passed     2/44 perf_trace Passed        3/44 navigation Passed
 4/44 directory_listing Passed    5/44 file_operations Passed   6/44 startup_window_geometry Passed
 7/44 viewer_acceptance Passed    8/44 thumbnails Passed        9/44 folder_tree Passed
10/44 pane_layout Passed         11/44 window_slots Passed     12/44 folder_grid ***Failed
13/44 input_ui ***Failed         14/44 pathbar_ui Passed       15/44 file_operation_service Passed
16/44 directory_model ***Failed  17/44 viewport Passed         18/44 themes Passed
19/44 platform_integration Passed 20/44 grid_search Passed     21/44 grid_density Passed
22/44 ui_settings Passed         23/44 fs_watch Passed         24/44 fs_watch_budget Passed
25/44 late_baseline Passed       26/44 fs_watch_ui Passed      27/44 favorites Passed
28/44 folder_history Passed      29/44 shortcuts_help Passed   30/44 filter Passed
31/44 cache Passed               32/44 io_scheduler Passed     33/44 metadata Passed
34/44 animation Passed           35/44 mutation Passed         36/44 copy_move Passed
37/44 format Passed              38/44 monitor_output Passed   39/44 recovery Passed
40/44 recovery_startup Passed    41/44 recovery_controller Passed 42/44 recovery_process Passed
43/44 monitor_customization Passed 44/44 monitor_customization_ui Passed
93% tests passed, 3 tests failed out of 44
Total Test time (real) = 417.53 sec
FAIL! DirectoryModelTests::selectionSurvivesRelistSortAndDropsRemovedKeys()   (baseline)
FAIL! GridUiTests::externalRenameAndDeleteRefreshKeepSelection()               (baseline)
FAIL! InputUiTests::treeRowAndPathBarMenusOpenAndMatch()                       (baseline)
```

The three failures are exactly the conda-Qt baseline functions listed in PHASE0-CYCLE1. There are no regressions. The budget suites passed on the first run, so no reruns were needed.

## What the Windows reviewer must run

From a read-only checkout at the branch tip, in `.worktrees/review-en-11`, with `build-review-en-11`:

1. Build. MSVC compiles all of it on hosted windows-2022 (see below).
2. `ctest -C Release -R "^late_baseline$" --output-on-failure`. The stderr lines `late_baseline: …` carry the evidence because QTest output does not reach CTest's log on Windows. Expect:
   - `in-flight open: provable 1 decodes 1 imageChanged 0 status "No supported images…"`
   - `replace over shared-delete reader: commit 10: Access is denied.` (recorded)
   - all six functions passing
3. `ctest -C Release -R "^(folder_tree|fs_watch|folder_grid|fs_watch_budget|fs_watch_ui|metadata|animation|cache)$" --output-on-failure` (targeted; `folder_grid` and `fs_watch_budget` respect the gate lock). In `fs-watch-results.txt`, count the `generated PNG atomic commit refused; retrying` lines.
4. Load reproduction (recommended, because it is the acceptance condition): run `folder_tree`, `folder_grid` and `fs_watch` together under load at **base `3893c77`** and at the **branch tip**, so the before/after evidence comes from the same machine. For example, start a full `scripts/build.ps1 -Test` in another worktree, or use the runner while it is idle and nothing else is gating.
5. Read the metadata and animation behaviour with a GIF or animated PNG and a JPEG with EXIF: the metadata panel format, size and orientation, and animation playback should be unchanged.
6. Decide whether to keep `119dbe6` (shared-delete readers) as described under root cause 2.
7. Acceptance still requires three consecutive full gates under the runner (`lead-windows-gate`). Only the lead can run those.

## Unverified

- The runner gates, Docker, and the retry branch in `png()` firing (it cannot fire on Linux).
- The fallback branch, a quiet decode when the source token is empty (FAT, exFAT or network shares without a file id or birth time), runs in `lateBaselineRefreshKeepsNewerMessage` through fixture tokens. It does not run on a real filesystem without identity.

## Hosted Windows feedback (`build.yml`, `viewer` job on windows-2022, MSVC)

- [Run 39891265117](https://github.com/dvortsis/glancelot/actions/runs/39891265117) (`9af0092`): build OK, 43/44. `folder_tree`, `folder_grid`, `fs_watch`, `fs_watch_budget`, `directory_model` and `input_ui` passed; only `late_baseline` failed, with no detail in the log.
- [Run 39894499556](https://github.com/dvortsis/glancelot/actions/runs/39894499556) (`b7a0e3f`, stderr diagnostics): build OK, 43/44. The failure was `sharedReaderAllowsAtomicReplace`, a wrong assumption about Windows replace semantics (quoted above). The late-baseline tests passed on Windows with `provable 1 decodes 1 imageChanged 0`. The test was then corrected to `sharedReaderAllowsRenameAndDelete`; its Windows run result is recorded below.
