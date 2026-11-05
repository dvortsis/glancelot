# PHASE2-CONTRACTS handoff

- Branch: `claude/phase2-contracts`
- Tip: the last commit on the branch. A commit can't name itself, so the report to the lead and the PR state its SHA. Code and headers are at `caa6a6a`; every later commit changes only `docs/`.
- Base: `b1d57cd` (`origin/main` when the branch was cut). `origin/main` has since moved to `f24f2e7`, which adds only merge-protection tooling and docs: outside `docs/`, `git diff --name-only b1d57cd f24f2e7` lists only `.gitattributes`, `AGENTS.md`, `.github/*` and `scripts/*`. No source, header or CMake file changed, so nothing here needed it Not merged in, because nothing conflicts. Not stacked on anything.
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
| A2 | `navigation.hpp` (`setDefaultSortKeyReader`), `directory_listing.hpp`, `directory_entry_model.hpp` (roles, `requestMetadata`, `requestAllMetadata`); also `settings_policy.cpp:140`, `SettingsDialog.qml:637-638`, `Main.qml:1827` |
| B1 | `file_operation_service.hpp`, `copy_move_service.hpp` (`planTransfer`, `confirmTransfer`), `mutation_service.hpp` (`planRename`, `confirmRename`) |
| B2 | `file_operations.hpp` (`trashToken`, `TrashAvailability`, `probeTrash`), `src/app/copy_safety.hpp`, the Overwrite prompt's QML, Overwrite and asset rename behind B1's entry points, the COPY-SAFETY-01 sweep test |
| C3 | `pane_layout.hpp`, plus named regions of `viewer_controller.*` and `Main.qml` |
| D1 | `display_manager.hpp`, `viewer_controller.hpp` (`imageTier`, `imageFinal`, `imageColour`), `src/app/decode_cache.hpp` |
| D2 | `src/app/viewer_context.hpp`, `Main.qml` `reference-pin` handler and `referenceWindow` block |
| F1 | `Dockerfile` `ENV`: `GLANCELOT_DEPLOYMENT=container` |

## Lead decisions written in

- **OPERATION-JOURNAL:** at startup, B2 finds `.~gb*.tmp` backups left by a crash or error 1177 inside `ReplaceFileW`, and restores or reports them. The backup path is made durable in the intent before the replace, because the name is random and nothing else could find it. See "Restart reconciliation" in the contract.
- **CAPABILITY:** the reference window's platform check and unsupported state (G-5), with a pure decision function asserted in the compile test and a runtime expose deadline. See "Reference window check and unsupported state".

## Pre-handoff checklist

### 1. Fail-before evidence

At `b1d57cd` the test cannot compile: `SortKey::Captured`, `DecodeTier` and the new headers do not exist. Every assertion was also checked by breaking its target, in `docs/verification/PHASE2-CONTRACTS/fail-before.txt` (MSVC, actual output). The review round's new and changed assertions were broken the same way, in `docs/verification/PHASE2-CONTRACTS/fail-before-r2.txt`: all 10 fail, by static assertion, by test exit 1 or by a moc compile error. The first round's results:

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

`git diff --name-only b1d57cd..caa6a6a`. Commits after `caa6a6a` touch only `docs/`.

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
docs/handoff/PHASE2-CONTRACTS-HANDOFF.md
docs/verification/PHASE2-CONTRACTS/fail-before.txt
docs/verification/PHASE2-CONTRACTS/full-suite-tail.txt
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

Targeted (Windows, MSVC 2022, Qt 6.8.3, offscreen), on `caa6a6a`. `fs_watch` is added for review finding B3:

```text
 1/11 Test  #3: navigation .......................   Passed    2.96 sec
 2/11 Test  #4: directory_listing ................   Passed   19.99 sec
 3/11 Test  #5: file_operations ..................   Passed    0.25 sec
 4/11 Test  #7: viewer_acceptance ................   Passed   10.60 sec
 5/11 Test #10: pane_layout ......................   Passed    1.72 sec
 6/11 Test #16: directory_model ..................   Passed   31.34 sec
 7/11 Test #23: fs_watch .........................   Passed   87.66 sec
 8/11 Test #31: cache ............................   Passed    5.34 sec
 9/11 Test #37: copy_safety ......................   Passed    0.91 sec
10/11 Test #45: contract_headers .................   Passed    0.07 sec
11/11 Test #51: reference_pin ....................   Passed   40.71 sec
100% tests passed out of 11
```

