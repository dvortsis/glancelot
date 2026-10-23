# EN-11 review: gate stability under load

**Verdict: CHANGES REQUIRED.** There is one blocking item. The new regression guard `late_baseline lateBaselineDuringDecodeRestartsOnce` is racy on Windows: it failed 4 of 6 idle runs here. The product fix, its root-cause analysis, the fail-before evidence, the fixture-writer retry and the oracle change are all sound, and I would approve them once that test is made deterministic.

Written for: the lead and the EN-11 fix session. Reviewer: a fresh-context Claude subagent on the lead's Windows PC that did not implement EN-11.

| | |
| --- | --- |
| Branch reviewed | `origin/claude/en-11-gate-stability` at `ada9e42` (code tip `7c9fe09`) |
| Base | `3893c77` (from the handoff; it is `git merge-base origin/main <tip>`) |
| Diff | `git diff 3893c77 ada9e42`: 9 files, +559 / -21 |
| Windows scratch state | `.worktrees/review-en-11`: `ada9e42` merged with `origin/main` `c903e6f` (local merge commit `6c90726`, not pushed), plus the two adaptations under "Integration notes". Built with `scripts/build.ps1 -BuildDir build-review-en-11` inside `build-slot.ps1`. |
| Base build for load comparison | `.worktrees/review-en-11-base` at `3893c77`, `build-review-en-11-base` |
| Toolchain | MSVC 2022, Qt 6.8.3 msvc2022_64 (project-local), Windows 11 Pro 26200, 20 logical CPUs |

## Blocking

### B1. `lateBaselineDuringDecodeRestartsOnce` races the fixture's config read (Windows: 4 of 6 idle runs fail)

```
FAIL!  : LateBaselineTests::lateBaselineDuringDecodeRestartsOnce() Compared values are not the same
   Actual   (decodes): 1
   Expected (2)      : 2
C:\src\Github\Glancelot\.worktrees\review-en-11\tests\late_baseline_tests.cpp(179) : failure location
```

Results with the fix in place, idle, running `late_baseline_tests.exe` directly: runs 1 and 2 passed, runs 3 to 6 failed, all on this function. Under `ctest --repeat until-fail:3` inside the gate lock, run 1 passed and run 2 failed. The function also fails with the fix reverted. It is a test defect, not a product regression.

Cause:
- `decodes` counts `workerStarted`, which fires on `QProcess::started`. That is before `fs_watch_decoder_fixture` has run `main()` and read `--fixture-config` (tests/fs_watch_decoder_fixture.cpp:27-37).
- The test overwrites `worker.json` with `{"color":"blue"}` immediately after `decodes==1`.
- Process start-up on Windows (Qt DLL loading) is slow, so the fixture often reads the new config. Decode 1 then has no 60 s delay, finishes blue on its own, and `imageReady&&!loading` holds with `decodes==1` before the baseline is even released.
- Linux start-up is fast enough that the old config is almost always read first, which is why the cloud session and the one green hosted run did not see it.

This is the only Windows-unsafe interleaving in the file. `lateBaselineRefreshKeepsNewerMessage` rewrites the config only after decode 1 has finished and the second worker has not started, so it is safe.

Required: make the handoff deterministic. Do not rewrite a file the running fixture may not have read yet. Two acceptable ways:
- Give the second launch a second config file by changing `hooks.decoderArguments` after `decodes==1`. `decode()` reads `testHooks->decoderArguments` at each launch, and `HookScope` points the controller at this `hooks` object, so the first process keeps its 60 s config.
- Or have the fixture signal that it has read its config (for example a marker file written after the read) and wait on that before rewriting.

Then rerun `late_baseline` at least 10 times on Windows and record the result.

A new flaky test in a packet whose acceptance is "suites pass reliably in full gates" would defeat the packet, so this blocks.

## Review findings (non-blocking)

### Root cause 1 (late baseline drops the newer request): convincing, and reproduced here

- At base, the late-baseline branch (`++generation;cancel();cacheReuseAllowed=false;scan(path,generation);decode(path,generation);`) bumped the generation from a background event. That made the navigation finish handler's `token==generation` check drop any user navigation already submitted. `scan()` also stopped the user's search and took the pending slot, and the re-decode overwrote the status line. Each listed symptom (`folder_tree` :427/:430, `folder_grid` :947/:958) maps onto one of these effects.
- **Base reproduction under load on this PC:**
  - With 20 busy loops, base `fs_watch` failed `replacementAndUnrelatedChanges` at :1021, `QCOMPARE(images.size(),0)` with actual 1. That is an `imageChanged` from the late-baseline re-decode, the same effect as `folder_grid` :947.
  - The tip passed the same run 2 of 2.
  - Pure CPU load did not reproduce `folder_tree` :427/:430 at base (base 2/2 pass). The handoff names slow parent-folder (`%TEMP%`) scans as the trigger, and CPU hogs alone do not cause those.

