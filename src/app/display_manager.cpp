#include "glancelot/display_manager.hpp"
#include "presentation_window.hpp"

#include <QGuiApplication>
#include <QScreen>
#include <QCryptographicHash>
#include <QColorSpace>
#include <QPointer>
#include <algorithm>

namespace glancelot {

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

    QPointer<PresentationWindow> presentationWindow;
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
        info.colorSpace = QStringLiteral("sRGB");
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

    void applyImagesToWindow() {
        if (!presentationWindow) return;

        const bool isComp = (mode == PresentationMode::Comparison);
        presentationWindow->setComparisonMode(isComp);

        if (mode == PresentationMode::Pinned) {
            presentationWindow->setPrimaryImage(pinnedImage);
        } else if (mode == PresentationMode::Comparison) {
            presentationWindow->setPrimaryImage(currentImage);
            presentationWindow->setSecondaryImage(!comparisonImage.isNull() ? comparisonImage : pinnedImage);
        } else {
            // Mirror
            presentationWindow->setPrimaryImage(currentImage);
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

QString DisplayManager::computeDisplayId(const QScreen* screen) {
    if (!screen) return QStringLiteral("none");

    const QString mfg = screen->manufacturer().trimmed();
    const QString model = screen->model().trimmed();
    const QString serial = screen->serialNumber().trimmed();
    const QString name = screen->name().trimmed();
    const QSize physical = screen->physicalSize().toSize();
    const QRect geom = screen->geometry();

    if (!mfg.isEmpty() && (!model.isEmpty() || !serial.isEmpty())) {
        const QString identity = QStringLiteral("hw:%1_%2_%3_%4x%5")
            .arg(mfg, model, serial)
            .arg(physical.width()).arg(physical.height());
        return QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
    }

    const QString fallback = QStringLiteral("heuristic:%1_%2x%3_%4x%5")
        .arg(name)
        .arg(geom.width()).arg(geom.height())
        .arg(physical.width()).arg(physical.height());
    return QString::fromLatin1(QCryptographicHash::hash(fallback.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
}

bool DisplayManager::isPresentationActive() const {
    return d->active && d->presentationWindow;
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
        d->presentationWindow = new PresentationWindow();
        connect(d->presentationWindow.data(), &PresentationWindow::escapePressed, this, &DisplayManager::closePresentation);
        connect(d->presentationWindow.data(), &PresentationWindow::closedByUser, this, &DisplayManager::closePresentation);
    }

    d->presentationWindow->setScreen(targetScreen);
    d->presentationWindow->setGeometry(targetScreen->geometry());

    d->applyImagesToWindow();
    d->presentationWindow->show();

    d->active = true;
    emit activeDisplayIdChanged(d->activeId);
    emit presentationModeChanged(d->mode);
    emit presentationActiveChanged(true);
    return true;
}

void DisplayManager::closePresentation() {
    if (!d->active && !d->presentationWindow) return;

    d->active = false;
    d->activeId.clear();
    d->activeName.clear();
    d->presentationScreen = nullptr;

    if (d->presentationWindow) {
        d->presentationWindow->hide();
        d->presentationWindow->deleteLater();
        d->presentationWindow = nullptr;
    }

    emit activeDisplayIdChanged(d->activeId);
    emit presentationActiveChanged(false);
    emit presentationClosed();
}

void DisplayManager::togglePresentation() {
    if (isPresentationActive()) {
        closePresentation();
    } else {
        openPresentation();
    }
}

void DisplayManager::setPresentationMode(const QString& modeStr) {
    if (modeStr == QStringLiteral("pinned")) {
        setMode(PresentationMode::Pinned);
    } else if (modeStr == QStringLiteral("comparison")) {
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
    if (generation < d->currentGeneration) return; // Discard stale async decode

    d->currentPath = path;
    d->currentImage = image;
    d->currentGeneration = generation;

    if (isPresentationActive()) {
        if (d->mode == PresentationMode::Mirror) {
            d->presentationWindow->setPrimaryImage(image);
        } else if (d->mode == PresentationMode::Comparison) {
            d->presentationWindow->setPrimaryImage(image);
        }
    }
    emit imageRouted(path, generation);
}

void DisplayManager::pinCurrentImage() {
    pinImage(d->currentPath, d->currentImage, d->currentGeneration);
}

void DisplayManager::pinImage(const QString& path, const QImage& image, quint64 generation) {
    d->pinnedPath = path;
    d->pinnedImage = image;
    d->pinnedGeneration = generation;
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
    d->comparisonPath = path;
    d->comparisonImage = image;
    d->comparisonGeneration = generation;
    setMode(PresentationMode::Comparison);
}

QQuickWindow* DisplayManager::presentationWindow() const {
    return d->presentationWindow.data();
}

} // namespace glancelot

