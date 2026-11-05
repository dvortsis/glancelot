# COMPARE-PANES contract

Status: **frozen** by PHASE2-CONTRACTS. Header: [`include/glancelot/compare_panes.hpp`](../../include/glancelot/compare_panes.hpp), the authoritative text of the interface. Code references are at `b1d57cd`. Depends on RESOURCE-LEDGER, and starts after PR 3 (`claude/stack-confirm-fix`) merges, because PR 3 changes the transfer path. Owner: Track C. Implementing packet: C3 (F026). Decisions and their alternatives: [2026-11-05-phase2-contracts.md](../decisions/2026-11-05-phase2-contracts.md).

## Purpose

Bring 2-up and 4-up comparison into the main window (ADV-01): linked or independent pan and zoom, an active candidate, candidate replacement and removal. Memory budgets include every pane, and **removing a pane never deletes a file**. The out-of-window precedent is the presentation comparison: `IPresentationWindow::setComparisonMode`, `setPrimaryImage`, `setSecondaryImage`, `viewportState`, `restoreViewport` and `viewportCommand`, with two canvases sharing one `CanvasTextureAdmission` and the texture budget split in half (`presentation_window.cpp:128-132`). This contract generalizes that to N <= 4 panes, in-app, without changing the presentation window.

## Consumers

**C3** (COMPARE-PANES + F026), **C4** (blink, wipe, difference over the same panes and transforms), **Track D** canvas (D1 fit tier for panes, D3 texture reuse, D6 sampling per pane), **E1** (ledger owners), **E6** (accessibility of pane focus).

## Frozen interface

### In the tree now: `include/glancelot/compare_panes.hpp`

