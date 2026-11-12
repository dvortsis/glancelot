#pragma once
// F3 (F072): user-invoked map handoff for a photo's GPS position.
//  * Coordinates come from the existing MetadataParser record. Only a complete, in-range fix with valid hemisphere
//    references (N/S, E/W) and not the 0/0 "null island" sentinel is offered; anything else reports a problem code.
//  * The viewer never contacts a map service. openMap() hands one URL to the user's default browser through
//    QDesktopServices::openUrl, and only when the user asks; no lookup or preview ever runs in the background.
//  * The provider is OpenStreetMap or the user's own https/http template with {lat}, {lon} and {zoom}.
//  * copyCoordinates() is the no-network alternative.
#include "glancelot/metadata_model.hpp"
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <functional>

namespace glancelot::gps {

enum class Problem { None, Missing, Partial, Reference, Range, NullIsland };
struct Fix {
    Problem problem = Problem::Missing;
    double latitude = 0.0;
    double longitude = 0.0;
    bool valid() const { return problem == Problem::None; }
};
// Validates the parser's GPS fields. Needs both coordinates and both hemisphere references from the GPS IFD.
Fix fromRecord(const MetadataRecord& record);
QString problemCode(Problem problem); // "", "missing", "partial", "reference", "range", "null-island"
// Decimal degrees, six places (about 0.1 m), C locale: "48.858370, 2.294481".
QString decimalText(double latitude, double longitude);

enum class TemplateProblem { None, Empty, TooLong, Placeholder, Unknown, Scheme, Malformed };
QString templateProblemCode(TemplateProblem problem); // "", "empty", "too-long", "placeholder", "unknown", "scheme", "malformed"
inline constexpr int kMaxTemplateLength = 2048;
inline constexpr int kDefaultZoom = 15; // street level
inline constexpr char kOpenStreetMapTemplate[] = "https://www.openstreetmap.org/?mlat={lat}&mlon={lon}#map={zoom}/{lat}/{lon}";
// A usable template is https or http, has a host, names {lat} and {lon} ({zoom} is optional) and no other placeholder.
TemplateProblem checkTemplate(const QString& pattern);
// The template with the placeholders filled with C-locale numbers. Empty when the template is not usable.
QUrl mapUrl(const QString& pattern, double latitude, double longitude, int zoom = kDefaultZoom);

} // namespace glancelot::gps

// The provider choice, shared by every handoff object in this process and saved in settings.json under "mapHandoff".
class GpsMapPreferences : public QObject {
    Q_OBJECT
public:
    static GpsMapPreferences& instance();
    QString provider() const { return provider_; }           // "openstreetmap" or "custom"
    QString customTemplate() const { return customTemplate_; }
    QString saveError() const { return saveError_; }
    void setProvider(const QString& provider);
    void setCustomTemplate(const QString& pattern);
    void reload(); // re-reads settings.json (another window may have changed it)
    // Tests: forget the cached choice so the next instance() call reads settings.json again.
    static void resetForTesting();
signals:
    void changed();
private:
    explicit GpsMapPreferences(QObject* parent);
    void save();
    QString provider_ = QStringLiteral("openstreetmap");
    QString customTemplate_;
    QString saveError_;
};

class GpsMapHandoff : public QObject {
    Q_OBJECT
    Q_PROPERTY(MetadataModel* metadata READ metadata WRITE setMetadata NOTIFY metadataChanged)
    // The photo on screen now. A record for another path (the previous photo, while the next one is read) offers nothing.
    Q_PROPERTY(QString currentPath READ currentPath WRITE setCurrentPath NOTIFY locationChanged)
    Q_PROPERTY(bool available READ available NOTIFY locationChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY locationChanged)
    Q_PROPERTY(QString coordinates READ coordinates NOTIFY locationChanged)
    Q_PROPERTY(QString provider READ provider WRITE setProvider NOTIFY settingsChanged)
    Q_PROPERTY(QString customTemplate READ customTemplate WRITE setCustomTemplate NOTIFY settingsChanged)
    Q_PROPERTY(QString templateProblem READ templateProblem NOTIFY settingsChanged)
    Q_PROPERTY(QString providerHost READ providerHost NOTIFY settingsChanged)
    Q_PROPERTY(QString saveError READ saveError NOTIFY settingsChanged)
public:
    using UrlOpener = std::function<bool(const QUrl&)>;
    explicit GpsMapHandoff(QObject* parent = nullptr);

    MetadataModel* metadata() const { return metadata_; }
    void setMetadata(MetadataModel* model);
    QString currentPath() const { return currentPath_; }
    void setCurrentPath(const QString& path);
    bool available() const;
    QString problem() const;
    QString coordinates() const;
    QString provider() const;
    void setProvider(const QString& provider);
    QString customTemplate() const;
    void setCustomTemplate(const QString& pattern);
    QString templateProblem() const;
    QString providerHost() const;
    QString saveError() const;

    // problem: "" when available, "pending" while the photo's metadata is read, else gps::problemCode().
    // The explicit user action: opens the map for the photo on screen. Returns false, and emits refused(code), when the
    // photo has no usable position ("pending", "missing", "partial", ...), the custom template is unusable ("template") or the
    // system could not open the URL ("open-failed").
    Q_INVOKABLE bool openMap();
    // Copies "lat, lon" in decimal degrees; empty (clipboard untouched) without a usable position.
    Q_INVOKABLE QString copyCoordinates();
    // Live check while the user types a template, without saving it.
    Q_INVOKABLE QString checkTemplate(const QString& pattern) const;
    Q_INVOKABLE void reloadSettings();

    // Test seam. Every URL leaves through this opener; an empty function restores QDesktopServices::openUrl.
    static void setUrlOpener(UrlOpener opener);

signals:
    void metadataChanged();
    void locationChanged();
    void settingsChanged();
    void opened(const QUrl& url);
    void refused(const QString& problem);

private:
    glancelot::gps::Fix currentFix() const;
    bool pending() const; // the record on hand belongs to another photo, or the next one is still being read
    QString activeTemplate() const;
    QPointer<MetadataModel> metadata_;
    QMetaObject::Connection recordConnection_, loadingConnection_;
    QString currentPath_;
};
