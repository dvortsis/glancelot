#include "glancelot/display_manager.hpp"

#include <QGuiApplication>
#include <QScreen>
#include <QCryptographicHash>
#include <QColorSpace>
#include <QPointer>
#include <algorithm>
#include <QHash>
#include <QSet>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace glancelot {

static PresentationWindowFactory s_presentationWindowFactory = nullptr;
// 256 MiB of retained decoded pixels across current/pinned/comparison/previous; GPU textures are
// budgeted separately per canvas (see PresentationWindow).
static qint64 s_imageBudgetBytes = 256LL * 1024 * 1024;

void DisplayManager::setImageBudgetBytes(qint64 bytes) { s_imageBudgetBytes = std::max<qint64>(0, bytes); }
qint64 DisplayManager::imageBudgetBytes() { return s_imageBudgetBytes; }

namespace {
#ifdef Q_OS_WIN
// Windows adapter: map QScreen::name() (\\.\DISPLAYn) to the monitor device interface path
// (\\?\DISPLAY#DEL40F4#...) and return its hardware id segment ("DEL40F4"), which encodes the EDID
// manufacturer + product code. The per-port instance segment is deliberately not used.
// UNVERIFIED on real multi-monitor hardware.
QString platformMonitorCode(const QScreen* screen) {
    const std::wstring adapterName = screen->name().toStdWString();
    DISPLAY_DEVICEW adapter{}; adapter.cb = sizeof(adapter);
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &adapter, 0); ++i) {
        if (adapterName != adapter.DeviceName) { adapter = {}; adapter.cb = sizeof(adapter); continue; }
        DISPLAY_DEVICEW mon{}; mon.cb = sizeof(mon);
        if (!EnumDisplayDevicesW(adapter.DeviceName, 0, &mon, 0x00000001 /*EDD_GET_DEVICE_INTERFACE_NAME*/)) return {};
        const QStringList parts = QString::fromWCharArray(mon.DeviceID).split(QLatin1Char('#'));
        return parts.size() > 2 ? parts[1] : QString();
    }
    return {};
}
#else
QString platformMonitorCode(const QScreen*) { return {}; }
#endif
} // namespace

void DisplayManager::setPresentationWindowFactory(PresentationWindowFactory factory) {
    s_presentationWindowFactory = std::move(factory);
}

QVariantMap DisplayInfo::toVariantMap() const {
    QVariantMap map;
    map[QStringLiteral("index")] = index;
    map[QStringLiteral("id")] = id;
    map[QStringLiteral("name")] = name;
    map[QStringLiteral("manufacturer")] = manufacturer;
    map[QStringLiteral("model")] = model;
    map[QStringLiteral("serialNumber")] = serialNumber;
    map[QStringLiteral("x")] = geometry.x();
    map[QStringLiteral("y")] = geometry.y();
    map[QStringLiteral("width")] = geometry.width();
    map[QStringLiteral("height")] = geometry.height();
    map[QStringLiteral("devicePixelRatio")] = devicePixelRatio;
    map[QStringLiteral("refreshRate")] = refreshRate;
    map[QStringLiteral("colorDepth")] = colorDepth;
    map[QStringLiteral("colorSpace")] = colorSpace;
    map[QStringLiteral("isPrimary")] = isPrimary;
    map[QStringLiteral("isAvailable")] = isAvailable;
    return map;
}

struct DisplayManager::Private {
    DisplayManager* q;
    PresentationMode mode{PresentationMode::Mirror};
    bool active{false};
    QString activeId;
    QString activeName;

    QString currentPath;
    QImage currentImage;
    quint64 currentGeneration{0};

    QString pinnedPath;
    QImage pinnedImage;
    quint64 pinnedGeneration{0};

    QString comparisonPath;
    QImage comparisonImage;
    quint64 comparisonGeneration{0};
    quint64 comparisonGenerationHighWater{0};

    QString previousPath;
    QImage previousImage;

    QHash<QString, QString> fitModes;           // per display id
    QHash<QString, QVariantMap> viewports;      // bounded session state by display identity
    std::function<QString(const QString&)> outputBindingProvider;
    std::function<QString()> toggleBindingProvider;

    std::unique_ptr<IPresentationWindow> presentationWindow;
    QPointer<QScreen> presentationScreen;

    explicit Private(DisplayManager* owner) : q(owner) {}

