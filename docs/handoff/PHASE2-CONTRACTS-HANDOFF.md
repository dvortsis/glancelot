# PHASE2-CONTRACTS handoff

- Branch: `claude/phase2-contracts`
- Tip: the commit that adds this handoff and the evidence, on top of `ff5ff5c`. The commit can't name itself, so the PR states its SHA. Everything else is at `ff5ff5c`.
- Base: `b1d57cd` (`origin/main` when the branch was cut). `origin/main` has since moved to `f24f2e7`, which adds only merge-protection tooling and docs, with no change under `include/`, `src/`, `cmake/` or `CMakeLists.txt`. Not merged in, because nothing conflicts. Not stacked on anything.
- Worktree and build: `.claude/worktrees/recursing-bassi-58ba22`, `build-claude-phase2-contracts`.
- ui-design-skills: not read, because this packet changes no QML. Consumers' UI work (C3, D2, B1, B2) must read them; COMPARE-PANES says so for C3, and AGENTS.md covers the rest.

## What this packet does

Freezes the seven cross-track contracts from section 6 of the lead plan, checked against current `main`, with every draft question resolved. Adds the frozen interfaces as headers and types. No behaviour changes.

| File | Change |
| --- | --- |
| `docs/contracts/{META-ROLES,DECODE-TIER,CAPABILITY,RESOURCE-LEDGER,ASSET-GROUP,OPERATION-JOURNAL,COMPARE-PANES}.md` | Frozen. Each has Consumers, Frozen interface, Failure and cancellation, and acceptance tests for its implementing packet |
| `docs/decisions/2026-11-05-phase2-contracts.md` | Every decision and its alternatives |
| `include/glancelot/{resource_ledger,asset_group,operation_journal,capability_model,compare_panes}.hpp` | New, declarations only |
| `include/glancelot/contracts.hpp` | `DecodeTier` types and appended defaulted fields; deleted the unused `DisplayCapabilities` and `DecoderCapability`; `"compare"` reserved in the scope comment |
| `include/glancelot/navigation.hpp` | `SortKey::Captured`, `SortKey::Dimensions`, appended `FileMeta` fields, `SortKeyReader` |
| `src/core/navigation.cpp` (+2) | `sortBefore`: the new keys give natural order instead of falling into the size branch |
| `src/app/viewer_controller.cpp` (+2) | `sortKeyName`: the two new cases |
| `cmake/Phase2ContractTests.cmake`, one line in `CMakeLists.txt` | Compile test wiring |
| `tests/contract_headers_tests.cpp` | Compile and frozen-value test |
| `docs/verification/PHASE2-CONTRACTS/` | Evidence |

**The header rule.** Existing headers only gain appended enum values, defaulted trailing fields, new standalone types and comments. A changed signature or a new `Q_PROPERTY` on an existing QObject would need a definition in this packet. Those changes are frozen as exact diffs inside each contract, and the implementing packet's brief must grant them:

| Packet | Diffs it applies |
| --- | --- |
| A2 | `navigation.hpp`, `directory_listing.hpp`, `directory_entry_model.hpp`; also `settings_policy.cpp:140`, `SettingsDialog.qml:637-638`, `Main.qml:1827` |
| B1 | `file_operation_service.hpp` |
| B2 | `file_operations.hpp`, `src/app/copy_safety.hpp` |
| C3 | `pane_layout.hpp`, plus named regions of `viewer_controller.*` and `Main.qml` |
| D1 | `display_manager.hpp`, `viewer_controller.hpp`, `src/app/decode_cache.hpp` |
| D2 | `src/app/viewer_context.hpp`, `Main.qml` `reference-pin` handler and `referenceWindow` block |
| F1 | `Dockerfile` `ENV`: `GLANCELOT_DEPLOYMENT=container` |

## Lead decisions written in

- **OPERATION-JOURNAL:** at startup, B2 finds `.~gb*.tmp` backups left by a crash or error 1177 inside `ReplaceFileW`, and restores or reports them. The backup path is made durable in the intent before the replace, because the name is random and nothing else could find it. See "Restart reconciliation" in the contract.
- **CAPABILITY:** the reference window's platform check and unsupported state (G-5), with a pure decision function asserted in the compile test and a runtime expose deadline. See "Reference window check and unsupported state".

## Pre-handoff checklist

### 1. Fail-before evidence

At `b1d57cd` the test cannot compile: `SortKey::Captured`, `DecodeTier` and the new headers do not exist. Every assertion was also checked by breaking its target, in `docs/verification/PHASE2-CONTRACTS/fail-before.txt` (MSVC, actual output). The results:

| Mutation | Result |
| --- | --- |
| Natural-order branch removed from `sortBefore` | Test exit 1: `FAIL captured ascending: natural name wins over a smaller size`, and 7 more shown |
| `SortKey` renumbered | `contract_headers_tests.cpp(22)`, `(23)`: `error C2607: static assertion failed` |
| Tier ladder reversed | `(29)`: static assertion failed |
| G-5: a container session claims full support | `(69)`: static assertion failed |
| Batch rename may run without a journal | `(55)`: static assertion failed |
| `operation_journal.hpp` loses an include | `operation_journal_selfcontained.cpp`: `error C2039: 'TargetKind'`, proving the per-header units work |
| `Q_PROPERTY` READ names a missing function | `moc_capability_model.cpp(184)`: `error C2039: 'referenceWindowMissing'`, proving `contract_headers_moc` compiles moc output |

### 2. Grant check

```text
CMakeLists.txt
cmake/Phase2ContractTests.cmake
docs/contracts/ASSET-GROUP.md
docs/contracts/CAPABILITY.md
docs/contracts/COMPARE-PANES.md
docs/contracts/DECODE-TIER.md
docs/contracts/META-ROLES.md
docs/contracts/OPERATION-JOURNAL.md
docs/contracts/RESOURCE-LEDGER.md
docs/decisions/2026-11-05-phase2-contracts.md
include/glancelot/asset_group.hpp
include/glancelot/capability_model.hpp
include/glancelot/compare_panes.hpp
include/glancelot/contracts.hpp
include/glancelot/navigation.hpp
include/glancelot/operation_journal.hpp
include/glancelot/resource_ledger.hpp
src/app/viewer_controller.cpp
src/core/navigation.cpp
tests/contract_headers_tests.cpp
```

Inside the grant: `include/glancelot/*` headers named by the brief, `cmake/Phase2ContractTests.cmake` and the one `CMakeLists.txt` line as minimum build wiring, the new test, docs.

Outside the literal "headers and types" and justified by the brief's "no switch over `SortKey` left unhandled":

