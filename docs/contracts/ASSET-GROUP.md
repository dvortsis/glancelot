# ASSET-GROUP contract proposal (draft for the lead)

Status: **draft, not frozen**. Proposed header: `include/glancelot/asset_group.hpp` plus a diff to `file_operation_service.hpp`. Base: `origin/main` at `3893c77`.

## 1. Purpose and consumers

Group the files that users think of as one photo, and make every mutation choose **explicitly** between the named files (file-only) and the whole asset. Kinds:

- **RAW+rendered pair:** `IMG_0001.CR2` + `IMG_0001.JPG`/`.HEIC`.
- **Sidecars:** `IMG_0001.xmp` (Lightroom/Capture One style, stem match) and `IMG_0001.CR2.xmp` (darktable style, full-name match).
- **Motion pairs:** Apple Live Photo `IMG_0001.HEIC` + `IMG_0001.MOV`. An Android motion photo is an *embedded* payload: a flag on one member, never a second file.

Today `planDeletion` says "paired/sidecar files are never added implicitly", and DELETE-PLAN defers paired sets to M4. That default stays: **file-only is the default, and asset scope is a per-operation user choice made in the confirmation.**

Consumers: **B1** (grouping service, companions previewed in every delete/copy/move/rename confirmation, file-only versus asset choice), **B2** (the journal records the expanded exact list), **B4** batch rename (stem rename of whole assets), **B5/B6** basket and drag/drop (same expansion), **C1** XMP engine (which sidecar a group writes), **C2/C5** culling (one rating per asset), **D7** lossless transforms (always file-only).

## 2. Proposed header

```cpp
#pragma once
#include "file_operations.hpp"   // FileIdentity
#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>

namespace glancelot::assets {
// Lead-frozen ASSET-GROUP. Pure grouping over a listing plus a bounded, worker-only expansion that
// re-probes exact companion names. Never called from UI/render code. Never mutates anything.
inline constexpr std::size_t kMaxMembers = 16;          // per group; extra candidates -> Collision
inline constexpr std::size_t kMaxProbesPerTarget = 48;  // exact-name stats during expansion

enum class Role : std::uint8_t { Raw, Rendered, Sidecar, MotionVideo, Other };
enum class SidecarStyle : std::uint8_t { None, Stem /*IMG.xmp*/, FullName /*IMG.CR2.xmp*/ };
enum class Match : std::uint8_t { Stem, ContentId };    // ContentId: Apple content identifier confirmed (later)
enum class GroupFlag : std::uint32_t {
    None = 0,
    Collision = 1u << 0,        // >1 Raw, >1 Rendered of one family, case variants, or > kMaxMembers
    AmbiguousSidecar = 1u << 1, // a Stem sidecar that could belong to more than one group
    EmbeddedMotion = 1u << 2,   // Android motion photo payload inside a Rendered member
    Incomplete = 1u << 3,       // listing truncated/unreadable; expansion re-probes exact names
};

struct Member {
    std::filesystem::path path;              // absolute, lexically normal
    Role role{Role::Other};
    SidecarStyle sidecar{SidecarStyle::None};
    FileIdentity identity;                   // zero when not yet snapshotted
};
struct Group {
    std::uint64_t id{};                      // stable within one GroupingSnapshot only
    std::filesystem::path directory;
    std::string stemKey;                     // case-folded on case-insensitive volumes
    std::vector<Member> members;             // Raw, Rendered, MotionVideo, Sidecar order; <= kMaxMembers
    std::size_t primary{};                   // index: Raw if present, else Rendered
    Match match{Match::Stem};
    std::uint32_t flags{};
};
struct GroupingSnapshot {
    std::filesystem::path directory;
    std::uint64_t listingGeneration{};       // generation of the listing it was built from
    bool caseSensitive{};                    // volume semantics, probed once per directory
    std::vector<Group> groups;               // only groups with >= 2 members; singletons are implicit
};
struct ListingInput { std::filesystem::path path; bool regularFile{}; };
// Pure, O(n log n), no I/O. Same directory only; folders and links are never grouped.
GroupingSnapshot groupListing(const std::filesystem::path& directory, const std::vector<ListingInput>& entries,
                              bool caseSensitive, std::uint64_t listingGeneration);

enum class Scope : std::uint8_t { FileOnly, Asset };
enum class Operation : std::uint8_t { Trash, Copy, Move, Rename, SidecarWrite, LosslessTransform };
enum class Exclusion : std::uint8_t { Ambiguous, Collision, NotApplicable, Missing, LimitReached };
struct Planned { Member member; std::filesystem::path requestedBy; bool companion{}; };
struct Excluded { std::filesystem::path path; std::filesystem::path requestedBy; Exclusion reason; };
struct Expansion {
    std::vector<Planned> included;           // deduplicated, requested items first, then companions
    std::vector<Excluded> excluded;          // always shown in the confirmation; never mutated
    bool cancelled{};
};
// Worker only. FileOnly returns exactly `requested` (today's behaviour) plus companions as *excluded,
// NotApplicable* so the confirmation can show them. Asset adds companions per the policy table after
// stat'ing exact candidate names (<= kMaxProbesPerTarget per target); the listing is only a hint.
Expansion expand(const std::vector<std::filesystem::path>& requested, Scope, Operation,
                 const GroupingSnapshot* hint, std::stop_token cancel = {});

// Asset-scope rename: one new stem for every member, preserving each extension and the darktable
// double extension. Empty on invalid stem (MutationService::isValidFileName rules apply).
std::vector<std::pair<std::filesystem::path, std::filesystem::path>>
renameAsset(const Expansion& planned, const std::string& newStem);
}
```

