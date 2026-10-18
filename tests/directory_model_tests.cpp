#include "glancelot/directory_entry_model.hpp"
#include "glancelot/navigation.hpp"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

// Generated, disposable folders only. Listing is asynchronous, so every assertion waits for `loading` to clear.
namespace {
void touch(const QString& path, int bytes = 1) { QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(QByteArray(bytes, 'x')); }
QStringList names(const DirectoryEntryModel& model) {
    QStringList out;
    for (int i = 0; i < model.count(); ++i) out << model.data(model.index(i), DirectoryEntryModel::NameRole).toString();
    return out;
}
bool settle(DirectoryEntryModel& model, int timeout = 10000) {
    QElapsedTimer t; t.start();
    while (model.loading() && t.elapsed() < timeout) QTest::qWait(5);
    return !model.loading();
}
}

class DirectoryModelTests : public QObject {
    Q_OBJECT
private slots:
    void emptyByDefault() {
        DirectoryEntryModel model;
        QCOMPARE(model.count(), 0); QCOMPARE(model.selectedCount(), 0); QCOMPARE(model.cursorRow(), -1);
        QVERIFY(model.thumbnailSource(0, 128).isEmpty());
        QVERIFY(!model.loading());
    }
    void shuffleSeedMatchesTraversalOrder() {
        QTemporaryDir dir; for (int i = 0; i < 12; ++i) touch(dir.filePath(QString("p%1.png").arg(i)));
        touch(dir.filePath("notes.txt"));
        glancelot::SortSpec spec; spec.key = glancelot::SortKey::Shuffle; spec.seed = 987654321;
        QStringList expected;
        for (const auto& path : glancelot::sortedImageFiles(dir.path().toStdWString(), spec))
            expected << QString::fromStdWString(path.filename().wstring());
        DirectoryEntryModel model; model.setActive(true); model.setSort("shuffle", false, spec.seed); model.setDirectory(dir.path());
        QVERIFY(settle(model));
        QStringList images;
        for (int i = 0; i < model.count(); ++i) if (model.kindAt(i) == "image") images << model.data(model.index(i), DirectoryEntryModel::NameRole).toString();
        QCOMPARE(images, expected);
    }
    void inactiveByDefaultDoesNoWork() {
        QTemporaryDir dir; touch(dir.filePath("a.png"));
        DirectoryEntryModel model;
        model.setDirectory(dir.path());
        QCOMPARE(model.directory(), dir.path());
        QVERIFY(!model.loading()); QTest::qWait(50); QCOMPARE(model.count(), 0);
        model.setActive(true); QVERIFY(model.loading());
        QVERIFY(settle(model)); QCOMPARE(model.count(), 1);
    }
    void foldersFirstMixedKindsAndRoles() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("b10"); QDir(dir.path()).mkpath("b2");
        touch(dir.filePath("img.png"), 40); touch(dir.filePath("notes.txt"), 7); touch(dir.filePath(".hidden.png"));
        DirectoryEntryModel model; model.setActive(true); model.setDirectory(dir.path());
        QVERIFY(settle(model));
        QCOMPARE(names(model), (QStringList{"b2", "b10", "img.png", "notes.txt"}));
        QCOMPARE(model.kindAt(0), QString("folder")); QCOMPARE(model.kindAt(2), QString("image")); QCOMPARE(model.kindAt(3), QString("file"));
        QCOMPARE(model.kindAt(9), QString());
        QCOMPARE(model.data(model.index(3), DirectoryEntryModel::SizeRole).toLongLong(), 7);
        QVERIFY(model.data(model.index(3), DirectoryEntryModel::ModifiedRole).toDateTime().isValid());
        QVERIFY(!model.data(model.index(0), DirectoryEntryModel::LinkRole).toBool());
        QCOMPARE(model.rowOf(dir.filePath("notes.txt")), 3);
#ifdef Q_OS_WIN
        QCOMPARE(model.rowOf(QDir::toNativeSeparators(dir.filePath("NOTES.TXT"))), 3); // case-insensitive key
#endif
        QCOMPARE(model.rowOf(dir.filePath("missing.txt")), -1);
        QVERIFY(model.accessible()); QVERIFY(!model.truncated());
        model.setIncludeHidden(true); QVERIFY(settle(model));
        QCOMPARE(model.count(), 5);
        QVERIFY(model.data(model.index(model.rowOf(dir.filePath(".hidden.png"))), DirectoryEntryModel::HiddenRole).toBool());
    }
    void thumbnailSourceOnlyForImages() {
        QTemporaryDir dir; QDir(dir.path()).mkpath("folder");
        touch(dir.filePath("a.png")); touch(dir.filePath("b.txt")); touch(dir.filePath("c.exe"));
        DirectoryEntryModel model; model.setActive(true); model.setDirectory(dir.path()); QVERIFY(settle(model));
        const int image = model.rowOf(dir.filePath("a.png"));
        const QString source = model.thumbnailSource(image, 256);
        QVERIFY(source.startsWith("image://thumb/1/256/"));
        for (const char* name : {"folder", "b.txt", "c.exe"}) QVERIFY2(model.thumbnailSource(model.rowOf(dir.filePath(name)), 256).isEmpty(), name);
        QSignalSpy epoch(&model, &DirectoryEntryModel::epochChanged);
        model.invalidate(); QCOMPARE(epoch.count(), 1);
        QVERIFY(model.thumbnailSource(image, 256).startsWith("image://thumb/2/256/"));
        QVERIFY(model.thumbnailSource(-1, 256).isEmpty());
    }
    void newestGenerationWins() {
        QTemporaryDir a, b;
        for (int i = 0; i < 3; ++i) touch(a.filePath(QString("a%1.png").arg(i)));
        for (int i = 0; i < 5; ++i) touch(b.filePath(QString("b%1.txt").arg(i)));
        DirectoryEntryModel model; model.setActive(true);
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        model.setDirectory(a.path()); model.setDirectory(b.path());
        QVERIFY(settle(model)); QTest::qWait(150); // any stale A result has had time to arrive
        QCOMPARE(model.directory(), b.path());
        QCOMPARE(model.count(), 5);
        QVERIFY(names(model).first().startsWith("b"));
        // Many rapid requests stay bounded and still land on the last one.
        for (int i = 0; i < 50; ++i) model.setDirectory(i % 2 ? a.path() : b.path());
        model.setDirectory(a.path());
        QVERIFY(settle(model)); QTest::qWait(150);
        QCOMPARE(model.count(), 3); QCOMPARE(model.directory(), a.path());
    }
    void staleResultAfterDeactivationIsDropped() {
        QTemporaryDir dir; for (int i = 0; i < 200; ++i) touch(dir.filePath(QString("f%1.png").arg(i)));
        DirectoryEntryModel model; model.setActive(true); model.setDirectory(dir.path());
        model.setActive(false);
        QVERIFY(!model.loading());
        QTest::qWait(300);
        QCOMPARE(model.count(), 0);
    }
    void selectionModesAndCount() {
        QTemporaryDir dir; for (const char* n : {"a.png", "b.png", "c.txt", "d.png", "e.txt"}) touch(dir.filePath(n));
        DirectoryEntryModel model; model.setActive(true); model.setDirectory(dir.path()); QVERIFY(settle(model));
        QSignalSpy changed(&model, &DirectoryEntryModel::selectionChanged);
        model.select(1); QCOMPARE(model.selectedCount(), 1); QCOMPARE(model.cursorRow(), 1);
        QVERIFY(model.data(model.index(1), DirectoryEntryModel::SelectedRole).toBool());
        QVERIFY(model.data(model.index(1), DirectoryEntryModel::CursorRole).toBool());
        model.select(3, DirectoryEntryModel::Range);  // anchor b .. d
        QCOMPARE(model.selectedPaths(), (QStringList{dir.filePath("b.png"), dir.filePath("c.txt"), dir.filePath("d.png")}));
        QCOMPARE(model.cursorRow(), 3);
        model.select(0, DirectoryEntryModel::Range);  // range replaces: a .. b, anchor stays b
        QCOMPARE(model.selectedCount(), 2); QCOMPARE(model.cursorRow(), 0);
        model.select(4, DirectoryEntryModel::Toggle); QCOMPARE(model.selectedCount(), 3);
        model.select(4, DirectoryEntryModel::Toggle); QCOMPARE(model.selectedCount(), 2);
        QVERIFY(!model.data(model.index(4), DirectoryEntryModel::SelectedRole).toBool());
        QCOMPARE(model.cursorRow(), 4); // toggling moves the cursor even when it deselects
        model.select(2, DirectoryEntryModel::Replace); model.select(4, DirectoryEntryModel::Toggle);
        model.select(0, DirectoryEntryModel::ExtendRange); // anchor e: add a..e to {c,e}
        QCOMPARE(model.selectedCount(), 5);
        model.clearSelection(); QCOMPARE(model.selectedCount(), 0); QCOMPARE(model.cursorRow(), 0); // cursor survives clear
        model.selectAll(); QCOMPARE(model.selectedCount(), 5);
        model.select(-1); model.select(99); QCOMPARE(model.selectedCount(), 5); // out-of-range rows are ignored
        QVERIFY(changed.count() >= 10);
    }
    void selectionSurvivesRelistSortAndDropsRemovedKeys() {
        QTemporaryDir dir;
        touch(dir.filePath("a.png"), 50); touch(dir.filePath("b.png"), 10); touch(dir.filePath("c.txt"), 30); touch(dir.filePath("d.png"), 20);
        DirectoryEntryModel model; model.setActive(true); model.setDirectory(dir.path()); QVERIFY(settle(model));
        model.select(model.rowOf(dir.filePath("b.png")));
        model.select(model.rowOf(dir.filePath("d.png")), DirectoryEntryModel::Toggle);
        model.select(model.rowOf(dir.filePath("c.txt")), DirectoryEntryModel::Toggle);
        QCOMPARE(model.selectedCount(), 3);
        // Sorting reorders rows; identity stays with the keys.
        model.setSort("size", false); QVERIFY(settle(model));
        QCOMPARE(names(model), (QStringList{"b.png", "d.png", "c.txt", "a.png"}));
        QCOMPARE(model.selectedPaths(), (QStringList{dir.filePath("b.png"), dir.filePath("d.png"), dir.filePath("c.txt")}));
        QCOMPARE(model.cursorRow(), 2); // cursor is c.txt, now third
        // External delete and rename: removed keys drop, survivors stay, new names are unselected.
        QVERIFY(QFile::remove(dir.filePath("c.txt")));
        QVERIFY(QFile::rename(dir.filePath("d.png"), dir.filePath("renamed.png")));
        touch(dir.filePath("new.png"), 5);
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        model.refresh(); QVERIFY(settle(model));
        QCOMPARE(model.selectedPaths(), QStringList{dir.filePath("b.png")});
        QCOMPARE(model.cursorRow(), -1); // the cursor's file was deleted
        QCOMPARE(model.count(), 4);
        QCOMPARE(model.rowOf(dir.filePath("d.png")), -1);
        QVERIFY(model.rowOf(dir.filePath("renamed.png")) >= 0);
        // A refresh never restores a removed entry, and an unchanged relisting does not reset the view.
        model.refresh(); QVERIFY(settle(model)); QTest::qWait(100);
        QCOMPARE(model.rowOf(dir.filePath("c.txt")), -1);
        QCOMPARE(resets.count(), 0);
        // Same directory through setDirectory is a refresh that keeps selection.
        model.setDirectory(dir.path() + "/"); QVERIFY(settle(model));
        QCOMPARE(model.selectedCount(), 1);
    }
    void incrementalRefreshInsertsAndRemovesInPlace() {
        QTemporaryDir dir; for (const char* n : {"b.png", "d.png", "f.png"}) touch(dir.filePath(n));
        DirectoryEntryModel model; model.setActive(true); model.setDirectory(dir.path()); QVERIFY(settle(model));
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset), inserted(&model, &QAbstractItemModel::rowsInserted), removed(&model, &QAbstractItemModel::rowsRemoved);
        touch(dir.filePath("a.png")); touch(dir.filePath("e.png")); touch(dir.filePath("g.png")); QVERIFY(QFile::remove(dir.filePath("d.png")));
        model.refresh(); QVERIFY(settle(model));
        QCOMPARE(names(model), (QStringList{"a.png", "b.png", "e.png", "f.png", "g.png"}));
        QCOMPARE(resets.count(), 0); QCOMPARE(removed.count(), 1); QCOMPARE(inserted.count(), 3);
    }
    void inactiveReleasesRowsAndKeepsSelection() {
        QTemporaryDir dir; for (const char* n : {"a.png", "b.png", "c.png"}) touch(dir.filePath(n));
        DirectoryEntryModel model; model.setActive(true); model.setDirectory(dir.path()); QVERIFY(settle(model));
        model.select(2);
        model.setActive(false);
        QCOMPARE(model.count(), 0); QCOMPARE(model.rowCount(), 0); QVERIFY(!model.loading());
        QCOMPARE(model.cursorRow(), -1);
        model.refresh(); QVERIFY(!model.loading()); // inactive: refresh does no work
        model.setActive(true); QVERIFY(settle(model));
        QCOMPARE(model.count(), 3); QCOMPARE(model.cursorRow(), 2); QCOMPARE(model.selectedCount(), 1);
    }
    void parentNavigationPutsCursorOnTheFolderLeft() {
        QTemporaryDir dir; QDir(dir.path()).mkpath("album/inner"); QDir(dir.path()).mkpath("zoo"); touch(dir.filePath("top.png"));
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(dir.filePath("album")); QVERIFY(settle(model));
        QCOMPARE(names(model), QStringList{"inner"});
        model.setDirectory(dir.path()); QVERIFY(settle(model));
        QCOMPARE(model.cursorRow(), model.rowOf(dir.filePath("album")));
        QCOMPARE(model.selectedPaths(), QStringList{dir.filePath("album")});
        model.setDirectory(dir.filePath("zoo")); QVERIFY(settle(model));
        QCOMPARE(model.cursorRow(), -1); QCOMPARE(model.selectedCount(), 0); // entering a folder starts clean
    }
    void inaccessibleFolderReportsFailure() {
        QTemporaryDir dir;
        DirectoryEntryModel model; model.setActive(true);
        QSignalSpy failed(&model, &DirectoryEntryModel::listingFailed);
        model.setDirectory(dir.filePath("does-not-exist")); QVERIFY(settle(model));
        QVERIFY(!model.accessible()); QCOMPARE(model.count(), 0);
        QCOMPARE(failed.count(), 1); QVERIFY(!failed.first().first().toString().isEmpty());
        touch(dir.filePath("x.png"));
        model.setDirectory(dir.path()); QVERIFY(model.accessible()); QVERIFY(settle(model)); QCOMPARE(model.count(), 1);
    }
    // A transient failure of the same folder (share blip, permission flicker) must not discard the selection.
    void transientFailureKeepsSelectionForRecovery() {
        QTemporaryDir root; QVERIFY(QDir(root.path()).mkpath("album"));
        const QString album = root.filePath("album"), away = root.filePath("away");
        for (const char* n : {"a.png", "b.png", "c.txt"}) touch(album + "/" + n);
        DirectoryEntryModel model; model.setActive(true); model.setDirectory(album); QVERIFY(settle(model));
        model.select(0); model.select(2, DirectoryEntryModel::Toggle);
        QCOMPARE(model.selectedCount(), 2);
        QVERIFY(QDir().rename(album, away));
        model.refresh(); QVERIFY(settle(model));
        QVERIFY(!model.accessible()); QCOMPARE(model.count(), 0);
        QCOMPARE(model.selectedCount(), 0); QVERIFY(model.selectedPaths().isEmpty()); // nothing actionable while failed
        QVERIFY(QDir().rename(away, album));
        model.refresh(); QVERIFY(settle(model));
        QVERIFY(model.accessible()); QCOMPARE(model.count(), 3);
        QCOMPARE(model.selectedCount(), 2); QCOMPARE(model.cursorRow(), 2);
    }
    void currentImageRole() {
        QTemporaryDir dir; for (const char* n : {"a.png", "b.png"}) touch(dir.filePath(n));
        DirectoryEntryModel model; model.setActive(true); model.setDirectory(dir.path()); QVERIFY(settle(model));
        QSignalSpy data(&model, &QAbstractItemModel::dataChanged);
        model.setCurrentImage(QDir::toNativeSeparators(dir.filePath("b.png")));
        QVERIFY(model.data(model.index(1), DirectoryEntryModel::CurrentRole).toBool());
        QVERIFY(!model.data(model.index(0), DirectoryEntryModel::CurrentRole).toBool());
        QCOMPARE(data.count(), 1);
        QCOMPARE(model.cursorRow(), -1); // the opened image is not the cursor
    }
    // Review B1: scattered external changes in a large folder must not stall the GUI thread during the refresh
    // merge, and selection/cursor/anchor keys must survive the fallback reset.
    void largeInterleavedRefreshStaysResponsive() {
        // GLANCELOT_MERGE_BASE / GLANCELOT_MERGE_ADD scale the case for manual runs (e.g. 80000 / 8000).
        const int base = qEnvironmentVariableIntValue("GLANCELOT_MERGE_BASE") > 0 ? qEnvironmentVariableIntValue("GLANCELOT_MERGE_BASE") : 30000;
        const int added = qEnvironmentVariableIntValue("GLANCELOT_MERGE_ADD") > 0 ? qEnvironmentVariableIntValue("GLANCELOT_MERGE_ADD") : 5000;
        const auto name = [](int n) { return QString("f%1.png").arg(n, 6, 10, QChar('0')); };
        QTemporaryDir dir;
        for (int i = 0; i < base; ++i) touch(dir.filePath(name(i * 2)));
        DirectoryEntryModel model; model.setActive(true); model.setDirectory(dir.path()); QVERIFY(settle(model, 60000));
        QCOMPARE(model.count(), base);
        // Originals are f(2i); the removal below deletes even i (multiples of 4), so 2 * odd survives both steps.
        const QString kept = dir.filePath(name(2 * 101)), cursor = dir.filePath(name(2 * 7001)), dropped = dir.filePath(name(0));
        model.select(model.rowOf(kept));
        model.select(model.rowOf(dropped), DirectoryEntryModel::Toggle);
        model.select(model.rowOf(cursor), DirectoryEntryModel::Toggle);
        QCOMPARE(model.selectedCount(), 3);
        QElapsedTimer clock; clock.start(); qint64 last = 0, worst = 0;
        QTimer tick; tick.setInterval(5);
        connect(&tick, &QTimer::timeout, this, [&] { const auto now = clock.elapsed(); worst = std::max(worst, now - last); last = now; });
        const auto measure = [&] {
            worst = 0; tick.start(); last = clock.elapsed();
            model.refresh(); const bool done = settle(model, 60000);
            QTest::qWait(20); tick.stop();
            return done;
        };
        // Interleaved insertions: one new name between existing ones, 5,000 separate runs.
        const int stride = base / added;
        for (int k = 0; k < added; ++k) touch(dir.filePath(name(k * stride * 2 + 1)));
        QVERIFY(measure());
        qInfo() << "interleaved +" << added << "on" << base << ": worst GUI gap" << worst << "ms";
        QCOMPARE(model.count(), base + added);
        QVERIFY2(worst < 250, qPrintable(QString("GUI thread stalled %1 ms on interleaved inserts").arg(worst)));
        QCOMPARE(model.selectedCount(), 3); QCOMPARE(model.cursorRow(), model.rowOf(cursor));
        // Interleaved removals: every other original file, 15,000 separate runs.
        for (int i = 0; i < base; i += 2) QVERIFY(QFile::remove(dir.filePath(name(i * 2))));
        QVERIFY(measure());
        qInfo() << "interleaved -" << base / 2 << ": worst GUI gap" << worst << "ms";
        QCOMPARE(model.count(), base / 2 + added);
        QVERIFY2(worst < 250, qPrintable(QString("GUI thread stalled %1 ms on interleaved removals").arg(worst)));
        QCOMPARE(model.selectedPaths(), (QStringList{kept, cursor}));
        QCOMPARE(model.cursorRow(), model.rowOf(cursor));
        // The anchor survived too: a range from it still works.
        model.select(model.rowOf(kept), DirectoryEntryModel::Range);
        QCOMPARE(model.selectedCount(), model.rowOf(cursor) - model.rowOf(kept) + 1);
    }
    void largeFolderStaysResponsive() {
        QTemporaryDir dir;
        QElapsedTimer make; make.start();
        for (int i = 0; i < 12000; ++i) touch(dir.filePath(QString("f%1.%2").arg(i).arg(i % 4 ? "png" : "dat")));
        QDir(dir.path()).mkpath("sub");
        qInfo() << "generated 12001 entries in" << make.elapsed() << "ms";
        DirectoryEntryModel model; model.setActive(true);
        // The GUI thread keeps ticking while the worker lists; the largest gap bounds the apply step.
        QElapsedTimer clock; clock.start(); qint64 last = 0, worst = 0;
        QTimer tick; tick.setInterval(5);
        connect(&tick, &QTimer::timeout, this, [&] { const auto now = clock.elapsed(); worst = std::max(worst, now - last); last = now; });
        tick.start();
        QElapsedTimer listing; listing.start();
        model.setDirectory(dir.path());
        QVERIFY(settle(model, 30000));
        const auto elapsed = listing.elapsed();
        tick.stop();
        qInfo() << "listed" << model.count() << "entries in" << elapsed << "ms; worst GUI gap" << worst << "ms";
        QCOMPARE(model.count(), 12001);
        QCOMPARE(model.kindAt(0), QString("folder"));
        QVERIFY2(worst < 250, qPrintable(QString("GUI thread stalled %1 ms").arg(worst)));
        model.selectAll(); QCOMPARE(model.selectedCount(), 12001);
        model.select(11000, DirectoryEntryModel::Range); QVERIFY(model.selectedCount() > 1);
        QVERIFY(!model.truncated());
    }
};
QTEST_GUILESS_MAIN(DirectoryModelTests)
#include "directory_model_tests.moc"
