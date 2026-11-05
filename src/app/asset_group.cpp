#include "asset_group_internal.hpp"
#include "glancelot/mutation_service.hpp"
#include "glancelot/raw_preview.hpp"
#include <QDir>
#include <QFile>
#include <algorithm>
#include <atomic>
#include <set>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

// ASSET-GROUP (docs/contracts/ASSET-GROUP.md). groupListing is pure; expand runs on a mutation service's plan worker and
// only ever reads. Nothing here mutates a file.
namespace glancelot::assets {
namespace fs = std::filesystem;
namespace detail {
namespace {
struct RealLookup final : LookupBackend {
    bool readDirectory(const fs::path& directory, const std::function<bool(const ListingInput&)>& visit) override {
        std::error_code error;
        fs::directory_iterator it(directory, fs::directory_options::skip_permission_denied, error);
        if (error) return false;
        for (const fs::directory_iterator end; it != end; it.increment(error)) {
            if (error) return false;
            std::error_code statusError;
            // No-follow: a link is never a member, whatever it points at.
            const auto status = it->symlink_status(statusError);
            if (!visit({it->path(), !statusError && fs::is_regular_file(status)})) return true;
        }
        return !error;
    }
    std::optional<MutationTarget> snapshot(const fs::path& path) override {
        std::error_code error;
        auto target = snapshotTarget(path, error);
        return error ? std::nullopt : target;
    }
    bool caseSensitive(const fs::path& directory, const fs::path& sample) override {
        // Only a stat of the same name in the other case can tell. Volume flags can't: a case-preserving volume reports
        // case-sensitive search and still matches names insensitively. No ASCII letter, or a sample that is gone, falls
        // back to what the platform usually does.
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
        constexpr bool usually = false;
#else
        constexpr bool usually = true;
#endif
        const QString name = displayPath(sample.filename());
        QString flipped = name;
        for (auto& c : flipped) {
            if (c.unicode() >= 0x80 || !c.isLetter()) continue;
            c = c.isLower() ? c.toUpper() : c.toLower();
            break;
        }
        if (flipped == name) return usually;
        const auto same = snapshot(directory / sample.filename());
        if (!same) return usually;
        const auto other = snapshot(directory / nativePath(flipped));
        return !(other && same->identity == other->identity);
    }
    std::chrono::steady_clock::time_point now() override { return std::chrono::steady_clock::now(); }
};
RealLookup realLookup;
std::atomic<LookupBackend*> activeLookup{&realLookup};
QString leaf(const fs::path& path) { return displayPath(path.filename()); }
}

void setLookupBackendForTests(LookupBackend* backend) { activeLookup.store(backend ? backend : &realLookup); }
QString displayPath(const fs::path& path) {
#ifdef Q_OS_WIN
    return QDir::fromNativeSeparators(QString::fromStdWString(path.native()));
#else
    return QFile::decodeName(path.native().c_str());
#endif
}
fs::path nativePath(const QString& path) {
#ifdef Q_OS_WIN
    return fs::path(QDir::toNativeSeparators(path).toStdWString());
#else
    return fs::path(QFile::encodeName(path).constData());
#endif
}
AssetName splitAssetName(const QString& fileName, SidecarStyle sidecar) {
    const auto split = [](const QString& name) {
        const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
        return dot <= 0 ? AssetName{name, {}} : AssetName{name.left(dot), name.mid(dot)};
    };
    AssetName parts = split(fileName);
    if (sidecar == SidecarStyle::FullName) { // IMG.CR2 + .xmp -> IMG + .CR2.xmp
        const AssetName inner = split(parts.stem);
        parts = {inner.stem, inner.suffix + parts.suffix};
    }
    return parts;
}
}

namespace {
using detail::displayPath;
using detail::leaf;
enum class Family : std::uint8_t { None, Jpeg, Heif, Avif, Jxl };
struct Kind { Role role{Role::Other}; Family family{Family::None}; };

QString lastSuffix(const QString& name) {
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    return dot <= 0 ? QString() : name.mid(dot + 1).toLower();
}
// Versioned with tests/asset_group_tests.cpp: extensions are always compared case-insensitively, whatever the volume does.
Kind classify(const QString& name) {
    const QString ext = lastSuffix(name);
    if (ext.isEmpty()) return {};
    if (ext == QLatin1String("xmp")) return {Role::Sidecar, Family::None};
    if (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg")) return {Role::Rendered, Family::Jpeg};
    if (ext == QLatin1String("heic") || ext == QLatin1String("heif")) return {Role::Rendered, Family::Heif};
    if (ext == QLatin1String("avif")) return {Role::Rendered, Family::Avif};
    if (ext == QLatin1String("jxl")) return {Role::Rendered, Family::Jxl};
    if (ext == QLatin1String("tif") || ext == QLatin1String("tiff") || ext == QLatin1String("png") || ext == QLatin1String("webp")) return {Role::Derived, Family::None};
    if (ext == QLatin1String("mov") || ext == QLatin1String("mp4")) return {Role::MotionVideo, Family::None};
    if (RawPreviewExtractor::isRawExtension(ext)) return {Role::Raw, Family::None};
    return {};
}
int roleRank(Role role) {
    switch (role) {
    case Role::Raw: return 0;
    case Role::Rendered: return 1;
    case Role::Derived: return 2;
    case Role::MotionVideo: return 3;
    case Role::Sidecar: return 4;
    default: return 5;
    }
}
std::string keyOf(const QString& text, bool caseSensitive) { return (caseSensitive ? text : text.toCaseFolded()).toUtf8().toStdString(); }
fs::path normalised(const fs::path& path) {
    fs::path p = path.lexically_normal();
    p.make_preferred();
    while (!p.has_filename() && p.has_relative_path()) p = p.parent_path();
    return p;
}
// No reader in this packet can see inside a file, so the embedded-motion flag rests on the names Android cameras give
// those files (PXL_x.MP.jpg, MVIMG_x.jpg). ADV-09 replaces this with the payload itself.
bool namedAsEmbeddedMotion(const QString& name) {
    const QString folded = name.toCaseFolded();
    return folded.startsWith(QLatin1String("mvimg_")) || folded.contains(QLatin1String(".mp."));
}

// groupListing's result plus what the expansion also needs and a Group can't carry.
struct Analysis {
    std::vector<Group> groups;
    std::vector<std::vector<Member>> overflow; // per group: past kMaxMembers
    std::vector<std::vector<Member>> leftOut;  // per group: a Derived beside Raw or Rendered, and its FullName sidecar
    std::unordered_map<std::string, std::size_t> groupOfName; // name key -> group, members and overflow alike
};
struct Entry {
    fs::path path;
    QString name;
    Kind kind;
    SidecarStyle sidecar{SidecarStyle::None};
    std::string stemKey, nameKey;
};
Member memberOf(const Entry& entry) {
    Member member;
    member.path = entry.path;
    member.role = entry.kind.role;
    member.sidecar = entry.sidecar;
    return member;
}
bool byRoleThenName(const Entry* a, const Entry* b) {
    const int ra = roleRank(a->kind.role), rb = roleRank(b->kind.role);
    return ra != rb ? ra < rb : a->name < b->name;
}

Analysis analyse(const fs::path& directory, const std::vector<ListingInput>& input, bool caseSensitive, bool truncated) {
    const fs::path dir = normalised(directory);
    const auto sameDirectory = [&](const fs::path& parent) {
        if (parent == dir) return true;
        return !caseSensitive && keyOf(displayPath(parent), false) == keyOf(displayPath(dir), false);
    };
    std::vector<Entry> entries;
    entries.reserve(input.size());
    for (const auto& item : input) {
        if (!item.regularFile) continue; // folders and links are never grouped
        const fs::path path = normalised(item.path);
        if (!sameDirectory(path.parent_path())) continue;
        const QString name = leaf(path);
        const Kind kind = classify(name);
        if (kind.role == Role::Other) continue;
        Entry entry;
        entry.name = name;
        entry.path = dir / path.filename();
        entry.kind = kind;
        entry.stemKey = keyOf(detail::splitAssetName(name, SidecarStyle::None).stem, caseSensitive);
        entry.nameKey = keyOf(name, caseSensitive);
        entries.push_back(std::move(entry));
    }
    std::unordered_map<std::string, std::vector<std::size_t>> imagesByName;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].kind.role != Role::Sidecar) imagesByName[entries[i].nameKey].push_back(i);
    }
    // A sidecar whose stem is the exact name of another file is a FullName sidecar of that file (IMG.CR2.xmp of IMG.CR2).
    std::unordered_map<std::string, std::vector<std::size_t>> stems, fullNameSidecars;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        Entry& entry = entries[i];
        if (entry.kind.role == Role::Sidecar && imagesByName.contains(entry.stemKey)) {
            entry.sidecar = SidecarStyle::FullName;
            fullNameSidecars[entry.stemKey].push_back(i);
            continue;
        }
        if (entry.kind.role == Role::Sidecar) entry.sidecar = SidecarStyle::Stem;
        stems[entry.stemKey].push_back(i);
    }
    const auto sidecarsOf = [&](const Entry& image, std::vector<const Entry*>& out) {
        if (const auto found = fullNameSidecars.find(image.nameKey); found != fullNameSidecars.end()) {
            for (const std::size_t i : found->second) out.push_back(&entries[i]);
        }
    };
    std::set<std::string> ordered; // ids follow the stem order, so a snapshot is deterministic for one listing
    for (const auto& [key, unused] : stems) ordered.insert(key);

    Analysis result;
    const auto addGroup = [&](const std::string& stemKey, std::vector<const Entry*> members, std::uint32_t flags,
                              const std::vector<const Entry*>& leftOut) {
        std::sort(members.begin(), members.end(), byRoleThenName);
        Group group;
        group.id = result.groups.size() + 1;
        group.directory = dir;
        group.stemKey = stemKey;
        group.flags = flags | (truncated ? std::uint32_t(GroupFlag::Incomplete) : 0u);
        std::vector<Member> overflow;
        for (std::size_t i = 0; i < members.size(); ++i) {
            result.groupOfName.emplace(members[i]->nameKey, result.groups.size());
            (i < kMaxMembers ? group.members : overflow).push_back(memberOf(*members[i]));
        }
        std::vector<Member> left;
        for (const Entry* entry : leftOut) left.push_back(memberOf(*entry));
        result.groups.push_back(std::move(group));
        result.overflow.push_back(std::move(overflow));
        result.leftOut.push_back(std::move(left));
    };
    for (const std::string& key : ordered) {
        std::vector<const Entry*> raws, rendered, derived, motion, sidecars;
        for (const std::size_t i : stems[key]) {
            const Entry* entry = &entries[i];
            switch (entry->kind.role) {
            case Role::Raw: raws.push_back(entry); break;
            case Role::Rendered: rendered.push_back(entry); break;
            case Role::Derived: derived.push_back(entry); break;
            case Role::MotionVideo: motion.push_back(entry); break;
            default: sidecars.push_back(entry); break;
            }
        }
        const bool anchored = !raws.empty() || !rendered.empty();
        if (!anchored && derived.empty()) continue; // a sidecar or a video alone is never grouped
        std::vector<const Entry*> members, leftOut;
        members.insert(members.end(), raws.begin(), raws.end());
        members.insert(members.end(), rendered.begin(), rendered.end());
        if (anchored) {
            members.insert(members.end(), motion.begin(), motion.end());
            for (const Entry* entry : derived) { // exports and edited masters stay out, and so do their own sidecars
                leftOut.push_back(entry);
                sidecarsOf(*entry, leftOut);
                if (fullNameSidecars.contains(entry->nameKey)) { // that pair is an asset of its own
                    std::vector<const Entry*> pair{entry};
                    sidecarsOf(*entry, pair);
                    addGroup(entry->nameKey, std::move(pair), 0, {});
                }
            }
        } else {
            members.insert(members.end(), derived.begin(), derived.end());
        }
        const std::size_t images = members.size();
        for (std::size_t i = 0; i < images; ++i) sidecarsOf(*members[i], members);
        members.insert(members.end(), sidecars.begin(), sidecars.end());

        std::uint32_t flags = 0;
        std::set<Family> families;
        bool repeatedFamily = false;
        for (const Entry* entry : rendered) repeatedFamily = !families.insert(entry->kind.family).second || repeatedFamily;
        std::set<std::string> folded;
        bool caseVariants = false;
        for (const Entry* entry : members) caseVariants = !folded.insert(keyOf(entry->name, false)).second || caseVariants;
        const bool tooMany = raws.size() > 1 || families.size() > 1 || repeatedFamily || (!anchored && derived.size() > 1)
            || caseVariants || members.size() > kMaxMembers;
        if (tooMany) flags |= std::uint32_t(GroupFlag::Collision);
        if (tooMany && !sidecars.empty()) flags |= std::uint32_t(GroupFlag::AmbiguousSidecar);
        for (const Entry* entry : rendered) {
            if (namedAsEmbeddedMotion(entry->name)) flags |= std::uint32_t(GroupFlag::EmbeddedMotion);
        }
        if (members.size() >= 2) addGroup(key, std::move(members), flags, leftOut);
    }
    return result;
}

