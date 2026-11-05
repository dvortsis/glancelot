# B3 re-review: PR 9 delta after APPROVED

**Verdict: APPROVED**

Written for: the lead. Reviewer: a fresh-context Claude Opus 5.5 subagent that wrote none of this packet and neither earlier review.

| | |
| --- | --- |
| Reviewed head | `e09805df83a6084477ddcfc62b27dbf48a256a76` (PR 9, `claude/b3-export-ui`) |
| Diff reviewed | `git diff 1a8be76 e09805d` only. Everything else stands on `docs/verification/B3-REVIEW.md` (`02b29f7`) |
| Build | `.worktrees/review-b3-r2/build-review-b3-r2`, under a build slot, as `scripts/build.ps1` does it |
| PR checks at head | `guard`, `core` (both), `viewer` and `ci-ok` all pass |

All three of the lead's points are met and nothing blocks. The four findings below are low or for the lead to note.

## The lead's three points

| # | Asked for | Result | Evidence |
| --- | --- | --- | --- |
| 1 | A confirmed Replace never swaps an earlier export for a header-only file | **Met** | Probe table below; `nothingToExportLeavesTheTargetAlone` has the all-vanished and all-outside rows, and both fail with the fix reverted |
| 2 | A destination that can't be replaced says it may be open in another app, keeps the old file, and doesn't lead with the raw error | **Met** | `lockedDestinationSaysToCloseItAndKeepsTheFile` uses a real open handle and then retries successfully; it asserts the detail row is hidden |
| 3 | A test that really exercises `changed` | **Met** | `itemChangedAfterSnapshotIsSkipped` rewrites the file and asserts reason `changed`; `changedItemIsNamedWithItsReason` asserts the dialog's text |

## Requested checks

- **The option can't lose the existing file.** The guard returns before any commit call, and the only earlier exits are refusals, failures and cancels, none of which touch the destination. `cleanupTemp` removes only the random temp sibling. Confirmed by the probe below.
- **No race left in the empty case.** The decision uses `result.written` after the last item's revalidation and the final cancel check, and with zero rows there is no commit to race with. The service sets the option and reads the result, and does no check of its own.
- **Default callers unchanged, by inspection and probe.** The default is `false`, and the service is the only production caller of `exportSelection`. With the option off the engine still replaces the target with a header-only file. `selection_export` does not pin this: see finding 1.
- **The header grant holds.** The only change to `include/glancelot/selection_export.hpp` is the one option line.
- **The message appears only for that error.** It needs `Failed`, `overwrite` and the exact engine text for error 5 or 32. The `another error` and `not a replace` rows keep the `failed` status. On Linux the text can't occur, so the status doesn't either.
- **Translatable, catalog current.** All three new strings are in `qsTr`. Every one of the 65 `qsTr` strings in `ui/ExportDialog.qml` matches a `<source>` in the catalog's `ExportDialog` context, and that context has no extras. The catalog has 654 messages, none unfinished, and the removed "was saved, but it lists no items" string is gone from `ui`, `src`, `app`, `tests` and `translations`.
- **No fixed sleeps.** The delta adds no `qWait` or sleep, and every added `QTRY_*` has a 10000 ms deadline. The `changed` tests change the file's size, so they don't depend on timestamp resolution.
- **Layout.** The new result and error texts use the existing wrapping labels. "Show in folder" and the path are hidden when nothing was written, and focus goes to Done or stays with the existing error flow. I read application-interface-design for this and took no captures of the new states.

## Findings

No finding is blocking.

| # | Severity | Where | Finding | Suggested action |
| --- | --- | --- | --- | --- |
| 1 | Low, test gap | `tests/selection_export_tests.cpp` | **No engine test covers zero rows, with the option on or off.** Making the guard ignore the option (`if (result.written == 0)`) still passes `selection_export`, so a later change to default behaviour wouldn't be caught there. The service tests do cover the option on. | Add both cases to the core test the next time the engine is touched. That file isn't in this delta's grant, so I don't hold the PR for it. |
| 2 | Low, lead's decision | `src/app/selection_export_service.cpp:76`; `ui/ExportDialog.qml:146` | **Error 5 also means a read-only file.** A read-only destination with no handle open gave error 5 in a direct `MoveFileExW` probe, so its message would be "close it and try again", and retrying won't help. "May be open" keeps this truthful, and the mapping is what the lead asked for. | Leave it, or add "or read-only" to the message if this turns up in use. |
| 3 | Note, lead's decision | `src/app/selection_export_service.cpp:239` | **The rule goes beyond Replace.** The service always sets the option, so a new file with no rows is no longer created, and the dialog says "was not created". The earlier "was saved, but it lists no items" message is gone. The handoff records this under "Decisions taken" and calls it a one-line change to reverse. I agree with the choice. | Confirm or reverse. |
| 4 | Note | `src/app/selection_export_service.cpp:252`; `ui/ExportDialog.qml:415` | **A narrow wording miss, from reading the code.** If the destination is deleted between the Replace question and the answer, and every item is also lost, the dialog says "was not changed" about a file that no longer exists. No data is at risk. | None. |

