#include "glancelot/filmstrip_model.hpp"
#include "glancelot/thumbnail_service.hpp"
#include "thumbnail_id.hpp"
#include "thumbnail_provider.hpp"
#include <QColorSpace>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QProcess>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include <cstdio>
#include <cstring>

#ifndef GLANCELOT_DECODER_PATH
#error "GLANCELOT_DECODER_PATH must name the glancelot-decoder executable"
#endif

namespace {
QString writePng(const QString& directory, const QString& name, const QSize& size) {
    QImage image(size, QImage::Format_RGBA8888);
    image.fill(QColor(30, 120, 220));
    for (int y = 0; y < size.height(); ++y)
        for (int x = 0; x < size.width() / 2; ++x) image.setPixelColor(x, y, Qt::red);
    image.setColorSpace(QColorSpace::SRgb);
    const auto path = directory + "/" + name;
    return image.save(path, "PNG") ? path : QString{};
}
QString writeJpeg(const QString& directory, const QString& name, const QSize& size) {
    QImage image(size, QImage::Format_RGB32);
    image.fill(QColor(200, 160, 40));
    image.setColorSpace(QColorSpace::SRgb);
    const auto path = directory + "/" + name;
    return image.save(path, "JPEG", 90) ? path : QString{};
}
// 30x20 JPEG carrying a minimal EXIF IFD with the requested orientation.
QString writeOrientedJpeg(const QString& directory, int orientation) {
    QImage image(30, 20, QImage::Format_RGB32);
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 30; ++x) image.setPixelColor(x, y, y < 10 ? (x < 15 ? Qt::red : Qt::green) : (x < 15 ? Qt::blue : Qt::yellow));
    image.setColorSpace(QColorSpace::SRgb);
    const auto base = directory + "/orientation-source.jpg";
    if (!image.save(base, "JPEG", 100)) return {};
    QFile input(base);
    if (!input.open(QIODevice::ReadOnly)) return {};
    const auto bytes = input.readAll();
    auto exif = QByteArray::fromHex("45786966000049492a0008000000010012010300010000000600000000000000");
    exif[24] = char(orientation);
    QByteArray segment = QByteArray::fromHex("ffe1");
    const auto length = exif.size() + 2;
    segment.append(char(length >> 8)); segment.append(char(length & 255)); segment += exif;
    const auto path = directory + QString("/oriented-%1.jpg").arg(orientation);
    QFile output(path);
    if (!output.open(QIODevice::WriteOnly)) return {};
    output.write(bytes.first(2)); output.write(segment); output.write(bytes.sliced(2));
    output.close(); input.close(); QFile::remove(base);
    return path;
}
QStringList listing(const QString& directory) {
    QStringList rows;
    for (const auto& info : QDir(directory).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        rows << info.fileName() + '|' + QString::number(info.size()) + '|' + QString::number(info.lastModified().toMSecsSinceEpoch());
    return rows;
}

struct Collector {
    QHash<quint64, QImage> ready;
    QHash<quint64, QString> failed;
    QHash<quint64, int> calls;
    explicit Collector(ThumbnailService& service) {
        QObject::connect(&service, &ThumbnailService::ready, &service, [this](quint64 id, const QImage& image) { ready.insert(id, image); ++calls[id]; });
        QObject::connect(&service, &ThumbnailService::failed, &service, [this](quint64 id, const QString& message) { failed.insert(id, message); ++calls[id]; });
    }
    bool done(quint64 id) const { return calls.value(id) > 0; }
};

// The test binary doubles as a misbehaving worker so protocol violations and hangs are deterministic.
int fakeWorker() {
    const QByteArray mode = qgetenv("GLANCELOT_FAKE_WORKER");
    char line[8192];
    if (!std::fgets(line, sizeof(line), stdin)) return 0;
    QByteArray id = "0";
    const QByteArray request(line);
    const int at = request.indexOf("\"id\":");
    if (at >= 0) id = request.mid(at + 5).split(',').first().split('}').first();
    auto out = [](const QByteArray& bytes) { std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout); std::fflush(stdout); };
    if (mode == "hang") { for (;;) QThread::sleep(1); }
    if (mode == "garbage") out("this is not json\n");
    else if (mode == "badgeometry") out("{\"id\":" + id + ",\"phase\":\"thumb\",\"width\":4,\"height\":4,\"bytes\":10}\n");
    else if (mode == "oversize") out("{\"id\":" + id + ",\"phase\":\"thumb\",\"width\":100000,\"height\":100000,\"bytes\":40000000000}\n");
    else if (mode == "wrongid") out("{\"id\":987654,\"phase\":\"thumb\",\"width\":1,\"height\":1,\"bytes\":4}\n" + QByteArray(4, 'x'));
    else if (mode == "truncated") { out("{\"id\":" + id + ",\"phase\":\"thumb\",\"width\":4,\"height\":4,\"bytes\":64}\n" + QByteArray(8, 'x')); return 0; }
    else if (mode == "floodnonewline") { for (int i = 0; i < 4; ++i) out(QByteArray(4096, 'a')); QThread::sleep(30); }
    else if (mode == "crash") std::abort();
    QThread::sleep(30);
    return 0;
}
}