**Diff to `include/glancelot/file_operation_service.hpp`** (B1). Existing calls keep file-only behaviour:

```diff
-    Q_INVOKABLE quint64 plan(const QStringList& paths, const QString& origin);
+    Q_INVOKABLE quint64 plan(const QStringList& paths, const QString& origin);   // == scope "file"
+    // scope: "file" | "asset". The summary gains companions [{path, role, requestedBy, included,
+    // exclusion}] and scope; confirm() executes exactly the snapshotted included list.
+    Q_INVOKABLE quint64 planScoped(const QStringList& paths, const QString& origin, const QString& scope);
```

`CopyMoveService::enqueueTransfer` and `MutationService::renamePath` get matching scoped entry points in B1/B4. They move behind the journal in B2 (see OPERATION-JOURNAL), so their exact signatures are frozen with that contract.

## 3. Rules

**Stem and case.** Stem = file name up to the last dot (`IMG.CR2.xmp` has stem `IMG.CR2`, which makes it a FullName sidecar of `IMG.CR2`). Keys are case-folded (`QString::toCaseFolded` semantics) on case-insensitive volumes, and exact on case-sensitive ones. On a case-sensitive volume, `a.JPG` and `a.jpg` are distinct members and set `Collision`.

**Classification.** `Raw` = `RawPreviewExtractor::isRawExtension`. `Rendered` = jpg/jpeg/heic/heif/avif/tif/tiff/png/webp/jxl. `Sidecar` = xmp. `MotionVideo` = mov/mp4, and only when a Rendered or Raw member shares the stem. Video files with no partner are never grouped. Everything else is `Other` and never grouped. The extension tables live in the `.cpp` and are versioned with tests.

**Groups.** Two or more members sharing a stem key. Primary = Raw if present, else Rendered. More than one Raw, more than one Rendered of the same family, case variants or more than `kMaxMembers` candidates set `Collision`. A Stem sidecar in a directory where the stem key maps to a Collision group sets `AmbiguousSidecar`. A FullName sidecar always attaches to its exact file.

**Expansion policy (Asset scope):**

| Operation | Companions added | Never added |
| --- | --- | --- |
| Trash, Copy, Move | All members of the requested file's group | Members of a `Collision` group other than the requested one (shown as `Collision`); `AmbiguousSidecar` (shown as `Ambiguous`) |
| Rename | All members, renamed by `renameAsset`; every target is checked for collisions (case folding included) before anything runs | Same as above. A Collision group falls back to FileOnly with an explanation |
| SidecarWrite (C1) | Not an expansion. It resolves *which* sidecar a group writes (C1 owns the naming profile) | Writes never touch image members |
| LosslessTransform (D7) | None: always FileOnly | Sidecar orientation fields are C1/D7 scope |

