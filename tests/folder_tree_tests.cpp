#include "glancelot/folder_tree_model.hpp"
#include "glancelot/navigation.hpp"
#include "glancelot/viewer_controller.hpp"
#include <QColorSpace>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include <atomic>

namespace {
namespace fs = std::filesystem;
fs::path nativePath(const QString& path) {
#ifdef Q_OS_WIN
    return fs::path(QDir::toNativeSeparators(path).toStdWString());
#else
    return fs::path(QFile::encodeName(path).constData());
#endif
}
QString displayPath(const fs::path& path) {
#ifdef Q_OS_WIN
    return QString::fromStdWString(path.native());
#else
    return QFile::decodeName(path.native().c_str());
#endif
}
QString make(const QTemporaryDir& dir, const QString& relative) {
    const auto path = dir.filePath(relative);
    QDir().mkpath(path);
    return path;
}
glancelot::NavigationSnapshot snapshot(const QString& root, const QString& directory) {
    glancelot::NavigationSnapshot value;
    value.root = nativePath(root);
    value.directory = nativePath(directory);
    value.current = value.directory / "x.png";
    return value;
}
QStringList names(const FolderTreeModel& model) {
    QStringList rows;
    for (int row = 0; row < model.count(); ++row) rows << model.data(model.index(row), FolderTreeModel::NameRole).toString();
    return rows;
}
int rowNamed(const FolderTreeModel& model, const QString& name) {
    for (int row = 0; row < model.count(); ++row)
        if (model.data(model.index(row), FolderTreeModel::NameRole).toString() == name) return row;
    return -1;
}
int stateOf(const FolderTreeModel& model, int row) { return model.data(model.index(row), FolderTreeModel::StateRole).toInt(); }
QString writePng(const QString& directory, const QString& name) {
    QImage image(20, 12, QImage::Format_RGBA8888);
    image.fill(QColor(40, 90, 200));
    image.setColorSpace(QColorSpace::SRgb);
    const auto path = directory + "/" + name;
    return image.save(path, "PNG") ? path : QString{};
}
bool same(const fs::path& actual, const QString& expected) { return QFileInfo(displayPath(actual)) == QFileInfo(expected); }
}

class FolderTreeTests final : public QObject {
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(QRegularExpression("QProcess: Destroyed while process.*still running.*")); }
    void auditRootRaiseKeepsMarker();
    void auditQueueIsBounded();
    void computerRootsExistBeforePhoto();
    void childFoldersUsesNaturalOrderAndOneLevel();
    void lazyListingExpandsOneLevelAtATime();
    void collapseDiscardsChildrenAndReexpandRelists();
    void emptyAndInaccessibleFoldersGetStates();
    void revealExpandsOnlyAncestorsOfCurrentFolder();
    void revealGivesUpOnMissingOrUnlistedTargets();
    void rootChangeDiscardsStaleLoads_data();
    void rootChangeDiscardsStaleLoads();
    void collapseWhileLoadingCancelsTheListing();
    void oversizedFoldersAreTruncated();
    void destroyingTheModelCancelsAndDoesNotHang();
    void wedgedListingDoesNotFreezeTeardown();
    void listingTruncationFlagIsReported();
    void rootRaiseAvailabilityFollowsParent();
    void windowsPathsCompareCaseInsensitively();
    void openFolderOpensFirstImageInSubtree();
    void openFolderWithoutImagesReportsAndKeepsSelection();
    void newestOpenFolderWins();
    void raiseRootWidensRootExplicitly();
    void treeSettingsPersistAndDoNotDisplaceBindings();
};

void FolderTreeTests::childFoldersUsesNaturalOrderAndOneLevel() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    for (const char* name : {"10", "2", "a/deep/deeper", "B", "1"}) make(dir, name);
    QVERIFY(!writePng(dir.path(), "file.png").isEmpty());
    const auto listing = glancelot::childFolders(nativePath(dir.path()));
    QVERIFY(listing.accessible); QVERIFY(!listing.cancelled);
    QStringList got;
    for (const auto& folder : listing.folders) got << displayPath(folder.filename());
    QCOMPARE(got, QStringList({"1", "2", "10", "a", "B"})); // numeric runs, ASCII case folded, files ignored, one level only
    std::stop_source stop; stop.request_stop();
    const auto cancelled = glancelot::childFolders(nativePath(dir.path()), stop.get_token());
    QVERIFY(cancelled.cancelled); QVERIFY(cancelled.folders.empty());
    const auto missing = glancelot::childFolders(nativePath(dir.filePath("nope")));
    QVERIFY(!missing.accessible); QVERIFY(missing.folders.empty());
    const auto notADirectory = glancelot::childFolders(nativePath(dir.filePath("file.png")));
    QVERIFY(!notADirectory.accessible);
    const auto empty = glancelot::childFolders(nativePath(make(dir, "1")));
    QVERIFY(empty.accessible); QVERIFY(empty.folders.empty());
}

