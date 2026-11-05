#include "asset_group_internal.hpp"
#include "glancelot/asset_group.hpp"
#include "glancelot/copy_move_service.hpp"
#include "glancelot/file_operation_service.hpp"
#include "glancelot/mutation_service.hpp"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>

Q_DECLARE_METATYPE(glancelot::assets::Operation)

// ASSET-GROUP acceptance tests (docs/contracts/ASSET-GROUP.md). Generated names and media only: pure grouping runs on
// name lists, the lookup tests on a fake volume with a virtual clock, and the services on files under a QTemporaryDir.
namespace fs = std::filesystem;
using namespace glancelot;
using namespace glancelot::assets;
namespace {
constexpr int kWait = 10000;
fs::path root() { return fs::path("/fixture"); }
QString name(const fs::path& path) { return detail::displayPath(path.filename()); }
std::vector<ListingInput> listing(const QStringList& names, const fs::path& directory = root()) {
    std::vector<ListingInput> entries;
    for (const auto& n : names) entries.push_back({directory / detail::nativePath(n), true});
    return entries;
}
GroupingSnapshot groups(const QStringList& names, bool caseSensitive = false, bool truncated = false) {
    return groupListing(root(), listing(names), caseSensitive, 7, truncated);
}
QStringList members(const Group& group) {
    QStringList out;
    for (const auto& member : group.members) out << name(member.path);
    return out;
}
bool has(const Group& group, GroupFlag flag) { return group.flags & std::uint32_t(flag); }
QStringList included(const Expansion& e) {
    QStringList out;
    for (const auto& item : e.included) out << name(item.member.path);
    return out;
}
std::map<QString, Exclusion> excluded(const Expansion& e) {
    std::map<QString, Exclusion> out;
    for (const auto& item : e.excluded) out[name(item.path)] = item.reason;
    return out;
}

// A volume that exists only as name lists, so bounds and budgets don't need 100,000 real files or real seconds.
struct FakeVolume final : detail::LookupBackend {
    std::map<fs::path, QStringList> directories;
    std::map<fs::path, std::vector<std::pair<fs::path, bool>>> extra; // non-regular entries
    std::set<fs::path> gone;
    bool sensitive{};
    int reads{}, stats{}, caseProbes{};
    std::chrono::steady_clock::time_point clock{};
    std::chrono::milliseconds costPerEntry{0}, costPerRead{0};
    std::function<void()> duringRead;
    bool unreadable{};
    bool readDirectory(const fs::path& directory, const std::function<bool(const ListingInput&)>& visit) override {
        ++reads;
        if (duringRead) duringRead();
        if (unreadable) return false;
        clock += costPerRead;
        for (const auto& n : directories[directory]) {
            clock += costPerEntry;
            if (!visit({directory / detail::nativePath(n), true})) return true;
        }
        return true;
    }
    std::optional<MutationTarget> snapshot(const fs::path& path) override {
        ++stats;
        if (gone.contains(path)) return std::nullopt;
        MutationTarget target;
        target.path = path;
        return target;
    }
    bool caseSensitive(const fs::path&, const fs::path&) override { ++caseProbes; return sensitive; }
    std::chrono::steady_clock::time_point now() override { return clock; }
};
struct InstallVolume {
    explicit InstallVolume(detail::LookupBackend& backend) { detail::setLookupBackendForTests(&backend); }
    ~InstallVolume() { detail::setLookupBackendForTests(nullptr); }
};
// Holds a lookup open until the test lets it go, so cancellation and supersession are deterministic.
struct GatedVolume final : detail::LookupBackend {
    std::mutex mutex;
    std::condition_variable wake;
    bool open{false};
    std::atomic_int entered{0};
    void release() { { std::lock_guard lock(mutex); open = true; } wake.notify_all(); }
    bool readDirectory(const fs::path&, const std::function<bool(const ListingInput&)>&) override {
        ++entered;
        std::unique_lock lock(mutex);
        wake.wait(lock, [this] { return open; });
        return true;
    }
    std::optional<MutationTarget> snapshot(const fs::path&) override { return std::nullopt; }
    bool caseSensitive(const fs::path&, const fs::path&) override { return false; }
    std::chrono::steady_clock::time_point now() override { return std::chrono::steady_clock::now(); }
};
fs::path nativeOf(const QString& path) { return detail::nativePath(path); }
QString touch(const QTemporaryDir& dir, const QString& fileName, const QByteArray& bytes = "generated") {
    QFile file(dir.filePath(fileName));
    if (file.open(QIODevice::WriteOnly)) file.write(bytes);
    return dir.filePath(fileName);
}
struct FakeTrash final : TrashBackend {
    fs::path bin;
    QStringList failNames;
    QStringList asked;
    explicit FakeTrash(fs::path folder) : bin(std::move(folder)) {}
    DeleteResult moveToTrash(const MutationTarget& target) override {
        const QString leaf = name(target.path);
        asked << leaf;
        if (failNames.contains(leaf)) return {target.path, DeleteOutcome::InUse, "generated failure"};
        std::error_code error;
        fs::rename(target.path, bin / target.path.filename(), error);
        return {target.path, error ? DeleteOutcome::Failed : DeleteOutcome::Trashed, error ? error.message() : std::string{}};
    }
};
QStringList namesIn(const QVariantList& rows, const char* key = "name") {
    QStringList out;
    for (const auto& row : rows) out << row.toMap().value(key).toString();
    out.sort();
    return out;
}
}

