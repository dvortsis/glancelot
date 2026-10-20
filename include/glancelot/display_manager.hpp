#pragma once
#include <QObject>
#include <QString>
#include <QRect>
#include <QSize>
#include <QImage>
#include <QList>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

class QScreen;
class QQuickWindow;

namespace glancelot {

enum class PresentationMode {
    Mirror,      // Follows current controller photo
    Pinned,      // Keeps a specific pinned reference photo
    Comparison   // Comparison display (side-by-side with reference)
};

struct DisplayInfo {
    int index{-1};
    QString id;                 // Stable identity string
    QString name;               // Readable display name (e.g., "Dell U2720Q", "Display 2")
    QString manufacturer;
    QString model;
    QString serialNumber;
    QRect geometry;             // Virtual desktop geometry
    QRect availableGeometry;    // Usable work area
    QSize physicalSizeMm;       // Physical dimensions in mm
    qreal devicePixelRatio{1.0};
    qreal refreshRate{60.0};
    int colorDepth{24};
    QString colorSpace{"sRGB"};
    bool isPrimary{false};
    bool isAvailable{true};

    QVariantMap toVariantMap() const;
};

class DisplayManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool presentationActive READ isPresentationActive NOTIFY presentationActiveChanged)
    Q_PROPERTY(QString presentationMode READ presentationModeString WRITE setPresentationMode NOTIFY presentationModeChanged)
    Q_PROPERTY(QString activeDisplayId READ activeDisplayId NOTIFY activeDisplayIdChanged)
    Q_PROPERTY(QString activeDisplayName READ activeDisplayName NOTIFY activeDisplayIdChanged)
    Q_PROPERTY(QVariantList displays READ displaysVariant NOTIFY displaysChanged)
    Q_PROPERTY(int displayCount READ displayCount NOTIFY displaysChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY imageRouted)
    Q_PROPERTY(QString pinnedPath READ pinnedPath NOTIFY pinnedPathChanged)
    Q_PROPERTY(bool isPinned READ isPinned NOTIFY presentationModeChanged)

public:
    explicit DisplayManager(QObject* parent = nullptr);
    ~DisplayManager() override;

    // Display enumeration & stable identities
    QList<DisplayInfo> displays() const;
    QVariantList displaysVariant() const;
    int displayCount() const;
    DisplayInfo displayForId(const QString& id) const;
    DisplayInfo preferredPresentationDisplay() const;

    static QString computeDisplayId(const QScreen* screen);

    // Presentation lifecycle
    bool isPresentationActive() const;
    PresentationMode presentationMode() const;
    QString presentationModeString() const;
    QString activeDisplayId() const;
    QString activeDisplayName() const;
    QString currentPath() const;
    QString pinnedPath() const;
    bool isPinned() const;
    quint64 currentGeneration() const;

    Q_INVOKABLE bool openPresentation(const QString& displayId = QString(), PresentationMode mode = PresentationMode::Mirror);
    Q_INVOKABLE void closePresentation();
    Q_INVOKABLE void togglePresentation();
    Q_INVOKABLE void setPresentationMode(const QString& mode);
    void setMode(PresentationMode mode);

    // Image routing
    void updateImage(const QString& path, const QImage& image, quint64 generation);
    Q_INVOKABLE void pinCurrentImage();
    Q_INVOKABLE void pinImage(const QString& path, const QImage& image, quint64 generation);
    Q_INVOKABLE void unpin();
    Q_INVOKABLE void setComparisonImage(const QString& path, const QImage& image, quint64 generation);

    // Presentation Window access (for testing/canvas inspection)
    QQuickWindow* presentationWindow() const;

signals:
    void displaysChanged();
    void presentationActiveChanged(bool active);
    void presentationModeChanged(PresentationMode mode);
    void activeDisplayIdChanged(const QString& id);
    void pinnedPathChanged(const QString& path);
    void presentationClosed();
    void imageRouted(const QString& path, quint64 generation);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace glancelot

