// B3 (F070): the export worker and the dialog over it, through the shipped ui/Main.qml. Generated media only.
// Offscreen/software rendering proves logic and layout, not native file dialogs or physical displays.
#include "selection_export_service.hpp"
#include "ui_test_support.hpp"
#include <QAccessible>
#include <QCryptographicHash>
#include <QJSEngine>
#include <QProcess>
#include <QSignalSpy>
#include <atomic>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {
namespace fs = std::filesystem;
using glancelot::ExportRequest;
using glancelot::ExportResult;
using glancelot::ExportStatus;

const int kDeadline = 10000;

// Generated files with a recorded hash and modified time: every case proves it left them alone.
struct Originals {
    QTemporaryDir dir;
    QStringList paths;
    explicit Originals(int count = 3) {
        for (int i = 0; i < count; ++i) {
            QImage image(32, 24, QImage::Format_RGB32);
            image.fill(QColor::fromHsv((i * 70) % 360, 110, 180));
            paths << dir.filePath(QString("img%1.png").arg(i + 1));
            image.save(paths.last());
        }
        record();
    }
    QMap<QString, QPair<QByteArray, QDateTime>> before;
    static QByteArray hash(const QString& path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256) : QByteArray();
    }
    void record() { for (const auto& path : paths) before[path] = {hash(path), QFileInfo(path).lastModified()}; }
    QString changed() const {
        for (const auto& path : paths) {
            if (hash(path) != before[path].first) return path + " content changed";
            if (QFileInfo(path).lastModified() != before[path].second) return path + " modified time changed";
        }
        return {};
    }
};
QByteArray readAll(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
void writeText(const QString& path, const QByteArray& text) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text);
}
QStringList leftovers(const QString& folder) {
    return QDir(folder).entryList({".glancelot_export_*"}, QDir::Files | QDir::Hidden);
}
QVariantMap csvOptions(const QString& destination, bool overwrite = false) {
    return {{"destination", destination}, {"format", "csv"}, {"neutralizeFormulas", true}, {"overwrite", overwrite}};
}
bool prepareOn(SelectionExportService& service, const QStringList& paths, QVariantMap* summary = nullptr) {
    QSignalSpy spy(&service, &SelectionExportService::prepared);
    const quint64 id = service.prepare(paths);
    if (!QTest::qWaitFor([&] { return spy.size() == 1; }, kDeadline)) return false;
    if (spy.first().first().toULongLong() != id) return false;
    if (summary) *summary = spy.first().at(1).toMap();
    return true;
}
QVariantMap runOn(SelectionExportService& service, const QVariantMap& options) {
    QSignalSpy spy(&service, &SelectionExportService::finished);
    if (service.start(options) == 0) return {{"status", "not started"}};
    if (!QTest::qWaitFor([&] { return spy.size() == 1; }, kDeadline)) return {{"status", "timed out"}};
    return spy.first().at(1).toMap();
}

// An engine that really exports but holds at the first item until it is released or stopped, so a test can act while
// the export is running without any timing assumption.
struct HeldEngine {
    std::shared_ptr<std::atomic<bool>> entered = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<std::atomic<bool>> release = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<std::atomic<std::thread::id>> worker = std::make_shared<std::atomic<std::thread::id>>();
    SelectionExportService::Engine engine() const {
        return [entered = entered, release = release, worker = worker](const ExportRequest& request, std::stop_token cancel,
                                                                       const SelectionExportService::Progress& progress) {
            worker->store(std::this_thread::get_id());
            return glancelot::exportSelection(request, cancel, [&](std::size_t done, std::size_t total) {
                progress(done, total);
                entered->store(true);
                while (!release->load() && !cancel.stop_requested()) std::this_thread::yield(); // sleep-ok: a held worker, released by the test
            });
        };
    }
};

// The dialog, opened through the shipped command on a real selection.
QQuickItem* item(Harness& h, const char* name) { return named(h.window, QString::fromLatin1(name)); }
QObject* flowOf(Harness& h) {
    QObject* loader = h.object("exportFlow");
    return loader ? loader->property("item").value<QObject*>() : nullptr;
}
SelectionExportService* serviceOf(Harness& h) {
    QObject* flow = flowOf(h);
    return flow ? flow->findChild<SelectionExportService*>("exportService") : nullptr;
}
QObject* dialogOf(Harness& h) { QObject* flow = flowOf(h); return flow ? flow->findChild<QObject*>("exportDialog") : nullptr; }
bool dialogOpen(Harness& h) { QObject* dialog = dialogOf(h); return dialog && dialog->property("opened").toBool(); }
QString phaseOf(Harness& h) { QObject* flow = flowOf(h); return flow ? flow->property("phase").toString() : QString(); }
QString textOf(Harness& h, const char* name) { QQuickItem* found = item(h, name); return found ? found->property("text").toString() : QString(); }
bool visibleItem(Harness& h, const char* name) { QQuickItem* found = item(h, name); return found && found->isVisible(); }
void click(Harness& h, const char* name) {
    QQuickItem* button = item(h, name);
    QVERIFY2(button, name);
    QVERIFY2(button->isVisible() && button->isEnabled(), name);
    (void)h.window->grabWindow(); // settles pending layout, so the click lands where the button is now
    QTest::mouseClick(h.window, Qt::LeftButton, Qt::NoModifier, sceneCenter(button));
}
bool setDestination(Harness& h, const QString& path) {
    QQuickItem* field = item(h, "exportDestination");
    return field && field->setProperty("text", path);
}
struct OpenedDialog {
    Originals media;
    Harness h;
    OpenedDialog(int count = 3) : media(count) {}
    bool open(const QStringList& selected = {}, QSize size = QSize(1180, 780)) {
        if (!h.window) return false;
        h.window->resize(size);
        h.window->show();
        h.viewer.setFilmstripVisible(false);
        h.viewer.open(media.paths.first());
        if (!QTest::qWaitFor([&] { return h.viewer.imageReady(); }, 15000)) return false;
        if (!QMetaObject::invokeMethod(h.window, "invoke", Q_ARG(QVariant, QStringLiteral("view-grid")))) return false;
        if (!QTest::qWaitFor([&] { return !h.fileGrid.loading() && h.fileGrid.count() == media.paths.size(); }, kDeadline)) return false;
        // Entering the grid selects the viewed photo a moment later; let that land before making the test's selection.
        if (!QTest::qWaitFor([&] { return h.fileGrid.selectedCount() == 1; }, kDeadline)) return false;
        h.fileGrid.clearSelection();
        for (const auto& path : selected.isEmpty() ? media.paths : selected) h.fileGrid.select(h.fileGrid.rowOf(path), DirectoryEntryModel::Toggle);
        if (!QMetaObject::invokeMethod(h.window, "invoke", Q_ARG(QVariant, QStringLiteral("export-selection")))) return false;
        return QTest::qWaitFor([&] { return dialogOpen(h) && phaseOf(h) == "ready"; }, kDeadline);
    }
};
} // namespace