class AssetGroupTests final : public QObject {
    Q_OBJECT
private slots:
    // --- Grouping
    void rawAndRenderedPair_data() {
        QTest::addColumn<QStringList>("files");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("raw+jpeg") << QStringList{"IMG.JPG", "IMG.CR2"} << QStringList{"IMG.CR2", "IMG.JPG"};
        QTest::newRow("raw+heic") << QStringList{"IMG.HEIC", "IMG.CR2"} << QStringList{"IMG.CR2", "IMG.HEIC"};
        QTest::newRow("jpeg+xmp stem") << QStringList{"IMG.xmp", "IMG.jpg"} << QStringList{"IMG.jpg", "IMG.xmp"};
        QTest::newRow("cr2+full name") << QStringList{"IMG.CR2.xmp", "IMG.CR2"} << QStringList{"IMG.CR2", "IMG.CR2.xmp"};
        QTest::newRow("both sidecars") << QStringList{"IMG.CR2.xmp", "IMG.xmp", "IMG.CR2"} << QStringList{"IMG.CR2", "IMG.CR2.xmp", "IMG.xmp"};
        QTest::newRow("live photo") << QStringList{"IMG.MOV", "IMG.HEIC"} << QStringList{"IMG.HEIC", "IMG.MOV"};
        QTest::newRow("png+xmp") << QStringList{"IMG.xmp", "IMG.png"} << QStringList{"IMG.png", "IMG.xmp"};
        QTest::newRow("several dots") << QStringList{"a.b.c.xmp", "a.b.c.jpg", "a.b.jpg"} << QStringList{"a.b.c.jpg", "a.b.c.xmp"};
    }
    void rawAndRenderedPair() {
        QFETCH(QStringList, files);
        QFETCH(QStringList, expected);
        const auto snapshot = groups(files);
        QCOMPARE(snapshot.groups.size(), std::size_t(1));
        const auto& group = snapshot.groups[0];
        QCOMPARE(members(group), expected);
        QCOMPARE(group.primary, std::size_t(0));
        QCOMPARE(group.flags, 0u);
        QCOMPARE(snapshot.listingGeneration, std::uint64_t(7));
    }
    void sidecarStylesAreTold() {
        const auto group = groups({"IMG.CR2.xmp", "IMG.xmp", "IMG.CR2"}).groups.at(0);
        QCOMPARE(group.members[0].role, Role::Raw);
        QCOMPARE(group.members[1].sidecar, SidecarStyle::FullName);
        QCOMPARE(group.members[2].sidecar, SidecarStyle::Stem);
    }
    void neverGrouped() {
        QVERIFY(groups({"IMG.MOV"}).groups.empty());                   // video alone
        QVERIFY(groups({"IMG.MOV", "IMG.xmp"}).groups.empty());        // video with a sidecar is still no asset
        QVERIFY(groups({"IMG.png", "IMG.MOV"}).groups.empty());        // video only pairs with Raw or Rendered
        QVERIFY(groups({"IMG.jpg", "IMG.txt"}).groups.empty());        // Other
        QVERIFY(groups({"IMG.xmp"}).groups.empty());
        QVERIFY(groups({".xmp", ".jpg", "noextension"}).groups.empty());
        std::vector<ListingInput> folder = listing({"IMG.CR2"});
        folder.push_back({root() / "IMG.jpg", false}); // a folder or link of that name
        QVERIFY(groupListing(root(), folder, false, 1).groups.empty());
        std::vector<ListingInput> elsewhere = listing({"IMG.CR2"});
        elsewhere.push_back({root() / "sub" / "IMG.jpg", true}); // same directory only
        QVERIFY(groupListing(root(), elsewhere, false, 1).groups.empty());
    }
    void embeddedMotionIsAFlagNeverAMember() {
        const auto snapshot = groups({"PXL_1.MP.jpg", "PXL_1.MP.xmp", "plain.jpg", "plain.xmp"});
        QCOMPARE(snapshot.groups.size(), std::size_t(2));
        const Group& motion = name(snapshot.groups[0].members[0].path).startsWith("PXL") ? snapshot.groups[0] : snapshot.groups[1];
        const Group& plain = &motion == &snapshot.groups[0] ? snapshot.groups[1] : snapshot.groups[0];
        QVERIFY(has(motion, GroupFlag::EmbeddedMotion));
        QCOMPARE(motion.members.size(), std::size_t(2));
        QVERIFY(!has(plain, GroupFlag::EmbeddedMotion));
    }
    void derivedStaysOutBesideRawOrRendered() {
        const auto group = groups({"IMG.CR2", "IMG.JPG", "IMG.TIF"}).groups.at(0);
        QCOMPARE(members(group), (QStringList{"IMG.CR2", "IMG.JPG"}));
        QCOMPARE(group.flags, 0u);
    }
    // --- Same-stem collisions
    void rawCollisionMakesTheSidecarAmbiguous() {
        const auto group = groups({"IMG.CR2", "IMG.NEF", "IMG.xmp"}).groups.at(0);
        QVERIFY(has(group, GroupFlag::Collision));
        QVERIFY(has(group, GroupFlag::AmbiguousSidecar));
        QCOMPARE(group.members.size(), std::size_t(3));
    }
    void fullNameSidecarIsNeverAmbiguous() {
        const auto group = groups({"IMG.CR2", "IMG.NEF", "IMG.CR2.xmp"}).groups.at(0);
        QVERIFY(has(group, GroupFlag::Collision));
        QVERIFY(!has(group, GroupFlag::AmbiguousSidecar));
    }
    void caseVariants() {
        const auto sensitive = groups({"a.JPG", "a.jpg"}, true);
        QCOMPARE(sensitive.groups.size(), std::size_t(1));
        QVERIFY(has(sensitive.groups[0], GroupFlag::Collision));
        QCOMPARE(sensitive.groups[0].members.size(), std::size_t(2));
        const auto sidecars = groups({"a.CR2", "a.xmp", "a.XMP"}, true);
        QVERIFY(has(sidecars.groups.at(0), GroupFlag::Collision));
        QVERIFY(has(sidecars.groups.at(0), GroupFlag::AmbiguousSidecar));
        // Different stems on a case-sensitive volume are different assets; on NTFS the same names would be one.
        QVERIFY(groups({"A.CR2", "a.jpg"}, true).groups.empty());
        QCOMPARE(groups({"A.CR2", "a.jpg"}, false).groups.size(), std::size_t(1));
    }
    void caseVariantsOnThisVolume() {
        // The real volume decides: NTFS and APFS hold one file, ext4 and tmpfs two.
        QTemporaryDir dir;
        touch(dir, "a.JPG");
        touch(dir, "a.jpg");
        touch(dir, "A.XMP");
        const bool separate = QDir(dir.path()).entryList(QDir::Files).size() == 3;
        const auto e = expand({nativeOf(dir.filePath("a.jpg"))}, Scope::Asset, Operation::Trash, nullptr);
        if (separate) {
            QCOMPARE(included(e), QStringList{"a.jpg"});
            QCOMPARE(excluded(e).at("a.JPG"), Exclusion::Collision);
            QCOMPARE(excluded(e).size(), std::size_t(1)); // A.XMP has another stem here
        } else {
            QCOMPARE(included(e), (QStringList{"a.jpg", "A.XMP"})); // one file, however the selection spells it
            QVERIFY(e.excluded.empty());
        }
    }
    void repeatedFamilies() {
        QVERIFY(has(groups({"IMG.jpg", "IMG.jpeg"}).groups.at(0), GroupFlag::Collision));
        QVERIFY(has(groups({"IMG.heic", "IMG.heif"}).groups.at(0), GroupFlag::Collision));
        QVERIFY(has(groups({"IMG.jpg", "IMG.heic"}).groups.at(0), GroupFlag::Collision));
        const auto two = groups({"IMG.png", "IMG.tif", "IMG.xmp"}).groups.at(0);
        QVERIFY(has(two, GroupFlag::Collision));
        QVERIFY(has(two, GroupFlag::AmbiguousSidecar));
    }
    void seventeenCandidates() {
        QStringList files{"IMG.CR2.xmp"};
        const QStringList exts{"cr2", "nef", "arw", "dng", "orf", "rw2", "pef", "raf", "sr2", "jpg", "heic", "avif", "jxl", "mov", "mp4", "xmp"};
        for (const auto& ext : exts) files << "IMG." + ext;
        QCOMPARE(files.size(), 17);
        const auto group = groups(files).groups.at(0);
        QVERIFY(has(group, GroupFlag::Collision));
        QCOMPARE(group.members.size(), kMaxMembers);
    }
    void seventeenthIsLimitReachedNeverDropped() {
        FakeVolume volume;
        InstallVolume install(volume);
        QStringList files;
        for (int i = 0; i < 16; ++i) files << QString("IMG.%1").arg(QString("cr2 nef arw dng orf rw2 pef raf sr2 jpg heic avif jxl mov mp4 xmp").split(' ')[i]);
        files << "IMG.CR2.xmp"; // a seventeenth candidate
        QCOMPARE(files.size(), 17);
        volume.directories[root()] = files;
        const auto e = expand({root() / "IMG.cr2"}, Scope::Asset, Operation::Trash, nullptr);
        QCOMPARE(e.included.size(), std::size_t(2)); // the request and its exact sidecar
        int limited = 0;
        for (const auto& item : e.excluded) limited += item.reason == Exclusion::LimitReached;
        QCOMPARE(limited, 1);
        QCOMPARE(e.excluded.size(), std::size_t(15));
    }
    void unicodeStemsMatchAfterCaseFolding() {
        // Final sigma: the three spellings differ under toLower and only meet under case folding.
        const QStringList files{QString::fromUtf8("\xCE\xA3\xCE\x91\xCE\xA3.CR2"), QString::fromUtf8("\xCF\x83\xCE\xB1\xCF\x82.jpg")};
        QCOMPARE(groups(files, false).groups.size(), std::size_t(1));
        QVERIFY(groups(files, true).groups.empty());
    }
    void groupIdsAndOrderAreStable() {
        const QStringList files{"b.jpg", "b.xmp", "a.mp4", "a.CR2", "a.mov", "a.jpg", "a.xmp"};
        const auto snapshot = groups(files);
        QCOMPARE(snapshot.groups.size(), std::size_t(2));
        QCOMPARE(QString::fromStdString(snapshot.groups[0].stemKey), QString("a"));
        QCOMPARE(members(snapshot.groups[0]), (QStringList{"a.CR2", "a.jpg", "a.mov", "a.mp4", "a.xmp"}));
        QVERIFY(snapshot.groups[0].id != snapshot.groups[1].id);
    }
    void truncatedListingSetsIncomplete() {
        const auto snapshot = groups({"a.CR2", "a.jpg"}, false, true);
        QVERIFY(has(snapshot.groups.at(0), GroupFlag::Incomplete));
        QVERIFY(!has(groups({"a.CR2", "a.jpg"}).groups.at(0), GroupFlag::Incomplete));
    }
    void hintsAreFencedByGeneration() {
        const auto snapshot = groups({"a.CR2", "a.jpg"});
        QCOMPARE(detail::hintIfCurrent(&snapshot, 7), &snapshot);
        QVERIFY(!detail::hintIfCurrent(&snapshot, 8));
        QVERIFY(!detail::hintIfCurrent(nullptr, 7));
    }