class ThumbnailTests final : public QObject {
    Q_OBJECT
    static QString decoder() { return QString::fromUtf8(GLANCELOT_DECODER_PATH); }
private slots:
    void init() { QTest::failOnWarning(QRegularExpression("QProcess: Destroyed while process.*still running.*")); }
    void validJpegAndPngAreBoundedAndKeepAspect();
    void smallImagesAreNotUpscaled();
    void exifOrientationAffectsThumbnailAspect_data();
    void exifOrientationAffectsThumbnailAspect();
    void malformedAndMissingFilesFailThenServiceContinues();
    void queueIsBounded();
    void cancelledRequestsNeverReport();
    void staleEpochResultsAreDropped();
    void cacheServesRepeatsUntilEpochChanges();
    void killedWorkerIsReplacedAndBatchContinues();
    void sourceDirectoryIsNeverModified();
    void missingWorkerProgramFails();
    void misbehavingWorkersFailTheJobOnly_data();
    void misbehavingWorkersFailTheJobOnly();
    void shutdownLeavesNoProcesses();
    void filmstripModelKeepsRowsWhileStepping();
    void thumbnailIdRoundTripsAwkwardPaths();
    void providerDeliversAndCancels();
};

void ThumbnailTests::validJpegAndPngAreBoundedAndKeepAspect() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto png = writePng(dir.path(), "wide.png", QSize(400, 200));
    const auto jpg = writeJpeg(dir.path(), "tall.jpg", QSize(150, 600));
    QVERIFY(!png.isEmpty() && !jpg.isEmpty());
    ThumbnailService service(decoder()); Collector got(service);
    const auto a = service.request(png, 64, 1), b = service.request(jpg, 64, 1);
    QVERIFY(a && b && a != b);
    QTRY_VERIFY_WITH_TIMEOUT(got.done(a) && got.done(b), 20000);
    QVERIFY2(got.ready.contains(a), qPrintable(got.failed.value(a)));
    QVERIFY2(got.ready.contains(b), qPrintable(got.failed.value(b)));
    QCOMPARE(got.ready[a].size(), QSize(64, 32));
    QCOMPARE(got.ready[b].size(), QSize(16, 64));
    QCOMPARE(got.ready[a].format(), QImage::Format_RGBA8888);
    QCOMPARE(got.ready[a].pixelColor(2, 16), QColor(Qt::red));
    QCOMPARE(got.calls[a], 1); QCOMPARE(got.calls[b], 1);
    QVERIFY(service.cacheBytes() > 0 && service.cacheBytes() <= ThumbnailService::kCacheBytes);
    service.shutdown();
}

void ThumbnailTests::smallImagesAreNotUpscaled() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto png = writePng(dir.path(), "small.png", QSize(17, 11));
    ThumbnailService service(decoder()); Collector got(service);
    const auto id = service.request(png, 160, 1);
    QTRY_VERIFY_WITH_TIMEOUT(got.done(id), 20000);
    QCOMPARE(got.ready.value(id).size(), QSize(17, 11));
    // Out-of-range edges are clamped, never trusted.
    const auto huge = service.request(writePng(dir.path(), "big.png", QSize(900, 300)), 100000, 1);
    QTRY_VERIFY_WITH_TIMEOUT(got.done(huge), 20000);
    QCOMPARE(got.ready.value(huge).size(), QSize(256, 85));
    service.shutdown();
}

