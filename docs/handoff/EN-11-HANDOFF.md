# EN-11 handoff: gate stability under load

Written for: the lead and the fresh local Windows reviewer of EN-11. Track session S7 (cloud, Linux).

| | |
| --- | --- |
| Branch | `claude/en-11-gate-stability` |
| Base (contract) SHA | `3893c77a729511c4a6a8056e2787ba21c39fdfb3` (`origin/main` at start) |
| Code tip SHA | `d267a02` (this handoff is committed on top of it; review with `git diff 3893c77 <branch tip>`) |
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

## Root cause 2: `QSaveFile` rename refused with "Access is denied" (Windows; partly unverifiable here)

`QFileDevice` error 10 is `RenameError`: `MoveFileEx(REPLACE_EXISTING)` over the PNG was refused. Right after a decode, the viewer opens the current image in-process twice:

- `MetadataParser::parse` (QImageReader by file name, then a `QFile` header read)
- `AnimationController::load` (QImageReader by file name, reading up to 64 frames)

On Windows, `QFile` opens with `FILE_SHARE_READ|FILE_SHARE_WRITE` and no `FILE_SHARE_DELETE`. Both failing fs_watch tests rewrite the image immediately after `imageReady`, which is exactly that window. The decoder worker already opens with delete sharing (`openShared` in `src/worker/main.cpp`), and the monitor's probes use all three share flags.

**Decision: fix the production readers, and also harden the fixture writer.**