    // --- File-only versus asset
    void fileOnlyReturnsExactlyTheRequest_data() {
        QTest::addColumn<Operation>("operation");
        QTest::newRow("trash") << Operation::Trash;
        QTest::newRow("copy") << Operation::Copy;
        QTest::newRow("move") << Operation::Move;
        QTest::newRow("rename") << Operation::Rename;
        QTest::newRow("lossless") << Operation::LosslessTransform;
    }
    void fileOnlyReturnsExactlyTheRequest() {
        QFETCH(Operation, operation);
        FakeVolume volume;
        InstallVolume install(volume);
        volume.directories[root()] = {"IMG.CR2", "IMG.JPG", "IMG.xmp", "IMG.TIF", "other.jpg"};
        const auto e = expand({root() / "IMG.CR2"}, Scope::FileOnly, operation, nullptr);
        QCOMPARE(included(e), QStringList{"IMG.CR2"});
        const auto left = excluded(e);
        QCOMPARE(left.size(), std::size_t(3));
        QCOMPARE(left.at("IMG.JPG"), Exclusion::NotApplicable);
        QCOMPARE(left.at("IMG.xmp"), Exclusion::NotApplicable);
        QCOMPARE(left.at("IMG.TIF"), Exclusion::NotApplicable);
        QVERIFY(!e.included[0].companion);
    }
    void assetAddsExactlyTheTablesMembers_data() {
        QTest::addColumn<Operation>("operation");
        QTest::newRow("trash") << Operation::Trash;
        QTest::newRow("copy") << Operation::Copy;
        QTest::newRow("move") << Operation::Move;
        QTest::newRow("rename") << Operation::Rename;
    }
    void assetAddsExactlyTheTablesMembers() {
        QFETCH(Operation, operation);
        FakeVolume volume;
        InstallVolume install(volume);
        volume.directories[root()] = {"IMG.xmp", "IMG.MOV", "IMG.TIF", "IMG.JPG", "IMG.CR2", "IMG.CR2.xmp", "other.jpg"};
        const auto e = expand({root() / "IMG.JPG"}, Scope::Asset, operation, nullptr);
        // Requested first, then companions in role order.
        QCOMPARE(included(e), (QStringList{"IMG.JPG", "IMG.CR2", "IMG.MOV", "IMG.CR2.xmp", "IMG.xmp"}));
        QVERIFY(!e.included[0].companion);
        for (std::size_t i = 1; i < e.included.size(); ++i) {
            QVERIFY(e.included[i].companion);
            QCOMPARE(name(e.included[i].requestedBy), QString("IMG.JPG"));
        }
        QCOMPARE(excluded(e).size(), std::size_t(1)); // the export stays, and the confirmation says so
        QCOMPARE(excluded(e).at("IMG.TIF"), Exclusion::NotApplicable);
    }
    void losslessIgnoresAsset() {
        FakeVolume volume;
        InstallVolume install(volume);
        volume.directories[root()] = {"IMG.CR2", "IMG.JPG"};
        const auto e = expand({root() / "IMG.CR2"}, Scope::Asset, Operation::LosslessTransform, nullptr);
        QCOMPARE(included(e), QStringList{"IMG.CR2"});
        QCOMPARE(excluded(e).at("IMG.JPG"), Exclusion::NotApplicable);
    }
    void sidecarWriteIsNotAnExpansion() {
        FakeVolume volume;
        InstallVolume install(volume);
        volume.directories[root()] = {"IMG.CR2", "IMG.xmp"};
        const auto e = expand({root() / "IMG.CR2"}, Scope::Asset, Operation::SidecarWrite, nullptr);
        QCOMPARE(included(e), QStringList{"IMG.CR2"});
        QVERIFY(e.excluded.empty());
        QCOMPARE(volume.reads, 0);
    }
    void collisionMembersAreExcludedWithReasons() {
        FakeVolume volume;
        InstallVolume install(volume);
        volume.directories[root()] = {"IMG.CR2", "IMG.NEF", "IMG.xmp", "IMG.CR2.xmp", "IMG.NEF.xmp"};
        const auto e = expand({root() / "IMG.CR2"}, Scope::Asset, Operation::Trash, nullptr);
        QCOMPARE(included(e), (QStringList{"IMG.CR2", "IMG.CR2.xmp"})); // its exact sidecar stays with it
        const auto left = excluded(e);
        QCOMPARE(left.at("IMG.NEF"), Exclusion::Collision);
        QCOMPARE(left.at("IMG.NEF.xmp"), Exclusion::Collision);
        QCOMPARE(left.at("IMG.xmp"), Exclusion::Ambiguous);
    }
    void collisionMemberCanStillBeRequestedExplicitly() {
        FakeVolume volume;
        InstallVolume install(volume);
        volume.directories[root()] = {"IMG.CR2", "IMG.NEF", "IMG.xmp"};
        const auto both = expand({root() / "IMG.CR2", root() / "IMG.NEF"}, Scope::Asset, Operation::Trash, nullptr);
        QCOMPARE(included(both), (QStringList{"IMG.CR2", "IMG.NEF"}));
        QCOMPARE(excluded(both).at("IMG.xmp"), Exclusion::Ambiguous);
        const auto sidecar = expand({root() / "IMG.xmp"}, Scope::FileOnly, Operation::Trash, nullptr);
        QCOMPARE(included(sidecar), QStringList{"IMG.xmp"});
    }
    void renameFallsBackToFileOnlyInACollisionGroup() {
        FakeVolume volume;
        InstallVolume install(volume);
        volume.directories[root()] = {"IMG.CR2", "IMG.NEF", "IMG.CR2.xmp"};
        const auto e = expand({root() / "IMG.CR2"}, Scope::Asset, Operation::Rename, nullptr);
        QCOMPARE(included(e), QStringList{"IMG.CR2"}); // not even the exact sidecar: a rename would then split the group
        QCOMPARE(excluded(e).at("IMG.CR2.xmp"), Exclusion::Collision);
    }
    void requestedCompanionExpandsTheSameWay() {
        FakeVolume volume;
        InstallVolume install(volume);
        volume.directories[root()] = {"IMG.CR2", "IMG.JPG", "IMG.xmp"};
        const auto e = expand({root() / "IMG.xmp"}, Scope::Asset, Operation::Copy, nullptr);
        QCOMPARE(included(e), (QStringList{"IMG.xmp", "IMG.CR2", "IMG.JPG"}));
    }
    void twoRequestedMembersOfOneGroupAreDeduplicated() {
        FakeVolume volume;
        InstallVolume install(volume);
        volume.directories[root()] = {"IMG.CR2", "IMG.JPG", "IMG.xmp"};
        const auto e = expand({root() / "IMG.xmp", root() / "IMG.CR2", root() / "IMG.CR2"}, Scope::Asset, Operation::Copy, nullptr);
        QCOMPARE(included(e), (QStringList{"IMG.xmp", "IMG.CR2", "IMG.JPG"}));
        QVERIFY(e.excluded.empty());
        QVERIFY(!e.included[1].companion);
    }
    void requestedLeftOutExportIsNotListedAsLeftOut() {
        FakeVolume volume;
        InstallVolume install(volume);
        volume.directories[root()] = {"IMG.CR2", "IMG.JPG", "IMG.TIF"};
        const auto e = expand({root() / "IMG.CR2", root() / "IMG.TIF"}, Scope::Asset, Operation::Trash, nullptr);
        QCOMPARE(included(e), (QStringList{"IMG.CR2", "IMG.TIF", "IMG.JPG"}));
        QVERIFY(e.excluded.empty());
    }
    void folderAndLinkRequestsNeverGainCompanions() {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkdir("album.jpg"));
        touch(dir, "album.xmp");
        const auto e = expand({nativeOf(dir.filePath("album.jpg"))}, Scope::Asset, Operation::Trash, nullptr);
        QCOMPARE(included(e), QStringList{"album.jpg"});
        QVERIFY(e.excluded.empty());
    }

    // --- Bounds
    void requestsStopAtTheCapWithTheRestLimitReached() {
        FakeVolume volume;
        InstallVolume install(volume);
        const std::size_t requests = kMaxExpandedItems + 5;
        QStringList files;
        files.reserve(qsizetype(requests));
        std::vector<fs::path> requested;
        requested.reserve(requests);
        for (std::size_t i = 0; i < requests; ++i) {
            const QString file = QString("IMG_%1.CR2").arg(i);
            files << file;
            requested.push_back(root() / detail::nativePath(file));
        }
        volume.directories[root()] = files;
        const auto e = expand(requested, Scope::Asset, Operation::Trash, nullptr);
        QCOMPARE(e.included.size(), kMaxExpandedItems);
        QCOMPARE(e.excluded.size() + e.excludedNotListed, std::size_t(5));
        QCOMPARE(e.excluded[0].reason, Exclusion::LimitReached);
    }
    void companionsStopAtTheCapAndExcludedAreCounted() {
        FakeVolume volume;
        InstallVolume install(volume);
        const std::size_t requests = 60000; // 120,000 members in all: 100,000 fit, 20,000 don't
        QStringList files;
        files.reserve(qsizetype(requests * 2));
        std::vector<fs::path> requested;
        requested.reserve(requests);
        for (std::size_t i = 0; i < requests; ++i) {
            const QString stem = QString("IMG_%1").arg(i);
            files << stem + ".CR2" << stem + ".JPG";
            requested.push_back(root() / detail::nativePath(stem + ".CR2"));
        }
        volume.directories[root()] = files;
        const auto e = expand(requested, Scope::Asset, Operation::Trash, nullptr);
        QCOMPARE(e.included.size(), kMaxExpandedItems);
        QCOMPARE(e.excluded.size(), kMaxExcludedListed);
        QCOMPARE(e.excludedNotListed, std::size_t(20000) - kMaxExcludedListed);
        for (const auto& item : e.excluded) QCOMPARE(item.reason, Exclusion::LimitReached);
        QCOMPARE(volume.reads, 1);
    }
    void fileOnlyCompanionsAreCountedPastTheListedLimit() {
        FakeVolume volume;
        InstallVolume install(volume);
        QStringList files;
        std::vector<fs::path> requested;
        for (int i = 0; i < 1000; ++i) {
            files << QString("IMG_%1.CR2").arg(i) << QString("IMG_%1.JPG").arg(i);
            requested.push_back(root() / detail::nativePath(QString("IMG_%1.CR2").arg(i)));
        }
        volume.directories[root()] = files;
        const auto e = expand(requested, Scope::FileOnly, Operation::Trash, nullptr);
        QCOMPARE(e.included.size(), std::size_t(1000));
        QCOMPARE(e.excluded.size(), kMaxExcludedListed);
        QCOMPARE(e.excludedNotListed, std::size_t(1000) - kMaxExcludedListed);
    }

    // --- Lookup cost
    void withAHintOnlyItsMembersAreProbed() {
        FakeVolume volume;
        InstallVolume install(volume);
        const auto hint = groupListing(root(), listing({"a.CR2", "a.jpg", "a.xmp", "b.CR2", "b.jpg"}), false, 3);
        const auto e = expand({root() / "a.CR2"}, Scope::Asset, Operation::Trash, &hint);
        QCOMPARE(included(e), (QStringList{"a.CR2", "a.jpg", "a.xmp"}));
        QCOMPARE(volume.reads, 0);
        QCOMPARE(volume.stats, 3); // not b's members, and no guessed names
        QCOMPARE(volume.caseProbes, 0);
    }
    void aHintForAnotherDirectoryIsIgnored() {
        FakeVolume volume;
        InstallVolume install(volume);
        volume.directories[root()] = {"a.CR2", "a.jpg"};
        const auto hint = groupListing(root() / "elsewhere", listing({"a.CR2", "a.xmp"}, root() / "elsewhere"), false, 3);
        const auto e = expand({root() / "a.CR2"}, Scope::Asset, Operation::Trash, &hint);
        QCOMPARE(included(e), (QStringList{"a.CR2", "a.jpg"}));
        QCOMPARE(volume.reads, 1);
    }
    void withoutAHintOneReadPerParentFindsMixedCase() {
        FakeVolume volume;
        InstallVolume install(volume);
        volume.sensitive = true;
        const fs::path other = root() / "other";
        volume.directories[root()] = {"IMG_1.CR2", "IMG_1.jPg", "IMG_2.CR2", "IMG_2.Xmp"};
        volume.directories[other] = {"x.ARW", "x.JPEG"};
        const auto e = expand({root() / "IMG_1.CR2", root() / "IMG_2.CR2", other / "x.ARW"}, Scope::Asset, Operation::Copy, nullptr);
        QCOMPARE(volume.reads, 2);
        QCOMPARE(volume.caseProbes, 2);
        QCOMPARE(volume.stats, 0);
        QCOMPARE(included(e), (QStringList{"IMG_1.CR2", "IMG_2.CR2", "x.ARW", "IMG_1.jPg", "x.JPEG", "IMG_2.Xmp"}));
    }
    void aSlowVolumeEndsAtTheBudget() {
        FakeVolume volume;
        InstallVolume install(volume);
        const fs::path second = root() / "second";
        volume.directories[root()] = {"a.CR2", "a.jpg"};
        volume.directories[second] = {"b.CR2", "b.jpg"};
        volume.costPerRead = std::chrono::milliseconds(kExpandBudgetMs); // the first read spends everything
        const auto e = expand({root() / "a.CR2", second / "b.CR2"}, Scope::Asset, Operation::Trash, nullptr);
        QVERIFY(e.companionsIncomplete);
        QCOMPARE(volume.reads, 1);
        QCOMPARE(included(e), (QStringList{"a.CR2", "b.CR2"})); // unchecked companions stay out, and a partial read can't group
        QVERIFY(e.excluded.empty());
        QVERIFY(!e.cancelled);
    }
    void aReadThatEndsInTime() {
        FakeVolume volume;
        InstallVolume install(volume);
        const fs::path second = root() / "second";
        volume.directories[root()] = {"a.CR2", "a.jpg"};
        volume.directories[second] = {"b.CR2", "b.jpg"};
        volume.costPerEntry = std::chrono::milliseconds(kExpandBudgetMs / 2 - 1); // the first read fits, then the budget is gone
        volume.costPerRead = std::chrono::milliseconds(0);
        const auto e = expand({root() / "a.CR2", second / "b.CR2"}, Scope::Asset, Operation::Trash, nullptr);
        QCOMPARE(volume.reads, 2);
        QVERIFY(e.companionsIncomplete);
        QCOMPARE(included(e), (QStringList{"a.CR2", "b.CR2", "a.jpg"}));
    }
    void anUnreadableParentIsReportedNotGuessed() {
        FakeVolume volume;
        InstallVolume install(volume);
        volume.unreadable = true;
        const auto e = expand({root() / "a.CR2"}, Scope::Asset, Operation::Trash, nullptr);
        QVERIFY(e.companionsIncomplete);
        QCOMPARE(included(e), QStringList{"a.CR2"});
        // Duplicates still collapse when no listing ever arrives.
        QCOMPARE(included(expand({root() / "a.CR2", root() / "a.CR2"}, Scope::Asset, Operation::Trash, nullptr)), QStringList{"a.CR2"});
    }

    // --- Expansion robustness
    void aTruncatedHintStillFindsCompanionsInTheParent() {
        FakeVolume volume;
        InstallVolume install(volume);
        volume.directories[root()] = {"a.CR2", "a.jpg", "a.xmp"};
        // The listing stopped before a.xmp, and in the second case before any group formed at all.
        const auto flagged = groupListing(root(), listing({"a.CR2", "a.jpg"}), false, 3, true);
        const auto first = expand({root() / "a.CR2"}, Scope::Asset, Operation::Trash, &flagged);
        QCOMPARE(included(first), (QStringList{"a.CR2", "a.jpg", "a.xmp"}));
        const auto empty = groupListing(root(), listing({"a.CR2"}), false, 3, true);
        const auto second = expand({root() / "a.CR2"}, Scope::Asset, Operation::Trash, &empty);
        QCOMPARE(included(second), (QStringList{"a.CR2", "a.jpg", "a.xmp"}));
        QCOMPARE(volume.reads, 2);
    }
    void aStaleHintGivesMissingNotIncluded() {
        FakeVolume volume;
        InstallVolume install(volume);
        const auto hint = groupListing(root(), listing({"a.CR2", "a.jpg", "a.xmp"}), false, 3);
        volume.gone.insert(root() / "a.xmp");
        const auto e = expand({root() / "a.CR2"}, Scope::Asset, Operation::Trash, &hint);
        QCOMPARE(included(e), (QStringList{"a.CR2", "a.jpg"}));
        QCOMPARE(excluded(e).at("a.xmp"), Exclusion::Missing);
    }
    void cancellationDuringLookupsLeavesNothing() {
        FakeVolume volume;
        InstallVolume install(volume);
        volume.directories[root()] = {"a.CR2", "a.jpg"};
        std::stop_source stop;
        volume.duringRead = [&] { stop.request_stop(); };
        const auto e = expand({root() / "a.CR2"}, Scope::Asset, Operation::Trash, nullptr, stop.get_token());
        QVERIFY(e.cancelled);
        QVERIFY(e.included.empty());
        QVERIFY(e.excluded.empty());
    }

    // --- Rename
    void renameAssetPreservesExtensions() {
        FakeVolume volume;
        InstallVolume install(volume);
        volume.directories[root()] = {"IMG.CR2", "IMG.JPG", "IMG.CR2.xmp", "IMG.xmp"};
        const auto e = expand({root() / "IMG.CR2"}, Scope::Asset, Operation::Rename, nullptr);
        const auto renames = renameAsset(e, "Holiday");
        QCOMPARE(renames.size(), std::size_t(4));
        QStringList to;
        for (const auto& [from, target] : renames) { QCOMPARE(target.parent_path(), root()); to << name(target); }
        QCOMPARE(to, (QStringList{"Holiday.CR2", "Holiday.JPG", "Holiday.CR2.xmp", "Holiday.xmp"}));
    }
    void renameAssetWithSeveralDots() {
        FakeVolume volume;
        InstallVolume install(volume);
        volume.directories[root()] = {"my.shot.CR2", "my.shot.CR2.xmp"};
        const auto e = expand({root() / "my.shot.CR2"}, Scope::Asset, Operation::Rename, nullptr);
        QStringList to;
        for (const auto& [from, target] : renameAsset(e, "Holiday.v2")) to << name(target);
        QCOMPARE(to, (QStringList{"Holiday.v2.CR2", "Holiday.v2.CR2.xmp"}));
    }
    void renameAssetRefusesAnInvalidStem() {
        FakeVolume volume;
        InstallVolume install(volume);
        volume.directories[root()] = {"IMG.CR2", "IMG.xmp"};
        const auto e = expand({root() / "IMG.CR2"}, Scope::Asset, Operation::Rename, nullptr);
        QVERIFY(renameAsset(e, "").empty());
        QVERIFY(renameAsset(e, "a/b").empty());
        QVERIFY(renameAsset(e, "CON").empty());
        QVERIFY(renameAsset(e, std::string(252, 'x')).empty()); // the stem alone fits; with its extension a member wouldn't
    }
    void planRenamePreviewsEveryMemberAndAssetIsRefused() {
        QTemporaryDir dir;
        const QString cr2 = touch(dir, "IMG.CR2");
        touch(dir, "IMG.jpg");
        touch(dir, "IMG.CR2.xmp");
        MutationService service;
        QSignalSpy ready(&service, &MutationService::renamePlanReady);
        const quint64 id = service.planRename(cr2, "Holiday.CR2", "asset");
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, kWait);
        QCOMPARE(ready[0][0].toULongLong(), id);
        const QVariantMap summary = ready[0][1].toMap();
        QCOMPARE(summary["scope"].toString(), QString("asset"));
        QVERIFY(summary["refused"].toBool());
        QVERIFY(!summary["refusal"].toString().isEmpty());
        QCOMPARE(namesIn(summary["renames"].toList(), "to").size(), 3);
        QVERIFY(namesIn(summary["renames"].toList(), "to").join('|').contains("Holiday.CR2.xmp"));
        QVERIFY(summary["collisions"].toList().isEmpty());
        QVERIFY(!service.confirmRename(id)); // refused until the journal lands
        QVERIFY(QFile::exists(cr2) && QFile::exists(dir.filePath("IMG.jpg")) && QFile::exists(dir.filePath("IMG.CR2.xmp")));
        QVERIFY(!QFile::exists(dir.filePath("Holiday.CR2")));
    }
    void planRenameFindsTargetCollisionsInAssetScope() {
        QTemporaryDir dir;
        const QString cr2 = touch(dir, "IMG.CR2");
        touch(dir, "IMG.xmp");
        touch(dir, "HOLIDAY.XMP"); // a case variant of a target
        MutationService service;
        QSignalSpy ready(&service, &MutationService::renamePlanReady);
        service.planRename(cr2, "Holiday.CR2", "asset");
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, kWait);
        const QVariantList collisions = ready[0][1].toMap()["collisions"].toList();