    DisplayInfo screenToInfo(const QScreen* screen, int index) const {
        if (!screen) return {};
        DisplayInfo info;
        info.index = index;
        info.id = DisplayManager::computeDisplayId(screen);
        info.name = !screen->name().trimmed().isEmpty() ? screen->name().trimmed() : QStringLiteral("Display %1").arg(index + 1);
        info.manufacturer = screen->manufacturer().trimmed();
        info.model = screen->model().trimmed();
        info.serialNumber = screen->serialNumber().trimmed();
        info.geometry = screen->geometry();
        info.availableGeometry = screen->availableGeometry();
        info.physicalSizeMm = screen->physicalSize().toSize();
        info.devicePixelRatio = screen->devicePixelRatio();
        info.refreshRate = screen->refreshRate() > 0 ? screen->refreshRate() : 60.0;
        info.colorDepth = screen->depth() > 0 ? screen->depth() : 24;
        info.colorSpace = DisplayManager::colorSpaceName(screen);
        info.isPrimary = (screen == QGuiApplication::primaryScreen());
        info.isAvailable = true;
        return info;
    }

    QList<DisplayInfo> enumerateDisplays() const {
        QList<DisplayInfo> result;
        const auto screens = QGuiApplication::screens();
        result.reserve(screens.size());
        for (int i = 0; i < screens.size(); ++i) {
            result.append(screenToInfo(screens[i], i));
        }
        return result;
    }

    QScreen* findScreenById(const QString& id) const {
        if (id.isEmpty()) return nullptr;
        for (QScreen* s : QGuiApplication::screens()) {
            if (DisplayManager::computeDisplayId(s) == id) return s;
        }
        return nullptr;
    }

    QImage secondaryImage() const {
        if (!comparisonImage.isNull()) return comparisonImage;
        return !pinnedImage.isNull() ? pinnedImage : previousImage;
    }

    qint64 retainedBytes() const {
        QSet<qint64> keys; qint64 total = 0;
        for (const QImage* im : {&currentImage, &pinnedImage, &comparisonImage, &previousImage}) {
            if (im->isNull() || keys.contains(im->cacheKey())) continue;
            keys.insert(im->cacheKey()); total += im->sizeInBytes();
        }
        return total;
    }

    // Shed in order: previous, comparison, pinned. The current image is never dropped.
    void enforceBudget() {
        if (retainedBytes() <= s_imageBudgetBytes) return;
        previousImage = QImage(); previousPath.clear();
        if (retainedBytes() <= s_imageBudgetBytes) {
            applyImagesToWindow();
            return;
        }
        comparisonImage = QImage(); comparisonPath.clear(); comparisonGeneration = 0;
        if (retainedBytes() <= s_imageBudgetBytes) {
            applyImagesToWindow();
            return;
        }
        if (!pinnedImage.isNull()) {
            pinnedImage = QImage(); pinnedPath.clear(); pinnedGeneration = 0;
            emit q->pinnedPathChanged(pinnedPath);
            if (mode == PresentationMode::Pinned) q->setMode(PresentationMode::Mirror);
        }
        applyImagesToWindow();
    }

    void applyImagesToWindow() {
        if (!presentationWindow) return;

        const bool isComp = (mode == PresentationMode::Comparison);
        presentationWindow->setComparisonMode(isComp);

        if (mode == PresentationMode::Pinned) {
            presentationWindow->setPrimaryImage(pinnedImage);
            presentationWindow->setSecondaryImage(QImage());
        } else if (mode == PresentationMode::Comparison) {
            presentationWindow->setPrimaryImage(currentImage);
            presentationWindow->setSecondaryImage(secondaryImage());
        } else {
            // Mirror
            presentationWindow->setPrimaryImage(currentImage);
            presentationWindow->setSecondaryImage(QImage());
        }
    }
};

DisplayManager::DisplayManager(QObject* parent)
    : QObject(parent), d(std::make_unique<Private>(this)) {
    // Listen to screen topology changes
    if (qApp) {
        connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen*) {
            emit displaysChanged();
        });
        connect(qApp, &QGuiApplication::screenRemoved, this, [this](QScreen* screen) {
            if (d->presentationScreen == screen) {
                closePresentation();
            }
            emit displaysChanged();
        });
        connect(qApp, &QGuiApplication::primaryScreenChanged, this, [this](QScreen*) {
            emit displaysChanged();
        });
    }
}

DisplayManager::~DisplayManager() {
    closePresentation();
}

QList<DisplayInfo> DisplayManager::displays() const {
    return d->enumerateDisplays();
}

QVariantList DisplayManager::displaysVariant() const {
    const auto list = d->enumerateDisplays();
    QVariantList varList;
    varList.reserve(list.size());
    for (const auto& item : list) {
        varList.append(item.toVariantMap());
    }
    return varList;
}

int DisplayManager::displayCount() const {
    return QGuiApplication::screens().size();
}