- `src/core/navigation.cpp` +2: `sortBefore` is an if/else chain, so the new keys would otherwise sort by size;
- `src/app/viewer_controller.cpp` +2: `sortKeyName` has a `default`, so this is for explicitness; without the cases a new key's name would read "name".

`include/glancelot/compare_panes.hpp` is not named in the brief's list, which says "the pane content modes". The modes cannot go on `PaneLayout` without a behaviour change, so they live here with the rest of the frozen compare interface. `Main.qml` is untouched. `docs/WORK-QUEUE.yaml` is untouched: this packet has no row, which is the lead's to add.

### 3. Exact base

Base `b1d57cd0ad9cab09a3f9a5483b726921b0c4dbcc`. `git merge-base origin/main HEAD` returns it.

### 4. Tests

Targeted (Windows, MSVC 2022, Qt 6.8.3, offscreen), on the code commit:

```text
ctest -R "^(contract_headers|navigation|directory_listing|directory_model|cache|viewer_acceptance|pane_layout|file_operations|copy_safety|reference_pin)$"
(tail of the output)
 4/10 Test  #7: viewer_acceptance ................   Passed   12.28 sec
 5/10 Test #10: pane_layout ......................   Passed    3.35 sec
 6/10 Test #16: directory_model ..................   Passed   37.64 sec
 7/10 Test #31: cache ............................   Passed    6.82 sec
 8/10 Test #37: copy_safety ......................   Passed    1.19 sec
 9/10 Test #45: contract_headers .................   Passed    0.09 sec
10/10 Test #51: reference_pin ....................   Passed   47.23 sec

100% tests passed out of 10
```

Full suite, `scripts/build.ps1 -BuildDir build-claude-phase2-contracts -Test` through `scripts/build-slot.ps1`, on the code commit:

```text
100% tests passed out of 52
Total Test time (real) = 1021.36 sec
script exit: 0
No baseline failures: every test passed, so none is named. Budget tests passed on the first run.
```

Log tail: `docs/verification/PHASE2-CONTRACTS/full-suite-tail.txt`.

Other checks:

- `pr_guard.py` from `origin/main`: `pr_guard: code=True, 0 error(s)`.
- Linux GCC 13.3 (WSL) with the project's flags, on the three `FileMeta{a,b,c}` sites: `navigation.cpp` and `directory_listing.cpp` compile with 0 warnings, so positional initializers stay clean under `-Wextra`.
- Reference check: a script compared the `file:line` references in the contracts and the decision record against `b1d57cd`. 111 resolve, with the file present and the line in range. The one that doesn't is `CompareView.qml`, which C3 creates. The script does not check what a line says; I read those lines while rewriting, and a reviewer may want to sample them.

### 5. Decisions taken

All in `docs/decisions/2026-11-05-phase2-contracts.md`: 58 rows, each draft question plus each problem found against `main`. Problems the drafts got wrong, beyond stale line numbers:

- **Pane generations (P6).** Taking a generation from the controller's counter would cancel the current image's decode. The session owns its own counter.
- **Pane decode path (P7).** `Private::decode` is bound to one worker and the selection, so C3 models its path on `startPrefetch`.
- **Case-only rename and partials (J7).** The draft's rule, "reconcile deletes partials", would have deleted the user's file at `.tmp_rename_<id>`. Originals now go in `backupPath`, which is never deleted.
- **Ladder state (D7).** Phase 1 code gates on `imageReady`, `loading` and now `navigating`, so the contract says what each means during a refinement.
- **`modeValid` (P8).** It is shared with settings load and presets, so widening it would restore compare from a hand-edited file.
- **PR 3's reaper (R6).** Transfer bytes stay charged until the segment is really gone, and a leaked segment stays charged and visible. E1 must start after PR 3 merges.
- **Sort settings (M8).** `settings_policy.cpp:140` would reject a saved new key.

## Open questions for the lead

None block this packet. I made these two decisions and they change something the user sees or touch data safety, so please confirm them at review:

1. **J1, unwritable journal.** The draft failed closed for every mutation. On a read-only profile that would remove delete, move and rename, which work today. The contract keeps them working with a notice and no undo, and refuses only sidecar writes, lossless transforms and batch rename. If you prefer fail closed, it is one function: `journal::requiresJournal`.
2. **J4, replaced files land in the trash.** B2 moves an Overwrite's replaced original to the trash after the atomic replace, which is what gives Overwrite an undo. The queue's COPY-SAFETY-01 note already expects this. The visible effect is that the Recycle Bin gains `<stem> (replaced)<ext>` files.

Other visible choices are marked "Yes" in the decision record's Visible column.

Also for the lead:

- E1 must start after PR 3 merges (RESOURCE-LEDGER).
- A1 (`a1-perf-enum`) is working in the listing and navigation code now. This packet only appends there; a note is in the shared notes folder. A2's diff is written so it can follow A1's signatures.
- G-5 is now D2's acceptance, and STACK-CONFIRM's carve-out can point at this contract.

## Failure and cancellation cases

This packet has no runtime paths beyond the two natural-order lines, which the test covers in both directions through `fileSortBefore`, `sortedImageFiles` and `listDirectory`. Each contract's "Failure and cancellation" section is what the implementing packets must cover.

## Unverified

- Docker build: not run here. The lead's gate decides.
- Linux full build: not run here. `build.yml` covers it on the PR, and WSL compiled only the two core files above.
- Whether `QPlatformIntegration::MultipleWindows` really reports true on `windows`, `xcb`, `wayland` and `offscreen`: the enumerator exists in Qt 6.8.3's header, but nothing calls it yet. D2's acceptance tests check it on the gate platforms, and the runtime deadline covers a wrong answer.
- The moc-generated code for the new QObjects is compiled but not linked, because the classes have no definitions until C3, D2 and E1.

## Exact `include/` diff

`git diff b1d57cd ff5ff5c -- include/`

