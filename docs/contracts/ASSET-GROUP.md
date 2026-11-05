# ASSET-GROUP contract

Status: **frozen** by PHASE2-CONTRACTS. Header: [`include/glancelot/asset_group.hpp`](../../include/glancelot/asset_group.hpp), the authoritative text of the interface. Code references are at `b1d57cd`. Owner: Track B. Implementing packet: B1 (F022). Decisions and their alternatives: [2026-11-05-phase2-contracts.md](../decisions/2026-11-05-phase2-contracts.md).

## Purpose

Group the files that users think of as one photo, and make every mutation choose **explicitly** between the named files (file-only) and the whole asset. Kinds:

- **RAW+rendered pair:** `IMG_0001.CR2` + `IMG_0001.JPG` or `.HEIC`.
- **Sidecars:** `IMG_0001.xmp` (Lightroom and Capture One style, stem match) and `IMG_0001.CR2.xmp` (darktable style, full-name match).
- **Motion pairs:** Apple Live Photo `IMG_0001.HEIC` + `IMG_0001.MOV`. An Android motion photo is an *embedded* payload: a flag on one member, never a second file.

`planDeletion` (`file_operations.hpp`) says "paired/sidecar files are never added implicitly", and `docs/DELETE-PLAN.md` defers paired sets to M4. That default stays: **file-only is the default every time, and asset scope is a per-operation user choice made in the confirmation.** The choice is not remembered.

## Consumers

**B1** (grouping service, companions previewed in every delete, copy, move and rename confirmation, file-only versus asset choice), **B2** (the journal records the expanded exact list), **B4** batch rename (stem rename of whole assets), **B5/B6** basket and drag/drop (same expansion), **C1** XMP engine (which sidecar a group writes), **C2/C5** culling (one rating per asset), **D7** lossless transforms (always file-only).

## Frozen interface

The header is frozen as committed: `kMaxMembers`, `kMaxExpandedItems`, `kMaxExcludedListed`, `kExpandBudgetMs`, `Role`, `SidecarStyle`, `Match`, `GroupFlag`, `Member`, `Group`, `GroupingSnapshot`, `ListingInput`, `groupListing`, `Scope`, `Operation`, `Exclusion`, `Planned`, `Excluded`, `Expansion`, `expand` and `renameAsset`.

**Where it lives.** B1 implements it in `src/app/asset_group.cpp`, added to `glancelot_platform`. Classification calls `RawPreviewExtractor::isRawExtension` (`raw_preview.hpp:19`) and `renameAsset` applies `MutationService::isValidFileName` (`mutation_service.hpp:34`), both Qt. The header itself stays free of Qt.