void FolderTreeTests::lazyListingExpandsOneLevelAtATime() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    for (const char* name : {"a/a1", "a/a2", "b", "10", "2"}) make(dir, name);
    FolderTreeModel model;
    QSignalSpy loaded(&model, &FolderTreeModel::loadFinished);
    model.sync(snapshot(dir.path(), dir.path()));
    QCOMPARE(model.count(), 1);                      // only the root row exists until its listing returns
    QVERIFY(model.data(model.index(0), FolderTreeModel::LoadingRole).toBool());
    QTRY_COMPARE_WITH_TIMEOUT(model.count(), 5, 10000);
    QCOMPARE(names(model).mid(1), QStringList({"2", "10", "a", "b"}));
    QCOMPARE(model.pendingLoads(), 0);
    QCOMPARE(model.currentRow(), 0);                 // the root is the current folder
    QVERIFY(!model.data(model.index(rowNamed(model, "a")), FolderTreeModel::ExpandedRole).toBool());
    QCOMPARE(stateOf(model, rowNamed(model, "a")), int(FolderTreeModel::Unknown)); // grandchildren were never read
    model.expand(rowNamed(model, "a"));
    QTRY_COMPARE_WITH_TIMEOUT(model.count(), 7, 10000);
    const int a1 = rowNamed(model, "a1");
    QVERIFY(a1 > rowNamed(model, "a"));
    QCOMPARE(model.data(model.index(a1), FolderTreeModel::DepthRole).toInt(), 2);
    QCOMPARE(model.parentRow(a1), rowNamed(model, "a"));
    QCOMPARE(model.parentRow(0), -1);
    QCOMPARE(model.pathAt(a1), QDir::toNativeSeparators(dir.filePath("a/a1")));
}

void FolderTreeTests::collapseDiscardsChildrenAndReexpandRelists() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    for (const char* name : {"a/a1", "a/a2", "b"}) make(dir, name);
    FolderTreeModel model;
    model.sync(snapshot(dir.path(), dir.path()));
    QTRY_COMPARE_WITH_TIMEOUT(model.count(), 3, 10000);
    model.expand(rowNamed(model, "a"));
    QTRY_COMPARE_WITH_TIMEOUT(model.count(), 5, 10000);
    model.collapse(rowNamed(model, "a"));
    QCOMPARE(model.count(), 3);
    make(dir, "a/a0"); // changes made while collapsed are visible on the next expansion; nothing stale is retained
    model.toggle(rowNamed(model, "a"));
    QTRY_COMPARE_WITH_TIMEOUT(model.count(), 6, 10000);
    QCOMPARE(names(model).mid(2, 3), QStringList({"a0", "a1", "a2"}));
    // Collapsing an ancestor removes the whole expanded subtree, including nested expansions.
    model.expand(rowNamed(model, "a1")); // empty: state only
    QTRY_COMPARE_WITH_TIMEOUT(stateOf(model, rowNamed(model, "a1")), int(FolderTreeModel::Empty), 10000);
    model.collapse(0);
    QCOMPARE(model.count(), 1);
    model.collapse(99); model.expand(-1); model.toggle(1234); // out-of-range calls are ignored
}