DisplayInfo DisplayManager::displayForId(const QString& id) const {
    for (const auto& info : d->enumerateDisplays()) {
        if (info.id == id) return info;
    }
    return {};
}

DisplayInfo DisplayManager::preferredPresentationDisplay() const {
    const auto screens = QGuiApplication::screens();
    if (screens.isEmpty()) return {};

    // If multiple monitors, pick first non-primary screen
    if (screens.size() > 1) {
        const QScreen* primary = QGuiApplication::primaryScreen();
        for (int i = 0; i < screens.size(); ++i) {
            if (screens[i] != primary) {
                return d->screenToInfo(screens[i], i);
            }
        }
    }

    // Single screen fallback
    return d->screenToInfo(screens.first(), 0);
}

QStringList DisplayManager::assignDisplayIds(const QList<DisplayIdentitySource>& sources) {
    QStringList ids;
    QHash<QString, int> seen;
    for (const auto& s : sources) {
        const QString mfg = s.manufacturer.trimmed(), model = s.model.trimmed();
        const QString serial = s.serial.trimmed(), code = s.platformCode.trimmed();
        QString key;
        if (!serial.isEmpty() && (!mfg.isEmpty() || !model.isEmpty() || !code.isEmpty())) {
            key = QStringLiteral("serial:%1|%2|%3|%4").arg(mfg, model, code, serial);
        } else if (!mfg.isEmpty() || !model.isEmpty() || !code.isEmpty()) {
            key = QStringLiteral("model:%1|%2|%3|%4x%5").arg(mfg, model, code)
                      .arg(s.physicalSizeMm.width()).arg(s.physicalSizeMm.height());
        } else {
            // Last resort: no hardware naming at all. The Windows \\.\DISPLAYn name is NOT used
            // because it changes with topology; the ordinal separates identical panels.
            key = QStringLiteral("geom:%1x%2|%3x%4").arg(s.pixelSize.width()).arg(s.pixelSize.height())
                      .arg(s.physicalSizeMm.width()).arg(s.physicalSizeMm.height());
        }
        const int ordinal = seen[key]++;
        const QString identity = QStringLiteral("%1#%2").arg(key).arg(ordinal);
        ids.append(QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex().left(16)));
    }
    return ids;
}

static DisplayIdentitySource identitySourceFor(const QScreen* s) {
    DisplayIdentitySource src;
    src.manufacturer = s->manufacturer();
    src.model = s->model();
    src.serial = s->serialNumber();
    src.platformCode = platformMonitorCode(s);
    src.physicalSizeMm = s->physicalSize().toSize();
    src.pixelSize = s->geometry().size();
    return src;
}

QString DisplayManager::computeDisplayId(const QScreen* screen) {
    if (!screen) return QStringLiteral("none");
    const auto screens = QGuiApplication::screens();
    QList<DisplayIdentitySource> sources;
    int self = -1;
    for (int i = 0; i < screens.size(); ++i) {
        if (screens[i] == screen) self = i;
        sources.append(identitySourceFor(screens[i]));
    }
    if (self < 0) { sources.append(identitySourceFor(screen)); self = sources.size() - 1; }
    return assignDisplayIds(sources).value(self);
}

QString DisplayManager::colorSpaceName(const QScreen* screen) {
    // Qt 6.8 public QScreen exposes no display color space, and reading the monitor ICC profile
    // needs a platform adapter that is not implemented (COLOR-01). Report honestly instead of "sRGB".
    Q_UNUSED(screen);
    return QStringLiteral("Unknown");
}
bool DisplayManager::isPresentationActive() const {
    return d->active;
}

PresentationMode DisplayManager::presentationMode() const {
    return d->mode;
}

QString DisplayManager::presentationModeString() const {
    switch (d->mode) {
    case PresentationMode::Pinned: return QStringLiteral("pinned");
    case PresentationMode::Comparison: return QStringLiteral("comparison");
    case PresentationMode::Mirror:
    default:
        return QStringLiteral("mirror");
    }
}

QString DisplayManager::activeDisplayId() const {
    return d->activeId;
}

QString DisplayManager::activeDisplayName() const {
    return d->activeName;
}

QString DisplayManager::currentPath() const {
    return d->currentPath;
}

QString DisplayManager::pinnedPath() const {
    return d->pinnedPath;
}

bool DisplayManager::isPinned() const {
    return d->mode == PresentationMode::Pinned;
}

quint64 DisplayManager::currentGeneration() const {
    return d->currentGeneration;
}