**Applied by B1 (B1's brief grants this exact diff)**, `include/glancelot/file_operation_service.hpp`. Existing calls keep file-only behaviour:

```diff
-    Q_INVOKABLE quint64 plan(const QStringList& paths, const QString& origin);
+    Q_INVOKABLE quint64 plan(const QStringList& paths, const QString& origin);   // == scope "file"
+    // scope: "file" | "asset". The summary gains companions [{path, role, requestedBy, included,
+    // exclusion}] and scope; confirm() executes exactly the snapshotted included list.
+    Q_INVOKABLE quint64 planScoped(const QStringList& paths, const QString& origin, const QString& scope);
```

`include/glancelot/copy_move_service.hpp`. `enqueueTransfer` (`:63`) stays for existing callers. B1 adds the plan step, so copy and move get a confirmation. Until B2, `confirmTransfer` accepts only `Skip` and `RenameBoth`. B2 adds Overwrite, the `replaceFate` summary key and the consent argument's effect (OPERATION-JOURNAL):

```diff
+    // scope: "file" | "asset". Summary as planScoped, plus collisions [{path, target, requestedBy}] and, from B2, replaceFate
+    // "trash" | "permanent" per the trash probe. confirmTransfer executes exactly the snapshotted list; one collision policy per plan.
+    Q_INVOKABLE quint64 planTransfer(const QStringList& sourcePaths, const QString& destinationDirectory,
+                                     glancelot::CopyMoveService::TransferMode mode, const QString& scope);
+    Q_INVOKABLE bool confirmTransfer(quint64 id, glancelot::CopyMoveService::CollisionPolicy collision,
+                                     bool permanentReplaceConsent = false);
@@ signals:
+    void transferPlanReady(quint64 id, const QVariantMap& summary);
```

`include/glancelot/mutation_service.hpp`. `renamePath` (`:60`) stays for a single-file rename. Asset scope needs a worker for `expand` and the journal for recovery, so B1 adds the plan and preview and B2 enables asset-scope `confirmRename`, as `Kind::BatchRename`. B4 builds batch rename on the same pair:

```diff
+    // scope: "file" | "asset". Summary: renames [{from, to}], collisions, excluded. Asset scope returns a refusal until B2 lands.
+    Q_INVOKABLE quint64 planRename(const QString& sourcePath, const QString& newName, const QString& scope);
+    Q_INVOKABLE bool confirmRename(quint64 id);
@@ signals:
+    void renamePlanReady(quint64 id, const QVariantMap& summary);
+    void renameFinished(quint64 id, const QVariantList& results);
```

`FileOperationService`'s summary lists at most `kMaxExcludedListed` companions, with a count for the rest.

## Rules

**Stem and case.** Stem = file name up to the last dot (`IMG.CR2.xmp` has stem `IMG.CR2`, which makes it a FullName sidecar of `IMG.CR2`). Keys are case-folded (`QString::toCaseFolded` semantics) on case-insensitive volumes, and exact on case-sensitive ones. On a case-sensitive volume, `a.JPG` and `a.jpg` are distinct members and set `Collision`.

**Classification.** The extension tables live in the `.cpp` and are versioned with tests.

| Role | Extensions | Rule |
| --- | --- | --- |
| `Raw` | `RawPreviewExtractor::isRawExtension` | Always a member of its stem |
| `Rendered` | jpg, jpeg, heic, heif, avif, jxl (families: jpeg = jpg+jpeg; heif = heic+heif; avif; jxl) | Always a member of its stem. Two files of one family set `Collision` |
| `Derived` | tif, tiff, png, webp | A member only when the stem has no `Raw` and no `Rendered` file, so a sidecar can attach to it. Beside a `Raw` or `Rendered` file it is left out of the group: exports and edited masters are never pulled into an asset. It appears in the confirmation as excluded (`NotApplicable`) |
| `Sidecar` | xmp | See below |
| `MotionVideo` | mov, mp4 | Only when a `Rendered` or `Raw` member shares the stem. Video with no partner is never grouped |
| `Other` | Everything else | Never grouped |

**Groups.** Two or more members sharing a stem key. Primary = Raw if present, else Rendered, else Derived. More than one Raw, more than one Rendered family or two files of one family (`IMG.jpg`+`IMG.jpeg`), two Derived with no Raw or Rendered, case variants or more than `kMaxMembers` candidates set `Collision`. `groupListing` sets `Incomplete` from its `truncated` argument. A Stem sidecar in a directory where the stem key maps to a Collision group sets `AmbiguousSidecar`. A FullName sidecar always attaches to its exact file.

**Motion pairs** match by stem only for B1. Every companion is listed by name in the confirmation before anything happens, so a coincidental stem match cannot act silently. `Match::ContentId` is reserved for ADV-09.

**Expansion policy (Asset scope):**

| Operation | Companions added | Never added |
| --- | --- | --- |
| Trash, Copy, Move | All members of the requested file's group | Members of a `Collision` group other than the requested one (shown as `Collision`); `AmbiguousSidecar` (shown as `Ambiguous`) |
| Rename | All members, renamed by `renameAsset`; every target is checked for collisions (case folding included) before anything runs | Same as above. A Collision group falls back to FileOnly with an explanation |
| SidecarWrite (C1) | Not an expansion: `expand` returns the request unchanged, and C1 resolves *which* sidecar a group writes (C1 owns the naming profile) | Writes never touch image members |
| LosslessTransform (D7) | None: always FileOnly | Sidecar orientation fields are C1 and D7 scope |

Collision members are excluded and the operation continues with the rest, with each exclusion and its reason in the confirmation. The user can still select them explicitly in file-only scope.

When a requested file is itself a companion (the user selected the `.xmp`), Asset scope expands to the whole group the same way. Two requested members of one group are deduplicated. `included` order is stable: requested items first, then companions in role order.

**Overwrite.** Copy and Move collisions under asset scope take one decision per asset, not per member, and each overwritten member goes through the COPY-SAFETY-01 path (`copy_safety::replaceFileAtomically`). The decision applies only to members that collide.

**Snapshot and revalidation.** `expand` runs inside the existing plan step (worker). Its `included` list becomes the immutable target snapshot (`planDeletion` input, or journal intents), and from there on `executeDeletion`-style identity revalidation applies unchanged.

**Generation fencing.** A `GroupingSnapshot` carries its listing generation. Grid badges and confirmation previews drop snapshots older than the current listing generation. `expand` never trusts the snapshot for existence (it re-probes), so a stale hint can only cost probes, never add a wrong file.

**Companion lookup.** A `plan()` for 10,000 files on a network share must stay usable, and it lists companions too. With a current hint, `expand` stats only the hint's members. Without one, it reads each parent directory once, which also finds mixed-case names that guessing can't. `kExpandBudgetMs` bounds the lookups per plan. When it runs out, `companionsIncomplete` is set: unchecked companions stay out, and the confirmation says "Companions couldn't all be checked."

**Bounds.** At most `kMaxMembers` per group and `kMaxExpandedItems` included per expansion, past which entries are `LimitReached`. `excluded` keeps at most `kMaxExcludedListed`, with the rest counted in `excludedNotListed`. Requests are capped by the plan's existing limit (100,000 entries). Grouping is pure and runs on the listing worker. Badges consume the snapshot on the UI thread as values.

## Failure and cancellation

| Case | Result |
| --- | --- |
| Cancel during lookups | `Expansion::cancelled`; no partial plan can be confirmed |
| Companion deleted after grouping (stale hint) | `Missing` in `excluded`, not included |
| `kMaxExpandedItems` or `kMaxMembers` exceeded | `LimitReached` in `excluded`; never silently dropped |
| `kExpandBudgetMs` exhausted | `companionsIncomplete`; the confirmation says so, and unchecked companions stay out |
| Companion replaced or vanished between confirmation and execution | Reported `Changed` or `Missing`, never substituted; the other members proceed |
| Partial failure on Trash, Copy or Move | Per-member results, as today; assets are not transactional |
| `renameAsset` target collision | Blocks the whole asset rename before any mutation |
| Asset rename fails partway | Stops, reverses the members already renamed as a new operation with `undoOf`, and reports each member's final name. If a reversal also fails, the report says exactly which names remain changed. Asset rename is `Kind::BatchRename`, so it requires the journal, and a crash partway reconciles as a split asset (OPERATION-JOURNAL) |
| Invalid new stem | `renameAsset` returns empty; nothing runs |
| Unreadable or truncated listing | `Incomplete`; expansion still finds companions by reading the parent directory |

## Migration without behaviour change

- `planDeletion`, `executeDeletion` and `FileOperationService::plan` stay as they are. `plan()` is `planScoped(..., "file")`. The only visible change in B1 is the *preview*: companions appear in the confirmation as "not included", plus the new choice.
- `DirectoryEntryModel` gains no role in this contract. B1 adds a grouping proxy or role later through the lead (META-ROLES is the precedent).
- `CopyMoveService` today moves exactly the given paths, and `enqueueTransfer` keeps doing so.
- Asset-scope rename arrives with B2, because it needs the journal. Until then B1's preview shows companions and asset scope is refused with the reason.

## Acceptance tests B1 must pass

`tests/asset_group_tests.cpp`; generated media only.

- **Grouping:** RAW+JPEG, RAW+HEIC, JPEG+XMP (stem), CR2+CR2.xmp (full name), both sidecar styles on one RAW, HEIC+MOV, MOV alone (not grouped), JPEG with an embedded motion payload (flag only), PNG+XMP (Derived primary), CR2+JPG+TIF (the TIF excluded, not a member).
- **Same-stem collisions:** `IMG.CR2`+`IMG.NEF`+`IMG.xmp` (Collision plus AmbiguousSidecar; the sidecar is excluded with reason); `a.JPG`+`a.jpg` on case-sensitive (Linux/tmpfs) versus case-insensitive (NTFS) fixtures; `IMG.jpg`+`IMG.jpeg` are two files of one family (Collision); `IMG.png`+`IMG.tif`+`IMG.xmp` (Collision plus AmbiguousSidecar); 17 same-stem files (`kMaxMembers`); Unicode stems that only match after case folding; names with several dots.
- **File-only versus asset:** for each operation, FileOnly returns exactly the request and lists companions as excluded; Asset adds exactly the table's members; LosslessTransform ignores Asset.
- **Bounds:** 100,000 requests with companions stop at `kMaxExpandedItems`, with the rest `LimitReached`; excluded entries stop at `kMaxExcludedListed`, with the count right; `groupListing` with `truncated` sets `Incomplete`.
- **Lookup cost:** with a hint, the backend is asked only for the hint's members (counted through a hook); without one, one directory read per parent, and a mixed-case companion is found on a case-sensitive volume; an injected slow volume ends at `kExpandBudgetMs` with `companionsIncomplete` and the confirmation's wording.
- **Expansion robustness:** a truncated listing (companion beyond the limit is still found by reading the parent); a stale hint (companion deleted after grouping gives `Missing`, not included); a companion replaced between confirmation and execution gives `Changed` at execution; cancellation during lookups gives `cancelled`, and no partial plan is confirmable.
- **Rename:** `renameAsset` preserves extensions and the darktable double extension; a target collision with an existing file (case-folded) blocks the whole asset rename before any mutation; a failure on the second member reverses the first and reports it; a fault that blocks the reversal reports the exact state; B1's asset-scope rename is refused until the journal lands.
- **Overwrite:** an asset Copy onto a folder where two members collide asks once, and an injected replace fault on one member keeps that member's target byte-identical while the report names it.
- **Transfer plan:** `planTransfer` lists collisions and companions before anything runs, and `confirmTransfer` executes exactly the snapshot.
- **Service:** `planScoped("asset")` summary lists companions; `confirm` trashes exactly the included list via a fake `TrashBackend`; a partial failure is reported per member; `plan()` output is unchanged from today apart from the added `companions` and `scope` keys.

## Not covered

- No implicit companion inclusion anywhere, ever. Asset scope is always an explicit confirmation choice.
- No cross-directory grouping (Capture One session folders, darktable's central library), no catalog, no persistent group database.
- No parsing of XMP content or Apple and Android motion payloads here (C1, ADV-09).
- No burst or time grouping (C5), and no duplicate detection (B7).
- No change to deletion semantics: there is still no permanent-delete path.