When a requested file is itself a companion (the user selected the `.xmp`), Asset scope expands to the whole group the same way. Two requested members of one group are deduplicated. `included` order is stable: requested items first, then companions in role order.

**Snapshot and revalidation.** `expand` runs inside the existing plan step (worker). Its `included` list becomes the immutable target snapshot (`planDeletion` input, or journal intents), and from there on `executeDeletion`-style identity revalidation applies unchanged. A companion that changed or vanished between confirmation and execution is reported (`Changed`/`Missing`), never substituted.

**Generation fencing.** A `GroupingSnapshot` carries its listing generation. Grid badges and confirmation previews drop snapshots older than the current listing generation. `expand` never trusts the snapshot for existence (it re-probes), so a stale hint can only cost probes, never add a wrong file.

**Bounds.** At most `kMaxMembers` per group and `kMaxProbesPerTarget` stats per target. Requests are capped by the plan's existing limit (100 000 entries). Grouping is pure and runs on the listing worker. Badges consume the snapshot on the UI thread as values.

## 4. Migration without behaviour change

- `planDeletion`, `executeDeletion` and `FileOperationService::plan` stay as they are. `plan()` is `planScoped(..., "file")`. The only visible change in B1 is the *preview*: companions appear in the confirmation as "not included", plus the new choice.
- `DirectoryEntryModel` gains no role in this contract. B1 adds a grouping proxy or role later through the lead (META-ROLES is the precedent).
- `CopyMoveService` today moves exactly the given paths, and that stays its default. Its collision policy for asset scope (one decision per asset, not per member) is B1 scope and is recorded in the journal contract.

## 5. Test plan (`tests/asset_group_tests.cpp`; generated media only)

- **Grouping:** RAW+JPEG, RAW+HEIC, JPEG+XMP (stem), CR2+CR2.xmp (full name), both sidecar styles on one RAW, HEIC+MOV, MOV alone (not grouped), JPEG with an embedded motion payload (flag only).
- **Same-stem collisions:** `IMG.CR2`+`IMG.NEF`+`IMG.xmp` (Collision plus AmbiguousSidecar; the sidecar is excluded with reason); `a.JPG`+`a.jpg` on case-sensitive (Linux/tmpfs) versus case-insensitive (NTFS) fixtures; 17 same-stem files (`kMaxMembers`); Unicode stems that only match after case folding; names with several dots.
- **File-only versus asset:** for each operation, FileOnly returns exactly the request and lists companions as excluded; Asset adds exactly the table's members; LosslessTransform ignores Asset.
- **Expansion robustness:** a truncated listing (companion beyond the limit is still found by probing); a stale hint (companion deleted after grouping → `Missing`, not included); a companion replaced between confirmation and execution → `Changed` at execution; cancellation during probing → `cancelled`, no partial plan is confirmable.
- **Rename:** `renameAsset` preserves extensions and the darktable double extension; a target collision with an existing file (case-folded) blocks the whole asset rename before any mutation.
- **Service:** `planScoped("asset")` summary lists companions; `confirm` trashes exactly the included list via a fake `TrashBackend`; a partial failure is reported per member.

## 6. Open questions for the lead

1. **Default scope.** Keep FileOnly as the default for every operation (proposed), or remember the user's last choice per operation kind?
2. **Collision behaviour.** For a Collision group, exclude the ambiguous members and continue (proposed), or block the operation until the user picks members explicitly?
3. **Apple content identifier.** Stem matching only for B1 (proposed), with `Match::ContentId` reserved for ADV-09 (E2). Is a stem-only Live Photo pairing acceptable for move/trash?
4. **Rendered family list.** Should TIFF/PNG count as "rendered" companions of a RAW (some workflows export them with the same stem), or only JPEG/HEIC/AVIF/JXL?
5. **Cross-folder sidecars** (Capture One session folders, darktable's central library) are excluded. Confirm they stay non-goals.

## 7. Non-goals

- No implicit companion inclusion anywhere, ever. Asset scope is always an explicit confirmation choice.
- No cross-directory grouping, no catalog, no persistent group database.
- No parsing of XMP content or Apple/Android motion payloads here (C1, ADV-09).
- No burst or time grouping (C5), and no duplicate detection (B7).
- No change to deletion semantics: there is still no permanent-delete path.
