# F002 independent Gemini review: Breadcrumbs and path completion

- **Packet:** F002 (Breadcrumbs and path completion in the path bar)
- **SHA pair reviewed:** `87b063d` → `b1b82e4` (`claude/f002`)
- **Reviewer:** Gemini lane (`agent/*`), isolated worktree `.worktrees/review-f002`, branch `agent/review-f002`
- **Disposition:** **APPROVED**

## Scope and Diff Summary

The review evaluated only `git diff 87b063d b1b82e4`:
- `CMakeLists.txt`: Added dedicated test target `pathbar_ui_tests` with assets and shader resources.
- `docs/F002-PLAN.md`: Contract specification and delivery notes.
- `include/glancelot/viewer_controller.hpp`: Declared `completePath(const QString& text)` and signal `pathCompletions(const QString& text, const QStringList& names)`.
- `src/app/viewer_controller.cpp`: Asynchronous, cancellable folder completion worker via `QtConcurrent::run` and `std::stop_source`/generation guards.
- `ui/FolderTree.qml`: Path breadcrumb navigation bar with horizontal flicking and folder suggestions popup list with Tab/click acceptance.
- `tests/pathbar_ui_tests.cpp`: Targeted UI tests for breadcrumb navigation and autocomplete/Tab acceptance.
- `tests/ui_test_support.hpp`: Shared UI test harness extraction.

## UI Design & Craft Evaluation

Evaluated against `application-interface-design` and Qt/QML design guidelines:
1. **Hierarchy & Structure**:
   - Breadcrumbs sit above the direct address edit field, establishing a clear hierarchy from ancestor roots down to the current working folder.
   - The current folder is highlighted using `variant: "outline"` while ancestor crumbs use `variant: "quiet"`, offering immediate visual orientation.
   - Chevron separators (`chevron-right`, 12px, `Theme.muted`) create clear directional flow without clutter.
2. **Layout & Spacing**:
   - Uses standard Theme tokens (`Theme.gapSm`, `Theme.controlHeight`).
   - Breadcrumbs are hosted in a `Flickable` with `boundsBehavior: Flickable.StopAtBounds` and automatically scroll the active/leaf crumb into view (`contentX = Math.max(0, contentWidth - width)`).
   - Suggestions popup is bounded to `Math.min(6, panel.suggestions.length)` to prevent view overflow.
3. **Typography & Affordance**:
   - Consistent button styling with standard application font stack.
   - Folder suggestions display left-aligned with a folder icon (`UiIcon { name: "folder" }`), making items identifiable at a glance.
4. **Interaction & State Management**:
   - Typing debounce timer (120ms) prevents thrashing off-thread folder scans during fast text input.
   - Asynchronous worker is properly bounded (up to 5000 scanned folders, max 20 returned) and cancellation-safe via `std::stop_source` and generation sequence check.
   - Clean dismissal: losing focus or pressing Escape clears suggestions immediately. Tab completion cleanly appends a slash and triggers subsequent scope completions.
5. **Accessibility**:
   - Breadcrumbs specify `Accessible.role: Accessible.Grouping` and `Accessible.name: "Folder breadcrumbs"`.
   - Each crumb has an accessible label (`"Go to " + label + (current ? ", current folder" : "")`).
   - Suggestions list specifies `Accessible.role: Accessible.List` with `Accessible.name: "Folder suggestions"`.

## Findings

1. **Minor / Scope Note (Arrow-key selection in suggestion popup)**:
   - As documented in `docs/F002-PLAN.md` ("Not done"), suggestions cannot be navigated via Up/Down arrow keys; users must use Tab (first item) or mouse click. Non-blocking; fits the declared plan.
2. **Minor / Platform separator normalization**:
   - `crumbList` normalizes Windows backslashes to forward slashes for splitting and preserves drive root formatting (`C:/`). Windows UNC paths (`//share`) are also parsed and handled cleanly.

## Targeted Test Output

Built and executed in `.worktrees/review-f002` (`build-review-f002`):
```
Test project C:/src/Github/Glancelot/.worktrees/review-f002/build-review-f002
    Start 10: input_ui
1/2 Test #10: input_ui .........................   Passed   29.35 sec
    Start 11: pathbar_ui
2/2 Test #11: pathbar_ui .......................   Passed    2.23 sec

100% tests passed out of 2
Total Test time (real) =  31.79 sec
```

Direct execution:
- `pathbar_ui_tests.exe`: 2 passed, 0 failed, 0 skipped.
  - `breadcrumbsJumpToAncestors`: PASS
  - `completionListsTypedScopeAndTabAccepts`: PASS