void ThumbnailTests::exifOrientationAffectsThumbnailAspect_data() {
    QTest::addColumn<int>("orientation");
    for (int value = 1; value <= 8; ++value) QTest::newRow(qPrintable(QString("exif%1").arg(value))) << value;
}

void ThumbnailTests::exifOrientationAffectsThumbnailAspect() {
    QFETCH(int, orientation);
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto path = writeOrientedJpeg(dir.path(), orientation);
    QVERIFY(!path.isEmpty());
    ThumbnailService service(decoder()); Collector got(service);
    const auto id = service.request(path, 16, 1);
    QTRY_VERIFY_WITH_TIMEOUT(got.done(id), 20000);
    QVERIFY2(got.ready.contains(id), qPrintable(got.failed.value(id)));
    const QSize size = got.ready[id].size();
    // Stored 30x20; orientations 5-8 rotate a quarter turn so the displayed image is portrait.
    if (orientation < 5) { QVERIFY2(size.width() > size.height(), qPrintable(QString("%1x%2").arg(size.width()).arg(size.height()))); QCOMPARE(size.width(), 16); }
    else { QVERIFY2(size.height() > size.width(), qPrintable(QString("%1x%2").arg(size.width()).arg(size.height()))); QCOMPARE(size.height(), 16); }
    service.shutdown();
}

void ThumbnailTests::malformedAndMissingFilesFailThenServiceContinues() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    QFile bad(dir.filePath("bad.jpg")); QVERIFY(bad.open(QIODevice::WriteOnly)); bad.write("\xff\xd8\xff garbage that is not a jpeg"); bad.close();
    QFile text(dir.filePath("note.png")); QVERIFY(text.open(QIODevice::WriteOnly)); text.write("plain text"); text.close();
    const auto good = writePng(dir.path(), "good.png", QSize(100, 100));
    ThumbnailService service(decoder()); Collector got(service);
    const auto a = service.request(bad.fileName(), 64, 1);
    const auto b = service.request(dir.filePath("missing.png"), 64, 1);
    const auto c = service.request(text.fileName(), 64, 1);
    const auto d = service.request(good, 64, 1);
    QTRY_VERIFY_WITH_TIMEOUT(got.done(a) && got.done(b) && got.done(c) && got.done(d), 20000);
    QVERIFY(got.failed.contains(a)); QVERIFY(got.failed.contains(b)); QVERIFY(got.failed.contains(c));
    QVERIFY2(got.ready.contains(d), qPrintable(got.failed.value(d)));
    QVERIFY(!got.failed.value(a).isEmpty());
    service.shutdown();
}

void ThumbnailTests::queueIsBounded() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto png = writePng(dir.path(), "q.png", QSize(64, 64));
    ThumbnailService service(decoder());
    QList<quint64> accepted;
    int rejected = 0;
    for (int i = 0; i < ThumbnailService::kMaxQueue + 50; ++i) {
        // Distinct paths defeat de-duplication through the cache.
        const auto id = service.request(png + QString::number(i), 64, 1);
        if (id) accepted << id; else ++rejected;
    }
    QVERIFY(accepted.size() <= ThumbnailService::kMaxQueue);
    QVERIFY(rejected >= 50);
    QVERIFY(service.pending() + service.active() <= ThumbnailService::kMaxQueue);
    for (const auto id : accepted) service.cancel(id);
    QCOMPARE(service.pending(), 0);
    service.shutdown();
}

void ThumbnailTests::cancelledRequestsNeverReport() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto png = writePng(dir.path(), "c.png", QSize(300, 300));
    ThumbnailService service(decoder()); Collector got(service);
    QList<quint64> ids;
    for (int i = 0; i < 6; ++i) ids << service.request(png, 64, 1);
    for (const auto id : ids) service.cancel(id); // includes the one already written to the worker
    const auto keeper = service.request(png, 32, 1);
    QTRY_VERIFY_WITH_TIMEOUT(got.done(keeper), 20000);
    QTest::qWait(300);
    for (const auto id : ids) QVERIFY2(!got.done(id), "cancelled request reported a result");
    QVERIFY(got.ready.contains(keeper));
    service.shutdown();
}