#ifdef Q_OS_WIN
        QCOMPARE(collisions.size(), 1);
        QVERIFY(collisions[0].toMap()["target"].toString().endsWith("Holiday.xmp"));
#else
        QVERIFY(collisions.isEmpty()); // case-sensitive: Holiday.xmp is free. Real-volume check only.
#endif
    }
    void fileScopeRenameRunsThroughThePlan() {
        QTemporaryDir dir;
        const QString cr2 = touch(dir, "IMG.CR2");
        touch(dir, "IMG.xmp");
        MutationService service;
        QSignalSpy ready(&service, &MutationService::renamePlanReady), finished(&service, &MutationService::renameFinished), renamed(&service, &MutationService::pathRenamed);
        const quint64 id = service.planRename(cr2, "Holiday.CR2", "file");
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, kWait);
        const QVariantMap summary = ready[0][1].toMap();
        QVERIFY(!summary["refused"].toBool());
        QCOMPARE(summary["renames"].toList().size(), 1);
        QCOMPARE(namesIn(summary["excluded"].toList()), QStringList{"IMG.xmp"}); // shown: it keeps its name
        QVERIFY(service.confirmRename(id));
        QVERIFY(!service.confirmRename(id)); // one confirmation per plan
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
        QVERIFY(!service.confirmRename(id)); // and not once it has finished either
        QVERIFY(finished[0][1].toList()[0].toMap()["success"].toBool());
        QCOMPARE(renamed.count(), 1);
        QVERIFY(QFile::exists(dir.filePath("Holiday.CR2")) && QFile::exists(dir.filePath("IMG.xmp")));
    }
    void aChangedSourceIsNotRenamed() {
        QTemporaryDir dir;
        const QString cr2 = touch(dir, "IMG.CR2");
        MutationService service;
        QSignalSpy ready(&service, &MutationService::renamePlanReady), finished(&service, &MutationService::renameFinished);
        const quint64 id = service.planRename(cr2, "Holiday.CR2", "file");
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, kWait);
        QVERIFY(QFile::remove(cr2));
        touch(dir, "IMG.CR2", "a different file with different bytes");
        QVERIFY(service.confirmRename(id));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
        const QVariantMap result = finished[0][1].toList()[0].toMap();
        QVERIFY(!result["success"].toBool());
        QVERIFY(result["error"].toString().startsWith("Changed"));
        QVERIFY(QFile::exists(cr2) && !QFile::exists(dir.filePath("Holiday.CR2")));
    }
    void anOlderRenamePreviewCannotReplaceANewerOne() {
        QTemporaryDir dir;
        const QString cr2 = touch(dir, "IMG.CR2");
        MutationService service;
        QSignalSpy ready(&service, &MutationService::renamePlanReady);
        const quint64 first = service.planRename(cr2, "One.CR2", "file");
        const quint64 second = service.planRename(cr2, "Two.CR2", "file");
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty(), kWait);
        const quint64 third = service.planRename(cr2, "Three.CR2", "file"); // third barrier: its result proves the first is gone
        QTRY_VERIFY_WITH_TIMEOUT(ready.last()[0].toULongLong() == third, kWait);
        for (const auto& call : ready) QVERIFY(call[0].toULongLong() != first);
        QVERIFY(!service.confirmRename(first));
        QVERIFY(!service.confirmRename(second));
    }

    // --- Transfer plan
    void transferPlanListsCompanionsAndExecutesTheSnapshot() {
        QTemporaryDir source, target;
        const QString cr2 = touch(source, "IMG.CR2");
        touch(source, "IMG.jpg");
        touch(source, "IMG.xmp");
        touch(source, "other.jpg");
        CopyMoveService service;
        QSignalSpy ready(&service, &CopyMoveService::transferPlanReady), batch(&service, &CopyMoveService::batchFinished);
        const quint64 id = service.planTransfer({cr2}, target.path(), CopyMoveService::TransferMode::Copy, "asset");
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, kWait);
        QVERIFY(batch.isEmpty());
        QVERIFY(QDir(target.path()).isEmpty()); // nothing ran
        const QVariantMap summary = ready[0][1].toMap();
        QCOMPARE(summary["id"].toULongLong(), id);
        QCOMPARE(summary["scope"].toString(), QString("asset"));
        QCOMPARE(namesIn(summary["targets"].toList()), (QStringList{"IMG.CR2", "IMG.jpg", "IMG.xmp"}));
        QCOMPARE(namesIn(summary["companions"].toList()), (QStringList{"IMG.jpg", "IMG.xmp"}));
        QVERIFY(service.confirmTransfer(id, CopyMoveService::CollisionPolicy::RenameBoth));
        QVERIFY(!service.confirmTransfer(id, CopyMoveService::CollisionPolicy::RenameBoth)); // once
        QTRY_COMPARE_WITH_TIMEOUT(batch.count(), 1, kWait);
        QCOMPARE(batch[0][1].toInt(), 3);
        QCOMPARE(QDir(target.path()).entryList(QDir::Files).size(), 3);
        QVERIFY(QFile::exists(cr2)); // copy
        QVERIFY(!service.confirmTransfer(id, CopyMoveService::CollisionPolicy::RenameBoth)); // and not once it has finished either
    }
    void transferFileScopeMovesOnlyTheFile() {
        QTemporaryDir source, target;
        const QString cr2 = touch(source, "IMG.CR2");
        const QString xmp = touch(source, "IMG.xmp");
        CopyMoveService service;
        QSignalSpy ready(&service, &CopyMoveService::transferPlanReady), batch(&service, &CopyMoveService::batchFinished);
        const quint64 id = service.planTransfer({cr2}, target.path(), CopyMoveService::TransferMode::Move, "file");
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, kWait);
        const QVariantMap summary = ready[0][1].toMap();
        QCOMPARE(namesIn(summary["targets"].toList()), QStringList{"IMG.CR2"});
        QCOMPARE(summary["companions"].toList().size(), 1);
        const QVariantMap companion = summary["companions"].toList().at(0).toMap();
        QCOMPARE(companion["name"].toString(), QString("IMG.xmp"));
        QVERIFY(!companion["included"].toBool());
        QCOMPARE(companion["exclusion"].toString(), QString("notApplicable"));
        QVERIFY(service.confirmTransfer(id, CopyMoveService::CollisionPolicy::RenameBoth));
        QTRY_COMPARE_WITH_TIMEOUT(batch.count(), 1, kWait);
        QVERIFY(!QFile::exists(cr2) && QFile::exists(xmp));
        QVERIFY(QFile::exists(QDir(target.path()).filePath("IMG.CR2")));
    }
    void overwriteIsRefusedUntilB2() {
        QTemporaryDir source, target;
        const QString file = touch(source, "IMG.jpg", "new");
        touch(target, "IMG.jpg", "old");
        CopyMoveService service;
        QSignalSpy ready(&service, &CopyMoveService::transferPlanReady), batch(&service, &CopyMoveService::batchFinished), started(&service, &CopyMoveService::itemStarted);
        const quint64 id = service.planTransfer({file}, target.path(), CopyMoveService::TransferMode::Copy, "file");
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, kWait);
        QVERIFY(!service.confirmTransfer(id, CopyMoveService::CollisionPolicy::Overwrite));
        QVERIFY(!service.confirmTransfer(id, CopyMoveService::CollisionPolicy::Overwrite, true)); // consent has no effect yet
        QVERIFY(!service.active());
        QVERIFY(started.isEmpty());
        QFile existing(QDir(target.path()).filePath("IMG.jpg"));
        QVERIFY(existing.open(QIODevice::ReadOnly));
        QCOMPARE(existing.readAll(), QByteArray("old"));
        // The refusal left the plan confirmable with an allowed policy.
        QVERIFY(service.confirmTransfer(id, CopyMoveService::CollisionPolicy::Skip));
        QTRY_COMPARE_WITH_TIMEOUT(batch.count(), 1, kWait);
    }
    void keepBothKeepsAnAssetTogether() {
        QTemporaryDir source, target;
        const QString cr2 = touch(source, "IMG.CR2");
        touch(source, "IMG.CR2.xmp");
        touch(source, "IMG.xmp");
        touch(target, "IMG.xmp", "another photo's sidecar");
        touch(target, "IMG (2).CR2", "another photo");
        CopyMoveService service;
        QSignalSpy ready(&service, &CopyMoveService::transferPlanReady), batch(&service, &CopyMoveService::batchFinished);
        const quint64 id = service.planTransfer({cr2}, target.path(), CopyMoveService::TransferMode::Copy, "asset");
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, kWait);
        const QVariantMap summary = ready[0][1].toMap();
        const QVariantList collisions = summary["collisions"].toList();
        QCOMPARE(collisions.size(), 1);
        QCOMPARE(collisions[0].toMap()["name"].toString(), QString("IMG.xmp"));
        QCOMPARE(collisions[0].toMap()["requestedBy"].toString(), cr2);
        QCOMPARE(namesIn(summary["targets"].toList(), "renamedTo"), (QStringList{"IMG (3).CR2", "IMG (3).CR2.xmp", "IMG (3).xmp"}));
        QVERIFY(service.confirmTransfer(id, CopyMoveService::CollisionPolicy::RenameBoth));
        QTRY_COMPARE_WITH_TIMEOUT(batch.count(), 1, kWait);
        const QDir out(target.path());
        QVERIFY(out.exists("IMG (3).CR2") && out.exists("IMG (3).CR2.xmp") && out.exists("IMG (3).xmp"));
        QCOMPARE(out.entryList(QDir::Files).size(), 5);
    }
    void skipLeavesACollidingAssetWhole() {
        QTemporaryDir source, target;
        const QString cr2 = touch(source, "IMG.CR2");
        touch(source, "IMG.xmp");
        const QString free = touch(source, "free.jpg");
        touch(target, "IMG.xmp", "another photo's sidecar");
        CopyMoveService service;
        QSignalSpy ready(&service, &CopyMoveService::transferPlanReady), batch(&service, &CopyMoveService::batchFinished);
        const quint64 id = service.planTransfer({cr2, free}, target.path(), CopyMoveService::TransferMode::Copy, "asset");
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, kWait);
        QVERIFY(service.confirmTransfer(id, CopyMoveService::CollisionPolicy::Skip));
        QTRY_COMPARE_WITH_TIMEOUT(batch.count(), 1, kWait);
        const QDir out(target.path());
        QVERIFY(out.exists("free.jpg") && !out.exists("IMG.CR2"));
        QCOMPARE(out.entryList(QDir::Files).size(), 2); // free.jpg and the sidecar that was there, nothing under another name
        QFile kept(out.filePath("IMG.xmp"));
        QVERIFY(kept.open(QIODevice::ReadOnly));
        QCOMPARE(kept.readAll(), QByteArray("another photo's sidecar"));
    }
    void aReplacedCompanionIsReportedAndTheOthersProceed() {
        QTemporaryDir source, target;
        const QString cr2 = touch(source, "IMG.CR2");
        const QString jpg = touch(source, "IMG.jpg");
        const QString xmp = touch(source, "IMG.xmp");
        CopyMoveService service;
        QSignalSpy ready(&service, &CopyMoveService::transferPlanReady), batch(&service, &CopyMoveService::batchFinished), item(&service, &CopyMoveService::itemFinished);
        const quint64 id = service.planTransfer({cr2}, target.path(), CopyMoveService::TransferMode::Copy, "asset");
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, kWait);
        QVERIFY(QFile::remove(xmp));
        touch(source, "IMG.xmp", "replaced after the confirmation showed it");
        QVERIFY(QFile::remove(jpg));
        QVERIFY(service.confirmTransfer(id, CopyMoveService::CollisionPolicy::RenameBoth));
        QTRY_COMPARE_WITH_TIMEOUT(batch.count(), 1, kWait);
        QCOMPARE(batch[0][1].toInt(), 1);
        QCOMPARE(batch[0][2].toInt(), 2);
        QStringList errors;
        for (const auto& call : item) if (!call[2].toBool()) errors << call[3].toString();
        errors.sort();
        QCOMPARE(errors.size(), 2);
        QVERIFY(errors[0].startsWith("Changed"));
        QVERIFY(errors[1].startsWith("Missing"));
        QVERIFY(!QDir(target.path()).exists("IMG.xmp"));
        QVERIFY(QDir(target.path()).exists("IMG.CR2"));
    }
    void aSupersededTransferPlanCannotBeConfirmed() {
        QTemporaryDir source, target;
        const QString file = touch(source, "IMG.jpg");
        CopyMoveService service;
        QSignalSpy ready(&service, &CopyMoveService::transferPlanReady);
        const quint64 first = service.planTransfer({file}, target.path(), CopyMoveService::TransferMode::Copy, "file");
        const quint64 second = service.planTransfer({file}, target.path(), CopyMoveService::TransferMode::Copy, "asset");
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, kWait);
        QCOMPARE(ready[0][0].toULongLong(), second);
        QVERIFY(!service.confirmTransfer(first, CopyMoveService::CollisionPolicy::RenameBoth));
        QCOMPARE(service.planTransfer({}, target.path(), CopyMoveService::TransferMode::Copy, "file"), quint64(0)); // withdraws
        QVERIFY(!service.confirmTransfer(second, CopyMoveService::CollisionPolicy::RenameBoth));
        QVERIFY(QDir(target.path()).isEmpty());
    }
    void transferToAMissingDestinationCannotRun() {
        QTemporaryDir source, target;
        const QString file = touch(source, "IMG.jpg");
        CopyMoveService service;
        QSignalSpy ready(&service, &CopyMoveService::transferPlanReady);
        const quint64 id = service.planTransfer({file}, target.filePath("gone"), CopyMoveService::TransferMode::Copy, "file");
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, kWait);
        QCOMPARE(ready[0][1].toMap()["error"].toString(), QString("destinationMissing"));
        QVERIFY(!service.confirmTransfer(id, CopyMoveService::CollisionPolicy::RenameBoth));
    }
    void movingIntoTheSameFolderIsRejected() {
        QTemporaryDir source;
        const QString file = touch(source, "IMG.jpg");
        CopyMoveService service;
        QSignalSpy ready(&service, &CopyMoveService::transferPlanReady);
        const quint64 id = service.planTransfer({file}, source.path(), CopyMoveService::TransferMode::Move, "file");
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, kWait);
        const QVariantMap summary = ready[0][1].toMap();
        QCOMPARE(summary["rejected"].toList().size(), 1);
        QCOMPARE(summary["rejected"].toList().at(0).toMap()["reason"].toString(), QString("sameFolder"));
        QVERIFY(summary["targets"].toList().isEmpty());
        QVERIFY(!service.confirmTransfer(id, CopyMoveService::CollisionPolicy::RenameBoth));
    }

    // --- FileOperationService
    void planScopedAssetListsAndTrashesTheIncludedList() {
        QTemporaryDir media, bin;
        const QString cr2 = touch(media, "IMG.CR2");
        const QString jpg = touch(media, "IMG.jpg");
        const QString xmp = touch(media, "IMG.xmp");
        const QString tif = touch(media, "IMG.tif");
        const QString other = touch(media, "other.jpg");
        auto backend = std::make_unique<FakeTrash>(nativeOf(bin.path()));
        FakeTrash* trash = backend.get();
        FileOperationService service(std::move(backend));
        QSignalSpy ready(&service, &FileOperationService::planReady), finished(&service, &FileOperationService::finished);
        const quint64 id = service.planScoped({cr2}, "grid", "asset");
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, kWait);
        const QVariantMap summary = ready[0][1].toMap();
        QCOMPARE(summary["scope"].toString(), QString("asset"));
        QCOMPARE(namesIn(summary["targets"].toList()), (QStringList{"IMG.CR2", "IMG.jpg", "IMG.xmp"}));
        QCOMPARE(namesIn(summary["companions"].toList()), (QStringList{"IMG.jpg", "IMG.tif", "IMG.xmp"}));
        for (const auto& row : summary["companions"].toList()) {
            const QVariantMap companion = row.toMap();
            const bool isTif = companion["name"].toString() == "IMG.tif";
            QCOMPARE(companion["included"].toBool(), !isTif);
            QCOMPARE(companion["exclusion"].toString(), isTif ? QString("notApplicable") : QString());
            QCOMPARE(companion["requestedBy"].toString(), cr2);
        }
        QVERIFY(service.confirm(id));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
        QStringList asked = trash->asked;
        asked.sort();
        QCOMPARE(asked, (QStringList{"IMG.CR2", "IMG.jpg", "IMG.xmp"}));
        QVERIFY(!QFile::exists(cr2) && !QFile::exists(jpg) && !QFile::exists(xmp));
        QVERIFY(QFile::exists(tif) && QFile::exists(other));
    }
    void partialFailureIsReportedPerMember() {
        QTemporaryDir media, bin;
        const QString cr2 = touch(media, "IMG.CR2");
        const QString jpg = touch(media, "IMG.jpg");
        auto backend = std::make_unique<FakeTrash>(nativeOf(bin.path()));
        backend->failNames = {"IMG.jpg"};
        FileOperationService service(std::move(backend));
        QSignalSpy ready(&service, &FileOperationService::planReady), finished(&service, &FileOperationService::finished), removed(&service, &FileOperationService::pathsRemoved);
        const quint64 id = service.planScoped({cr2}, "grid", "asset");
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, kWait);
        QVERIFY(service.confirm(id));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
        const QVariantList results = finished[0][1].toList();
        QCOMPARE(results.size(), 2);
        QCOMPARE(results[0].toMap()["outcome"].toString(), QString("trashed"));
        QCOMPARE(results[1].toMap()["outcome"].toString(), QString("inUse"));
        QCOMPARE(removed.count(), 1);
        QCOMPARE(removed[0][0].toStringList().size(), 1);
        QVERIFY(!QFile::exists(cr2) && QFile::exists(jpg));
    }
    void aCompanionReplacedBeforeConfirmIsChangedAtExecution() {
        QTemporaryDir media, bin;
        const QString cr2 = touch(media, "IMG.CR2");
        const QString xmp = touch(media, "IMG.xmp");
        FileOperationService service(std::make_unique<FakeTrash>(nativeOf(bin.path())));
        QSignalSpy ready(&service, &FileOperationService::planReady), finished(&service, &FileOperationService::finished);
        const quint64 id = service.planScoped({cr2}, "grid", "asset");
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, kWait);
        QVERIFY(QFile::remove(xmp));
        touch(media, "IMG.xmp", "someone else wrote this sidecar");
        QVERIFY(service.confirm(id));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
        QCOMPARE(finished[0][1].toList().size(), 2);
        QCOMPARE(finished[0][1].toList()[1].toMap()["outcome"].toString(), QString("changed"));
        QVERIFY(QFile::exists(xmp) && !QFile::exists(cr2));
    }
    void planOutputOnlyGainsTheCompanionKeys() {
        QTemporaryDir media, bin;
        const QString jpg = touch(media, "IMG.jpg");
        touch(media, "IMG.xmp");
        FileOperationService service(std::make_unique<FakeTrash>(nativeOf(bin.path())));
        QSignalSpy ready(&service, &FileOperationService::planReady);
        service.plan({jpg}, "grid");
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, kWait);
        const QVariantMap summary = ready[0][1].toMap();
        QStringList keys = summary.keys();
        keys.sort();
        // Today's keys, plus scope and companions and the two the contract's confirmation needs (a count, and the budget notice).
        QCOMPARE(keys, (QStringList{"companions", "companionsIncomplete", "companionsNotListed", "contentCountComplete", "contentFiles", "contentFolders",
                                    "fileCount", "folderCount", "id", "origin", "rejected", "scope", "targets", "trashName"}));
        QCOMPARE(summary["scope"].toString(), QString("file"));
        QCOMPARE(namesIn(summary["targets"].toList()), QStringList{"IMG.jpg"}); // never implicit
        QCOMPARE(namesIn(summary["companions"].toList()), QStringList{"IMG.xmp"});
        QVERIFY(!summary["companions"].toList()[0].toMap()["included"].toBool());
    }
    void anUnknownScopeIsFileOnly() {
        QTemporaryDir media, bin;
        const QString jpg = touch(media, "IMG.jpg");
        touch(media, "IMG.xmp");
        FileOperationService service(std::make_unique<FakeTrash>(nativeOf(bin.path())));
        QSignalSpy ready(&service, &FileOperationService::planReady);
        service.planScoped({jpg}, "grid", "whole-folder");
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, kWait);
        QCOMPARE(ready[0][1].toMap()["scope"].toString(), QString("file"));
        QCOMPARE(namesIn(ready[0][1].toMap()["targets"].toList()), QStringList{"IMG.jpg"});
    }
    void theBudgetReachesTheSummary() {
        QTemporaryDir media, bin;
        const QString jpg = touch(media, "IMG.jpg");
        FakeVolume volume;
        volume.costPerRead = std::chrono::milliseconds(kExpandBudgetMs);
        volume.directories[nativeOf(media.path())] = {"IMG.jpg"};
        InstallVolume install(volume);
        FileOperationService service(std::make_unique<FakeTrash>(nativeOf(bin.path())));
        QSignalSpy ready(&service, &FileOperationService::planReady);
        service.planScoped({jpg}, "grid", "asset");
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, kWait);
        QVERIFY(ready[0][1].toMap()["companionsIncomplete"].toBool());
        QCOMPARE(ready[0][1].toMap()["targets"].toList().size(), 1);
    }
    void companionSummaryIsBounded() {
        QTemporaryDir media, bin;
        FakeVolume volume;
        InstallVolume install(volume);
        const fs::path dir = nativeOf(media.path());
        QStringList files, requested;
        for (int i = 0; i < 400; ++i) {
            files << QString("IMG_%1.CR2").arg(i) << QString("IMG_%1.JPG").arg(i);
            requested << touch(media, QString("IMG_%1.CR2").arg(i));
        }
        volume.directories[dir] = files;
        FileOperationService service(std::make_unique<FakeTrash>(nativeOf(bin.path())));
        QSignalSpy ready(&service, &FileOperationService::planReady);
        service.plan(requested, "grid");
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, kWait);
        const QVariantMap summary = ready[0][1].toMap();
        QCOMPARE(summary["companions"].toList().size(), int(kMaxExcludedListed));
        QCOMPARE(summary["companionsNotListed"].toInt(), 400 - int(kMaxExcludedListed));
        service.planScoped(requested, "grid", "asset");
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, kWait);
        const QVariantMap asset = ready[1][1].toMap();
        QCOMPARE(asset["companions"].toList().size(), int(kMaxExcludedListed));
        QCOMPARE(asset["companionsNotListed"].toInt(), 400 - int(kMaxExcludedListed));
        QVERIFY(asset["companions"].toList()[0].toMap()["included"].toBool());
    }
    void cancelDuringLookupsLeavesNoConfirmablePlan() {
        QTemporaryDir media, bin;
        const QString jpg = touch(media, "IMG.jpg");
        GatedVolume volume;
        InstallVolume install(volume);
        FileOperationService service(std::make_unique<FakeTrash>(nativeOf(bin.path())));
        QSignalSpy ready(&service, &FileOperationService::planReady);
        const quint64 id = service.planScoped({jpg}, "grid", "asset");
        QTRY_VERIFY_WITH_TIMEOUT(volume.entered.load() == 1, kWait);
        service.cancel(id);
        QVERIFY(!service.confirm(id));
        volume.release();
        // A later plan's result is the barrier that proves the cancelled one reported nothing.
        const quint64 next = service.planScoped({jpg}, "grid", "file");
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, kWait);
        QCOMPARE(ready[0][0].toULongLong(), next);
        QVERIFY(!service.confirm(id));
        QVERIFY(QFile::exists(jpg));
    }
    void aScopeSwitchSupersedesThePlanBeingBuilt() {
        QTemporaryDir media, bin;
        const QString jpg = touch(media, "IMG.jpg");
        touch(media, "IMG.xmp");
        GatedVolume volume;
        detail::setLookupBackendForTests(&volume);
        FileOperationService service(std::make_unique<FakeTrash>(nativeOf(bin.path())));
        QSignalSpy ready(&service, &FileOperationService::planReady);
        const quint64 fileScope = service.plan({jpg}, "grid");
        QTRY_VERIFY_WITH_TIMEOUT(volume.entered.load() == 1, kWait);
        const quint64 assetScope = service.planScoped({jpg}, "grid", "asset");
        volume.release();
        detail::setLookupBackendForTests(nullptr); // the second plan's lookups run on the real volume
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, kWait);
        QCOMPARE(ready[0][0].toULongLong(), assetScope);
        QCOMPARE(ready[0][1].toMap()["scope"].toString(), QString("asset"));
        QVERIFY(!service.confirm(fileScope));
    }
};

QTEST_GUILESS_MAIN(AssetGroupTests)
#include "asset_group_tests.moc"
