#include "asset_group_internal.hpp"
#include "glancelot/asset_group.hpp"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <map>
#include <set>

Q_DECLARE_METATYPE(glancelot::assets::Operation)

// ASSET-GROUP acceptance tests (docs/contracts/ASSET-GROUP.md). Generated names and media only: pure grouping runs on
// name lists, the lookup tests on a fake volume with a virtual clock.
namespace fs = std::filesystem;
using namespace glancelot;
using namespace glancelot::assets;
namespace {
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
fs::path nativeOf(const QString& path) { return detail::nativePath(path); }
QString touch(const QTemporaryDir& dir, const QString& fileName, const QByteArray& bytes = "generated") {
    QFile file(dir.filePath(fileName));
    if (file.open(QIODevice::WriteOnly)) file.write(bytes);
    return dir.filePath(fileName);
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
        const bool separate = QDir(dir.path()).entryList(QDir::Files).size() == 2;
        const auto e = expand({nativeOf(dir.filePath("a.JPG"))}, Scope::Asset, Operation::Trash, nullptr);
        if (separate) {
            QCOMPARE(included(e), QStringList{"a.JPG"});
            QCOMPARE(excluded(e).at("a.jpg"), Exclusion::Collision);
        } else {
            QCOMPARE(e.included.size(), std::size_t(1));
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
        const QStringList files{"b.jpg", "b.xmp", "a.CR2", "a.mov", "a.jpg", "a.xmp"};
        const auto snapshot = groups(files);
        QCOMPARE(snapshot.groups.size(), std::size_t(2));
        QCOMPARE(QString::fromStdString(snapshot.groups[0].stemKey), QString("a"));
        QCOMPARE(members(snapshot.groups[0]), (QStringList{"a.CR2", "a.jpg", "a.mov", "a.xmp"}));
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
};

QTEST_GUILESS_MAIN(AssetGroupTests)
#include "asset_group_tests.moc"