void ThumbnailTests::staleEpochResultsAreDropped() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto png = writePng(dir.path(), "e.png", QSize(300, 300));
    ThumbnailService service(decoder()); Collector got(service);
    QList<quint64> oldIds;
    for (int i = 0; i < 5; ++i) oldIds << service.request(png + QString::number(i), 64, 1);
    service.setEpoch(2);
    QCOMPARE(service.epoch(), quint64(2));
    QCOMPARE(service.pending(), 0);
    QCOMPARE(service.request(png, 64, 1), quint64(0)); // stale epoch rejected outright
    const auto fresh = service.request(png, 64, 2);
    QVERIFY(fresh);
    QTRY_VERIFY_WITH_TIMEOUT(got.done(fresh), 20000);
    QTest::qWait(300);
    for (const auto id : oldIds) QVERIFY2(!got.done(id), "stale-epoch request reported a result");
    service.shutdown();
}

void ThumbnailTests::cacheServesRepeatsUntilEpochChanges() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto path = writePng(dir.path(), "r.png", QSize(200, 100));
    ThumbnailService service(decoder()); Collector got(service);
    const auto first = service.request(path, 64, 1);
    QTRY_VERIFY_WITH_TIMEOUT(got.done(first), 20000);
    QCOMPARE(got.ready[first].size(), QSize(64, 32));
    QVERIFY(QFile::remove(path));
    writePng(dir.path(), "r.png", QSize(100, 200));
    const auto cached = service.request(path, 64, 1);
    QTRY_VERIFY_WITH_TIMEOUT(got.done(cached), 5000);
    QCOMPARE(got.ready[cached].size(), QSize(64, 32)); // served from memory without touching the file
    QCOMPARE(service.active(), 0);
    service.setEpoch(2);
    QCOMPARE(service.cacheBytes(), qint64(0));
    const auto refreshed = service.request(path, 64, 2);
    QTRY_VERIFY_WITH_TIMEOUT(got.done(refreshed), 20000);
    QCOMPARE(got.ready[refreshed].size(), QSize(32, 64));
    service.shutdown();
}

void ThumbnailTests::killedWorkerIsReplacedAndBatchContinues() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto png = writePng(dir.path(), "k.png", QSize(120, 120));
    ThumbnailService service(decoder()); Collector got(service);
    const auto warm = service.request(png, 64, 1);
    QTRY_VERIFY_WITH_TIMEOUT(got.done(warm), 20000);
    QList<quint64> ids;
    for (int i = 0; i < 6; ++i) ids << service.request(png + QString::number(i) + ".missing", 64, 1);
    const auto last = service.request(png, 48, 1);
    for (auto* process : service.findChildren<QProcess*>()) process->kill();
    QTRY_VERIFY_WITH_TIMEOUT(got.done(last), 20000);
    for (const auto id : ids) QTRY_VERIFY_WITH_TIMEOUT(got.done(id), 20000);
    QVERIFY2(got.ready.contains(last), qPrintable(got.failed.value(last)));
    for (const auto id : ids) QCOMPARE(got.calls[id], 1);
    service.shutdown();
}

void ThumbnailTests::sourceDirectoryIsNeverModified() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto a = writePng(dir.path(), "a.png", QSize(80, 60)), b = writeJpeg(dir.path(), "b.jpg", QSize(80, 60));
    QFile bad(dir.filePath("bad.png")); QVERIFY(bad.open(QIODevice::WriteOnly)); bad.write("nope"); bad.close();
    const auto before = listing(dir.path());
    ThumbnailService service(decoder()); Collector got(service);
    const auto x = service.request(a, 64, 1), y = service.request(b, 64, 1), z = service.request(bad.fileName(), 64, 1);
    QTRY_VERIFY_WITH_TIMEOUT(got.done(x) && got.done(y) && got.done(z), 20000);
    service.shutdown();
    QCOMPARE(listing(dir.path()), before);
}

