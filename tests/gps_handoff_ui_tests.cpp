// F3 (F072): GPS validation on generated EXIF, map URL building, saved provider choice, and the shipped metadata
// inspector, command and Settings page. Every URL goes through GpsMapHandoff's opener seam: no test reaches a browser
// or the network. Offscreen/software rendering proves logic and layout, not a real browser handoff.
#include "gps_map_handoff.hpp"
#include "glancelot/metadata_parser.hpp"
#include "glancelot/settings_file.hpp"
#include "ui_test_support.hpp"
#include <QAccessible>
#include <QBuffer>
#include <QJsonDocument>
#include <QSignalSpy>
#include <optional>

namespace {
const int kDeadline = 10000;
using glancelot::gps::Problem;
using glancelot::gps::TemplateProblem;

// ---- Generated EXIF -------------------------------------------------------------------------------------------------
struct Rational { quint32 num, den; };
using Dms = std::array<Rational, 3>;
struct GpsSpec {
    QByteArray latRef, lonRef;           // empty: tag omitted
    std::optional<Dms> lat, lon;
    bool altitude = false;
    bool refsAfterCoordinates = false;   // out of TIFF tag order
    bool gpsIfd = true;                  // false: no GPS IFD at all
};
const Dms kEiffelLat{{{48, 1}, {51, 1}, {30132, 1000}}};   // 48.858370
const Dms kEiffelLon{{{2, 1}, {17, 1}, {40132, 1000}}};    // 2.294481
const Dms kRioLat{{{22, 1}, {57, 1}, {0, 1}}};             // 22.950000 (S)
const Dms kRioLon{{{43, 1}, {12, 1}, {36, 1}}};            // 43.210000 (W)
const Dms kZero{{{0, 1}, {0, 1}, {0, 1}}};
const QString kEiffelUrl = "https://www.openstreetmap.org/?mlat=48.858370&mlon=2.294481#map=15/48.858370/2.294481";

GpsSpec eiffel() { return {"N", "E", kEiffelLat, kEiffelLon}; }

struct Entry { quint16 tag, type; quint32 count; QByteArray data; };
Entry ascii(quint16 tag, const QByteArray& text) { QByteArray data = text; data.append('\0'); return {tag, 2, quint32(data.size()), data}; }
Entry rationals(quint16 tag, const Dms& dms) {
    QByteArray data; QDataStream out(&data, QIODevice::WriteOnly); out.setByteOrder(QDataStream::LittleEndian);
    for (const auto& r : dms) out << r.num << r.den;
    return {tag, 5, 3, data};
}
// A little-endian TIFF: IFD0 points at the GPS IFD, whose entries keep the given order.
QByteArray tiffWith(const GpsSpec& spec) {
    QList<Entry> gps;
    QList<Entry> refs, coords;
    if (!spec.latRef.isEmpty()) refs << ascii(0x0001, spec.latRef);
    if (spec.lat) coords << rationals(0x0002, *spec.lat);
    if (!spec.lonRef.isEmpty()) refs << ascii(0x0003, spec.lonRef);
    if (spec.lon) coords << rationals(0x0004, *spec.lon);
    if (spec.refsAfterCoordinates) gps << coords << refs;
    else {
        // Ascending tag order, as TIFF requires.
        gps << refs << coords;
        std::sort(gps.begin(), gps.end(), [](const Entry& a, const Entry& b) { return a.tag < b.tag; });
    }
    if (spec.altitude) gps << Entry{0x0006, 5, 1, [] { QByteArray d; QDataStream o(&d, QIODevice::WriteOnly); o.setByteOrder(QDataStream::LittleEndian); o << quint32(155) << quint32(10); return d; }()};

    QByteArray tiff; QDataStream ds(&tiff, QIODevice::WriteOnly); ds.setByteOrder(QDataStream::LittleEndian);
    ds << quint8('I') << quint8('I') << quint16(42) << quint32(8);
    const quint32 gpsIfd = 8 + 2 + 12 + 4;
    ds << quint16(spec.gpsIfd ? 1 : 0);
    if (spec.gpsIfd) ds << quint16(0x8825) << quint16(4) << quint32(1) << gpsIfd;
    ds << quint32(0);
    if (!spec.gpsIfd) return tiff;
    quint32 dataOffset = gpsIfd + 2 + 12 * quint32(gps.size()) + 4;
    QByteArray extra;
    ds << quint16(gps.size());
    for (const auto& e : gps) {
        ds << e.tag << e.type << e.count;
        if (e.data.size() <= 4) { QByteArray inlineValue = e.data.leftJustified(4, '\0'); ds.writeRawData(inlineValue.constData(), 4); }
        else { ds << quint32(dataOffset + quint32(extra.size())); extra.append(e.data); }
    }
    ds << quint32(0);
    ds.writeRawData(extra.constData(), int(extra.size()));
    return tiff;
}
// A decodable JPEG (generated pixels) with the EXIF APP1 segment right after SOI.
QByteArray jpegWith(const GpsSpec& spec, QColor color = QColor(90, 140, 200)) {
    QImage image(64, 48, QImage::Format_RGB32);
    image.fill(color);
    QByteArray encoded; QBuffer buffer(&encoded); buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPG");
    const QByteArray tiff = tiffWith(spec);
    QByteArray app1("\xFF\xE1", 2);
    const quint16 length = quint16(2 + 6 + tiff.size());
    app1.append(char(length >> 8)).append(char(length & 0xFF)).append("Exif\0\0", 6).append(tiff);
    return encoded.left(2) + app1 + encoded.mid(2);
}
QString writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(bytes);
    return path;
}
QByteArray settingsJson() {
    QFile file(glancelot::settingsFilePath());
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Every URL the product tries to open lands here.
struct OpenRecorder {
    QList<QUrl> urls;
    bool result = true;
    OpenRecorder() { GpsMapHandoff::setUrlOpener([this](const QUrl& url) { urls << url; return result; }); }
    ~OpenRecorder() { GpsMapHandoff::setUrlOpener({}); }
};

QQuickItem* item(Harness& h, const char* name) { return named(h.window, QString::fromLatin1(name)); }
bool shown(Harness& h, const char* name) { QQuickItem* found = item(h, name); return found && found->isVisible(); }
GpsMapHandoff* hudHandoff(Harness& h) { return h.window->findChild<GpsMapHandoff*>("gpsMapHandoff"); }
QObject* notice(Harness& h) { return h.window->findChild<QObject*>("gpsMapNotice"); }
QString noticeText(Harness& h) { QObject* n = notice(h); return n && n->property("opened").toBool() ? n->property("text").toString() : QString(); }
void click(Harness& h, const char* name) {
    QQuickItem* button = item(h, name);
    QVERIFY2(button, name);
    QVERIFY2(button->isVisible() && button->isEnabled(), name);
    (void)h.window->grabWindow(); // settles pending layout, so the click lands where the button is now
    QTest::mouseClick(h.window, Qt::LeftButton, Qt::NoModifier, sceneCenter(button));
}
// The inspector pane ships hidden in the layout; the I key and the toolbar show it the same way.
void showInspector(Harness& h) { h.layout.setVisible("metadata", true); h.viewer.setMetadataVisible(true); }
// Screenshots for review go to GLANCELOT_F3_UI_EVIDENCE when it names a directory.
void evidence(Harness& h, const char* name) {
    const QString dir = qEnvironmentVariable("GLANCELOT_F3_UI_EVIDENCE");
    if (!dir.isEmpty()) h.window->grabWindow().save(QDir(dir).filePath(QString::fromLatin1(name) + ".png"));
}
bool invoke(Harness& h, const QString& id) { return QMetaObject::invokeMethod(h.window, "invoke", Q_ARG(QVariant, id)); }
bool view(Harness& h, const QString& path) {
    h.viewer.open(path);
    // The record for this exact path has arrived when the handoff stops reporting "pending".
    return QTest::qWaitFor([&] { return h.viewer.imageReady() && h.viewer.filePath() == path && !h.viewer.metadataModel()->loading()
                                        && hudHandoff(h) && hudHandoff(h)->problem() != "pending"; }, 15000);
}
} // namespace

