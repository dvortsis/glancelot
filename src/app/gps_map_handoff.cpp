#include "gps_map_handoff.hpp"
#include "settings_retry.hpp"
#include "glancelot/settings_file.hpp"
#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QGuiApplication>
#include <QJsonObject>
#include <QQmlEngine>
#include <QRegularExpression>
#include <cmath>

namespace glancelot::gps {
namespace {
const MetadataItem* findItem(const MetadataRecord& record, QStringView key) {
    const MetadataItem* found = nullptr;
    for (const auto& item : record.items)
        if (item.key == key) found = &item; // the parser's last value wins, as it does for latitude and longitude
    return found;
}
// EXIF 2.3: GPSLatitudeRef is "N" or "S", GPSLongitudeRef "E" or "W" (ASCII, count 2). Case and padding are tolerated.
bool referenceMatches(const MetadataItem* ref, QChar positive, QChar negative, double value) {
    if (!ref) return false;
    const QString text = ref->value.trimmed().toUpper();
    if (text.size() != 1 || (text[0] != positive && text[0] != negative)) return false;
    // The parser applies the reference only when it comes before the coordinate (TIFF tag order). A reference that
    // disagrees with the sign it produced is an out-of-order or contradictory IFD: refuse it rather than guess.
    return text[0] == negative ? value <= 0.0 : value >= 0.0;
}
QString number(double value) { return QString::number(value, 'f', 6); }
} // namespace

Fix fromRecord(const MetadataRecord& record) {
    const auto* lat = findItem(record, u"Exif.GPSInfo.GPSLatitude");
    const auto* lon = findItem(record, u"Exif.GPSInfo.GPSLongitude");
    const auto* latRef = findItem(record, u"Exif.GPSInfo.GPSLatitudeRef");
    const auto* lonRef = findItem(record, u"Exif.GPSInfo.GPSLongitudeRef");
    Fix fix;
    fix.latitude = record.latitude;
    fix.longitude = record.longitude;
    if (!lat && !lon) {
        // Altitude or references alone still say the camera tried to record a position.
        fix.problem = (record.hasGps || latRef || lonRef) ? Problem::Partial : Problem::Missing;
        return fix;
    }
    if (!lat || !lon) { fix.problem = Problem::Partial; return fix; }
    if (!std::isfinite(fix.latitude) || !std::isfinite(fix.longitude) || std::abs(fix.latitude) > 90.0 || std::abs(fix.longitude) > 180.0) {
        fix.problem = Problem::Range;
        return fix;
    }
    if (!referenceMatches(latRef, u'N', u'S', fix.latitude) || !referenceMatches(lonRef, u'E', u'W', fix.longitude)) {
        fix.problem = Problem::Reference;
        return fix;
    }
    // 0/0 is what cameras and phones write when they had no fix (or a zero denominator was read as 0).
    if (std::abs(fix.latitude) < 1e-9 && std::abs(fix.longitude) < 1e-9) { fix.problem = Problem::NullIsland; return fix; }
    fix.problem = Problem::None;
    return fix;
}

QString problemCode(Problem problem) {
    switch (problem) {
    case Problem::None: return {};
    case Problem::Missing: return QStringLiteral("missing");
    case Problem::Partial: return QStringLiteral("partial");
    case Problem::Reference: return QStringLiteral("reference");
    case Problem::Range: return QStringLiteral("range");
    case Problem::NullIsland: return QStringLiteral("null-island");
    }
    return QStringLiteral("missing");
}

QString decimalText(double latitude, double longitude) { return number(latitude) + QStringLiteral(", ") + number(longitude); }

QString templateProblemCode(TemplateProblem problem) {
    switch (problem) {
    case TemplateProblem::None: return {};
    case TemplateProblem::Empty: return QStringLiteral("empty");
    case TemplateProblem::TooLong: return QStringLiteral("too-long");
    case TemplateProblem::Placeholder: return QStringLiteral("placeholder");
    case TemplateProblem::Unknown: return QStringLiteral("unknown");
    case TemplateProblem::Scheme: return QStringLiteral("scheme");
    case TemplateProblem::Malformed: return QStringLiteral("malformed");
    }
    return QStringLiteral("malformed");
}

namespace {
QString fill(const QString& pattern, double latitude, double longitude, int zoom) {
    QString text = pattern.trimmed();
    text.replace(QStringLiteral("{lat}"), number(latitude));
    text.replace(QStringLiteral("{lon}"), number(longitude));
    text.replace(QStringLiteral("{zoom}"), QString::number(zoom));
    return text;
}
} // namespace

TemplateProblem checkTemplate(const QString& pattern) {
    const QString text = pattern.trimmed();
    if (text.isEmpty()) return TemplateProblem::Empty;
    if (text.size() > kMaxTemplateLength) return TemplateProblem::TooLong;
    if (!text.contains(QStringLiteral("{lat}")) || !text.contains(QStringLiteral("{lon}"))) return TemplateProblem::Placeholder;
    // Filled with the most negative values, so a template that only breaks with a minus sign is caught too.
    const QString filled = fill(text, -90.0, -180.0, kDefaultZoom);
    static const QRegularExpression leftover(QStringLiteral("[{}]"));
    if (filled.contains(leftover)) return TemplateProblem::Unknown;
    const QUrl url(filled, QUrl::StrictMode);
    if (!url.isValid()) return TemplateProblem::Malformed;
    const QString scheme = url.scheme().toLower();
    // Only web pages: a file:, javascript:, data: or custom-scheme URL could run something other than a browser.
    if (scheme != QStringLiteral("https") && scheme != QStringLiteral("http")) return TemplateProblem::Scheme;
    if (url.host().isEmpty() || !url.userInfo().isEmpty()) return TemplateProblem::Malformed;
    return TemplateProblem::None;
}

QUrl mapUrl(const QString& pattern, double latitude, double longitude, int zoom) {
    if (checkTemplate(pattern) != TemplateProblem::None) return {};
    if (!std::isfinite(latitude) || !std::isfinite(longitude) || std::abs(latitude) > 90.0 || std::abs(longitude) > 180.0) return {};
    const QUrl url(fill(pattern, latitude, longitude, zoom), QUrl::StrictMode);
    return url.isValid() ? url : QUrl();
}

} // namespace glancelot::gps

