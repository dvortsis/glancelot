#pragma once
#include <QAbstractListModel>
#include <QString>
#include <QStringList>
#include <QList>
#include <QVariantMap>
#include <memory>

namespace glancelot {

struct MetadataItem {
    QString key;
    QString label;
    QString value;
    QString rawValue;
    QString category; // "Summary", "Camera", "Image", "GPS", "IPTC", "Raw"
};

struct MetadataRecord {
    quint64 generation = 0;
    QString filePath;
    bool valid = false;
    bool hasMetadata = false;

    // File / Dimensions
    QString fileName;
    qint64 fileSize = 0;
    QString fileFormat;
    int storedWidth = 0;
    int storedHeight = 0;
    int displayWidth = 0;
    int displayHeight = 0;
    int orientationCode = 1;
    QString orientationDescription;
    QString colorSpace;
    QString iccProfileName;
    int bitDepth = 8;

    // Camera / Lens / Exposure
    QString cameraMake;
    QString cameraModel;
    QString lensModel;
    QString exposureTime;
    QString fNumber;
    QString isoRating;
    QString focalLength;
    QString focalLength35mm;
    QString exposureProgram;
    QString meteringMode;
    QString flash;
    QString whiteBalance;

    // Timestamps
    QString dateTimeOriginal;
    QString dateTimeDigitized;

    // Location / GPS
    bool hasGps = false;
    double latitude = 0.0;
    double longitude = 0.0;
    double altitude = 0.0;
    QString gpsLatitudeStr;
    QString gpsLongitudeStr;
    QString gpsAltitudeStr;

    // IPTC / Authorship
    QString iptcTitle;
    QString iptcCreator;
    QString iptcCopyright;
    QString iptcDescription;
    QStringList iptcKeywords;

    // Full item list
    QList<MetadataItem> items;
};

} // namespace glancelot

// Metadata inspection model for the right HUD (META-HUD-01 / META-01 / UX-04).
// Contract (frozen by the lead; change only through the lead):
//  * Parsing runs asynchronously off the UI thread; never stalls viewing or rendering.
//  * Generation tracking ensures stale async results are discarded.
//  * Exposes categorized items and fast summary properties for high-priority display.
//  * Provides live search and category filtering for tag discovery.
class MetadataModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool hasMetadata READ hasMetadata NOTIFY recordChanged)
    Q_PROPERTY(QString searchFilter READ searchFilter WRITE setSearchFilter NOTIFY filterChanged)
    Q_PROPERTY(QString categoryFilter READ categoryFilter WRITE setCategoryFilter NOTIFY filterChanged)
    Q_PROPERTY(QString cameraSummary READ cameraSummary NOTIFY recordChanged)
    Q_PROPERTY(QString exposureSummary READ exposureSummary NOTIFY recordChanged)
    Q_PROPERTY(QString dimensionsSummary READ dimensionsSummary NOTIFY recordChanged)
    Q_PROPERTY(QString dateTimeSummary READ dateTimeSummary NOTIFY recordChanged)
    Q_PROPERTY(QString locationSummary READ locationSummary NOTIFY recordChanged)
    Q_PROPERTY(bool hasGps READ hasGps NOTIFY recordChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY recordChanged)
public:
    enum Role {
        KeyRole = Qt::UserRole + 1,
        LabelRole,
        ValueRole,
        RawValueRole,
        CategoryRole
    };
    Q_ENUM(Role)

    explicit MetadataModel(QObject* parent = nullptr);
    ~MetadataModel() override;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    bool loading() const;
    bool hasMetadata() const;
    QString searchFilter() const;
    void setSearchFilter(const QString& filter);
    QString categoryFilter() const;
    void setCategoryFilter(const QString& category);

    QString cameraSummary() const;
    QString exposureSummary() const;
    QString dimensionsSummary() const;
    QString dateTimeSummary() const;
    QString locationSummary() const;
    bool hasGps() const;
    QString filePath() const;

    const glancelot::MetadataRecord& record() const;
    void setRecord(const glancelot::MetadataRecord& record);
    void setLoading(bool loading);
    void clear();

    Q_INVOKABLE QString copySummary() const;
    Q_INVOKABLE QString copyGps() const;
    Q_INVOKABLE QString copyTag(const QString& key) const;
    Q_INVOKABLE QVariantMap get(int index) const;

signals:
    void countChanged();
    void loadingChanged();
    void filterChanged();
    void recordChanged();

private:
    struct Private;
    std::unique_ptr<Private> d;
};