- **Production readers** (a real product issue: viewing must not make an editor's save fail). The new `src/app/shared_file_read.hpp` `openSharedRead()` opens with the worker's share mode through `CreateFileW` + `_open_osfhandle`. If the native open fails (for example a long path only QFile can reach), it falls back to plain `QFile`, so no file that was readable before becomes unreadable. Both readers now read through that one handle.
  - Metadata still reports name and size for an unreadable file, as before.
  - I checked that `QImageReader(&handleOpenedFile).format()` and `size()` equal the by-name results for jpg, jpeg, png, webp, tiff, bmp and a misnamed file. Format names come from content sniffing either way.
  - Not changed: `src/app/raw_preview.cpp` opens RAW files with plain `QFile`, but only inside the decoder process (Track D's file). Suggested follow-up: use `openShared` there.
- **Fixture writer** (`png()` in `tests/fs_watch_tests.cpp`): it now retries the whole write on `RenameError` for up to 2 s with backoff from 10 ms to 200 ms, logging each retry with `qInfo`. This is for external holders a test cannot control (Defender, the search indexer), which are a well-known cause of this exact error. It is media generation, not an oracle or synchronization. So that the retry cannot hide a regression in our own helper, `late_baseline sharedReaderAllowsAtomicReplace` asserts directly that a reader holding the file through `openSharedRead` does not block `QSaveFile::commit`. The test calls the helper itself; nothing would catch MetadataParser or AnimationController going back to a by-name QImageReader, so review should watch for that.

**Linux cannot reproduce this.** POSIX rename never blocks on open readers, so on Linux that test passes trivially. Windows verification is listed below.

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
| `sharedReaderAllowsAtomicReplace` | `QSaveFile` commit over a file held through `openSharedRead` and a QImageReader | commit succeeds, new pixels read back. On Windows it also logs (does not assert) whether a plain `QFile` reader blocks the replace |

### Fail before (base controller; helper header present, readers unchanged)

```
PASS   : LateBaselineTests::initTestCase()
FAIL!  : LateBaselineTests::lateBaselineKeepsInFlightFolderOpen() 'viewer.status().contains("No supported images")' returned FALSE. (Untagged source assumed sRGB · display profile not yet managed)
   Loc: [/home/user/glancelot/tests/late_baseline_tests.cpp(99)]
FAIL!  : LateBaselineTests::lateBaselineRefreshKeepsNewerMessage() 'viewer.status().contains("No supported images")' returned FALSE. (sRGB fixture · display profile not yet managed)
   Loc: [/home/user/glancelot/tests/late_baseline_tests.cpp(141)]
PASS   : LateBaselineTests::lateBaselineDuringDecodeRestartsOnce()
PASS   : LateBaselineTests::sharedReaderAllowsAtomicReplace()
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
PASS   : LateBaselineTests::sharedReaderAllowsAtomicReplace()
PASS   : LateBaselineTests::cleanupTestCase()
Totals: 6 passed, 0 failed, 0 skipped, 0 blacklisted, 888ms
```

Stability: 20/20 passes idle, and 10/10 under 8 busy-loop CPU hogs on a 4-core container.

## Changes to existing tests, and why no oracle is weaker

- `fs_watch controllerLateCoalescedBaselineDecodesOnce(newer-open-wins)`: the expected decode count was `3`. It is now `bProvable?2:3`. b was decoded on the fallback, and its late baseline registers the same revision, so the proof makes the old third decode unnecessary; without a source token the count is still 3. The count stays exact and now also pins the no-waste behaviour. The fresh-pixels row is unchanged (2). **Reviewer: please confirm you accept this oracle change.**
- `folder_grid auditFailedCurrentPhotoCanBeRetriedFromGrid`: the forced path spelling is re-applied (`h.model.sync(snapshot); QCOMPARE(h.selected(),0);`) with no event dispatch between it and `QCOMPARE(pathAt(0), filePath())` and the activation. Any background `stateChanged` (a late scan, a monitor status) legitimately re-syncs the live model to the navigator's spelling, so the old precondition depended on timing. All original assertions and timeouts are unchanged.
- `fs_watch png()`: retry as described above; every other line of the helper is the same.
- `folder_tree`, and `folder_grid auditReadyCurrentPhotoDoesNotReopen`: unchanged. They are fixed by the product change.

No timeout was changed anywhere.

## ctest output (Linux, offscreen, conda Qt 6.8.3)

Targeted, on tip `d267a02`:

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

1. Build. MSVC must compile `src/app/shared_file_read.hpp` (`CreateFileW`, `_open_osfhandle`, `_close`) and `tests/late_baseline_tests.cpp`. The hosted `build.yml` `viewer` job result for this branch is recorded below.
2. `ctest -C Release -R "^late_baseline$" --output-on-failure`. Check in `late-baseline-results.txt`:
   - `sharedReaderAllowsAtomicReplace` **passes on Windows**. This is the only real proof that the delete-sharing handle lets `QSaveFile::commit` replace a held file. If it fails, the production reader fix does not help on this Windows build: report it, and the fixture retry becomes the only mitigation.
   - Record the logged line `plain QFile reader blocks replace: true|false`. `true` confirms the mechanism behind `:298` and `:1023`.
   - `lateBaselineKeepsInFlightFolderOpen` logs `source revision provable true` on NTFS (identity uses the file index and creation time), so the quiet path, with 1 decode and 0 `imageChanged`, is what Windows exercises.
3. `ctest -C Release -R "^(folder_tree|fs_watch|folder_grid|fs_watch_budget|fs_watch_ui|metadata|animation|cache)$" --output-on-failure` (targeted; `folder_grid` and `fs_watch_budget` respect the gate lock). Check that `fs-watch-results.txt` either has no `generated PNG atomic commit refused; retrying` lines or records how many there were.
4. Load reproduction (recommended, because it is the acceptance condition): run `folder_tree`, `folder_grid` and `fs_watch` together under load at **base `3893c77`** and at the **branch tip**, so the before/after evidence comes from the same machine. For example, start a full `scripts/build.ps1 -Test` in another worktree, or use the runner while it is idle and nothing else is gating.
5. Read the metadata and animation behaviour with a GIF or animated PNG and a JPEG with EXIF: the metadata panel format, size and orientation, and animation playback should be unchanged.
6. Acceptance still requires three consecutive full gates under the runner (`lead-windows-gate`). Only the lead can run those.

## Unverified

- Windows share-mode behaviour (above), the MSVC build of the new code (pending the hosted run, below), Docker, and the runner gates.
- The fallback branch, a quiet decode when the source token is empty (FAT, exFAT or network shares without a file id or birth time), runs in `lateBaselineRefreshKeepsNewerMessage` through fixture tokens. It does not run on a real filesystem without identity.

## Hosted Windows feedback (`build.yml`)

- Run [39891265117](https://github.com/dvortsis/glancelot/actions/runs/39891265117) (`9af0092`) and run [39891322833](https://github.com/dvortsis/glancelot/actions/runs/39891322833) (`d267a02`). Results: see the section below.