void FolderTreeTests::emptyAndInaccessibleFoldersGetStates() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    for (const char* name : {"empty", "locked", "open/child"}) make(dir, name);
    FolderTreeModel model;
    model.setLister([](const fs::path& path, std::stop_token cancel) {
        glancelot::FolderListing listing = glancelot::childFolders(path, cancel);
        if (path.filename() == "locked") listing.accessible = false; // stands in for a permission denial
        return listing;
    });
    model.sync(snapshot(dir.path(), dir.path()));
    QTRY_COMPARE_WITH_TIMEOUT(model.count(), 4, 10000);
    for (const char* name : {"empty", "locked", "open"}) model.expand(rowNamed(model, name));
    QTRY_COMPARE_WITH_TIMEOUT(model.pendingLoads(), 0, 10000);
    QCOMPARE(stateOf(model, rowNamed(model, "empty")), int(FolderTreeModel::Empty));
    QCOMPARE(stateOf(model, rowNamed(model, "locked")), int(FolderTreeModel::Inaccessible));
    QCOMPARE(stateOf(model, rowNamed(model, "open")), int(FolderTreeModel::HasChildren));
    QVERIFY(!model.data(model.index(rowNamed(model, "empty")), FolderTreeModel::ExpandedRole).toBool());
    QVERIFY(!model.data(model.index(rowNamed(model, "empty")), FolderTreeModel::LoadingRole).toBool());
    QCOMPARE(rowNamed(model, "child") > 0, true);
    QCOMPARE(model.count(), 5);                      // no children were invented for empty/locked folders
}

void FolderTreeTests::revealExpandsOnlyAncestorsOfCurrentFolder() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    for (const char* name : {"x/y/z", "x/w", "q/r"}) make(dir, name);
    FolderTreeModel model;
    QSignalSpy rows(&model, &FolderTreeModel::currentRowChanged);
    model.sync(snapshot(dir.path(), dir.filePath("x/y/z")));
    QTRY_VERIFY_WITH_TIMEOUT(model.currentRow() >= 0, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(model.pendingLoads(), 0, 10000);
    QCOMPARE(model.pathAt(model.currentRow()), QDir::toNativeSeparators(dir.filePath("x/y/z")));
    QVERIFY(model.data(model.index(model.currentRow()), FolderTreeModel::IsCurrentRole).toBool());
    QCOMPARE(names(model), QStringList({QFileInfo(dir.path()).fileName(), "q", "x", "w", "y", "z"}));
    QCOMPARE(stateOf(model, rowNamed(model, "q")), int(FolderTreeModel::Unknown)); // siblings off the path stay unlisted
    // Stepping to another folder moves the marker without relisting what is already visible.
    model.sync(snapshot(dir.path(), dir.filePath("x/w")));
    QCOMPARE(model.pathAt(model.currentRow()), QDir::toNativeSeparators(dir.filePath("x/w")));
    QCOMPARE(model.pendingLoads(), 0);
    // A manual collapse is respected until the folder changes again.
    model.collapse(rowNamed(model, "x"));
    QCOMPARE(model.currentRow(), -1);
    model.sync(snapshot(dir.path(), dir.filePath("x/w")));
    QCOMPARE(model.count(), 3);
    model.sync(snapshot(dir.path(), dir.filePath("q/r")));
    QTRY_VERIFY_WITH_TIMEOUT(model.currentRow() >= 0, 10000);
    QVERIFY(rows.size() >= 2);
}

void FolderTreeTests::revealGivesUpOnMissingOrUnlistedTargets() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    make(dir, "real/inner");
    FolderTreeModel model;
    model.sync(snapshot(dir.path(), dir.filePath("ghost/deeper")));
    QTRY_COMPARE_WITH_TIMEOUT(model.count(), 2, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(model.pendingLoads(), 0, 10000);
    QCOMPARE(model.currentRow(), -1);                // no loop, no drive-wide search
    // A directory outside the root is ignored rather than followed.
    QTemporaryDir other; QVERIFY(other.isValid());
    model.sync(snapshot(dir.path(), other.path()));
    QCOMPARE(model.currentRow(), -1); QCOMPARE(model.pendingLoads(), 0);
    FolderTreeModel empty;
    empty.sync(glancelot::NavigationSnapshot{});
    QCOMPARE(empty.count(), 0); QCOMPARE(empty.currentRow(), -1); QVERIFY(!empty.canRaiseRoot());
}

void FolderTreeTests::rootChangeDiscardsStaleLoads_data() {
    QTest::addColumn<bool>("cooperative");
    QTest::newRow("lister honors cancellation") << true;
    QTest::newRow("lister ignores cancellation") << false;
}