// The expansion of one directory's requests, built group by group so a path is decided once whatever the request count.
class Expander {
public:
    Expander(Scope scope, Operation operation, Expansion& out) : scope_(scope), operation_(operation), out_(out) {}
    std::vector<Planned> companions;

    void exclude(const fs::path& path, const fs::path& requestedBy, Exclusion reason) {
        if (out_.excluded.size() < kMaxExcludedListed) out_.excluded.push_back({path, requestedBy, reason});
        else ++out_.excludedNotListed;
    }
    // `requestedNames`: every request of this directory, keyed by volume semantics, so a requested file is never listed
    // as left out, whichever group asked.
    void applyGroup(const Group& group, const std::vector<Member>& overflow, const std::vector<Member>& leftOut,
                    const std::vector<const Member*>& requested, const std::unordered_set<std::string>& requestedNames,
                    bool caseSensitive, const std::vector<fs::path>& missing, std::size_t room) {
        const fs::path& first = requested.front()->path;
        const bool collision = group.flags & std::uint32_t(GroupFlag::Collision);
        const auto isRequested = [&](const Member& member) { return requestedNames.contains(keyOf(leaf(member.path), caseSensitive)); };
        for (const Member& member : group.members) {
            if (isRequested(member)) continue;
            if (std::find(missing.begin(), missing.end(), member.path) != missing.end()) { exclude(member.path, first, Exclusion::Missing); continue; }
            if (scope_ == Scope::FileOnly) { exclude(member.path, first, Exclusion::NotApplicable); continue; }
            const Member* owner = nullptr;
            if (!collision) owner = requested.front();
            else if (operation_ != Operation::Rename) { // "a FullName sidecar always attaches to its exact file"
                for (const Member* r : requested) if (attached(member, *r)) owner = r;
            }
            if (owner) {
                if (companions.size() < room) companions.push_back({member, owner->path, true});
                else exclude(member.path, owner->path, Exclusion::LimitReached);
            } else {
                const bool ambiguous = (group.flags & std::uint32_t(GroupFlag::AmbiguousSidecar)) && member.sidecar == SidecarStyle::Stem;
                exclude(member.path, first, ambiguous ? Exclusion::Ambiguous : Exclusion::Collision);
            }
        }
        for (const Member& member : overflow) if (!isRequested(member)) exclude(member.path, first, Exclusion::LimitReached);
        for (const Member& member : leftOut) if (!isRequested(member)) exclude(member.path, first, Exclusion::NotApplicable);
    }
private:
    static bool attached(const Member& a, const Member& b) {
        const auto sidecarOf = [](const Member& sidecar, const Member& image) {
            if (sidecar.sidecar != SidecarStyle::FullName) return false;
            fs::path inner = sidecar.path.filename();
            inner.replace_extension();
            return keyOf(displayPath(inner), false) == keyOf(leaf(image.path), false);
        };
        return sidecarOf(a, b) || sidecarOf(b, a);
    }
    Scope scope_;
    Operation operation_;
    Expansion& out_;
};
}