bool DisplayManager::openPresentation(const QString& displayId, PresentationMode mode) {
    if (d->presentationWindow && !d->activeId.isEmpty()) {
        if (d->viewports.size()>=64 && !d->viewports.contains(d->activeId)) d->viewports.erase(d->viewports.begin());
        d->viewports[d->activeId]=d->presentationWindow->viewportState();
    }
    QScreen* targetScreen = nullptr;
    if (!displayId.isEmpty()) {
        targetScreen = d->findScreenById(displayId);
    }
    if (!targetScreen) {
        const auto preferred = preferredPresentationDisplay();
        if (!preferred.id.isEmpty()) {
            targetScreen = d->findScreenById(preferred.id);
        }
    }
    if (!targetScreen && !QGuiApplication::screens().isEmpty()) {
        targetScreen = QGuiApplication::screens().first();
    }
    if (!targetScreen) return false;

    d->presentationScreen = targetScreen;
    d->activeId = computeDisplayId(targetScreen);
    d->activeName = targetScreen->name();
    d->mode = mode;

    if (!d->presentationWindow) {
        if (s_presentationWindowFactory) {
            d->presentationWindow = s_presentationWindowFactory(this);
        }
    }

    if (d->presentationWindow) {
        d->presentationWindow->setFitMode(outputFitMode(d->activeId));
        d->presentationWindow->setScreen(targetScreen);
        d->presentationWindow->setGeometry(targetScreen->geometry());
        d->applyImagesToWindow();
        d->presentationWindow->restoreViewport(d->viewports.value(d->activeId));
        d->presentationWindow->show();
    }

    d->active = true;
    emit activeDisplayIdChanged(d->activeId);
    emit presentationModeChanged(d->mode);
    emit presentationActiveChanged(true);
    emit outputColorStatusChanged();
    return true;
}

void DisplayManager::closePresentation() {
    if (!d->active && !d->presentationWindow) return;
    if (d->presentationWindow && !d->activeId.isEmpty()) {
        if (d->viewports.size()>=64 && !d->viewports.contains(d->activeId)) d->viewports.erase(d->viewports.begin());
        d->viewports[d->activeId]=d->presentationWindow->viewportState();
    }

    d->active = false;
    d->activeId.clear();
    d->activeName.clear();
    d->presentationScreen = nullptr;
    // Closing releases the window (and its canvases/textures) and the non-essential pixel copies.
    d->previousImage = QImage(); d->previousPath.clear();
    d->comparisonImage = QImage(); d->comparisonPath.clear(); d->comparisonGeneration = 0;

    if (d->presentationWindow) {
        d->presentationWindow->setPrimaryImage(QImage());
        d->presentationWindow->setSecondaryImage(QImage());
        d->presentationWindow->hide();
        d->presentationWindow->deleteLater();
        d->presentationWindow.reset();
    }

    emit activeDisplayIdChanged(d->activeId);
    emit presentationActiveChanged(false);
    emit presentationClosed();
    emit outputColorStatusChanged();
}

void DisplayManager::togglePresentation() {
    if (isPresentationActive()) {
        closePresentation();
    } else {
        openPresentation();
    }
}

void DisplayManager::setPresentationMode(const QString& mode) {
    if (mode == QStringLiteral("pinned")) {
        setMode(PresentationMode::Pinned);
    } else if (mode == QStringLiteral("comparison")) {
        setMode(PresentationMode::Comparison);
    } else {
        setMode(PresentationMode::Mirror);
    }
}

void DisplayManager::setMode(PresentationMode mode) {
    if (d->mode == mode) return;
    d->mode = mode;
    d->applyImagesToWindow();
    emit presentationModeChanged(d->mode);
}

void DisplayManager::updateImage(const QString& path, const QImage& image, quint64 generation) {
    if (generation < d->currentGeneration) return;

    if (path != d->currentPath && !d->currentImage.isNull()) {
        d->previousPath = d->currentPath;
        d->previousImage = d->currentImage;
    }
    d->currentPath = path;
    d->currentImage = image;
    d->currentGeneration = generation;
    d->comparisonGenerationHighWater = (std::max)(d->comparisonGenerationHighWater, generation);
    d->enforceBudget();

    if (d->mode == PresentationMode::Mirror || d->mode == PresentationMode::Comparison) {
        d->applyImagesToWindow();
    }
    emit imageRouted(path, generation);
}

void DisplayManager::pinCurrentImage() {
    pinImage(d->currentPath, d->currentImage, d->currentGeneration);
}