### The fix keeps generation fencing and adds no stale-result path

- `open()` still bumps `generation` and calls `cancel()`. A newer user request still retires the old decode, because every decode callback checks `token!=generation||process!=worker`.
- The late baseline now never bumps the generation or touches navigation. The open's own listing job is still current, so the old re-scan is unnecessary. The listing-versus-registration window is the same as when the baseline is on time, because `open()` submits the scan without waiting for the baseline.
- The proof that skips the decode compares the cache entry's worker-verified source token with `fs_watch::sourceToken(value.imageAfter)`. The token hashes volume, file id, birth time, size, mtime, ctime and link count, the same token EN-05's revision reuse already trusts. It is computed from the observation without I/O on the GUI thread.
- `quietDecode` is set only for a shown image. `cancel()` clears it, so open, fail and navigation all retire it. The decoded handler consumes it with `std::exchange`. A failing quiet decode still reports its error through `fail()`, which is correct.
- Pre-existing behaviour, not introduced here: when the fallback decode is still running, its normal status can still overwrite a navigation message that arrived first. The same happens when no baseline arrives at all.

### Fail-before on Windows (reverting only the late-baseline branch in the scratch copy)

Revert diff: the 13-line EN-11 block in the monitor callback was replaced by the base one-liner. `quietDecode` plumbing stays, so it compiles. Only `late_baseline_tests` and `fs_watch_tests` were rebuilt.

```
FAIL!  : LateBaselineTests::lateBaselineKeepsInFlightFolderOpen() 'viewer.status().contains("No supported images")' returned FALSE. (Untagged source assumed sRGB · display profile not yet managed)
   late_baseline_tests.cpp(112)
FAIL!  : LateBaselineTests::lateBaselineRefreshKeepsNewerMessage() 'viewer.status().contains("No supported images")' returned FALSE. (sRGB fixture · display profile not yet managed)
   late_baseline_tests.cpp(156)
FAIL!  : FSWatchTests::controllerLateCoalescedBaselineDecodesOnce(newer-open-wins) Compared values are not the same
   Actual   (trace.readAll().count("decode:begin\n")): 3
   Expected (newerOpen?(bProvable?2:3):2)            : 2
   fs_watch_tests.cpp(786)
```

With the fix restored, `controllerLateCoalescedBaselineDecodesOnce` passes both rows. `lateBaselineKeepsInFlightFolderOpen` reports `provable 1 decodes 1 imageChanged 0 status "No supported images in that folder or its subfolders."`. Both new late-baseline tests fail before and pass after, so they meet the acceptance. The tests hold explicit gates on the monitor's first read and on the single navigation thread, with 2 ms polling of atomics inside the hooks, not sleeps used as synchronization.

### Oracle change in `fs_watch` (`newer-open-wins`: `3` became `bProvable?2:3`): accepted

The count stays exact and can still fail: it fails with 3 decodes when the fix is reverted, shown above. It now also pins the behaviour that a proven-current image costs no extra decode. The `fresh-pixels` row still requires the validating decode when pixels changed. No timeout was changed anywhere in the diff.

### Root cause 2 and the fixture writer: accepted

- Windows evidence here matches the hosted run: `replace over shared-delete reader: commit 10: Access is denied.` and `replace over plain QFile reader: commit 10: Access is denied.` Delete sharing cannot fix a `MoveFileEx(REPLACE_EXISTING)` onto an open name.
- The bounded retry in `fs_watch` `png()` (2 s, 10 to 200 ms backoff, retries only on `RenameError`, logged with `qInfo`, still fails with an attempt count) makes test-media generation robust. It does not weaken an oracle.
- **Observed firing:** in the tip-under-load run 1, `fs-watch-results.txt` logged `QINFO : FSWatchTests::mutationAdapterRearmsMonitorAndPreservesNewestOpen() generated PNG atomic commit refused; retrying 1 "Access is denied."`, and the suite passed. The idle targeted runs logged 0 retries (last results file kept).
- `folder_grid auditFailedCurrentPhotoCanBeRetriedFromGrid`: re-applying the forced spelling right before the assertions keeps every original assertion and timeout. It removes the dependency on there being no background `stateChanged`. Accepted. It edits an existing test in `tests/grid_ui_tests.cpp`, which the hot-file rule allows because it adds no new test there.

