# F014 independent Gemini review: Thumbnail badges

- **Packet:** F014 (Thumbnail badges: type and failed-preview badges)
- **SHA pair reviewed:** `c266824` → `f7aa764` (`claude/f014`)
- **Reviewer:** Gemini lane (`agent/*`), isolated worktree `.worktrees/review-f014`, branch `agent/review-f014`
- **Disposition:** **APPROVED**

## Scope and Diff Summary

The review evaluated `git diff c266824 f7aa764`:
- `docs/F014-PLAN.md`: Scope contract and delivery notes.
- `ui/PhotoGrid.qml`: Added `gridTypeBadge` (file format extension at top-left) and `gridErrorBadge` (alert icon + "No preview" label at bottom-left); verified layout clearances against link badge (top-right) and "Viewing" indicator (bottom).
- `tests/input_ui_tests.cpp`: Added regression test `badgesAppearAndDoNotOverlap` verifying badge appearance, non-overlapping coordinates, and error state transitions.
- Note: This diff also contains the synchronization with main (`244bfae` bringing in platform test provenance and grid search) and proposals for F013/F042. The review focused specifically on F014's UI deliverables.

## UI Design & Craft Evaluation

Evaluated against `application-interface-design`, badge typography, iconography, and Qt/QML design guidelines:
1. **Clear Semantic Placement & Non-Collision**:
   - Badges are anchored to the preview's left margin (`anchors.left: parent.left; margins: Theme.gapXs`), keeping the right side clear for `gridLinkBadge` (symlink icon) and bottom-center/right clear for the `"Viewing"` active-photo tag.
   - Distinct vertical separation: `gridTypeBadge` anchors to `parent.top`, whereas `gridErrorBadge` anchors to `parent.bottom`.
2. **Accessible Status Communication (Never Color Alone)**:
   - In adherence to accessibility standards, `gridErrorBadge` communicates preview failure using both a distinct warning icon (`UiIcon { name: "alert"; size: 12; lineWidth: 2; color: Theme.danger }`) and text (`"No preview"`), rather than relying exclusively on a red border or tint.
3. **Responsive Scaling & Degraded Displays**:
   - At compact thumbnail widths (< 96px), `gridTypeBadge` automatically hides to prioritize thumbnail content.
   - For `gridErrorBadge`, the `"No preview"` text hides below 96px, while the 12px alert icon remains visible.
4. **Visual Styling & Consistency**:
   - Badges use standard surface tokens: `color: Theme.surface`, `radius: Theme.radiusSmall`, and standard font tokens (`Theme.textCaption`, `Font.Medium`, letter spacing 0.4).

## Findings

1. **Scope Note (Lazy metadata badges)**:
   - As documented in `docs/F014-PLAN.md` ("Not done"), ratings, camera models, and dimension badges require the lazy metadata engine (META-HUD-01) and are not included in this delegate slice to avoid blocking UI thread scans. This satisfies the decoupled architecture requirement.

## Targeted Test Output

Executed in `.worktrees/review-f014` (`build-review-f014`):
```
Test project C:/src/Github/Glancelot/.worktrees/review-f014/build-review-f014
    Start 11: input_ui
1/1 Test #11: input_ui .........................   Passed   44.15 sec

100% tests passed out of 1
Total Test time (real) =  44.22 sec
```
Direct run:
- `input_ui_tests.exe`: All test cases passed, including `badgesAppearAndDoNotOverlap`.
