#pragma once
// Private to src/app and tests/asset_group_tests.cpp: helpers the three mutation services share, and the seam that lets
// tests count, slow down and fake the filesystem reads behind assets::expand (whose signature is frozen).
#include "glancelot/asset_group.hpp"
#include <QString>
#include <QVariantMap>
#include <chrono>
#include <functional>
#include <optional>

namespace glancelot::assets::detail {
// The reads expand() may perform. Worker thread only.
struct LookupBackend {
    virtual ~LookupBackend() = default;
    // One directory read. `visit` returns false to stop early. False when the directory can't be read.
    virtual bool readDirectory(const std::filesystem::path& directory, const std::function<bool(const ListingInput&)>& visit) = 0;
    // One no-follow stat; nullopt when the path is gone or can't be read.
    virtual std::optional<MutationTarget> snapshot(const std::filesystem::path& path) = 0;
    // Volume semantics of `directory`, judged from `sample`, a file name expected to be inside it.
    virtual bool caseSensitive(const std::filesystem::path& directory, const std::filesystem::path& sample) = 0;
    virtual std::chrono::steady_clock::time_point now() = 0;
};
// Tests only: nullptr restores the real filesystem. Not synchronised, so set it while no expansion is running.
void setLookupBackendForTests(LookupBackend* backend);

// A snapshot is only a hint while it describes the current listing. Older generations are dropped, never merged.
inline const GroupingSnapshot* hintIfCurrent(const GroupingSnapshot* snapshot, std::uint64_t currentListingGeneration) {
    return snapshot && snapshot->listingGeneration == currentListingGeneration ? snapshot : nullptr;
}

// "file" and anything unknown is FileOnly, so a typo can never widen an operation.
Scope scopeFromName(const QString& name);
QString scopeName(Scope scope);

QString displayPath(const std::filesystem::path& path);
std::filesystem::path nativePath(const QString& path);

// A file name split where an asset rename would split it: IMG.CR2 -> IMG + .CR2, and the FullName sidecar IMG.CR2.xmp ->
// IMG + .CR2.xmp. A name with no extension (or only a leading dot) has an empty suffix.
struct AssetName { QString stem, suffix; };
AssetName splitAssetName(const QString& fileName, SidecarStyle sidecar);

// The summary keys every scoped plan shares: companions [{path, name, role, requestedBy, included, exclusion}] (included
// ones first, at most kMaxExcludedListed), companionsNotListed, companionsIncomplete. `exclusion` is empty on an included
// companion. An empty scope name leaves the scope key out.
void addCompanionSummary(QVariantMap& summary, const Expansion& expansion, Scope scope);

enum class SnapshotState { Same, Changed, Missing };
// Revalidation for transfers and renames: identity for everything, plus size and modified time for files.
SnapshotState compareToSnapshot(const MutationTarget& snapshot);
}