Full suite, `scripts/build.ps1 -BuildDir build-claude-phase2-contracts -Test` through `scripts/build-slot.ps1`, on `caa6a6a`:

```text
100% tests passed out of 52
Total Test time (real) = 1080.81 sec
script exit: 0
No baseline failures: every test passed, so none is named. Budget tests passed on the first run.
```

Log tail: `docs/verification/PHASE2-CONTRACTS/full-suite-tail.txt`.

Other checks:

- `pr_guard.py` from `origin/main`: `pr_guard: code=True, 0 error(s)`.
- Linux GCC 13.3 (WSL) with the project's flags, on the three `FileMeta{a,b,c}` sites: `navigation.cpp` and `directory_listing.cpp` compile with 0 warnings, so positional initializers stay clean under `-Wextra`.
- Reference check: a script compared the `file:line` references in the contracts and the decision record against `b1d57cd`. 119 resolve, with the file present and the line in range. The two that don't are `CompareView.qml`, which C3 creates, and the review document, which lives on `claude/review-phase2-contracts`. The script does not check what a line says; I read those lines while rewriting, and a reviewer may want to sample them.

### 5. Decisions taken

All in `docs/decisions/2026-11-05-phase2-contracts.md`: 76 rows, each draft question plus each problem found against `main`. Problems the drafts got wrong, beyond stale line numbers:

- **Pane generations (P6).** Taking a generation from the controller's counter would cancel the current image's decode. The session owns its own counter.
- **Pane decode path (P7).** `Private::decode` is bound to one worker and the selection, so C3 models its path on `startPrefetch`.
- **Case-only rename and partials (J7).** The draft's rule, "reconcile deletes partials", would have deleted the user's file at `.tmp_rename_<id>`. Originals now go in `backupPath`, which is never deleted.
- **Ladder state (D7).** Phase 1 code gates on `imageReady`, `loading` and now `navigating`, so the contract says what each means during a refinement.
- **`modeValid` (P8).** It is shared with settings load and presets, so widening it would restore compare from a hand-edited file.
- **Transfer buffers (R6, lead decision).** The contracts state rules for an abstract transfer buffer, because PR 3 round 5 replaces its transfer class. E1, D1 and C3 start after PR 3 merges.
- **Sort settings (M8).** `settings_policy.cpp:140` would reject a saved new key.

## Open questions for the lead

None. The lead confirmed J1 and J4 and decided B1, B2 and B3.

For the lead:

- **Plan edges.** Please add **D1 -> D2** (B8: the HUD and `colour.source` read `imageTier` and `imageColour`). If D2 must land first, its brief can grant the `imageColour` property alone, with `tagged` parsed from the worker's two existing strings. E1, D1 and C3 start after PR 3 merges. Asset-scope rename arrives with B2, not B1.
- **C3 and D1.** I recommend ordering C3 after D1 (CP-N5). Before D1, 4-up holds 16 MP per pane and 2-up holds 32 MP, so typical camera files show `TooLarge` in 4-up. The contract is honest about it either way.
- **PR 3 round 5.** Not on `origin/claude/stack-confirm-fix` at this push (`9c0c919`), so the transfer wording isn't checked against it yet. Please have it checked when round 5 lands. The contracts name no class, so the check is that the rules hold.
- **A1** is working in the listing and navigation code. A2's diff no longer changes `listDirectory`'s parameters, which lowers the collision.
- G-5 is D2's acceptance, and STACK-CONFIRM's carve-out can point at this contract.

## Review fixes

Review: `docs/verification/PHASE2-CONTRACTS-REVIEW.md` on `claude/review-phase2-contracts` (`979206a`), CHANGES REQUIRED at `53dab6c`. J1 and J4 were not reopened.