void FolderTreeTests::rootChangeDiscardsStaleLoads() {
    QFETCH(bool, cooperative);
    QTemporaryDir dir; QVERIFY(dir.isValid());
    make(dir, "sub/inside"); make(dir, "sibling");
    std::atomic<bool> release{false}, sawStop{false};
    FolderTreeModel model;
    model.setLister([&](const fs::path& path, std::stop_token cancel) {
        const bool slow = path.filename() == "sub";
        while (slow && !release) {
            if (cooperative && cancel.stop_requested()) { sawStop = true; glancelot::FolderListing out; out.cancelled = true; return out; }
            QThread::msleep(5);
        }
        return glancelot::childFolders(path, cancel);
    });
    model.sync(snapshot(dir.filePath("sub"), dir.filePath("sub")));
    QTRY_VERIFY_WITH_TIMEOUT(model.pendingLoads() == 1, 5000);
    QCOMPARE(model.count(), 1);
    QVERIFY(model.canRaiseRoot());
    QSignalSpy roots(&model, &FolderTreeModel::rootChanged);
    model.sync(snapshot(dir.path(), dir.path()));    // the user raised the root: the old epoch must not leak in
    QCOMPARE(roots.size(), 1);
    if (cooperative) {
        // Cancellation frees the worker, so the new root lists even though the old listing never finished.
        QTRY_COMPARE_WITH_TIMEOUT(model.count(), 3, 10000); // new root + sibling + sub
        QTRY_VERIFY_WITH_TIMEOUT(sawStop.load(), 5000);
    }
    release = true;
    QTRY_COMPARE_WITH_TIMEOUT(model.count(), 3, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(model.pendingLoads(), 0, 10000);
    QCOMPARE(names(model), QStringList({QFileInfo(dir.path()).fileName(), "sibling", "sub"}));
    QVERIFY(rowNamed(model, "inside") < 0);          // the stale listing of the old root was discarded
}

void FolderTreeTests::collapseWhileLoadingCancelsTheListing() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    make(dir, "slow/child"); make(dir, "fast");
    std::atomic<bool> release{false}, sawStop{false};
    FolderTreeModel model;
    model.setLister([&](const fs::path& path, std::stop_token cancel) {
        while (path.filename() == "slow" && !release) {
            if (cancel.stop_requested()) { sawStop = true; glancelot::FolderListing out; out.cancelled = true; return out; }
            QThread::msleep(5);
        }
        return glancelot::childFolders(path, cancel);
    });
    model.sync(snapshot(dir.path(), dir.path()));
    QTRY_COMPARE_WITH_TIMEOUT(model.count(), 3, 10000);
    const int slow = rowNamed(model, "slow");
    model.expand(slow);
    QVERIFY(model.data(model.index(slow), FolderTreeModel::LoadingRole).toBool());
    QTRY_VERIFY_WITH_TIMEOUT(model.pendingLoads() == 1, 5000);
    model.toggle(slow);                              // collapse while loading
    QVERIFY(!model.data(model.index(slow), FolderTreeModel::LoadingRole).toBool());
    QVERIFY(!model.data(model.index(slow), FolderTreeModel::ExpandedRole).toBool());
    QTRY_VERIFY_WITH_TIMEOUT(sawStop.load(), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(model.pendingLoads(), 0, 10000);
    QCOMPARE(model.count(), 3);                      // the cancelled listing inserted nothing
    // Re-expanding afterwards starts a fresh load whose result is applied.
    release = true;
    model.expand(slow);
    QTRY_COMPARE_WITH_TIMEOUT(model.count(), 4, 10000);
}

void FolderTreeTests::oversizedFoldersAreTruncated() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    FolderTreeModel model;
    model.setLister([&](const fs::path& path, std::stop_token) {
        glancelot::FolderListing out;
        for (int i = 0; i < FolderTreeModel::kMaxChildren + 500; ++i) out.folders.push_back(path / ("folder-" + std::to_string(100000 + i)));
        return out;
    });
    model.sync(snapshot(dir.path(), dir.path()));
    QTRY_COMPARE_WITH_TIMEOUT(model.count(), 1 + FolderTreeModel::kMaxChildren, 10000);
    QCOMPARE(stateOf(model, 0), int(FolderTreeModel::Truncated));
    QVERIFY(model.count() <= FolderTreeModel::kMaxRows);
}

void FolderTreeTests::destroyingTheModelCancelsAndDoesNotHang() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    std::atomic<bool> sawStop{false}, started{false};
    QElapsedTimer timer; timer.start();
    {
        FolderTreeModel model;
        model.setLister([&](const fs::path&, std::stop_token cancel) {
            started = true;
            while (!cancel.stop_requested()) QThread::msleep(5);
            sawStop = true;
            glancelot::FolderListing out; out.cancelled = true; return out;
        });
        model.sync(snapshot(dir.path(), dir.path()));
        QTRY_VERIFY_WITH_TIMEOUT(started.load(), 5000);
    }
    QVERIFY(sawStop.load());
    QVERIFY2(timer.elapsed() < 10000, "destruction waited for an unbounded listing");
}