class GpsHandoffUiTests : public QObject {
    Q_OBJECT
    QTemporaryDir config;
    QTemporaryDir media;
    QByteArray previousConfig;
    QString photo(const char* name, const GpsSpec& spec) { return writeFile(media.filePath(QString::fromLatin1(name)), jpegWith(spec)); }
    void resetPreferences() {
        QFile::remove(glancelot::settingsFilePath());
        GpsMapPreferences::resetForTesting();
    }

private slots:
    void initTestCase() {
        previousConfig = qgetenv("GLANCELOT_CONFIG_DIR");
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        QQuickStyle::setStyle("Basic"); // the style the application uses
        for (const char* face : {"Geist-Regular", "Geist-Medium", "Geist-SemiBold", "GeistMono-Regular"})
            QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/geist/%1.ttf").arg(QLatin1String(face)));
        qmlRegisterType<ImageCanvas>("Glancelot", 1, 0, "ImageCanvas");
        // The source-loaded UI has no generated qmldir, so the token singleton is registered as the app module does.
        qmlRegisterSingletonType(QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/Theme.qml")), "Glancelot", 1, 0, "Theme");
    }
    void cleanupTestCase() { qputenv("GLANCELOT_CONFIG_DIR", previousConfig); }
    void init() { resetPreferences(); }

    // ---- Validation through the existing parser ----
    void validatesParsedCoordinates_data() {
        QTest::addColumn<int>("spec");
        QTest::addColumn<int>("expected");
        QTest::addColumn<double>("lat");
        QTest::addColumn<double>("lon");
        QTest::newRow("north-east") << 0 << int(Problem::None) << 48.858370 << 2.294481;
        QTest::newRow("south-west") << 1 << int(Problem::None) << -22.95 << -43.21;
        QTest::newRow("lower-case references") << 2 << int(Problem::None) << 48.858370 << 2.294481;
        QTest::newRow("no GPS IFD") << 3 << int(Problem::Missing) << 0.0 << 0.0;
        QTest::newRow("empty GPS IFD") << 4 << int(Problem::Missing) << 0.0 << 0.0;
        QTest::newRow("latitude only") << 5 << int(Problem::Partial) << 0.0 << 0.0;
        QTest::newRow("references only") << 6 << int(Problem::Partial) << 0.0 << 0.0;
        QTest::newRow("altitude only") << 7 << int(Problem::Partial) << 0.0 << 0.0;
        QTest::newRow("no latitude reference") << 8 << int(Problem::Reference) << 0.0 << 0.0;
        QTest::newRow("invalid longitude reference") << 9 << int(Problem::Reference) << 0.0 << 0.0;
        QTest::newRow("swapped references") << 10 << int(Problem::Reference) << 0.0 << 0.0;
        QTest::newRow("reference after coordinate") << 11 << int(Problem::Reference) << 0.0 << 0.0;
        QTest::newRow("latitude beyond 90") << 12 << int(Problem::Range) << 0.0 << 0.0;
        QTest::newRow("longitude beyond 180") << 13 << int(Problem::Range) << 0.0 << 0.0;
        QTest::newRow("minutes overflow past the pole") << 14 << int(Problem::Range) << 0.0 << 0.0;
        QTest::newRow("0/0 sentinel") << 15 << int(Problem::NullIsland) << 0.0 << 0.0;
        QTest::newRow("zero denominators") << 16 << int(Problem::NullIsland) << 0.0 << 0.0;
    }
    void validatesParsedCoordinates() {
        QFETCH(int, spec);
        QFETCH(int, expected);
        QFETCH(double, lat);
        QFETCH(double, lon);
        const Dms zeroDen{{{48, 0}, {51, 0}, {30, 0}}};
        const QList<GpsSpec> specs{
            eiffel(),
            {"S", "W", kRioLat, kRioLon},
            {"n", "e", kEiffelLat, kEiffelLon},
            {"", "", std::nullopt, std::nullopt, false, false, false},
            {"", "", std::nullopt, std::nullopt},
            {"N", "", kEiffelLat, std::nullopt},
            {"N", "E", std::nullopt, std::nullopt},
            {"", "", std::nullopt, std::nullopt, true},
            {"", "E", kEiffelLat, kEiffelLon},
            {"N", "X", kEiffelLat, kEiffelLon},
            {"E", "N", kEiffelLat, kEiffelLon},
            {"S", "W", kRioLat, kRioLon, false, true},
            {"N", "E", Dms{{{95, 1}, {0, 1}, {0, 1}}}, kEiffelLon},
            {"N", "E", kEiffelLat, Dms{{{190, 1}, {0, 1}, {0, 1}}}},
            {"N", "E", Dms{{{89, 1}, {70, 1}, {0, 1}}}, kEiffelLon},
            {"N", "E", kZero, kZero},
            {"N", "E", zeroDen, zeroDen},
        };
        const QString path = writeFile(media.filePath(QString("validate-%1.jpg").arg(spec)), jpegWith(specs.at(spec)));
        const auto record = glancelot::MetadataParser::parse(path, 1);
        const auto fix = glancelot::gps::fromRecord(record);
        QCOMPARE(int(fix.problem), expected);
        if (fix.valid()) {
            QVERIFY(std::abs(fix.latitude - lat) < 1e-6);
            QVERIFY(std::abs(fix.longitude - lon) < 1e-6);
        }
    }
    void decimalTextIgnoresTheLocale() {
        const QLocale previous;
        QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
        const QString text = glancelot::gps::decimalText(-22.95, 151.2153);
        const QUrl url = glancelot::gps::mapUrl(QString::fromLatin1(glancelot::gps::kOpenStreetMapTemplate), 48.85837, 2.294481);
        QLocale::setDefault(previous);
        QCOMPARE(text, QString("-22.950000, 151.215300"));
        QCOMPARE(url.toString(), kEiffelUrl);
    }

    // ---- Templates ----
    void checksTemplates_data() {
        QTest::addColumn<QString>("pattern");
        QTest::addColumn<int>("expected");
        QTest::newRow("OpenStreetMap") << QString::fromLatin1(glancelot::gps::kOpenStreetMapTemplate) << int(TemplateProblem::None);
        QTest::newRow("custom https") << "https://maps.example.org/@{lat},{lon},{zoom}z" << int(TemplateProblem::None);
        QTest::newRow("custom http, no zoom") << "  http://localhost:8080/map?lat={lat}&lon={lon}  " << int(TemplateProblem::None);
        QTest::newRow("empty") << "   " << int(TemplateProblem::Empty);
        QTest::newRow("too long") << "https://example.org/{lat}/{lon}/" + QString(3000, 'a') << int(TemplateProblem::TooLong);
        QTest::newRow("no longitude") << "https://example.org/?lat={lat}" << int(TemplateProblem::Placeholder);
        QTest::newRow("unknown placeholder") << "https://example.org/{lat}/{lon}/{key}" << int(TemplateProblem::Unknown);
        QTest::newRow("javascript") << "javascript:alert({lat},{lon})" << int(TemplateProblem::Scheme);
        QTest::newRow("file") << "file:///tmp/{lat}/{lon}" << int(TemplateProblem::Scheme);
        QTest::newRow("geo") << "geo:{lat},{lon}" << int(TemplateProblem::Scheme);
        QTest::newRow("ftp") << "ftp://example.org/{lat}/{lon}" << int(TemplateProblem::Scheme);
        QTest::newRow("no host") << "https:///{lat}/{lon}" << int(TemplateProblem::Malformed);
        QTest::newRow("user info") << "https://user:pass@example.org/{lat}/{lon}" << int(TemplateProblem::Malformed);
        QTest::newRow("space in host") << "https://exa mple.org/{lat}/{lon}" << int(TemplateProblem::Malformed);
    }
    void checksTemplates() {
        QFETCH(QString, pattern);
        QFETCH(int, expected);
        QCOMPARE(int(glancelot::gps::checkTemplate(pattern)), expected);
        const QUrl url = glancelot::gps::mapUrl(pattern, -33.856784, 151.215297, 12);
        QCOMPARE(url.isEmpty(), expected != int(TemplateProblem::None));
    }
    void fillsCustomTemplate() {
        const QUrl url = glancelot::gps::mapUrl("https://maps.example.org/@{lat},{lon},{zoom}z?q={lat}", -33.856784, 151.215297, 12);
        QCOMPARE(url.toString(), QString("https://maps.example.org/@-33.856784,151.215297,12z?q=-33.856784"));
        QVERIFY(glancelot::gps::mapUrl("https://example.org/{lat}/{lon}", 91.0, 0.0).isEmpty()); // never a URL for a bad fix
    }

    // ---- The stale-record fence ----
    void staleOrLoadingRecordOffersNothing() {
        OpenRecorder recorder;
        MetadataModel model;
        GpsMapHandoff handoff;
        handoff.setMetadata(&model);
        const QString a = photo("fence-a.jpg", eiffel());
        auto record = glancelot::MetadataParser::parse(a, 1);
        model.setRecord(record);
        QSignalSpy refused(&handoff, &GpsMapHandoff::refused);
        QVERIFY(!handoff.available()); // no current photo
        handoff.setCurrentPath(a);
        QVERIFY(handoff.available());
        QCOMPARE(handoff.coordinates(), QString("48.858370, 2.294481"));
        // The next photo is on screen while its metadata is read: the previous record must not be offered.
        handoff.setCurrentPath(media.filePath("fence-b.jpg"));
        QVERIFY(!handoff.available());
        QCOMPARE(handoff.problem(), QString("pending"));
        QVERIFY(!handoff.openMap());
        QCOMPARE(handoff.copyCoordinates(), QString());
        handoff.setCurrentPath(a);
        model.setLoading(true);
        QVERIFY(!handoff.available());
        QCOMPARE(handoff.problem(), QString("pending"));
        QVERIFY(!handoff.openMap());
        QCOMPARE(recorder.urls.size(), 0);
        QCOMPARE(refused.size(), 3);
        QCOMPARE(refused.at(0).at(0).toString(), QString("pending"));
        model.setRecord(record);
        QVERIFY(handoff.openMap());
        QCOMPARE(recorder.urls, QList<QUrl>{QUrl(kEiffelUrl)});
    }

    // ---- Preferences ----
    void savesAndReloadsProvider() {
        GpsMapHandoff handoff;
        QCOMPARE(handoff.provider(), QString("openstreetmap"));
        QCOMPARE(handoff.providerHost(), QString("www.openstreetmap.org"));
        QSignalSpy changed(&handoff, &GpsMapHandoff::settingsChanged);
        handoff.setCustomTemplate("https://maps.example.org/{lat}/{lon}/{zoom}");
        handoff.setProvider("custom");
        QVERIFY(changed.size() >= 2);
        QTRY_VERIFY_WITH_TIMEOUT(settingsJson().contains("maps.example.org"), kDeadline);
        const auto saved = QJsonDocument::fromJson(settingsJson()).object().value("mapHandoff").toObject();
        QCOMPARE(saved.value("provider").toString(), QString("custom"));
        QCOMPARE(saved.value("template").toString(), QString("https://maps.example.org/{lat}/{lon}/{zoom}"));
        QCOMPARE(handoff.providerHost(), QString("maps.example.org"));
        // Another window wrote a different choice; reloading picks it up.
        QVERIFY(glancelot::updateSettings([](QJsonObject& root) { root["mapHandoff"] = QJsonObject{{"provider", "openstreetmap"}}; }).ok());
        handoff.reloadSettings();
        QCOMPARE(handoff.provider(), QString("openstreetmap"));
        QCOMPARE(handoff.customTemplate(), QString());
        // Damaged values fall back to OpenStreetMap without failing.
        QVERIFY(glancelot::updateSettings([](QJsonObject& root) { root["mapHandoff"] = QJsonObject{{"provider", "evil"}, {"template", 42}}; }).ok());
        handoff.reloadSettings();
        QCOMPARE(handoff.provider(), QString("openstreetmap"));
        QCOMPARE(handoff.customTemplate(), QString());
        QCOMPARE(handoff.templateProblem(), QString());
    }

    // ---- The shipped inspector, command and Settings page ----
    void nothingOpensWithoutTheAction() {
        OpenRecorder recorder;
        const QString located = photo("located.jpg", eiffel());
        const QString plain = photo("plain.jpg", GpsSpec{"", "", std::nullopt, std::nullopt, false, false, false});
        Harness h;
        QVERIFY(h.window);
        h.window->resize(1180, 780);
        h.window->show();
        h.viewer.setFilmstripVisible(false);
        showInspector(h);
        QVERIFY(view(h, located));
        QTRY_VERIFY_WITH_TIMEOUT(shown(h, "gpsShowOnMap") && shown(h, "gpsCopyCoordinates"), kDeadline);
        evidence(h, "inspector-actions");
        // Browsing, hiding and showing the inspector and changing the provider never open anything.
        QVERIFY(view(h, plain));
        QTRY_VERIFY_WITH_TIMEOUT(!shown(h, "gpsMapActions"), kDeadline);
        QVERIFY(view(h, located));
        h.viewer.setMetadataVisible(false);
        h.viewer.setMetadataVisible(true);
        QVERIFY(QMetaObject::invokeMethod(h.window, "openSettings", Q_ARG(QVariant, "viewing"), Q_ARG(QVariant, false)));
        QTRY_VERIFY_WITH_TIMEOUT(shown(h, "gpsMapSettings"), kDeadline);
        auto* settingsHandoff = h.window->findChild<GpsMapHandoff*>("gpsMapSettingsHandoff");
        QVERIFY(settingsHandoff);
        QCOMPARE(settingsHandoff->providerHost(), QString("www.openstreetmap.org"));
        QQuickItem* provider = item(h, "gpsMapProvider");
        QVERIFY(provider);
        provider->setProperty("currentIndex", 1);
        QVERIFY(QMetaObject::invokeMethod(provider, "activated", Q_ARG(int, 1)));
        QTRY_VERIFY_WITH_TIMEOUT(shown(h, "gpsMapTemplate"), kDeadline);
        evidence(h, "settings-custom");
        provider->setProperty("currentIndex", 0);
        QVERIFY(QMetaObject::invokeMethod(provider, "activated", Q_ARG(int, 0)));
        QCOMPARE(hudHandoff(h)->provider(), QString("openstreetmap"));
        QMetaObject::invokeMethod(h.object("settingsDialog"), "close");
        QCOMPARE(recorder.urls.size(), 0);

        // The explicit actions: the button and the command, each opening exactly one URL for this photo.
        click(h, "gpsShowOnMap");
        QTRY_COMPARE_WITH_TIMEOUT(recorder.urls.size(), 1, kDeadline);
        QCOMPARE(recorder.urls.first(), QUrl(kEiffelUrl));
        QTRY_VERIFY_WITH_TIMEOUT(noticeText(h).contains("www.openstreetmap.org"), kDeadline);
        QVERIFY(invoke(h, "show-on-map"));
        QCOMPARE(recorder.urls.size(), 2);
        QCOMPARE(recorder.urls.last(), QUrl(kEiffelUrl));
        // The command works with the inspector hidden too.
        h.viewer.setMetadataVisible(false);
        QVERIFY(invoke(h, "show-on-map"));
        QCOMPARE(recorder.urls.size(), 3);
    }
    void invalidOrMissingLocationOffersNoAction() {
        OpenRecorder recorder;
        const QString noRef = photo("no-reference.jpg", GpsSpec{"", "E", kEiffelLat, kEiffelLon});
        const QString sentinel = photo("null-island.jpg", GpsSpec{"N", "E", kZero, kZero});
        const QString partial = photo("partial.jpg", GpsSpec{"N", "", kEiffelLat, std::nullopt});
        const QString missing = photo("missing.jpg", GpsSpec{"", "", std::nullopt, std::nullopt, false, false, false});
        Harness h;
        QVERIFY(h.window);
        h.window->resize(1180, 780);
        h.window->show();
        h.viewer.setFilmstripVisible(false);
        showInspector(h);
        QGuiApplication::clipboard()->setText("untouched");
        const QList<std::pair<QString, QString>> damaged{{noRef, "north/south"}, {sentinel, "0, 0"}, {partial, "incomplete"}};
        for (const auto& [path, words] : damaged) {
            QVERIFY(view(h, path));
            QTRY_VERIFY_WITH_TIMEOUT(shown(h, "gpsMapReason"), kDeadline);
            QVERIFY2(item(h, "gpsMapReason")->property("text").toString().contains(words), qPrintable(path));
            QAccessibleInterface* spoken = QAccessible::queryAccessibleInterface(item(h, "gpsMapReason"));
            QVERIFY2(spoken && spoken->text(QAccessible::Name).contains(words), qPrintable(path)); // the reason is read out, too
            QVERIFY(!shown(h, "gpsShowOnMap"));
            QVERIFY(!shown(h, "gpsCopyCoordinates"));
            QVERIFY(invoke(h, "show-on-map"));
            QTRY_VERIFY_WITH_TIMEOUT(noticeText(h).contains(words), kDeadline);
            if (path == noRef) evidence(h, "inspector-reason-notice");
            QVERIFY(invoke(h, "copy-coordinates"));
        }
        QVERIFY(view(h, missing));
        QTRY_VERIFY_WITH_TIMEOUT(!shown(h, "gpsMapActions"), kDeadline);
        QVERIFY(invoke(h, "show-on-map"));
        QTRY_VERIFY_WITH_TIMEOUT(noticeText(h).contains("no location data"), kDeadline);
        QCOMPARE(recorder.urls.size(), 0);
        QCOMPARE(QGuiApplication::clipboard()->text(), QString("untouched"));
    }
    void copyCoordinatesSendsNothing() {
        OpenRecorder recorder;
        const QString rio = photo("rio.jpg", GpsSpec{"S", "W", kRioLat, kRioLon});
        Harness h;
        QVERIFY(h.window);
        h.window->resize(1180, 780);
        h.window->show();
        h.viewer.setFilmstripVisible(false);
        showInspector(h);
        QVERIFY(view(h, rio));
        QTRY_VERIFY_WITH_TIMEOUT(shown(h, "gpsCopyCoordinates"), kDeadline);
        QGuiApplication::clipboard()->setText("before");
        click(h, "gpsCopyCoordinates");
        QTRY_COMPARE_WITH_TIMEOUT(QGuiApplication::clipboard()->text(), QString("-22.950000, -43.210000"), kDeadline);
        QTRY_VERIFY_WITH_TIMEOUT(noticeText(h).contains("-22.950000, -43.210000"), kDeadline);
        QGuiApplication::clipboard()->setText("before");
        QVERIFY(invoke(h, "copy-coordinates"));
        QCOMPARE(QGuiApplication::clipboard()->text(), QString("-22.950000, -43.210000"));
        QCOMPARE(recorder.urls.size(), 0);
    }
    void customProviderIsUsedAndAnInvalidOneBlocks() {
        OpenRecorder recorder;
        const QString located = photo("custom.jpg", eiffel());
        Harness h;
        QVERIFY(h.window);
        h.window->resize(1180, 780);
        h.window->show();
        h.viewer.setFilmstripVisible(false);
        showInspector(h);
        QVERIFY(view(h, located));
        QVERIFY(QMetaObject::invokeMethod(h.window, "openSettings", Q_ARG(QVariant, "viewing"), Q_ARG(QVariant, false)));
        QTRY_VERIFY_WITH_TIMEOUT(shown(h, "gpsMapSettings"), kDeadline);
        QQuickItem* provider = item(h, "gpsMapProvider");
        provider->setProperty("currentIndex", 1);
        QVERIFY(QMetaObject::invokeMethod(provider, "activated", Q_ARG(int, 1)));
        QQuickItem* field = item(h, "gpsMapTemplate");
        QTRY_VERIFY_WITH_TIMEOUT(field && field->isVisible(), kDeadline);
        // An empty custom link: the HUD says why, the button is off and the command opens nothing.
        QCOMPARE(hudHandoff(h)->templateProblem(), QString("empty"));
        field->setProperty("text", "javascript:alert({lat},{lon})");
        QTRY_VERIFY_WITH_TIMEOUT(item(h, "gpsMapTemplateNote")->property("text").toString().contains("https://"), kDeadline);
        QVERIFY(QMetaObject::invokeMethod(field, "editingFinished"));
        QCOMPARE(hudHandoff(h)->templateProblem(), QString("scheme"));
        QMetaObject::invokeMethod(h.object("settingsDialog"), "close");
        QTRY_VERIFY_WITH_TIMEOUT(shown(h, "gpsShowOnMap") && !item(h, "gpsShowOnMap")->isEnabled(), kDeadline);
        QVERIFY(item(h, "gpsMapReason")->property("text").toString().contains("custom map link"));
        QVERIFY(invoke(h, "show-on-map"));
        QTRY_VERIFY_WITH_TIMEOUT(noticeText(h).contains("custom map link"), kDeadline);
        QCOMPARE(recorder.urls.size(), 0);
        // A valid custom link is used as written, with C-locale numbers.
        hudHandoff(h)->setCustomTemplate("https://maps.example.org/@{lat},{lon},{zoom}z");
        QTRY_VERIFY_WITH_TIMEOUT(item(h, "gpsShowOnMap")->isEnabled(), kDeadline);
        click(h, "gpsShowOnMap");
        QTRY_COMPARE_WITH_TIMEOUT(recorder.urls.size(), 1, kDeadline);
        QCOMPARE(recorder.urls.first(), QUrl("https://maps.example.org/@48.858370,2.294481,15z"));
    }
    void reportsWhenTheSystemCannotOpen() {
        OpenRecorder recorder;
        recorder.result = false;
        const QString located = photo("open-failed.jpg", eiffel());
        Harness h;
        QVERIFY(h.window);
        h.window->resize(1180, 780);
        h.window->show();
        h.viewer.setFilmstripVisible(false);
        showInspector(h);
        QVERIFY(view(h, located));
        QVERIFY(invoke(h, "show-on-map"));
        QCOMPARE(recorder.urls.size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(noticeText(h).contains("could not open a browser"), kDeadline);
    }
};

QTEST_MAIN(GpsHandoffUiTests)
#include "gps_handoff_ui_tests.moc"