GroupingSnapshot groupListing(const fs::path& directory, const std::vector<ListingInput>& entries, bool caseSensitive,
                              std::uint64_t listingGeneration, bool truncated) {
    GroupingSnapshot snapshot;
    snapshot.directory = normalised(directory);
    snapshot.listingGeneration = listingGeneration;
    snapshot.caseSensitive = caseSensitive;
    snapshot.groups = analyse(directory, entries, caseSensitive, truncated).groups;
    return snapshot;
}

Expansion expand(const std::vector<fs::path>& requested, Scope scope, Operation operation, const GroupingSnapshot* hint, std::stop_token cancel) {
    Expansion out;
    if (operation == Operation::LosslessTransform) scope = Scope::FileOnly;
    struct Request { fs::path path; Member member; bool duplicate{}; };
    std::vector<Request> requests;
    requests.reserve(requested.size());
    {
        std::unordered_set<fs::path::string_type> seen;
        for (const auto& path : requested) {
            Request request;
            request.path = normalised(path);
            if (!seen.insert(request.path.native()).second) continue;
            request.member.path = request.path;
            request.member.role = classify(leaf(request.path)).role;
            requests.push_back(std::move(request));
        }
    }
    const auto finish = [&](std::vector<Planned> companions) {
        out.included.reserve(std::min(requests.size(), kMaxExpandedItems) + companions.size());
        for (Request& request : requests) {
            if (request.duplicate) continue;
            if (out.included.size() >= kMaxExpandedItems) {
                if (out.excluded.size() < kMaxExcludedListed) out.excluded.push_back({request.path, request.path, Exclusion::LimitReached});
                else ++out.excludedNotListed;
                continue;
            }
            out.included.push_back({std::move(request.member), request.path, false});
        }
        std::stable_sort(companions.begin(), companions.end(), [](const Planned& a, const Planned& b) { return roleRank(a.member.role) < roleRank(b.member.role); });
        for (Planned& companion : companions) out.included.push_back(std::move(companion));
        return std::move(out);
    };
    if (operation == Operation::SidecarWrite) return finish({}); // not an expansion: C1 decides which sidecar a group writes

    detail::LookupBackend& lookup = *detail::activeLookup.load();
    const auto deadline = lookup.now() + std::chrono::milliseconds(kExpandBudgetMs);
    const auto outOfBudget = [&] { return lookup.now() >= deadline; };

    std::vector<fs::path> directories;
    std::unordered_map<fs::path::string_type, std::vector<std::size_t>> byDirectory;
    for (std::size_t i = 0; i < requests.size(); ++i) {
        const fs::path parent = requests[i].path.parent_path();
        if (parent.empty() || parent == requests[i].path) continue; // a root has no siblings
        auto& list = byDirectory[parent.native()];
        if (list.empty()) directories.push_back(parent);
        list.push_back(i);
    }

    Expander expander(scope, operation, out);
    // Requests come first in `included`, so companions get whatever the cap leaves.
    const std::size_t room = kMaxExpandedItems - std::min(requests.size(), kMaxExpandedItems);
    const auto cancelled = [&] {
        if (!cancel.stop_requested()) return false;
        Expansion stopped;
        stopped.cancelled = true;
        out = std::move(stopped);
        return true;
    };
    for (const fs::path& directory : directories) {
        if (cancelled()) return out;
        if (out.companionsIncomplete || outOfBudget()) { out.companionsIncomplete = true; break; }
        const std::vector<std::size_t>& indices = byDirectory[directory.native()];
        const fs::path sample = requests[indices.front()].path.filename();

        // With a current hint, only its members are probed. A truncated hint (any group flagged, or no groups at all) and
        // a group that hit kMaxMembers hide names, so those read the directory instead.
        const bool hintUsable = hint && !hint->groups.empty() && normalised(hint->directory) == directory
            && std::none_of(hint->groups.begin(), hint->groups.end(), [](const Group& g) {
                   return (g.flags & std::uint32_t(GroupFlag::Incomplete)) || g.members.size() >= kMaxMembers; });
        Analysis analysis;
        bool caseSensitive{};
        std::vector<fs::path> missing;
        if (hintUsable) {
            caseSensitive = hint->caseSensitive;
            for (const Group& group : hint->groups) {
                analysis.groups.push_back(group);
                analysis.overflow.emplace_back();
                analysis.leftOut.emplace_back();
                for (const Member& member : group.members) analysis.groupOfName.emplace(keyOf(leaf(member.path), caseSensitive), analysis.groups.size() - 1);
            }
        } else {
            caseSensitive = lookup.caseSensitive(directory, sample);
            std::vector<ListingInput> listing;
            bool stopped = false;
            const bool readable = lookup.readDirectory(directory, [&](const ListingInput& entry) {
                if (cancel.stop_requested() || outOfBudget()) { stopped = true; return false; }
                listing.push_back(entry);
                return true;
            });
            if (cancelled()) return out;
            // A partial read can't be grouped: an unseen same-stem file would hide a Collision.
            if (stopped || !readable) { out.companionsIncomplete = true; continue; }
            analysis = analyse(directory, listing, caseSensitive, false);
        }
        // One group at a time, in first-request order.
        std::unordered_map<std::size_t, std::vector<std::size_t>> requestsOfGroup;
        std::vector<std::size_t> groupOrder;
        std::unordered_set<std::string> namesSeen;
        for (const std::size_t index : indices) {
            Request& request = requests[index];
            const std::string key = keyOf(leaf(request.path), caseSensitive);
            if (!namesSeen.insert(key).second) { request.duplicate = true; continue; } // one file named two ways
            const auto group = analysis.groupOfName.find(key);
            if (group == analysis.groupOfName.end()) continue;
            auto& list = requestsOfGroup[group->second];
            if (list.empty()) groupOrder.push_back(group->second);
            list.push_back(index);
        }
        for (const std::size_t groupIndex : groupOrder) {
            if (cancelled()) return out;
            const Group& group = analysis.groups[groupIndex];
            std::vector<const Member*> members;
            for (const std::size_t index : requestsOfGroup[groupIndex]) {
                Request& request = requests[index];
                const std::string key = keyOf(leaf(request.path), caseSensitive);
                const auto same = [&](const Member& m) { return keyOf(leaf(m.path), caseSensitive) == key; };
                auto found = std::find_if(group.members.begin(), group.members.end(), same);
                const auto& overflow = analysis.overflow[groupIndex];
                if (found == group.members.end()) found = std::find_if(overflow.begin(), overflow.end(), same);
                if (found != group.members.end() && found != overflow.end()) {
                    const fs::path asRequested = request.path;
                    request.member = *found;
                    request.member.path = asRequested; // keep the spelling the user's selection used
                    members.push_back(&request.member);
                }
            }
            if (members.empty()) continue;
            if (hintUsable) {
                // Re-probe every member: the hint is never trusted for existence.
                for (const Member& member : group.members) {
                    if (outOfBudget()) { out.companionsIncomplete = true; break; }
                    const auto now = lookup.snapshot(member.path);
                    if (!now || now->kind != TargetKind::File || now->link) missing.push_back(member.path);
                }
                if (out.companionsIncomplete) break;
            }
            expander.applyGroup(group, analysis.overflow[groupIndex], analysis.leftOut[groupIndex], members, namesSeen, caseSensitive, missing, room);
            missing.clear();
        }
    }
    if (cancelled()) return out;
    return finish(std::move(expander.companions));
}

std::vector<std::pair<fs::path, fs::path>> renameAsset(const Expansion& planned, const std::string& newStem) {
    const QString stem = QString::fromStdString(newStem);
    QString error;
    if (!MutationService::isValidFileName(stem, &error)) return {};
    std::vector<std::pair<fs::path, fs::path>> renames;
    renames.reserve(planned.included.size());
    for (const Planned& item : planned.included) {
        const QString name = stem + detail::splitAssetName(leaf(item.member.path), item.member.sidecar).suffix;
        if (!MutationService::isValidFileName(name, &error)) return {};
        renames.emplace_back(item.member.path, item.member.path.parent_path() / detail::nativePath(name));
    }
    return renames;
}
}
