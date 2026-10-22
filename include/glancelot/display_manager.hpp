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
#include <functional>

class QScreen;
class QQuickWindow;

namespace glancelot {

class DisplayManager;

enum class PresentationMode {
    Mirror,      // Follows current controller photo
    Pinned,      // Keeps a specific pinned reference photo
    Comparison   // Comparison display (side-by-side with reference)
};

// Raw facts used to derive a stable display id. platformCode is a platform adapter value
// (Windows: hardware id segment of the monitor device interface path, e.g. "DEL40F4").
struct DisplayIdentitySource {
    QString manufacturer, model, serial, platformCode;
    QSize physicalSizeMm;
    QSize pixelSize;
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
    QString colorSpace{"Unknown"};   // "Unknown" when the platform reports none
    bool isPrimary{false};
    bool isAvailable{true};

    QVariantMap toVariantMap() const;
};

class IPresentationWindow {
public:
    virtual ~IPresentationWindow() = default;
    virtual void setScreen(QScreen* screen) = 0;
    virtual void setGeometry(const QRect& geom) = 0;
    virtual void show() = 0;
    virtual void hide() = 0;
    virtual void deleteLater() = 0;
    virtual void setComparisonMode(bool comparison) = 0;
    virtual void setPrimaryImage(const QImage& image) = 0;
    virtual void setSecondaryImage(const QImage& image) = 0;
    virtual void setFitMode(const QString& mode) = 0;
    virtual QVariantMap viewportState() const = 0;
    virtual void restoreViewport(const QVariantMap& state) = 0;
    virtual void viewportCommand(const QString& command) = 0;
    virtual QString outputColorStatus() const = 0;
    virtual qint64 colorResourceBytes() const = 0;
    virtual QQuickWindow* quickWindow() = 0;
};

using PresentationWindowFactory = std::function<std::unique_ptr<IPresentationWindow>(DisplayManager* owner)>;

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
    Q_PROPERTY(QString outputColorStatus READ outputColorStatus NOTIFY outputColorStatusChanged)

public:
    explicit DisplayManager(QObject* parent = nullptr);
    ~DisplayManager() override;

    // Presentation window factory hook
    static void setPresentationWindowFactory(PresentationWindowFactory factory);

    // Display enumeration & stable identities
    QList<DisplayInfo> displays() const;
    QVariantList displaysVariant() const;
    int displayCount() const;
    DisplayInfo displayForId(const QString& id) const;
    DisplayInfo preferredPresentationDisplay() const;

    static QString computeDisplayId(const QScreen* screen);
    // Pure id derivation for a whole topology. Identical monitors (same key) receive a
    // disambiguating ordinal in list order; serial-bearing monitors are order independent.
    static QStringList assignDisplayIds(const QList<DisplayIdentitySource>& sources);
    static QString colorSpaceName(const QScreen* screen);

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

    // Image routing. Every producer uses the controller's monotonic selection-generation
    // space (including pin/comparison requests), never an independent per-producer counter.
    void updateImage(const QString& path, const QImage& image, quint64 generation);
    // MONITOR-CUSTOM-01: validated coordinator policy. One bounded latest-frame
    // slot for independent presentation cadence; no independent decode/scan.
    void setRoutingPolicy(const QVariantMap& policy);
    quint64 outputGeneration() const;
    QString routingStatus() const;
    Q_INVOKABLE void pinCurrentImage();
    Q_INVOKABLE void pinImage(const QString& path, const QImage& image, quint64 generation);
    Q_INVOKABLE void unpin();
    Q_INVOKABLE void setComparisonImage(const QString& path, const QImage& image, quint64 generation);
    // Comparison against the pinned image, else the previously shown image.
    Q_INVOKABLE void compareWithPinnedOrPrevious();

    // Per-output fit mode, remembered by display id and restored when that output reopens.
    Q_INVOKABLE void setOutputFitMode(const QString& mode);
    QString outputFitMode(const QString& displayId = QString()) const;
    Q_INVOKABLE void outputViewportCommand(const QString& command);
    QVariantMap outputViewportState(const QString& displayId = QString()) const;
    QString outputColorStatus() const;
    qint64 outputColorResourceBytes() const;

    // Aggregate budget for decoded images retained by the presentation (shared QImages count once).
    static void setImageBudgetBytes(qint64 bytes);
    static qint64 imageBudgetBytes();
    qint64 retainedImageBytes() const;
    // Drops non-essential retained pixels (previous/comparison); hook for low-memory signals.
    Q_INVOKABLE void releaseCachedImages();

    // Window-local key handling consults the user-rebindable toggle binding through this provider.
    void setToggleBindingProvider(std::function<QString()> provider);
    QString toggleBinding() const;
    void setOutputBindingProvider(std::function<QString(const QString&)> provider);
    QString outputBinding(const QString& command) const;

    // Presentation Window access (for testing/canvas inspection)
    QQuickWindow* presentationWindow() const;

signals:
    void routingStatusChanged();
    void currentImageRequested();
    void displaysChanged();
    void presentationActiveChanged(bool active);
    void presentationModeChanged(PresentationMode mode);
    void activeDisplayIdChanged(const QString& id);
    void pinnedPathChanged(const QString& path);
    void presentationClosed();
    void imageRouted(const QString& path, quint64 generation);
    void outputColorStatusChanged();

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace glancelot