void ThumbnailTests::missingWorkerProgramFails() {
    ThumbnailService service(QDir::tempPath() + "/glancelot-no-such-worker.exe"); Collector got(service);
    const auto id = service.request("C:/does/not/matter.png", 64, 1);
    QVERIFY(id);
    QTRY_VERIFY_WITH_TIMEOUT(got.done(id), 20000);
    QVERIFY(got.failed.contains(id));
    QVERIFY(got.failed[id].contains("missing", Qt::CaseInsensitive));
    // Later requests keep failing cleanly rather than wedging the queue.
    const auto again = service.request("C:/does/not/matter-either.png", 64, 1);
    QTRY_VERIFY_WITH_TIMEOUT(got.done(again), 20000);
    service.shutdown();
}

void ThumbnailTests::misbehavingWorkersFailTheJobOnly_data() {
    QTest::addColumn<QString>("mode");
    for (const char* mode : {"hang", "garbage", "badgeometry", "oversize", "wrongid", "truncated", "floodnonewline", "crash"})
        QTest::newRow(mode) << QString::fromLatin1(mode);
}

void ThumbnailTests::misbehavingWorkersFailTheJobOnly() {
    QFETCH(QString, mode);
    qputenv("GLANCELOT_FAKE_WORKER", mode.toLatin1());
    ThumbnailService service(QCoreApplication::applicationFilePath()); Collector got(service);
    service.setDeadlineMs(mode == "hang" || mode == "floodnonewline" ? 400 : 5000);
    const auto first = service.request("C:/fake/one.png", 64, 1), second = service.request("C:/fake/two.png", 64, 1);
    QTRY_VERIFY_WITH_TIMEOUT(got.done(first) && got.done(second), 20000);
    QVERIFY2(got.failed.contains(first), "a protocol violation must fail the job, not produce an image");
    QVERIFY(got.failed.contains(second));
    QCOMPARE(got.calls[first], 1); QCOMPARE(got.calls[second], 1);
    QVERIFY(got.ready.isEmpty());
    QCOMPARE(service.active(), 0);
    service.shutdown();
    qunsetenv("GLANCELOT_FAKE_WORKER");
}

void ThumbnailTests::shutdownLeavesNoProcesses() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto png = writePng(dir.path(), "s.png", QSize(50, 50));
    ThumbnailService service(decoder()); Collector got(service);
    service.setWorkerCount(2);
    const auto a = service.request(png, 64, 1), b = service.request(png + "x", 64, 1);
    QTRY_VERIFY_WITH_TIMEOUT(got.done(a) && got.done(b), 20000);
    QVERIFY(!service.findChildren<QProcess*>().isEmpty());
    service.shutdown();
    for (auto* process : service.findChildren<QProcess*>()) QVERIFY(process->state() == QProcess::NotRunning);
    QCOMPARE(service.pending(), 0); QCOMPARE(service.active(), 0);
    // The temporary media can now be removed on Windows without a lingering worker handle.
}