`kMaxComparePanes`, `CompareLayout`, `kContentModeCompare2`, `kContentModeCompare4`, `parseCompareContentMode`, `comparePaneCount`, `PaneState`, `ICompareSurface`, `compareStateFromPresentation`, `presentationStateFromCompare` and `CompareSession` (its constructor takes the window's `resource::OwnerId`, and `setSurface` attaches the `ICompareSurface` it drives). These are declarations; C3 defines them in `src/app/compare_session.cpp`. In `contracts.hpp`, the `Command` scope comment reserves `"compare"`.

### Applied by C3 (C3's brief grants this exact diff)

`include/glancelot/pane_layout.hpp`:

```diff
-    Q_PROPERTY(QString contentMode READ contentMode NOTIFY changed)   // "preview" | "grid" | "combined"
+    // "preview" | "grid" | "combined" | "compare2" | "compare4". Compare modes are session-only:
+    // never persisted, never saved in presets or monitor snapshots (baseContentMode is stored instead).
+    Q_PROPERTY(QString contentMode READ contentMode NOTIFY changed)
+    Q_PROPERTY(QString baseContentMode READ baseContentMode NOTIFY changed) // mode restored on exit
 ...
+    QString baseContentMode() const;
+    Q_INVOKABLE bool exitCompare();                                    // contentMode = baseContentMode
 private:
+    QString baseMode_{QStringLiteral("preview")};
```

Until C3 applies this, `PaneLayout` rejects the compare strings, so the modes are unreachable.

### What C3 must keep in `pane_layout.cpp`

`modeValid` (`:23`) stays at the three persisted values, because `load` (`:79`) and the preset reader (`:296`) share it. `setContentMode` (`:233`) gains its own check through `parseCompareContentMode`; a compare value sets `mode_` and leaves `baseMode_`, with no `persist` call. Like `combined`, it is a view preference, allowed under the global lock and with frozen center panes.

Every site that writes the mode writes `baseMode_` while compare is active:

| Site | Rule |
| --- | --- |
| `snapshot` (`:42-43`) | Stores `baseMode_` |
| `savePreset` (`:305`, F049) | Stores `baseMode_` |
| `persistWhole` (`:346`, F049) | Stores `baseMode_` |
| `applyPreset` (`:370`, F049) | Sets both `mode_` and `baseMode_` from the preset, which exits compare. Its lock and freeze refusals are unchanged |
| `restoreSnapshot` (`:50`) | Exits compare first |
| Validators `settings_policy.cpp:90`, `monitor_customization_state.hpp:67` | Unchanged: three values, so older binaries and monitor presets are unaffected |

`Main.qml`'s `onGridModeChanged` (`:76`) only writes when `gridMode` differs from `contentMode === "grid"`, which holds for compare modes without any change.

## Semantics

**Entering and leaving.** `begin()` comes from the grid or filmstrip selection (2 to 4 files) or "compare with current". Entering stops a running slideshow, as grid mode does (`Main.qml:76`). The main preview canvas binds an empty image while compare is shown, so it releases its texture. Pane 0 shows the current image, which avoids a second texture of the same pixels in the same window. The viewer's selection does not change while comparing. `end()` or `exitCompare()` restores `baseContentMode` and releases every pane reservation.

**Tabs (F015).** Switching tabs ends the compare session, because the tab's folder and image become current. A `TabLocation` (`recovery_checkpoint.hpp:7`) holds only a folder and an image, so compare state cannot enter the recovery checkpoint, and it never does. A restart restores the selection and `baseContentMode`, never compare.

**Reference window (F040).** Independent of compare. `reference-pin` still pins the viewer's current image, and the reference canvas keeps its own canvas group.

**Decoding.** C3 adds a pane decode path modelled on `Private::startPrefetch` (`viewer_controller.cpp:690`), not on `Private::decode`. `decode` is bound to the selection generation and one `worker`, so a pane cannot share it. The pane path uses the same worker protocol, the same `DecodeCache` with a revision probe, the same probe-time validation and the same transfer buffers, one per pane, under RESOURCE-LEDGER's rules. `viewer_controller.{hpp,cpp}` and `ui/Main.qml` are hot files, so C3's brief names the regions it may edit.

- **Fencing.** The session owns a monotonic counter, and each pane request takes the next value. It never increments the controller's `generation`, because that would cancel the current image's decode (`token != generation`). `GenerationSource` reads the controller's generation only to hand a pane to `DisplayManager::setComparisonImage`, whose rule is that every producer uses that space.
- **Priority.** The active pane runs at `JobPriority::Foreground`. Other panes are queued behind it one at a time. At most one compare decode runs besides the current-image decode, and `IoScheduler` admission still applies.
- **Tiers.** If D1 has landed, panes use its ladder and label the tier in `candidates[].tier`, and `deliver` applies DECODE-TIER's apply rule per pane. C3 does not wait for D1, and a pane is `TooLarge` only when no labelled lower tier exists. Until then 4-up holds 16 MP per pane and 2-up holds 32 MP, so typical camera files get `TooLarge` in 4-up.

**Wiring.** `CompareSession` drives the surface set by `setSurface`: `deliver`, `swap`, `removeCandidate` and `end` update it, so swap and removal state lives in one place. Callers pass files known from the model, and `begin` does no file access: a folder that slips through becomes `Failed` from the decode path.

**Linking.** `Linked` matches the presentation's `m_linked` behaviour (`copyViewport`): one pane's `viewState()` (`image_canvas.hpp:56`) is copied to the others on every change. `Independent` keeps each pane's own state. Rotation and fit follow the presentation rules (`viewportCommand`). C4 reuses the aligned transforms.

**Ledger (every pane counted).**

- CPU: one owner per window, `compare:<slot>`, with a CpuPixels owner quota of 256 MiB, matching the texture budget. Each pane holds a CpuPixels `Reservation` with `shareKey = image.cacheKey()`, so a pane showing the current or a cached image is charged once (shared). Four distinct images make four charges.
- GPU: all pane canvases share one canvas group (`shareTextureAdmissionWith`, `image_canvas.hpp:50`), whose GpuTextures owner quota equals the main canvas budget. As in `PresentationWindow::setComparisonMode`, each pane's `textureBudget` is `aggregate / paneCount` (2-up 128 MiB, 4-up 64 MiB per pane). The quota is not raised while comparing.
- Under pressure, panes' textures are reclaimed at tier Hidden only when the window isn't exposed (`QWindow::isExposed` false, or minimized). An unfocused but visible window keeps them.

**Removal never deletes.** `removeCandidate` is not wired to any `FileOperationService`, `CopyMoveService` or `MutationService` path, and holds no reference to them. In compare scope, `Delete` and `Backspace` are bound to `compare-remove-pane`. Deleting the active candidate's file requires the explicit delete command (palette or menu), which opens the normal FS-01 confirmation for that one file.

**Threading and bounds.** `CompareSession` and `ICompareSurface` are GUI-thread only. QML gets value lists, never `QImage`. Limits: at most 4 panes, one pending request per pane, one compare decode in flight.

**Commands.** C3 registers its commands in its own block of `command_registry.cpp`, with scope `"compare"`.

## Failure and cancellation

| Case | Result |
| --- | --- |
| Pane image exceeds its GPU share | `TooLarge`, settled at probe before any transfer is allocated, with honest text naming the limit ("This photo is larger than 4-up's 16 MP limit. Use 2-up, which holds 32 MP, or single view."), until a labelled lower tier exists. Never silently downscaled on the UI thread |
| Ledger refusal | That pane is `Refused`. Other panes and the current image are untouched |
| Decode fails | That pane is `Failed`, with the worker's message. The other panes stay |
| `setCandidate`, `removeCandidate`, `swap` or `end` during a decode | Cancels the stale request. A late `deliver` for an old counter value is dropped and its reservation released |
| Worker dies mid-pane | That pane is `Failed`. Reservations return through the normal path |
| Navigation in the main window, a tab switch or a folder change | Ends compare; every request is cancelled and everything returns to baseline |
| A candidate file disappears | Its pane is `Failed` ("no longer available") and the session continues |
| `begin` with wrong counts or more paths than panes | Returns false; nothing changes |

## Migration without behaviour change

- `PaneLayout`: the existing values, persistence, presets and the `combinedMode`/`gridMode` logic in `Main.qml` are unchanged.
- `IPresentationWindow`, `PresentationWindow` and `DisplayManager` are unchanged. Only the two pure conversion functions are added beside them.
- `Main.qml` gains one component instantiation (`CompareView.qml`) in the region the lead grants C3. Everything else lives in C3's component, `src/app/compare_session.cpp` and the tests below.
- C3 applies the ui-design-skills (application-interface-design, layout, spacing, geometry, motion, design-review) to the compare view, per AGENTS.md.

## Acceptance tests C3 must pass

`tests/compare_panes_tests.cpp`, `tests/compare_panes_ui_tests.cpp`; generated media.

- **Removing a pane never deletes:** remove each pane in 2-up and 4-up with mouse, `Delete` and `Backspace`. Source hashes, mtimes and existence are unchanged, the fake file-operation services record zero calls, and no confirmation opens.
- **Candidate replacement and late completion:** replace pane 2 while its decode is in flight. The old result is dropped, the reservation count returns to its prior value and the new image appears. Same for `swap` and `end` during a decode.
- **The current image is undisturbed:** requesting panes never changes `viewer`'s generation, and a current-image decode in flight still completes.
- **Every pane in the ledger:** four distinct images give four CpuPixels charges and textures within the group quota. The same image in pane 0 and as the current image is charged once on the CPU. `end()` returns the ledger to baseline. A 4-up of four 60 MP images gives `TooLarge` per pane with the text, and the current image stays intact.
- **Refusal and pressure:** a lowered effective limit gives `Refused` on the last pane only. Simulated Elevated pressure with the window not exposed reclaims pane textures, and exposing it re-uploads them without a decode. An unfocused, visible window keeps them. `TooLarge` allocates no transfer buffer.
- **Two windows, same source:** two windows (two processes) compare the same files. Each process accounts on its own and no file is written. The presentation comparison mirrors panes 0 and 1 through the conversion functions (round-trip test).
- **Persistence and compatibility:** compare mode survives neither restart nor `savePreset`. `snapshot()` and `persistWhole` store `baseContentMode`. A settings file with `"mode":"compare2"` (hand-edited) loads as `preview`, keeping today's fallback. `applyPreset` during compare exits compare and obeys its lock and freeze refusals.
- **Tabs and reference:** a tab switch during compare ends it, and the recovery checkpoint holds no compare state. A reference window stays open and unchanged through entering and leaving compare.
- **Linking:** linked zoom, pan and rotate propagate. Independent mode keeps per-pane state. `restoreViewport` round-trips.
- **Keyboard and scope:** 1 to 4 select a pane, Tab cycles, L toggles the link, Escape exits. Text fields and modals take precedence (IN-02). Large text and 540x360 layouts keep pane labels readable (E6).

## Not covered

- No change to `IPresentationWindow`, the presentation window or monitor customization.
- No blink, wipe or difference (C4), no burst stacks (C5) and no rating UI (C2) here.
- No decoding, scaling or file access in `CompareSession` or the surface, and no compare-specific cache.
- No more than four panes, and no compare across windows or processes.
- No persistence of candidates or compare state.
