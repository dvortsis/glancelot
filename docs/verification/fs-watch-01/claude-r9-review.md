# FS-WATCH-01 independent Claude-lane review R9 (static only)

Candidate: `825f7f2150f2d513d9cf4f44ce4b932f0452e8ab` (fixed), reviewed in `claude-review-fs-watch-01-r9` by the Claude lane,
independent of the Codex implementing lane. Inspected diff: `git diff f20711e..825f7f2`. Source-touching files are only
`src/app/directory_entry_model.cpp` (+3/-3) and `tests/fs_watch_tests.cpp` (+5/-1). The rest is docs/evidence
(FS-WATCH-01.md, the Cache-integration handoff note, preserved `claude-r8-review.md`, `windows-r9-*` and
`windows-r10-unique-final-size.txt`). No CMake, header, QML or queue change. Prior reports `claude-r7-review.md` and
`claude-r8-review.md` are preserved unchanged.

**Static review only.** I ran no test, compile, Docker build or native app, and edited no product, test, queue, config
or other-worktree file. Everything below comes from reading source and the author's saved output, not from my own execution.
The candidate still carries only the f708068 Cache contracts. I did not merge main (the Cache engine); its owner holds
the integration lock.

## Diff assessment
1. **Applied marker (G5).** `publicationStats.applied/filtered` moved from the top of `apply()` into the
   `if (!truncated || ...)` publication guard, under `GLANCELOT_FS_WATCH_TESTING`. `applied` now means "the model
   publication/projection path ran". A retained truncated listing is not counted. The pump handler still resets the stats
   first, so stale and cancelled results stay `applied=false`. The duplicate `#ifdef` pair was removed. In production builds
   the block compiles out: no behaviour, ABI or header change. G5's naming half is resolved.
2. **Unique final size (G4).** The writer takes `sequence=filteredWrites.load()` and writes `QByteArray(128+sequence,'x')`.
   Sizes are strictly increasing per write, so the final expected size equals no earlier replacement. They are also all >=128 bytes,
   so they differ from the unfiltered phase's short `"N generated changed bytes"` content. `filteredSize` is stored after
   a successful `commit()`, before the counter increments. The test joins the writer before reading it, so there is no race.
   The 4096 cap sets `filteredFailure`, which is asserted. At about 25 ms per write over the 2 s window it is unreachable. The final `QTRY`
   (30 s, unchanged) now passes only once the model row shows the last replacement's size, so it proves convergence to the final content.
   G4 is resolved.
3. **Lifetime.** The atomics are declared before the `jthread`, so they outlive it on early `QVERIFY` returns, and `join()` is
   explicit before the read. No lifetime issue.
4. **Unchanged oracles.** Heartbeat gap, publication `<250`, filter change `<250`, filtered publication `<250` and `QTRY` 30 s
   are unchanged. So are the full last-complete-baseline assertion (`observed 0 / accepted 1`), the all-publication
   `<250` loop, and the final selection/cursor/hash rename assertions after `clearFilter()`. The filtered phase calls no
   `refresh()`: publications come from the real `QSaveFile` writer through the monitor path. The only `refresh()` calls
   are the initial one (line 220) and the post-cleanup rename (line 300), which are the pre-existing baseline steps.
5. **Not claimed.** The docs state this is a model-only regression and do not claim QML delegate/layout timing. That is
   the correct scope; G5's measurement-scope caveat remains an honest limitation, not a defect.

## Evidence read (author-run, not mine)
- `windows-r10-unique-final-size.txt`: FSWatchTests 3 passed, 0 failed, 0 skipped, 84285 ms on 825f7f2. Filter change 33 ms, filtered
  publications 85/70/62 ms, filtered gap 86 ms, unfiltered max callback 160 ms, GUI gap 161 ms. All below 250 ms, but the
  unfiltered 160 ms leaves about 90 ms headroom on a host the author discloses as shared. Only three filtered
  publications were sampled, so this is bounded-cost evidence, not a margin proof.
- `windows-r9-full-gate.txt`: 25/25 passed, 403.85 s, f20711e source, with `fs_watch` 149.52 s, `fs_watch_ui` 1.83 s and
  `folder_grid` 151.63 s. **This is f20711e, not 825f7f2.** The author states this, and I agree the 25-target pass must not be
  relabelled as a full gate of the later test revision. 825f7f2 has only the targeted 100k test run on Windows. Qt CTest
  stdout is empty, so no per-case or zero-skip count is inferred from the full gate.
- `windows-r9-ui.txt`: `fs_watch_ui_tests` 3/3, 0 failures, 0 skips, 1472 ms, via the real QML key route (preview/grid/combined F5,
  text-input and settings-modal suppression). Offscreen/software with "No QRhi found" warnings. Pre-existing
  `ShortcutCheatSheet.qml:327 keys` and `PhotoGrid.qml:298` warnings are not FS-WATCH changes and are not claimed fixed.
- UI scope: the F5 contract is real QML key events, which this evidence covers. Physical keyboard, GPU/DPI and focus-ring visuals are
  separate qualification that nobody has run; I do not treat that as a blocker and add no new visual design requirement.

## Findings
No blocker. No major. No new finding.

**Minor (carried, not waived)**
- G2: the final rename-follow assertions were never shown red against a baseline-replacing mutation. The budget assertion that
  guards the same mechanism was shown red by the author.
- G3: a stale or cancelled `Listing` is destroyed on the GUI thread. It is bounded by the row cap and inside the asserted `<250 ms`
  publication window.
- Real QML GridView delegate/relayout cost under an active filter at 100k rows is unmeasured. The filtered refresh still resets the whole
  model (inherited behaviour).
- Timing assertions ran on shared hosts: no exclusive-host qualification.
- Bound, not a bug: roughly 35k+ entries per folder get no rename tracking (16 MiB identity budget).

## Unexecuted by me
All of: Windows build/CTest, the 100k test, `fs_watch_ui_tests`, Linux/Docker, native app, QML F5, and any
combined-with-Cache build or test.

## Disposition
- **Implementation/source disposition:** the f20711e..825f7f2 delta has no blocking issue. It is a test-only oracle
  strengthening plus a marker relocation, with production behaviour unchanged. With R7 (no blocking implementation issue; own
  Windows backend 52/52 zero skips and Linux QML F5 3/3) and R8, I consider the FS implementation acceptable for integration, subject to the gates below.
- **Final packet approval: NOT GIVEN.** The packet is not done and no done claim is made.

## Remaining final gates
1. Full Windows `scripts/build.ps1 -Test` (25 targets) on the exact integrated source. The existing pass covers f20711e only.
2. Current-source Linux/Docker run (author's is in flight) and `docker build -t glancelot-browser:main .` on the integrated source.
3. Serial integration under the lead lock: merge current main (Cache engine 8b28060), reconcile revision validation and
   invalidation with the Cache hit path (the handoff note records `cacheKey` from path/size/mtime-ms only), and add generated
   cross-folder replacement tests.
4. Independent review of the combined source, then checks on the integrated commit.
5. Unverified by anyone: physical keyboard/GPU, network/coarse-timestamp/ReFS/cloud filesystems, registration ABA.