```diff
diff --git a/include/glancelot/asset_group.hpp b/include/glancelot/asset_group.hpp
new file mode 100644
index 0000000..41094bb
--- /dev/null
+++ b/include/glancelot/asset_group.hpp
@@ -0,0 +1,77 @@
+#pragma once
+#include "file_operations.hpp" // FileIdentity
+#include <cstddef>
+#include <cstdint>
+#include <filesystem>
+#include <stop_token>
+#include <string>
+#include <utility>
+#include <vector>
+
+// Lead-frozen ASSET-GROUP (docs/contracts/ASSET-GROUP.md). Implemented by packet B1 in src/app/asset_group.cpp, linked into
+// glancelot_platform: classification needs RawPreviewExtractor and rename validation needs MutationService. The header stays
+// Qt-free so tests and later core code can use the types. Pure grouping over a listing plus a bounded, worker-only expansion
+// that re-probes exact companion names. Never called from UI or render code. Never mutates anything.
+namespace glancelot::assets {
+inline constexpr std::size_t kMaxMembers = 16;          // per group; extra candidates set Collision
+inline constexpr std::size_t kMaxProbesPerTarget = 48;  // exact-name stats during expansion
+
+// Raw: a camera RAW. Rendered: the camera-native companion formats (JPEG, HEIF, AVIF, JPEG XL). Derived: an exported or edited
+// format (TIFF, PNG, WebP), a member only when it is the stem's sole image, so a RAW's asset never pulls in an edited master.
+enum class Role : std::uint8_t { Raw, Rendered, Derived, Sidecar, MotionVideo, Other };
+enum class SidecarStyle : std::uint8_t { None, Stem /*IMG.xmp*/, FullName /*IMG.CR2.xmp*/ };
+enum class Match : std::uint8_t { Stem, ContentId };    // ContentId: reserved for a confirmed Apple content identifier (ADV-09)
+enum class GroupFlag : std::uint32_t {
+    None = 0,
+    Collision = 1u << 0,        // more than one Raw, more than one Rendered family, case variants, or > kMaxMembers
+    AmbiguousSidecar = 1u << 1, // a Stem sidecar that could belong to more than one group
+    EmbeddedMotion = 1u << 2,   // an Android motion photo payload inside a Rendered member
+    Incomplete = 1u << 3,       // the listing was truncated or unreadable; expansion re-probes exact names
+};
+
+struct Member {
+    std::filesystem::path path;              // absolute, lexically normal
+    Role role{Role::Other};
+    SidecarStyle sidecar{SidecarStyle::None};
+    FileIdentity identity;                   // zero when not yet snapshotted
+};
+struct Group {
+    std::uint64_t id{};                      // stable within one GroupingSnapshot only
+    std::filesystem::path directory;
+    std::string stemKey;                     // case-folded on case-insensitive volumes
+    std::vector<Member> members;             // Raw, Rendered, Derived, MotionVideo, Sidecar order; <= kMaxMembers
+    std::size_t primary{};                   // index: Raw if present, else Rendered, else Derived
+    Match match{Match::Stem};
+    std::uint32_t flags{};
+};
+struct GroupingSnapshot {
+    std::filesystem::path directory;
+    std::uint64_t listingGeneration{};       // generation of the listing it was built from
+    bool caseSensitive{};                    // volume semantics, probed once per directory
+    std::vector<Group> groups;               // only groups with >= 2 members; singletons are implicit
+};
+struct ListingInput { std::filesystem::path path; bool regularFile{}; };
+// Pure, O(n log n), no I/O. Same directory only; folders and links are never grouped.
+GroupingSnapshot groupListing(const std::filesystem::path& directory, const std::vector<ListingInput>& entries,
+                              bool caseSensitive, std::uint64_t listingGeneration);
+
+enum class Scope : std::uint8_t { FileOnly, Asset };
+enum class Operation : std::uint8_t { Trash, Copy, Move, Rename, SidecarWrite, LosslessTransform };
+enum class Exclusion : std::uint8_t { Ambiguous, Collision, NotApplicable, Missing, LimitReached };
+struct Planned { Member member; std::filesystem::path requestedBy; bool companion{}; };
+struct Excluded { std::filesystem::path path; std::filesystem::path requestedBy; Exclusion reason{Exclusion::NotApplicable}; };
+struct Expansion {
+    std::vector<Planned> included;           // deduplicated, requested items first, then companions in role order
+    std::vector<Excluded> excluded;          // always shown in the confirmation; never mutated
+    bool cancelled{};                        // no partial plan can be confirmed
+};
+// Worker only. FileOnly returns exactly `requested` plus companions as excluded (NotApplicable) so the confirmation can show
+// them. Asset adds companions per the contract's policy table after stat'ing exact candidate names; the listing is only a hint.
+Expansion expand(const std::vector<std::filesystem::path>& requested, Scope scope, Operation operation,
+                 const GroupingSnapshot* hint, std::stop_token cancel = {});
+
+// Asset-scope rename: one new stem for every member, preserving each extension and the full-name sidecar's double extension.
+// Empty on an invalid stem (MutationService::isValidFileName rules).
+std::vector<std::pair<std::filesystem::path, std::filesystem::path>>
+renameAsset(const Expansion& planned, const std::string& newStem);
+}
diff --git a/include/glancelot/capability_model.hpp b/include/glancelot/capability_model.hpp
new file mode 100644
index 0000000..e85a9a2
--- /dev/null
+++ b/include/glancelot/capability_model.hpp
@@ -0,0 +1,96 @@
+#pragma once
+#include <QAbstractListModel>
+#include <QByteArray>
+#include <QHash>
+#include <QList>
+#include <QString>
+#include <QVariantList>
+#include <QVariantMap>
+#include <memory>
+
+// Lead-frozen CAPABILITY (docs/contracts/CAPABILITY.md). Implemented by packet D2. One place that states what this build, this
+// machine and this source can do, with unknown as an explicit answer. Display only: services recheck at operation time.
+namespace glancelot {
+enum class CapabilitySection : quint8 { Decoder, Colour, Display, Source };
+// Unknown is the default and the honest answer when nothing has established the fact.
+enum class Support : quint8 { Unknown, Unsupported, Partial, Supported };
+// How the value was established. Runtime code never claims physical qualification: that is RUNBOOK evidence only.
+enum class Evidence : quint8 { None, BuildFact, RuntimeProbe, Assumed, UserSetting };
+
+struct CapabilityItem {
+    QString id;            // stable dotted id, e.g. "decoder.webp.still", "display.<displayId>.icc", "source.readOnly"
+    CapabilitySection section{CapabilitySection::Decoder};
+    Support support{Support::Unknown};
+    Evidence evidence{Evidence::None};
+    QString label;         // translated short name
+    QString detail;        // translated user-facing value or reason ("Read-only mount")
+    QString value;         // untranslated machine value ("D3D11", "sRGB", "6.8.3"); never a host path guess
+};
+// One section replaced atomically. `scope` fences per-context sections: Source = directory key; Display and Colour = the
+// window's screen id ("" = process-wide). `revision` is the populator's monotonic counter for that section and scope.
+struct CapabilitySnapshot {
+    CapabilitySection section{CapabilitySection::Decoder};
+    QString scope;
+    quint64 revision{};
+    QList<CapabilityItem> items;
+};
+
+// STACK-CONFIRM G-5: whether the F040 reference window can be shown, and placed, in this session.
+inline constexpr char kReferenceWindowCapabilityId[] = "display.referenceWindow";
+struct SecondaryWindowFacts {
+    bool multipleWindows{};      // QPlatformIntegration::hasCapability(MultipleWindows)
+    bool canPosition{};          // not Wayland: the same rule as DisplayManager::displaysVariant "canPosition"
+    bool containerDeployment{};  // GLANCELOT_DEPLOYMENT=container, the single-output remote desktop
+};
+// Unsupported: the pin is refused with a message. Partial: it opens, and the detail says it cannot be placed. Supported never
+// means physically qualified; its evidence is RuntimeProbe.
+constexpr Support referenceWindowSupport(const SecondaryWindowFacts& facts) {
+    if (!facts.multipleWindows) return Support::Unsupported;
+    if (facts.containerDeployment || !facts.canPosition) return Support::Partial;
+    return Support::Supported;
+}
+inline constexpr int kReferenceWindowExposeDeadlineMs = 2000; // not exposed by then: unsupported state, see the contract
+
+// GUI thread only. Populators build snapshots on worker threads and deliver them by queued call to apply().
+// Bounds: at most kMaxItems rows in total and kMaxItemsPerSection per snapshot (extra items are dropped and an
+// "<section>.truncated" item is added), kMaxDisplays display scopes retained.
+class CapabilityModel : public QAbstractListModel {
+    Q_OBJECT
+    Q_PROPERTY(QString decoderSummary READ decoderSummary NOTIFY sectionChanged)
+    Q_PROPERTY(QString colourSummary READ colourSummary NOTIFY sectionChanged)
+    Q_PROPERTY(QString displaySummary READ displaySummary NOTIFY sectionChanged)
+    Q_PROPERTY(QString sourceSummary READ sourceSummary NOTIFY sectionChanged)
+    // Display-only conveniences for F075 and Track B. Never authorization: services recheck at operation time.
+    Q_PROPERTY(QString sourceReadOnly READ sourceReadOnly NOTIFY sectionChanged)   // "unknown" | "yes" | "no"
+    Q_PROPERTY(bool serverPaths READ serverPaths NOTIFY sectionChanged)
+    // "unknown" | "unsupported" | "partial" | "supported"; read by the reference-pin command and the reference window.
+    Q_PROPERTY(QString referenceWindow READ referenceWindow NOTIFY sectionChanged)
+public:
+    static constexpr int kMaxItems = 256, kMaxItemsPerSection = 96, kMaxDisplays = 16;
+    enum Role { IdRole = Qt::UserRole + 1, SectionRole, SupportRole, EvidenceRole, LabelRole, DetailRole, ValueRole };
+    explicit CapabilityModel(QObject* parent = nullptr);
+    ~CapabilityModel() override;
+    int rowCount(const QModelIndex& parent = {}) const override;
+    QVariant data(const QModelIndex& index, int role) const override;
+    QHash<int, QByteArray> roleNames() const override;
+
+    // Returns false (no change) when the scope is not current or the revision is not newer than the stored one.
+    bool apply(CapabilitySnapshot snapshot);
+    // Current contexts. Changing the source scope clears the Source section to Unknown until a matching snapshot arrives.
+    void setSourceScope(const QString& directoryKey);
+    void setScreenScope(const QString& displayId);
+
+    Q_INVOKABLE QString support(const QString& id) const;   // "unknown" | "unsupported" | "partial" | "supported"; absent id -> "unknown"
+    Q_INVOKABLE QVariantMap item(const QString& id) const;  // {} for an absent id
+    Q_INVOKABLE QVariantList section(const QString& name) const; // "decoder" | "colour" | "display" | "source"
+    Q_INVOKABLE QString exportText() const;                 // for E2; source paths replaced by "<source>"
+    QString decoderSummary() const; QString colourSummary() const; QString displaySummary() const;
+    QString sourceSummary() const; QString sourceReadOnly() const; bool serverPaths() const;
+    QString referenceWindow() const;
+signals:
+    void sectionChanged(glancelot::CapabilitySection section);
+private:
+    struct Private;
+    std::unique_ptr<Private> d;
+};
+}
diff --git a/include/glancelot/compare_panes.hpp b/include/glancelot/compare_panes.hpp
new file mode 100644
index 0000000..6113dbf
--- /dev/null
+++ b/include/glancelot/compare_panes.hpp
@@ -0,0 +1,87 @@
+#pragma once
+#include <QImage>
+#include <QObject>
+#include <QString>
+#include <QStringList>
+#include <QVariantList>
+#include <QVariantMap>
+#include <functional>
+#include <memory>
+#include <optional>
+#include <string_view>
+
+// Lead-frozen COMPARE-PANES (docs/contracts/COMPARE-PANES.md). Implemented by packet C3. GUI thread only. Compare never decodes
+// and never touches files: it asks the window's ViewerController to decode through the ordinary path and receives values.
+namespace glancelot {
+inline constexpr int kMaxComparePanes = 4;
+// The PaneLayout center content modes. Off is one of the three persisted modes ("preview", "grid", "combined"); the compare
+// modes are session-only and PaneLayout accepts them only once C3 applies the contract's diff.
+enum class CompareLayout : quint8 { Off, Two, Four };
+inline constexpr std::string_view kContentModeCompare2{"compare2"}, kContentModeCompare4{"compare4"};
+constexpr std::optional<CompareLayout> parseCompareContentMode(std::string_view mode) {
+    if (mode == kContentModeCompare2) return CompareLayout::Two;
+    if (mode == kContentModeCompare4) return CompareLayout::Four;
+    return std::nullopt;
+}
+constexpr int comparePaneCount(CompareLayout layout) {
+    return layout == CompareLayout::Two ? 2 : layout == CompareLayout::Four ? 4 : 0;
+}
+enum class PaneState : quint8 { Empty, Loading, Ready, Failed, Refused /*ledger*/, TooLarge /*per-pane GPU*/ };
+
+// The in-app counterpart of IPresentationWindow's comparison surface, implemented by the QML compare host (C3) and by a fake
+// in tests. Fit modes, viewport keys and commands use the presentation vocabulary.
+class ICompareSurface {
+public:
+    virtual ~ICompareSurface() = default;
+    virtual void setLayout(CompareLayout layout) = 0;
+    virtual void setPaneImage(int pane, const QImage& image, quint64 generation) = 0; // a null image clears the pane
+    virtual void setActivePane(int pane) = 0;
+    virtual void setFitMode(const QString& mode) = 0;
+    // {"panes":[ImageCanvas::viewState()...], "linked":bool, "active":int}
+    virtual QVariantMap viewportState() const = 0;
+    virtual void restoreViewport(const QVariantMap& state) = 0;
+    // "actual" | "rotate" | "linked" | "fit:<mode>" | "zoom-in" | "zoom-out" | "pan:<dx>,<dy>"
+    virtual void viewportCommand(const QString& command) = 0;
+    virtual qint64 admittedTextureBytes() const = 0;            // the window's canvas-group owner view
+};
+
+// Two-pane presentation state <-> compare state, so "send comparison to presentation" can reuse both.
+QVariantMap compareStateFromPresentation(const QVariantMap& presentation); // primary, secondary, linked, activeSecondary
+QVariantMap presentationStateFromCompare(const QVariantMap& compare);      // panes[0..1] only
+
+class CompareSession final : public QObject {
+    Q_OBJECT
+    Q_PROPERTY(int paneCount READ paneCount NOTIFY changed)              // 0, 2 or 4
+    Q_PROPERTY(int activePane READ activePane WRITE setActivePane NOTIFY changed)
+    Q_PROPERTY(bool linked READ linked WRITE setLinked NOTIFY changed)
+    // [{pane, path, name, state, tier, message}]: values only, no QImage in QML
+    Q_PROPERTY(QVariantList candidates READ candidates NOTIFY changed)
+public:
+    // Reads the controller's current selection generation, for handing a pane to DisplayManager. It never increments it: that
+    // would cancel the current image's decode. Pane requests are fenced by the session's own counter.
+    using GenerationSource = std::function<quint64()>;
+    explicit CompareSession(GenerationSource generations, QObject* parent = nullptr);
+    ~CompareSession() override;
+    int paneCount() const; int activePane() const; bool linked() const; QVariantList candidates() const;
+    void setActivePane(int pane); void setLinked(bool linked);
+
+    Q_INVOKABLE bool begin(int panes, const QStringList& paths);   // 2 or 4; paths.size() <= panes; files only
+    Q_INVOKABLE bool setCandidate(int pane, const QString& path);  // replace; cancels the pane's request
+    Q_INVOKABLE bool replaceActive(const QString& path);
+    Q_INVOKABLE void swap(int a, int b);
+    // Drops the pane's image, request and reservations. Never a file operation, never a confirmation.
+    Q_INVOKABLE void removeCandidate(int pane);
+    Q_INVOKABLE void end();                                         // releases everything to baseline
+
+    // Controller side. Results whose generation is not the pane's current one are dropped silently.
+    void deliver(int pane, quint64 generation, const QImage& image, const QString& tier);
+    void fail(int pane, quint64 generation, PaneState state, const QString& message);
+signals:
+    void changed();
+    void decodeRequested(int pane, const QString& path, quint64 generation, bool active);
+    void cancelRequested(int pane, quint64 generation);
+private:
+    struct Private;
+    std::unique_ptr<Private> d;
+};
+}
diff --git a/include/glancelot/contracts.hpp b/include/glancelot/contracts.hpp
index 14e3b70..f7584ec 100644
--- a/include/glancelot/contracts.hpp
+++ b/include/glancelot/contracts.hpp
@@ -2,7 +2,9 @@
 #include <cstddef>
 #include <cstdint>
 #include <filesystem>
+#include <optional>
 #include <string>
+#include <string_view>
 #include <vector>
 
 namespace glancelot {
@@ -15,15 +17,47 @@ struct NavigationSnapshot {
     BoundaryMode boundary{BoundaryMode::ContinueTree};
     std::uint64_t generation{};
 };
-struct DecodeRequest { AssetId asset; SourceRevision revision; std::size_t memoryLimit{256U*1024U*1024U}; };
+// DECODE-TIER (docs/contracts/DECODE-TIER.md). Preview < Fit < Full is one whole-image fidelity ladder. Tile is outside it (rank 0)
+// and never replaces a base image. The names are the worker wire and QML spelling.
+enum class DecodeTier : std::uint8_t { Preview, Fit, Full, Tile };
+constexpr int tierRank(DecodeTier tier) {
+    return tier == DecodeTier::Preview ? 1 : tier == DecodeTier::Fit ? 2 : tier == DecodeTier::Full ? 3 : 0;
+}
+constexpr std::string_view decodeTierName(DecodeTier tier) {
+    return tier == DecodeTier::Preview ? "preview" : tier == DecodeTier::Fit ? "fit" : tier == DecodeTier::Full ? "full" : "tile";
+}
+constexpr std::optional<DecodeTier> parseDecodeTier(std::string_view name) {
+    if (name == "preview") return DecodeTier::Preview;
+    if (name == "fit") return DecodeTier::Fit;
+    if (name == "full") return DecodeTier::Full;
+    if (name == "tile") return DecodeTier::Tile;
+    return std::nullopt;
+}
+inline constexpr int kFitBucketMinEdge = 512, kFitBucketMaxEdge = 8192; // powers of two; a bucket is a long edge in physical pixels
+// Region in Full-resolution, display-oriented source pixels. Tile only; empty otherwise.
+struct DecodeRegion {
+    int x{}, y{}, width{}, height{};
+    constexpr bool empty() const { return width <= 0 || height <= 0; }
+};
+struct DecodeRequest {
+    AssetId asset; SourceRevision revision; std::size_t memoryLimit{256U*1024U*1024U};
+    DecodeTier tier{DecodeTier::Full};   // the default is the behaviour before tiers
+    int sizeBucket{};                    // Fit: bucket long edge in physical pixels; 0 otherwise
+    DecodeRegion region;                 // Tile only
+    std::uint64_t generation{};          // the selection (or pane) generation that owns the request
+};
 enum class PixelFormat { Rgba8, Rgba16Float };
-struct PixelBufferDescriptor { int width{}, height{}, stride{}; PixelFormat format{PixelFormat::Rgba8}; std::string colorSpace{"sRGB"}; std::uint64_t generation{}; };
+struct PixelBufferDescriptor {
+    int width{}, height{}, stride{}; PixelFormat format{PixelFormat::Rgba8}; std::string colorSpace{"sRGB"}; std::uint64_t generation{};
+    DecodeTier tier{DecodeTier::Full};
+    int sourceWidth{}, sourceHeight{};   // display-oriented full source size; 0 = unknown (some Preview results)
+    bool finalTier{true};                // no higher tier will follow (a RAW without a sensor decoder, a refused Full)
+    std::string sourceToken;             // verified revision token; empty = unverified, so never cached
+};
 struct ViewportState { double physicalScale{1.0}, centerX{0.5}, centerY{0.5}; int quarterTurns{}; };
-struct DisplayCapabilities { std::string backend; double devicePixelRatio{1.0}; bool hdr{}, monitorProfileManaged{}; };
-// INPUT-02: scope is "global" | "workspace" | "canvas" | "grid" | "tree"; a hold command activates on press and restores
+// INPUT-02: scope is "global" | "workspace" | "canvas" | "grid" | "tree" | "compare" (reserved for packet C3); a hold command activates on press and restores
 // on release, focus loss, pointer cancel or image change. Defaults keep existing positional initializers valid.
 struct Command { std::string id, label, defaultBinding; bool repeat{};
                  std::string scope{"global"};
                  bool hold{}; };
-struct DecoderCapability { std::string format; bool still{}, animated{}, multiPage{}, preview{}, highPrecision{}; };
 }
diff --git a/include/glancelot/navigation.hpp b/include/glancelot/navigation.hpp
index 58525ff..d30d0c0 100644
--- a/include/glancelot/navigation.hpp
+++ b/include/glancelot/navigation.hpp
@@ -1,5 +1,6 @@
 #pragma once
 #include "contracts.hpp"
+#include <functional>
 #include <optional>
 #include <system_error>
 #include <stop_token>
@@ -14,14 +15,30 @@ std::vector<std::filesystem::path> imageFiles(const std::filesystem::path& direc
 // Stepping across a boundary starts at the target folder's visible sorted endpoint to avoid skipping images.
 // Ties break by natural name; entries whose metadata cannot be read sort last in both directions. Shuffle is a
 // deterministic permutation of (file name, seed): it does not change when files are added or removed elsewhere.
-enum class SortKey { Name, Modified, Size, Shuffle };
+// META-ROLES (docs/contracts/META-ROLES.md). Captured: EXIF capture time as a wall-clock value, ignoring any offset. Dimensions:
+// display-oriented pixel area, then width. Missing values are last in both directions and ties break by natural name. Until
+// packet A2 supplies keys every value is missing, so both give natural order. Append only.
+enum class SortKey { Name, Modified, Size, Shuffle, Captured, Dimensions };
 struct SortSpec {
     SortKey key{SortKey::Name};
     bool descending{false};
     std::uint64_t seed{1};
     bool operator==(const SortSpec&) const = default;
 };
-struct FileMeta { std::int64_t modified{}; std::uintmax_t size{}; bool known{}; };
+// `known` means the stat succeeded. The extended keys carry their own flags so "missing last" applies per key; they are
+// appended with defaults so positional FileMeta{modified, size, known} stays valid.
+struct FileMeta {
+    std::int64_t modified{}; std::uintmax_t size{}; bool known{};
+    std::int64_t captured{};                     // wall-clock milliseconds on a fixed epoch
+    std::uint32_t pixelWidth{}, pixelHeight{};   // display-oriented
+    bool capturedKnown{}, dimensionsKnown{};
+};
+// META-ROLES order path. Fills the extended keys for `files` (same length as `meta`) on the calling worker thread. It honours
+// `cancel`, reads a bounded header per file under IoScheduler Metadata admission, and leaves a *Known flag false for any file
+// it could not or would not read. Returns false when the folder budget ended the pass early (the result is then partial).
+// Packet A2 adds the parameters that carry it; core stays Qt-free because the app installs the reader.
+using SortKeyReader = std::function<bool(const std::vector<std::filesystem::path>& files,
+                                         std::vector<FileMeta>& meta, std::stop_token cancel)>;
 // The file order itself (exposed so ordering properties, e.g. unreadable metadata last, are testable).
 bool fileSortBefore(const SortSpec& sort, const std::filesystem::path& a, const FileMeta& metaA,
                     const std::filesystem::path& b, const FileMeta& metaB);
diff --git a/include/glancelot/operation_journal.hpp b/include/glancelot/operation_journal.hpp
new file mode 100644
index 0000000..cdd712c
--- /dev/null
+++ b/include/glancelot/operation_journal.hpp
@@ -0,0 +1,157 @@
+#pragma once
+#include "file_operations.hpp" // FileIdentity, TargetKind
+#include <QString>
+#include <QtGlobal>
+#include <cstddef>
+#include <cstdint>
+#include <memory>
+#include <optional>
+#include <stop_token>
+#include <vector>
+
+// Lead-frozen OPERATION-JOURNAL (docs/contracts/OPERATION-JOURNAL.md). Implemented by packet B2. Worker-thread only; never UI
+// or render code. One OperationJournal per process over a per-profile directory shared by all viewer processes. An intent is
+// durable before its mutation starts.
+namespace glancelot::journal {
+inline constexpr std::size_t kMaxItemsPerOperation = 100000;
+inline constexpr std::size_t kMaxIntentBatch = 64;        // items made durable per fsync
+inline constexpr qint64 kMaxOperationFileBytes = 64LL << 20;
+inline constexpr qint64 kMaxJournalBytes = 256LL << 20;   // records and preimages; completed operations pruned oldest first
+inline constexpr int kMaxRetainedOperations = 200;
+inline constexpr int kSchemaVersion = 1;
+inline constexpr int kMaxBackupFindings = 256;            // per reconcile pass; more are reported as a count
+
+enum class Kind : std::uint8_t { Trash, Copy, Move, Rename, CreateFolder, SidecarWrite, LosslessTransform, BatchRename };
+// Whether an operation may run when the journal cannot be written. Operations that work without a journal today keep working,
+// with an explicit notice and no undo. Features that exist only because of the journal refuse.
+constexpr bool requiresJournal(Kind kind) {
+    return kind == Kind::SidecarWrite || kind == Kind::LosslessTransform || kind == Kind::BatchRename;
+}
+enum class Outcome : std::uint8_t {
+    Pending, Done, Skipped, Missing, Changed, Denied, InUse, TrashUnavailable, Collision,
+    Cancelled, Failed,
+    Unknown,           // intent durable, no outcome: interrupted; resolved only by reconcile()
+    OriginalDisplaced  // Windows Overwrite: the target name is empty and the original is at ItemIntent::backupPath
+};
+enum class UndoCapability : std::uint32_t {
+    None = 0,
+    RestoreFromTrash = 1u << 0,   // trash token recorded and still present at undo time
+    ReverseRename = 1u << 1,
+    ReverseMove = 1u << 2,        // same-volume rename or verified copy back
+    TrashCopy = 1u << 3,          // undo a copy by trashing the created file (never permanent)
+    RestorePreimage = 1u << 4,    // sidecar or transform: sealed preimage retained in the journal
+};
+enum class State : std::uint8_t { Open, Running, Paused, Interrupted, Completed, Cancelled, Failed };
+
+struct OperationId { quint64 value{}; bool operator==(const OperationId&) const = default; };
+struct TargetSnapshot {
+    QString path;                   // absolute, as shown to the user
+    glancelot::TargetKind kind{glancelot::TargetKind::File};
+    bool link{};
+    glancelot::FileIdentity identity;
+    qint64 bytes{}, modified{};
+};
+struct ItemIntent {
+    quint32 index{};                // 0-based, stable, < kMaxItemsPerOperation
+    Kind kind{Kind::Trash};
+    TargetSnapshot source;
+    QString target;                 // destination path; empty for Trash
+    QString partialPath;            // temporary copy being written (".~gl<8 hex>.tmp" beside the target); never holds an original
+    // Where the original sits while an item replaces or renames it, durable before the step starts, so reconcile can find it.
+    // Overwrite: the existing target is `overwritten` and this is ".~gb<8 hex>.tmp" beside it. Case-only rename: `source` is the
+    // original and this is the intermediate ".tmp_rename_<id>". Empty when the item makes no such file.
+    std::optional<TargetSnapshot> overwritten;
+    QString backupPath;
+    QString requestedBy;            // ASSET-GROUP: the requested file that pulled this companion in
+};
+struct ItemOutcome {
+    quint32 index{};
+    Outcome outcome{Outcome::Failed};
+    glancelot::FileIdentity resultIdentity;   // the created or moved file, for undo revalidation
+    QString trashToken;             // POSIX: path in the trash; Windows: Recycle Bin item parsing name; empty if none
+    QString overwrittenTrashToken;  // Overwrite: where the replaced original went; empty when it was not trashed
+    QString preimagePath;           // RestorePreimage only, inside the journal directory
+    std::uint32_t undo{};           // UndoCapability bits valid for this item
+    QString message;
+};
+struct OperationHeader {
+    Kind kind{Kind::Trash};
+    QString origin;                 // "grid" | "tree" | "canvas" | "command" | "drop"
+    QString scope;                  // ASSET-GROUP "file" | "asset"
+    QString destination;            // copy or move directory, if any
+    std::optional<OperationId> undoOf;   // an undo is itself a journaled operation
+    quint32 itemCount{};
+};
+struct OperationSummary {
+    OperationId id; OperationHeader header; State state{State::Open};
+    quint32 done{}, failed{}, unknown{}, notStarted{}, displaced{};
+    std::uint32_t undoAvailable{};  // intersection over Done items; None if any Done item has none
+    quint64 revision{};             // increments on every durable change; UI drops older revisions
+    qint64 createdMs{}, updatedMs{};
+};
+enum class Code : std::uint8_t { Ok, Busy, Unwritable, Corrupt, LimitExceeded, Conflict, Cancelled };
+struct Status { Code code{Code::Corrupt}; QString detail; };
+
+// One backup found at startup that reconcile did not restore. Reported in the operation center; only explicit user actions
+// touch it, and neither action can delete it permanently.
+enum class BackupState : std::uint8_t {
+    Restored,        // moved back to the empty target name; reported once for the record
+    TargetOccupied,  // the target name holds a different file: the backup is an extra copy of the old original
+    IdentityMismatch,// the file at backupPath is not the recorded original: never moved automatically
+    RestoreFailed    // the target name is free and the identity matched, but the move back failed; message says why
+};
+struct BackupFinding {
+    OperationId operation; quint32 index{};
+    QString backupPath, targetPath;
+    BackupState state{BackupState::TargetOccupied};
+    QString message;
+};
+
+class Operation;   // move-only handle; holds the per-operation cross-process FileLock
+class OperationJournal {
+public:
+    // Null with Status when the directory cannot be used. Services then apply requiresJournal(): see the contract.
+    static std::unique_ptr<OperationJournal> open(const QString& directory, Status* status);
+    ~OperationJournal();
+    // Durable header (fsync) before any intent. Unwritable or LimitExceeded refuse the operation.
+    std::unique_ptr<Operation> begin(const OperationHeader& header, Status* status);
+    std::vector<OperationSummary> list(std::size_t max = kMaxRetainedOperations) const;
+    // Interrupted operations whose lock is free (owner process gone), and terminal operations with an OriginalDisplaced item.
+    // Probes the filesystem with exact stats; never re-executes; resolves Unknown items to done or not started with evidence.
+    struct Reconciled {
+        OperationId id;
+        quint32 resolvedDone{}, notStarted{}, partialsRemoved{}, backupsRestored{}, stillUnknown{};
+    };
+    struct ReconcileReport {
+        std::vector<Reconciled> operations;
+        std::vector<BackupFinding> backups;   // at most kMaxBackupFindings
+        quint32 backupsNotListed{};
+    };
+    ReconcileReport reconcile(std::stop_token cancel = {});
+    // Explicit user actions on a finding. Restore never replaces an existing file; trash goes through TrashBackend.
+    Status restoreBackup(OperationId operation, quint32 index);
+    Status trashBackup(OperationId operation, quint32 index);
+    Status dismiss(OperationId operation);    // completed or reconciled only; pruning never drops Interrupted or displaced items
+private:
+    OperationJournal();
+    struct Private;
+    std::unique_ptr<Private> d;
+};
+class Operation {
+public:
+    ~Operation();                   // without a terminal state the record stays Interrupted
+    Operation(Operation&&) noexcept;
+    Operation& operator=(Operation&&) noexcept;
+    OperationId id() const;
+    // Append up to kMaxIntentBatch intents and fsync. Mutate only items whose intent returned Ok.
+    Status recordIntents(const std::vector<ItemIntent>& intents);
+    // Append outcomes; fsync at the end of each batch and on state changes.
+    Status recordOutcomes(const std::vector<ItemOutcome>& outcomes);
+    Status setState(State state);   // Paused, Running, Cancelled, Completed or Failed; terminal states are final
+private:
+    friend class OperationJournal;
+    Operation();
+    struct Private;
+    std::unique_ptr<Private> d;
+};
+}
diff --git a/include/glancelot/resource_ledger.hpp b/include/glancelot/resource_ledger.hpp
new file mode 100644
index 0000000..1a98784
--- /dev/null
+++ b/include/glancelot/resource_ledger.hpp
@@ -0,0 +1,132 @@
+#pragma once
+#include <QObject>
+#include <QString>
+#include <QtGlobal>
+#include <array>
+#include <functional>
+#include <memory>
+
+// Lead-frozen RESOURCE-LEDGER (docs/contracts/RESOURCE-LEDGER.md). Implemented by packet E1.
+// One ledger per process: every image-sized allocation is reserved before it is made, committed when it exists and
+// released exactly once on success, cancel, worker death, stale completion or shutdown.
+namespace glancelot::resource {
+enum class Ledger : quint8 { CpuPixels, WorkerScratch, Transfer, RetainedCache, Thumbnails, GpuTextures };
+inline constexpr int kLedgerCount = 6;
+enum class Priority : quint8 { Foreground, Visible, Background, Speculative };
+enum class Pressure : quint8 { Normal, Elevated, Critical };
+enum class Refusal : quint8 { None, InvalidRequest, OwnerQuota, LedgerCeiling, AggregateLimit,
+                              PressureShed, TooManyReservations, ShuttingDown };
+enum class Enforcement : quint8 { Shadow, Enforce }; // Shadow: always admit, count what would have been refused
+
+inline constexpr qint64 kUnknown = -1;               // an unreadable or unlimited source; never treated as zero
+inline constexpr int kMaxLiveReservations = 4096;
+inline constexpr int kMaxOwners = 64;
+inline constexpr qint64 kDefaultSafetyReserveBytes = 64LL << 20;
+inline constexpr qint64 kDefaultFallbackBytes = 1LL << 30;
+// One foreground decode at the largest validated size: 256 MiB of pixels plus 2x that in worker scratch.
+inline constexpr qint64 kMaxForegroundHeadroomBytes = 768LL << 20;
+
+struct LimitSources {
+    qint64 userCapBytes{kUnknown};      // Settings or power profile (E3)
+    qint64 osHeadroomBytes{kUnknown};   // safe fraction of OS-available physical memory
+    qint64 cgroupLimitBytes{kUnknown};  // minimum over ancestors: memory.max (v2) or memory.limit_in_bytes (v1)
+    qint64 overheadBytes{0};            // measured non-image process overhead
+    qint64 safetyReserveBytes{kDefaultSafetyReserveBytes};
+    qint64 fallbackBytes{kDefaultFallbackBytes}; // when user, OS and cgroup are all unknown
+};
+// min(known of user, OS, cgroup) - overhead - safetyReserve, floored at 0; fallbackBytes when none is known.
+qint64 effectiveLimit(const LimitSources& sources) noexcept;
+
+struct ProbeOptions { QString procRoot{"/proc"}, cgroupRoot{"/sys/fs/cgroup"}; double osFraction{0.40}; };
+// Blocking file reads: worker thread only (LimitProbe), never the UI or render thread. Tests pass fake roots.
+LimitSources probeSystemLimits(const ProbeOptions& options = {});
+
+struct Thresholds { double reclaimAt{0.80}, recoverAt{0.65}, criticalAt{0.95}; int cooldownMs{2000}; };
+
+struct OwnerId {
+    quint32 value{};
+    constexpr explicit operator bool() const noexcept { return value != 0; }
+    bool operator==(const OwnerId&) const = default;
+};
+
+struct Request {
+    Ledger ledger{Ledger::CpuPixels};
+    Priority priority{Priority::Background};
+    OwnerId owner;
+    qint64 bytes{};           // checked stride*height (plus staging); >= 0, overflow checked by the caller
+    quint64 shareKey{};       // nonzero: physical storage identity (QImage::cacheKey of the detached buffer)
+    quint64 generation{};     // the caller's generation; diagnostics only, the ledger never judges staleness
+    const char* tag{""};      // static string, diagnostics only (see the contract's decisions)
+};
+
+struct ReservationState;
+// Move-only. Empty -> Reserved -> Owned -> Released. release() and the destructor are idempotent and thread-safe; the
+// bytes leave the ledger exactly once whichever path gets there first.
+class Reservation {
+public:
+    Reservation() = default;
+    Reservation(Reservation&&) noexcept;
+    Reservation& operator=(Reservation&&) noexcept;   // releases the held reservation first
+    Reservation(const Reservation&) = delete;
+    Reservation& operator=(const Reservation&) = delete;
+    ~Reservation();
+    explicit operator bool() const noexcept;          // Reserved or Owned
+    bool owned() const noexcept;
+    qint64 bytes() const noexcept;                    // what the owner holds (its quota view)
+    qint64 charged() const noexcept;                  // what this handle adds to physical totals (0 for a share)
+    Ledger ledger() const noexcept;
+    Refusal refusal() const noexcept;                 // the reason when empty after tryReserve
+    // Publication. actualBytes <= bytes(); the difference returns at once. Growing needs a new reservation.
+    // False and no change when empty, released or already owned.
+    bool commit(qint64 actualBytes);
+    void release() noexcept;
+private:
+    friend class ResourceLedger;
+    std::shared_ptr<ReservationState> state_;
+};
+
+class ResourceLedger final : public QObject {
+    Q_OBJECT
+public:
+    static ResourceLedger& instance();                // created on the GUI thread before any window
+    OwnerId registerOwner(const QString& diagnosticName);  // an empty OwnerId when kMaxOwners is reached
+    void unregisterOwner(OwnerId owner);              // the owner must have no live reservations
+    void setOwnerQuota(OwnerId owner, Ledger ledger, qint64 bytes);  // kUnknown = no per-owner quota
+    void setLedgerCeiling(Ledger ledger, qint64 bytes);              // kUnknown = aggregate only
+    void setAggregateMember(Ledger ledger, bool counts);             // GpuTextures: false in M1, see the contract
+    void setForegroundHeadroom(qint64 bytes);         // kept free from non-Foreground requests
+    void setLimitSources(const LimitSources& sources);
+    void setThresholds(const Thresholds& thresholds);
+    void setEnforcement(Enforcement enforcement);
+    void notifyExternalPressure(Pressure level);      // OS signal (Windows notification, PSI, memory.events)
+
+    // Never blocks or waits; O(1) under one mutex; any thread, including scene-graph render threads.
+    Reservation tryReserve(const Request& request);
+
+    struct LedgerSnapshot { qint64 charged{}, ceiling{kUnknown}, highWater{}; int live{}; quint64 refusals{}, wouldRefuse{}; };
+    struct Snapshot {
+        std::array<LedgerSnapshot, kLedgerCount> ledgers;
+        qint64 effectiveLimit{}, aggregate{}, highWater{};
+        Pressure pressure{Pressure::Normal};
+        quint64 revision{};                           // increments on every change; UI drops older snapshots
+    };
+    Snapshot snapshot() const;
+    qint64 ownerBytes(OwnerId owner, Ledger ledger) const;
+
+    enum class ReclaimTier : quint8 { Speculative, Hidden, Background, Quality };
+    // Runs on the GUI thread, never under the ledger lock, cheapest tier first. Returns the bytes it released through
+    // Reservation::release(); it must not allocate or reserve.
+    using Reclaimer = std::function<qint64(qint64 targetBytes)>;
+    quint64 addReclaimer(ReclaimTier tier, OwnerId owner, Reclaimer reclaimer);
+    void removeReclaimer(quint64 token);
+    void shutdown();                                  // refuse new reservations; existing ones still release
+signals:
+    void pressureChanged(glancelot::resource::Pressure level);   // queued, hysteresis applied
+    void capacityAvailable(glancelot::resource::Ledger ledger);  // coalesced, emitted outside the lock
+private:
+    ResourceLedger();
+    ~ResourceLedger() override;
+    struct Private;
+    std::unique_ptr<Private> d;
+};
+}
```