void DisplayManager::pinImage(const QString& path, const QImage& image, quint64 generation) {
    if (generation < d->pinnedGeneration) return;   // stale pin request
    d->pinnedPath = path;
    d->pinnedImage = image;
    d->pinnedGeneration = generation;
    d->enforceBudget();
    if (!image.isNull() && d->pinnedImage.isNull()) return;   // shed by budget
    if (d->mode == PresentationMode::Pinned) d->applyImagesToWindow();
    setMode(PresentationMode::Pinned);
    emit pinnedPathChanged(path);
}

void DisplayManager::unpin() {
    d->pinnedPath.clear();
    d->pinnedImage = QImage();
    d->pinnedGeneration = 0;
    setMode(PresentationMode::Mirror);
    emit pinnedPathChanged(d->pinnedPath);
}

void DisplayManager::setComparisonImage(const QString& path, const QImage& image, quint64 generation) {
    if (generation < d->comparisonGenerationHighWater) return;   // stale comparison request
    d->comparisonGenerationHighWater = (std::max)(d->comparisonGenerationHighWater, generation);
    d->comparisonPath = path;
    d->comparisonImage = image;
    d->comparisonGeneration = generation;
    d->enforceBudget();
    d->applyImagesToWindow();
    setMode(PresentationMode::Comparison);
}

void DisplayManager::compareWithPinnedOrPrevious() {
    d->comparisonImage = QImage(); d->comparisonPath.clear(); d->comparisonGeneration = 0;
    d->comparisonGenerationHighWater = (std::max)(d->comparisonGenerationHighWater, d->currentGeneration);
    setMode(PresentationMode::Comparison);
    d->applyImagesToWindow();
}

static bool isValidFitMode(const QString& mode) {
    return (mode == QLatin1String("shrink") || mode == QLatin1String("fit") ||
            mode == QLatin1String("fill") || mode == QLatin1String("width") ||
            mode == QLatin1String("height") || mode==QLatin1String("actual"));
}

void DisplayManager::setOutputFitMode(const QString& mode) {
    if (!isValidFitMode(mode)) return;
    if (!d->active || d->activeId.isEmpty()) return;
    if (d->fitModes.size()>=64 && !d->fitModes.contains(d->activeId)) d->fitModes.erase(d->fitModes.begin());
    d->fitModes[d->activeId] = mode;
    if (d->presentationWindow) d->presentationWindow->setFitMode(mode);
}
void DisplayManager::outputViewportCommand(const QString& command) {
    if (!d->active || !d->presentationWindow) return;
    d->presentationWindow->viewportCommand(command);
}
QVariantMap DisplayManager::outputViewportState(const QString& displayId) const {
    if (d->presentationWindow && (displayId.isEmpty() || displayId==d->activeId)) return d->presentationWindow->viewportState();
    return d->viewports.value(displayId);
}
void DisplayManager::setOutputBindingProvider(std::function<QString(const QString&)> provider) {
    d->outputBindingProvider=std::move(provider);
}
QString DisplayManager::outputBinding(const QString& command) const {
    return d->outputBindingProvider ? d->outputBindingProvider(command) : QString();
}

QString DisplayManager::outputFitMode(const QString& displayId) const {
    if (d->presentationWindow && (displayId.isEmpty() || displayId==d->activeId)) {
        const auto fit=d->presentationWindow->viewportState().value("primary").toMap().value("fit").toString();
        return fit.isEmpty()?QStringLiteral("custom"):fit;
    }
    return d->fitModes.value(displayId.isEmpty() ? d->activeId : displayId, QStringLiteral("shrink"));
}
QString DisplayManager::outputColorStatus() const {
    return d->presentationWindow?d->presentationWindow->outputColorStatus():QStringLiteral("Presentation closed");
}
qint64 DisplayManager::outputColorResourceBytes() const {
    return d->presentationWindow?d->presentationWindow->colorResourceBytes():0;
}

qint64 DisplayManager::retainedImageBytes() const { return d->retainedBytes(); }

void DisplayManager::releaseCachedImages() {
    d->previousImage = QImage(); d->previousPath.clear();
    d->comparisonImage = QImage(); d->comparisonPath.clear(); d->comparisonGeneration = 0;
    d->applyImagesToWindow();
    if (d->presentationWindow && d->mode != PresentationMode::Comparison) {
        d->presentationWindow->setSecondaryImage(QImage());
    }
}

void DisplayManager::setToggleBindingProvider(std::function<QString()> provider) {
    d->toggleBindingProvider = std::move(provider);
}

QString DisplayManager::toggleBinding() const {
    return d->toggleBindingProvider ? d->toggleBindingProvider() : QStringLiteral("F8");
}

QQuickWindow* DisplayManager::presentationWindow() const {
    return d->presentationWindow ? d->presentationWindow->quickWindow() : nullptr;
}

} // namespace glancelot