// ---- Preferences ----------------------------------------------------------------------------------------------------
namespace {
GpsMapPreferences* g_preferences = nullptr;
const QString kSettingsKey = QStringLiteral("mapHandoff");
const QString kOpenStreetMap = QStringLiteral("openstreetmap");
const QString kCustom = QStringLiteral("custom");
Qt::CaseSensitivity pathSensitivity() {
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}
GpsMapHandoff::UrlOpener& urlOpener() {
    static GpsMapHandoff::UrlOpener opener;
    return opener;
}
} // namespace

GpsMapPreferences::GpsMapPreferences(QObject* parent) : QObject(parent) { reload(); }

GpsMapPreferences& GpsMapPreferences::instance() {
    if (!g_preferences) {
        g_preferences = new GpsMapPreferences(QCoreApplication::instance());
        QObject::connect(g_preferences, &QObject::destroyed, [] { g_preferences = nullptr; });
    }
    return *g_preferences;
}

void GpsMapPreferences::resetForTesting() {
    if (g_preferences) g_preferences->reload();
}

void GpsMapPreferences::reload() {
    QJsonObject root;
    QString provider = kOpenStreetMap, pattern;
    if (glancelot::readSettings(root).ok()) {
        glancelot::settings_detail::overlayPendingSettings(this, root); // this window's unsaved choice still wins
        const auto saved = root.value(kSettingsKey).toObject();
        if (saved.value(QStringLiteral("provider")).toString() == kCustom) provider = kCustom;
        pattern = saved.value(QStringLiteral("template")).toString().left(glancelot::gps::kMaxTemplateLength);
    }
    if (provider == provider_ && pattern == customTemplate_) return;
    provider_ = provider;
    customTemplate_ = pattern;
    emit changed();
}

void GpsMapPreferences::setProvider(const QString& provider) {
    const QString value = provider == kCustom ? kCustom : kOpenStreetMap;
    if (value == provider_) return;
    provider_ = value;
    save();
    emit changed();
}

void GpsMapPreferences::setCustomTemplate(const QString& pattern) {
    const QString value = pattern.trimmed().left(glancelot::gps::kMaxTemplateLength);
    if (value == customTemplate_) return;
    customTemplate_ = value;
    save();
    emit changed();
}

void GpsMapPreferences::save() {
    QJsonObject value{{QStringLiteral("provider"), provider_}};
    if (!customTemplate_.isEmpty()) value.insert(QStringLiteral("template"), customTemplate_);
    saveError_.clear();
    const bool accepted = glancelot::settings_detail::saveWithRetry(this, kSettingsKey, [value](QJsonObject& root) { root[kSettingsKey] = value; },
        [this](glancelot::SettingsStatus status) {
            const QString error = status.ok() ? QString() : QStringLiteral("not-saved");
            if (error == saveError_) return;
            saveError_ = error;
            emit changed();
        });
    if (!accepted) saveError_ = QStringLiteral("not-saved");
}

// ---- Handoff --------------------------------------------------------------------------------------------------------
GpsMapHandoff::GpsMapHandoff(QObject* parent) : QObject(parent) {
    connect(&GpsMapPreferences::instance(), &GpsMapPreferences::changed, this, &GpsMapHandoff::settingsChanged);
}