### Shared-delete readers (`119dbe6`): keep, with the notes below

- `openSharedRead` opens with `FILE_SHARE_READ|WRITE|DELETE` and `GENERIC_READ` only. It never writes, so "viewing never writes original media" holds. It falls back to a plain `QFile` when the native open fails. `sharedReaderAllowsRenameAndDelete` passed in every run here: rename-away and delete under a held reader succeed. With a plain `QFile`, rename-away is `refused`.
- On Windows the handle-opened `QFile` has no file name, so `QImageReader` gets no suffix hint. The handoff's format check ran on Linux, where the fallback path keeps the name, so I checked the nameless path on Windows with a scratch probe. The probe compared `QImageReader(path)` against `QImageReader(&openSharedRead file)` for format, size, transformation, imageCount, supportsAnimation, loopCount, nextImageDelay, first-frame pixels and frame count.
  - Files covered: png, jpg, jpeg, bmp, tif, tiff, webp, ico, wbmp and a 2-frame GIF; `.apng`; PNG named `.jpg` and `.tga`; TGA named `.png`; TIFF named `.dng`; uncompressed 24- and 32-bit TGA and RLE TGA; TGA with no extension; and EXIF JPEGs with orientation 3, 6 and 8.
  - Result: `diffs 0 of 24`. Every field was identical, including transformation (orientation) and GIF frames and delays.
  - Side note, unchanged by EN-11: Qt 6.8.3 identifies a minimal uncompressed TGA with no ID field as `ico` (CUR signature `00 00 02 00`) by name and by handle alike.
- `metadata` and `animation` suites pass on the merged tip on Windows.
- Follow-ups, already listed in the handoff:
  - `AnimationController::load` holds the handle across up to 64 frames.
  - `raw_preview.cpp` still opens with a plain `QFile`.
  - Commit `119dbe6`'s message overstates the effect. The comments were corrected later.

### Grant and region check

`git diff --name-only 3893c77 ada9e42`:

| File | Assessment |
| --- | --- |
| `src/app/viewer_controller.cpp` | Watcher/first-scan region (monitor callback) plus two one-line edits outside it: `cancel()` resets `quietDecode`, and the decoded handler's `status=` line. Both serve the quiet flag and are justified in the handoff. The navigation finish handler is unchanged. |
| `src/app/metadata_parser.cpp`, `src/app/animation_controller.cpp`, `src/app/shared_file_read.hpp` (new) | Outside the two `viewer_controller` regions the brief names. They are not hot files and not in another track's ownership row. The lead decides whether `119dbe6` belongs in EN-11; the late-baseline fix does not depend on it. |
| `cmake/FSWatchTests.cmake` | Lead-owned. The handoff flags it as a provisional target for the lead to keep or move. |
| `tests/fs_watch_tests.cpp`, `tests/grid_ui_tests.cpp`, `tests/late_baseline_tests.cpp` (new) | Test-only, as described above. |
| `docs/handoff/EN-11-HANDOFF.md` | Handoff. |

Overlap with other packets' regions (`git merge-tree --write-tree` of EN-11 against each tip, all clean):
- `claude/stack-f040-pinned` touches `viewer_controller.cpp` around :113, :405 and :1267, not EN-11's lines.
- `claude/stack-f015-recovery` touches the tab regions and one line in the navigation finish handler (`tabSettling=false;` in the cleared-image branch). EN-11 leaves that handler unchanged.
- `stack-f049-presets` and `stack-f054-coverage` do not touch these files.
- EN-06's emit region (`trigger()`, now on main) is untouched.

Handoff format: it has commits, root causes, tests and output. Against the current AGENTS.md pre-handoff checklist, it lacks the literal grant-check output and the "Decisions taken" / "Open questions for the lead" headings, although the decisions (keep or revert `119dbe6`, the oracle change, the provisional target) are written inline. The fix session should add them.

## Integration notes for the lead (required adaptations, not defects)

1. **The merge with main is clean as text but does not compile.** EN-05's `DecodeCache` on main has no `object()`. I applied the handoff's own merge note in the scratch copy, and it builds and passes:
   ```
   const bool shown=!loading&&imageReady;
   const bool proven=!registered.isEmpty()&&!cache.reuse(cacheKey(path),registered).isNull();
   if(!(shown&&proven)) {
   ```
   `git merge-tree --write-tree --name-only origin/main ada9e42` gives exit 0, tree `1d345c6`.