void ThumbnailTests::filmstripModelKeepsRowsWhileStepping() {
    FilmstripModel model;
    glancelot::NavigationSnapshot snapshot;
    snapshot.directory = std::filesystem::path(u8"C:/photos/trip");
    for (const char* name : {"a.jpg", "b.jpg", "c.png"}) snapshot.files.push_back(snapshot.directory / name);
    snapshot.current = snapshot.files[0];
    QSignalSpy resets(&model, &QAbstractItemModel::modelReset), changed(&model, &QAbstractItemModel::dataChanged), rows(&model, &FilmstripModel::currentRowChanged);
    model.sync(snapshot);
    QCOMPARE(model.count(), 3); QCOMPARE(model.currentRow(), 0); QCOMPARE(resets.size(), 1);
    QCOMPARE(model.data(model.index(2), FilmstripModel::NameRole).toString(), QString("c.png"));
    snapshot.current = snapshot.files[1]; ++snapshot.generation;
    model.sync(snapshot);
    QCOMPARE(resets.size(), 1); // stepping must not rebuild delegates
    QCOMPARE(changed.size(), 2);
    QCOMPARE(model.currentRow(), 1);
    QVERIFY(model.data(model.index(1), FilmstripModel::CurrentRole).toBool());
    QVERIFY(!model.data(model.index(0), FilmstripModel::CurrentRole).toBool());
    model.sync(snapshot); // unchanged
    QCOMPARE(changed.size(), 2);
    snapshot.files.push_back(snapshot.directory / "d.jpg");
    model.sync(snapshot);
    QCOMPARE(resets.size(), 2); QCOMPARE(model.count(), 4);
    snapshot.directory = std::filesystem::path(u8"C:/photos/other");
    snapshot.files = {snapshot.directory / "z.jpg"}; snapshot.current = snapshot.files[0];
    model.sync(snapshot);
    QCOMPARE(resets.size(), 3); QCOMPARE(model.count(), 1); QCOMPARE(model.currentRow(), 0);
    glancelot::NavigationSnapshot empty;
    model.sync(empty);
    QCOMPARE(model.count(), 0); QCOMPARE(model.currentRow(), -1);
    QSignalSpy epochs(&model, &FilmstripModel::epochChanged);
    const auto before = model.epoch(); model.invalidate();
    QCOMPARE(model.epoch(), before + 1); QCOMPARE(epochs.size(), 1);
    QVERIFY(model.pathAt(99).isEmpty());
}

void ThumbnailTests::thumbnailIdRoundTripsAwkwardPaths() {
    const QStringList paths{"C:/Users/Zoë Ångström/Pictures/100% #1 & (copy)?.jpg", "/mnt/photos/日本語/写真 01.png", "C:\\a b\\c%2Fd.png", "/tmp/with+plus=eq.jpg"};
    FilmstripModel model;
    for (const auto& path : paths) {
        const auto source = model.thumbnailSource(path, 160, 7);
        QVERIFY(source.startsWith("image://thumb/"));
        const auto parts = thumbnail_id::decode(source.mid(QString("image://thumb/").size()));
        QVERIFY(parts.has_value());
        QCOMPARE(parts->path, path); QCOMPARE(parts->epoch, quint64(7)); QCOMPARE(parts->edge, 160);
        QVERIFY(!source.mid(14).contains('%'));
    }
    QVERIFY(!thumbnail_id::decode("garbage").has_value());
    QVERIFY(!thumbnail_id::decode("1/160/!!!").has_value());
    QVERIFY(!thumbnail_id::decode("x/160/YQ").has_value());
}

void ThumbnailTests::providerDeliversAndCancels() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto png = writePng(dir.path(), "p.png", QSize(320, 160));
    ThumbnailService service(decoder());
    ThumbnailProvider provider(&service);
    std::unique_ptr<QQuickImageResponse> ok(provider.requestImageResponse(thumbnail_id::encode(1, 80, png), {}));
    QSignalSpy finished(ok.get(), &QQuickImageResponse::finished);
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
    QVERIFY2(ok->errorString().isEmpty(), qPrintable(ok->errorString()));
    std::unique_ptr<QQuickTextureFactory> texture(ok->textureFactory());
    QVERIFY(texture); QCOMPARE(texture->textureSize(), QSize(80, 40));

    // A retargeted/destroyed Image cancels: the response still finishes (so Qt can clean it up) with no texture.
    std::unique_ptr<QQuickImageResponse> cancelled(provider.requestImageResponse(thumbnail_id::encode(1, 80, png + ".never"), {}));
    QSignalSpy cancelFinished(cancelled.get(), &QQuickImageResponse::finished);
    cancelled->cancel();
    QTRY_COMPARE_WITH_TIMEOUT(cancelFinished.size(), 1, 20000);
    QVERIFY(!cancelled->textureFactory());

    std::unique_ptr<QQuickImageResponse> bad(provider.requestImageResponse("not-a-valid-id", {}));
    QSignalSpy badFinished(bad.get(), &QQuickImageResponse::finished);
    QTRY_COMPARE_WITH_TIMEOUT(badFinished.size(), 1, 20000);
    QVERIFY(!bad->errorString().isEmpty()); QVERIFY(!bad->textureFactory());
    service.shutdown();
}

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--thumbnail-server") == 0) return fakeWorker();
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    ThumbnailTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "thumbnail_tests.moc"