void FolderTreeTests::wedgedListingDoesNotFreezeTeardown() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    std::atomic<bool> release{false}, started{false};
    QElapsedTimer timer;
    {
        FolderTreeModel model;
        model.setLister([&](const fs::path&, std::stop_token) { // ignores cancellation, like a dead share
            started = true;
            while (!release) QThread::msleep(5);
            return glancelot::FolderListing{};
        });
        model.sync(snapshot(dir.path(), dir.path()));
        QTRY_VERIFY_WITH_TIMEOUT(started.load(), 5000);
        timer.start();
    }
    const auto elapsed = timer.elapsed();
    release = true;                                  // let the abandoned worker finish before the directory goes away
    QVERIFY2(elapsed < 3500, qPrintable(QString("teardown blocked the GUI thread for %1 ms").arg(elapsed)));
    QTest::qWait(100);
}

void FolderTreeTests::listingTruncationFlagIsReported() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    FolderTreeModel model;
    model.setLister([&](const fs::path& path, std::stop_token) {
        glancelot::FolderListing out;
        out.folders = {path / "one", path / "two"};
        out.truncated = true;                        // the worker stopped at its own limit
        return out;
    });
    model.sync(snapshot(dir.path(), dir.path()));
    QTRY_COMPARE_WITH_TIMEOUT(model.count(), 3, 10000);
    QCOMPARE(stateOf(model, 0), int(FolderTreeModel::Truncated));
}

void FolderTreeTests::rootRaiseAvailabilityFollowsParent() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    make(dir, "a/b");
    FolderTreeModel model;
    model.setLister([](const fs::path& path, std::stop_token cancel) { return glancelot::childFolders(path, cancel); });
    model.sync(snapshot(dir.filePath("a"), dir.filePath("a")));
    QVERIFY(model.canRaiseRoot());
    QCOMPARE(model.rootPath(), QDir::toNativeSeparators(dir.filePath("a")));
    QTRY_COMPARE_WITH_TIMEOUT(model.count(), 2, 10000);
    const auto filesystemRoot = QFileInfo(dir.path()).absoluteDir().rootPath();
    model.setLister([](const fs::path&, std::stop_token) { return glancelot::FolderListing{}; });
    model.sync(snapshot(filesystemRoot, filesystemRoot));
    QVERIFY(!model.canRaiseRoot());                  // never offers to go above the filesystem root
    QTRY_COMPARE_WITH_TIMEOUT(model.pendingLoads(), 0, 10000);
}

void FolderTreeTests::windowsPathsCompareCaseInsensitively() {
#ifdef Q_OS_WIN
    QTemporaryDir dir; QVERIFY(dir.isValid());
    make(dir, "Photos/Trip");
    FolderTreeModel model;
    model.sync(snapshot(dir.path(), dir.filePath("photos/trip").toUpper()));
    QTRY_VERIFY_WITH_TIMEOUT(model.currentRow() >= 0, 10000);
    QCOMPARE(names(model).last(), QString("Trip"));
#else
    QSKIP("Case-insensitive path keys apply to Windows only");
#endif
}

void FolderTreeTests::openFolderOpensFirstImageInSubtree() {
    QTemporaryDir files, config; QVERIFY(files.isValid() && config.isValid());
    qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
    const auto start = writePng(files.path(), "p1.png"); // the opened file defines the browsing root
    const auto deep = writePng(make(files, "two/sub/deeper"), "p2.png");
    writePng(make(files, "two/sub"), "p3.png");
    make(files, "two/empty");
    ViewerController viewer;
    viewer.open(start);
    QTRY_VERIFY_WITH_TIMEOUT(!viewer.snapshot().current.empty() && !viewer.loading(), 15000);
    // "two" has no images of its own: the first image in traversal order is in two/sub.
    viewer.openFolder(files.filePath("two"));
    QTRY_VERIFY_WITH_TIMEOUT(same(viewer.snapshot().current, files.filePath("two/sub/p3.png")), 15000);
    QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading(), 15000);
    viewer.openFolder(files.filePath("two/sub/deeper"));
    QTRY_VERIFY_WITH_TIMEOUT(same(viewer.snapshot().current, deep), 15000);
    QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading(), 15000);
    QVERIFY(viewer.imageWidth() == 20 && viewer.imageHeight() == 12);
    qunsetenv("GLANCELOT_CONFIG_DIR");
}