2. **EN-03 label.** Add `glancelot_test_labels(late_baseline LABELS process)` next to the target (it starts `glancelot-decoder` and the fixture). With that line, `ctest -N -L process -R ^late_baseline$` lists it and it is also computed `fast`. It is not a budget test: it has no wall-clock assertion, and the 15 s values are QTRY ceilings.
3. On main, `fs_watch_backend_sources` already expands `${GLANCELOT_VIEWER_SOURCES}`, so the provisional target picks up `decode_cache.cpp` without edits.

## Windows results (merged scratch, 6c90726 + adaptations)

Gate-lock block `review/en-11`, PATH prefixed with `.tools\python\Scripts` and `.tools\Qt\6.8.3\msvc2022_64\bin`. QTest output does not reach CTest's log on this machine, so per-function detail comes from the `-o` results files.

| Run | Result |
| --- | --- |
| `ctest -R "^late_baseline$" --repeat until-fail:3 -V` | Run 1 passed, run 2 **failed** (`lateBaselineDuringDecodeRestartsOnce` :179, decodes 1). See B1. |
| `late_baseline_tests.exe` x6, idle, fix restored | 2 pass, 4 **fail**, all on `lateBaselineDuringDecodeRestartsOnce` :179. The other five functions pass every time. |
| `ctest -R "^(folder_tree\|folder_grid\|fs_watch\|fs_watch_budget\|viewer_acceptance)$" --repeat until-fail:3 --output-on-failure` (run with `-V`) | **All 15 passed** (exit 0): viewer_acceptance 13.8/12.0/9.7 s, folder_tree 9.8/9.4/9.4 s, folder_grid 206.7/199.4/217.7 s, fs_watch 89.1/87.4/85.8 s, fs_watch_budget 81.0/85.9/93.8 s. Last `fs-watch-results.txt`: `82 passed, 0 failed`, 0 retry lines. |
| `ctest -R "^(fs_watch_ui\|metadata\|animation\|cache)$" --output-on-failure` (handoff step 3) | 4/4 passed |
| Handoff step 2 evidence lines | `in-flight open: provable 1 decodes 1 imageChanged 0 status "No supported images…"`; `replace over shared-delete reader: commit 10: Access is denied.` (recorded); `rename away under plain QFile reader: refused` |
| Fail-before (late-baseline branch reverted) | See above: both new late-baseline tests fail, and the changed `fs_watch` oracle fails 3 vs 2 |
| Reader format probe (Windows nameless handle vs by name) | 0 differences in 24 files |

**Load reproduction** (handoff step 4): 20 busy-loop `pwsh` processes on 20 logical CPUs, inside the same gate-lock block. Base `3893c77` and tip were interleaved, two iterations each, `ctest -R "^(folder_tree|folder_grid|fs_watch)$"`.

| | folder_tree | fs_watch | folder_grid |
| --- | --- | --- | --- |
| base, run 1 | pass 29/29 | pass 82/82 | Timeout 412 s (`virtualizedAndHidden` :980) |
| tip, run 1 | pass 29/29 | pass 82/82 (one `png()` retry fired) | Timeout 412 s (`activationAndSingleClick` :1030 needed 11.2 s of a 5 s QTRY) |
| base, run 2 | pass 29/29 | **fail** 81/82: `replacementAndUnrelatedChanges` :1021, stray `imageChanged` | Timeout 410 s |
| tip, run 2 | pass 29/29 | pass 82/82 | Timeout 412 s (`virtualizedAndHidden` :984) |

Twenty hogs over-subscribe the machine too much for `folder_grid` at both base and tip, with budget and QTRY timing failures unrelated to EN-11, so that column does not discriminate. `fs_watch` reproduced a root-cause-1 effect at base and stayed green at tip. Under this CPU-only load, `folder_tree` did not reproduce at base.

Not run by this review: three consecutive full gates under the runner (`lead-windows-gate`; the lead's acceptance step), Docker, and a manual GUI check of the metadata panel and animation playback. The reader probe and the `metadata`/`animation` suites cover the same fields headlessly. The runner gates are still required for EN-11 acceptance.

## What the fix session must do

1. Make `lateBaselineDuringDecodeRestartsOnce` deterministic as described in B1. Show it passing at least 10 of 10 on Windows (hosted `build.yml` repeats or a local rerun), and still failing or passing as before with the fix reverted.
2. Add the grant-check output and the "Decisions taken" / "Open questions for the lead" sections to the handoff.
3. Optionally apply the `DecodeCache` adaptation and the `late_baseline` label on a merge of main, so the lead does not have to.

A new fresh reviewer re-reviews the fix. Everything else in this document stands.