| Finding | Fix |
| --- | --- |
| **B1** no-trash volume (lead decision) | OPERATION-JOURNAL: `TrashBackend::probeTrash` in B2's diff, run at the plan step. **B2 builds the Overwrite prompt**; the false claim about an existing confirmation is corrected, citing `pasteTo` (`copy_move_service.cpp:708`). `Unavailable` or `Unknown` says replaced files are permanently deleted and needs explicit consent (`permanentReplaceConsent`, recorded in `OperationHeader`). A failed trash move never falls through to removal: the operation stops and reports, and the original stays (`BackupState::TrashFailed`, `heldBackups`). A route that can't keep a backup needs the same consent (`allowNoBackup`). Execution table and tests added |
| **B2** transfer class (lead decision) | RESOURCE-LEDGER, DECODE-TIER and COMPARE-PANES state rules for an abstract transfer buffer and name no class. No leak is charged. The acceptance test now holds teardown open through a hook. E1, D1 and C3 start after PR 3 |
| **B3** EN-11 refresh (lead decision) | DECODE-TIER: `DecodeRequest::refresh` may replace an equal tier. **Differs from the review's proposal in one way:** a refresh never starts below what's shown, so the view never drops in quality and `DisplayManager` needs nothing more. A higher rank under a new token just applies, because one generation's decodes are sequential, so no restart and no loop bound are needed. Citation corrected, site added to the table, `fs_watch` named in the acceptance tests and run here |
| **B4** read items | `OperationJournal::items`, paged, and `ItemRecord` |
| **B5** preimage order | `ItemIntent::preimagePath`, `Operation::preimagePathFor`, `Operation::sealPreimage`; `ItemOutcome::preimagePath` removed; reconcile table for in-place writes; kill tests |
| **B6** share key | `Reservation::commit(actualBytes, shareKey = 0)`, plus a compile check |
| **B7** key pass | `requestAllMetadata`, `metadataPending`, `metadataPartial` in A2's diff; consumers, M5 and tests updated |
| **B8** colour path | `imageColour` in D1's diff; CAPABILITY reads it on `imageChanged`. D1 -> D2 recorded above |
| OJ-N1 | Header comments corrected; `OperationJournal::probe()` at the plan step; a `begin` failure returns to the prompt once |
| OJ-N2 | `kMaxOperationFileBytes` 256 MiB, `kMaxJournalBytes` 1 GiB, `totalPathBytes` for `begin`; `LimitExceeded` follows the failure policy; relation asserted at compile time |
| OJ-N4 | `OperationSummary::reconciled`, durable `reconciled` record |
| RL-N1 | `kMaxForegroundHeadroomBytes` 1 GiB; fallback raised by the reserve so today's largest decode still fits; both asserted |
| AG-N1 | `kMaxExpandedItems` (asserted equal to the journal's limit), `kMaxExcludedListed`, `excludedNotListed` |
| AG-N2 | `kMaxProbesPerTarget` removed. A current hint's members are statted; otherwise one directory read per parent, within `kExpandBudgetMs`; `companionsIncomplete` |
| CP-N1 | `setSurface`, and the ledger owner in the constructor |
| MR-N1 | `setDefaultSortKeyReader`, process-wide. `listDirectory` needs no new parameter |
| OJ-N3, N5 to N10 | Reconcile tables by kind; asset rename journaled and `Kind::BatchRename`; sweep kept only when the journal is unavailable, with an age bound; a case-only rename restores the old spelling; `replacedPath` on both platforms; POSIX directory fsync; D7's cap noted |
| AG-N3 to N6 | Two files of one family collide; `truncated` argument; frozen `planTransfer` and `planRename` diffs; `expand` for SidecarWrite returns the request |
| RL-N2 to N7 | `IoScheduler` keeps its byte check until M2; retired owners; public constructor for tests; `GpuTextures` out of the charge order, and a move may exceed a ceiling; `Quality` never swaps shown pixels; tier priorities in DECODE-TIER |
| DT-N1 to N4 | `kFitMaxSourceBytes` per format class; re-publishes keep their tier; a Fit insert evicts other buckets; dimensions compared only under one non-empty token |
| CP-N2 to N6 | No file access in `begin`; comment corrected; reclaim only when not exposed; `TooLarge` names the limit and settles at probe; 256 MiB quota; `deliver` uses the apply rule. C3-after-D1 is for the lead, above |
| MR-N2 to N5 | Admission wait counted in the budget, with one thumbnail slot kept; time budget on anything not known to be an SSD; landing rule; one pass per folder and revision; overlay only when ratings are wanted |
| CA-N1 to N4 | `serverPaths` a process fact; one builder per section and scope; runtime `Unsupported` survives a rebuild; D6 owns the bit-depth parser; Docker text until F1; populators in the viewer sources |
| Section 10, cmake comment | "Stay unreachable" dropped, since nothing asserts it |

## Failure and cancellation cases

This packet has no runtime paths beyond the two natural-order lines, which the test covers in both directions through `fileSortBefore`, `sortedImageFiles` and `listDirectory`. Each contract's "Failure and cancellation" section is what the implementing packets must cover.

## Unverified

- Docker build: not run here. The lead's gate decides.
- Linux full build: not run here. `build.yml` covers it on the PR, and WSL compiled only the two core files above.
- Whether `QPlatformIntegration::MultipleWindows` really reports true on `windows`, `xcb`, `wayland` and `offscreen`: the enumerator exists in Qt 6.8.3's header, but nothing calls it yet. D2's acceptance tests check it on the gate platforms, and the runtime deadline covers a wrong answer.
- The moc-generated code for the new QObjects is compiled but not linked, because the classes have no definitions until C3, D2 and E1.

## Exact `include/` diff

`git diff b1d57cd caa6a6a -- include/`

```diff
diff --git a/include/glancelot/asset_group.hpp b/include/glancelot/asset_group.hpp
new file mode 100644
index 0000000..fdc6e1b
--- /dev/null
+++ b/include/glancelot/asset_group.hpp
@@ -0,0 +1,83 @@
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
+inline constexpr std::size_t kMaxExpandedItems = 100000;  // per expansion, equal to journal::kMaxItemsPerOperation
+inline constexpr std::size_t kMaxExcludedListed = 256;    // Expansion::excluded entries kept; more are counted
+inline constexpr int kExpandBudgetMs = 2000;              // per plan, spent on companion lookups; see Expansion
+
+// Raw: a camera RAW. Rendered: the camera-native companion formats (JPEG, HEIF, AVIF, JPEG XL). Derived: an exported or edited
+// format (TIFF, PNG, WebP), a member only when it is the stem's sole image, so a RAW's asset never pulls in an edited master.
+enum class Role : std::uint8_t { Raw, Rendered, Derived, Sidecar, MotionVideo, Other };
+enum class SidecarStyle : std::uint8_t { None, Stem /*IMG.xmp*/, FullName /*IMG.CR2.xmp*/ };
+enum class Match : std::uint8_t { Stem, ContentId };    // ContentId: reserved for a confirmed Apple content identifier (ADV-09)
+enum class GroupFlag : std::uint32_t {
+    None = 0,
+    Collision = 1u << 0,        // more than one Raw, more than one Rendered family or two files of one family, case
+                                // variants, two Derived with nothing else, or > kMaxMembers
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
+// Pure, O(n log n), no I/O. Same directory only; folders and links are never grouped. `truncated` sets Incomplete.
+GroupingSnapshot groupListing(const std::filesystem::path& directory, const std::vector<ListingInput>& entries,
+                              bool caseSensitive, std::uint64_t listingGeneration, bool truncated = false);
+
+enum class Scope : std::uint8_t { FileOnly, Asset };
+enum class Operation : std::uint8_t { Trash, Copy, Move, Rename, SidecarWrite, LosslessTransform };
+enum class Exclusion : std::uint8_t { Ambiguous, Collision, NotApplicable, Missing, LimitReached };
+struct Planned { Member member; std::filesystem::path requestedBy; bool companion{}; };
+struct Excluded { std::filesystem::path path; std::filesystem::path requestedBy; Exclusion reason{Exclusion::NotApplicable}; };
+struct Expansion {
+    std::vector<Planned> included;           // deduplicated, requested items first, then companions in role order
+    std::vector<Excluded> excluded;          // shown in the confirmation; never mutated; at most kMaxExcludedListed
+    std::size_t excludedNotListed{};         // beyond kMaxExcludedListed
+    bool companionsIncomplete{};             // kExpandBudgetMs ended the lookups: unchecked companions stay out, and the confirmation says so
+    bool cancelled{};                        // no partial plan can be confirmed
+};
+// Worker only. FileOnly returns exactly `requested` plus companions as excluded (NotApplicable) so the confirmation can show
+// them. Asset adds companions per the contract's policy table. With a current hint it stats only the hint's members; without
+// one it reads each parent directory once, so mixed-case names are found. The result is re-checked, so a stale hint adds nothing.
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
index 0000000..c949fa4
--- /dev/null
+++ b/include/glancelot/capability_model.hpp
@@ -0,0 +1,97 @@
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
+    // serverPaths is a process fact set once at startup, so it does not drop when the Source section clears.
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
index 0000000..61f7c5a
--- /dev/null
+++ b/include/glancelot/compare_panes.hpp
@@ -0,0 +1,90 @@
+#pragma once
+#include "resource_ledger.hpp"
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
+// and never touches files: it asks the window's ViewerController to decode through the pane decode path and receives values.
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
+    // `owner` is this window's compare ledger owner, charged for every pane.
+    CompareSession(GenerationSource generations, resource::OwnerId owner, QObject* parent = nullptr);
+    ~CompareSession() override;
+    void setSurface(ICompareSurface* surface);   // not owned; null detaches. deliver, swap and removal drive it
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
index 14e3b70..53e3a6c 100644
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
@@ -15,15 +17,48 @@ struct NavigationSnapshot {
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
+    bool refresh{};                      // the shown pixels aren't proven current: the result may replace an equal tier
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
index 58525ff..6ac8994 100644
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
+// Packet A2 adds the process-wide install point; core stays Qt-free because the app installs the reader.
+using SortKeyReader = std::function<bool(const std::vector<std::filesystem::path>& files,
+                                         std::vector<FileMeta>& meta, std::stop_token cancel)>;
 // The file order itself (exposed so ordering properties, e.g. unreadable metadata last, are testable).
 bool fileSortBefore(const SortSpec& sort, const std::filesystem::path& a, const FileMeta& metaA,
                     const std::filesystem::path& b, const FileMeta& metaB);
diff --git a/include/glancelot/operation_journal.hpp b/include/glancelot/operation_journal.hpp
new file mode 100644
index 0000000..b6878a5
--- /dev/null
+++ b/include/glancelot/operation_journal.hpp
@@ -0,0 +1,176 @@
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
+inline constexpr qint64 kMaxOperationFileBytes = 256LL << 20;  // room for kMaxItemsPerOperation at ordinary path lengths
+inline constexpr qint64 kMaxJournalBytes = 1LL << 30;     // records and preimages; completed operations pruned oldest first
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
+    QString replacedPath;           // Overwrite: "<stem> (replaced)<ext>", the name the original takes on its way to the trash
+    // SidecarWrite, LosslessTransform: Operation::preimagePathFor(index), inside the journal directory. Empty when there is no
+    // prior file (a new sidecar). Operation::sealPreimage fills it before the item may touch the original.
+    QString preimagePath;
+    QString requestedBy;            // ASSET-GROUP: the requested file that pulled this companion in
+};
+struct ItemOutcome {
+    quint32 index{};
+    Outcome outcome{Outcome::Failed};
+    glancelot::FileIdentity resultIdentity;   // the created or moved file, for undo revalidation
+    QString trashToken;             // POSIX: path in the trash; Windows: Recycle Bin item parsing name; empty if none
+    QString overwrittenTrashToken;  // Overwrite: where the replaced original went; empty when it was not trashed
+    std::uint32_t undo{};           // UndoCapability bits valid for this item
+    QString message;
+};
+struct OperationHeader {
+    Kind kind{Kind::Trash};
+    QString origin;                 // "grid" | "tree" | "canvas" | "command" | "drop"
+    QString scope;                  // ASSET-GROUP "file" | "asset"
+    QString destination;            // copy or move directory, if any
+    bool permanentReplaceConsented{};    // the user confirmed that replaced files are permanently deleted here
+    quint64 totalPathBytes{};       // UTF-8 length of every path in the plan, so begin can refuse an operation that can't fit
+    std::optional<OperationId> undoOf;   // an undo is itself a journaled operation
+    quint32 itemCount{};
+};
+struct OperationSummary {
+    OperationId id; OperationHeader header; State state{State::Open};
+    quint32 done{}, failed{}, unknown{}, notStarted{}, displaced{};
+    quint32 heldBackups{};          // items whose original still sits at backupPath or replacedPath
+    bool reconciled{};              // a reconcile pass has resolved this operation; durable
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
+    RestoreFailed,   // the target name is free and the identity matched, but the move back failed; message says why
+    TrashFailed      // an Overwrite's replaced original couldn't reach the trash; it stays where the finding says
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
+    // Null with Status when the directory cannot be used. Callers then apply requiresJournal(): see the contract.
+    static std::unique_ptr<OperationJournal> open(const QString& directory, Status* status);
+    ~OperationJournal();
+    // Plan step, before the confirmation: a synced scratch append, so a confirmation can say the journal is unavailable.
+    Status probe();
+    // Durable header (fsync) before any intent. Null with Status on failure; the caller applies requiresJournal().
+    std::unique_ptr<Operation> begin(const OperationHeader& header, Status* status);
+    std::vector<OperationSummary> list(std::size_t max = kMaxRetainedOperations) const;
+    struct ItemRecord { ItemIntent intent; std::optional<ItemOutcome> outcome; };
+    // One page, at most min(max, kMaxIntentBatch) records from `first`; empty past the end. Conflict for an unknown id. A Corrupt
+    // operation returns its good prefix with Status Corrupt.
+    std::vector<ItemRecord> items(OperationId operation, quint32 first, std::size_t max, Status* status) const;
+    // Interrupted operations not yet reconciled whose lock is free (owner process gone), and any operation with heldBackups or
+    // displaced items. Probes the filesystem with exact stats; never re-executes; resolves Unknown items to done or not started
+    // with evidence and records the pass durably.
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
+    Status dismiss(OperationId operation);    // completed or reconciled only; Conflict while a backup is held; pruning skips those too
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
+    QString preimagePathFor(quint32 index) const;
+    // After recordIntents, before the item touches its original: copies the source to the intent's preimagePath, syncs it and
+    // records its checksum. LimitExceeded when it can't fit after pruning; the item then must not run.
+    Status sealPreimage(quint32 index);
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
index 0000000..fe0e25a
--- /dev/null
+++ b/include/glancelot/resource_ledger.hpp
@@ -0,0 +1,137 @@
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
+// One foreground decode at the largest validated size, all held at once: 256 MiB of pixels, the same again in transfer,
+// and 2x that in worker scratch.
+inline constexpr qint64 kMaxForegroundHeadroomBytes = 1LL << 30;
+// With every source unknown, that decode still fits once the safety reserve is taken off.
+inline constexpr qint64 kDefaultFallbackBytes = kMaxForegroundHeadroomBytes + kDefaultSafetyReserveBytes;
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
+    // A nonzero shareKey publishes the buffer for share hits (a QImage has no cacheKey until it exists, so a reservation
+    // made before the allocation can't name it). If an owned entry with that key already exists, this handle becomes a
+    // share and its charge returns at once. False and no change when empty, released or already owned.
+    bool commit(qint64 actualBytes, quint64 shareKey = 0);
+    void release() noexcept;
+private:
+    friend class ResourceLedger;
+    std::shared_ptr<ReservationState> state_;
+};
+
+class ResourceLedger final : public QObject {
+    Q_OBJECT
+public:
+    ResourceLedger();                                 // tests own theirs; production uses instance()
+    ~ResourceLedger() override;
+    static ResourceLedger& instance();                // created on the GUI thread before any window
+    // Owners and setters: any thread, under the same lock as tryReserve.
+    OwnerId registerOwner(const QString& diagnosticName);  // an empty OwnerId when kMaxOwners is reached
+    void unregisterOwner(OwnerId owner);              // retired: no new reservations; freed when its last handle releases
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
+    struct Private;
+    std::unique_ptr<Private> d;
+};
+}
```