void FolderTreeTests::openFolderWithoutImagesReportsAndKeepsSelection() {
    QTemporaryDir files, config; QVERIFY(files.isValid() && config.isValid());
    qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
    const auto start = writePng(files.path(), "p1.png");
    make(files, "bare/also-bare");
    for (int i = 0; i < 520; ++i) make(files, QString("wide/w%1").arg(1000 + i)); // before open(): the watcher would reload
    ViewerController viewer;
    viewer.open(start);
    QTRY_VERIFY_WITH_TIMEOUT(!viewer.snapshot().current.empty() && !viewer.loading(), 15000);
    viewer.openFolder(files.filePath("bare"));
    QTRY_VERIFY_WITH_TIMEOUT(viewer.status().contains("No supported images"), 15000);
    QVERIFY(same(viewer.snapshot().current, start));
    viewer.openFolder(files.filePath("does-not-exist"));
    QTRY_VERIFY_WITH_TIMEOUT(viewer.status().contains("cannot be read"), 15000);
    QVERIFY(same(viewer.snapshot().current, start));
    // A hostile or huge empty tree ends in a bounded search with an honest message, not a hang.
    viewer.openFolder(files.filePath("wide"));
    QTRY_VERIFY2_WITH_TIMEOUT(viewer.status().contains("first 500 folders"), qPrintable(viewer.status()), 30000);
    QVERIFY(same(viewer.snapshot().current, start));
    viewer.openFolder(QString{});                    // ignored
    ViewerController fresh;
    fresh.openFolder(files.path());                  // folder-first browsing creates a navigation root
    QTRY_VERIFY_WITH_TIMEOUT(same(fresh.snapshot().current,start) && fresh.imageReady(),15000);
    QTemporaryDir elsewhere; QVERIFY(elsewhere.isValid());
    const auto next=writePng(elsewhere.path(), "other.png");
    fresh.openFolder(elsewhere.path());
    QTRY_VERIFY_WITH_TIMEOUT(same(fresh.snapshot().current,next) && fresh.imageReady(),15000);
    QVERIFY(same(fresh.snapshot().root,elsewhere.path()));
    fresh.openFolder(files.path());
    QTRY_VERIFY_WITH_TIMEOUT(same(fresh.snapshot().current,start) && fresh.imageReady(),15000);
    QVERIFY(same(fresh.snapshot().root,files.path()));
    qunsetenv("GLANCELOT_CONFIG_DIR");
}

void FolderTreeTests::newestOpenFolderWins() {
    QTemporaryDir files, config; QVERIFY(files.isValid() && config.isValid());
    qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
    const auto start = writePng(files.path(), "p1.png");
    writePng(make(files, "alpha"), "a.png");
    const auto beta = writePng(make(files, "beta"), "b.png");
    writePng(make(files, "gamma"), "g.png");
    const auto delta = writePng(make(files, "delta"), "d.png");
    ViewerController viewer;
    viewer.open(start);
    QTRY_VERIFY_WITH_TIMEOUT(!viewer.snapshot().current.empty() && !viewer.loading(), 15000);
    for (const char* folder : {"alpha", "beta", "gamma", "delta"}) viewer.openFolder(files.filePath(folder));
    QTRY_VERIFY_WITH_TIMEOUT(same(viewer.snapshot().current, delta), 15000);
    QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading(), 15000);
    QTest::qWait(300);                               // a late older job must not replace the newest selection
    QVERIFY(same(viewer.snapshot().current, delta));
    Q_UNUSED(beta);
    qunsetenv("GLANCELOT_CONFIG_DIR");
}