Two more items for the grant record:

- `src/core/selection_export.cpp` isn't named in the grant. The option needs it, the change is 7 lines, and the handoff lists it.
- Error 32 goes beyond the lead's "error 5". It is a sharing violation, so the advice fits. Every real lock I tried gave error 5, so only an injected test row reaches 32.

This also answers the handoff's open question about which error a real lock gives: a `MoveFileExW` probe gave 5 with the target held open under each of three sharing modes (read and write, read only, none), and the old content stayed.

## Targeted tests (actual output, clean head)

```
Test project C:/src/Github/Glancelot/.worktrees/review-b3-r2/build-review-b3-r2
    Start  1: selection_export
1/3 Test  #1: selection_export .................   Passed    1.70 sec
    Start 47: translation_coverage_ui
2/3 Test #47: translation_coverage_ui ..........   Passed   48.05 sec
    Start 52: export_ui
3/3 Test #52: export_ui ........................   Passed   86.03 sec

100% tests passed out of 3
```

After the mutations were restored and the target rebuilt, `export_ui_tests.exe` reported `Totals: 55 passed, 0 failed, 0 skipped`. The full suite wasn't run locally, as briefed.

## Fail-before spot-check

Each edit was restored afterwards, and `git status` showed no changes.

| Mutation | Test | Result |
| --- | --- | --- |
| Engine guard removed (fix 1 reverted) | `export_ui` | **8 fail, 47 pass:** all 5 rows of `nothingToExportLeavesTheTargetAlone`, including `all vanished` and `all outside the root`, plus `nothingToExportCreatesNoNewFile`, `confirmedReplaceWithNothingToExportKeepsTheEarlierFile` and `nothingToExportSaysNoFileWasCreated` |
| Locked-file detection off (fix 2 reverted) | `export_ui` | **3 fail, 52 pass:** `aBlockedReplaceIsToldApartFromOtherFailures(error 5)` and `(error 32)`, and `lockedDestinationSaysToCloseItAndKeepsTheFile` |
| Guard ignores the option | `selection_export` | **Passes:** finding 1 |

One earlier run of the first mutation passed, and I discarded it. A stale copy of my own job was running at the same time, and the second copy's cleanup restored the source before the build, so that binary had the fix in it. The run in the table was on its own, and `git status` showed the edit still in place after the build.

## Engine probe (scratch code in `selection_export_tests.cpp`, not kept)

`status` 0 is `Written`, 1 `Cancelled` and 2 `DestinationExists`. The destination started as `earlier`. The eight cases that left it alone or created nothing logged no temp file left behind. The two where the file changed are cut off in my log at the first newline, so for those I saw the start of the new content but not the temp count.

| Case | Status | Written | Destination afterwards |
| --- | --- | --- | --- |
| Option off, replace, all gone | 0 | 0 | **Header-only file**, the default behaviour as before |
| Option on, replace, all gone | 0 | 0 | `earlier` |
| Option on, replace, empty item list | 0 | 0 | `earlier` |
| Option on, replace, path list | 0 | 0 | `earlier` |
| Option on, replace, cancelled before the start | 1 | 0 | `earlier` |
| Option on, replace, cancelled in the loop | 1 | 0 | `earlier` |
| Option on, replace, outside the root, no revalidation | 0 | 0 | `earlier` |
| Option on, new file | 0 | 0 | Not created |
| Option on, no overwrite, destination exists | 2 | 0 | `earlier` |
| Option on, replace, one survivor | 0 | 1 | Replaced with the new export |

## Not verified

- **`lupdate` wasn't available.** It isn't on `PATH` or under `.tools`, so I couldn't regenerate the catalog and compare. The string-set check above and the `translation_coverage_ui` pass stand in for that.
- **A real spreadsheet app.** The test and the probe hold the file with plain handles, not Excel.
- **Linux, locally.** The hosted `core (ubuntu-24.04)` job passes, but it doesn't run `export_ui`. The hosted `viewer` job is `windows-2022`, per `AGENTS.md`.
- **Captures of the new dialog states.** None taken.
- **The full gate on this head.** That is for the tester.