void GpsMapHandoff::setMetadata(MetadataModel* model) {
    if (metadata_ == model) return;
    disconnect(recordConnection_);
    disconnect(loadingConnection_);
    metadata_ = model;
    if (model) {
        recordConnection_ = connect(model, &MetadataModel::recordChanged, this, &GpsMapHandoff::locationChanged);
        loadingConnection_ = connect(model, &MetadataModel::loadingChanged, this, &GpsMapHandoff::locationChanged);
    }
    emit metadataChanged();
    emit locationChanged();
}

void GpsMapHandoff::setCurrentPath(const QString& path) {
    if (currentPath_ == path) return;
    currentPath_ = path;
    emit locationChanged();
}

glancelot::gps::Fix GpsMapHandoff::currentFix() const {
    glancelot::gps::Fix none;
    // Generation fence: while the next photo's metadata is read, the model still holds the previous record.
    if (!metadata_ || metadata_->loading() || currentPath_.isEmpty()) return none;
    const auto& record = metadata_->record();
    if (!record.valid || QDir::cleanPath(record.filePath).compare(QDir::cleanPath(currentPath_), pathSensitivity()) != 0) return none;
    return glancelot::gps::fromRecord(record);
}

bool GpsMapHandoff::available() const { return currentFix().valid(); }
bool GpsMapHandoff::pending() const {
    if (!metadata_ || currentPath_.isEmpty()) return false;
    if (metadata_->loading()) return true;
    const auto& record = metadata_->record();
    return record.valid && QDir::cleanPath(record.filePath).compare(QDir::cleanPath(currentPath_), pathSensitivity()) != 0;
}
QString GpsMapHandoff::problem() const {
    if (pending()) return QStringLiteral("pending");
    return glancelot::gps::problemCode(currentFix().problem);
}
QString GpsMapHandoff::coordinates() const {
    const auto fix = currentFix();
    return fix.valid() ? glancelot::gps::decimalText(fix.latitude, fix.longitude) : QString();
}

QString GpsMapHandoff::provider() const { return GpsMapPreferences::instance().provider(); }
void GpsMapHandoff::setProvider(const QString& provider) { GpsMapPreferences::instance().setProvider(provider); }
QString GpsMapHandoff::customTemplate() const { return GpsMapPreferences::instance().customTemplate(); }
void GpsMapHandoff::setCustomTemplate(const QString& pattern) { GpsMapPreferences::instance().setCustomTemplate(pattern); }
QString GpsMapHandoff::saveError() const { return GpsMapPreferences::instance().saveError(); }

QString GpsMapHandoff::activeTemplate() const {
    return provider() == kCustom ? customTemplate() : QString::fromLatin1(glancelot::gps::kOpenStreetMapTemplate);
}
QString GpsMapHandoff::templateProblem() const { return glancelot::gps::templateProblemCode(glancelot::gps::checkTemplate(activeTemplate())); }
QString GpsMapHandoff::checkTemplate(const QString& pattern) const { return glancelot::gps::templateProblemCode(glancelot::gps::checkTemplate(pattern)); }
QString GpsMapHandoff::providerHost() const {
    const QString pattern = activeTemplate();
    if (glancelot::gps::checkTemplate(pattern) != glancelot::gps::TemplateProblem::None) return {};
    return glancelot::gps::mapUrl(pattern, 0.0, 0.0).host();
}
void GpsMapHandoff::reloadSettings() { GpsMapPreferences::instance().reload(); }

bool GpsMapHandoff::openMap() {
    const auto fix = currentFix();
    if (!fix.valid()) { emit refused(problem()); return false; }
    const QUrl url = glancelot::gps::mapUrl(activeTemplate(), fix.latitude, fix.longitude);
    if (url.isEmpty()) { emit refused(QStringLiteral("template")); return false; }
    const auto& opener = urlOpener();
    const bool handed = opener ? opener(url) : QDesktopServices::openUrl(url);
    if (!handed) { emit refused(QStringLiteral("open-failed")); return false; }
    emit opened(url);
    return true;
}

QString GpsMapHandoff::copyCoordinates() {
    const QString text = coordinates();
    if (text.isEmpty()) { emit refused(problem()); return {}; }
    if (auto* clipboard = QGuiApplication::clipboard()) clipboard->setText(text);
    return text;
}

void GpsMapHandoff::setUrlOpener(UrlOpener opener) { urlOpener() = std::move(opener); }

namespace {
void registerGpsMapHandoff() { qmlRegisterType<GpsMapHandoff>("Glancelot", 1, 0, "GpsMapHandoff"); }
} // namespace
Q_COREAPP_STARTUP_FUNCTION(registerGpsMapHandoff)