class ExportUiTests : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> config;
private slots:
    void initTestCase() {
        QQuickStyle::setStyle("Basic"); // the style the application uses
        for (const char* face : {"Geist-Light", "Geist-Regular", "Geist-Medium", "Geist-SemiBold", "GeistMono-Regular"})
            QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/geist/%1.ttf").arg(QLatin1String(face)));
        qmlRegisterType<ImageCanvas>("Glancelot", 1, 0, "ImageCanvas");
        // The source-loaded UI has no generated qmldir, so the token singleton is registered as the app module does.
        qmlRegisterSingletonType(QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/Theme.qml")), "Glancelot", 1, 0, "Theme");
    }
    void init() {
        config = std::make_unique<QTemporaryDir>();
        QVERIFY(config->isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config->path().toUtf8());
    }
    void cleanup() { qunsetenv("GLANCELOT_CONFIG_DIR"); config.reset(); }
    // ---- Worker ----
    void exportsSnapshotOnWorkerAndLeavesOriginals() {
        Originals media;
        QTemporaryDir out;
        const QString destination = out.filePath("list.csv");
        SelectionExportService service;
        QSignalSpy busy(&service, &SelectionExportService::busyChanged);
        QVariantMap summary;
        QVERIFY(prepareOn(service, media.paths, &summary));
        QCOMPARE(summary["status"].toString(), QString("ready"));
        QCOMPARE(summary["files"].toInt(), 3);
        QCOMPARE(summary["unavailable"].toInt(), 0);
        const auto result = runOn(service, csvOptions(destination));
        QCOMPARE(result["status"].toString(), QString("written"));
        QCOMPARE(result["written"].toInt(), 3);
        const QByteArray csv = readAll(destination);
        QVERIFY(csv.startsWith("\xEF\xBB\xBF" "path,name,kind,bytes,modified\r\n"));
        QVERIFY(csv.contains("img2.png"));
        QVERIFY2(media.changed().isEmpty(), qPrintable(media.changed()));
        QVERIFY(busy.size() >= 2);
        QVERIFY(!service.busy());
        // The engine really ran off the calling thread.
        HeldEngine held;
        *held.release = true;
        service.setEngine(held.engine());
        QCOMPARE(runOn(service, csvOptions(out.filePath("second.csv")))["status"].toString(), QString("written"));
        QVERIFY(held.worker->load() != std::this_thread::get_id());
    }
    void missingItemIsReportedWithoutStoppingTheRest() {
        Originals media;
        QTemporaryDir out;
        SelectionExportService service;
        const QString gone = media.dir.filePath("gone.png");
        QVariantMap summary;
        QVERIFY(prepareOn(service, QStringList(media.paths) << gone, &summary));
        QCOMPARE(summary["files"].toInt(), 3);
        QCOMPARE(summary["unavailable"].toInt(), 1);
        const auto result = runOn(service, csvOptions(out.filePath("list.csv")));
        QCOMPARE(result["status"].toString(), QString("written"));
        QCOMPARE(result["written"].toInt(), 3);
        QCOMPARE(result["skippedCount"].toInt(), 1);
        const auto first = result["skipped"].toList().first().toMap();
        QCOMPARE(first["reason"].toString(), QString("missing"));
        QCOMPARE(first["name"].toString(), QString("gone.png"));
    }
    void itemChangedAfterSnapshotIsSkipped() {
        Originals media;
        QTemporaryDir out;
        SelectionExportService service;
        QVERIFY(prepareOn(service, media.paths));
        QVERIFY(QFile::remove(media.paths[1]));
        const auto result = runOn(service, csvOptions(out.filePath("list.csv")));
        QCOMPARE(result["status"].toString(), QString("written"));
        QCOMPARE(result["written"].toInt(), 2);
        QCOMPARE(result["skippedCount"].toInt(), 1);
    }
    // Review O1: a thrown engine failure must stay on the worker and become a result.
    void engineExceptionsBecomeResults() {
        Originals media;
        QTemporaryDir out;
        SelectionExportService service;
        QVERIFY(prepareOn(service, media.paths));
        service.setEngine([](const ExportRequest&, std::stop_token, const SelectionExportService::Progress&) -> ExportResult {
            throw std::runtime_error("disk fell off");
        });
        auto result = runOn(service, csvOptions(out.filePath("a.csv")));
        QCOMPARE(result["status"].toString(), QString("error"));
        QCOMPARE(result["detail"].toString(), QString("disk fell off"));
        QCOMPARE(result["written"].toInt(), 0);
        QVERIFY(!QFile::exists(out.filePath("a.csv")));
        service.setEngine([](const ExportRequest&, std::stop_token, const SelectionExportService::Progress&) -> ExportResult { throw 42; });
        result = runOn(service, csvOptions(out.filePath("b.csv")));
        QCOMPARE(result["status"].toString(), QString("error"));
        // The service stays usable afterwards.
        service.setEngine([](const ExportRequest& request, std::stop_token cancel, const SelectionExportService::Progress& progress) {
            return glancelot::exportSelection(request, cancel, progress);
        });
        QCOMPARE(runOn(service, csvOptions(out.filePath("c.csv")))["status"].toString(), QString("written"));
    }
#ifdef Q_OS_WIN
    // The real O1 input: an NTFS name with a lone surrogate. That one item is left out and the rest are exported.
    void loneSurrogateNameIsLeftOutNotFatal() {
        Originals media;
        QTemporaryDir out;
        const QString bad = media.dir.filePath(QString("bad") + QChar(0xD800) + ".png");
        {
            std::ofstream file(fs::path(QDir::toNativeSeparators(bad).toStdWString()), std::ios::binary);
            file << "x";
            QVERIFY(file.good());
        }
        SelectionExportService service;
        QVariantMap summary;
        QVERIFY(prepareOn(service, QStringList(media.paths) << bad, &summary));
        QCOMPARE(summary["status"].toString(), QString("ready"));
        QCOMPARE(summary["files"].toInt(), 3);
        QCOMPARE(summary["unavailable"].toInt(), 1);
        const auto result = runOn(service, csvOptions(out.filePath("list.csv")));
        QCOMPARE(result["status"].toString(), QString("written"));
        QCOMPARE(result["written"].toInt(), 3);
        QCOMPARE(result["skippedCount"].toInt(), 1);
        QCOMPARE(result["skipped"].toList().first().toMap()["reason"].toString(), QString("unrepresentable"));
        QVERIFY(leftovers(out.path()).isEmpty());
    }
#endif
    void cancelMidExportLeavesNothing() {
        Originals media;
        QTemporaryDir out;
        const QString destination = out.filePath("list.csv");
        writeText(destination, "earlier export");
        SelectionExportService service;
        HeldEngine held;
        service.setEngine(held.engine());
        QVERIFY(prepareOn(service, media.paths));
        QSignalSpy finished(&service, &SelectionExportService::finished);
        QVERIFY(service.start(csvOptions(destination, true)) != 0);
        QTRY_VERIFY_WITH_TIMEOUT(held.entered->load(), 10000);
        QVERIFY(service.busy());
        QCOMPARE(service.start(csvOptions(destination, true)), quint64(0)); // no duplicate export while one runs
        service.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QCOMPARE(finished.first().at(1).toMap()["status"].toString(), QString("cancelled"));
        QCOMPARE(readAll(destination), QByteArray("earlier export"));
        QVERIFY(leftovers(out.path()).isEmpty());
        QVERIFY2(media.changed().isEmpty(), qPrintable(media.changed()));
    }
    void newerPrepareDropsTheOlderResult() {
        Originals media;
        QTemporaryDir out;
        const QString destination = out.filePath("list.csv");
        SelectionExportService service;
        HeldEngine held;
        service.setEngine(held.engine());
        QVERIFY(prepareOn(service, media.paths));
        QSignalSpy finished(&service, &SelectionExportService::finished);
        QSignalSpy prepared(&service, &SelectionExportService::prepared);
        const quint64 exportId = service.start(csvOptions(destination));
        QVERIFY(exportId != 0);
        QTRY_VERIFY_WITH_TIMEOUT(held.entered->load(), 10000);
        const quint64 first = service.prepare({media.paths[0]});
        const quint64 second = service.prepare({media.paths[1], media.paths[2]}); // replaces the queued one
        QTRY_COMPARE_WITH_TIMEOUT(prepared.size(), 1, 10000);
        QVERIFY(first != second);
        QCOMPARE(prepared.first().first().toULongLong(), second);
        QCOMPARE(prepared.first().at(1).toMap()["files"].toInt(), 2);
        QCOMPARE(finished.size(), 0); // the superseded export never reports
        QVERIFY(!service.busy());
        QVERIFY(leftovers(out.path()).isEmpty());
        QVERIFY(!QFile::exists(destination));
    }
    void destroyingTheServiceStopsTheWorker() {
        Originals media;
        QTemporaryDir out;
        const QString destination = out.filePath("list.csv");
        HeldEngine held;
        {
            SelectionExportService service;
            service.setEngine(held.engine());
            QVERIFY(prepareOn(service, media.paths));
            QVERIFY(service.start(csvOptions(destination)) != 0);
            QTRY_VERIFY_WITH_TIMEOUT(held.entered->load(), 10000);
        }
        QVERIFY(!QFile::exists(destination));
        QVERIFY(leftovers(out.path()).isEmpty());
    }
    // However fast the engine reports, at most one update is queued at a time.
    void progressUpdatesAreCoalesced() {
        Originals media;
        QTemporaryDir out;
        SelectionExportService service;
        auto finishedReporting = std::make_shared<std::atomic<bool>>(false);
        service.setEngine([finishedReporting](const ExportRequest& request, std::stop_token, const SelectionExportService::Progress& progress) {
            for (int i = 0; i < 200000; ++i) progress(std::size_t(i), 200000);
            finishedReporting->store(true);
            ExportResult result;
            result.generation = request.generation;
            result.status = ExportStatus::Written;
            return result;
        });
        QVERIFY(prepareOn(service, media.paths));
        QSignalSpy updates(&service, &SelectionExportService::progress);
        QSignalSpy finished(&service, &SelectionExportService::finished);
        QVERIFY(service.start(csvOptions(out.filePath("list.csv"))) != 0);
        // The event loop deliberately does not run here, so anything the worker posted is still queued.
        QElapsedTimer waited;
        waited.start();
        while (!finishedReporting->load() && waited.elapsed() < kDeadline) std::this_thread::yield(); // sleep-ok: waits on the worker with the loop stopped on purpose
        QVERIFY(finishedReporting->load());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QCOMPARE(updates.size(), 1);
        QCOMPARE(updates.first().at(1).toInt(), 199999);
    }
    void refusalsWriteNothing_data() {
        QTest::addColumn<QString>("destinationKind");
        QTest::addColumn<QString>("status");
        QTest::newRow("empty") << "empty" << "destinationEmpty";
        QTest::newRow("relative") << "relative" << "destinationRelative";
        QTest::newRow("folder") << "folder" << "destinationIsFolder";
        QTest::newRow("no such folder") << "missing-parent" << "folderMissing";
        QTest::newRow("parent is a file") << "file-parent" << "folderMissing";
        QTest::newRow("existing without overwrite") << "existing" << "destinationExists";
        QTest::newRow("an exported item") << "item" << "destinationIsItem";
        QTest::newRow("a photo not in the selection") << "other-photo" << "destinationProtected";
    }
    void refusalsWriteNothing() {
        QFETCH(QString, destinationKind);
        QFETCH(QString, status);
        Originals media(2);
        QTemporaryDir out;
        writeText(out.filePath("existing.csv"), "earlier");
        writeText(out.filePath("plain.txt"), "a file");
        const QString otherPhoto = media.dir.filePath("other.png");
        writeText(otherPhoto, "not selected");
        const QMap<QString, QString> destinations{
            {"empty", ""}, {"relative", "list.csv"}, {"folder", out.path()},
            {"missing-parent", out.filePath("nope/list.csv")}, {"file-parent", out.filePath("plain.txt/list.csv")},
            {"existing", out.filePath("existing.csv")}, {"item", media.paths[0]}, {"other-photo", otherPhoto}};
        SelectionExportService service;
        QVERIFY(prepareOn(service, media.paths));
        // An item or another photo stays refused even when replacing was allowed.
        const bool overwrite = destinationKind == "item" || destinationKind == "other-photo";
        const auto result = runOn(service, csvOptions(destinations[destinationKind], overwrite));
        QCOMPARE(result["status"].toString(), status);
        QCOMPARE(readAll(out.filePath("existing.csv")), QByteArray("earlier"));
        QCOMPARE(readAll(otherPhoto), QByteArray("not selected"));
        QVERIFY(leftovers(out.path()).isEmpty());
        QVERIFY(!QFile::exists(out.filePath("list.csv")));
        QVERIFY2(media.changed().isEmpty(), qPrintable(media.changed()));
    }
    void tooManyItemsAreRefusedBeforeAnyWork() {
        Originals media;
        SelectionExportService service;
        service.setMaxItems(2);
        QVariantMap summary;
        QVERIFY(prepareOn(service, media.paths, &summary));
        QCOMPARE(summary["status"].toString(), QString("tooMany"));
        QCOMPARE(summary["total"].toInt(), 3);
        QCOMPARE(service.start(csvOptions("x")), quint64(0)); // nothing was prepared
    }
    void pathListAndRelativeRootOptionsReachTheEngine() {
        Originals media;
        QTemporaryDir out;
        const QString destination = out.filePath("list.txt");
        SelectionExportService service;
        QVERIFY(prepareOn(service, media.paths));
        const auto result = runOn(service, {{"destination", destination}, {"format", "paths"}, {"relative", true}, {"root", media.dir.path()}});
        QCOMPARE(result["status"].toString(), QString("written"));
        QCOMPARE(readAll(destination), QByteArray("img1.png\nimg2.png\nimg3.png\n"));
    }

    // ---- Dialog through the shipped Main.qml ----
    void mainLoadsTheDialogOnlyOnFirstUse() {
        Harness h;
        QVERIFY(h.window);
        QVERIFY(h.object("exportFlow"));
        QVERIFY(!flowOf(h));
        bool listed = false;
        for (const auto& command : h.viewer.commands()) {
            const auto row = command.toMap();
            if (row["id"].toString() == "export-selection") { listed = true; QCOMPARE(row["binding"].toString(), QString("Ctrl+Shift+E")); }
        }
        QVERIFY(listed);
    }
    void emptySelectionExplainsAndOpensNothing() {
        Harness h;
        QVERIFY(h.window);
        h.window->resize(1180, 780);
        h.window->show();
        QVERIFY(QMetaObject::invokeMethod(h.window, "invoke", Q_ARG(QVariant, QStringLiteral("export-selection"))));
        QObject* deleteFlow = h.object("deleteFlow");
        QVERIFY(deleteFlow);
        QTRY_COMPARE_WITH_TIMEOUT(deleteFlow->property("noticeText").toString(), QString("Select items in the grid to export them."), 10000);
        QVERIFY(!dialogOpen(h));
        QVERIFY(!h.window->property("modalOpen").toBool());
    }
    void exportFromTheDialogShowsTheResult() {
        OpenedDialog d;
        QVERIFY(d.open());
        QVERIFY(d.h.window->property("modalOpen").toBool());
        QCOMPARE(textOf(d.h, "exportSummary"), QString("3 files to export"));
        QTemporaryDir out;
        const QString destination = out.filePath("list.csv");
        QVERIFY(setDestination(d.h, destination));
        click(d.h, "exportStart");
        QTRY_COMPARE_WITH_TIMEOUT(phaseOf(d.h), QString("done"), 10000);
        QCOMPARE(textOf(d.h, "exportResultText"), QString("Exported 3 items to “list.csv”."));
        QVERIFY(item(d.h, "exportDone")->hasActiveFocus());
        QVERIFY(readAll(destination).contains("img3.png"));
        QVERIFY2(d.media.changed().isEmpty(), qPrintable(d.media.changed()));
        click(d.h, "exportDone");
        QTRY_VERIFY_WITH_TIMEOUT(!d.h.window->property("modalOpen").toBool(), 10000);
    }
    void guiStaysLiveAndEscapeCancelsARunningExport() {
        OpenedDialog d;
        QVERIFY(d.open());
        QTemporaryDir out;
        const QString destination = out.filePath("list.csv");
        QVERIFY(setDestination(d.h, destination));
        HeldEngine held;
        serviceOf(d.h)->setEngine(held.engine());
        int ticks = 0;
        QTimer tick;
        QObject::connect(&tick, &QTimer::timeout, [&] { ++ticks; });
        tick.start(1);
        click(d.h, "exportStart");
        QTRY_VERIFY_WITH_TIMEOUT(held.entered->load(), 10000);
        QCOMPARE(phaseOf(d.h), QString("running"));
        QTRY_COMPARE_WITH_TIMEOUT(textOf(d.h, "exportProgressText"), QString("Exported 0 of 3"), 10000);
        // The worker is held inside the engine, and the event loop still runs.
        const int ticksAtHold = ticks;
        QTRY_VERIFY_WITH_TIMEOUT(ticks > ticksAtHold + 5, 10000);
        QVERIFY(item(d.h, "exportStop")->hasActiveFocus());
        QVERIFY(!item(d.h, "exportDestination")->isEnabled());
        // A running export can't be dismissed: Escape cancels it, and the dialog stays to say so.
        QTest::keyClick(d.h.window, Qt::Key_Escape);
        QTRY_COMPARE_WITH_TIMEOUT(textOf(d.h, "exportStatus"), QString("Export cancelled. Nothing was written."), 10000);
        QVERIFY(visibleItem(d.h, "exportStatus"));
        QVERIFY(dialogOpen(d.h));
        QCOMPARE(phaseOf(d.h), QString("ready"));
        QVERIFY(!QFile::exists(destination));
        QVERIFY(leftovers(out.path()).isEmpty());
        // The form keeps what was entered, and the same dialog can export after all.
        QCOMPARE(item(d.h, "exportDestination")->property("text").toString(), destination);
        *held.release = true;
        click(d.h, "exportStart");
        QTRY_COMPARE_WITH_TIMEOUT(phaseOf(d.h), QString("done"), 10000);
    }
    void existingFileIsReplacedOnlyAfterConfirmation() {
        OpenedDialog d;
        QVERIFY(d.open());
        QTemporaryDir out;
        const QString destination = out.filePath("list.csv");
        writeText(destination, "earlier export");
        QVERIFY(setDestination(d.h, destination));
        (void)d.h.window->grabWindow();
        const QPoint exportAt = sceneCenter(item(d.h, "exportStart"));
        click(d.h, "exportStart");
        QTRY_COMPARE_WITH_TIMEOUT(phaseOf(d.h), QString("replace"), 10000);
        QCOMPARE(readAll(destination), QByteArray("earlier export"));
        // A double-click on Export lands on the safe answer, never on Replace.
        (void)d.h.window->grabWindow();
        QQuickItem* keep = item(d.h, "exportKeep");
        QQuickItem* replace = item(d.h, "exportReplace");
        const auto spanOf = [](QQuickItem* i) { return i->mapRectToScene(QRectF(0, 0, i->width(), i->height())); };
        QVERIFY(spanOf(keep).left() <= exportAt.x() && exportAt.x() <= spanOf(keep).right());
        QVERIFY(exportAt.x() < spanOf(replace).left() || exportAt.x() > spanOf(replace).right());
        QCOMPARE(textOf(d.h, "exportReplaceQuestion"), QString("“list.csv” already exists. Replace it with this export?"));
        QVERIFY(visibleItem(d.h, "exportReplaceQuestion"));
        // The safe answer holds the focus, and Escape takes it.
        QVERIFY(item(d.h, "exportKeep")->hasActiveFocus());
        QTest::keyClick(d.h.window, Qt::Key_Escape);
        QTRY_COMPARE_WITH_TIMEOUT(phaseOf(d.h), QString("ready"), 10000);
        QVERIFY(dialogOpen(d.h));
        QVERIFY(item(d.h, "exportDestination")->hasActiveFocus());
        QCOMPARE(readAll(destination), QByteArray("earlier export"));
        // A held key can't confirm.
        click(d.h, "exportStart");
        QTRY_COMPARE_WITH_TIMEOUT(phaseOf(d.h), QString("replace"), 10000);
        item(d.h, "exportReplace")->forceActiveFocus(Qt::TabFocusReason);
        sendKey(d.h.window, Qt::Key_Return, true);
        QCOMPARE(phaseOf(d.h), QString("replace"));
        click(d.h, "exportReplace");
        QTRY_COMPARE_WITH_TIMEOUT(phaseOf(d.h), QString("done"), 10000);
        QVERIFY(readAll(destination).contains("img1.png"));
        QVERIFY(leftovers(out.path()).isEmpty());
        QVERIFY2(d.media.changed().isEmpty(), qPrintable(d.media.changed()));
    }
    void anExportedItemIsNeverOfferedForReplacing() {
        OpenedDialog d;
        QVERIFY(d.open());
        QVERIFY(setDestination(d.h, d.media.paths[1]));
        click(d.h, "exportStart");
        QTRY_VERIFY_WITH_TIMEOUT(visibleItem(d.h, "exportError"), 10000);
        QCOMPARE(phaseOf(d.h), QString("ready"));
        QVERIFY(textOf(d.h, "exportErrorText").contains("can't be replaced"));
        QVERIFY(!visibleItem(d.h, "exportReplace"));
        QVERIFY2(d.media.changed().isEmpty(), qPrintable(d.media.changed()));
    }
    // A real permission refusal from the operating system, on a generated folder.
    void unwritableDestinationBecomesAClearMessage() {
        OpenedDialog d;
        QVERIFY(d.open());
        QTemporaryDir out;
        const QString folder = out.filePath("locked");
        QVERIFY(QDir().mkpath(folder));
#ifdef Q_OS_WIN
        const QString native = QDir::toNativeSeparators(folder);
        QCOMPARE(QProcess::execute("icacls", {native, "/deny", "*S-1-1-0:(WD,AD)"}), 0);
        auto restore = qScopeGuard([&] { QProcess::execute("icacls", {native, "/remove:d", "*S-1-1-0"}); });
#else
        QVERIFY(QFile::setPermissions(folder, QFile::ReadOwner | QFile::ExeOwner));
        auto restore = qScopeGuard([&] { QFile::setPermissions(folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner); });
#endif
        {
            QFile probe(folder + "/probe.tmp");
            if (probe.open(QIODevice::WriteOnly)) { probe.close(); probe.remove(); QSKIP("This account can write anywhere (root in a container)."); }
        }
        QVERIFY(setDestination(d.h, folder + "/list.csv"));
        click(d.h, "exportStart");
        QTRY_VERIFY_WITH_TIMEOUT(visibleItem(d.h, "exportError"), 10000);
        QCOMPARE(phaseOf(d.h), QString("ready"));
        QVERIFY(textOf(d.h, "exportErrorText").startsWith("That location can't be written to"));
        QVERIFY(item(d.h, "exportDestination")->hasActiveFocus());
        QCOMPARE(item(d.h, "exportDestination")->property("text").toString(), folder + "/list.csv");
        QVERIFY(!QFile::exists(folder + "/list.csv"));
        // Editing the path clears the message.
        QTest::keyClick(d.h.window, Qt::Key_A);
        QTRY_VERIFY_WITH_TIMEOUT(!visibleItem(d.h, "exportError"), 10000);
    }
    void engineExceptionBecomesAClearMessage() {
        OpenedDialog d;
        QVERIFY(d.open());
        QTemporaryDir out;
        QVERIFY(setDestination(d.h, out.filePath("list.csv")));
        serviceOf(d.h)->setEngine([](const ExportRequest&, std::stop_token, const SelectionExportService::Progress&) -> ExportResult {
            throw std::runtime_error("no mapping for the character");
        });
        click(d.h, "exportStart");
        QTRY_VERIFY_WITH_TIMEOUT(visibleItem(d.h, "exportError"), 10000);
        QCOMPARE(textOf(d.h, "exportErrorText"), QString("The export stopped because of an unexpected problem. Nothing was written."));
        QCOMPARE(textOf(d.h, "exportErrorDetail"), QString("no mapping for the character"));
        QVERIFY(dialogOpen(d.h));
        QVERIFY(!QFile::exists(out.filePath("list.csv")));
    }
    // The dialog exports what it captured. Changing the selection behind it only offers to switch.
    void selectionChangeBehindTheDialogNeverChangesTheExport() {
        OpenedDialog d;
        QVERIFY(d.open({}));
        QTemporaryDir out;
        const QString first = out.filePath("first.csv");
        QVERIFY(setDestination(d.h, first));
        QVERIFY(!visibleItem(d.h, "exportSelectionChanged"));
        d.h.fileGrid.clearSelection();
        d.h.fileGrid.select(d.h.fileGrid.rowOf(d.media.paths[0]));
        QTRY_VERIFY_WITH_TIMEOUT(visibleItem(d.h, "exportSelectionChanged"), 10000);
        QCOMPARE(textOf(d.h, "exportSelectionNote"), QString("The selection is now 1 item. This export still uses the 3 items you started with."));
        QCOMPARE(textOf(d.h, "exportSummary"), QString("3 files to export"));
        // Using the current selection is explicit, and it re-checks the items.
        click(d.h, "exportUseCurrent");
        QTRY_COMPARE_WITH_TIMEOUT(textOf(d.h, "exportSummary"), QString("1 file to export"), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(phaseOf(d.h), QString("ready"), 10000);
        QVERIFY(!visibleItem(d.h, "exportSelectionChanged"));
        click(d.h, "exportStart");
        QTRY_COMPARE_WITH_TIMEOUT(phaseOf(d.h), QString("done"), 10000);
        const QByteArray csv = readAll(first);
        QVERIFY(csv.contains("img1.png"));
        QVERIFY(!csv.contains("img2.png"));
    }
    void selectionEmptiedBehindTheDialogStillExportsTheCapture() {
        OpenedDialog d;
        QVERIFY(d.open());
        QTemporaryDir out;
        const QString destination = out.filePath("list.csv");
        QVERIFY(setDestination(d.h, destination));
        // No selection and no open photo: nothing to switch to, and the captured items still export.
        QVERIFY(flowOf(d.h)->setProperty("targetsProvider", QVariant::fromValue(d.h.engine.evaluate("(function () { return [] })"))));
        d.h.fileGrid.clearSelection();
        QTRY_VERIFY_WITH_TIMEOUT(visibleItem(d.h, "exportSelectionChanged"), 10000);
        QCOMPARE(textOf(d.h, "exportSelectionNote"), QString("The selection is now empty. This export still uses the 3 items you started with."));
        QVERIFY(!visibleItem(d.h, "exportUseCurrent"));
        click(d.h, "exportStart");
        QTRY_COMPARE_WITH_TIMEOUT(phaseOf(d.h), QString("done"), 10000);
        QCOMPARE(textOf(d.h, "exportResultText"), QString("Exported 3 items to “list.csv”."));
    }
    void itemsLeftOutAreNamedWithTheirReason() {
        OpenedDialog d;
        QVERIFY(d.open());
        QTemporaryDir out;
        QVERIFY(QFile::remove(d.media.paths[2]));
        QVERIFY(setDestination(d.h, out.filePath("list.csv")));
        click(d.h, "exportStart");
        QTRY_COMPARE_WITH_TIMEOUT(phaseOf(d.h), QString("done"), 10000);
        QCOMPARE(textOf(d.h, "exportResultText"), QString("Exported 2 items to “list.csv”."));
        QCOMPARE(textOf(d.h, "exportSkippedTitle"), QString("Left out (1):"));
        QVERIFY(visibleItem(d.h, "exportSkipped"));
    }
    void changingTheFileTypeFollowsThroughToThePathAndOptions() {
        OpenedDialog d;
        QVERIFY(d.open());
        QTemporaryDir out;
        QVERIFY(setDestination(d.h, out.filePath("list.csv")));
        QVERIFY(visibleItem(d.h, "exportFormulas"));
        QQuickItem* format = item(d.h, "exportFormat");
        QVERIFY(format->setProperty("currentIndex", 1));
        QVERIFY(QMetaObject::invokeMethod(format, "activated", Q_ARG(int, 1)));
        QCOMPARE(item(d.h, "exportDestination")->property("text").toString(), out.filePath("list.txt"));
        QVERIFY(!visibleItem(d.h, "exportFormulas"));
        QVERIFY(item(d.h, "exportPaths")->setProperty("currentIndex", 1));
        click(d.h, "exportStart");
        QTRY_COMPARE_WITH_TIMEOUT(phaseOf(d.h), QString("done"), 10000);
        QCOMPARE(readAll(out.filePath("list.txt")), QByteArray("img1.png\nimg2.png\nimg3.png\n"));
    }
    void aFileChosenInTheSaveDialogFillsTheField() {
        OpenedDialog d;
        QVERIFY(d.open());
        QTemporaryDir out;
        QVERIFY(QMetaObject::invokeMethod(flowOf(d.h), "applyChosenFile", Q_ARG(QVariant, QUrl::fromLocalFile(out.filePath("chosen.csv")))));
        QCOMPARE(QDir::fromNativeSeparators(item(d.h, "exportDestination")->property("text").toString()), out.filePath("chosen.csv"));
    }
    void keyboardOperableWithAccessibleNames() {
        OpenedDialog d;
        QVERIFY(d.open());
        QTemporaryDir out;
        QQuickItem* field = item(d.h, "exportDestination");
        // Opening puts the focus in the path with the base name selected, as a save dialog does.
        QTRY_VERIFY_WITH_TIMEOUT(field->hasActiveFocus(), 10000);
        QCOMPARE(field->property("selectedText").toString(), QString("selection"));
        // Every stop is reachable with Tab and has a name.
        QSet<QString> reached;
        for (int i = 0; i < 12; ++i) {
            QTest::keyClick(d.h.window, Qt::Key_Tab);
            QQuickItem* now = d.h.window->activeFocusItem();
            QVERIFY(now);
            reached << now->objectName();
        }
        for (const char* stop : {"exportFormat", "exportPaths", "exportDestination", "exportBrowse", "exportFormulas", "exportCancel", "exportStart"}) {
            QVERIFY2(reached.contains(stop), stop);
            QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(item(d.h, stop));
            QVERIFY2(accessible && !accessible->text(QAccessible::Name).isEmpty(), stop);
        }
        // Enter in the path starts the export.
        QVERIFY(setDestination(d.h, out.filePath("keys.csv")));
        field->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(d.h.window, Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(phaseOf(d.h), QString("done"), 10000);
        QVERIFY(QFile::exists(out.filePath("keys.csv")));
        // Enter closes the result, and the focus goes back to the content.
        QTest::keyClick(d.h.window, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!d.h.window->property("modalOpen").toBool(), 10000);
    }
    void escapeClosesAnIdleDialog() {
        OpenedDialog d;
        QVERIFY(d.open());
        QTest::keyClick(d.h.window, Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!d.h.window->property("modalOpen").toBool(), 10000);
        QCOMPARE(phaseOf(d.h), QString());
    }
    // The dialog fits and nothing is clipped at the narrow and wide window sizes. GLANCELOT_B3_EVIDENCE saves captures.
    void fitsAtNarrowAndWideWindows_data() {
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("height");
        QTest::newRow("560") << 560 << 640;
        QTest::newRow("560 short") << 560 << 420;
        QTest::newRow("1180") << 1180 << 780;
    }
    void fitsAtNarrowAndWideWindows() {
        QFETCH(int, width);
        QFETCH(int, height);
        OpenedDialog d;
        QVERIFY(d.open({}, QSize(width, height)));
        QTRY_COMPARE_WITH_TIMEOUT(d.h.window->width(), width, 10000);
        QTemporaryDir out;
        QVERIFY(setDestination(d.h, out.filePath("a-long-folder-name-for-the-layout-check/that-does-not-exist/selection.csv")));
        QObject* dialog = dialogOf(d.h);
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(), 10000);
        auto box = [](QQuickItem* i) { return i->mapRectToScene(QRectF(0, 0, i->width(), i->height())); };
        const auto check = [&] {
            const QRectF frame(dialog->property("x").toReal(), dialog->property("y").toReal(), dialog->property("width").toReal(), dialog->property("height").toReal());
            QVERIFY2(frame.left() >= 0 && frame.right() <= width, qPrintable(QString("dialog %1..%2 of %3").arg(frame.left()).arg(frame.right()).arg(width)));
            QVERIFY2(frame.top() >= 0 && frame.bottom() <= height, qPrintable(QString("dialog %1..%2 of %3").arg(frame.top()).arg(frame.bottom()).arg(height)));
            for (const char* name : {"exportFormat", "exportPaths", "exportDestination", "exportBrowse", "exportFormulas", "exportStart", "exportCancel", "exportDone", "exportReveal", "exportReplace", "exportKeep"}) {
                QQuickItem* control = item(d.h, name);
                if (!control || !control->isVisible()) continue;
                const QRectF rect = box(control);
                QVERIFY2(rect.left() >= 0 && rect.right() <= width, name);
                QVERIFY2(rect.width() >= 40 && rect.height() >= 24, name);
            }
            // The state that needs an answer or shows progress is in view above the buttons, in every window size.
            QQuickItem* buttons = item(d.h, "exportStart")->isVisible() ? item(d.h, "exportStart") : item(d.h, "exportKeep")->isVisible() ? item(d.h, "exportKeep") : item(d.h, "exportStop");
            for (const char* name : {"exportReplaceQuestion", "exportProgress", "exportStatus"}) {
                QQuickItem* block = item(d.h, name);
                if (!block || !block->isVisible() || !buttons->isVisible()) continue;
                QVERIFY2(box(block).top() >= frame.top() && box(block).bottom() <= box(buttons).top(), name);
            }
        };
        const auto capture = [&](const QString& state) {
            const QString folder = qEnvironmentVariable("GLANCELOT_B3_EVIDENCE");
            if (folder.isEmpty()) return;
            QVERIFY(QDir().mkpath(folder));
            QTest::qWait(250); // sleep-ok: lets the popup's enter transition finish before a capture
            QVERIFY(d.h.window->grabWindow().save(QDir(folder).filePath(QString("export-%1-%2x%3.png").arg(state).arg(width).arg(height))));
        };
        check();
        QVERIFY(!QTest::currentTestFailed());
        capture("form");
        // The refused state, whose message grows the form.
        click(d.h, "exportStart");
        QTRY_VERIFY_WITH_TIMEOUT(visibleItem(d.h, "exportError"), 10000);
        check();
        QVERIFY(!QTest::currentTestFailed());
        capture("error");
        // The replace question.
        const QString destination = out.filePath("selection.csv");
        writeText(destination, "earlier");
        QVERIFY(setDestination(d.h, destination));
        click(d.h, "exportStart");
        QTRY_COMPARE_WITH_TIMEOUT(phaseOf(d.h), QString("replace"), 10000);
        QVERIFY(visibleItem(d.h, "exportReplaceQuestion"));
        (void)d.h.window->grabWindow();
        check();
        QVERIFY(!QTest::currentTestFailed());
        capture("replace");
        click(d.h, "exportKeep");
        QTRY_COMPARE_WITH_TIMEOUT(phaseOf(d.h), QString("ready"), 10000);
        // A running export.
        HeldEngine held;
        serviceOf(d.h)->setEngine(held.engine());
        QVERIFY(setDestination(d.h, out.filePath("running.csv")));
        click(d.h, "exportStart");
        QTRY_VERIFY_WITH_TIMEOUT(held.entered->load(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(visibleItem(d.h, "exportProgress"), 10000);
        (void)d.h.window->grabWindow();
        check();
        QVERIFY(!QTest::currentTestFailed());
        capture("running");
        *held.release = true;
        QTRY_COMPARE_WITH_TIMEOUT(phaseOf(d.h), QString("done"), 10000);
        check();
        QVERIFY(!QTest::currentTestFailed());
        capture("done");
    }
};

QTEST_MAIN(ExportUiTests)
#include "export_ui_tests.moc"
