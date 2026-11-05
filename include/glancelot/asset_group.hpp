#pragma once
#include "file_operations.hpp" // FileIdentity
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

// Lead-frozen ASSET-GROUP (docs/contracts/ASSET-GROUP.md). Implemented by packet B1 in src/app/asset_group.cpp, linked into
// glancelot_platform: classification needs RawPreviewExtractor and rename validation needs MutationService. The header stays
// Qt-free so tests and later core code can use the types. Pure grouping over a listing plus a bounded, worker-only expansion
// that re-probes exact companion names. Never called from UI or render code. Never mutates anything.
namespace glancelot::assets {
inline constexpr std::size_t kMaxMembers = 16;          // per group; extra candidates set Collision
inline constexpr std::size_t kMaxProbesPerTarget = 48;  // exact-name stats during expansion

// Raw: a camera RAW. Rendered: the camera-native companion formats (JPEG, HEIF, AVIF, JPEG XL). Derived: an exported or edited
// format (TIFF, PNG, WebP), a member only when it is the stem's sole image, so a RAW's asset never pulls in an edited master.
enum class Role : std::uint8_t { Raw, Rendered, Derived, Sidecar, MotionVideo, Other };
enum class SidecarStyle : std::uint8_t { None, Stem /*IMG.xmp*/, FullName /*IMG.CR2.xmp*/ };
enum class Match : std::uint8_t { Stem, ContentId };    // ContentId: reserved for a confirmed Apple content identifier (ADV-09)
enum class GroupFlag : std::uint32_t {
    None = 0,
    Collision = 1u << 0,        // more than one Raw, more than one Rendered family, case variants, or > kMaxMembers
    AmbiguousSidecar = 1u << 1, // a Stem sidecar that could belong to more than one group
    EmbeddedMotion = 1u << 2,   // an Android motion photo payload inside a Rendered member
    Incomplete = 1u << 3,       // the listing was truncated or unreadable; expansion re-probes exact names
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
    std::vector<Member> members;             // Raw, Rendered, Derived, MotionVideo, Sidecar order; <= kMaxMembers
    std::size_t primary{};                   // index: Raw if present, else Rendered, else Derived
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
struct Excluded { std::filesystem::path path; std::filesystem::path requestedBy; Exclusion reason{Exclusion::NotApplicable}; };
struct Expansion {
    std::vector<Planned> included;           // deduplicated, requested items first, then companions in role order
    std::vector<Excluded> excluded;          // always shown in the confirmation; never mutated
    bool cancelled{};                        // no partial plan can be confirmed
};
// Worker only. FileOnly returns exactly `requested` plus companions as excluded (NotApplicable) so the confirmation can show
// them. Asset adds companions per the contract's policy table after stat'ing exact candidate names; the listing is only a hint.
Expansion expand(const std::vector<std::filesystem::path>& requested, Scope scope, Operation operation,
                 const GroupingSnapshot* hint, std::stop_token cancel = {});

// Asset-scope rename: one new stem for every member, preserving each extension and the full-name sidecar's double extension.
// Empty on an invalid stem (MutationService::isValidFileName rules).
std::vector<std::pair<std::filesystem::path, std::filesystem::path>>
renameAsset(const Expansion& planned, const std::string& newStem);
}