void FolderTreeTests::raiseRootWidensRootExplicitly() {
    QTemporaryDir files, config; QVERIFY(files.isValid() && config.isValid());
    qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
    const auto start = writePng(make(files, "one/inner"), "p1.png");
    writePng(make(files, "two"), "p2.png");
    ViewerController viewer;
    viewer.open(start);
    QTRY_VERIFY_WITH_TIMEOUT(!viewer.snapshot().current.empty() && !viewer.loading(), 15000);
    QVERIFY(same(viewer.snapshot().root, files.filePath("one/inner"))); // opening a file never widens the root
    viewer.raiseRoot();
    QTRY_VERIFY_WITH_TIMEOUT(same(viewer.snapshot().root, files.filePath("one")), 15000);
    QVERIFY(same(viewer.snapshot().directory, files.filePath("one/inner"))); // the viewed folder is unchanged
    viewer.raiseRoot();
    QTRY_VERIFY_WITH_TIMEOUT(same(viewer.snapshot().root, files.path()), 15000);
    QTest::qWait(200);
    QVERIFY(same(viewer.snapshot().current, start));
    qunsetenv("GLANCELOT_CONFIG_DIR");
}

void FolderTreeTests::treeSettingsPersistAndDoNotDisplaceBindings() {
    QTemporaryDir config; QVERIFY(config.isValid());
    qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
    {
        ViewerController viewer;
        QCOMPARE(viewer.bindingFor("tree"), QString("Ctrl+B"));
        QVERIFY(!viewer.treeVisible()); QVERIFY(!viewer.treePinned());
        QSignalSpy shown(&viewer, &ViewerController::treeShown);
        viewer.trigger("tree");
        QVERIFY(viewer.treeVisible()); QCOMPARE(shown.size(), 1);
        viewer.setTreePinned(true);
        viewer.trigger("tree"); QVERIFY(!viewer.treeVisible()); QCOMPARE(shown.size(), 1);
        viewer.trigger("tree");
        QVERIFY(viewer.setBinding("fit", "X"));
    }
    ViewerController restored;
    QVERIFY(restored.treeVisible()); QVERIFY(restored.treePinned());
    QCOMPARE(restored.bindingFor("fit"), QString("X"));
    // A user binding that already uses the new command's default key wins; the new command starts unassigned.
    QFile file(QDir(config.path()).filePath("settings.json"));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(R"({"schema":1,"bindings":{"rotate":"Ctrl+B"},"tree":true})");
    file.close();
    ViewerController collided;
    QCOMPARE(collided.bindingFor("rotate"), QString("Ctrl+B"));
    QCOMPARE(collided.bindingFor("tree"), QString());
    QCOMPARE(collided.bindingFor("next"), QString("Right"));
    QVERIFY(collided.treeVisible());
    qunsetenv("GLANCELOT_CONFIG_DIR");
}

void FolderTreeTests::auditRootRaiseKeepsMarker() {
 QTemporaryDir d; make(d,"a/b"); FolderTreeModel m;
 m.sync(snapshot(d.filePath("a/b"), d.filePath("a/b")));
 QTRY_COMPARE(m.pendingLoads(),0); QVERIFY(m.currentRow()>=0);
 m.sync(snapshot(d.filePath("a"),d.filePath("a/b")));
 QTRY_COMPARE(m.pendingLoads(),0); QVERIFY(m.currentRow()>=0);
 m.sync(snapshot(d.path(),d.filePath("a/b")));
 QTRY_COMPARE(m.pendingLoads(),0);
 QVERIFY2(m.currentRow()>=0,"root raise lost current folder marker");
}
void FolderTreeTests::auditQueueIsBounded() {
 QTemporaryDir d; std::atomic<bool> release{false}; FolderTreeModel m;
 m.setLister([&](const fs::path&,std::stop_token){while(!release) QThread::msleep(1); return glancelot::FolderListing{};});
 m.sync(snapshot(d.path(),d.path()));
 for(int i=0;i<1000;++i){m.collapse(0); m.expand(0);}
 const int queued=m.pendingLoads(); release=true;
 QTRY_COMPARE(m.pendingLoads(),0);
 QVERIFY2(queued<=256,qPrintable(QString("unbounded accepted jobs=%1").arg(queued)));
}
QTEST_MAIN(FolderTreeTests)
#include "folder_tree_tests.moc"



void FolderTreeTests::computerRootsExistBeforePhoto() {
    FolderTreeModel model;
    model.browseComputer();
    QVERIFY(model.count()>0);
    const QString root=model.pathAt(0); QVERIFY(!root.isEmpty());
    model.sync({}); QCOMPARE(model.pathAt(0),root);
    QVERIFY(!model.canRaiseRoot());
    QVERIFY(model.pendingLoads()<=FolderTreeModel::kMaxConcurrentLoads);
}
